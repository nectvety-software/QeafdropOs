#include "desktop.h"
#include "ui/ios_layout.h"
#include "ui/ios_metrics.h"
#include "ui/ios_status_bar.h"
#include "ui/control_center.h"
#include "ui/fonts.h"
#include "ui/i18n.h"
#include "app_registry.h"
#include "app_manager.h"

#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IOS_ASSET(name) "A:assets/ios/" name
#define IOS_WALLPAPER_PORTRAIT IOS_ASSET("wallpaper-portrait.png")
#define IOS_WALLPAPER_LANDSCAPE IOS_ASSET("wallpaper-landscape.png")

#define MAX_APP_LAUNCHES 24
#define EDGE_GESTURE_BOTTOM_SIZE 36
#ifdef HACHIMI_SIMULATOR
#define EDGE_GESTURE_SIDE_SIZE 64
#else
#define EDGE_GESTURE_SIDE_SIZE EDGE_GESTURE_BOTTOM_SIZE
#endif
#define APP_OPEN_DURATION 320
#define APP_CLOSE_DURATION 250
#define APP_CORNER_RADIUS 20
#define ANIMATION_SCALE 1024

/* The shell is portrait first (540x1170). Landscape apps use the 1170x540
 * canvas, an exact 2x fit, so rotating sideways is full-screen with no
 * black bars. Settings runs on the shell canvas. */
#define APP_CANVAS_W 1170
#define APP_CANVAS_H 540

typedef struct {
    const hachimi_app_descriptor_t * app;
    lv_coord_t x;
    lv_coord_t y;
    lv_obj_t * image;
    lv_obj_t * label;
    lv_obj_t * badge;
    bool dock;
    bool hidden;
} app_launch_t;

typedef enum {
    APP_STATE_IDLE,
    APP_STATE_OPENING,
    APP_STATE_OPEN,
    APP_STATE_CLOSING,
} app_state_t;

typedef enum {
    GESTURE_FROM_LEFT,
    GESTURE_FROM_RIGHT,
    GESTURE_FROM_BOTTOM,
} app_gesture_action_t;


typedef struct {
    int32_t from_x;
    int32_t from_y;
    int32_t from_width;
    int32_t from_height;
    int32_t from_radius;
    int32_t to_x;
    int32_t to_y;
    int32_t to_width;
    int32_t to_height;
    int32_t to_radius;
} app_transition_t;

static hachimi_app_manager_t app_manager;
static lv_obj_t * app_view;
static lv_obj_t * app_mask;
static lv_obj_t * transition_content;
static lv_draw_buf_t * transition_content_buffer;
static lv_obj_t * transition_background;
static lv_draw_buf_t * transition_background_buffer;
static lv_obj_t * app_title;
static lv_obj_t * home_indicator;
static lv_obj_t * gesture_zones[3];
static lv_obj_t * wallpaper;
static lv_obj_t * status_bar;
static lv_obj_t * dock;
static lv_obj_t * home_bar;
static app_launch_t app_launches[MAX_APP_LAUNCHES];
static size_t app_launch_count;
static size_t home_launch_count;
static app_launch_t * selected_launch;
static app_launch_t * pending_launch;
static app_state_t app_state = APP_STATE_IDLE;
static app_transition_t app_transition;
static hachimi_desktop_transition_backend_t transition_backend;
static bool direct_transition_active;
static bool app_mode;
static bool wallpaper_is_portrait;

/* Edit mode (drag / uninstall) and the running-app switcher. */
static bool edit_mode;
static app_launch_t * dragging;
static int32_t drag_off_x;
static int32_t drag_off_y;
static lv_obj_t * switcher;
static lv_obj_t * switcher_list;
static bool switcher_is_open;
/* Home paging swipe effects: 0 = first page, 1 = second page. */
static int home_page;
static int32_t page_slide_x;

/* Forward declarations: the drag/edit/switcher helpers above use the layout
 * helpers defined near the bottom after the edit-mode block was inserted. */
static void place_launch(app_launch_t * launch, lv_coord_t x, lv_coord_t y);
static void relayout_home(void);
static void hide_launch(app_launch_t * launch);
static void switcher_close(void);
static void switcher_open_fn(void);

/* Current LVGL display size.  Every coordinate below is derived from it, so
 * the same desktop serves 540x1170 portrait, 1170x540 landscape and the
 * 800x480 app canvas. */
static int display_w = 800;
static int display_h = 480;
static ios_metrics_t metrics;

static void measure_display(void)
{
    lv_display_t * display = lv_display_get_default();
    display_w = display ? lv_display_get_horizontal_resolution(display) : 800;
    display_h = display ? lv_display_get_vertical_resolution(display) : 480;
    if(display_w < 100) display_w = 800;
    if(display_h < 100) display_h = 480;
    ios_metrics(display_w, display_h, &metrics);
}

static bool portrait_shell(void)
{
    return display_h > display_w;
}

static bool app_display_ready(void)
{
    return display_w == APP_CANVAS_W && display_h == APP_CANVAS_H;
}

/* Capture once, before hiding the application. Never relayout live widgets
 * during the animation, and retain the allocation for the next close. */
static const lv_draw_buf_t * cache_transition_content(void)
{
    lv_obj_add_flag(transition_content, LV_OBJ_FLAG_HIDDEN);
    const char * enabled = getenv("HACHIMI_TRANSITION_CONTENT");
    if(enabled && strcmp(enabled, "0") == 0) return NULL;
    lv_obj_update_layout(app_view);
    if(transition_content_buffer != NULL &&
       (transition_content_buffer->header.w != (uint32_t)display_w ||
        transition_content_buffer->header.h != (uint32_t)display_h)) {
        lv_image_cache_drop(transition_content_buffer);
        lv_draw_buf_destroy(transition_content_buffer);
        transition_content_buffer = NULL;
    }
    if(transition_content_buffer == NULL)
        transition_content_buffer = lv_snapshot_create_draw_buf(app_view, LV_COLOR_FORMAT_RGB565);
    if(transition_content_buffer == NULL) return NULL;
    lv_image_cache_drop(transition_content_buffer);
    if(lv_snapshot_take_to_draw_buf(app_view, LV_COLOR_FORMAT_RGB565,
                                   transition_content_buffer) != LV_RESULT_OK) return NULL;
    lv_image_set_src(transition_content, transition_content_buffer);
    lv_obj_clear_flag(transition_content, LV_OBJ_FLAG_HIDDEN);
    return transition_content_buffer;
}

/* Cubic deceleration: immediate response, zero terminal velocity, no overshoot.
 * Integer arithmetic keeps the motion identical on simulator and Cortex-A7. */
static int32_t app_transition_path(const lv_anim_t * anim)
{
    if(anim->duration <= 0) return anim->end_value;
    int32_t t = anim->act_time < 0 ? 0 : anim->act_time;
    if(t > anim->duration) t = anim->duration;
    int32_t remaining = ANIMATION_SCALE - t * ANIMATION_SCALE / anim->duration;
    int32_t eased = ANIMATION_SCALE - (int32_t)((int64_t)remaining * remaining * remaining /
                                                (ANIMATION_SCALE * ANIMATION_SCALE));
    return anim->start_value + (int32_t)((int64_t)(anim->end_value - anim->start_value) * eased / ANIMATION_SCALE);
}

static bool cache_transition_background(void)
{
    lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
    const char * enabled = getenv("HACHIMI_TRANSITION_CACHE");
    if(enabled && strcmp(enabled, "0") == 0) return false;
    lv_obj_t * screen = lv_screen_active();
    if(transition_background_buffer != NULL &&
       (transition_background_buffer->header.w != (uint32_t)display_w ||
        transition_background_buffer->header.h != (uint32_t)display_h)) {
        lv_image_cache_drop(transition_background_buffer);
        lv_draw_buf_destroy(transition_background_buffer);
        transition_background_buffer = NULL;
    }
    if(transition_background_buffer == NULL) {
        transition_background_buffer = lv_snapshot_create_draw_buf(screen, LV_COLOR_FORMAT_RGB565);
    }
    if(transition_background_buffer == NULL) return false;
    lv_image_cache_drop(transition_background_buffer);
    if(lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_RGB565, transition_background_buffer) != LV_RESULT_OK) return false;
    lv_image_set_src(transition_background, transition_background_buffer);
    lv_obj_clear_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
    return true;
}



static const app_gesture_action_t gesture_actions[] = {
    GESTURE_FROM_LEFT,
    GESTURE_FROM_RIGHT,
    GESTURE_FROM_BOTTOM,
};

static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if(value < min_value) return min_value;
    if(value > max_value) return max_value;
    return value;
}

static int32_t interpolate_i32(int32_t from, int32_t to, int32_t progress)
{
    return from + (int32_t)(((int64_t)(to - from) * progress) / ANIMATION_SCALE);
}

static void layout_app_content(void)
{
    if(app_view == NULL) return;

    lv_coord_t parent_width = lv_obj_get_width(app_view);
    lv_coord_t parent_height = lv_obj_get_height(app_view);
    if(app_title != NULL) {
        lv_obj_set_pos(app_title,
                       (parent_width - lv_obj_get_width(app_title)) / 2,
                       (parent_height - lv_obj_get_height(app_title)) / 2);
    }
    hachimi_app_manager_layout(&app_manager, parent_width, parent_height);
    if(home_indicator != NULL) {
        lv_obj_set_pos(home_indicator,
                       (parent_width - lv_obj_get_width(home_indicator)) / 2,
                       parent_height - lv_obj_get_height(home_indicator) - 12);
    }
}

static void layout_gestures(void)
{
    if(gesture_zones[0] == NULL) return;
    lv_obj_set_pos(gesture_zones[0], 0, 60);
    lv_obj_set_size(gesture_zones[0], EDGE_GESTURE_SIDE_SIZE,
                    display_h - 60 - EDGE_GESTURE_BOTTOM_SIZE);
    lv_obj_set_pos(gesture_zones[1], display_w - EDGE_GESTURE_SIDE_SIZE, 60);
    lv_obj_set_size(gesture_zones[1], EDGE_GESTURE_SIDE_SIZE,
                    display_h - 60 - EDGE_GESTURE_BOTTOM_SIZE);
    lv_obj_set_pos(gesture_zones[2], EDGE_GESTURE_SIDE_SIZE,
                   display_h - EDGE_GESTURE_BOTTOM_SIZE);
    lv_obj_set_size(gesture_zones[2], display_w - EDGE_GESTURE_SIDE_SIZE * 2,
                    EDGE_GESTURE_BOTTOM_SIZE);
}

static void app_transition_step(void * var, int32_t progress)
{
    app_transition_t * transition = var;
    if(transition == NULL || app_mask == NULL) return;

    int32_t x = interpolate_i32(transition->from_x, transition->to_x, progress);
    int32_t y = interpolate_i32(transition->from_y, transition->to_y, progress);
    int32_t width = interpolate_i32(transition->from_width, transition->to_width, progress);
    int32_t height = interpolate_i32(transition->from_height, transition->to_height, progress);
    /* Keep the card rounded until it meets the display edges. On return,
     * establish the corners early instead of sharpening a shrinking card. */
    int32_t edge_distance = app_state == APP_STATE_OPENING ? ANIMATION_SCALE - progress : progress;
    int32_t radius = clamp_i32(edge_distance * APP_CORNER_RADIUS / 160, 0, APP_CORNER_RADIUS);
    if(direct_transition_active && transition_backend.draw != NULL) {
        transition_backend.draw(transition_backend.user_data, x, y,
                               clamp_i32(width, 1, display_w + 80),
                               clamp_i32(height, 1, display_h + 80),
                               clamp_i32(radius, 0, APP_CORNER_RADIUS),
                               app_state == APP_STATE_CLOSING && progress >= ANIMATION_SCALE);
        return; /* No invisible LVGL transforms/layout during framebuffer animation. */
    }
    lv_obj_set_pos(app_mask, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_size(app_mask, (lv_coord_t)clamp_i32(width, 1, display_w + 80),
                    (lv_coord_t)clamp_i32(height, 1, display_h + 80));
    lv_obj_set_style_radius(app_mask, clamp_i32(radius, 0, APP_CORNER_RADIUS), LV_PART_MAIN);
}

static void finish_direct_transition(void)
{
    if(!direct_transition_active) return;
    direct_transition_active = false;
    if(transition_backend.end != NULL) transition_backend.end(transition_backend.user_data);
}

static void present_direct_transition_target(void)
{
    if(transition_backend.present != NULL) transition_backend.present(transition_backend.user_data);
}

/* Called from an animation completion callback. lv_refr_now() also runs the
 * animation engine, which reenters its linked-list traversal and can leave
 * a freed animation node in use when other app animations are alive. */
static void refresh_transition_target(void)
{
    lv_display_t * display=lv_display_get_next(NULL);
    while(display) {
        lv_timer_t * timer=lv_display_get_refr_timer(display);
        if(timer) lv_display_refr_timer(timer);
        display=lv_display_get_next(display);
    }
}

static void app_transition_complete(lv_anim_t * anim)
{
    LV_UNUSED(anim);

    if(app_state == APP_STATE_OPENING) {
        bool direct = direct_transition_active;
        /* Present the full-screen white mask before creating or opening the app. */
        if(!direct) {
            app_transition_step(&app_transition, ANIMATION_SCALE);
            lv_obj_clear_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
        }
        finish_direct_transition();
        if(!direct) refresh_transition_target();
        if(!hachimi_app_manager_open(&app_manager, selected_launch->app->module)) {
            lv_obj_add_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
            selected_launch = NULL;
            pending_launch = NULL;
            app_state = APP_STATE_IDLE;
            app_mode = false; /* let the platform return to the shell */
            if(direct) lv_obj_invalidate(lv_screen_active());
            refresh_transition_target();
            if(direct) present_direct_transition_target();
            return;
        }
        lv_obj_clear_flag(app_view, LV_OBJ_FLAG_HIDDEN);
        layout_app_content();
        lv_obj_update_layout(app_view);
        lv_obj_add_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
        app_state = APP_STATE_OPEN;
        for(size_t i = 0; i < sizeof(gesture_zones) / sizeof(gesture_zones[0]); i++) {
            /* CarPlay's narrow left rail contains navigation controls. Keep
             * its full hit area usable; bottom/right return gestures remain. */
            if(i == 0 && selected_launch->app->reserve_left_edge) {
                lv_obj_add_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
        }
        if(direct) {
            lv_obj_invalidate(lv_screen_active());
            refresh_transition_target();
            present_direct_transition_target();
        }
    }
    else if(app_state == APP_STATE_CLOSING) {
        bool direct = direct_transition_active;
        /* Về Home: giữ các zone vuốt để chuyển trang Home vẫn nhận được. */
        for(size_t i = 0; i < sizeof(gesture_zones) / sizeof(gesture_zones[0]); i++) {
            lv_obj_remove_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(app_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
        hachimi_app_manager_close(&app_manager);
        app_state = APP_STATE_IDLE;
        app_mode = false; /* let the platform return to the shell */
        pending_launch = NULL;
        finish_direct_transition();
        if(direct) lv_obj_invalidate(lv_screen_active());
        refresh_transition_target();
        if(direct) present_direct_transition_target();
    }
}

static void start_app_transition(bool opening)
{
    if(selected_launch == NULL || app_view == NULL || app_mask == NULL) return;

    const lv_draw_buf_t * content = NULL;
    if(!opening) content = cache_transition_content();
    else lv_obj_add_flag(transition_content, LV_OBJ_FLAG_HIDDEN);
    app_state = opening ? APP_STATE_OPENING : APP_STATE_CLOSING;
    if(opening) {
        app_transition.from_x = selected_launch->x;
        app_transition.from_y = selected_launch->y;
        app_transition.from_width = metrics.icon;
        app_transition.from_height = metrics.icon;
        app_transition.from_radius = APP_CORNER_RADIUS;
        app_transition.to_x = 0;
        app_transition.to_y = 0;
        app_transition.to_width = display_w;
        app_transition.to_height = display_h;
        app_transition.to_radius = 0;
    }
    else {
        app_transition.from_x = 0;
        app_transition.from_y = 0;
        app_transition.from_width = display_w;
        app_transition.from_height = display_h;
        app_transition.from_radius = 0;
        app_transition.to_x = selected_launch->x;
        app_transition.to_y = selected_launch->y;
        app_transition.to_width = metrics.icon;
        app_transition.to_height = metrics.icon;
        app_transition.to_radius = APP_CORNER_RADIUS;
    }

    /* Animate a frozen card; the app keeps its full-screen layout. A cold
     * launch retains the cheap opaque panel so app construction cannot delay
     * the first visible response. Failed snapshots use that same fallback. */
    lv_obj_add_flag(app_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
    for(size_t i = 0; i < sizeof(gesture_zones) / sizeof(gesture_zones[0]); i++) {
        lv_obj_add_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
    }
    bool snapshot_ready = cache_transition_background();
    direct_transition_active = snapshot_ready && transition_backend.begin != NULL &&
        transition_backend.begin(transition_backend.user_data, transition_background_buffer, content,
                                 app_transition.from_x, app_transition.from_y,
                                 app_transition.from_width, app_transition.from_height,
                                 app_transition.from_radius);
    if(direct_transition_active) lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
    else {
        app_transition_step(&app_transition, 0);
        lv_obj_clear_flag(app_mask, LV_OBJ_FLAG_HIDDEN);
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &app_transition);
    lv_anim_set_exec_cb(&anim, app_transition_step);
    lv_anim_set_values(&anim, 0, ANIMATION_SCALE);
    /* Both rendering paths already presented progress 0 above. */
    lv_anim_set_early_apply(&anim, false);
    lv_anim_set_duration(&anim, opening ? APP_OPEN_DURATION : APP_CLOSE_DURATION);
    lv_anim_set_path_cb(&anim, app_transition_path);
    lv_anim_set_completed_cb(&anim, app_transition_complete);
    lv_anim_start(&anim);
}

static void close_app(void)
{
    if(hachimi_control_center_is_visible()) return;
    if(app_state != APP_STATE_OPEN) return;
    start_app_transition(false);
}

void hachimi_desktop_set_app_gestures_enabled(bool enabled)
{
    if(app_state != APP_STATE_OPEN) return;

    for(size_t i = 0; i < sizeof(gesture_zones) / sizeof(gesture_zones[0]); i++) {
        if(!enabled || (i == 0 && selected_launch != NULL &&
                        selected_launch->app->reserve_left_edge)) {
            lv_obj_add_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(gesture_zones[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void begin_launch(app_launch_t * launch)
{
    if(hachimi_control_center_is_visible()) return;
    if(launch == NULL || launch->app == NULL || app_view == NULL ||
       app_title == NULL || app_state != APP_STATE_IDLE) return;

    selected_launch = launch;
    lv_label_set_text(app_title, homo_i18n_app_title((int)launch->app->id));
    if(launch->app->module != NULL) {
        lv_obj_add_flag(app_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(home_indicator, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(app_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(home_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(home_indicator, lv_color_hex(0x202124), LV_PART_MAIN);
    }
    start_app_transition(true);
}

static bool launch_app(app_launch_t * launch)
{
    if(hachimi_control_center_is_visible()) return false;
    if(launch == NULL || launch->app == NULL || app_view == NULL ||
       app_title == NULL || app_state != APP_STATE_IDLE) return false;

    /* Settings runs on the shell canvas (540x1170 portrait, full-screen
     * responsive) so it never asks the platform for 800x480. Every other
     * module is still drawn for 800x480 and asks for that canvas first. */
    if(launch->app->id == HACHIMI_APP_SETTINGS) {
        begin_launch(launch);
        return true;
    }
    if(!app_display_ready()) {
        pending_launch = launch;
        app_mode = true;
        return true;
    }
    begin_launch(launch);
    return true;
}

bool hachimi_desktop_launch(hachimi_app_id_t id)
{
    for(size_t i = 0; i < app_launch_count; i++) {
        if(app_launches[i].app->id == id) return launch_app(&app_launches[i]);
    }
    return false;
}

bool hachimi_desktop_home(void)
{
    if(hachimi_control_center_is_visible()) {
        hachimi_control_center_close();
        return true;
    }
    if(app_state != APP_STATE_OPEN) return false;
    close_app();
    return true;
}

static void persist_desktop_state(void);
static void layout_home_icons(void);
static void layout_dock(void);
static void switcher_close(void);
static void switcher_open_fn(void);

static void set_edit_mode(bool on)
{
    edit_mode = on;
    dragging = NULL;
    for(size_t i = 0; i < app_launch_count; i++) {
        app_launch_t * l = &app_launches[i];
        if(l->badge == NULL) continue;
        if(on && !l->hidden && !l->dock)
            lv_obj_remove_flag(l->badge, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(l->badge, LV_OBJ_FLAG_HIDDEN);
    }
    if(!on) relayout_home();
}

static void open_app_event_cb(lv_event_t * event)
{
    app_launch_t * launch = lv_event_get_user_data(event);
    if(edit_mode) return; /* in edit mode taps drag, they do not launch */
    (void)launch_app(launch);
}

static void badge_event_cb(lv_event_t * event)
{
    app_launch_t * launch = lv_event_get_user_data(event);
    if(launch == NULL) return;
    hide_launch(launch);
}

static void icon_longpress_event_cb(lv_event_t * event)
{
    app_launch_t * launch = lv_event_get_user_data(event);
    if(launch == NULL || launch->dock || launch->hidden) return;
    if(lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) return;
    /* Nhấn giữ lâu: hiện X gỡ, không mở app. Chạm nhanh vẫn mở như thường. */
    if(edit_mode) return;
    set_edit_mode(true);
    /* Bắt đầu kéo ngay trong cùng một lần chạm, không cần nhấc tay rồi chạm lại. */
    lv_indev_t * indev = lv_event_get_indev(event);
    if(indev != NULL) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        dragging = launch;
        drag_off_x = point.x - launch->x;
        drag_off_y = point.y - launch->y;
        if(launch->image != NULL) lv_obj_move_foreground(launch->image);
        if(launch->label != NULL) lv_obj_move_foreground(launch->label);
        if(launch->badge != NULL) lv_obj_move_foreground(launch->badge);
    }
}

static void icon_drag_event_cb(lv_event_t * event)
{
    app_launch_t * launch = lv_event_get_user_data(event);
    if(launch == NULL || launch->dock || launch->hidden) return;
    lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_LONG_PRESSED) return;
    lv_indev_t * indev = lv_event_get_indev(event);
    if(indev == NULL) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if(code == LV_EVENT_PRESSED) {
        /* Ngoài edit mode, chạm nhanh không làm gì để CLICKED mở app. Chỉ
         * khi đã nhấn giữ vào edit mode mới bắt đầu kéo. */
        if(!edit_mode) return;
        dragging = launch;
        drag_off_x = point.x - launch->x;
        drag_off_y = point.y - launch->y;
        if(launch->image != NULL) lv_obj_move_foreground(launch->image);
        if(launch->label != NULL) lv_obj_move_foreground(launch->label);
        if(launch->badge != NULL) lv_obj_move_foreground(launch->badge);
    }
    else if(code == LV_EVENT_PRESSING && dragging == launch) {
        lv_coord_t x = (lv_coord_t)(point.x - drag_off_x);
        lv_coord_t y = (lv_coord_t)(point.y - drag_off_y);
        if(x < 0) x = 0;
        if(y < 0) y = 0;
        if(x + metrics.icon > display_w) x = display_w - metrics.icon;
        if(y + metrics.icon > display_h) y = display_h - metrics.icon;
        place_launch(launch, x, y);
    }
    else if(code == LV_EVENT_RELEASED && dragging == launch) {
        /* Kéo sang màn hình kia: thả ở mép trái/phải là sang trang trước/sau.
         * Giữ icon ở mép lúc PRESSING cũng tự sang trang (xem PRESSING). */
        int center_x = launch->x + metrics.icon / 2;
        int center_y = launch->y + metrics.icon / 2;
        size_t per_page = metrics.portrait ? 8 : 12;
        size_t visible = 0;
        for(size_t i = 0; i < home_launch_count; i++)
            if(!app_launches[i].hidden) visible++;
        int pages = per_page > 0 ? (int)((visible + per_page - 1) / per_page) : 1;
        bool edge_next = center_x > display_w - 70;
        bool edge_prev = center_x < 70;
        size_t from = 0;
        size_t seen = 0;
        for(size_t i = 0; i < home_launch_count; i++) {
            if(app_launches[i].hidden) continue;
            if(&app_launches[i] == launch) { from = seen; break; }
            seen++;
        }
        if((edge_next && home_page + 1 < pages) || (edge_prev && home_page > 0)) {
            int target_page = edge_next ? home_page + 1 : home_page - 1;
            size_t target = (size_t)target_page * per_page;
            if(edge_prev && target + per_page > visible) target = visible - 1;
            if(edge_next && target >= visible) target = visible - 1;
            if(from != target && visible > 0) {
                app_launch_t moved = app_launches[0];
                bool found = false;
                for(size_t i = 0; i < home_launch_count; i++)
                    if(&app_launches[i] == launch) { moved = app_launches[i]; found = true; break; }
                if(found) {
                    /* Chuyển mảng visible sang vị trí target (kể cả ham bị xoá ở cuối). */
                    if(from < target) {
                        for(size_t i = from; i < target; i++) app_launches[i] = app_launches[i + 1];
                        app_launches[target] = moved;
                    } else {
                        for(size_t i = from; i > target; i--) app_launches[i] = app_launches[i - 1];
                        app_launches[target] = moved;
                    }
                }
                home_page = target_page;
                persist_desktop_state();
            }
            dragging = NULL;
            relayout_home();
            return;
        }
        /* Snap trong trang như cũ. */
        {
        int columns = metrics.columns > 0 ? metrics.columns : 4;
        int col = (center_x - metrics.margin_x) / (metrics.icon + metrics.gap_x);
        int row = (center_y - metrics.first_row_top) /
                  (metrics.row_pitch > 0 ? metrics.row_pitch : metrics.icon + 20);
        if(col < 0) col = 0;
        if(col >= columns) col = columns - 1;
        if(row < 0) row = 0;
        size_t target = (size_t)(row * columns + col) + (size_t)home_page * per_page;
        if(target >= visible) target = visible > 0 ? visible - 1 : 0;
        if(from != target && visible > 0) {
            app_launch_t moved = app_launches[0];
            bool found = false;
            for(size_t i = 0; i < home_launch_count; i++)
                if(&app_launches[i] == launch) { moved = app_launches[i]; found = true; break; }
            if(found) {
                if(from < target) {
                    for(size_t i = from; i < target; i++) {
                        app_launches[i] = app_launches[i + 1];
                    }
                    app_launches[target] = moved;
                }
                else {
                    for(size_t i = from; i > target; i--) {
                        app_launches[i] = app_launches[i - 1];
                    }
                    app_launches[target] = moved;
                }
            }
            persist_desktop_state();
        }
        dragging = NULL;
        relayout_home();
        } /* snap trong trang */
    }
}

static void wallpaper_tap_event_cb(lv_event_t * event)
{
    (void)event;
    if(edit_mode) set_edit_mode(false);
    else if(switcher_is_open) switcher_close();
}

static void switcher_row_click(lv_event_t * event)
{
    app_launch_t * launch = lv_event_get_user_data(event);
    switcher_close();
    if(launch != NULL) (void)launch_app(launch);
}

static void switcher_row_close(lv_event_t * event)
{
    app_launch_t * launch = lv_event_get_user_data(event);
    if(launch == NULL || launch->app == NULL) return;
    if(launch->app->module != NULL)
        hachimi_app_manager_evict(&app_manager, launch->app->module);
    /* Rebuild the list in place. */
    lv_obj_clean(switcher_list);
    switcher_open_fn();
}

static void switcher_close(void)
{
    switcher_is_open = false;
    if(switcher != NULL) lv_obj_add_flag(switcher, LV_OBJ_FLAG_HIDDEN);
}

static void switcher_open_fn(void)
{
    if(switcher == NULL || switcher_list == NULL) return;
    lv_obj_clean(switcher_list);
    size_t count = hachimi_app_manager_count(&app_manager);
    if(count == 0) {
        lv_obj_t * empty = lv_label_create(switcher_list);
        lv_label_set_text(empty, homo_i18n_tr("No running apps"));
        lv_obj_set_style_text_font(empty, hachimi_ui_font(16), LV_PART_MAIN);
        lv_obj_set_style_text_color(empty, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_size(empty, display_w - 60, 28);
    }
    else {
        for(size_t i = 0; i < count; i++) {
            const hachimi_app_module_t * module = hachimi_app_manager_module_at(&app_manager, i);
            if(module == NULL) continue;
            app_launch_t * launch = NULL;
            for(size_t j = 0; j < app_launch_count; j++) {
                if(app_launches[j].app != NULL && app_launches[j].app->module == module) {
                    launch = &app_launches[j];
                    break;
                }
            }
            lv_obj_t * row = lv_obj_create(switcher_list);
            lv_obj_remove_style_all(row);
            lv_obj_set_size(row, display_w - 60, 64);
            lv_obj_set_style_radius(row, 16, LV_PART_MAIN);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2a2f3a), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            if(launch != NULL)
                lv_obj_add_event_cb(row, switcher_row_click, LV_EVENT_CLICKED, launch);

            if(launch != NULL && launch->image != NULL) {
                lv_obj_t * icon = lv_image_create(row);
                lv_image_set_src(icon, launch->app->image);
                lv_obj_set_pos(icon, 10, 10);
                lv_obj_set_size(icon, 44, 44);
                lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_STRETCH);
                lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            }
            lv_obj_t * title = lv_label_create(row);
            lv_label_set_text(title, launch != NULL
                ? homo_i18n_app_name((int)launch->app->id) : "App");
            lv_obj_set_pos(title, 66, 20);
            lv_obj_set_size(title, display_w - 60 - 66 - 60, 24);
            lv_obj_set_style_text_font(title, hachimi_ui_font(18), LV_PART_MAIN);
            lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
            lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t * close = lv_obj_create(row);
            lv_obj_remove_style_all(close);
            lv_obj_set_pos(close, display_w - 60 - 48, 10);
            lv_obj_set_size(close, 44, 44);
            lv_obj_set_style_radius(close, 22, LV_PART_MAIN);
            lv_obj_set_style_bg_color(close, lv_color_hex(0xe03131), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(close, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t * x = lv_label_create(close);
            lv_label_set_text(x, LV_SYMBOL_CLOSE);
            lv_obj_center(x);
            lv_obj_set_style_text_font(x, hachimi_ui_font(18), LV_PART_MAIN);
            lv_obj_set_style_text_color(x, lv_color_white(), LV_PART_MAIN);
            lv_obj_clear_flag(x, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(close, switcher_row_close, LV_EVENT_CLICKED, launch);
        }
    }
    lv_obj_clear_flag(switcher, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(switcher);
    switcher_is_open = true;
}

static void home_bar_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if(edit_mode) { set_edit_mode(false); return; }
    if(switcher_is_open) switcher_close();
    else switcher_open_fn();
}

static void page_slide_done(lv_anim_t * anim)
{
    (void)anim;
    page_slide_x = 0;
    layout_home_icons();
}

static void page_slide_cb(void * var, int32_t v)
{
    (void)var;
    page_slide_x = v;
    layout_home_icons();
}

static void home_goto_page(int target)
{
    size_t visible = 0;
    for(size_t i = 0; i < home_launch_count; i++)
        if(!app_launches[i].hidden) visible++;
    size_t per_page = metrics.portrait ? 8 : 12;
    int pages = per_page > 0 ? (int)((visible + per_page - 1) / per_page) : 1;
    if(pages < 1) pages = 1;
    if(target < 0) target = 0;
    if(target >= pages) target = pages - 1;
    if(target == home_page) return;
    int32_t from = page_slide_x;
    int32_t to = target > home_page ? -display_w : display_w;
    home_page = target;
    /* Hiệu ứng trượt: trượt ra/xuống rồi về 0 trên trang mới. */
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, NULL);
    lv_anim_set_exec_cb(&anim, page_slide_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, 120);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&anim, page_slide_done);
    lv_anim_start(&anim);
}

static void app_gesture_event_cb(lv_event_t * event)
{
    const app_gesture_action_t * action = lv_event_get_user_data(event);
    lv_indev_t * indev = lv_indev_active();
    if(action == NULL || indev == NULL) return;
    lv_dir_t direction = lv_indev_get_gesture_dir(indev);

    /* Home (IDLE): trái/phải chuyển trang, dưới lên mở đa nhiệm. */
    if(app_state == APP_STATE_IDLE) {
        bool left = (*action == GESTURE_FROM_LEFT && direction == LV_DIR_RIGHT);
        bool right = (*action == GESTURE_FROM_RIGHT && direction == LV_DIR_LEFT);
        bool up = (*action == GESTURE_FROM_BOTTOM && direction == LV_DIR_TOP);
        if(left) home_goto_page(home_page - 1);
        else if(right) home_goto_page(home_page + 1);
        else if(up) {
            if(!switcher_is_open) switcher_open_fn();
        }
        return;
    }
    if(app_state != APP_STATE_OPEN) return;

    /* Vuốt trái từ mép phải (và vuốt phải từ mép trái) = Back về màn hình
     * trước: Cài đặt đang ở trang chi tiết thì về danh sách, nếu không đóng
     * app về Home. Vuốt lên từ dưới luôn về Home. */
    bool back_swipe = (*action == GESTURE_FROM_LEFT && direction == LV_DIR_RIGHT) ||
                      (*action == GESTURE_FROM_RIGHT && direction == LV_DIR_LEFT);
    bool home_swipe = (*action == GESTURE_FROM_BOTTOM && direction == LV_DIR_TOP);
    if(back_swipe) {
        extern bool hachimi_settings_handle_back(void);
        if(selected_launch != NULL && selected_launch->app != NULL &&
           selected_launch->app->id == HACHIMI_APP_SETTINGS &&
           hachimi_settings_handle_back()) {
            return;
        }
        close_app();
    }
    else if(home_swipe) {
        close_app();
    }
}

static lv_obj_t * create_gesture_zone(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                                      lv_coord_t width, lv_coord_t height,
                                      const app_gesture_action_t * action)
{
    lv_obj_t * zone = lv_obj_create(parent);
    lv_obj_remove_style_all(zone);
    lv_obj_set_pos(zone, x, y);
    lv_obj_set_size(zone, width, height);
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
    /* Luôn hiện để vuốt Home (chuyển trang / đa nhiệm) và vuốt app (Back/Home)
     * đều nhận được. Zone mép 36 px không che icon. */
    lv_obj_remove_flag(zone, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(zone, app_gesture_event_cb, LV_EVENT_GESTURE, (void *)action);
    return zone;
}

static void set_glass_panel(lv_obj_t * panel, lv_coord_t radius, lv_opa_t opacity)
{
    lv_obj_remove_style_all(panel);
    lv_obj_set_style_bg_color(panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, opacity, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, radius, LV_PART_MAIN);
}

static void place_launch(app_launch_t * launch, lv_coord_t x, lv_coord_t y)
{
    launch->x = x;
    launch->y = y;
    if(launch->image != NULL) {
        lv_obj_set_pos(launch->image, x, y);
        /* The shipped icon art is 64x64; stretch it into the iOS-sized square
         * so every canvas draws the same proportions. */
        lv_obj_set_size(launch->image, metrics.icon, metrics.icon);
        lv_image_set_inner_align(launch->image, LV_IMAGE_ALIGN_STRETCH);
        lv_image_set_antialias(launch->image, true);
    }
    if(launch->label != NULL) {
        lv_obj_set_width(launch->label, metrics.label_width);
        lv_obj_set_style_text_font(launch->label,
                                   hachimi_ui_font((unsigned)metrics.label_font), LV_PART_MAIN);
        /* iOS draws Home labels in white over the wallpaper. */
        lv_obj_set_style_text_color(launch->label,
            lv_color_hex(metrics.portrait ? 0xf7fbff : 0x202124), LV_PART_MAIN);
        lv_obj_align_to(launch->label, launch->image, LV_ALIGN_OUT_BOTTOM_MID,
                        0, metrics.label_gap);
    }
    if(launch->badge != NULL) {
        lv_obj_set_pos(launch->badge, x - 8, y - 8);
    }
}

static void persist_desktop_state(void)
{
    /* Session store: icon order + hidden mask. /etc is writable during the
     * session; the durable copy is deferred (prefs slot) so the oldest build
     * is never wedged by a schema change. Best-effort, never fatal. */
    FILE * f = fopen("/etc/qeaf-apps.conf", "w");
    if(f == NULL) return;
    fprintf(f, "hidden=");
    for(size_t i = 0; i < home_launch_count; i++)
        if(app_launches[i].hidden)
            fprintf(f, "%d,", (int)app_launches[i].app->id);
    fprintf(f, "\norder=");
    bool first = true;
    for(size_t i = 0; i < home_launch_count; i++) {
        if(app_launches[i].hidden) continue;
        fprintf(f, "%s%d", first ? "" : ",", (int)app_launches[i].app->id);
        first = false;
    }
    fprintf(f, "\n");
    fclose(f);
}

static void relayout_home(void)
{
    layout_home_icons();
    layout_dock();
}

static void hide_launch(app_launch_t * launch)
{
    if(launch == NULL || launch->dock || launch->hidden) return;
    launch->hidden = true;
    if(launch->image != NULL) lv_obj_add_flag(launch->image, LV_OBJ_FLAG_HIDDEN);
    if(launch->label != NULL) lv_obj_add_flag(launch->label, LV_OBJ_FLAG_HIDDEN);
    if(launch->badge != NULL) lv_obj_add_flag(launch->badge, LV_OBJ_FLAG_HIDDEN);
    /* Keep visible apps as a prefix so the drop math stays a plain rotate. */
    size_t at = 0;
    for(size_t i = 0; i < home_launch_count; i++)
        if(&app_launches[i] == launch) { at = i; break; }
    app_launch_t tmp = app_launches[at];
    for(size_t i = at; i + 1 < home_launch_count; i++) app_launches[i] = app_launches[i + 1];
    app_launches[home_launch_count - 1] = tmp;
    persist_desktop_state();
    relayout_home();
}

static void load_desktop_state(void)
{
    /* Apply a saved session order/hidden mask, if one was written this boot
     * already (previous session on the same tmpfs). Unknown ids are ignored. */
    FILE * f = fopen("/etc/qeaf-apps.conf", "r");
    if(f == NULL) return;
    char line[256];
    int hidden_ids[32];
    size_t hidden_count = 0;
    int order_ids[32];
    size_t order_count = 0;
    while(fgets(line, sizeof(line), f) != NULL) {
        if(strncmp(line, "hidden=", 7) == 0) {
            char * p = line + 7;
            while(*p && hidden_count < 32) {
                char * end;
                long v = strtol(p, &end, 10);
                if(end == p) break;
                hidden_ids[hidden_count++] = (int)v;
                p = end;
                if(*p == ',') p++;
            }
        }
        else if(strncmp(line, "order=", 6) == 0) {
            char * p = line + 6;
            while(*p && order_count < 32) {
                char * end;
                long v = strtol(p, &end, 10);
                if(end == p) break;
                order_ids[order_count++] = (int)v;
                p = end;
                if(*p == ',') p++;
            }
        }
    }
    fclose(f);
    for(size_t i = 0; i < hidden_count; i++) {
        for(size_t j = 0; j < home_launch_count; j++) {
            if(!app_launches[j].hidden && (int)app_launches[j].app->id == hidden_ids[i]) {
                app_launches[j].hidden = true;
                if(app_launches[j].image != NULL) lv_obj_add_flag(app_launches[j].image, LV_OBJ_FLAG_HIDDEN);
                if(app_launches[j].label != NULL) lv_obj_add_flag(app_launches[j].label, LV_OBJ_FLAG_HIDDEN);
                break;
            }
        }
    }
    if(order_count == 0) return;
    /* Reorder the visible prefix stably by the saved order. */
    app_launch_t visible[24];
    size_t visible_count = 0;
    for(size_t i = 0; i < home_launch_count; i++)
        if(!app_launches[i].hidden) visible[visible_count++] = app_launches[i];
    size_t next = 0;
    for(size_t i = 0; i < order_count && next < visible_count; i++) {
        for(size_t j = 0; j < visible_count; j++) {
            if((int)visible[j].app->id == order_ids[i]) {
                app_launches[next++] = visible[j];
                visible[j].app = NULL;
                break;
            }
        }
    }
    for(size_t j = 0; j < visible_count && next < visible_count; j++)
        if(visible[j].app != NULL) app_launches[next++] = visible[j];
}

static void create_app(lv_obj_t * parent, const hachimi_app_descriptor_t * app, bool dock_icon)
{
    if(app_launch_count >= MAX_APP_LAUNCHES) return;
    app_launch_t * launch = &app_launches[app_launch_count++];
    launch->app = app;
    launch->x = 0;
    launch->y = 0;
    launch->image = NULL;
    launch->label = NULL;
    launch->badge = NULL;
    launch->dock = dock_icon;
    launch->hidden = false;

    lv_obj_t * image = lv_image_create(parent);
    lv_image_set_src(image, app->image);
    lv_obj_set_pos(image, 0, 0);
    lv_obj_add_flag(image, LV_OBJ_FLAG_CLICKABLE);
    /* Chạm nhanh mở app; nhấn giữ (LONG_PRESSED) mới vào edit mode. */
    if(!dock_icon) lv_obj_add_event_cb(image, icon_longpress_event_cb, LV_EVENT_LONG_PRESSED, launch);
    lv_obj_add_event_cb(image, open_app_event_cb, LV_EVENT_CLICKED, launch);
    lv_obj_add_event_cb(image, icon_drag_event_cb, LV_EVENT_PRESSED, launch);
    lv_obj_add_event_cb(image, icon_drag_event_cb, LV_EVENT_PRESSING, launch);
    lv_obj_add_event_cb(image, icon_drag_event_cb, LV_EVENT_RELEASED, launch);
    launch->image = image;

    if(app->label_image != NULL) {
        lv_obj_t * label = lv_label_create(parent);
        lv_label_set_text(label, homo_i18n_app_name((int)app->id));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
        if(!dock_icon) lv_obj_add_event_cb(label, icon_longpress_event_cb, LV_EVENT_LONG_PRESSED, launch);
        lv_obj_add_event_cb(label, open_app_event_cb, LV_EVENT_CLICKED, launch);
        lv_obj_add_event_cb(label, icon_drag_event_cb, LV_EVENT_PRESSED, launch);
        lv_obj_add_event_cb(label, icon_drag_event_cb, LV_EVENT_PRESSING, launch);
        lv_obj_add_event_cb(label, icon_drag_event_cb, LV_EVENT_RELEASED, launch);
        launch->label = label;
    }

    /* Uninstall badge (X), visible only in edit mode. */
    lv_obj_t * badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 22, 22);
    lv_obj_set_style_radius(badge, 11, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xe03131), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t * x = lv_label_create(badge);
    lv_label_set_text(x, LV_SYMBOL_CLOSE);
    lv_obj_center(x);
    lv_obj_set_style_text_font(x, hachimi_ui_font(12), LV_PART_MAIN);
    lv_obj_set_style_text_color(x, lv_color_white(), LV_PART_MAIN);
    lv_obj_clear_flag(x, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(badge, badge_event_cb, LV_EVENT_CLICKED, launch);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
    launch->badge = badge;
}

static lv_color_t status_bar_foreground(lv_color_t background)
{
    lv_color32_t rgba = lv_color_to_32(background, LV_OPA_COVER);
    bool light_background = lv_color32_luminance(rgba) >= 128;
    return light_background ? lv_color_hex(0x202124) : lv_color_white();
}

static lv_obj_t * create_status_bar(lv_obj_t * parent, lv_color_t background)
{
    lv_color_t foreground = status_bar_foreground(background);
    return ios_status_bar_create(parent, foreground);
}

static void layout_home_icons(void)
{
    if(home_launch_count == 0) return;
    /* Hidden (uninstalled) apps sit at the tail and are skipped. */
    size_t visible_count = 0;
    for(size_t i = 0; i < home_launch_count; i++)
        if(!app_launches[i].hidden) visible_count++;
    if(visible_count == 0) return;
    int columns = metrics.columns;
    int rows = (int)((visible_count + (size_t)columns - 1) / (size_t)columns);
    int top = metrics.first_row_top;
    int step = metrics.row_pitch;
    if(step > 0) {
        /* Portrait: the iPhone row pitch is fixed, so a page with fewer than
         * six rows keeps iOS spacing and leaves the floor empty, exactly like
         * an iPhone with 13 apps. Only a full page is compressed. */
        if(metrics.rows_capacity > 0 && rows > metrics.rows_capacity) {
            int available = metrics.dock_top - metrics.first_row_top;
            step = rows > 0 ? available / rows : step;
        }
    }
    else {
        int available = metrics.dock_top - 24 - top;
        step = rows > 0 ? available / rows : 0;
        if(step > 120) step = 120;
        if(step < 90) step = 90;
    }

    /* Home paging: portrait 8 icons/page (4x2). Swipe left/right slides. */
    size_t per_page = metrics.portrait ? 8 : 12;
    size_t page_start = (size_t)home_page * per_page;
    size_t slot = 0;
    for(size_t i = 0; i < home_launch_count; i++) {
        if(app_launches[i].hidden) continue;
        if(slot < page_start || slot >= page_start + per_page) {
            if(app_launches[i].image != NULL) lv_obj_add_flag(app_launches[i].image, LV_OBJ_FLAG_HIDDEN);
            if(app_launches[i].label != NULL) lv_obj_add_flag(app_launches[i].label, LV_OBJ_FLAG_HIDDEN);
            if(app_launches[i].badge != NULL) lv_obj_add_flag(app_launches[i].badge, LV_OBJ_FLAG_HIDDEN);
            slot++;
            continue;
        }
        if(app_launches[i].image != NULL) lv_obj_remove_flag(app_launches[i].image, LV_OBJ_FLAG_HIDDEN);
        if(app_launches[i].label != NULL) lv_obj_remove_flag(app_launches[i].label, LV_OBJ_FLAG_HIDDEN);
        size_t local = slot - page_start;
        int column = (int)(local % (size_t)columns);
        int row = (int)(local / (size_t)columns);
        int center = ios_column_center(&metrics, column);
        place_launch(&app_launches[i], (lv_coord_t)(center - metrics.icon / 2 + page_slide_x),
                     (lv_coord_t)(top + row * step));
        slot++;
    }
}

static void layout_dock(void)
{
    if(dock == NULL) return;
    size_t dock_count = app_launch_count - home_launch_count;
    if(dock_count == 0) return;

    int x, width, y;
    if(metrics.portrait) {
        /* The iPhone dock is a wide glass slab with the icons spread evenly. */
        x = metrics.dock_margin_x;
        width = display_w - 2 * x;
        y = metrics.dock_top;
        lv_obj_set_style_radius(dock, metrics.dock_radius, LV_PART_MAIN);
        for(size_t i = 0; i < dock_count; i++) {
            app_launch_t * launch = &app_launches[home_launch_count + i];
            int center = x + (int)((2 * i + 1) * width / (2 * dock_count));
            int icon_y = y + (metrics.dock_height - metrics.icon) / 2;
            place_launch(launch, (lv_coord_t)(center - metrics.icon / 2), (lv_coord_t)icon_y);
        }
        lv_obj_set_pos(dock, x, y);
        lv_obj_set_size(dock, width, metrics.dock_height);
        return;
    }

    int spacing = 78;
    if(dock_count > 1) {
        int maximum = (display_w - 20 - 96) / (int)(dock_count - 1);
        if(maximum < spacing) spacing = maximum;
    }
    if(spacing < 24) spacing = 24;
    width = dock_count > 1 ? (int)(dock_count - 1) * spacing + 96 : 96;
    x = (display_w - width) / 2;
    y = metrics.dock_top;
    lv_obj_set_style_radius(dock, metrics.dock_radius, LV_PART_MAIN);
    lv_obj_set_pos(dock, x, y);
    lv_obj_set_size(dock, width, metrics.dock_height);

    for(size_t i = 0; i < dock_count; i++) {
        app_launch_t * launch = &app_launches[home_launch_count + i];
        int center = x + 48 + (int)i * spacing;
        int icon_y = y + (metrics.dock_height - metrics.icon) / 2;
        place_launch(launch, (lv_coord_t)(center - metrics.icon / 2), (lv_coord_t)icon_y);
    }
}

static void layout_shell(void)
{
    lv_obj_t * screen = lv_screen_active();
    if(screen == NULL) return;
    lv_obj_set_size(screen, display_w, display_h);

    if(wallpaper != NULL) {
        bool portrait = portrait_shell();
        if(portrait != wallpaper_is_portrait) {
            lv_image_set_src(wallpaper, portrait ? IOS_WALLPAPER_PORTRAIT
                                                 : IOS_WALLPAPER_LANDSCAPE);
            wallpaper_is_portrait = portrait;
        }
        lv_obj_set_pos(wallpaper, 0, 0);
        lv_obj_set_size(wallpaper, display_w, display_h);
        lv_image_set_inner_align(wallpaper, LV_IMAGE_ALIGN_STRETCH);
    }
    if(status_bar != NULL) ios_status_bar_set_size(status_bar, display_w, display_h);
    layout_home_icons();
    layout_dock();
    if(home_bar != NULL) {
        lv_obj_set_size(home_bar, metrics.home_bar_w, metrics.home_bar_h);
        lv_obj_set_pos(home_bar, (display_w - metrics.home_bar_w) / 2, metrics.home_bar_y);
    }
    if(switcher != NULL) {
        lv_obj_set_pos(switcher, 0, 0);
        lv_obj_set_size(switcher, display_w, display_h);
    }
}

static void layout_app_surfaces(void)
{
    if(transition_background != NULL) {
        lv_obj_set_pos(transition_background, 0, 0);
        lv_obj_set_size(transition_background, display_w, display_h);
    }
    if(app_view != NULL) {
        lv_obj_set_pos(app_view, 0, 0);
        lv_obj_set_size(app_view, display_w, display_h);
    }
    if(app_mask != NULL) {
        lv_obj_set_size(app_mask, display_w, display_h);
    }
    layout_app_content();
    layout_gestures();
}

void hachimi_desktop_relayout(void)
{
    if(app_view == NULL) return;
    measure_display();
    layout_shell();
    layout_app_surfaces();
    hachimi_control_center_relayout();
    if(switcher_is_open) switcher_open_fn();
    if(pending_launch != NULL && app_display_ready() && app_state == APP_STATE_IDLE) {
        app_launch_t * launch = pending_launch;
        pending_launch = NULL;
        begin_launch(launch);
    }
}

bool hachimi_desktop_app_mode(void)
{
    return app_view != NULL && app_mode;
}

bool hachimi_desktop_app_is_settings(void)
{
    app_launch_t * l = pending_launch != NULL ? pending_launch : selected_launch;
    return l != NULL && l->app != NULL && l->app->id == HACHIMI_APP_SETTINGS;
}

void hachimi_desktop_set_status_bar_visible(bool visible)
{
    if(status_bar == NULL) return;
    if(visible) lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(status_bar, LV_OBJ_FLAG_HIDDEN);
}

static void create_dock(lv_obj_t * parent)
{
    dock = lv_obj_create(parent);
    set_glass_panel(dock, 25, 95);
    lv_obj_set_style_shadow_color(dock, lv_color_hex(0x443c38), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(dock, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dock, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(dock, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dock, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    size_t dock_count = 0;
    const hachimi_app_descriptor_t * dock_apps = hachimi_app_dock_list(&dock_count);
    for(size_t i = 0; i < dock_count; i++) {
        create_app(parent, &dock_apps[i], true);
    }
}

static void create_app_view(lv_obj_t * parent)
{
    transition_background = lv_image_create(parent);
    lv_obj_set_pos(transition_background, 0, 0);
    /* Let LVGL's cover search skip the original desktop underneath. */
    lv_obj_set_style_bg_opa(transition_background, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(transition_background, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(transition_background, LV_OBJ_FLAG_HIDDEN);
    app_view = lv_obj_create(parent);
    lv_obj_remove_style_all(app_view);
    lv_obj_set_size(app_view, display_w, display_h);
    lv_obj_set_pos(app_view, 0, 0);
    lv_obj_set_style_bg_color(app_view, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(app_view, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(app_view, 0, LV_PART_MAIN);
    lv_obj_clear_flag(app_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(app_view, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    app_title = lv_label_create(app_view);
    lv_obj_set_style_align(app_title, LV_ALIGN_TOP_LEFT, LV_PART_MAIN);
    lv_obj_set_width(app_title, 600);
    lv_obj_set_style_text_font(app_title, hachimi_ui_font(28), LV_PART_MAIN);
    lv_obj_set_style_text_color(app_title, lv_color_hex(0x202124), LV_PART_MAIN);
    lv_obj_set_style_text_align(app_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(app_title, homo_i18n_app_title(HACHIMI_APP_BROWSER));
    lv_obj_clear_flag(app_title, LV_OBJ_FLAG_CLICKABLE);

    hachimi_app_manager_init(&app_manager, app_view);

    home_indicator = lv_obj_create(app_view);
    lv_obj_remove_style_all(home_indicator);
    lv_obj_set_style_align(home_indicator, LV_ALIGN_TOP_LEFT, LV_PART_MAIN);
    lv_obj_set_size(home_indicator, 104, 5);
    lv_obj_set_style_bg_color(home_indicator, lv_color_hex(0x202124), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(home_indicator, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(home_indicator, 3, LV_PART_MAIN);
    lv_obj_clear_flag(home_indicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    layout_app_content();

    app_mask = lv_obj_create(parent);
    lv_obj_remove_style_all(app_mask);
    lv_obj_set_size(app_mask, display_w, display_h);
    lv_obj_set_style_bg_color(app_mask, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(app_mask, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(app_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(app_mask, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_clip_corner(app_mask, true, LV_PART_MAIN);
    transition_content = lv_image_create(app_mask);
    lv_obj_set_size(transition_content, lv_pct(100), lv_pct(100));
    lv_image_set_inner_align(transition_content, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(transition_content, false);
    lv_obj_clear_flag(transition_content, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(transition_content, LV_OBJ_FLAG_HIDDEN);

    gesture_zones[0] = create_gesture_zone(parent, 0, 60, EDGE_GESTURE_SIDE_SIZE,
                                          display_h - 60 - EDGE_GESTURE_BOTTOM_SIZE,
                                          &gesture_actions[0]);
    gesture_zones[1] = create_gesture_zone(parent, display_w - EDGE_GESTURE_SIDE_SIZE, 60,
                                          EDGE_GESTURE_SIDE_SIZE,
                                          display_h - 60 - EDGE_GESTURE_BOTTOM_SIZE,
                                          &gesture_actions[1]);
    gesture_zones[2] = create_gesture_zone(parent, EDGE_GESTURE_SIDE_SIZE,
                                          display_h - EDGE_GESTURE_BOTTOM_SIZE,
                                          display_w - EDGE_GESTURE_SIDE_SIZE * 2,
                                          EDGE_GESTURE_BOTTOM_SIZE, &gesture_actions[2]);
}

static bool control_center_can_open(void)
{
    return app_state == APP_STATE_IDLE || app_state == APP_STATE_OPEN;
}

bool hachimi_desktop_create(const hachimi_app_catalog_t * catalog)
{
    if(app_view != NULL || !hachimi_app_registry_configure(catalog)) return false;
    homo_i18n_attach();
    measure_display();
    app_launch_count = 0;
    home_launch_count = 0;
    selected_launch = NULL;
    pending_launch = NULL;
    app_state = APP_STATE_IDLE;
    app_mode = false;
    edit_mode = false;
    dragging = NULL;
    home_page = 0;
    page_slide_x = 0;
    switcher = NULL;
    switcher_list = NULL;
    switcher_is_open = false;
    wallpaper_is_portrait = !portrait_shell();
    lv_obj_t * screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x202020),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_size(screen, display_w, display_h);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    wallpaper = lv_image_create(screen);
    lv_image_set_src(wallpaper, portrait_shell() ? IOS_WALLPAPER_PORTRAIT
                                                 : IOS_WALLPAPER_LANDSCAPE);
    lv_obj_set_pos(wallpaper, 0, 0);
    lv_obj_set_size(wallpaper, display_w, display_h);
    lv_image_set_inner_align(wallpaper, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_add_flag(wallpaper, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wallpaper, wallpaper_tap_event_cb, LV_EVENT_CLICKED, NULL);

    /* The wallpaper's status-bar strip is dark blue, so white ink wins. */
    status_bar = create_status_bar(screen, lv_color_hex(0x2b6ca8));

    size_t home_count = 0;
    const hachimi_app_descriptor_t * home_apps = hachimi_app_home_list(&home_count);
    for(size_t i = 0; i < home_count; i++) {
        create_app(screen, &home_apps[i], false);
    }
    home_launch_count = app_launch_count;
    load_desktop_state();

    create_dock(screen);

    home_bar = lv_obj_create(screen);
    lv_obj_remove_style_all(home_bar);
    lv_obj_set_style_bg_color(home_bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(home_bar, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(home_bar, 3, LV_PART_MAIN);
    lv_obj_add_flag(home_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(home_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(home_bar, home_bar_event_cb, LV_EVENT_CLICKED, NULL);

    create_app_view(screen);

    /* Multitasking switcher: tap the Home bar to open. */
    switcher = lv_obj_create(screen);
    lv_obj_remove_style_all(switcher);
    lv_obj_set_pos(switcher, 0, 0);
    lv_obj_set_size(switcher, display_w, display_h);
    lv_obj_set_style_bg_color(switcher, lv_color_hex(0x10131a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(switcher, LV_OPA_90, LV_PART_MAIN);
    lv_obj_clear_flag(switcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(switcher, LV_OBJ_FLAG_HIDDEN);
    switcher_list = lv_obj_create(switcher);
    lv_obj_remove_style_all(switcher_list);
    lv_obj_set_pos(switcher_list, 30, 80);
    lv_obj_set_size(switcher_list, display_w - 60, display_h - 120);
    lv_obj_clear_flag(switcher_list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(switcher_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(switcher_list, 10, LV_PART_MAIN);

    layout_shell();
    layout_app_surfaces();
    hachimi_app_system_start();
    hachimi_control_center_create(control_center_can_open);
    return true;
}

void hachimi_desktop_destroy(void)
{
    hachimi_control_center_destroy();
    finish_direct_transition();
    if(app_view == NULL) return;
    hachimi_app_system_stop();
    lv_anim_delete(&app_transition, app_transition_step);
    hachimi_app_manager_destroy(&app_manager);
    lv_obj_clean(lv_screen_active());
    if(transition_content_buffer != NULL) {
        lv_image_cache_drop(transition_content_buffer);
        lv_draw_buf_destroy(transition_content_buffer);
        transition_content_buffer = NULL;
    }
    if(transition_background_buffer != NULL) {
        lv_image_cache_drop(transition_background_buffer);
        lv_draw_buf_destroy(transition_background_buffer);
        transition_background_buffer = NULL;
    }
    transition_background = NULL;
    transition_content = NULL;
    app_view = app_mask = app_title = home_indicator = NULL;
    wallpaper = status_bar = dock = home_bar = NULL;
    switcher = switcher_list = NULL;
    switcher_is_open = false;
    edit_mode = false;
    dragging = NULL;
    selected_launch = NULL;
    pending_launch = NULL;
    app_launch_count = 0;
    home_launch_count = 0;
    app_state = APP_STATE_IDLE;
    app_mode = false;
}

void hachimi_desktop_set_transition_backend(const hachimi_desktop_transition_backend_t * backend)
{
    if(backend == NULL) memset(&transition_backend, 0, sizeof(transition_backend));
    else transition_backend = *backend;
}
