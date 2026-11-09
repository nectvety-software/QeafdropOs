#include "display.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef _WIN32
#include <dirent.h>
#endif
#define BRIGHTNESS_STATE_DIR "/var/lib/hachimi"
#define BRIGHTNESS_STATE_PATH BRIGHTNESS_STATE_DIR "/display-brightness"
#define MIN_BRIGHTNESS_PERCENT 20
static char backlight_dir[192];
static bool read_brightness_file(const char * path, int * value)
{
    FILE * file = fopen(path, "r");
    if(file == NULL) return false;

    int result = fscanf(file, "%d", value);
    fclose(file);
    return result == 1;
}

/* Boards name the panel backlight differently: the simulator uses
 * /sys/class/backlight/backlight, i.MX6ULL exposes it as an LED and the phone
 * (violet) publishes /sys/class/leds/lcd-backlight. Probe once instead of
 * hardcoding one path, so the brightness slider works on all of them. */
#ifndef _WIN32
static bool usable_backlight_dir(const char * dir)
{
    char path[256];
    int maximum = 0;
    if(snprintf(path, sizeof(path), "%s/max_brightness", dir) >=
       (int)sizeof(path)) return false;
    return read_brightness_file(path, &maximum) && maximum > 0;
}

static bool scan_backlight(const char * root, bool leds_only)
{
    DIR * directory = opendir(root);
    struct dirent * entry;
    if(directory == NULL) return false;
    while((entry = readdir(directory)) != NULL) {
        char candidate[192];
        if(entry->d_name[0] == '.') continue;
        if(leds_only && strstr(entry->d_name, "lcd") == NULL &&
           strstr(entry->d_name, "backlight") == NULL &&
           strstr(entry->d_name, "wl") == NULL) continue;
        if(snprintf(candidate, sizeof(candidate), "%s/%s", root, entry->d_name) >=
           (int)sizeof(candidate)) continue;
        if(!usable_backlight_dir(candidate)) continue;
        snprintf(backlight_dir, sizeof(backlight_dir), "%s", candidate);
        closedir(directory);
        return true;
    }
    closedir(directory);
    return false;
}
#endif

static bool resolve_backlight(void)
{
    if(backlight_dir[0]) return true;
#ifdef _WIN32
    return false;
#else
    return scan_backlight("/sys/class/backlight", false) ||
           scan_backlight("/sys/class/leds", true);
#endif
}

static bool backlight_attr(const char * name, char * out, size_t capacity)
{
    if(!resolve_backlight()) return false;
    return snprintf(out, capacity, "%s/%s", backlight_dir, name) <
           (int)capacity;
}

static bool save_brightness(int percent)
{
#ifdef _WIN32
    (void)percent;
    return false; /* The simulator has no Linux backlight state directory. */
#else
    if(mkdir(BRIGHTNESS_STATE_DIR, 0755) < 0 && errno != EEXIST) return false;
    char temporary[sizeof(BRIGHTNESS_STATE_PATH) + 8];
    if(snprintf(temporary, sizeof(temporary), "%s.tmp", BRIGHTNESS_STATE_PATH) >=
       (int)sizeof(temporary)) return false;

    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if(fd < 0) return false;
    char value[8];
    int length = snprintf(value, sizeof(value), "%d\n", percent);
    bool success = length > 0 && (size_t)length < sizeof(value) &&
                   write(fd, value, (size_t)length) == (ssize_t)length && fsync(fd) == 0;
    if(close(fd) < 0) success = false;
    if(success && rename(temporary, BRIGHTNESS_STATE_PATH) < 0) success = false;
    if(!success) unlink(temporary);
    else {
        int dir_fd = open(BRIGHTNESS_STATE_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if(dir_fd >= 0) { (void)fsync(dir_fd); close(dir_fd); }
    }
    return success;
#endif
}

static bool load_brightness(int * percent)
{
    int saved = 0;
    if(!read_brightness_file(BRIGHTNESS_STATE_PATH, &saved) ||
       saved < MIN_BRIGHTNESS_PERCENT || saved > 100) return false;
    *percent = saved;
    return true;
}

bool hachimi_display_set_brightness(int percent)
{
    char path[256];
    int max_brightness = 0;
    if(!backlight_attr("max_brightness", path, sizeof(path)) ||
       !read_brightness_file(path, &max_brightness) || max_brightness <= 0) return false;

    if(percent < MIN_BRIGHTNESS_PERCENT) percent = MIN_BRIGHTNESS_PERCENT;
    if(percent > 100) percent = 100;
    int value = (max_brightness * percent + 50) / 100;
    int minimum = (max_brightness * MIN_BRIGHTNESS_PERCENT + 99) / 100;
    if(minimum < 1) minimum = 1;
    if(value < minimum) value = minimum;
    if(value > max_brightness) value = max_brightness;

    if(!backlight_attr("brightness", path, sizeof(path))) return false;
    FILE * file = fopen(path, "w");
    if(file == NULL) return false;
    bool success = fprintf(file, "%d\n", value) > 0;
    if(fclose(file) != 0) success = false;
    if(success) success = save_brightness(percent);
    return success;
}


bool hachimi_display_get_brightness(int * percent)
{
    char max_path[256], value_path[256];
    int maximum = 0, value = 0;
    if(!backlight_attr("max_brightness", max_path, sizeof(max_path)) ||
       !backlight_attr("brightness", value_path, sizeof(value_path)) ||
       !read_brightness_file(max_path, &maximum) ||
       !read_brightness_file(value_path, &value) || maximum <= 0) return false;
    *percent = (value * 100 + maximum / 2) / maximum;
    if(*percent < MIN_BRIGHTNESS_PERCENT) {
        *percent = MIN_BRIGHTNESS_PERCENT;
        if(!hachimi_display_set_brightness(*percent)) return false;
    }
    if(*percent > 100) *percent = 100;
    return true;
}

bool hachimi_display_restore_brightness(void)
{
    int percent;
    return load_brightness(&percent) && hachimi_display_set_brightness(percent);
}
