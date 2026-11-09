/* SPDX-License-Identifier: MIT */
/* tests/prefs_test.c - the preference file gate.
 *
 * Exercises the same parser/writer the device runs, against a file in out/ so
 * a test never touches a real /etc. The checks are what the boot path relies
 * on: missing file, valid round-trip, partial file, corrupt numbers, and the
 * "write once" behaviour that keeps the shell from rewriting every frame.
 */

#include "core/prefs.h"
#include "core/sysio.h"

#include <stdio.h>

static int g_checks, g_fail;

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

int main(void) {
    const char *path = "out/prefs_test.conf";
    int lang, rot, aut;

    remove(path);
    prefs_set_path(path);

    lang = 1; rot = 0; aut = 0;
    check(prefs_load(&lang, &rot, &aut) == -1, "a missing file is reported");
    check(lang == 1 && rot == 0 && aut == 0,
          "a missing file leaves the caller's defaults alone");

    check(prefs_save(3, 2, 1) == 0, "a valid preference set is written");
    lang = rot = aut = -1;
    check(prefs_load(&lang, &rot, &aut) == 0, "the written file loads");
    check(lang == 3 && rot == 2 && aut == 1, "all three values round-trip");

    check(prefs_sync(3, 2, 1) == 0, "an unchanged state does not rewrite");
    check(prefs_sync(5, 2, 1) == 1, "a language change writes once");
    check(prefs_sync(5, 2, 1) == 0, "the same state does not write twice");

    lang = 0; rot = 3; aut = 1;
    check(prefs_load(&lang, &rot, &aut) == 0, "the rewritten file loads");
    check(lang == 5 && rot == 2 && aut == 1, "the change reached the file");

    check(sysio_write_str(path, "language=7\nbogus=nope\nauto_rotate=0\n") == 0,
          "a partial fixture is written");
    lang = 1; rot = 3; aut = 1;
    check(prefs_load(&lang, &rot, &aut) == 0, "a partial file still loads");
    check(lang == 7 && rot == 3 && aut == 0,
          "missing keys keep defaults, valid keys apply");

    check(sysio_write_str(path, "language=99\norientation=7\nauto_rotate=2\n") == 0,
          "an out-of-range fixture is written");
    lang = 1; rot = 0; aut = 0;
    check(prefs_load(&lang, &rot, &aut) == -1,
          "a file with no usable key is rejected");
    check(lang == 1 && rot == 0 && aut == 0,
          "out-of-range values are never clamped or applied");

    check(prefs_save(99, 0, 0) < 0 && prefs_save(0, 0, 2) < 0,
          "invalid values are refused before writing");

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
