#ifndef HACHIMI_I18N_H
#define HACHIMI_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

/* Values match core/i18n.h. The label catalog ships copy for VI, EN and ZH;
 * every other selection (including AUTO) falls back to English labels while
 * the stored preference still records the choice. */
#define HOMO_LANG_VI   0
#define HOMO_LANG_EN   1
#define HOMO_LANG_JA   2
#define HOMO_LANG_ZH   3
#define HOMO_LANG_ES   4
#define HOMO_LANG_DE   5
#define HOMO_LANG_KO   6
#define HOMO_LANG_AUTO 7

/* Installs the LVGL label hook, so every label created after this call is
 * translated without touching each app's drawing code. */
void homo_i18n_attach(void);

/* Select the language used by labels created from now on. Changing it does
 * not retranslate existing labels; rebuild the desktop to apply it. */
void homo_i18n_set(int lang);
int homo_i18n_get(void);

/* Exact-match catalog lookup. Unknown strings (including Chinese content that
 * has no translation yet) are returned unchanged. */
const char *homo_i18n_tr(const char *text);

/* Launcher name for a hachimi_app_id_t. Always non-NULL: the launcher draws
 * every tile name as text so it follows the selected language. */
const char *homo_i18n_app_name(int app_id);

/* Text title for the launch overlay. Unlike app_name, this returns the
 * original Chinese too, because the title is drawn as text in every language. */
const char *homo_i18n_app_title(int app_id);

/* Native menu label, English name and chip code. */
const char *homo_i18n_lang_native(int lang);
const char *homo_i18n_lang_name(int lang);
const char *homo_i18n_lang_code(int lang);

#ifdef __cplusplus
}
#endif

#endif
