#pragma once

#include <furi.h>
#include <furi_hal.h>

#define PCSG_VERSION_APP FAP_VERSION
#define PCSG_DEVELOPED \
    "@xMasterX & @Shmuma\nProtocol improvements by:\n@htotoo\nIcons by:\n@Svaarich"
#define PCSG_GITHUB "https://github.com/xMasterX/flipper-pager"

#define PCSG_KEY_FILE_VERSION 1
#define PCSG_KEY_FILE_TYPE    "Flipper POCSAG Pager Key File"

typedef enum {
    PCSGRxKeyStateIDLE,
    PCSGRxKeyStateBack,
    PCSGRxKeyStateStart,
    PCSGRxKeyStateAddKey,
} PCSGRxKeyState;

typedef enum {
    PCSGHopperStateOFF,
    PCSGHopperStateRunnig,
    PCSGHopperStatePause,
    PCSGHopperStateRSSITimeOut,
} PCSGHopperState;

typedef enum {
    PCSGLockOff,
    PCSGLockOn,
} PCSGLock;

typedef enum {
    POCSAGPagerViewVariableItemList,
    POCSAGPagerViewSubmenu,
    POCSAGPagerViewReceiver,
    POCSAGPagerViewReceiverInfo,
    POCSAGPagerViewWidget,
} POCSAGPagerView;

typedef enum {
    PCSGTxRxStateIDLE,
    PCSGTxRxStateRx,
    PCSGTxRxStateTx,
    PCSGTxRxStateSleep,
} PCSGTxRxState;
