/* SPDX-License-Identifier: MIT */
/* tests/rescue_channel_test.c - the QEAF.CMD loop against a real FAT16 LUN.
 *
 * Builds the same 32 MiB RAM volume the phone serves, then plays the part of
 * a PC: writes QEAF.CMD batches, ticks rescue_poll(), reads RESPONSE.TXT back
 * by name and checks the answer. The flash verb writes to a FIXTURE FILE
 * instead of p77, and its byte-for-byte readback is measured too - including
 * the negative controls (missing confirm, unsafe target).
 *
 * All FAT calls go through usb_gadget_volume(): the gadget owns the volume,
 * and a second fat16_init() on a different buffer would silently build a
 * different disk (which is exactly what the first version of this gate did).
 */

#include "fs/fat16.h"
#include "fs/rescue.h"
#include "core/sysio.h"
#include "qeaf_version.h"
#include "hal/include/usb_gadget.h"
#include "hal/include/display.h"
#include "gui/gfx/gfx.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct fat16_volume *vol;
static int g_checks, g_fail;
static int g_test_lang = -1;

static void check(int cond, const char *label) {
    g_checks++;
    if (cond) printf("ok    %s\n", label);
    else { g_fail++; printf("FAIL  %s\n", label); }
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void host_write(const char *cmd) {
    fat16_write_file(vol, "QEAF.CMD", (const uint8_t *)cmd, (size_t)slen(cmd));
}

/* Records the core/i18n value the rescue verb forwarded to the platform. */
static int test_language_hook(int lang) {
    g_test_lang = lang;
    return 0;
}

static int read_back(const char *name, char *out, int cap) {
    int n = fat16_read_file(vol, name, (uint8_t *)out, (size_t)cap - 1);
    if (n < 0) return -1;
    out[n] = '\0';
    return n;
}

int main(void) {
    char resp[16384];
    char help[16384];
    const uint8_t *bootimg = NULL;
    size_t boot_size = 0;
    static const char part[] = "out/rescue_fx/p77.img";
    FILE *f;

    printf("== QEAF.CMD rescue channel, real FAT16 in RAM ==\n");
    check(usb_gadget_init() == 0, "gadget builds the LUN + channel files");
    vol = usb_gadget_volume();
    check(vol != NULL, "the gadget exposes the very volume the PC reads");
    check(usb_gadget_start() == 0, "gadget starts (set_mode becomes legal)");

    /* The channel's own files must exist on the volume the PC sees. */
    check(read_back("HELP.TXT", help, sizeof(help)) > 0,
          "HELP.TXT is a real file on the LUN");
    check(strstr(help, "QEAF.CMD") != NULL &&
          strstr(help, "flash recovery BOOTIMG.BIN confirm") != NULL,
          "HELP.TXT documents the command channel and the flash verb");
    check(read_back("SUMMARY.TXT", help, sizeof(help)) > 0,
          "SUMMARY.TXT is present");
    check(read_back("DEVINFO.TXT", help, sizeof(help)) > 0 &&
          strstr(help, "VQEAF-SYNC/1") != NULL,
          "DEVINFO.TXT carries the sync protocol banner");
    check(read_back("QEAFBOOT.LOG", help, sizeof(help)) > 0 &&
          strstr(help, QEAF_OS_STRING " starting") != NULL,
          "QEAFBOOT.LOG has the boot banner");

    /* Storage folders survived the extra channel files. */
    check(vol->root_entries_used > 5, "channel files did not replace folders");

    /* A batch with a nonce: the answer must echo it. */
    host_write("status\necho NONCE1234\n");
    usb_gadget_poll();
    resp[0] = '\0';
    check(read_back("RESPONSE.TXT", resp, sizeof(resp)) > 0 &&
          strstr(resp, "NONCE1234") != NULL &&
          strstr(resp, QEAF_OS_STRING) != NULL,
          "status + echo batch is answered with the nonce");

    /* Same batch again must NOT be executed twice. */
    usb_gadget_poll();
    check(rescue_last_result() == 0, "no privileged action ran yet");

    /* usb verb changes mode and republishes DEVINFO.TXT. */
    host_write("usb mtp\necho M1\n");
    usb_gadget_poll();
    check(usb_gadget_mode() == USB_MODE_MTP, "usb mtp reaches the gadget");
    check(read_back("DEVINFO.TXT", help, sizeof(help)) > 0 &&
          strstr(help, "usb_mode=mtp") != NULL,
          "the mode change is republished for VQEAF Tool");
    host_write("usb nonsense\necho M2\n");
    usb_gadget_poll();
    check(usb_gadget_mode() == USB_MODE_MTP, "an unknown mode is refused");

    /* Unsafe flash verbs must be refused before any write. */
    host_write("flash boot BOOTIMG.BIN confirm\necho F0\n");
    usb_gadget_poll();
    resp[0] = '\0';
    read_back("RESPONSE.TXT", resp, sizeof(resp));
    check(strstr(resp, "REFUSED") != NULL,
          "a flash to p76/boot is refused");
    host_write("flash recovery BOOTIMG.BIN\necho F1\n");
    usb_gadget_poll();
    resp[0] = '\0';
    read_back("RESPONSE.TXT", resp, sizeof(resp));
    check(strstr(resp, "REFUSED") != NULL,
          "a flash without the confirm token is refused");

    /* Stage a small but structurally valid image and flash it for real. */
    {
        static uint8_t fake[8192];
        memset(fake, 0x5A, sizeof(fake));
        memcpy(fake, "ANDROID!", 8);
        check(fat16_write_file(vol, "BOOTIMG.BIN", fake, sizeof(fake)) == 0,
              "a BOOTIMG.BIN is staged on the LUN");
        check(fat16_file_span(vol, "BOOTIMG.BIN", &bootimg, &boot_size) == 0 &&
              boot_size == sizeof(fake),
              "its cluster chain is contiguous");
    }
    rescue_set_partition_path(part);
    host_write("flash recovery BOOTIMG.BIN confirm\necho FLASHED\n");
    usb_gadget_poll();
    resp[0] = '\0';
    read_back("RESPONSE.TXT", resp, sizeof(resp));
    check(strstr(resp, "written and verified byte for byte") != NULL,
          "the flash verb answers with the byte-for-byte proof");
    check(strstr(resp, "FLASHED") != NULL, "the batch nonce is echoed");
    check(rescue_last_result() == 1, "the verb reports a verified flash");
    check(read_back("QEAFBOOT.LOG", help, sizeof(help)) > 0 &&
          strstr(help, "lun: verified - recovery and BOOTIMG.BIN agree "
                       "byte for byte over 8192 bytes") != NULL,
          "QEAFBOOT.LOG carries the second proof line VQEAF Tool reads");
    f = fopen(part, "rb");
    if (f) {
        uint8_t got[8192];
        size_t n = fread(got, 1, sizeof(got), f);
        fclose(f);
        check(n == boot_size && memcmp(got, bootimg, boot_size) == 0,
              "the fixture partition really holds the staged bytes");
    } else {
        check(0, "the fixture partition exists after the flash");
    }

    /* Bad magic must be refused even with the exact safe verb. */
    {
        static uint8_t bad[8192];
        memset(bad, 0x11, sizeof(bad));
        memcpy(bad, "NOTBOOT!", 8);
        fat16_write_file(vol, "BOOTIMG.BIN", bad, sizeof(bad));
        host_write("flash recovery BOOTIMG.BIN confirm\necho BAD\n");
        usb_gadget_poll();
        resp[0] = '\0';
        read_back("RESPONSE.TXT", resp, sizeof(resp));
        check(strstr(resp, "REFUSED") != NULL &&
              strstr(resp, "magic") != NULL,
              "a payload without ANDROID! is refused before any write");
    }

    host_write("sync\necho DONE\n");
    usb_gadget_poll();
    resp[0] = '\0';
    read_back("RESPONSE.TXT", resp, sizeof(resp));
    check(strstr(resp, "DEVINFO.TXT republished") != NULL,
          "sync republishes the machine state");

    /* ---- screen rotation policy over the same rescue channel ------------- */
    {
        static uint16_t panel[16 * 32];
        gfx_init(panel, 16, 32, 16 * 2);
        check(display_parse_orientation("90") == DISPLAY_LANDSCAPE &&
              display_parse_orientation("auto") == -2 &&
              display_parse_orientation("45") == -1,
              "the rotate verb parses 0/90/180/270/auto and refuses junk");
        host_write("rotate 90\necho R1\n");
        usb_gadget_poll();
        check(display_orientation() == DISPLAY_LANDSCAPE &&
              gfx_rotation() == GFX_ROT_90,
              "rotate 90 drives the gfx transform");
        check(gfx_logical_width() == 32 && gfx_logical_height() == 16,
              "landscape swaps the logical surface dimensions");
        check(read_back("DEVINFO.TXT", help, sizeof(help)) > 0 &&
              strstr(help, "orientation=landscape") != NULL,
              "DEVINFO.TXT publishes the orientation for VQEAF Tool");
        resp[0] = '\0';
        read_back("RESPONSE.TXT", resp, sizeof(resp));
        check(strstr(resp, "orientation -> landscape") != NULL,
              "the rotate verb answers with the applied orientation");
        host_write("rotate auto\necho R2\n");
        usb_gadget_poll();
        check(display_auto_rotate() == 1, "rotate auto enables auto-rotate");
        display_feed_sensor(1000, 0);
        display_poll();
        check(display_orientation() == DISPLAY_LANDSCAPE,
              "a dominant +X gravity holds landscape");
        display_feed_sensor(0, -1000);
        display_poll();
        check(display_orientation() == DISPLAY_PORTRAIT_REV &&
              gfx_rotation() == GFX_ROT_180,
              "a dominant -Y gravity flips to portrait-rev");
        display_feed_sensor(0, 0);
        display_poll();
        check(display_orientation() == DISPLAY_PORTRAIT_REV,
              "a flat device holds its orientation instead of flipping");
        display_set_auto_rotate(0);
        display_set_orientation(DISPLAY_PORTRAIT);
    }

    /* ---- a host-written QEAF.CMD beyond root_entries_used ---------------- *
     * Windows never calls fat16_write_file: it appends a raw 8.3 entry at the
     * first free root slot (the 0x00 end marker, past the builder's high-water
     * mark) and allocates its own cluster. The loop must still read it. Before
     * the root-region scan fix this entry was invisible and the channel never
     * answered - which is exactly what the phone did while every host_write
     * above (builder-side, low slot) passed. */
    {
        static const char hostcmd[] = "status\necho HOSTSLOT\n";
        uint32_t hc = 200;                      /* a cluster the builder never used */
        uint32_t slot = vol->root_entries_used; /* first free slot, contiguous end */
        uint8_t *dst = vol->data +
            (size_t)(vol->data_start + (hc - 2) * vol->sectors_per_cluster) *
            vol->bytes_per_sector;
        uint8_t *fat = vol->data + (size_t)vol->fat_start * vol->bytes_per_sector;
        size_t fat2 = (size_t)vol->fat_size * vol->bytes_per_sector;
        uint8_t *root = vol->data +
            (size_t)vol->root_dir_start * vol->bytes_per_sector;
        uint8_t *e;
        uint32_t k;

        memcpy(dst, hostcmd, sizeof(hostcmd) - 1);
        fat[hc * 2] = 0xFF; fat[hc * 2 + 1] = 0xFF;
        fat[fat2 + hc * 2] = 0xFF; fat[fat2 + hc * 2 + 1] = 0xFF;

        /* Retire the builder-side QEAF.CMD so the only live one is the host's. */
        for (k = 0; k < vol->root_entries_used; k++) {
            e = root + k * 32;
            if (e[0] != 0xE5 && (e[11] & 0x0F) != 0x0F &&
                memcmp(e, "QEAF    CMD ", 11) == 0)
                e[0] = 0xE5;
        }
        e = root + slot * 32;
        memset(e, 0, 32);
        memcpy(e, "QEAF    CMD ", 11);
        e[11] = FAT16_ATTR_ARCHIVE;
        e[26] = (uint8_t)(hc & 0xFF); e[27] = (uint8_t)(hc >> 8);
        e[28] = (uint8_t)((sizeof(hostcmd) - 1) & 0xFF);
        e[29] = (uint8_t)(((sizeof(hostcmd) - 1) >> 8) & 0xFF);

        usb_gadget_poll();
        resp[0] = '\0';
        read_back("RESPONSE.TXT", resp, sizeof(resp));
        check(strstr(resp, "HOSTSLOT") != NULL &&
              strstr(resp, QEAF_OS_STRING) != NULL,
              "a host-appended QEAF.CMD past root_entries_used is answered");
    }

    /* ---- UI language switch over the same rescue channel ----------------- */
    {
        /* Negative control first: with no platform hook the verb must say so
         * rather than answer "applied" for a switch nothing performed. */
        rescue_set_language_hook(NULL);
        host_write("lang en\necho L0\n");
        usb_gadget_poll();
        resp[0] = '\0';
        read_back("RESPONSE.TXT", resp, sizeof(resp));
        check(strstr(resp, "lang: unavailable (no language hook)") != NULL,
              "lang without a platform hook answers unavailable, not applied");

        g_test_lang = -1;
        rescue_set_language_hook(test_language_hook);
        host_write("lang en\necho L1\n");
        usb_gadget_poll();
        resp[0] = '\0';
        read_back("RESPONSE.TXT", resp, sizeof(resp));
        check(g_test_lang == 1 && strstr(resp, "lang: applied en") != NULL,
              "lang en passes the core/i18n value 1 to the platform hook");

        host_write("lang vi\necho L2\n");
        usb_gadget_poll();
        check(g_test_lang == 0, "lang vi passes value 0");

        host_write("lang xx\necho L3\n");
        usb_gadget_poll();
        resp[0] = '\0';
        read_back("RESPONSE.TXT", resp, sizeof(resp));
        check(strstr(resp, "lang: unknown value") != NULL && g_test_lang == 0,
              "an unknown language is refused before the hook runs");
        rescue_set_language_hook(NULL);
    }

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
