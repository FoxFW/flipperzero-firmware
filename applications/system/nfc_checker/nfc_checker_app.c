/**
 * @file nfc_checker_app.c
 * @brief NFC Checker: menu navigation, tag acquisition and results rendering.
 *
 * Flow:
 *   1. Baseline tag  -> read live (with a save prompt) or pick a /nfc file
 *   2. Target tag    -> read live or pick a /nfc file
 *   3. nfc_checker_compare() produces the Emulation Success Score
 *   4. Scrollable results screen
 */
#include "nfc_checker_app.h"

#include <stdio.h>
#include <string.h>

#define NFC_CHECKER_ROW_TOP (20)
#define NFC_CHECKER_ROW_PITCH (9)
#define NFC_CHECKER_VALUE_X (120)
#define NFC_CHECKER_SCROLL_X (124)

static void nfc_checker_app_menu_update(NfcCheckerApp* app);
static void nfc_checker_app_switch(NfcCheckerApp* app, NfcCheckerViewId view);
static void nfc_checker_app_menu_callback(void* context, uint32_t index);
static void nfc_checker_app_start_read(NfcCheckerApp* app);
static void nfc_checker_app_read_finished(NfcCheckerReadResult result, void* context);
static void nfc_checker_app_handle_read(NfcCheckerApp* app);
static void nfc_checker_app_show_save_dialog(NfcCheckerApp* app);
static void nfc_checker_app_save_result(DialogExResult result, void* context);
static void nfc_checker_app_stage_done(NfcCheckerApp* app);
static void nfc_checker_app_start_file_browser(NfcCheckerApp* app);
static void nfc_checker_app_browser_callback(void* context);
static void nfc_checker_app_handle_file_picked(NfcCheckerApp* app);
static void nfc_checker_app_show_results(NfcCheckerApp* app);
static void nfc_checker_app_reset(NfcCheckerApp* app);
static void nfc_checker_app_show_message(
    NfcCheckerApp* app,
    const char* header,
    const char* text);
static bool nfc_checker_app_custom_event(void* context, uint32_t event);
static bool nfc_checker_app_navigation(void* context);
static void nfc_checker_app_results_draw(Canvas* canvas, void* model);
static bool nfc_checker_app_results_input(InputEvent* event, void* context);

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static NfcCheckerTag* nfc_checker_app_stage_tag(NfcCheckerApp* app) {
    return (app->stage == NfcCheckerStageBaseline) ? app->baseline : app->target;
}

static void nfc_checker_app_switch(NfcCheckerApp* app, NfcCheckerViewId view) {
    app->view = view;
    view_dispatcher_switch_to_view(app->view_dispatcher, view);
}

static void nfc_checker_app_show_message(
    NfcCheckerApp* app,
    const char* header,
    const char* text) {
    popup_reset(app->popup);
    popup_set_header(app->popup, header, 64, 2, AlignCenter, AlignTop);
    popup_set_text(app->popup, text, 64, 22, AlignCenter, AlignTop);
    nfc_checker_app_switch(app, NfcCheckerViewPopup);
}

static void nfc_checker_app_menu_update(NfcCheckerApp* app) {
    submenu_reset(app->menu);
    submenu_set_header(
        app->menu,
        (app->stage == NfcCheckerStageBaseline) ? "1/2 Baseline Tag" : "2/2 Target Tag");
    submenu_add_item(
        app->menu, "Read New Tag", NfcCheckerMenuReadTag, nfc_checker_app_menu_callback, app);
    submenu_add_item(
        app->menu,
        "Select Saved Tag",
        NfcCheckerMenuSelectFile,
        nfc_checker_app_menu_callback,
        app);
    submenu_set_selected_item(app->menu, 0);
}

static void nfc_checker_app_reset(NfcCheckerApp* app) {
    app->stage = NfcCheckerStageBaseline;
    nfc_checker_tag_reset(app->baseline);
    nfc_checker_tag_reset(app->target);
    nfc_checker_app_menu_update(app);
    nfc_checker_app_switch(app, NfcCheckerViewMenu);
}

/* ------------------------------------------------------------------------- */
/* Step 1 & 2: tag acquisition                                              */
/* ------------------------------------------------------------------------- */

static void nfc_checker_app_start_read(NfcCheckerApp* app) {
    NfcCheckerTag* tag = nfc_checker_app_stage_tag(app);
    nfc_checker_tag_reset(tag);

    app->reading = true;
    app->read_result = NfcCheckerReadFailed;

    nfc_checker_app_show_message(
        app, "Reading Tag", "Hold the tag\nagainst the back\nof the Flipper");

    nfc_checker_reader_start(
        app->reader, tag, app->live_device, nfc_checker_app_read_finished, app);
}

static void nfc_checker_app_read_finished(NfcCheckerReadResult result, void* context) {
    NfcCheckerApp* app = context;
    app->read_result = result;
    view_dispatcher_send_custom_event(app->view_dispatcher, NfcCheckerEventReadComplete);
}

static void nfc_checker_app_handle_read(NfcCheckerApp* app) {
    /* The reader thread is finished or about to be; collect it before the
     * snapshot is used. */
    nfc_checker_reader_join(app->reader);

    switch(app->read_result) {
    case NfcCheckerReadOk:
        if(app->stage == NfcCheckerStageBaseline) {
            nfc_checker_app_show_save_dialog(app);
        } else {
            nfc_checker_app_show_results(app);
        }
        break;
    case NfcCheckerReadNoTag:
        nfc_checker_app_show_message(
            app, "No Tag Found", "Hold the tag flat\nagainst the back\nand try again");
        break;
    case NfcCheckerReadUnsupported: {
        const char* name = nfc_checker_reader_detected_name(app->reader);
        char text[96];
        snprintf(
            text,
            sizeof(text),
            "%s\nfound, but the UID\ncould not be read",
            (name[0] != '\0') ? name : "A tag");
        nfc_checker_app_show_message(app, "Not Readable", text);
        break;
    }
    case NfcCheckerReadAborted:
        nfc_checker_app_switch(app, NfcCheckerViewMenu);
        break;
    case NfcCheckerReadFailed:
    default:
        nfc_checker_app_show_message(
            app, "Read Failed", "The tag did not\nrespond. Try again");
        break;
    }
}

static void nfc_checker_app_show_save_dialog(NfcCheckerApp* app) {
    nfc_checker_build_save_path(app->baseline, app->save_path);

    char uid[4 * NFC_CHECKER_UID_MAX_SIZE] = {0};
    nfc_checker_tag_uid_to_string(app->baseline, uid, sizeof(uid));

    char text[64];
    snprintf(text, sizeof(text), "UID %s\nSave to /nfc?", uid);

    dialog_ex_reset(app->save_dialog);
    /* dialog_ex_reset() drops the context and the result callback, so they
     * have to be installed again for every dialog. */
    dialog_ex_set_context(app->save_dialog, app);
    dialog_ex_set_result_callback(app->save_dialog, nfc_checker_app_save_result);

    dialog_ex_set_header(app->save_dialog, "Save Tag?", 64, 2, AlignCenter, AlignTop);
    dialog_ex_set_text(app->save_dialog, text, 64, 22, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(app->save_dialog, "Save");
    dialog_ex_set_right_button_text(app->save_dialog, "Skip");

    nfc_checker_app_switch(app, NfcCheckerViewSaveDialog);
}

static void nfc_checker_app_save_result(DialogExResult result, void* context) {
    NfcCheckerApp* app = context;
    bool saved = true;

    if(result == DialogExResultLeft) {
        saved = nfc_checker_save_file(
            furi_string_get_cstr(app->save_path), app->live_device);
    }

    nfc_checker_app_stage_done(app);

    if(!saved) {
        /* The tag is still in memory, so the comparison can continue. */
        nfc_checker_app_show_message(app, "Save Failed", "Tag kept in memory\nfor this session");
    }
}

static void nfc_checker_app_stage_done(NfcCheckerApp* app) {
    if(app->stage == NfcCheckerStageBaseline) {
        app->stage = NfcCheckerStageTarget;
    }
    nfc_checker_app_menu_update(app);
    nfc_checker_app_switch(app, NfcCheckerViewMenu);
}

static void nfc_checker_app_start_file_browser(NfcCheckerApp* app) {
    furi_string_reset(app->file_path);
    furi_string_set(app->browser_path, NFC_CHECKER_FOLDER);

    if(app->browser_running) {
        file_browser_stop(app->browser);
        app->browser_running = false;
    }

    file_browser_configure(
        app->browser, NFC_CHECKER_FILE_EXTENSION, NFC_CHECKER_FOLDER, true, true, NULL, false);
    file_browser_set_callback(app->browser, nfc_checker_app_browser_callback, app);
    file_browser_start(app->browser, app->browser_path);
    app->browser_running = true;

    nfc_checker_app_switch(app, NfcCheckerViewBrowser);
}

static void nfc_checker_app_browser_callback(void* context) {
    NfcCheckerApp* app = context;
    /* Do the real work outside of the browser input handler. */
    view_dispatcher_send_custom_event(app->view_dispatcher, NfcCheckerEventFilePicked);
}

static void nfc_checker_app_handle_file_picked(NfcCheckerApp* app) {
    if(app->browser_running) {
        file_browser_stop(app->browser);
        app->browser_running = false;
    }

    if(furi_string_empty(app->file_path)) {
        nfc_checker_app_switch(app, NfcCheckerViewMenu);
        return;
    }

    NfcCheckerTag* tag = nfc_checker_app_stage_tag(app);
    if(!nfc_checker_load_file(furi_string_get_cstr(app->file_path), app->live_device, tag)) {
        nfc_checker_app_show_message(
            app, "Load Failed", "Could not read this\n.nfc file");
        return;
    }

    if(app->stage == NfcCheckerStageBaseline) {
        nfc_checker_app_stage_done(app);
    } else {
        nfc_checker_app_show_results(app);
    }
}

static void nfc_checker_app_menu_callback(void* context, uint32_t index) {
    NfcCheckerApp* app = context;

    if(index == NfcCheckerMenuReadTag) {
        nfc_checker_app_start_read(app);
    } else if(index == NfcCheckerMenuSelectFile) {
        nfc_checker_app_start_file_browser(app);
    }
}

/* ------------------------------------------------------------------------- */
/* Step 3 & 4: comparison and results                                       */
/* ------------------------------------------------------------------------- */

static void nfc_checker_app_show_results(NfcCheckerApp* app) {
    nfc_checker_compare(app->baseline, app->target, &app->report);

    NfcCheckerResultsModel* model = view_get_model(app->results_view);
    memset(model, 0, sizeof(NfcCheckerResultsModel));
    model->report = app->report;
    model->selected = 0;
    model->scroll = 0;
    view_commit_model(app->results_view, true);

    nfc_checker_app_switch(app, NfcCheckerViewResults);
}

static void nfc_checker_app_draw_status_icon(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    NfcCheckerCheckStatus status) {
    switch(status) {
    case NfcCheckerCheckStatusOk:
        canvas_draw_line(canvas, x, y + 2, x + 1, y + 4);
        canvas_draw_line(canvas, x + 1, y + 4, x + 4, y);
        break;
    case NfcCheckerCheckStatusFail:
        canvas_draw_line(canvas, x, y, x + 4, y + 4);
        canvas_draw_line(canvas, x + 4, y, x, y + 4);
        break;
    case NfcCheckerCheckStatusWarn:
        canvas_draw_line(canvas, x + 2, y, x + 2, y + 2);
        canvas_draw_dot(canvas, x + 2, y + 4);
        break;
    case NfcCheckerCheckStatusInfo:
    default:
        canvas_draw_frame(canvas, x, y, 5, 5);
        break;
    }
}

static void nfc_checker_app_results_draw(Canvas* canvas, void* model_pointer) {
    NfcCheckerResultsModel* model = model_pointer;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    /* Header: Emulation Score: XX% */
    canvas_draw_box(canvas, 0, 0, 128, 13);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, 10, "Emulation Score");

    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%u%%", (unsigned)model->report.score);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 125, 10, AlignRight, AlignBottom, buffer);

    /* Score gauge */
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 1, 14, 126, 6);
    uint8_t fill = (uint8_t)((120u * model->report.score) / 100u);
    if(fill > 0) {
        canvas_draw_box(canvas, 3, 15, fill, 4);
    }

    /* Check list */
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t row = 0; row < NFC_CHECKER_VISIBLE_ROWS; row++) {
        uint8_t index = model->scroll + row;
        if(index >= model->report.check_count) {
            break;
        }

        const NfcCheckerCheck* check = &model->report.checks[index];
        uint8_t top = NFC_CHECKER_ROW_TOP + row * NFC_CHECKER_ROW_PITCH;
        bool selected = (index == model->selected);

        canvas_set_color(canvas, ColorBlack);
        if(selected) {
            canvas_draw_box(canvas, 0, top, 122, NFC_CHECKER_ROW_PITCH);
            canvas_set_color(canvas, ColorWhite);
        }

        nfc_checker_app_draw_status_icon(canvas, 3, top + 2, check->status);
        canvas_draw_str(canvas, 11, top + 7, check->label);
        canvas_draw_str_aligned(
            canvas, NFC_CHECKER_VALUE_X, top + 7, AlignRight, AlignBottom, check->value);
    }

    /* Scrollbar */
    if(model->report.check_count > NFC_CHECKER_VISIBLE_ROWS) {
        const uint8_t top = NFC_CHECKER_ROW_TOP;
        const uint8_t height = NFC_CHECKER_VISIBLE_ROWS * NFC_CHECKER_ROW_PITCH;
        uint8_t thumb =
            (uint8_t)(((uint16_t)height * NFC_CHECKER_VISIBLE_ROWS) /
                      model->report.check_count);
        if(thumb < 6) {
            thumb = 6;
        }
        uint8_t max_scroll = model->report.check_count - NFC_CHECKER_VISIBLE_ROWS;
        uint8_t offset = max_scroll ?
                             (uint8_t)(((uint16_t)(height - thumb) * model->scroll) /
                                       max_scroll) :
                             0;

        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, NFC_CHECKER_SCROLL_X, top, 3, height);
        if(thumb > 2) {
            canvas_draw_box(canvas, NFC_CHECKER_SCROLL_X + 1, top + 1 + offset, 1, thumb - 2);
        }
    }

    /* Detail of the selected row */
    if(model->selected < model->report.check_count) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_str(canvas, 2, 63, model->report.checks[model->selected].detail);
    }
}

static bool nfc_checker_app_results_input(InputEvent* event, void* context) {
    NfcCheckerApp* app = context;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    bool consumed = true;

    switch(event->key) {
    case InputKeyUp: {
        NfcCheckerResultsModel* model = view_get_model(app->results_view);
        if(model->selected > 0) {
            model->selected--;
            if(model->selected < model->scroll) {
                model->scroll = model->selected;
            }
        }
        view_commit_model(app->results_view, true);
        break;
    }
    case InputKeyDown: {
        NfcCheckerResultsModel* model = view_get_model(app->results_view);
        if(model->selected + 1 < model->report.check_count) {
            model->selected++;
            if(model->selected >= model->scroll + NFC_CHECKER_VISIBLE_ROWS) {
                model->scroll = model->selected - NFC_CHECKER_VISIBLE_ROWS + 1;
            }
        }
        view_commit_model(app->results_view, true);
        break;
    }
    case InputKeyOk:
        view_dispatcher_send_custom_event(app->view_dispatcher, NfcCheckerEventResultsExit);
        break;
    default:
        consumed = false;
        break;
    }

    return consumed;
}

/* ------------------------------------------------------------------------- */
/* Dispatcher callbacks                                                     */
/* ------------------------------------------------------------------------- */

static bool nfc_checker_app_custom_event(void* context, uint32_t event) {
    NfcCheckerApp* app = context;

    switch(event) {
    case NfcCheckerEventReadComplete:
        nfc_checker_reader_join(app->reader);
        if(!app->reading) {
            /* The user cancelled while the hardware was still scanning. */
            return true;
        }
        app->reading = false;
        nfc_checker_app_handle_read(app);
        return true;
    case NfcCheckerEventFilePicked:
        nfc_checker_app_handle_file_picked(app);
        return true;
    case NfcCheckerEventResultsExit:
        nfc_checker_app_reset(app);
        return true;
    default:
        return false;
    }
}

static bool nfc_checker_app_navigation(void* context) {
    NfcCheckerApp* app = context;

    switch(app->view) {
    case NfcCheckerViewMenu:
        if(app->stage == NfcCheckerStageTarget) {
            app->stage = NfcCheckerStageBaseline;
            nfc_checker_app_menu_update(app);
            return true;
        }
        view_dispatcher_stop(app->view_dispatcher);
        return true;

    case NfcCheckerViewPopup:
        if(app->reading) {
            app->reading = false;
            nfc_checker_reader_abort(app->reader);
        }
        nfc_checker_app_switch(app, NfcCheckerViewMenu);
        return true;

    case NfcCheckerViewBrowser:
        if(app->browser_running) {
            file_browser_stop(app->browser);
            app->browser_running = false;
        }
        nfc_checker_app_switch(app, NfcCheckerViewMenu);
        return true;

    case NfcCheckerViewSaveDialog:
        /* Back means "do not save". */
        nfc_checker_app_stage_done(app);
        return true;

    case NfcCheckerViewResults:
        nfc_checker_app_reset(app);
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------- */

static NfcCheckerApp* nfc_checker_app_alloc(void) {
    NfcCheckerApp* app = malloc(sizeof(NfcCheckerApp));
    furi_check(app);
    memset(app, 0, sizeof(NfcCheckerApp));

    app->storage = furi_record_open(RECORD_STORAGE);
    app->gui = furi_record_open(RECORD_GUI);

    app->baseline = nfc_checker_tag_alloc();
    app->target = nfc_checker_tag_alloc();
    app->live_device = nfc_device_alloc();
    app->reader = nfc_checker_reader_alloc();

    app->file_path = furi_string_alloc();
    app->save_path = furi_string_alloc();
    app->browser_path = furi_string_alloc();

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, nfc_checker_app_custom_event);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, nfc_checker_app_navigation);

    app->menu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, NfcCheckerViewMenu, submenu_get_view(app->menu));

    app->popup = popup_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, NfcCheckerViewPopup, popup_get_view(app->popup));

    app->save_dialog = dialog_ex_alloc();
    dialog_ex_set_context(app->save_dialog, app);
    dialog_ex_set_result_callback(app->save_dialog, nfc_checker_app_save_result);
    view_dispatcher_add_view(
        app->view_dispatcher,
        NfcCheckerViewSaveDialog,
        dialog_ex_get_view(app->save_dialog));

    app->browser = file_browser_alloc(app->file_path);
    file_browser_set_callback(app->browser, nfc_checker_app_browser_callback, app);
    view_dispatcher_add_view(
        app->view_dispatcher, NfcCheckerViewBrowser, file_browser_get_view(app->browser));

    app->results_view = view_alloc();
    view_set_context(app->results_view, app);
    view_allocate_model(
        app->results_view, ViewModelTypeLocking, sizeof(NfcCheckerResultsModel));
    view_set_draw_callback(app->results_view, nfc_checker_app_results_draw);
    view_set_input_callback(app->results_view, nfc_checker_app_results_input);
    view_dispatcher_add_view(
        app->view_dispatcher, NfcCheckerViewResults, app->results_view);

    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->stage = NfcCheckerStageBaseline;
    app->view = NfcCheckerViewMenu;
    app->read_result = NfcCheckerReadFailed;

    return app;
}

static void nfc_checker_app_free(NfcCheckerApp* app) {
    furi_assert(app);

    /* Stop the reader before any of its buffers go away. */
    nfc_checker_reader_abort(app->reader);
    nfc_checker_reader_free(app->reader);

    if(app->browser_running) {
        file_browser_stop(app->browser);
        app->browser_running = false;
    }

    view_dispatcher_remove_view(app->view_dispatcher, NfcCheckerViewResults);
    view_free(app->results_view);

    view_dispatcher_remove_view(app->view_dispatcher, NfcCheckerViewBrowser);
    file_browser_free(app->browser);

    view_dispatcher_remove_view(app->view_dispatcher, NfcCheckerViewSaveDialog);
    dialog_ex_free(app->save_dialog);

    view_dispatcher_remove_view(app->view_dispatcher, NfcCheckerViewPopup);
    popup_free(app->popup);

    view_dispatcher_remove_view(app->view_dispatcher, NfcCheckerViewMenu);
    submenu_free(app->menu);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    nfc_device_free(app->live_device);
    nfc_checker_tag_free(app->baseline);
    nfc_checker_tag_free(app->target);

    furi_string_free(app->file_path);
    furi_string_free(app->save_path);
    furi_string_free(app->browser_path);

    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t nfc_checker_app(void* p) {
    UNUSED(p);

    NfcCheckerApp* app = nfc_checker_app_alloc();
    nfc_checker_app_menu_update(app);
    nfc_checker_app_switch(app, NfcCheckerViewMenu);

    view_dispatcher_run(app->view_dispatcher);

    nfc_checker_app_free(app);
    return 0;
}
