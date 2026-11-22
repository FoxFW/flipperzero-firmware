#pragma once

typedef enum {
    SaflokCustomEventReserved = 100,

    SaflokCustomEventDictAttackComplete,
    SaflokCustomEventDictAttackSkip,
    SaflokCustomEventDictAttackDataUpdate,

    SaflokCustomEventCardDetected,
    SaflokCustomEventCardLost,

    SaflokCustomEventViewExit,
    SaflokCustomEventWorkerExit,
    SaflokCustomEventWorkerUpdate,
    SaflokCustomEventWrongCard,
    SaflokCustomEventTimerExpired,
    SaflokCustomEventByteInputDone,
    SaflokCustomEventTextInputDone,
    SaflokCustomEventDictAttackDone,

    SaflokCustomEventRpcLoadFile,
    SaflokCustomEventRpcExit,
    SaflokCustomEventRpcSessionClose,

    SaflokCustomEventPollerSuccess,
    SaflokCustomEventPollerIncomplete,
    SaflokCustomEventPollerFailure,

    SaflokCustomEventListenerUpdate,

    SaflokCustomEventEmulationTimeExpired,
    SaflokCustomEventReturnFromPresetKey,
    SaflokCustomEventReturnFromWriteToTag,
    SaflokCustomEventReturnFromDeleteTag,
    SaflokCustomEventReturnFromViewInfo,
    SaflokCustomEventReturnFromDisplaySettings,
    SaflokCustomEventReturnFromToggleNfcMode,
    SaflokCustomEventReturnFromEmulate,
} SaflokCustomEvent;
