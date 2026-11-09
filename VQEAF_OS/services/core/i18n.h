/* SPDX-License-Identifier: MIT */
/* core/i18n.h - system language for the shell screens.
 *
 * The UI font is the 5x7 ASCII face, so a language entry means "the system
 * language", not "every glyph in that script": Japanese, Chinese and Korean
 * rows exist and are selectable, but their copy falls back to English until a
 * glyph set ships. Vocabulary is deliberately small - the labels the launcher,
 * lock screen and settings hub actually draw - and a key that is missing falls
 * back to English rather than printing its enum name.
 */

#ifndef UMOS_I18N_H
#define UMOS_I18N_H

enum i18n_lang {
    I18N_VI = 0,
    I18N_EN,
    I18N_JA,
    I18N_ZH,
    I18N_ES,
    I18N_DE,
    I18N_KO,
    I18N_AUTO,
    I18N_LANG_COUNT
};

enum i18n_key {
    I18N_GLANCE = 0,
    I18N_SEARCH,
    I18N_LOCKED,
    I18N_NO_NOTIF,
    I18N_SETTINGS,
    I18N_LANGUAGE,
    I18N_AUTO_LABEL,
    I18N_AUTO_SUB,
    I18N_WIFI,
    I18N_BT,
    I18N_KEYBOARD,
    I18N_ROTATION,
    I18N_FEATURES,
    I18N_TIME,
    I18N_UP_SINCE_BOOT,
    I18N_LANG_HINT,
    I18N_KEY_COUNT
};

/* Stored preference; I18N_AUTO resolves to English because this board has no
 * browser or locale service to ask. */
void i18n_set(enum i18n_lang lang);
enum i18n_lang i18n_selected(void);
/* The language whose strings are actually drawn (AUTO -> English). */
enum i18n_lang i18n_effective(void);

const char *i18n_t(enum i18n_key key);
const char *i18n_name(enum i18n_lang lang);   /* menu title */
const char *i18n_native(enum i18n_lang lang); /* second line */
const char *i18n_code(enum i18n_lang lang);   /* chip: VI, EN, ... AUTO */

#endif
