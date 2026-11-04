#include <furi/furi.h>
#include "nfc_device_base_i.h"
#include "nfc_protocol.h"
#include "../nfc_group_manager.h"

#include <nfc/protocols/iso14443_3a/iso14443_3a_device_defs.h>
#include <nfc/protocols/iso14443_3b/iso14443_3b_device_defs.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_device_defs.h>
#include <nfc/protocols/iso14443_4b/iso14443_4b_device_defs.h>
#include <nfc/protocols/iso15693_3/iso15693_3_device_defs.h>
#include <nfc/protocols/felica/felica.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_plus/mf_plus.h>
#include <nfc/protocols/mf_desfire/mf_desfire.h>
#include <nfc/protocols/slix/slix_device_defs.h>
#include <nfc/protocols/st25tb/st25tb.h>
#include <nfc/protocols/ntag4xx/ntag4xx.h>
#include <nfc/protocols/type_4_tag/type_4_tag.h>
#include <nfc/protocols/emv/emv.h>
#include <nfc/protocols/texkom/texkom.h>

const NfcDeviceBase* const nfc_devices[NfcProtocolNum] = {
    [NfcProtocolIso14443_3a] = &nfc_device_iso14443_3a,
    [NfcProtocolIso14443_3b] = &nfc_device_iso14443_3b,
    [NfcProtocolIso14443_4a] = &nfc_device_iso14443_4a,
    [NfcProtocolIso14443_4b] = &nfc_device_iso14443_4b,
    [NfcProtocolIso15693_3] = &nfc_device_iso15693_3,
    [NfcProtocolFelica] = NULL,
    [NfcProtocolMfUltralight] = &nfc_device_mf_ultralight,
    [NfcProtocolMfClassic] = &nfc_device_mf_classic,
    [NfcProtocolMfPlus] = NULL,
    [NfcProtocolMfDesfire] = NULL,
    [NfcProtocolSlix] = NULL,
    [NfcProtocolSt25tb] = NULL,
    [NfcProtocolNtag4xx] = NULL,
    [NfcProtocolType4Tag] = NULL,
    [NfcProtocolEmv] = NULL,
    [NfcProtocolTexkom] = &nfc_device_texkom,

};

static const char* const nfc_protocol_names[NfcProtocolNum] = {
    [NfcProtocolIso14443_3a] = "ISO14443-3A",
    [NfcProtocolIso14443_3b] = "ISO14443-3B",
    [NfcProtocolIso14443_4a] = "ISO14443-4A",
    [NfcProtocolIso14443_4b] = "ISO14443-4B",
    [NfcProtocolIso15693_3] = "ISO15693-3",
    [NfcProtocolFelica] = "FeliCa",
    [NfcProtocolMfUltralight] = "NTAG/Ultralight",
    [NfcProtocolMfClassic] = "Mifare Classic",
    [NfcProtocolMfPlus] = "Mifare Plus",
    [NfcProtocolMfDesfire] = "Mifare DESFire",
    [NfcProtocolSlix] = "SLIX",
    [NfcProtocolSt25tb] = "ST25TB",
    [NfcProtocolNtag4xx] = "NTAG4xx",
    [NfcProtocolType4Tag] = "Type 4 Tag",
    [NfcProtocolEmv] = "EMV",
    [NfcProtocolTexkom] = "Texkom",
};

const char* nfc_protocol_get_name(NfcProtocol protocol) {
    furi_check(protocol < NfcProtocolNum);
    return nfc_protocol_names[protocol];
}

const NfcDeviceBase* nfc_device_get_base(NfcProtocol protocol) {
    furi_check(protocol < NfcProtocolNum);
    if(nfc_group_is_external(protocol)) {
        return nfc_group_get_device(protocol);
    }
    return nfc_devices[protocol];
}
