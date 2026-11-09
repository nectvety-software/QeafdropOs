/* SPDX-License-Identifier: MIT */
/* tests/storage_fat16_test.c - the QEAFRESCUE volume, read back as bytes.
 *
 * The product builds a FAT16 image in RAM; a host then reads raw sectors off
 * the LUN and mounts them. So this harness does not ask the builder what it
 * thinks it made - it re-derives the geometry from the boot sector, walks the
 * root directory the way the FAT spec says, reassembles long-name records and
 * follows each cluster chain. If the builder wrote nothing, the walk finds
 * nothing, and the bare-volume control below proves the walk can see that.
 *
 * Build and run (from repo root):  make storage-check
 * It also writes out/qeafrescue_meta.img, which tests/check_rescue_volume.py
 * parses again with a second, independent implementation.
 */

#include "fs/fat16.h"
#include "fs/storage.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t img[FAT16_VOLUME_SIZE];
static struct fat16_volume vol;

static int g_checks = 0;
static int g_fail = 0;

static void check(int cond, const char *label) {
    g_checks++;
    if (cond) {
        printf("ok    %s\n", label);
    } else {
        g_fail++;
        printf("FAIL  %s\n", label);
    }
}

/* ---- the view a host gets: geometry out of the boot sector only ----------- */

struct geom {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t fat_size;
    uint32_t total_sectors;
    uint32_t fat_start;
    uint32_t root_start;
    uint32_t data_start;
    char label[12];
    char fstype[9];
};

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int geom_read(struct geom *g) {
    const uint8_t *bs = img;
    memset(g, 0, sizeof(*g));
    if (bs[510] != 0x55 || bs[511] != 0xAA) return -1;
    g->bytes_per_sector = le16(bs + 11);
    g->sectors_per_cluster = bs[13];
    g->reserved = le16(bs + 14);
    g->num_fats = bs[16];
    g->root_entries = le16(bs + 17);
    g->fat_size = le16(bs + 22);
    g->total_sectors = le32(bs + 32);
    memcpy(g->label, bs + 43, 11); g->label[11] = 0;
    memcpy(g->fstype, bs + 54, 8); g->fstype[8] = 0;
    g->fat_start = g->reserved;
    g->root_start = g->fat_start + (uint32_t)g->num_fats * g->fat_size;
    g->data_start = g->root_start + ((uint32_t)g->root_entries * 32 +
                                     g->bytes_per_sector - 1) / g->bytes_per_sector;
    return 0;
}

static uint16_t fat_at(const struct geom *g, uint8_t which, uint32_t cluster) {
    return le16(img + (size_t)(g->fat_start + which * g->fat_size) *
                       g->bytes_per_sector + cluster * 2);
}

static const uint8_t *dir_sector(const struct geom *g, uint32_t index) {
    return img + (size_t)g->root_start * g->bytes_per_sector + (size_t)index * 32;
}

static uint16_t sfn_checksum(const uint8_t *short_name) {
    uint16_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint16_t)(((sum & 1u) ? 0x8000u : 0u) + (sum >> 1) + short_name[i]);
    return sum;
}

/* ---- one root walk, written from the spec, not from the builder ----------- */

struct found {
    char name[24];
    char short_name[16];
    uint8_t attr;
    uint32_t cluster;
    int from_lfn;
};

/* The readable 8.3 form of an entry: trailing blanks dropped, extension
 * rejoined. A reader that ignores long names sees exactly this. A volume label
 * is the one entry whose 11 bytes are a single name, so it is not split. */
static void short83(const uint8_t *e, char *out) {
    int n = 0, hi = (e[11] == FAT16_ATTR_VOLUME) ? 10 : 7, i;

    for (i = hi; i >= 0 && e[i] == ' '; i--) ;
    for (int c = 0; c <= i; c++) out[n++] = (char)e[c];
    if (hi == 10) { out[n] = 0; return; }
    for (i = 10; i >= 8 && e[i] == ' '; i--) ;
    if (i >= 8) {
        out[n++] = '.';
        for (int c = 8; c <= i; c++) out[n++] = (char)e[c];
    }
    out[n] = 0;
}

/* Returns how many real entries (not free, not long-name records) the root
 * holds; fills `out` with them. */
static int walk_root(const struct geom *g, struct found *out, int max) {
    char lfn[64];
    int lfn_len = 0, lfn_expect = -1, n = 0;
    uint8_t sum = 0;

    for (uint32_t i = 0; i < g->root_entries; i++) {
        const uint8_t *e = dir_sector(g, i);
        uint8_t attr = e[11];
        if (attr == 0x00) break;                       /* no more entries */
        if (e[0] == 0xE5) continue;                    /* deleted */
        if (attr == FAT16_ATTR_LFN) {
            int seq = e[0];
            int first;
            if (seq & FAT16_LFN_LAST) { lfn_len = 0; lfn_expect = (seq & 0x3F); sum = e[13]; }
            if (lfn_expect < 0 || (seq & 0x3F) != lfn_expect) { lfn_expect = -1; continue; }
            first = (lfn_expect - 1) * 13;
            lfn_expect--;
            static const int off[3] = {1, 14, 28};
            static const int cnt[3] = {5, 6, 2};
            int slot = 0, k = 0;
            for (int c = 0; c < 13; c++) {
                uint16_t u;
                if (k == cnt[slot]) { slot++; k = 0; }
                u = le16(e + off[slot] + k * 2);
                k++;
                if (u == 0x0000) break;
                if (u == 0xFFFF) continue;
                if (first + c >= (int)sizeof(lfn)) break;
                lfn[first + c] = (char)(u & 0xFF);
                if (first + c + 1 > lfn_len) lfn_len = first + c + 1;
            }
            continue;
        }
        if (n < max) {
            struct found *f = &out[n];
            memset(f, 0, sizeof(*f));
            short83(e, f->short_name);
            f->attr = attr;
            f->cluster = le16(e + 26);
            /* The checksum field is one byte: only its low half is stored. */
            if (lfn_expect == 0 && lfn_len > 0 && sum == (uint8_t)sfn_checksum(e)) {
                memcpy(f->name, lfn, (size_t)lfn_len);
                f->name[lfn_len] = 0;
                f->from_lfn = 1;
            } else {
                strcpy(f->name, f->short_name);
                f->from_lfn = 0;
            }
            n++;
        }
        lfn_expect = -1;
        lfn_len = 0;
    }
    return n;
}

static int dir_of(const struct found *f) { return (f->attr & FAT16_ATTR_DIR) != 0; }

/* The two entries every subdirectory must open with. */
static int dotdot_ok(const struct geom *g, uint32_t cluster) {
    const uint8_t *d = img + (size_t)(g->data_start + (cluster - 2) *
                                     g->sectors_per_cluster) * g->bytes_per_sector;
    uint32_t self = le16(d + 26), parent = le16(d + 32 + 26);
    return d[0] == '.' && d[11] == FAT16_ATTR_DIR && d[32] == '.' && d[33] == '.' &&
           d[43] == FAT16_ATTR_DIR && self == cluster && parent == 0;
}

/* ---- the listing a human reads -------------------------------------------- */

static void print_listing(const struct found *list, int n) {
    printf("      root directory, %d entr(y/ies) read back from raw sectors:\n", n);
    for (int i = 0; i < n; i++)
        printf("        %-10s  attr 0x%02X  cluster %u  short '%s'  %s\n",
               list[i].name, list[i].attr, list[i].cluster, list[i].short_name,
               list[i].from_lfn ? "long name" : "8.3 only");
}

int main(void) {
    struct geom g;
    struct found list[16];
    int n, dirs, ok_chain, ok_dots, i;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== storage: the QEAFRESCUE volume's folder structure ==\n");

    /* ---- build it the way the phone does ---------------------------------- */
    check(fat16_init(&vol, img, FAT16_VOLUME_SIZE) == 0,
          "the volume initialises at 32 MiB");
    check(storage_publish(&vol) == UMOS_STORAGE_COUNT,
          "all storage folders are published");

    /* ---- geometry, read back before anything else ------------------------- */
    check(geom_read(&g) == 0, "the boot sector carries the 0x55AA signature");
    check(g.bytes_per_sector == 512 && g.sectors_per_cluster == 8,
          "512-byte sectors, 4 KiB clusters");
    check(g.num_fats == 2 && g.fat_size == 256 && g.root_entries == 512,
          "two FATs and a 512-entry root, as the BPB claims");
    check(g.total_sectors == FAT16_VOLUME_SIZE / 512, "the sector count is the size");
    check(memcmp(g.fstype, "FAT16", 5) == 0, "the type string says FAT16");
    check(memcmp(g.label, "QEAFRESCUE   ", 11) == 0, "the BPB label is QEAFRESCUE");
    printf("      geometry: FAT at %u, root at %u, data at %u\n",
           g.fat_start, g.root_start, g.data_start);

    /* ---- the media entries, which used to be three loose bytes ------------ */
    check(fat_at(&g, 0, 0) == 0xFFF8 && fat_at(&g, 1, 0) == 0xFFF8,
          "FAT[0] is the media descriptor in both FATs");
    check(fat_at(&g, 0, 1) == 0xFFFF && fat_at(&g, 1, 1) == 0xFFFF,
          "FAT[1] is an end-of-chain marker, not a reserved value");

    /* ---- the root, walked from the spec ----------------------------------- */
    n = walk_root(&g, list, 16);
    print_listing(list, n);
    check(n == 1 + UMOS_STORAGE_COUNT, "the root holds the label plus storage folders");

    dirs = 0; ok_chain = 1; ok_dots = 1;
    for (i = 0; i < n; i++) {
        if (!dir_of(&list[i])) continue;
        dirs++;
        if (list[i].cluster < 2 || fat_at(&g, 0, list[i].cluster) < 0xFFF8 ||
            fat_at(&g, 1, list[i].cluster) != fat_at(&g, 0, list[i].cluster))
            ok_chain = 0;
        if (!dotdot_ok(&g, list[i].cluster)) ok_dots = 0;
    }
    check(dirs == UMOS_STORAGE_COUNT, "all of them are directories");
    check(ok_chain, "every folder's cluster is allocated and ends its chain in both FATs");
    check(ok_dots, "every folder opens with '.' pointing at itself and '..' at the root");

    /* ---- the names are the storage table, not an 8.3 approximation -------- */
    for (i = 0; i < UMOS_STORAGE_COUNT; i++) {
        int matched = 0;
        for (int j = 0; j < n; j++)
            if (dir_of(&list[j]) && strcmp(list[j].name, storage_name(i)) == 0)
                matched = list[j].from_lfn;
        printf("      %-10s -> %s\n", storage_name(i),
               matched ? "long name, exact case" : "MISSING");
        check(matched, "the folder reads back exactly as the storage table spells it");
    }
    check(storage_kind_from_name("UserData") == UMOS_STORAGE_USERDATA &&
          storage_kind_from_name("VQEAF") == UMOS_STORAGE_VQEAF,
          "the table maps a name back to its kind");
    check(storage_kind_from_name("DCIM") == -1 && storage_name(UMOS_STORAGE_COUNT) == NULL,
          "and refuses what it does not know");

    /* A reader that ignores long names still gets distinct, legal 8.3
     * names: nothing about the volume depends on LFN support. */
    {
        int eight_ok = 1;
        for (i = 0; i < n; i++) {
            if (!dir_of(&list[i])) continue;
            if (list[i].short_name[0] == 0 || strlen(list[i].short_name) > 12) eight_ok = 0;
            for (int j = i + 1; j < n; j++)
                if (dir_of(&list[j]) &&
                    strcmp(list[i].short_name, list[j].short_name) == 0)
                    eight_ok = 0;
        }
        check(eight_ok, "the folders also carry distinct short names");
    }
    check(vol.root_entries_used == 1 + 2 * UMOS_STORAGE_COUNT,
          "the root spent exactly label + (long, short) per folder");

    /* ---- the label is a root entry, not only a BPB field ------------------ */
    {
        int labels = 0;
        for (i = 0; i < n; i++) if (list[i].attr == FAT16_ATTR_VOLUME) labels++;
        check(labels == 1 && strcmp(list[0].name, "QEAFRESCUE") == 0,
              "one volume label entry, first in the root");
    }

    /* ---- hand the product state to the second, independent parser --------- *
     * Snapshot taken here, before the controls below rewrite the same RAM. */
    {
        FILE *f = fopen("out/qeafrescue_meta.img", "wb");
        size_t written = f ? fwrite(img, 1, 1024 * 1024, f) : 0;
        if (f) fclose(f);
        check(written == 1024 * 1024,
              "wrote the first MiB (boot, both FATs, root, folder clusters) for the "
              "Python parser");
    }

    /* ---- negative controls ------------------------------------------------ *
     * 1. A volume that only got fat16_init must read back with no folders.
     *    Without this, "five folders" could be the walk inventing structure. */
    check(fat16_init(&vol, img, FAT16_VOLUME_SIZE) == 0, "re-init the same RAM");
    n = walk_root(&g, list, 16);
    check(n == 1, "a bare volume reads back as label only - the walk sees nothing extra");
    {
        int bare_dirs = 0;
        for (i = 0; i < n; i++) if (dir_of(&list[i])) bare_dirs++;
        check(bare_dirs == 0, "and zero directories before storage_publish");
    }

    /* 2. Rebuild, then wipe the root: the same walk must find nothing. */
    check(fat16_init(&vol, img, FAT16_VOLUME_SIZE) == 0 &&
          storage_publish(&vol) == UMOS_STORAGE_COUNT, "rebuilt with the folders");
    memset(img + (size_t)g.root_start * g.bytes_per_sector, 0,
           (size_t)g.root_entries * 32);
    n = walk_root(&g, list, 16);
    check(n == 0, "a zeroed root yields no entries at all - the count observes");

    /* 3. The builder refuses names a file system cannot carry. */
    check(fat16_init(&vol, img, FAT16_VOLUME_SIZE) == 0, "a clean volume for the rejects");
    check(fat16_mkdir(&vol, "") < 0, "an empty name is rejected");
    check(fat16_mkdir(&vol, "bad/name") < 0, "a slash is rejected");
    check(fat16_mkdir(&vol, "way too long a name for this volume") < 0,
          "a name past the table's reach is rejected");
    check(vol.root_entries_used == 1, "and none of them took a root entry");

    /* 4. Two nine-character stems share the same six visible 8.3 characters,
     *    so the second must land on ~2 rather than alias over the first. */
    check(storage_publish(&vol) == UMOS_STORAGE_COUNT, "published again");
    check(fat16_mkdir(&vol, "Documentz") >= 0, "a near-collision name is accepted");
    n = walk_root(&g, list, 16);
    {
        const char *short1 = NULL, *short2 = NULL;
        for (i = 0; i < n; i++) {
            if (strcmp(list[i].name, "UserData") == 0) short1 = list[i].short_name;
            if (strcmp(list[i].name, "Documentz") == 0) short2 = list[i].short_name;
        }
        printf("      short names: UserData -> '%s', Documentz -> '%s'\n",
               short1 ? short1 : "?", short2 ? short2 : "?");
        check(short1 && short2 && strncmp(short1, short2, 11) != 0,
              "the two stems get different short names - no aliasing");
        check(short1 && strcmp(short1, "USERDATA") == 0 &&
              short2 && strcmp(short2, "DOCUME~1") == 0,
              "and they are what a host would generate itself");
    }

    /* ---- DEVINFO.TXT: the sync file, create + rewrite in place ------------ */
    {
        static const char first[] = "VQEAF-SYNC/1\nusb_mode=charge\n";
        static const char second[] = "VQEAF-SYNC/1\nusb_mode=ptp\n";
        struct geom fg;
        struct found fl[16];
        uint32_t entries_before, n_before, cluster_bytes, off;
        const uint8_t *raw;

        printf("\n== sync file: DEVINFO.TXT on the real FAT16 builder ==\n");
        geom_read(&fg);
        n_before = (uint32_t)walk_root(&fg, fl, 16);
        entries_before = vol.root_entries_used;
        check(fat16_write_file(&vol, "DEVINFO.TXT",
                               (const uint8_t *)first, sizeof(first) - 1) == 0,
              "a small root file is published");
        check(geom_read(&fg) == 0, "the boot sector still parses after a file");
        n = walk_root(&fg, fl, 16);
        check(n == (int)n_before + 1,
              "exactly one real root entry was added");
        check(vol.root_entries_used == entries_before + 2,
              "one long-name record plus its 8.3 entry, nothing more");
        {
            int found = -1;
            for (i = 0; i < n; i++)
                if (strcmp(fl[i].short_name, "DEVINFO.TXT") == 0) found = i;
            check(found >= 0 && fl[found].from_lfn,
                  "the 8.3 name is exact and a long-name record is written");
            if (found >= 0) {
                cluster_bytes = (uint32_t)fg.sectors_per_cluster *
                                fg.bytes_per_sector;
                off = (uint32_t)fg.data_start * fg.bytes_per_sector +
                      (fl[found].cluster - 2) * cluster_bytes;
                raw = img + off;
                check(memcmp(raw, first, sizeof(first) - 1) == 0,
                      "a FAT-spec walk reads the exact bytes back");
            }
        }
        check(fat16_write_file(&vol, "DEVINFO.TXT",
                               (const uint8_t *)second, sizeof(second) - 1) == 0,
              "the same file can be republished when USB mode changes");
        check(vol.root_entries_used == entries_before + 2,
              "the rewrite did NOT create a DEVINF~1.TXT duplicate");
        n = walk_root(&fg, fl, 16);
        {
            int found = -1;
            for (i = 0; i < n; i++)
                if (strcmp(fl[i].short_name, "DEVINFO.TXT") == 0) found = i;
            if (found >= 0) {
                cluster_bytes = (uint32_t)fg.sectors_per_cluster *
                                fg.bytes_per_sector;
                off = (uint32_t)fg.data_start * fg.bytes_per_sector +
                      (fl[found].cluster - 2) * cluster_bytes;
                check(memcmp(img + off, second, sizeof(second) - 1) == 0,
                      "the rewritten bytes are what a host now reads");
            } else {
                check(0, "the rewritten file is still in the root");
            }
        }
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
