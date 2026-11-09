#include "lvgl.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

static lv_label_translate_cb_t installed;

void lv_label_set_translate_cb(lv_label_translate_cb_t cb)
{
    installed = cb;
}

static int failures;

static void expect(const char * what, const char * got, const char * want)
{
    if((got == NULL && want == NULL) ||
       (got != NULL && want != NULL && strcmp(got, want) == 0)) {
        printf("ok    %s\n", what);
        return;
    }
    printf("FAIL  %s: got '%s', want '%s'\n", what,
           got == NULL ? "(null)" : got, want == NULL ? "(null)" : want);
    failures++;
}

int main(void)
{
    expect("shipped default language is Vietnamese",
           homo_i18n_get() == HOMO_LANG_VI ? "VI" : "other", "VI");

    homo_i18n_set(HOMO_LANG_ZH);
    expect("Chinese source stays Chinese", homo_i18n_tr("设置"), "设置");
    expect("Chinese title is text", homo_i18n_app_title(0), "浏览器");
    expect("Chinese launcher name is text", homo_i18n_app_name(0), "浏览器");

    homo_i18n_set(HOMO_LANG_EN);
    homo_i18n_attach();
    expect("hook installed", installed == homo_i18n_tr ? "yes" : "no", "yes");
    expect("shell setting", homo_i18n_tr("设置"), "Settings");
    expect("app setting", homo_i18n_tr("屏幕亮度"), "Screen brightness");
    expect("calendar header", homo_i18n_tr("%d年%d月%d日"), "%d/%d/%d");
    expect("browser demo", homo_i18n_tr("浏览器示范页"), "Browser demo");
    expect("mail sample", homo_i18n_tr("欢迎使用 Hachimi"), "Welcome to Hachimi");
    expect("weather condition", homo_i18n_tr("晴"), "Clear");
    expect("service error", homo_i18n_tr("请先配置天气 API 密钥"),
           "Configure the weather API key first");
    expect("unknown text falls back", homo_i18n_tr("未翻译的数据"), "未翻译的数据");
    expect("English title", homo_i18n_app_title(12), "Settings");
    expect("English app name", homo_i18n_app_name(0), "Browser");

    homo_i18n_set(HOMO_LANG_VI);
    expect("Vietnamese shell", homo_i18n_tr("设置"), "Cài đặt");
    expect("Vietnamese app", homo_i18n_tr("消息"), "Tin nhắn");
    expect("Vietnamese title", homo_i18n_app_title(0), "Trình duyệt");
    expect("Vietnamese launcher name", homo_i18n_app_name(0), "Trình duyệt");
    expect("Vietnamese diacritics survive a template", homo_i18n_tr("%d月"),
           "%02d");
    expect("Vietnamese native name", homo_i18n_lang_native(HOMO_LANG_VI),
           "Tiếng Việt");
    expect("native Chinese row", homo_i18n_lang_native(HOMO_LANG_ZH), "中文");

    homo_i18n_set(99);
    expect("unsupported language uses English", homo_i18n_tr("设置"), "Settings");

    printf("\n%d failures\n", failures);
    return failures != 0;
}
