#pragma once

#include <furi.h>
#include <furi_hal.h>

typedef enum {
    SubGhzNotificationStateStarting,
    SubGhzNotificationStateIDLE,
    SubGhzNotificationStateTx,
    SubGhzNotificationStateRx,
    SubGhzNotificationStateRxDone,
    SubGhzNotificationStateTxWait,
} SubGhzNotificationState;

typedef enum {
    SubGhzTxRxStateIDLE,
    SubGhzTxRxStateRx,
    SubGhzTxRxStateTx,
    SubGhzTxRxStateSleep,
} SubGhzTxRxState;

typedef enum {
    SubGhzHopperStateOFF,
    SubGhzHopperStateRunning,
    SubGhzHopperStatePause,
    SubGhzHopperStateRSSITimeOut,
} SubGhzHopperState;

typedef enum {
    SubGhzPresetHopperStateOFF,
    SubGhzPresetHopperStateRunning,
    SubGhzPresetHopperStatePause,
    SubGhzPresetHopperStateRSSITimeOut,
} SubGhzPresetHopperState;

typedef enum {
    SubGhzSpeakerStateDisable,
    SubGhzSpeakerStateShutdown,
    SubGhzSpeakerStateEnable,
} SubGhzSpeakerState;

typedef enum {
    SubGhzRadioDeviceTypeAuto,
    SubGhzRadioDeviceTypeInternal,
    SubGhzRadioDeviceTypeExternalCC1101,
} SubGhzRadioDeviceType;

typedef enum {
    SubGhzRxKeyStateIDLE,
    SubGhzRxKeyStateNoSave,
    SubGhzRxKeyStateNeedSave,
    SubGhzRxKeyStateBack,
    SubGhzRxKeyStateStart,
    SubGhzRxKeyStateAddKey,
    SubGhzRxKeyStateExit,
    SubGhzRxKeyStateRAWLoad,
    SubGhzRxKeyStateRAWMore,
    SubGhzRxKeyStateRAWSave,
} SubGhzRxKeyState;

typedef enum {
    SubGhzLoadKeyStateUnknown,
    SubGhzLoadKeyStateOK,
    SubGhzLoadKeyStateParseErr,
    SubGhzLoadKeyStateOnlyRx,
    SubGhzLoadKeyStateUnsuportedFreq,
    SubGhzLoadKeyStateProtocolDescriptionErr,
} SubGhzLoadKeyState;

typedef enum {
    SubGhzLockOff,
    SubGhzLockOn,
} SubGhzLock;

typedef enum {
    SubGhzViewIdMenu,
    SubGhzViewIdReceiver,
    SubGhzViewIdPopup,
    SubGhzViewIdTextInput,
    SubGhzViewIdByteInput,
    SubGhzViewIdWidget,
    SubGhzViewIdTransmitter,
    SubGhzViewIdVariableItemList,
    SubGhzViewIdReadRAW,
    SubGhzViewIdPsaDecrypt,
    SubGhzViewIdSeedBf,
    SubGhzViewIdFiatV1Recover,
    SubGhzViewIdSignalVisualizer,
    SubGhzViewIdStartGrid,
    SubGhzViewIdNumberInput,
    SubGhzViewIdReceiverInfoBar,

} SubGhzViewId;

typedef enum {
    SubGhzLoadTypeFileNoLoad,
    SubGhzLoadTypeFileKey,
    SubGhzLoadTypeFileRaw,
} SubGhzLoadTypeFile;

typedef enum {
    SubGhzViewReceiverModeLive,
    SubGhzViewReceiverModeFile,
} SubGhzViewReceiverMode;

typedef enum {
    SubGhzDecodeRawStateStart,
    SubGhzDecodeRawStateLoading,
    SubGhzDecodeRawStateLoaded,
} SubGhzDecodeRawState;
