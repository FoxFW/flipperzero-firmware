#pragma once
#include <toolbox/protocols/protocol.h>
#include "../tools/t5577.h"
#include "../tools/em4305.h"
#include "../tools/hitagmicro.h"

typedef enum {
    LFRFIDFeatureASK = 1 << 0,
    LFRFIDFeaturePSK = 1 << 1,
} LFRFIDFeature;

typedef enum {
    LFRFIDProtocolEM4100,
    LFRFIDProtocolEM4100_32,
    LFRFIDProtocolEM4100_16,
    LFRFIDProtocolElectra,
    LFRFIDProtocolH10301,
    LFRFIDProtocolIdteck,
    LFRFIDProtocolIndala26,
    LFRFIDProtocolIOProxXSF,
    LFRFIDProtocolAwid,
    LFRFIDProtocolFDXA,
    LFRFIDProtocolFDXB,
    LFRFIDProtocolHidGeneric,
    LFRFIDProtocolHidExGeneric,
    LFRFIDProtocolPyramid,
    LFRFIDProtocolViking,
    LFRFIDProtocolJablotron,
    LFRFIDProtocolParadox,
    LFRFIDProtocolPACStanley,
    LFRFIDProtocolKeri,
    LFRFIDProtocolGallagher,
    LFRFIDProtocolNexwatch,
    LFRFIDProtocolSecurakey,
    LFRFIDProtocolGProxII,
    LFRFIDProtocolNoralsy,
    LFRFIDProtocolIndala224,

    LFRFIDProtocolMax,
} LFRFIDProtocol;

extern const ProtocolBase* const lfrfid_protocols[];

typedef enum {
    LFRFIDWriteTypeT5577,
    LFRFIDWriteTypeEM4305,
    LFRFIDWriteTypeHitagMicro,

    LFRFIDWriteTypeMax,
} LFRFIDWriteType;

typedef struct {
    LFRFIDWriteType write_type;
    union {
        LFRFIDT5577 t5577;
        LFRFIDEM4305 em4305;
        LFRFIDHitagMicro hitagmicro;
    };
} LFRFIDWriteRequest;
