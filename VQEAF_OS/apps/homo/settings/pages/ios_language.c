#include "ios_language.h"

#include "core/i18n.h"
#include "framework/desktop.h"
#include "framework/ui/i18n.h"

#include <stdint.h>
#include <stdio.h>

#define LANG_PAGE_W 582
#define LANG_PAGE_H 452
#define LANG_ROW_H 52
#define LANG_ROW_GAP 4

static lv_obj_t * language_root;
static lv_obj_t * language_rows[I18N_LANG_COUNT];
static lv_obj_t * language_checks[I18N_LANG_COUNT];
static int language_current = -1;

static void rebuild_desktop(void * unused)
{
    (void)unused;
    hachimi_desktop_destroy();
    ios_desktop_create();
}

static void language_clicked(lv_event_t * event)
{
    int lang = (int)(intptr_t)lv_event_get_user_data(event);

    if(lang == language_current) return;
    /* core/i18n is what prefs_sync() writes; homo_i18n drives the labels.
     * JA/ES/DE/KO/AUTO keep their stored value and draw English labels. */
    i18n_set((enum i18n_lang)lang);
    if(lang == HOMO_LANG_VI || lang == HOMO_LANG_EN || lang == HOMO_LANG_ZH)
        homo_i18n_set(lang);
    else
        homo_i18n_set(HOMO_LANG_EN);
    /* Deleting the tree from inside this click handler would free the button
     * under LVGL's own event dispatch. Run after the event returns. */
    lv_async_call(rebuild_desktop, NULL);
}

static lv_obj_t * make_label(lv_obj_t * parent, const char * text,
                             lv_coord_t x, lv_coord_t y, lv_coord_t width,
                             lv_coord_t height, lv_color_t color,
                             const lv_font_t * font)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static void language_row(lv_obj_t * parent, int lang, lv_coord_t y,
                         const lv_font_t * heading_font,
                         const lv_font_t * small_font)
{
    bool selected = (lang == language_current);
    bool automatic = (lang == HOMO_LANG_AUTO);

    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 14, y);
    lv_obj_set_size(card, lv_obj_get_width(language_root) - 28, LANG_ROW_H);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
    /* Screenshot: dark list, the selected row carries a green tint. */
    lv_obj_set_style_bg_color(card, selected ? lv_color_hex(0x14302a)
                                             : lv_color_hex(0x2a2f3a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    if(selected) {
        lv_obj_set_style_border_color(card, lv_color_hex(0x0f3d2e), LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    }
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, language_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)lang);

    /* Code chip on the left, like VI / EN / JA in the reference. */
    lv_obj_t * chip = lv_obj_create(card);
    lv_obj_remove_style_all(chip);
    lv_obj_set_pos(chip, 12, 12);
    lv_obj_set_size(chip, 40, 28);
    lv_obj_set_style_radius(chip, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chip, selected ? lv_color_hex(0x1f8a6b)
                                             : lv_color_hex(0x3a4a5a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    char code_buf[4];
    if(automatic) {
        lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        snprintf(code_buf, sizeof(code_buf), "%s", homo_i18n_lang_code(lang));
        lv_obj_t * code = lv_label_create(chip);
        lv_label_set_text(code, code_buf);
        lv_obj_center(code);
        lv_obj_set_style_text_font(code, small_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(code, lv_color_white(), LV_PART_MAIN);
        lv_obj_clear_flag(code, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_coord_t text_x = automatic ? 18 : 64;
    make_label(card, homo_i18n_lang_native(lang), text_x, 6, 360, 24,
               lv_color_white(), heading_font);
    if(automatic) {
        make_label(card, "Browser language detected automatically", text_x, 27, 360, 18,
                   lv_color_hex(0x9aa3b2), small_font);
    }
    else {
        make_label(card, homo_i18n_lang_name(lang), text_x, 27, 360, 18,
                   lv_color_hex(0x9aa3b2), small_font);
    }

    /* Green check on the right when this row is active. */
    lv_obj_t * check = lv_label_create(card);
    lv_label_set_text(check, LV_SYMBOL_OK);
    lv_obj_set_pos(check, lv_obj_get_width(card) - 44, 14);
    lv_obj_set_size(check, 32, 24);
    lv_obj_set_style_text_font(check, heading_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(check, selected ? lv_color_hex(0x2ecc8a)
                                                 : lv_color_hex(0x5a6577), LV_PART_MAIN);
    lv_obj_set_style_text_align(check, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_clear_flag(check, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if(!selected) lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);

    language_rows[lang] = card;
    language_checks[lang] = check;
}

void ios_language_create(lv_obj_t * parent, const lv_font_t * font,
                         const lv_font_t * small_font,
                         const lv_font_t * heading_font)
{
    (void)font;
    language_current = (int)i18n_selected();
    for(int i = 0; i < I18N_LANG_COUNT; i++) {
        language_rows[i] = NULL;
        language_checks[i] = NULL;
    }

    language_root = lv_obj_create(parent);
    lv_obj_remove_style_all(language_root);
    lv_obj_set_pos(language_root, 0, 0);
    lv_obj_set_size(language_root, lv_obj_get_width(parent), lv_obj_get_height(parent));
    lv_obj_set_style_bg_color(language_root, lv_color_hex(0x10131a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(language_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(language_root, LV_OBJ_FLAG_SCROLLABLE);

    make_label(language_root, "LANGUAGE", 18, 10, 200, 16,
               lv_color_hex(0x6b7686), small_font);
    make_label(language_root, "Language", 16, 24, 200, 30,
               lv_color_white(), heading_font);

    lv_coord_t y = 62;
    for(int lang = 0; lang < I18N_LANG_COUNT; lang++) {
        /* Screenshot order: VI, EN, JA, ZH, ES, DE, KO, Automatic. The enum
         * is VI, EN, JA, ZH, ES, DE, KO, AUTO, which matches. */
        language_row(language_root, lang, y, heading_font, small_font);
        y += LANG_ROW_H + LANG_ROW_GAP;
    }
}

void ios_language_refresh(void)
{
    /* Rows are rebuilt with the desktop on selection, so there is nothing to
     * repaint here; keep the entry point for the Settings hub contract. */
}
