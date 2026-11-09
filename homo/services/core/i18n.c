/* SPDX-License-Identifier: MIT */
/* core/i18n.c - the four translated vocabularies and the language menu names.
 *
 * Vietnamese, Spanish and German fit the ASCII 5x7 face when written without
 * diacritics. Japanese, Chinese and Korean have no glyphs in that face yet, so
 * their rows are offered (the preference is real) and their copy resolves to
 * English instead of drawing empty boxes.
 */

#include "core/i18n.h"

#include <stddef.h>

enum { T_VI = 0, T_EN, T_ES, T_DE, T_COUNT };

static const char *const STRINGS[T_COUNT][I18N_KEY_COUNT] = {
    /* Vietnamese (ASCII transliteration; the face has no precomposed glyphs) */
    {
        "Khong co su kien hom nay",
        "Tim kiem",
        "Da khoa",
        "Khong co thong bao",
        "Cai dat",
        "Ngon ngu",
        "Tu dong",
        "Phat hien tu he thong",
        "Wi-Fi",
        "Bluetooth",
        "Ban phim",
        "Xoay man hinh",
        "Tat ca tinh nang",
        "THOI GIAN (UTC)",
        "TU KHI KHOI DONG",
        "Ap dung cho phien nay.",
    },
    /* English */
    {
        "No events today",
        "Search",
        "Locked",
        "No notifications",
        "Settings",
        "Language",
        "Automatic",
        "Detected from the system",
        "Wi-Fi",
        "Bluetooth",
        "Keyboard",
        "Display rotation",
        "All features",
        "TIME (UTC)",
        "UP SINCE BOOT",
        "Applies to this session.",
    },
    /* Spanish */
    {
        "No hay eventos hoy",
        "Buscar",
        "Bloqueado",
        "Sin notificaciones",
        "Ajustes",
        "Idioma",
        "Automatico",
        "Detectado del sistema",
        "Wi-Fi",
        "Bluetooth",
        "Teclado",
        "Rotacion de pantalla",
        "Todas las funciones",
        "HORA (UTC)",
        "DESDE EL ARRANQUE",
        "Se aplica a esta sesion.",
    },
    /* German */
    {
        "Keine Termine heute",
        "Suchen",
        "Gesperrt",
        "Keine Benachrichtigungen",
        "Einstellungen",
        "Sprache",
        "Automatisch",
        "Vom System erkannt",
        "Wi-Fi",
        "Bluetooth",
        "Tastatur",
        "Bildschirm drehen",
        "Alle Funktionen",
        "ZEIT (UTC)",
        "SEIT DEM START",
        "Gilt fuer diese Sitzung.",
    },
};

static enum i18n_lang g_lang = I18N_VI;

static int table_of(enum i18n_lang lang) {
    switch (lang) {
    case I18N_VI: return T_VI;
    case I18N_ES: return T_ES;
    case I18N_DE: return T_DE;
    default:      return T_EN;   /* EN, JA, ZH, KO, AUTO */
    }
}

void i18n_set(enum i18n_lang lang) {
    if (lang < 0 || lang >= I18N_LANG_COUNT) return;
    g_lang = lang;
}

enum i18n_lang i18n_selected(void) { return g_lang; }

enum i18n_lang i18n_effective(void) {
    /* AUTO and the CJK entries have no drawable vocabulary yet: the strings
     * actually on the glass are English, and this function must say so. */
    switch (g_lang) {
    case I18N_VI:
    case I18N_EN:
    case I18N_ES:
    case I18N_DE:
        return g_lang;
    default:
        return I18N_EN;
    }
}

const char *i18n_t(enum i18n_key key) {
    int t;
    if (key < 0 || key >= I18N_KEY_COUNT) return "";
    t = table_of(g_lang);
    return STRINGS[t][key] ? STRINGS[t][key] : STRINGS[T_EN][key];
}

const char *i18n_name(enum i18n_lang lang) {
    switch (lang) {
    case I18N_VI:   return "Vietnamese";
    case I18N_EN:   return "English";
    case I18N_JA:   return "Japanese";
    case I18N_ZH:   return "Chinese";
    case I18N_ES:   return "Spanish";
    case I18N_DE:   return "German";
    case I18N_KO:   return "Korean";
    case I18N_AUTO: return "Automatic";
    default:        return "";
    }
}

const char *i18n_native(enum i18n_lang lang) {
    switch (lang) {
    case I18N_VI:   return "Tieng Viet";
    case I18N_EN:   return "English";
    case I18N_JA:   return "Nihongo";
    case I18N_ZH:   return "Zhongwen";
    case I18N_ES:   return "Espanol";
    case I18N_DE:   return "Deutsch";
    case I18N_KO:   return "Hangugeo";
    case I18N_AUTO: return "System language";
    default:        return "";
    }
}

const char *i18n_code(enum i18n_lang lang) {
    switch (lang) {
    case I18N_VI:   return "VI";
    case I18N_EN:   return "EN";
    case I18N_JA:   return "JA";
    case I18N_ZH:   return "ZH";
    case I18N_ES:   return "ES";
    case I18N_DE:   return "DE";
    case I18N_KO:   return "KO";
    case I18N_AUTO: return "AUTO";
    default:        return "";
    }
}
