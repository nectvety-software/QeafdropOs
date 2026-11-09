#include "homo_theme.h"

const char *homo_theme_name(homo_theme_id_t id)
{
    switch (id) {
    case HOMO_THEME_VQEAF20:       return "VQEAF OS 2.0";
    case HOMO_THEME_MODERN_DARK:   return "VQEAF Midnight";
    case HOMO_THEME_CLAUDE_DAY:    return "Claude S40 Day";
    case HOMO_THEME_CLAUDE_NIGHT:  return "Claude S40 Night";
    default:                       return "VQEAF Night";
    }
}

void homo_theme_get(homo_theme_id_t id, homo_theme_t *out)
{
    if (out == NULL) return;
    switch (id) {
    case HOMO_THEME_VQEAF20:
        *out = (homo_theme_t){
            .bg = lv_color_hex(0x000000), .panel = lv_color_hex(0x000000),
            .selected = lv_color_hex(0x006FCE), .text = lv_color_hex(0xFFFFFF),
            .dim = lv_color_hex(0xB2B2B2), .chrome = lv_color_hex(0x1A1A1A),
            .chrome_text = lv_color_hex(0xFFFFFF), .accent = lv_color_hex(0x4C9ADD),
            .danger = lv_color_hex(0xFF4C00), .popup = lv_color_hex(0x000000),
            .popup_text = lv_color_hex(0xFFFFFF), .popup_selected = lv_color_hex(0x006FCE),
            .border = lv_color_hex(0x2A2A2A),
        };
        break;
    case HOMO_THEME_MODERN_DARK:
        *out = (homo_theme_t){
            .bg = lv_color_hex(0x0B0F18), .panel = lv_color_hex(0x151C2B),
            .selected = lv_color_hex(0x263652), .text = lv_color_hex(0xECF4FF),
            .dim = lv_color_hex(0x9EADCA), .chrome = lv_color_hex(0x101826),
            .chrome_text = lv_color_hex(0xECF4FF), .accent = lv_color_hex(0x84B2FF),
            .danger = lv_color_hex(0xFF5477), .popup = lv_color_hex(0x151E30),
            .popup_text = lv_color_hex(0xF7BF), .popup_selected = lv_color_hex(0x283A60),
            .border = lv_color_hex(0x354664),
        };
        break;
    case HOMO_THEME_CLAUDE_DAY:
        *out = (homo_theme_t){
            .bg = lv_color_hex(0xFAF6EF), .panel = lv_color_hex(0xFFFFFF),
            .selected = lv_color_hex(0xF6E3D9), .text = lv_color_hex(0x26252C),
            .dim = lv_color_hex(0x7C766B), .chrome = lv_color_hex(0x26252C),
            .chrome_text = lv_color_hex(0xFFFFFF), .accent = lv_color_hex(0xC96442),
            .danger = lv_color_hex(0xB3261E), .popup = lv_color_hex(0xFFFFFF),
            .popup_text = lv_color_hex(0x26252C), .popup_selected = lv_color_hex(0xF6E3D9),
            .border = lv_color_hex(0xE4DCCD),
        };
        break;
    case HOMO_THEME_CLAUDE_NIGHT:
        *out = (homo_theme_t){
            .bg = lv_color_hex(0x141417), .panel = lv_color_hex(0x25252B),
            .selected = lv_color_hex(0x33272A), .text = lv_color_hex(0xECE8E1),
            .dim = lv_color_hex(0x9A958C), .chrome = lv_color_hex(0x0B0B0D),
            .chrome_text = lv_color_hex(0xF4EFE6), .accent = lv_color_hex(0xE08A6B),
            .danger = lv_color_hex(0xFF8A7A), .popup = lv_color_hex(0x25252B),
            .popup_text = lv_color_hex(0xECE8E1), .popup_selected = lv_color_hex(0x33272A),
            .border = lv_color_hex(0x34343C),
        };
        break;
    default: /* HOMO_THEME_NIGHT */
        *out = (homo_theme_t){
            .bg = lv_color_hex(0x000000), .panel = lv_color_hex(0x080808),
            .selected = lv_color_hex(0x006FCE), .text = lv_color_hex(0xFFFFFF),
            .dim = lv_color_hex(0xAAAAAA), .chrome = lv_color_hex(0x101010),
            .chrome_text = lv_color_hex(0xFFFFFF), .accent = lv_color_hex(0x4C9ADD),
            .danger = lv_color_hex(0xFF4C00), .popup = lv_color_hex(0x080808),
            .popup_text = lv_color_hex(0xFFFFFF), .popup_selected = lv_color_hex(0x006FCE),
            .border = lv_color_hex(0x2A2A2A),
        };
        break;
    }
}
