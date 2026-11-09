/* SPDX-License-Identifier: MIT */
/* tests/prefs_boot_test.c - the durable slot across two process lifetimes.
 *
 * The gate runs this executable twice: `write` saves a choice, then `verify`
 * (a separate process, the host's stand-in for a reboot) removes the volatile
 * /etc copy and loads again. Only the p77 fixture can supply the values. A
 * later check flips one payload byte and requires the CRC to reject the record
 * rather than apply a half-written setting.
 *
 * Usage:
 *   prefs_boot_test write  <block-fixture> <conf-fixture>
 *   prefs_boot_test verify <block-fixture> <conf-fixture>
 */

#include "core/prefs.h"
#include "core/sysio.h"

#include <stdio.h>
#include <string.h>

#define FIXTURE_OFFSET (64u * 1024u)
#define FIXTURE_SIZE   (128u * 1024u)

static int g_checks, g_fail;

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

/* A fixture the size of the reserved slot region, filled with zeroes so the
 * first write lands exactly where a p77 tail write would. */
static int make_fixture(const char *path) {
    static unsigned char zero[4096];
    unsigned left = FIXTURE_SIZE;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    while (left) {
        unsigned n = left < sizeof(zero) ? left : (unsigned)sizeof(zero);
        if (fwrite(zero, 1, n, f) != n) { fclose(f); return -1; }
        left -= n;
    }
    return fclose(f) == 0 ? 0 : -1;
}

static int flip_payload_byte(const char *path) {
    unsigned char b;
    FILE *f = fopen(path, "r+b");
    if (!f) return -1;
    if (fseek(f, (long)(FIXTURE_OFFSET + PREFS_BLOCK_HEADER + 2),
              SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    if (fread(&b, 1, 1, f) != 1) { fclose(f); return -1; }
    b ^= 0x20;
    if (fseek(f, (long)(FIXTURE_OFFSET + PREFS_BLOCK_HEADER + 2),
              SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    if (fwrite(&b, 1, 1, f) != 1) { fclose(f); return -1; }
    return fclose(f) == 0 ? 0 : -1;
}

static int write_stage(const char *block, const char *conf) {
    char buf[64];
    int n;

    remove(conf);
    if (make_fixture(block) != 0) {
        printf("FAIL  could not create the block fixture\n");
        return 1;
    }
    prefs_set_path(conf);
    prefs_set_block(block, FIXTURE_OFFSET);

    check(prefs_save(4, 1, 0) == 0,
          "a choice is saved to the p77 slot and the session file");
    check(prefs_source()[0] == 'p', "the save reports the p77 slot as its source");
    check(sysio_block_read_at(block, FIXTURE_OFFSET, (uint8_t *)buf, 8) == 0 &&
          memcmp(buf, PREFS_BLOCK_MAGIC, 8) == 0,
          "the record starts with the QEAFPRF1 magic");
    n = sysio_read_file(conf, buf, (int)sizeof(buf));
    check(n > 0 && strstr(buf, "language=4") != NULL,
          "the volatile session copy agrees with the slot");

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

static int verify_stage(const char *block, const char *conf) {
    int lang = 0, rot = 0, aut = 0;

    /* A reboot loses the initramfs, so only the block fixture remains. */
    remove(conf);
    prefs_set_path(conf);
    prefs_set_block(block, FIXTURE_OFFSET);

    check(prefs_load(&lang, &rot, &aut) == 0,
          "a fresh process finds a readable record");
    check(lang == 4 && rot == 1 && aut == 0,
          "the record restores language, orientation and auto-rotate");
    check(strcmp(prefs_source(), "p77 slot") == 0,
          "the restored values came from the p77 slot, not /etc");

    check(prefs_sync(4, 1, 0) == 0,
          "boot restores without rewriting an unchanged record");

    check(flip_payload_byte(block) == 0, "one payload byte is corrupted");
    lang = 7; rot = 3; aut = 1;
    check(prefs_load(&lang, &rot, &aut) == -1,
          "a CRC mismatch is rejected, not applied");
    check(lang == 7 && rot == 3 && aut == 1,
          "a corrupt slot leaves the caller's values alone");

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("usage: %s write|verify <block-fixture> <conf-fixture>\n",
               argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "write") == 0) return write_stage(argv[2], argv[3]);
    if (strcmp(argv[1], "verify") == 0) return verify_stage(argv[2], argv[3]);
    printf("unknown stage %s\n", argv[1]);
    return 2;
}
