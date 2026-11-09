#include "control_center_controls.h"
#include "control_center.h"
#include "fonts.h"
#include "i18n.h"
#include "ios_icon_assets.h"
#include "framework/desktop.h"
#include "hal/include/display.h"
#include "services/display.h"
#include "services/music.h"
#include "services/network.h"
#include "services/power.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef enum { PREVIOUS, PLAY, NEXT } icon_t;
static lv_obj_t * content, * date_label, * wifi_status, * battery;
static lv_obj_t * network_cards[2], * network_icons[2], * network_titles[2], * network_subtitles[2];
static lv_obj_t * brightness_slider, * volume_slider;
static lv_obj_t * sliders[2], * slider_icons[2];
static lv_obj_t * player, * music_icon, * music_label;
static lv_obj_t * orientation_card, * orientation_state;
static lv_obj_t * orient_icon_label;
static lv_obj_t * quick_cards[8], * quick_titles[8], * quick_subs[8];
static int display_w_cache = 800, display_h_cache = 480;
static bool focus_on, flashlight_on, player_shown;
static lv_obj_t * track_title, * track_artist, * track_progress, * player_buttons[3], * play_icon, * feedback;
static lv_timer_t * timer;
static hachimi_network_service_t network;
static uint32_t last_media, last_system, last_network;
static int brightness = 70;
static bool visible, preview, network_known, preview_wifi, preview_ethernet, playing;

static void label_set(lv_obj_t * label, const char * text)
{
    if(strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
static const char *quick_names[8] = {"Calculator", "Focus", "Alarm", "Flashlight", "VPN", "Voice Memo", "Settings", "Translate"};
static const char *quick_marks[8] = {"=", "F", "A", "L", "V", "M", "S", "T"};

static void render_orientation(void)
{
    if(orientation_state == NULL) return;
    bool locked = !display_auto_rotate();
    label_set(orientation_state, locked ? "Locked" : "Unlocked");
    if(orient_icon_label != NULL)
        lv_obj_set_style_text_color(orient_icon_label,
            lv_color_hex(locked ? 0xff5a5a : 0x9aa3b2), 0);
}

static void render_quick_state(void)
{
    if(quick_subs[1] != NULL) label_set(quick_subs[1], focus_on ? "On" : "Off");
    if(quick_subs[3] != NULL) label_set(quick_subs[3], flashlight_on ? "On" : "Off");
}

static void orientation_clicked(lv_event_t * event)
{
    (void)event;
    display_set_auto_rotate(display_auto_rotate() ? 0 : 1);
    render_orientation();
}

static void quick_clicked(lv_event_t * event)
{
    unsigned idx = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    label_set(feedback, "");
    if(idx == 0) { hachimi_control_center_close(); (void)hachimi_desktop_launch(HACHIMI_APP_CALCULATOR); return; }
    if(idx == 1) { focus_on = !focus_on; render_quick_state(); return; }
    if(idx == 2) { hachimi_control_center_close(); (void)hachimi_desktop_launch(HACHIMI_APP_CLOCK); return; }
    if(idx == 3) { flashlight_on = !flashlight_on; render_quick_state(); label_set(feedback, flashlight_on ? "Flashlight on (demo)" : "Flashlight off"); return; }
    if(idx == 6) { hachimi_control_center_close(); (void)hachimi_desktop_launch(HACHIMI_APP_SETTINGS); return; }
    if(idx == 4 || idx == 5 || idx == 7) { label_set(feedback, "Not available on this board"); return; }
    label_set(feedback, "Not available");
}

static lv_obj_t * label(lv_obj_t * parent, const char * text, int x, int y, int w, int h, unsigned size, uint32_t color)
{
    lv_obj_t * object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y); lv_obj_set_size(object, w, h);
    lv_obj_set_style_text_font(object, hachimi_ui_font(size), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(object, text);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
static lv_obj_t * panel(lv_obj_t * parent, int x, int y, int w, int h, int radius)
{
    lv_obj_t * object = lv_obj_create(parent); lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y); lv_obj_set_size(object, w, h);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_bg_color(object, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(object, 42, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_white(), 0);
    lv_obj_set_style_border_opa(object, 48, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return object;
}
static lv_obj_t * asset_icon(lv_obj_t * parent, const lv_image_dsc_t * source, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t * object=lv_image_create(parent);
    lv_image_set_src(object,source); lv_obj_set_pos(object,x,y); lv_obj_set_size(object,w,h);
    lv_image_set_inner_align(object,LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_style_image_recolor(object,lv_color_hex(color),0);
    lv_obj_set_style_image_recolor_opa(object,LV_OPA_COVER,0);
    lv_obj_remove_flag(object,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
static void line(lv_layer_t * layer, int x1, int y1, int x2, int y2, int width, lv_color_t color)
{
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = color; d.width = width; d.round_start = d.round_end = 1;
    d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2; lv_draw_line(layer, &d);
}
static void triangle(lv_layer_t * layer, int x, int y, int direction, lv_color_t color)
{
    lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d); d.color = color;
    d.p[0] = (lv_point_precise_t){x - 7 * direction, y - 10};
    d.p[1] = (lv_point_precise_t){x - 7 * direction, y + 10};
    d.p[2] = (lv_point_precise_t){x + 9 * direction, y}; lv_draw_triangle(layer, &d);
}
static void draw_icon(lv_event_t * event)
{
    lv_obj_t * object = lv_event_get_target(event); lv_area_t area; lv_obj_get_coords(object, &area);
    int x = (area.x1 + area.x2) / 2, y = (area.y1 + area.y2) / 2;
    lv_layer_t * layer = lv_event_get_layer(event);
    lv_color_t color = lv_obj_get_style_text_color(object, 0);
    icon_t icon = (icon_t)(uintptr_t)lv_event_get_user_data(event);
    if(icon == PLAY) {
        if(playing) { line(layer,x-5,y-10,x-5,y+10,4,color); line(layer,x+5,y-10,x+5,y+10,4,color); }
        else triangle(layer,x,y,1,color);
    }
    else if(icon == PREVIOUS || icon == NEXT) {
        int direction = icon == NEXT ? 1 : -1;
        triangle(layer,x,y,direction,color); line(layer,x+11*direction,y-10,x+11*direction,y+10,3,color);
    }
}
static lv_obj_t * icon(lv_obj_t * parent, icon_t kind, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t * object = lv_obj_create(parent); lv_obj_remove_style_all(object);
    lv_obj_set_pos(object,x,y); lv_obj_set_size(object,w,h);
    lv_obj_set_style_text_color(object,lv_color_hex(color),0);
    lv_obj_remove_flag(object,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(object,draw_icon,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)kind); return object;
}
static void render_network(void)
{
    bool enabled[] = {preview ? preview_wifi : network.state.wifi_enabled,
                      preview ? preview_ethernet : network.state.ethernet_enabled};
    bool present[] = {preview || network.state.wifi_present, preview || network.state.ethernet_present};
    const char * wifi = !network_known ? (network.job.running ? "读取中…" : "状态不可用") : !present[0] ? "未检测到网卡" :
        !enabled[0] ? "已关闭" : network.state.ssid[0] ? "已连接" : "未连接";
    const char * ethernet = !network_known ? (network.job.running ? "读取中…" : "状态不可用") : !present[1] ? "未检测到接口" :
        !enabled[1] ? "已关闭" : network.state.ethernet_connected ? network.state.ethernet_ip : "网线未连接";
    if(preview) { wifi = enabled[0] ? "预览 · 已开启" : "预览 · 已关闭";
                 ethernet = enabled[1] ? "预览 · 已开启" : "预览 · 已关闭"; }
    label_set(network_titles[0], !preview && enabled[0] && network.state.ssid[0] ? network.state.ssid : "Wi-Fi");
    label_set(network_titles[1],"以太网");
    label_set(network_subtitles[0],wifi); label_set(network_subtitles[1],ethernet);
    for(unsigned i=0;i<2;i++) {
        bool active=enabled[i] && present[i] && network_known;
        lv_obj_set_style_bg_opa(network_cards[i],active ? 235 : 42,0);
        lv_obj_set_style_text_color(network_titles[i],lv_color_hex(active ? 0x202831 : 0xffffff),0);
        lv_obj_set_style_text_color(network_subtitles[i],lv_color_hex(active ? 0x6c7681 : 0xc5d0da),0);
        lv_color_t color=lv_color_hex(active ? (i ? 0x29b963 : 0x277ef2) : 0xffffff);
        lv_obj_set_style_image_recolor(network_icons[i],color,0);
        if(network.job.running || !present[i] || !network_known) lv_obj_add_state(network_cards[i],LV_STATE_DISABLED);
        else lv_obj_remove_state(network_cards[i],LV_STATE_DISABLED);
    }
    lv_obj_set_style_image_opa(wifi_status,enabled[0] && present[0] ? LV_OPA_COVER : LV_OPA_40,0);
}
static void network_clicked(lv_event_t * event)
{
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    if(network.job.running) return;
    if(preview) { if(index) preview_ethernet=!preview_ethernet; else preview_wifi=!preview_wifi; render_network(); return; }
    bool on=index ? network.state.ethernet_enabled : network.state.wifi_enabled;
    const char * operation=index ? (on ? "ethernet-off" : "ethernet-on") : (on ? "wifi-off" : "wifi-on");
    label_set(feedback,"");
    if(!hachimi_network_request(&network,operation,NULL)) label_set(feedback,"无法启动网络操作，请稍后重试");
    render_network();
}
static void update_media(void)
{
    hachimi_music_state_t state; hachimi_music_state(&state);
    const hachimi_music_track_t * track=state.count ? hachimi_music_track(state.index) : NULL;
    /* Now-playing chỉ hiện khi đang phát (mở app Nhạc rồi nhấn phát), giống
     * iOS/Android: không phát thì ẩn, layout dồn lên để lộ hàng dưới. */
    /* Landscape (2048x480 test): giữ player luôn hiện để gate cũ. Portrait
     * như iOS/Android: chỉ hiện khi đang phát. */
    bool portrait = display_h_cache > display_w_cache;
    bool should_show = portrait ? (track != NULL && state.playing) : true;
    if(player != NULL && should_show != player_shown) {
        player_shown = should_show;
        if(should_show) lv_obj_remove_flag(player, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(player, LV_OBJ_FLAG_HIDDEN);
        hachimi_control_center_controls_layout(display_w_cache, display_h_cache);
    }
    label_set(track_title,track && should_show ? track->title : "Now Playing");
    label_set(track_artist,track && should_show ? (track->artist[0] ? track->artist : "Local music") : "");
    if(playing!=state.playing) { playing=state.playing; lv_obj_invalidate(play_icon); }
    if(!lv_slider_is_dragged(volume_slider)) lv_slider_set_value(volume_slider,(int)state.volume,LV_ANIM_OFF);
    int percent=state.playing && state.duration_ms ? (int)((uint64_t)state.position_ms*1000/state.duration_ms) : 0;
    lv_bar_set_value(track_progress,percent,LV_ANIM_OFF);
    for(unsigned i=0;i<3;i++) {
        lv_obj_set_style_opa(player_buttons[i],track ? LV_OPA_COVER : LV_OPA_40,0);
        if(track) lv_obj_remove_state(player_buttons[i],LV_STATE_DISABLED);
        else lv_obj_add_state(player_buttons[i],LV_STATE_DISABLED);
    }
}
static void media_clicked(lv_event_t * event)
{
    hachimi_music_state_t state; hachimi_music_state(&state);
    if(!state.count) return;
    unsigned action=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    if(action==1) hachimi_music_set_playing(!state.playing);
    else hachimi_music_next(action==2);
    hachimi_music_state(&state); label_set(feedback,state.error); update_media();
}
static void brightness_changed(lv_event_t * event)
{
    lv_event_code_t code=lv_event_get_code(event);
    int value=lv_slider_get_value(brightness_slider);
    if(value<HACHIMI_DISPLAY_MIN_PERCENT) { value=HACHIMI_DISPLAY_MIN_PERCENT; lv_slider_set_value(brightness_slider,value,LV_ANIM_OFF); }
    if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {
        if(preview || hachimi_display_set_brightness(value)) brightness=value;
        else { label_set(feedback,"亮度调整失败"); lv_slider_set_value(brightness_slider,brightness,LV_ANIM_OFF); }
    }
}
static void volume_changed(lv_event_t * event)
{
    (void)event; unsigned value=(unsigned)lv_slider_get_value(volume_slider); hachimi_music_volume(value);
}
static void update_system(void)
{
    static const char * days_zh[]={"周日","周一","周二","周三","周四","周五","周六"};
    static const char * days_en[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char * days_vi[]={"CN","T2","T3","T4","T5","T6","T7"};
    const char * const * days = homo_i18n_get()==HOMO_LANG_ZH ? days_zh :
                                homo_i18n_get()==HOMO_LANG_VI ? days_vi : days_en;
    const char * date_fmt = homo_i18n_get()==HOMO_LANG_ZH ? "%d月%d日  %s" : "%d/%d  %s";
    time_t now=time(NULL); struct tm * local=localtime(&now); char text[96];
    if(local) { snprintf(text,sizeof(text),date_fmt,local->tm_mon+1,local->tm_mday,days[local->tm_wday]); label_set(date_label,text); }
    hachimi_power_state_t power; bool available=hachimi_power_read(&power);
    const lv_image_dsc_t * source=available && (power.external_power || power.charging) ?
        &hachimi_ios_battery_charging_36 : &hachimi_ios_battery_36;
    if(lv_image_get_src(battery)!=source) lv_image_set_src(battery,source);
    if(available && power.battery_present) {
        lv_obj_set_style_image_recolor(battery,lv_color_hex(power.charging ? 0x54da8b :
                                      power.percent>=0 && power.percent<=20 ? 0xff6969 : 0xffffff),0);
    }
    else {
        lv_obj_set_style_image_recolor(battery,lv_color_white(),0);
    }
    if(!lv_slider_is_dragged(brightness_slider)) {
        int percent;
        if(!preview && hachimi_display_get_brightness(&percent)) brightness=percent;
        lv_slider_set_value(brightness_slider,brightness,LV_ANIM_OFF);
    }
}
static void refresh(lv_timer_t * unused)
{
    (void)unused;
    if(hachimi_network_poll(&network)) {
        bool status=!strcmp(network.operation,"status");
        if(status) { network_known=!network.failed; last_network=lv_tick_get(); }
        if(network.failed) label_set(feedback,network.message[0] ? network.message : "网络状态读取失败");
        if(!status) (void)hachimi_network_request(&network,"status",NULL);
        render_network();
    }
    if(!visible) { if(!network.job.running) lv_timer_pause(timer); return; }
    if(lv_tick_elaps(last_media)>=500) { update_media(); last_media=lv_tick_get(); }
    if(lv_tick_elaps(last_system)>=5000) { update_system(); last_system=lv_tick_get(); }
    if(!preview && !network.job.running && lv_tick_elaps(last_network)>=10000) {
        last_network=lv_tick_get(); (void)hachimi_network_request(&network,"status",NULL);
    }
}
static lv_obj_t * vertical_slider(unsigned index, int x, const lv_image_dsc_t * source, uint32_t color)
{
    lv_obj_t * slider=lv_slider_create(content); lv_obj_remove_style_all(slider);
    lv_obj_set_pos(slider,x,180); lv_obj_set_size(slider,98,218);
    lv_slider_set_range(slider,0,100); lv_slider_set_orientation(slider,LV_SLIDER_ORIENTATION_VERTICAL);
    lv_obj_set_style_radius(slider,28,LV_PART_MAIN); lv_obj_set_style_radius(slider,28,LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider,lv_color_hex(0x303a46),LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider,145,LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider,lv_color_hex(0xf2f6fb),LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,LV_PART_INDICATOR);
    lv_obj_set_style_border_color(slider,lv_color_white(),LV_PART_MAIN);
    lv_obj_set_style_border_width(slider,1,LV_PART_MAIN); lv_obj_set_style_border_opa(slider,45,LV_PART_MAIN);
    lv_obj_set_style_opa(slider,LV_OPA_TRANSP,LV_PART_KNOB); lv_obj_set_style_pad_all(slider,0,LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider,0);
    lv_obj_remove_flag(slider,LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_flag(slider,LV_OBJ_FLAG_PRESS_LOCK);
    slider_icons[index]=asset_icon(slider,source,29,164,40,40,color);
    sliders[index]=slider;
    return slider;
}
void hachimi_control_center_controls_create(lv_obj_t * parent)
{
    content=lv_obj_create(parent); lv_obj_remove_style_all(content); lv_obj_set_size(content,800,480);
    lv_obj_remove_flag(content,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    date_label=label(content,"",332,55,250,26,18,0xffffff);
    wifi_status=asset_icon(content,&hachimi_ios_wifi_26,690,54,26,26,0xffffff);
    battery=asset_icon(content,&hachimi_ios_battery_36,734,50,36,36,0xffffff);
    for(unsigned i=0;i<2;i++) {
        network_cards[i]=panel(content,332+(int)i*224,98,210,68,24);
        lv_obj_add_flag(network_cards[i],LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(network_cards[i],network_clicked,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        network_icons[i]=asset_icon(network_cards[i],i ? &hachimi_ethernet_44 : &hachimi_ios_wifi_44,
                                    12,12,46,44,0xffffff);
        network_titles[i]=label(network_cards[i],i ? "以太网" : "Wi-Fi",70,10,126,26,18,0xffffff);
        network_subtitles[i]=label(network_cards[i],"",70,39,128,21,14,0xc5d0da);
    }
    player=panel(content,332,180,210,218,28);
    music_icon=lv_image_create(player);
    lv_image_set_src(music_icon,"A:assets/ios/music.png");
    lv_obj_set_pos(music_icon,16,13); lv_obj_set_size(music_icon,30,30);
    lv_image_set_inner_align(music_icon,LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_remove_flag(music_icon,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    music_label=label(player,"音乐",54,18,130,24,14,0xe7edf4);
    track_title=label(player,"暂无播放",16,65,178,30,18,0xffffff);
    track_artist=label(player,"",16,99,178,24,14,0xd5dfe9);
    lv_obj_set_style_text_align(track_title,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_align(track_artist,LV_TEXT_ALIGN_CENTER,0);
    track_progress=lv_bar_create(player); lv_obj_remove_style_all(track_progress);
    lv_obj_set_pos(track_progress,18,143); lv_obj_set_size(track_progress,174,3); lv_bar_set_range(track_progress,0,1000);
    lv_obj_set_style_bg_color(track_progress,lv_color_white(),LV_PART_MAIN); lv_obj_set_style_bg_opa(track_progress,55,LV_PART_MAIN);
    lv_obj_set_style_bg_color(track_progress,lv_color_white(),LV_PART_INDICATOR); lv_obj_set_style_bg_opa(track_progress,230,LV_PART_INDICATOR);
    for(unsigned i=0;i<3;i++) {
        player_buttons[i]=panel(player,12+(int)i*66,159,54,44,22);
        lv_obj_set_style_bg_opa(player_buttons[i],0,0); lv_obj_set_style_border_width(player_buttons[i],0,0);
        lv_obj_add_flag(player_buttons[i],LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(player_buttons[i],media_clicked,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        lv_obj_t * object=icon(player_buttons[i],i==0 ? PREVIOUS : i==1 ? PLAY : NEXT,0,0,54,44,0xffffff);
        if(i==1) play_icon=object;
    }
    brightness_slider=vertical_slider(0,556,&hachimi_ios_sunny_40,0xf3a722);
    volume_slider=vertical_slider(1,668,&hachimi_ios_musical_notes_40,0x277ef2);
    /* Orientation Lock tile (giữa lưới, trái các slider) */
    orientation_card=panel(content,332,412,210,56,18);
    lv_obj_add_flag(orientation_card,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(orientation_card,orientation_clicked,LV_EVENT_CLICKED,NULL);
    orient_icon_label=label(orientation_card,"Q",12,12,30,30,18,0xff5a5a);
    label(orientation_card,"Orientation",54,10,140,22,14,0xffffff);
    orientation_state=label(orientation_card,"Locked",54,32,140,18,12,0xc5d0da);
    render_orientation();
    /* Lưới tính năng nhanh 2x4 (Calculator/Focus/Alarm/Flashlight/VPN/Memo/Settings/Translate) */
    for(unsigned i=0;i<8;i++) {
        quick_cards[i]=panel(content,20+(int)(i%2)*142,20+(int)(i/2)*74,132,64,18);
        lv_obj_add_flag(quick_cards[i],LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(quick_cards[i],quick_clicked,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        lv_obj_t * mark=label(quick_cards[i],quick_marks[i],10,10,28,28,16,0xffffff);
        lv_obj_set_style_bg_color(mark,lv_color_hex(0x3a4a5a),0);
        lv_obj_set_style_bg_opa(mark,LV_OPA_COVER,0);
        lv_obj_set_style_radius(mark,14,0);
        lv_obj_set_style_text_align(mark,LV_TEXT_ALIGN_CENTER,0);
        quick_titles[i]=label(quick_cards[i],quick_names[i],42,10,84,22,13,0xffffff);
        quick_subs[i]=label(quick_cards[i],(i==1||i==3)?"Off":"",42,34,84,18,11,0xc5d0da);
    }
    render_quick_state();
    feedback=label(content,"",332,434,434,18,12,0xffffff);
    lv_obj_add_event_cb(brightness_slider,brightness_changed,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(brightness_slider,brightness_changed,LV_EVENT_RELEASED,NULL);
    lv_obj_add_event_cb(brightness_slider,brightness_changed,LV_EVENT_PRESS_LOST,NULL);
    lv_obj_add_event_cb(volume_slider,volume_changed,LV_EVENT_VALUE_CHANGED,NULL);
    hachimi_network_init(&network); preview=!hachimi_system_services_supported();
    preview_wifi=preview_ethernet=true; network_known=preview; visible=playing=false;
    timer=lv_timer_create(refresh,100,NULL); lv_timer_pause(timer);
}
/* Portrait: the same widgets stacked in the iPhone Control Center order - one
 * status line, two connectivity tiles side by side, the player, then the two
 * vertical sliders filling the lower half. Every number is a fraction of the
 * canvas, so 540x1170 and any other portrait size keep the same proportions.
 * Landscape keeps the original 800x480 composition, merely centred. */
static void layout_portrait(int w, int h)
{
    const int margin = w * 5 / 100;
    const int gap = w * 3 / 100;
    const int card_w = (w - 2 * margin - gap) / 2;
    const int card_h = h * 9 / 100;
    const int date_y = h * 35 / 1000;
    const int cards_y = h * 10 / 100;
    const int orient_y = cards_y + card_h + gap;
    const int orient_h = 60;
    const int player_y = orient_y + orient_h + gap;
    const int player_h = h * 13 / 100;
    /* Ẩn player khi không phát: slider và lưới tính năng trượt lên như iOS. */
    const int player_eff_h = player_shown ? player_h : 0;
    const int slider_y = player_y + player_eff_h + (player_shown ? gap : 0);
    const int slider_h = h * 17 / 100;
    const int player_w = w - 2 * margin;
    const int buttons_x = (player_w - 3 * 54 - 2 * 12) / 2;

    lv_obj_set_pos(date_label, margin, date_y);
    lv_obj_set_size(date_label, player_w - 130, 30);
    lv_obj_set_pos(battery, w - margin - 36, date_y - 5);
    lv_obj_set_pos(wifi_status, w - margin - 36 - 6 - 26, date_y - 1);

    for(unsigned i = 0; i < 2; i++) {
        lv_obj_set_pos(network_cards[i], margin + (int)i * (card_w + gap), cards_y);
        lv_obj_set_size(network_cards[i], card_w, card_h);
        lv_obj_set_pos(network_icons[i], 12, (card_h - 44) / 2);
        lv_obj_set_pos(network_titles[i], 70, 10);
        lv_obj_set_size(network_titles[i], card_w - 82, 26);
        lv_obj_set_pos(network_subtitles[i], 70, 39);
        lv_obj_set_size(network_subtitles[i], card_w - 82, 21);
    }

    lv_obj_set_pos(player, margin, player_y);
    lv_obj_set_size(player, player_w, player_h);
    lv_obj_set_pos(music_icon, 16, 13);
    lv_obj_set_pos(music_label, 54, 18);
    lv_obj_set_size(music_label, player_w - 70, 24);
    lv_obj_set_pos(track_title, 16, 65);
    lv_obj_set_size(track_title, player_w - 32, 30);
    lv_obj_set_pos(track_artist, 16, 99);
    lv_obj_set_size(track_artist, player_w - 32, 24);
    lv_obj_set_pos(track_progress, 18, player_h - 75);
    lv_obj_set_size(track_progress, player_w - 36, 3);
    for(unsigned i = 0; i < 3; i++) {
        lv_obj_set_pos(player_buttons[i], buttons_x + (int)i * 66, player_h - 59);
        lv_obj_set_size(player_buttons[i], 54, 44);
    }

    for(unsigned i = 0; i < 2; i++) {
        lv_obj_set_pos(sliders[i], margin + (int)i * (card_w + gap), slider_y);
        lv_obj_set_size(sliders[i], card_w, slider_h);
        lv_obj_set_pos(slider_icons[i], card_w / 2 - 20, slider_h - 54);
    }

    /* Orientation tile: dải đầy đủ chiều ngang giữa lưới mạng và player. */
    if(orientation_card != NULL) {
        lv_obj_set_pos(orientation_card, margin, orient_y);
        lv_obj_set_size(orientation_card, player_w, orient_h);
    }
    /* Lưới 8 tính năng nhanh dưới player/sliders, vẫn theo fraction. */
    {
        int tiles_y = slider_y + slider_h + 12;
        int tile_w = (player_w - gap) / 2;
        int tile_h = 64;
        for(unsigned i = 0; i < 8; i++) {
            if(quick_cards[i] == NULL) continue;
            int col = (int)(i % 2), row = (int)(i / 2);
            lv_obj_set_pos(quick_cards[i], margin + col * (tile_w + gap), tiles_y + row * (tile_h + 10));
            lv_obj_set_size(quick_cards[i], tile_w, tile_h);
        }
    }

    lv_obj_set_pos(feedback, margin, h - h * 11 / 100);
    lv_obj_set_size(feedback, player_w, 18);
}

static void layout_landscape(int w, int h)
{
    const int used_x = 332, used_w = 434;
    const int used_y = 50, used_h = 402;
    lv_obj_set_size(content, 800, 480);
    lv_obj_set_pos(content, (w - used_w) / 2 - used_x,
                   (h - used_h) / 2 - used_y);
    /* Portrait/full: content object is the full display; position the new
     * tiles inside its 800x480 design without shifting the legacy widgets so
     * existing tap coordinates stay valid. */
    if(orientation_card != NULL) {
        lv_obj_set_pos(orientation_card, 20, 412);
        lv_obj_set_size(orientation_card, 290, 56);
    }
    for(unsigned i = 0; i < 8; i++) {
        if(quick_cards[i] == NULL) continue;
        lv_obj_set_pos(quick_cards[i], 20 + (int)(i % 2) * 149, 20 + (int)(i / 2) * 74);
        lv_obj_set_size(quick_cards[i], 140, 64);
    }
}

void hachimi_control_center_controls_layout(int display_w, int display_h)
{
    if(content == NULL) return;
    display_w_cache = display_w;
    display_h_cache = display_h;
    if(display_h > display_w) {
        lv_obj_set_pos(content, 0, 0);
        lv_obj_set_size(content, display_w, display_h);
        layout_portrait(display_w, display_h);
    }
    else {
        layout_landscape(display_w, display_h);
    }
}

void hachimi_control_center_controls_open(void)
{
    if(content==NULL) return;
    visible=true; label_set(feedback,""); update_system(); update_media(); render_orientation(); render_quick_state();
    last_media=last_system=last_network=lv_tick_get();
    if(!preview && !network.job.running && !hachimi_network_request(&network,"status",NULL))
        label_set(feedback,"网络状态读取失败");
    render_network(); lv_timer_resume(timer);
}
void hachimi_control_center_controls_close(void)
{
    visible=false;
    if(timer!=NULL && !network.job.running) lv_timer_pause(timer);
}
void hachimi_control_center_controls_destroy(void)
{
    if(content==NULL) return;
    hachimi_network_cancel(&network);
    if(timer!=NULL) lv_timer_delete(timer);
    timer=NULL;
    lv_obj_delete(content); content=NULL;
    visible=false;
}
