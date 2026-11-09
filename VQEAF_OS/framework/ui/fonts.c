#include "fonts.h"
#include "libs/tiny_ttf/lv_tiny_ttf.h"
#include <stddef.h>

/* Ubuntu (Google Fonts, OFL 1.1) is the system face: it covers the full
 * Latin/Vietnamese set including precomposed diacritics, so Vietnamese
 * labels no longer depend on a fallback. Liberation Sans stays chained as
 * the second fallback for glyphs Ubuntu is missing; PingFang (plus the
 * bitmap Han face) renders the Chinese text that keeps its original wording
 * and is the last resort for CJK. */
#define UBUNTU_TTF "A:assets/ios/Ubuntu-Regular.ttf"
#define LATIN_TTF  "A:assets/ios/LiberationSans-Regular.ttf"
#define CJK_TTF    "A:assets/ios/PingFang.ttf"

static const lv_font_t * build_face(unsigned size)
{
    lv_font_t * cjk = lv_tiny_ttf_create_file(CJK_TTF, (int32_t)size);
    lv_font_t * latin = lv_tiny_ttf_create_file(LATIN_TTF, (int32_t)size);
    lv_font_t * ubuntu = lv_tiny_ttf_create_file(UBUNTU_TTF, (int32_t)size);
    if(cjk != NULL) cjk->fallback = &lv_font_source_han_sans_sc_14_cjk;
    if(latin != NULL)
        latin->fallback = cjk != NULL ? cjk : &lv_font_source_han_sans_sc_14_cjk;
    if(ubuntu != NULL) {
        ubuntu->fallback = latin != NULL ? latin
                         : (cjk != NULL ? cjk : &lv_font_source_han_sans_sc_14_cjk);
        return ubuntu;
    }
    if(latin != NULL) return latin;
    if(cjk != NULL) return cjk;
    return &lv_font_source_han_sans_sc_14_cjk;
}

/* Cached for the lifetime of the desktop; every app uses the same typefaces. */
const lv_font_t * hachimi_ui_font(unsigned size)
{
    static const unsigned sizes[] = {12, 14, 15, 16, 18, 22, 28, 32, 42, 56, 64};
    static const lv_font_t * fonts[sizeof(sizes) / sizeof(sizes[0])];
    size_t index = 0;
    for(size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        if(sizes[i] <= size) index = i;
    }
    if(fonts[index] == NULL) fonts[index] = build_face(sizes[index]);
    return fonts[index];
}
