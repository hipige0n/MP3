#include "fat_verify.h"
#include "lvgl.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define PAGE_SIZE 5U
#define PATH_CAPACITY 512U
#define MAX_DEPTH 32U
typedef struct {
    char alias[13];
    DWORD size;
    bool folder;
} RootEntry;

static FATFS filesystem;
static DIR directory;
static FILINFO info;
static char long_name[256];
static RootEntry entries[PAGE_SIZE];
static lv_obj_t *rows[PAGE_SIZE], *names[PAGE_SIZE], *status_label;
static lv_obj_t *previous_button, *next_button;
static unsigned page, count, seen;
static bool pending, scanning, mounted;
static FRESULT scan_result;
static char current_path[PATH_CAPACITY] = "0:/";
static char requested_path[PATH_CAPACITY] = "0:/";
static unsigned parent_length[MAX_DEPTH], parent_page[MAX_DEPTH];
static unsigned depth, requested_page;
typedef enum { NAV_PAGE, NAV_ENTER, NAV_BACK } Navigation;
static Navigation navigation;
static lv_obj_t *back_button, *path_label;

static void update_header(void)
{
    lv_label_set_text(path_label, current_path);
    if (depth && !scanning) lv_obj_remove_state(back_button, LV_STATE_DISABLED);
    else lv_obj_add_state(back_button, LV_STATE_DISABLED);
}

static void back_clicked(lv_event_t *event)
{
    (void)event;
    if (pending || scanning || !mounted || !depth) return;
    memcpy(requested_path, current_path, parent_length[depth - 1]);
    requested_path[parent_length[depth - 1]] = 0;
    requested_page = parent_page[depth - 1];
    navigation = NAV_BACK;
    pending = true;
}

static bool is_mp3(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && strlen(ext) == 4 &&
           (ext[1] == 'm' || ext[1] == 'M') &&
           (ext[2] == 'p' || ext[2] == 'P') && ext[3] == '3';
}

static void page_clicked(lv_event_t *event)
{
    if (pending || scanning || !mounted) return;
    requested_page = page;
    if (lv_event_get_user_data(event)) ++requested_page;
    else if (requested_page) --requested_page;
    snprintf(requested_path, sizeof(requested_path), "%s", current_path);
    navigation = NAV_PAGE;
    pending = true;
}

static void entry_clicked(lv_event_t *event)
{
    if (pending || scanning) return;
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index >= count) return;
    RootEntry *entry = &entries[index];
    char path[PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s%s%s", current_path,
                          current_path[strlen(current_path) - 1] == '/' ? "" : "/",
                          entry->alias);
    if (length < 0 || (unsigned)length >= sizeof(path)) {
        lv_label_set_text(status_label, "Path too long");
        return;
    }
    if (entry->folder) {
        if (depth >= MAX_DEPTH) {
            lv_label_set_text(status_label, "Maximum folder depth reached");
            return;
        }
        snprintf(requested_path, sizeof(requested_path), "%s", path);
        requested_page = 0;
        navigation = NAV_ENTER;
        pending = true;
    } else {
        lv_label_set_text_fmt(status_label, "%s / %lu B", entry->alias, (unsigned long)entry->size);
        printf("[FILE SELECT] %s size=%lu\r\n", path, (unsigned long)entry->size);
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int width, int height,
                             const char *text, lv_event_cb_t callback, void *argument)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, argument);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void FAT_Verify(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x17212b), 0);
    back_button = make_button(screen, 8, 3, 65, 28, "Back", back_clicked, NULL);
    path_label = lv_label_create(screen);
    lv_obj_set_pos(path_label, 80, 9);
    lv_obj_set_width(path_label, 151);
    lv_label_set_long_mode(path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(path_label, lv_color_white(), 0);
    update_header();
    for (unsigned i = 0; i < PAGE_SIZE; ++i) {
        rows[i] = make_button(screen, 8, 35 + i * 43, 224, 37, "", entry_clicked,
                              (void *)(uintptr_t)i);
        names[i] = lv_obj_get_child(rows[i], 0);
        lv_obj_set_width(names[i], 202);
        lv_label_set_long_mode(names[i], LV_LABEL_LONG_DOT);
        lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    previous_button = make_button(screen, 8, 255, 103, 30, "Previous", page_clicked, NULL);
    next_button = make_button(screen, 129, 255, 103, 30, "Next", page_clicked, (void *)1);
    status_label = lv_label_create(screen);
    lv_obj_set_pos(status_label, 8, 293);
    lv_obj_set_width(status_label, 224);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_screen_load(screen);
    FRESULT result = f_mount(&filesystem, "0:", 1);
    mounted = result == FR_OK;
    if (!mounted) {
        lv_label_set_text_fmt(status_label, "Mount failed: %u", result);
        printf("[ROOT FAIL] mount=%u\r\n", result);
    } else {
        pending = true;
        lv_label_set_text(status_label, "Reading root...");
    }
}

void RootBrowser_Process(void)
{
    if (!mounted) return;
    if (pending) {
        pending = false;
        scan_result = f_opendir(&directory, requested_path);
        if (scan_result != FR_OK) {
            lv_label_set_text_fmt(status_label, "Open failed: %u", scan_result);
            printf("[BROWSER FAIL] %s result=%u\r\n", requested_path, scan_result);
            return;
        }
        if (navigation == NAV_ENTER) {
            parent_length[depth] = strlen(current_path);
            parent_page[depth] = page;
            ++depth;
        } else if (navigation == NAV_BACK) {
            --depth;
        }
        snprintf(current_path, sizeof(current_path), "%s", requested_path);
        page = requested_page;
        count = seen = 0;
        scanning = true;
        update_header();
        lv_obj_add_state(previous_button, LV_STATE_DISABLED);
        lv_obj_add_state(next_button, LV_STATE_DISABLED);
        for (unsigned i = 0; i < PAGE_SIZE; ++i) lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(status_label, "Reading folder...");
        memset(&info, 0, sizeof(info));
        info.lfname = long_name;
        info.lfsize = sizeof(long_name);
        printf("[BROWSER OPEN] %s page=%u depth=%u\r\n", current_path, page + 1, depth);
    }
    if (!scanning) return;
    bool complete = false;
    for (unsigned budget = 0; budget < 8; ++budget) {
        long_name[0] = 0;
        scan_result = f_readdir(&directory, &info);
        if (scan_result != FR_OK || !info.fname[0]) { complete = true; break; }
        if (info.fname[0] == '.' || (info.fattrib & (AM_HID | AM_SYS))) continue;
        bool folder = (info.fattrib & AM_DIR) != 0;
        if (!folder && !is_mp3(info.fname)) continue;
        if (seen++ < page * PAGE_SIZE) continue;
        if (count == PAGE_SIZE) { complete = true; break; }
        RootEntry *entry = &entries[count++];
        snprintf(entry->alias, sizeof(entry->alias), "%s", info.fname);
        entry->folder = folder;
        entry->size = info.fsize;
        printf("[ROOT] %s %s [%s]\r\n", folder ? "DIR" : "MP3",
               long_name[0] ? long_name : info.fname, info.fname);
    }
    if (!complete) return;
    FRESULT close_result = f_closedir(&directory);
    scanning = false;
    update_header();
    if (scan_result == FR_OK) scan_result = close_result;
    if (scan_result != FR_OK) {
        lv_label_set_text_fmt(status_label, "Read failed: %u", scan_result);
        return;
    }
    for (unsigned i = 0; i < count; ++i) {
        lv_label_set_text_fmt(names[i], "%s %s", entries[i].folder ? "[DIR]" : "[MP3]", entries[i].alias);
        lv_obj_remove_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (page) lv_obj_remove_state(previous_button, LV_STATE_DISABLED);
    if (seen > (page + 1) * PAGE_SIZE) lv_obj_remove_state(next_button, LV_STATE_DISABLED);
    lv_label_set_text_fmt(status_label, count ? "Page %u" : "No folders or MP3 files", page + 1);
}
