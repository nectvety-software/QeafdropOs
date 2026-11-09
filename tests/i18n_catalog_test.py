#!/usr/bin/env python3
"""i18n gate: every Chinese UI string must have an English and Vietnamese row.

The desktop translates labels through an exact-match catalog installed with
lv_label_set_translate_cb (homo/framework/ui/i18n.c). A Chinese string that is
NOT in the catalog silently stays Chinese on the glass, and the host render
tests happily draw whatever the catalog returns - so a missing row is invisible
to every other gate. This one reads the same sources the linker sees and fails
when:

  * a CJK string literal in an app/framework/product source has no catalog row,
  * a catalog triple is malformed, empty, duplicated or not actually Chinese,
  * the launcher title path can fall back to a baked PNG instead of text
    (that fallback is what kept titles Chinese after switching language).

    python tests/i18n_catalog_test.py            # gate: exit 1 on any finding
    python tests/i18n_catalog_test.py --report   # print every missing string
"""

import argparse
import os
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOMO = os.path.join(ROOT, "homo")
I18N_C = os.path.join(HOMO, "framework", "ui", "i18n.c")
I18N_INC = os.path.join(HOMO, "framework", "ui", "i18n_apps.inc")

# Directories whose CJK literals are user-visible UI copy. lvgl and the linux
# platform backends are excluded; platform/violet is our own board layer.
SCAN_DIRS = ["apps", "framework", "product", "services", "custom",
             os.path.join("platform", "violet")]
SCAN_EXTS = (".c", ".h", ".cc", ".cpp")
# The catalog files ARE the translations; scanning them for coverage is a
# tautology (and their zh columns would always match themselves).
SCAN_EXCLUDE = {os.path.normcase(I18N_C), os.path.normcase(I18N_INC)}


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def has_cjk(text):
    for ch in text:
        o = ord(ch)
        if (0x3400 <= o <= 0x4DBF or 0x4E00 <= o <= 0x9FFF or
                0x3000 <= o <= 0x303F or 0xF900 <= o <= 0xFAFF or
                0xFF00 <= o <= 0xFFEF):
            return True
    return False


ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"',
           "'": "'", "0": "\0", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}


def decode_raw(raw):
    out, i, n = [], 0, len(raw)
    while i < n:
        ch = raw[i]
        if ch == "\\" and i + 1 < n:
            e = raw[i + 1]
            if e == "x":
                j, hx = i + 2, ""
                while j < n and len(hx) < 2 and raw[j] in "0123456789abcdefABCDEF":
                    hx += raw[j]
                    j += 1
                if hx:
                    out.append(chr(int(hx, 16)))
                    i = j
                    continue
            if e in "01234567":
                j, oc = i + 1, ""
                while j < n and len(oc) < 3 and raw[j] in "01234567":
                    oc += raw[j]
                    j += 1
                out.append(chr(int(oc, 8)))
                i = j
                continue
            out.append(ESCAPES.get(e, e))
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def strip_comments(src):
    """Blank // and /* */ comments while preserving string/char literals."""
    out, i, n, state = [], 0, len(src), "code"
    while i < n:
        ch = src[i]
        if state == "code":
            if ch == "/" and i + 1 < n and src[i + 1] == "/":
                state = "line"
                out.append("  ")
                i += 2
                continue
            if ch == "/" and i + 1 < n and src[i + 1] == "*":
                state = "block"
                out.append("  ")
                i += 2
                continue
            if ch == '"':
                state = "str"
                out.append(ch)
                i += 1
                continue
            if ch == "'":
                state = "char"
                out.append(ch)
                i += 1
                continue
            out.append(ch)
            i += 1
            continue
        if state == "line":
            if ch == "\n":
                state = "code"
                out.append("\n")
            else:
                out.append(" ")
            i += 1
            continue
        if state == "block":
            if ch == "*" and i + 1 < n and src[i + 1] == "/":
                state = "code"
                out.append("  ")
                i += 2
                continue
            out.append("\n" if ch == "\n" else " ")
            i += 1
            continue
        # string or char literal: copy verbatim, honouring escapes
        out.append(ch)
        if ch == "\\" and i + 1 < n:
            out.append(src[i + 1])
            i += 2
            continue
        if (state == "str" and ch == '"') or (state == "char" and ch == "'"):
            state = "code"
        i += 1
    return "".join(out)


def read_literal(src, pos):
    """Read one C string literal starting at src[pos]=='"'; return (text, next)."""
    n = len(src)
    raw, i = [], pos + 1
    while i < n:
        ch = src[i]
        if ch == "\\" and i + 1 < n:
            raw.append(ch)
            raw.append(src[i + 1])
            i += 2
            continue
        if ch == '"':
            return decode_raw("".join(raw)), i + 1
        raw.append(ch)
        i += 1
    return None, pos


def extract_literals(src):
    """All C string literals, with adjacent-literal concatenation applied."""
    lits, i, n = [], 0, len(src)
    while i < n:
        if src[i] == '"':
            text, j = read_literal(src, i)
            if text is None:
                break
            while True:
                k = j
                while k < n and src[k] in " \t\r\n":
                    k += 1
                if k < n and src[k] == '"':
                    more, j = read_literal(src, k)
                    if more is None:
                        break
                    text += more
                    continue
                j = k
                break
            lits.append(text)
            i = j
            continue
        if src[i] == "'":
            j = i + 1
            while j < n:
                if src[j] == "\\" and j + 1 < n:
                    j += 2
                    continue
                if src[j] == "'":
                    j += 1
                    break
                j += 1
            i = j
            continue
        i += 1
    return lits


def parse_triples(text):
    """Parse { "zh", "en", "vi" } initializers from comment-stripped text."""
    triples, i, n = [], 0, len(text)
    while i < n:
        if text[i] != "{":
            i += 1
            continue
        j = i + 1
        while j < n and text[j] in " \t\r\n":
            j += 1
        vals, ok = [], True
        for field in range(3):
            if j >= n or text[j] != '"':
                ok = False
                break
            val, j = read_literal(text, j)
            if val is None:
                ok = False
                break
            vals.append(val)
            while j < n and text[j] in " \t\r\n":
                j += 1
            if field < 2:
                if j < n and text[j] == ",":
                    j += 1
                    while j < n and text[j] in " \t\r\n":
                        j += 1
                else:
                    ok = False
                    break
        if ok:
            k = j
            while k < n and text[k] in " \t\r\n":
                k += 1
            if k < n and text[k] == "}":
                triples.append(tuple(vals))
                i = k + 1
                continue
        i += 1
    return triples


def catalog_region():
    src = read(I18N_C)
    start = src.index("CATALOG[] = {")
    end = src.index("\n};", start)
    return src[start:end] + "\n" + read(I18N_INC)


def coverage_keys():
    """Every zh string the catalog (and the app-name tables) can translate."""
    text = strip_comments(read(I18N_C)) + "\n" + strip_comments(read(I18N_INC))
    return {triple[0] for triple in parse_triples(text)}


def iter_sources():
    for rel in SCAN_DIRS:
        base = os.path.join(HOMO, rel)
        if not os.path.isdir(base):
            continue
        for dirpath, _dirs, files in os.walk(base):
            for name in files:
                if not name.endswith(SCAN_EXTS):
                    continue
                path = os.path.join(dirpath, name)
                if os.path.normcase(path) in SCAN_EXCLUDE:
                    continue
                yield path


def format_specifiers(text):
    """printf conversion specifiers in order, for template comparisons."""
    specs, i, n = [], 0, len(text)
    while i < n:
        if text[i] != "%":
            i += 1
            continue
        j = i + 1
        if j < n and text[j] == "%":
            i = j + 1
            continue
        while j < n and text[j] in "-+ #0":
            j += 1
        while j < n and text[j].isdigit():
            j += 1
        if j < n and text[j] == ".":
            j += 1
            while j < n and text[j].isdigit():
                j += 1
        while j < n and text[j] in "hljztL":
            j += 1
        if j < n:
            specs.append(text[j])
            j += 1
        i = j
    return specs


def check_catalog():
    problems, triples = [], parse_triples(strip_comments(catalog_region()))
    seen = {}
    for zh, en, vi in triples:
        if not zh or not en or not vi:
            problems.append("empty field in triple for %r" % zh)
            continue
        if not has_cjk(zh):
            problems.append("catalog key is not Chinese: %r" % zh)
        if zh in seen:
            problems.append("duplicate catalog key: %r" % zh)
        seen[zh] = (en, vi)
        # A template is translated BEFORE vsnprintf runs, so a row that drops
        # or reorders a conversion prints the wrong value (or reads a missing
        # argument) in that language only.
        want = format_specifiers(zh)
        for name, val in (("en", en), ("vi", vi)):
            got = format_specifiers(val)
            if got != want:
                problems.append(
                    "format specifiers differ (%s) for %r: %r vs %r"
                    % (name, zh, want, got))
    return problems, triples


def cjk_literals(src):
    """CJK-bearing string literals, comments already stripped."""
    return [lit for lit in extract_literals(src) if has_cjk(lit)]


def check_probe():
    """Negative control for the same scan the coverage pass uses.

    The planted string is not in the catalog by construction, so if the scanner
    cannot see it the coverage pass could never see a real regression either.
    """
    probe = 'static const char *i18n_probe = "此门禁探针不应存在于目录";\n'
    found = cjk_literals(strip_comments(probe))
    keys = coverage_keys()
    if not found:
        return ["PROBE: scanner observed no CJK in its own planted probe"]
    if any(lit in keys for lit in found):
        return ["PROBE: planted untranslated string matched the catalog"]
    return []


def check_title_is_text():
    """The launcher label must be able to resolve to text in every language.

    homo_i18n_app_name() returns NULL only for Chinese, and desktop.c turns a
    NULL into a baked label_image PNG. That is exactly the path that left home
    titles Chinese. Fail if the NULL fallback returns.
    """
    problems = []
    text = strip_comments(read(I18N_C))
    start = text.index("const char *homo_i18n_app_name(int app_id)")
    body = text[start:text.index("\n}\n", start)]
    if "return NULL" in body:
        problems.append("homo_i18n_app_name still returns NULL (PNG label fallback)")
    desktop = os.path.join(HOMO, "framework", "desktop.c")
    if os.path.isfile(desktop):
        dsrc = strip_comments(read(desktop))
        if "homo_i18n_app_name" not in dsrc:
            problems.append("desktop.c no longer resolves tile labels via "
                            "homo_i18n_app_name")
    return problems


def collect():
    """Return (keys, missing, total_cjk, file_count).

    total_cjk is the observed sample size: a gate that scans 0 CJK literals
    would trivially report nothing missing, so the count is printed and a zero
    sample is itself a failure.
    """
    keys = coverage_keys()
    missing = {}
    total_cjk = 0
    file_count = 0
    for path in iter_sources():
        file_count += 1
        rel = os.path.relpath(path, ROOT)
        for lit in cjk_literals(strip_comments(read(path))):
            total_cjk += 1
            if lit not in keys:
                missing.setdefault(rel, set()).add(lit)
    return keys, missing, total_cjk, file_count


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", action="store_true",
                    help="print every missing string, never fail")
    args = ap.parse_args(argv)

    catalog_problems, triples = check_catalog()
    for problem in check_probe():
        catalog_problems.append(problem)
    title_problems = check_title_is_text()
    keys, missing, total_cjk, files = collect()

    total_missing = sum(len(v) for v in missing.values())
    print("catalog: %d triples, %d unique zh keys" % (len(triples), len(keys)))
    print("sources: scanned %d file(s), %d CJK literal(s) observed"
          % (files, total_cjk))
    print("sources: %d CJK literal(s) missing a translation across %d file(s)"
          % (total_missing, len(missing)))
    if total_cjk == 0:
        catalog_problems.append("scanned 0 CJK literals - the scanner observed "
                                "nothing, so PASS would be meaningless")

    for problem in catalog_problems:
        print("CATALOG: %s" % problem)
    for problem in title_problems:
        print("TITLE  : %s" % problem)

    if missing:
        for rel in sorted(missing):
            print("--- %s" % rel)
            for lit in sorted(missing[rel]):
                shown = lit.replace("\n", "\\n")
                print("  %r" % shown)

    failed = bool(catalog_problems or title_problems or (missing and not args.report))
    print("RESULT: %s" % ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
