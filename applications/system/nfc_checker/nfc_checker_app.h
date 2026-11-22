/**
 * @file nfc_checker_app.h
 * @brief Application state, menu/UI identifiers and entry point.
 */
#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/popup.h>
#include <gui/modules/file_browser.h>
#include <storage/storage.h>
#include <nfc/nfc_device.h>

#include "nfc_checker_compare.h"
#include "nfc_checker_nfc.h"

/** Number of checks that fit on the results screen at once. */
#define NFC_CHECKER_VISIBLE_ROWS (4)

typedef enum {
    NfcCheckerViewMenu = 0,
    NfcCheckerViewPopup,
    NfcCheckerViewBrowser,
    NfcCheckerViewSaveDialog,
    NfcCheckerViewResults,
} NfcCheckerViewId;

typedef enum {
    NfcCheckerMenuReadTag = 0,
    NfcCheckerMenuSelectFile,
} NfcCheckerMenuItem;

typedef enum {
    NfcCheckerEventReadComplete = 100,
    NfcCheckerEventFilePicked,
    NfcCheckerEventResultsExit,
} NfcCheckerEvent;

typedef enum {
    NfcCheckerStageBaseline = 0,
    NfcCheckerStageTarget,
} NfcCheckerStage;

/** Model shared between the results view logic and its renderer. */
typedef struct {
    NfcCheckerReport report;
    uint8_t selected;
    uint8_t scroll;
} NfcCheckerResultsModel;

typedef struct NfcCheckerApp {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;

    Submenu* menu;
    Popup* popup;
    FileBrowser* browser;
    DialogEx* save_dialog;
    View* results_view;

    FuriString* file_path; /**< Filled by FileBrowser on selection. */
    FuriString* save_path;
    FuriString* browser_path;

    NfcCheckerTag* baseline;
    NfcCheckerTag* target;
    NfcDevice* live_device; /**< Backing store for live reads and dumping. */
    NfcCheckerReader* reader;

    NfcCheckerReport report;
    NfcCheckerStage stage;
    NfcCheckerViewId view;
    NfcCheckerReadResult read_result;

    bool reading;
    bool browser_running;
} NfcCheckerApp;

/**
 * @brief Flipper Zero application entry point.
 *
 * Selects a baseline tag, then a clone, compares them and shows the
 * Emulation Success Score.
 */
int32_t nfc_checker_app(void* p);
