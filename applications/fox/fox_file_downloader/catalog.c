#include "catalog.h"
#include "download.h"
#include "json_mini.h"
#include "fox_file_downloader_icons.h"

#include <storage/storage.h>
#include <furi_hal_version.h>
#include <furi_hal_info.h>
#include <gui/icon.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char* label;
    const char* id;
} CatalogCategoryInfo;

static const CatalogCategoryInfo k_categories[CatalogCategoryCount] = {
    [CatalogCategoryBluetooth] = {"Bluetooth", "64a69817effe1f448a4053b4"},
    [CatalogCategoryGames] = {"Games", "64971d11be1a76c06747de2f"},
    [CatalogCategoryGPIO] = {"GPIO", "64971d106617ba37a4bc79b9"},
    [CatalogCategoryInfrared] = {"Infrared", "64971d106617ba37a4bc79b6"},
    [CatalogCategoryIButton] = {"iButton", "64971d11be1a76c06747de29"},
    [CatalogCategoryMedia] = {"Media", "64971d116617ba37a4bc79bc"},
    [CatalogCategoryNFC] = {"NFC", "64971d10be1a76c06747de26"},
    [CatalogCategoryRFID] = {"RFID", "64971d10577d519190ede5c2"},
    [CatalogCategorySubGHz] = {"Sub-GHz", "64971d0f6617ba37a4bc79b3"},
    [CatalogCategoryTools] = {"Tools", "64971d11577d519190ede5c5"},
    [CatalogCategoryUSB] = {"USB", "64971d11be1a76c06747de2c"},
};

#define FOX_CATALOG_PAGE_PATH FOX_DOWNLOAD_DATA_DIR "/catalog_page.json"
#define CATALOG_PAGE_LIMIT    25
#define CATALOG_OBJ_BUF_MAX   768

static void draw_wrapped(Canvas* canvas, int32_t x, int32_t y, int32_t max_w, int32_t line_h, int32_t max_lines, const char* text) {
    int32_t line_y = y;
    int32_t lines_drawn = 0;
    size_t i = 0;
    size_t len = strlen(text);

    while(i < len && lines_drawn < max_lines) {
        size_t line_start = i;
        size_t last_break = 0;
        size_t j = i;
        char buf[64];

        while(j < len) {
            size_t n = j - line_start;
            if(n + 1 >= sizeof(buf)) break;
            memcpy(buf, text + line_start, n + 1);
            buf[n + 1] = '\0';
            if(canvas_string_width(canvas, buf) > max_w) break;
            if(text[j] == ' ') last_break = j;
            j++;
        }

        size_t cut = j;
        if(j < len && last_break > line_start) cut = last_break;
        if(cut == line_start) cut = j;

        char line[64];
        size_t n = cut - line_start;
        if(n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, text + line_start, n);
        line[n] = '\0';
        canvas_draw_str(canvas, x, line_y, line);

        line_y += line_h;
        lines_drawn++;
        i = cut;
        while(i < len && text[i] == ' ') i++;
    }
}

void catalog_open(App* app) {
    if(app->catalog_disclaimer_shown) {
        app_switch_to_menu(app, MenuContextCatalog);
        return;
    }
    app->catalog_disclaimer_shown = true;
    app->menu_context = MenuContextCatalog;
    catalog_show_disclaimer(
        app, "Firmware API is 88.5, most catalog apps are still 87.x - installs may fail for now.", false);
}

void catalog_show_disclaimer(App* app, const char* text, bool is_mismatch) {
    snprintf(app->catalog_disclaimer_text, sizeof(app->catalog_disclaimer_text), "%s", text);
    app->catalog_disclaimer_is_mismatch = is_mismatch;
    app_log(app, "%s", text);
    app->current_view = FoxDownloaderViewCatalogDisclaimer;
    view_dispatcher_switch_to_view(app->view_dispatcher, FoxDownloaderViewCatalogDisclaimer);
}

#define CATALOG_DISCLAIMER_BAR_H 16

static App* s_catalog_disclaimer_app = NULL;

static void catalog_disclaimer_draw(Canvas* canvas, void* model) {
    UNUSED(model);
    App* app = s_catalog_disclaimer_app;
    if(!app) return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    draw_wrapped(canvas, 4, 8, 120, 10, 4, app->catalog_disclaimer_text);

    int32_t bar_y = 64 - CATALOG_DISCLAIMER_BAR_H;
    const Icon* icon = &I_ButtonCenter_7x7;
    int32_t icon_w = icon_get_width(icon);
    int32_t icon_h = icon_get_height(icon);
    int32_t icon_gap = 3;
    int32_t pad_x = 10;
    int32_t content_w = icon_w + icon_gap + (int32_t)canvas_string_width(canvas, "OK");
    int32_t btn_w = content_w + pad_x * 2;
    int32_t x = (128 - btn_w) / 2;

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rbox(canvas, x, bar_y, btn_w, CATALOG_DISCLAIMER_BAR_H, 3);
    canvas_set_color(canvas, ColorWhite);

    int32_t gx = x + (btn_w - content_w) / 2;
    int32_t gy_icon = bar_y + (CATALOG_DISCLAIMER_BAR_H - icon_h) / 2;
    canvas_draw_icon(canvas, gx, gy_icon, icon);
    canvas_draw_str_aligned(
        canvas, gx + icon_w + icon_gap, bar_y + CATALOG_DISCLAIMER_BAR_H / 2, AlignLeft, AlignCenter, "OK");

    canvas_set_color(canvas, ColorBlack);
}

static bool catalog_disclaimer_input(InputEvent* event, void* context) {
    App* app = context;
    if(event->type != InputTypeShort) return false;

    switch(event->key) {
    case InputKeyOk:

        if(app->catalog_disclaimer_is_mismatch) {
            if(app->download_return_view == FoxDownloaderViewMenu) {
                app_switch_to_menu(app, app->menu_return_context);
            } else {
                app->current_view = app->download_return_view;
                view_dispatcher_switch_to_view(app->view_dispatcher, app->download_return_view);
            }
        } else {
            app_switch_to_menu(app, MenuContextCatalog);
        }
        return true;
    default:
        return false;
    }
}

View* catalog_disclaimer_view_alloc(App* app) {
    s_catalog_disclaimer_app = app;
    View* v = view_alloc();
    view_set_draw_callback(v, catalog_disclaimer_draw);
    view_set_input_callback(v, catalog_disclaimer_input);
    view_set_context(v, app);
    view_allocate_model(v, ViewModelTypeLocking, sizeof(uint8_t));
    return v;
}

void catalog_disclaimer_view_free(View* v) {
    s_catalog_disclaimer_app = NULL;
    view_free(v);
}

void catalog_render_menu(App* app) {
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "App Catalog");
    for(size_t i = 0; i < CatalogCategoryCount; i++) {
        submenu_add_item(app->submenu, k_categories[i].label, i, app_menu_item_callback, app);
    }
}

static void catalog_fetch_page(App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, "/ext/apps_data");
    storage_simply_mkdir(storage, FOX_DOWNLOAD_DATA_DIR);
    furi_record_close(RECORD_STORAGE);

    snprintf(
        app->download_url,
        sizeof(app->download_url),
        "https://catalog.flipperzero.one/api/v0/0/application?limit=%d&is_latest_release_version=true&offset=%zu&sort_by=updated_at&sort_order=-1&category_id=%s",
        CATALOG_PAGE_LIMIT,
        app->catalog_page_offset,
        k_categories[app->catalog_category].id);
    snprintf(app->download_path, sizeof(app->download_path), "%s", FOX_CATALOG_PAGE_PATH);
    char combined_name[FOX_DOWNLOAD_NAME_MAX];
    snprintf(
        combined_name,
        sizeof(combined_name),
        "App Directory: %s",
        k_categories[app->catalog_category].label);
    snprintf(
        app->download_found_name,
        sizeof(app->download_found_name),
        "%s",
        strlen(combined_name) <= 20 ? combined_name : "App Directory");
    app->download_purpose = DownloadPurposeCatalogPage;

    app_start_download(app);
}

void catalog_menu_select(App* app, uint32_t index) {
    if(index >= CatalogCategoryCount) return;
    app->catalog_category = (CatalogCategory)index;
    app->catalog_page_offset = 0;
    app->catalog_has_more = false;
    app->catalog_page_nav_backward = false;
    catalog_fetch_page(app);
}

void catalog_free_buffers(App* app) {
    if(app->catalog_apps) {
        free(app->catalog_apps);
        app->catalog_apps = NULL;
    }
}

void catalog_show_app_list(App* app) {
    if(!app->catalog_apps) app->catalog_apps = malloc(FOX_CATALOG_APP_MAX * sizeof(CatalogAppEntry));
    if(!app->catalog_apps) {
        app_log(app, "Out of memory.");
        app_render_log(app);
        return;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, FOX_CATALOG_PAGE_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    if(!opened) {
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        app_log(app, "No response from the catalog.");
        app_render_log(app);
        return;
    }

    JsonFileReader reader;
    json_file_reader_init(&reader, file);

    app->catalog_app_count = 0;
    size_t raw_count = 0;
    char obj_buf[CATALOG_OBJ_BUF_MAX];

    while(app->catalog_app_count < FOX_CATALOG_APP_MAX &&
          json_file_reader_next_object(&reader, obj_buf, sizeof(obj_buf))) {
        raw_count++;

        CatalogAppEntry* entry = &app->catalog_apps[app->catalog_app_count];
        if(!json_mini_get_string(obj_buf, "alias", entry->alias, sizeof(entry->alias))) continue;

        const char* cv = strstr(obj_buf, "\"current_version\"");
        if(!cv) continue;
        while(*cv && *cv != '{') cv++;
        if(!*cv) continue;

        char cv_buf[CATALOG_OBJ_BUF_MAX];
        const char* cv_end = json_object_end(cv);
        size_t cv_len = (size_t)(cv_end - cv);
        if(cv_len >= sizeof(cv_buf)) cv_len = sizeof(cv_buf) - 1;
        memcpy(cv_buf, cv, cv_len);
        cv_buf[cv_len] = '\0';

        if(!json_mini_get_string(cv_buf, "name", entry->name, sizeof(entry->name))) continue;
        json_mini_get_string(
            cv_buf, "short_description", entry->description, sizeof(entry->description));
        json_mini_get_string(cv_buf, "version", entry->version, sizeof(entry->version));
        if(!json_mini_get_string(cv_buf, "_id", entry->build_id, sizeof(entry->build_id))) continue;

        app->catalog_app_count++;
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(app->catalog_app_count == 0) {
        app_log(app, "No more apps in this category.");
        app_render_log(app);
        return;
    }

    app->catalog_has_more = (raw_count >= CATALOG_PAGE_LIMIT);
    app->catalog_selected = app->catalog_page_nav_backward ? app->catalog_app_count - 1 : 0;
    app->catalog_page_nav_backward = false;
    app->current_view = FoxDownloaderViewCatalogAppList;
    view_dispatcher_switch_to_view(app->view_dispatcher, FoxDownloaderViewCatalogAppList);
    with_view_model(app->catalog_app_list_view, uint8_t * m, { UNUSED(m); }, true);
}

static void catalog_install_selected(App* app) {
    if(app->catalog_app_count == 0) return;
    const CatalogAppEntry* entry = &app->catalog_apps[app->catalog_selected];

    uint8_t target = furi_hal_version_get_hw_target();
    uint16_t api_major = 0, api_minor = 0;
    furi_hal_info_get_api_version(&api_major, &api_minor);

    snprintf(
        app->download_url,
        sizeof(app->download_url),
        "https://catalog.flipperzero.one/api/v0/application/version/%s/build/compatible?target=f%d&api=%d.%d",
        entry->build_id,
        target,
        api_major,
        api_minor);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    char category_dir[64];
    snprintf(
        category_dir,
        sizeof(category_dir),
        "%s/%s",
        FOX_APPS_DIR,
        k_categories[app->catalog_category].label);
    storage_simply_mkdir(storage, FOX_APPS_DIR);
    storage_simply_mkdir(storage, category_dir);
    furi_record_close(RECORD_STORAGE);

    snprintf(
        app->download_path,
        sizeof(app->download_path),
        "%s/%s.fap",
        category_dir,
        entry->alias);

    snprintf(app->download_found_name, sizeof(app->download_found_name), "%s", entry->name);
    snprintf(
        app->download_found_type,
        sizeof(app->download_found_type),
        "App  v%s",
        entry->version[0] ? entry->version : "?");
    app->download_found_size = 0;
    app->download_found_type_suspicious = false;
    app->download_purpose = DownloadPurposeCatalogInstall;
    app->download_return_view = FoxDownloaderViewCatalogAppList;
    app->download_found_focus_left = false;
    app->current_view = FoxDownloaderViewDownloadFound;
    view_dispatcher_switch_to_view(app->view_dispatcher, FoxDownloaderViewDownloadFound);
}

static App* s_catalog_list_app = NULL;

static void catalog_list_draw(Canvas* canvas, void* model) {
    UNUSED(model);
    App* app = s_catalog_list_app;
    if(!app || app->catalog_app_count == 0) return;
    const CatalogAppEntry* entry = &app->catalog_apps[app->catalog_selected];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, entry->name);

    canvas_set_font(canvas, FontSecondary);
    draw_wrapped(canvas, 4, 24, 120, 10, 3, entry->description);

    char nav[48];
    snprintf(
        nav,
        sizeof(nav),
        "P%u App %u/%u%s  v%s",
        (unsigned)(app->catalog_page_offset / CATALOG_PAGE_LIMIT) + 1,
        (unsigned)(app->catalog_selected + 1),
        (unsigned)app->catalog_app_count,
        app->catalog_has_more ? "+" : "",
        entry->version[0] ? entry->version : "?");
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, nav);
}

static bool catalog_list_input(InputEvent* event, void* context) {
    App* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(app->catalog_app_count == 0) return false;

    switch(event->key) {
    case InputKeyLeft:
    case InputKeyUp:
        if(app->catalog_selected > 0) {
            app->catalog_selected--;
            with_view_model(app->catalog_app_list_view, uint8_t * m, { UNUSED(m); }, true);
        } else if(app->catalog_page_offset > 0) {
            app->catalog_page_offset -= CATALOG_PAGE_LIMIT;
            app->catalog_page_nav_backward = true;
            catalog_fetch_page(app);
        }
        return true;
    case InputKeyRight:
    case InputKeyDown:
        if(app->catalog_selected + 1 < app->catalog_app_count) {
            app->catalog_selected++;
            with_view_model(app->catalog_app_list_view, uint8_t * m, { UNUSED(m); }, true);
        } else if(app->catalog_has_more) {
            app->catalog_page_offset += CATALOG_PAGE_LIMIT;
            app->catalog_page_nav_backward = false;
            catalog_fetch_page(app);
        }
        return true;
    case InputKeyOk:
        if(event->type == InputTypeShort) catalog_install_selected(app);
        return true;
    case InputKeyBack:
        return false;
    default:
        return false;
    }
}

View* catalog_app_list_view_alloc(App* app) {
    s_catalog_list_app = app;
    View* v = view_alloc();
    view_set_draw_callback(v, catalog_list_draw);
    view_set_input_callback(v, catalog_list_input);
    view_set_context(v, app);
    view_allocate_model(v, ViewModelTypeLocking, sizeof(uint8_t));
    return v;
}

void catalog_app_list_view_free(View* v) {
    s_catalog_list_app = NULL;
    view_free(v);
}
