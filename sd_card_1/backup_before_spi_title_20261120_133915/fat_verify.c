#include "fat_verify.h"
#include "lvgl.h"
#include "ff.h"
#include "fat_utf8.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define PAGE_SIZE 5U
#define PATH_CAPACITY 512U
#define MAX_DEPTH 32U
typedef struct {
    char alias[40];
    char display_name[768];
    DWORD size;
    bool folder;
} RootEntry;

static FATFS filesystem;
static DIR directory;
static FILINFO info;
static TCHAR long_name[256];
static TCHAR unicode_path[512];
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

/* LVGL font-only read-only filesystem. File data remains on the SD card. */
static FIL font_files[2];
static bool font_used[2];
static lv_font_t *browser_font;
static void *font_open(lv_fs_drv_t *driver, const char *path, lv_fs_mode_t mode)
{
    (void)driver;
    if (mode != LV_FS_MODE_RD) return NULL;
    const TCHAR jp[] = {'0',':','/','F','O','N','T','J','P','.','T','T','C',0};
    const TCHAR cn[] = {'0',':','/','F','O','N','T','C','N','.','T','T','C',0};
    unsigned index;
    const TCHAR *file;
    while (*path == '/') ++path;
    if (!strcmp(path, "FONTJP.TTC")) { index = 0; file = jp; }
    else if (!strcmp(path, "FONTCN.TTC")) { index = 1; file = cn; }
    else return NULL;
    if (font_used[index]) return NULL;
    FRESULT result = f_open(&font_files[index], file, FA_READ);
    printf("[FONT OPEN] %s FAT=%u\r\n", path, result);
    if (result != FR_OK) return NULL;
    printf("[FONT SIZE] %s bytes=%lu\r\n", path, (unsigned long)f_size(&font_files[index]));
    font_used[index] = true;
    return &font_files[index];
}
static lv_fs_res_t font_close(lv_fs_drv_t *driver, void *file)
{
    (void)driver;
    FRESULT result = f_close(file);
    for (unsigned i = 0; i < 2; ++i) if (file == &font_files[i]) font_used[i] = false;
    return result == FR_OK ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}
static lv_fs_res_t font_read(lv_fs_drv_t *driver, void *file, void *buffer, uint32_t size, uint32_t *read)
{
    (void)driver;
    UINT actual = 0;
    FRESULT result = f_read(file, buffer, size, &actual);
    *read = actual;
    return result == FR_OK ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}
static lv_fs_res_t font_seek(lv_fs_drv_t *driver, void *file, uint32_t offset, lv_fs_whence_t origin)
{
    (void)driver;
    FIL *stream = file;
    uint64_t position = offset;
    if (origin == LV_FS_SEEK_CUR) position += f_tell(stream);
    else if (origin == LV_FS_SEEK_END) position += f_size(stream);
    if (position > f_size(stream)) return LV_FS_RES_INV_PARAM;
    return f_lseek(stream, (DWORD)position) == FR_OK ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}
static lv_fs_res_t font_tell(lv_fs_drv_t *driver, void *file, uint32_t *position)
{
    (void)driver;
    *position = f_tell((FIL *)file);
    return LV_FS_RES_OK;
}
static void load_browser_font(lv_obj_t *screen)
{
    (void)screen;
    static lv_fs_drv_t driver;
    lv_fs_drv_init(&driver);
    driver.letter = 'S';
    driver.cache_size = 2048;
    driver.open_cb = font_open;
    driver.close_cb = font_close;
    driver.read_cb = font_read;
    driver.seek_cb = font_seek;
    driver.tell_cb = font_tell;
    lv_fs_drv_register(&driver);
    browser_font = lv_tiny_ttf_create_file_ex("S:/FONTJP.TTC", 16, LV_FONT_KERNING_NONE, 32);
    printf("[FONT INIT] JP_done=%u; loading CN\r\n", browser_font != NULL);
    lv_font_t *chinese = lv_tiny_ttf_create_file_ex("S:/FONTCN.TTC", 16, LV_FONT_KERNING_NONE, 32);
    printf("[FONT INIT] CN_done=%u\r\n", chinese != NULL);
    if (browser_font) {
        browser_font->fallback = chinese ? chinese : LV_FONT_DEFAULT;
    } else browser_font = chinese;
    if (browser_font) {
        /* Keep frequently refreshed ASCII controls on the built-in flash font.
         * Assign the SD font only to rows containing non-ASCII characters.
         */
        lv_font_glyph_dsc_t glyph;
        memset(&glyph, 0, sizeof(glyph));
        printf("[FONT PROBE] Chinese start\r\n");
        bool chinese_ok = lv_font_get_glyph_dsc(browser_font, &glyph, 0x4E2D, 0);
        bool chinese_missing = glyph.is_placeholder;
        memset(&glyph, 0, sizeof(glyph));
        printf("[FONT PROBE] Japanese start\r\n");
        bool japanese_ok = lv_font_get_glyph_dsc(browser_font, &glyph, 0x3042, 0);
        printf("[FONT GLYPH] Chinese=%u Japanese=%u\r\n",
               chinese_ok && !chinese_missing, japanese_ok && !glyph.is_placeholder);
    } else {
        lv_label_set_text(status_label, "Copy FONTJP.TTC + FONTCN.TTC");
    }
    printf("[FONT READY] usable=%u CN_created=%u\r\n", browser_font != NULL, chinese != NULL);
    printf("[FONT] JP=%u CN=%u (copy FONTJP.TTC and FONTCN.TTC into SD root)\r\n",
           font_used[0], font_used[1]);
}

static const lv_font_t *font_for_text(const char *text)
{
    if (browser_font) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
            if (*p >= 0x80) return browser_font;
    }
    return LV_FONT_DEFAULT;
}

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
        lv_obj_set_style_text_font(status_label, font_for_text(entry->display_name), 0);
        lv_label_set_text_fmt(status_label, "%s / %lu B", entry->display_name, (unsigned long)entry->size);
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
    lv_label_set_text(status_label, "Loading SD fonts...");
    lv_screen_load(screen);
    lv_refr_now(NULL);
    FRESULT result = f_mount(&filesystem, (const TCHAR[]){'0',':',0}, 1);
    mounted = result == FR_OK;
    if (!mounted) {
        lv_label_set_text_fmt(status_label, "Mount failed: %u", result);
        printf("[ROOT FAIL] mount=%u\r\n", result);
    } else {
        load_browser_font(screen);
        pending = true;
        lv_label_set_text(status_label, "Reading root...");
    }
}

void RootBrowser_Process(void)
{
    if (!mounted) return;
    if (pending) {
        pending = false;
        scan_result = fat_path_utf16(requested_path, unicode_path, 512)
                      ? f_opendir(&directory, unicode_path) : FR_INVALID_NAME;
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
        lv_obj_set_style_text_font(status_label, LV_FONT_DEFAULT, 0);
        lv_label_set_text(status_label, "Reading folder...");
        memset(&info, 0, sizeof(info));
        info.lfname = long_name;
        info.lfsize = sizeof(long_name) / sizeof(long_name[0]);
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
        char alias[40];
        if (!fat_name_utf8(info.fname, alias, sizeof(alias))) continue;
        if (!folder && !is_mp3(alias)) continue;
        if (seen++ < page * PAGE_SIZE) continue;
        if (count == PAGE_SIZE) { complete = true; break; }
        RootEntry *entry = &entries[count++];
        snprintf(entry->alias, sizeof(entry->alias), "%s", alias);
        fat_name_utf8(long_name[0] ? long_name : info.fname, entry->display_name, sizeof(entry->display_name));
        entry->folder = folder;
        entry->size = info.fsize;
        printf("[ROOT] %s %s [%s]\r\n", folder ? "DIR" : "MP3",
               entry->display_name, entry->alias);
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
        lv_obj_set_style_text_font(names[i], font_for_text(entries[i].display_name), 0);
        lv_label_set_text_fmt(names[i], "%s %s", entries[i].folder ? "[DIR]" : "[MP3]", entries[i].display_name);
        lv_obj_remove_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (page) lv_obj_remove_state(previous_button, LV_STATE_DISABLED);
    if (seen > (page + 1) * PAGE_SIZE) lv_obj_remove_state(next_button, LV_STATE_DISABLED);
    if (!browser_font) lv_label_set_text(status_label, "Missing SD fonts; check serial");
    else lv_label_set_text_fmt(status_label, count ? "Page %u" : "No folders or MP3 files", page + 1);
}
