/* SPDX-License-Identifier: MIT */
/* fs/fat16.c - FAT16 volume builder (in RAM) for the QEAFRESCUE LUN. */

#include "fat16.h"
#include <string.h>
#include "core/log.h"

/* One timestamp for the whole volume: the rescue disk is rebuilt at every
 * boot, so a clock here would only make two boots differ for no reason. */
#define DOS_TIME 0x0000
#define DOS_DATE (((2026 - 1980) << 9) | (11 << 5) | 5)

static uint32_t cluster_sector(const struct fat16_volume *vol, uint32_t cluster) {
    return vol->data_start + (cluster - 2) * vol->sectors_per_cluster;
}

/* Writes one FAT entry into every FAT, because a host may read either one. */
static void fat_set(struct fat16_volume *vol, uint32_t cluster, uint16_t value) {
    uint32_t off = cluster * 2;
    for (uint8_t f = 0; f < vol->num_fats; f++) {
        uint8_t *fat = vol->data + (size_t)(vol->fat_start + f * vol->fat_size) *
                         vol->bytes_per_sector;
        fat[off] = (uint8_t)(value & 0xFF);
        fat[off + 1] = (uint8_t)(value >> 8);
    }
}

static uint8_t *root_entry(struct fat16_volume *vol, uint32_t index) {
    return vol->data + (size_t)vol->root_dir_start * vol->bytes_per_sector +
           (size_t)index * FAT16_ENTRY_SIZE;
}

static uint16_t lfn_checksum(const uint8_t *short_name) {
    uint16_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint16_t)(((sum & 1u) ? 0x8000u : 0u) + (sum >> 1) + short_name[i]);
    return sum;
}

/* 13 UTF-16LE slots per record: five at 1, six at 14, two at 28. Past the
 * name comes a NUL, then 0xFFFF to the end of the record. */
static void lfn_put_chars(uint8_t *e, const char *name, int len, int first) {
    static const int slot_off[3] = {1, 14, 28};
    static const int slot_len[3] = {5, 6, 2};
    int slot = 0, n = 0;

    for (int i = 0; i < 13; i++) {
        int ci = first + i;
        uint16_t u;
        uint8_t *p;
        if (ci < len) u = (uint16_t)(unsigned char)name[ci];
        else if (ci == len) u = 0x0000;
        else u = 0xFFFF;
        if (n == slot_len[slot]) { slot++; n = 0; }
        p = e + slot_off[slot] + n * 2;
        p[0] = (uint8_t)(u & 0xFF);
        p[1] = (uint8_t)(u >> 8);
        n++;
    }
}

static int char_ok(unsigned char c) {
    if (c < 0x20 || c > 0x7E) return 0;
    if (strchr("\"*/\\[]:;|=,+?<>", (int)c)) return 0;
    return 1;
}

static int name_ok(const char *name) {
    size_t n;
    if (!name || !name[0]) return 0;
    n = strlen(name);
    if (n > 20) return 0;                    /* this volume's names are <= 9 */
    if (name[0] == ' ' || name[n - 1] == ' ') return 0;
    for (size_t i = 0; i < n; i++)
        if (!char_ok((unsigned char)name[i])) return 0;
    return 1;
}

static char upper_of(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* Splits NAME into the base and the extension an 8.3 entry would carry. */
static void split_name(const char *name, char *base, int *base_len,
                       char *ext, int *ext_len) {
    const char *dot = strrchr(name, '.');
    size_t n = dot ? (size_t)(dot - name) : strlen(name);

    for (size_t i = 0; i < n; i++) base[i] = upper_of(name[i]);
    *base_len = (int)n;
    if (!dot) { *ext_len = 0; return; }
    n = strlen(dot + 1);
    if (n > 3) n = 3;
    for (size_t i = 0; i < n; i++) ext[i] = upper_of(dot[1 + i]);
    *ext_len = (int)n;
}

static int short_taken(struct fat16_volume *vol, const uint8_t *short_name) {
    for (uint32_t i = 0; i < FAT16_ROOT_ENTRIES; i++) {
        const uint8_t *e = root_entry(vol, i);
        if (e[0] == 0x00) break;
        if (e[0] == 0xE5) continue;
        if (e[11] == FAT16_ATTR_LFN || e[11] == 0x00 || e[11] == FAT16_ATTR_VOLUME)
            continue;
        if (memcmp(e, short_name, 11) == 0) return 1;
    }
    return 0;
}

/* Fills OUT with the padded 11-byte short name: the stem itself when it fits
 * eight characters and is free, otherwise six characters plus ~n. Returns 0
 * when no short name is left, which means the root directory is full. */
static int make_short_name(struct fat16_volume *vol, const char *base,
                           int base_len, const char *ext, int ext_len,
                           uint8_t *out) {
    int take = base_len <= 8 ? base_len : 6;

    for (int i = 0; i < 11; i++) out[i] = ' ';
    memcpy(out, base, (size_t)take);
    for (int i = 0; i < ext_len; i++) out[8 + i] = (uint8_t)ext[i];
    if (base_len <= 8 && !short_taken(vol, out)) return 1;

    /* The stem does not fit, or the plain stem is already taken: BASE~n,
     * with at most six characters left for the stem. */
    take = base_len < 6 ? base_len : 6;
    for (uint32_t n = 1; n <= 999999u; n++) {
        uint32_t v = n;
        int d = 0;
        for (int i = 0; i < 11; i++) out[i] = ' ';
        memcpy(out, base, (size_t)take);
        while (v) { v /= 10; d++; }
        if (take + 1 + d > 8) return 0;
        v = n;
        out[take] = '~';
        for (int i = d; i > 0; i--) { out[take + i] = (char)('0' + v % 10); v /= 10; }
        for (int i = 0; i < ext_len; i++) out[8 + i] = (uint8_t)ext[i];
        if (!short_taken(vol, out)) return 1;
    }
    return 0;
}

static void put_entry(struct fat16_volume *vol, const uint8_t *name11,
                      uint8_t attr, uint32_t cluster, uint32_t size) {
    uint8_t *e = root_entry(vol, vol->root_entries_used++);
    memcpy(e, name11, 11);
    e[11] = attr;
    e[13] = 0;                              /* creation time tenths */
    e[14] = (uint8_t)(DOS_TIME & 0xFF); e[15] = (uint8_t)(DOS_TIME >> 8);
    e[16] = (uint8_t)(DOS_DATE & 0xFF); e[17] = (uint8_t)(DOS_DATE >> 8);
    e[18] = (uint8_t)(DOS_DATE & 0xFF); e[19] = (uint8_t)(DOS_DATE >> 8);
    e[20] = 0; e[21] = 0;                   /* no high cluster in FAT16 */
    e[22] = (uint8_t)(DOS_TIME & 0xFF); e[23] = (uint8_t)(DOS_TIME >> 8);
    e[24] = (uint8_t)(DOS_DATE & 0xFF); e[25] = (uint8_t)(DOS_DATE >> 8);
    e[26] = (uint8_t)(cluster & 0xFF); e[27] = (uint8_t)(cluster >> 8);
    e[28] = (uint8_t)(size & 0xFF);
    e[29] = (uint8_t)((size >> 8) & 0xFF);
    e[30] = (uint8_t)((size >> 16) & 0xFF);
    e[31] = (uint8_t)((size >> 24) & 0xFF);
}

int fat16_init(struct fat16_volume *vol, uint8_t *data, size_t size) {
    uint8_t label11[11];
    uint8_t *bs;
    uint32_t total_sectors;

    if (!vol || !data || size < FAT16_VOLUME_SIZE) return -1;
    memset(vol, 0, sizeof(*vol));
    vol->data = data;
    vol->size = size;

    /* Geometry: 32 MiB FAT16, 4 KiB clusters, 63 sectors/track, 255 heads */
    vol->bytes_per_sector = FAT16_SECTOR_SIZE;
    vol->sectors_per_cluster = 8;  /* 4 KiB cluster */
    vol->reserved_sectors = 32;
    vol->num_fats = 2;
    vol->fat_size = 256;  /* sectors per FAT */
    vol->root_dir_sectors = FAT16_ROOT_ENTRIES * FAT16_ENTRY_SIZE / FAT16_SECTOR_SIZE;
    vol->fat_start = vol->reserved_sectors;
    vol->root_dir_start = vol->fat_start + vol->num_fats * vol->fat_size;
    vol->data_start = vol->root_dir_start + vol->root_dir_sectors;
    vol->total_clusters = (size / vol->bytes_per_sector - vol->data_start) /
                          vol->sectors_per_cluster;
    vol->next_cluster = 2;                  /* clusters 0 and 1 are the FAT's own */
    vol->root_entries_used = 0;

    /* Build boot sector (BPB) */
    bs = vol->data;
    memset(bs, 0, vol->bytes_per_sector);
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;         /* jump boot */
    memcpy(bs + 3, "QEAFOS  ", 8);                     /* OEM name */
    *(uint16_t *)(bs + 11) = FAT16_SECTOR_SIZE;        /* bytes per sector */
    bs[13] = vol->sectors_per_cluster;
    *(uint16_t *)(bs + 14) = vol->reserved_sectors;
    bs[16] = vol->num_fats;
    *(uint16_t *)(bs + 17) = FAT16_ROOT_ENTRIES;
    *(uint16_t *)(bs + 19) = 0;                        /* use the 32-bit count */
    bs[21] = 0xF8;                                     /* media descriptor */
    *(uint16_t *)(bs + 22) = vol->fat_size;
    *(uint16_t *)(bs + 24) = 63;                       /* sectors per track */
    *(uint16_t *)(bs + 26) = 255;                      /* heads */
    *(uint32_t *)(bs + 28) = 0;                        /* hidden sectors */
    total_sectors = (uint32_t)(size / FAT16_SECTOR_SIZE);
    *(uint32_t *)(bs + 32) = total_sectors;
    bs[36] = 0x80;                                     /* physical drive number */
    bs[38] = 0x29;                                     /* extended boot signature */
    *(uint32_t *)(bs + 39) = 0x12345678;               /* volume serial */
    memset(label11, ' ', 11);
    memcpy(label11, FAT16_LABEL, strlen(FAT16_LABEL));
    memcpy(bs + 43, label11, 11);
    memcpy(bs + 54, "FAT16   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;

    /* Clear FATs and root directory */
    memset(vol->data + (size_t)vol->fat_start * vol->bytes_per_sector, 0,
           (size_t)vol->num_fats * vol->fat_size * vol->bytes_per_sector);
    memset(vol->data + (size_t)vol->root_dir_start * vol->bytes_per_sector, 0,
           (size_t)vol->root_dir_sectors * vol->bytes_per_sector);

    /* FAT[0] carries the media descriptor in its high byte and FAT[1] is an
     * end-of-chain marker. They are 16-bit entries, in every FAT copy - the
     * old builder wrote three loose bytes, which left FAT[1] a reserved
     * value a host is free to read as a corrupt disk. */
    fat_set(vol, 0, 0xFFF8);
    fat_set(vol, 1, 0xFFFF);

    /* The label is a root entry too, not only a BPB field: the reader on the
     * phone walks directory entries and would call an unlabelled disk lost. */
    put_entry(vol, label11, FAT16_ATTR_VOLUME, 0, 0);

    umos_logf("fat16: volume %u bytes, %u clusters, %u root entries\n",
              (unsigned)size, (unsigned)vol->total_clusters,
              (unsigned)FAT16_ROOT_ENTRIES);
    return 0;
}

int fat16_mkdir(struct fat16_volume *vol, const char *name) {
    char base[21], ext[4];
    uint8_t short_name[11];
    uint8_t *dir;
    int base_len, ext_len, len, records;
    uint32_t cluster;
    uint16_t sum;

    if (!vol || !name_ok(name)) return -1;
    if (vol->next_cluster - 2 >= vol->total_clusters) return -1;
    len = (int)strlen(name);
    /* One long-name record per 13 characters, always: the short entry cannot
     * carry the case the storage table asks for ("Music", not "MUSIC"). */
    records = (len + 1 + 12) / 13;
    if (vol->root_entries_used + (uint32_t)records + 1 > FAT16_ROOT_ENTRIES)
        return -1;

    split_name(name, base, &base_len, ext, &ext_len);
    if (!make_short_name(vol, base, base_len, ext, ext_len, short_name))
        return -1;

    cluster = vol->next_cluster++;
    fat_set(vol, cluster, FAT16_EOF);

    /* The new directory holds "." and ".." and nothing else yet. Its cluster
     * is wiped first: the RAM behind the LUN is reused between boots. */
    dir = vol->data + (size_t)cluster_sector(vol, cluster) * vol->bytes_per_sector;
    memset(dir, 0, (size_t)vol->sectors_per_cluster * vol->bytes_per_sector);
    for (int i = 0; i < 2; i++) {
        uint8_t *e = dir + i * FAT16_ENTRY_SIZE;
        uint32_t c = i ? 0 : cluster;       /* ".." points at the root: no cluster */
        memset(e, ' ', 11);
        e[0] = '.';
        if (i) e[1] = '.';
        e[11] = FAT16_ATTR_DIR;
        e[16] = (uint8_t)(DOS_DATE & 0xFF); e[17] = (uint8_t)(DOS_DATE >> 8);
        e[24] = (uint8_t)(DOS_DATE & 0xFF); e[25] = (uint8_t)(DOS_DATE >> 8);
        e[26] = (uint8_t)(c & 0xFF); e[27] = (uint8_t)(c >> 8);
    }

    sum = lfn_checksum(short_name);
    for (int r = records; r >= 1; r--) {
        uint8_t *e = root_entry(vol, vol->root_entries_used++);
        memset(e, 0, FAT16_ENTRY_SIZE);
        e[0] = (uint8_t)((r == records ? FAT16_LFN_LAST : 0) | r);
        e[11] = FAT16_ATTR_LFN;
        e[13] = (uint8_t)(sum & 0xFF);
        lfn_put_chars(e, name, len, (r - 1) * 13);
    }
    put_entry(vol, short_name, FAT16_ATTR_DIR, cluster, 0);

    umos_logf("fat16: mkdir %s -> cluster %u, short %c%c%c%c%c%c%c%c%c%c%c, "
              "%d long record(s)\n", name, (unsigned)cluster,
              short_name[0], short_name[1], short_name[2], short_name[3],
              short_name[4], short_name[5], short_name[6], short_name[7],
              short_name[8], short_name[9], short_name[10], records);
    return (int)cluster;
}

int fat16_mkdir_in(struct fat16_volume *vol, uint32_t parent_cluster, const char *name)
{
    char base[21], ext[4];
    uint8_t short_name[11];
    uint8_t *dir;
    uint8_t *pdir;
    int base_len, ext_len, len, records;
    uint32_t cluster;
    uint16_t sum;
    int slot;

    if (!vol || !name_ok(name) || parent_cluster < 2) return -1;
    if (vol->next_cluster - 2 >= vol->total_clusters) return -1;
    len = (int)strlen(name);
    records = (len + 1 + 12) / 13;

    split_name(name, base, &base_len, ext, &ext_len);
    if (!make_short_name(vol, base, base_len, ext, ext_len, short_name))
        return -1;

    cluster = vol->next_cluster++;
    fat_set(vol, cluster, FAT16_EOF);

    dir = vol->data + (size_t)cluster_sector(vol, cluster) * vol->bytes_per_sector;
    memset(dir, 0, (size_t)vol->sectors_per_cluster * vol->bytes_per_sector);
    for (int i = 0; i < 2; i++) {
        uint8_t *e = dir + i * FAT16_ENTRY_SIZE;
        uint32_t c = i ? parent_cluster : cluster;
        memset(e, ' ', 11);
        e[0] = '.';
        if (i) e[1] = '.';
        e[11] = FAT16_ATTR_DIR;
        e[16] = (uint8_t)(DOS_DATE & 0xFF); e[17] = (uint8_t)(DOS_DATE >> 8);
        e[24] = (uint8_t)(DOS_DATE & 0xFF); e[25] = (uint8_t)(DOS_DATE >> 8);
        e[26] = (uint8_t)(c & 0xFF); e[27] = (uint8_t)(c >> 8);
    }

    pdir = vol->data + (size_t)cluster_sector(vol, parent_cluster) * vol->bytes_per_sector;
    slot = 2;
    while (slot * FAT16_ENTRY_SIZE < (int)(vol->sectors_per_cluster * vol->bytes_per_sector) &&
           pdir[slot * FAT16_ENTRY_SIZE] != 0x00 &&
           pdir[slot * FAT16_ENTRY_SIZE] != 0xE5)
        slot++;
    sum = lfn_checksum(short_name);
    for (int r = records; r >= 1; r--) {
        uint8_t *e = pdir + slot * FAT16_ENTRY_SIZE;
        memset(e, 0, FAT16_ENTRY_SIZE);
        e[0] = (uint8_t)((r == records ? FAT16_LFN_LAST : 0) | r);
        e[11] = FAT16_ATTR_LFN;
        e[13] = (uint8_t)(sum & 0xFF);
        lfn_put_chars(e, name, len, (r - 1) * 13);
        slot++;
    }
    {
        uint8_t *e = pdir + slot * FAT16_ENTRY_SIZE;
        memset(e, 0, FAT16_ENTRY_SIZE);
        memcpy(e, short_name, 11);
        e[11] = FAT16_ATTR_DIR;
        e[16] = (uint8_t)(DOS_DATE & 0xFF); e[17] = (uint8_t)(DOS_DATE >> 8);
        e[24] = (uint8_t)(DOS_DATE & 0xFF); e[25] = (uint8_t)(DOS_DATE >> 8);
        e[26] = (uint8_t)(cluster & 0xFF); e[27] = (uint8_t)(cluster >> 8);
    }
    umos_logf("fat16: mkdir_in %s in %u -> cluster %u\n", name, (unsigned)parent_cluster, (unsigned)cluster);
    return (int)cluster;
}

/* ---- small root files (sync protocol) ------------------------------------- */

static uint8_t *find_short_entry(struct fat16_volume *vol,
                                 const uint8_t *short_name) {
    /* Scan the whole root region, not just the entries this builder wrote: a
     * host (Windows) appends QEAF.CMD past root_entries_used, and that is the
     * entry the rescue loop must read. Stop at the 0x00 end marker, skip 0xE5
     * deleted slots; root_entries_used stays the builder's own allocation
     * high-water mark and is never trusted as the host-visible boundary. */
    for (uint32_t i = 0; i < FAT16_ROOT_ENTRIES; i++) {
        uint8_t *e = root_entry(vol, i);
        if (e[0] == 0x00) break;
        if (e[0] == 0xE5) continue;
        if (e[11] == FAT16_ATTR_LFN || e[11] == FAT16_ATTR_VOLUME) continue;
        if (e[11] & FAT16_ATTR_DIR) continue;
        if (memcmp(e, short_name, 11) == 0) return e;
    }
    return NULL;
}

static uint32_t file_capacity(struct fat16_volume *vol, uint32_t first) {
    uint32_t n = 0, cur = first;
    uint16_t fat_val;
    while (cur >= 2 && cur < FAT16_EOF - 8 && n < 64) {
        uint8_t *f = vol->data + (size_t)vol->fat_start * vol->bytes_per_sector;
        memcpy(&fat_val, f + cur * 2, 2);
        n++;
        cur = fat_val;
    }
    return n * vol->sectors_per_cluster * vol->bytes_per_sector;
}

int fat16_write_file(struct fat16_volume *vol, const char *name,
                     const uint8_t *data, size_t len) {
    char base[21], ext[4];
    uint8_t short_name[11];
    uint8_t exact[11];
    int base_len, ext_len, name_len, records;
    uint32_t cluster_bytes, need, first, prev, cur, written;
    uint8_t *entry;
    uint16_t sum;

    if (!vol || !name_ok(name) || (!data && len)) return -1;
    cluster_bytes = (uint32_t)vol->sectors_per_cluster * vol->bytes_per_sector;
    need = (uint32_t)((len + cluster_bytes - 1) / cluster_bytes);
    if (need == 0) need = 1;
    if (need > 64 || len > 64 * cluster_bytes) return -1;

    split_name(name, base, &base_len, ext, &ext_len);
    /* The exact 8.3 spelling is the identity of this file. Calling
     * make_short_name() on a rewrite would see the old entry as taken and
     * hand back DEVINF~1.TXT, creating a second file instead of replacing. */
    for (int i = 0; i < 11; i++) exact[i] = ' ';
    for (int i = 0; i < base_len && i < 8; i++) exact[i] = (uint8_t)base[i];
    for (int i = 0; i < ext_len && i < 3; i++) exact[8 + i] = (uint8_t)ext[i];
    memcpy(short_name, exact, 11);

    entry = find_short_entry(vol, exact);
    if (entry) {
        /* Replace in place. Refuse a rewrite that would need more clusters
         * than the first publication reserved: growing a file in a FAT the
         * host may be reading right now is not safe. */
        first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);
        if (!first || file_capacity(vol, first) < len) return -1;
        cur = first;
        written = 0;
        while (written < len) {
            uint8_t *dst = vol->data +
                (size_t)cluster_sector(vol, cur) * vol->bytes_per_sector;
            uint32_t chunk = (uint32_t)len - written;
            if (chunk > cluster_bytes) chunk = cluster_bytes;
            memset(dst, 0, cluster_bytes);
            memcpy(dst, data + written, chunk);
            written += chunk;
            {
                uint8_t *f = vol->data +
                    (size_t)vol->fat_start * vol->bytes_per_sector;
                uint16_t next;
                memcpy(&next, f + cur * 2, 2);
                cur = next;
            }
        }
        entry[28] = (uint8_t)(len & 0xFF);
        entry[29] = (uint8_t)((len >> 8) & 0xFF);
        entry[30] = (uint8_t)((len >> 16) & 0xFF);
        entry[31] = (uint8_t)((len >> 24) & 0xFF);
        umos_logf("fat16: rewrote %s (%u bytes)\n", name, (unsigned)len);
        return 0;
    }

    name_len = (int)strlen(name);
    records = (name_len + 1 + 12) / 13;
    if (vol->root_entries_used + (uint32_t)records + 1 > FAT16_ROOT_ENTRIES)
        return -1;
    if (vol->next_cluster + need - 2 > vol->total_clusters) return -1;

    first = vol->next_cluster;
    prev = 0;
    written = 0;
    for (uint32_t i = 0; i < need; i++) {
        cur = vol->next_cluster++;
        if (prev) fat_set(vol, prev, (uint16_t)cur);
        prev = cur;
        {
            uint8_t *dst = vol->data +
                (size_t)cluster_sector(vol, cur) * vol->bytes_per_sector;
            uint32_t chunk = (uint32_t)len - written;
            memset(dst, 0, cluster_bytes);
            if (chunk > cluster_bytes) chunk = cluster_bytes;
            if (chunk) memcpy(dst, data + written, chunk);
            written += chunk;
        }
    }
    fat_set(vol, prev, FAT16_EOF);

    sum = lfn_checksum(short_name);
    for (int r = records; r >= 1; r--) {
        uint8_t *e = root_entry(vol, vol->root_entries_used++);
        memset(e, 0, FAT16_ENTRY_SIZE);
        e[0] = (uint8_t)((r == records ? FAT16_LFN_LAST : 0) | r);
        e[11] = FAT16_ATTR_LFN;
        e[13] = (uint8_t)(sum & 0xFF);
        lfn_put_chars(e, name, name_len, (r - 1) * 13);
    }
    put_entry(vol, short_name, FAT16_ATTR_ARCHIVE, first, (uint32_t)len);
    umos_logf("fat16: wrote %s (%u bytes, %u cluster(s))\n",
              name, (unsigned)len, (unsigned)need);
    return 0;
}

int fat16_read_file(struct fat16_volume *vol, const char *name,
                    uint8_t *out, size_t cap) {
    char base[21], ext[4];
    uint8_t exact[11];
    int base_len, ext_len;
    uint8_t *entry;
    uint32_t size, first, cluster_bytes, done, cur;
    uint16_t next;

    if (!vol || !name_ok(name) || !out) return -1;
    split_name(name, base, &base_len, ext, &ext_len);
    for (int i = 0; i < 11; i++) exact[i] = ' ';
    for (int i = 0; i < base_len && i < 8; i++) exact[i] = (uint8_t)base[i];
    for (int i = 0; i < ext_len && i < 3; i++) exact[8 + i] = (uint8_t)ext[i];
    entry = find_short_entry(vol, exact);
    if (!entry) return -1;
    size = (uint32_t)entry[28] | ((uint32_t)entry[29] << 8) |
           ((uint32_t)entry[30] << 16) | ((uint32_t)entry[31] << 24);
    if (size > cap) return -1;
    first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);
    cluster_bytes = (uint32_t)vol->sectors_per_cluster * vol->bytes_per_sector;
    cur = first;
    done = 0;
    while (done < size && cur >= 2 && cur < FAT16_EOF - 8) {
        uint8_t *src = vol->data +
            (size_t)cluster_sector(vol, cur) * vol->bytes_per_sector;
        uint32_t chunk = size - done;
        if (chunk > cluster_bytes) chunk = cluster_bytes;
        memcpy(out + done, src, chunk);
        done += chunk;
        memcpy(&next,
               vol->data + (size_t)vol->fat_start * vol->bytes_per_sector +
               cur * 2, 2);
        cur = next;
    }
    return done == size ? (int)size : -1;
}

int fat16_file_span(struct fat16_volume *vol, const char *name,
                    const uint8_t **out, size_t *size) {
    char base[21], ext[4];
    uint8_t exact[11];
    int base_len, ext_len;
    uint8_t *entry;
    uint32_t bytes, first, cur, expected, n;
    uint16_t next;

    if (!vol || !name_ok(name) || !out || !size) return -1;
    split_name(name, base, &base_len, ext, &ext_len);
    for (int i = 0; i < 11; i++) exact[i] = ' ';
    for (int i = 0; i < base_len && i < 8; i++) exact[i] = (uint8_t)base[i];
    for (int i = 0; i < ext_len && i < 3; i++) exact[8 + i] = (uint8_t)ext[i];
    entry = find_short_entry(vol, exact);
    if (!entry) return -1;
    bytes = (uint32_t)entry[28] | ((uint32_t)entry[29] << 8) |
            ((uint32_t)entry[30] << 16) | ((uint32_t)entry[31] << 24);
    if (!bytes) return -1;
    first = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8);
    cur = first;
    expected = first;
    n = 0;
    while (cur >= 2 && cur < FAT16_EOF - 8) {
        if (cur != expected) return -1;      /* fragmented: refuse */
        n++;
        memcpy(&next,
               vol->data + (size_t)vol->fat_start * vol->bytes_per_sector +
               cur * 2, 2);
        cur = next;
        expected++;
        if (n * vol->sectors_per_cluster * vol->bytes_per_sector >= bytes) break;
    }
    *out = vol->data +
           (size_t)cluster_sector(vol, first) * vol->bytes_per_sector;
    *size = bytes;
    return 0;
}

int fat16_read_sector(struct fat16_volume *vol, uint32_t lba, uint8_t *buf) {
    if (!vol || !buf || lba * vol->bytes_per_sector >= vol->size) return -1;
    memcpy(buf, vol->data + lba * vol->bytes_per_sector, vol->bytes_per_sector);
    return 0;
}

int fat16_write_sector(struct fat16_volume *vol, uint32_t lba, const uint8_t *buf) {
    if (!vol || !buf || lba * vol->bytes_per_sector >= vol->size) return -1;
    memcpy(vol->data + lba * vol->bytes_per_sector, buf, vol->bytes_per_sector);
    return 0;
}

uint32_t fat16_total_sectors(struct fat16_volume *vol) {
    return vol ? (uint32_t)(vol->size / vol->bytes_per_sector) : 0;
}
