/* SPDX-License-Identifier: MIT */
/* fs/fat16.h - FAT16 filesystem in RAM (for the USB mass storage LUN).
 *
 * The rescue volume is built here, byte by byte, and served straight from RAM
 * by bsp/violet/usb_gadget.c. Everything a host reads off the drive - boot
 * sector, both FATs, the root directory, subdirectory clusters - comes from
 * this file, so the geometry written and the entries appended have to agree
 * exactly or Windows sees an empty or corrupt disk.
 */

#ifndef UMOS_FAT16_H
#define UMOS_FAT16_H

#include <stdint.h>
#include <stddef.h>

#define FAT16_VOLUME_SIZE (32 * 1024 * 1024)  /* 32 MiB */
#define FAT16_SECTOR_SIZE 512
#define FAT16_LABEL "QEAFRESCUE"
#define FAT16_ROOT_ENTRIES 512

/* Directory entry attributes. Only the four this volume actually writes. */
#define FAT16_ATTR_READ_ONLY 0x01
#define FAT16_ATTR_VOLUME    0x08  /* the one label entry in the root */
#define FAT16_ATTR_DIR       0x10
#define FAT16_ATTR_ARCHIVE   0x20
#define FAT16_ATTR_LFN       0x0F  /* a long-name fragment, not an entry */

#define FAT16_ENTRY_SIZE 32
#define FAT16_LFN_LAST 0x40        /* OR'd into the sequence byte */
#define FAT16_EOF 0xFFFF           /* end of a cluster chain */

struct fat16_volume {
    uint8_t *data;
    size_t size;
    uint32_t fat_start;
    uint32_t fat_size;
    uint32_t root_dir_start;
    uint32_t root_dir_sectors;
    uint32_t data_start;
    uint32_t total_clusters;
    uint8_t sectors_per_cluster;
    uint8_t num_fats;
    uint16_t reserved_sectors;
    uint16_t bytes_per_sector;
    /* Allocation state the builder keeps between calls. Cluster 2 is the
     * first data cluster; entries are handed out from the root in order. */
    uint32_t next_cluster;
    uint32_t root_entries_used;
};

int fat16_init(struct fat16_volume *vol, uint8_t *data, size_t size);
int fat16_read_sector(struct fat16_volume *vol, uint32_t lba, uint8_t *buf);
int fat16_write_sector(struct fat16_volume *vol, uint32_t lba, const uint8_t *buf);
uint32_t fat16_total_sectors(struct fat16_volume *vol);

/* Adds a subdirectory of the root, with the name exactly as it should appear
 * in a file manager. Names that do not fit the 8.3 short form get a long-name
 * record in front of their short entry, so Windows shows "Documents" while a
 * reader that only understands 8.3 still sees a valid DOCUME~1.
 * Returns the cluster it occupies, or -1. */
int fat16_mkdir(struct fat16_volume *vol, const char *name);
int fat16_mkdir_in(struct fat16_volume *vol, uint32_t parent_cluster, const char *name);

/* Creates (or replaces in place) a small root file. Used by the sync
 * protocol: the phone publishes DEVINFO.TXT so a PC tool reads machine
 * state instead of scraping a 64 KiB log ring. `len` must fit the cluster
 * chain already allocated on the first call; returns 0 or -1. */
int fat16_write_file(struct fat16_volume *vol, const char *name,
                     const uint8_t *data, size_t len);

/* Reads a root file back by name. Returns the byte count, or -1. Used by the
 * rescue command loop to see the QEAF.CMD a host just dropped on the LUN. */
int fat16_read_file(struct fat16_volume *vol, const char *name,
                    uint8_t *out, size_t cap);

/* Returns a direct pointer to a root file's bytes when its cluster chain is
 * physically contiguous, plus its size. Lets the rescue flash verb write a
 * 24 MiB image to p77 without allocating a second 24 MiB buffer. Returns -1
 * for a fragmented chain (the host must defrag by rewriting BOOTIMG.BIN). */
int fat16_file_span(struct fat16_volume *vol, const char *name,
                    const uint8_t **out, size_t *size);

#endif
