#include <furi/furi.h>
#include "nfc_listener_defs.h"
#include "../nfc_group_manager.h"

#include <nfc/protocols/iso14443_3a/iso14443_3a_listener_defs.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_listener_defs.h>
#include <nfc/protocols/iso15693_3/iso15693_3_listener_defs.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight_listener_defs.h>
#include <nfc/protocols/mf_classic/mf_classic_listener_defs.h>
#include <nfc/protocols/mf_plus/mf_plus_listener_defs.h>
#include <nfc/protocols/slix/slix_listener_defs.h>
#include <nfc/protocols/type_4_tag/type_4_tag_listener_defs.h>
#include <nfc/protocols/texkom/texkom_listener_defs.h>

const NfcListenerBase* const nfc_listeners_api[NfcProtocolNum] = {
    [NfcProtocolIso14443_3a] = &nfc_listener_iso14443_3a,
    [NfcProtocolIso14443_3b] = NULL,
    [NfcProtocolIso14443_4a] = &nfc_listener_iso14443_4a,
    [NfcProtocolIso14443_4b] = NULL,
    [NfcProtocolIso15693_3] = &nfc_listener_iso15693_3,
    [NfcProtocolFelica] = NULL,
    [NfcProtocolMfUltralight] = &mf_ultralight_listener,
    [NfcProtocolMfClassic] = &mf_classic_listener,
    [NfcProtocolMfPlus] = NULL,
    [NfcProtocolMfDesfire] = NULL,
    [NfcProtocolSlix] = NULL,
    [NfcProtocolSt25tb] = NULL,
    [NfcProtocolNtag4xx] = NULL,
    [NfcProtocolType4Tag] = NULL,
    [NfcProtocolEmv] = NULL,
    [NfcProtocolTexkom] = &nfc_listener_texkom,
};

const NfcListenerBase* nfc_listener_get_base(NfcProtocol protocol) {
    furi_check(protocol < NfcProtocolNum);
    if(nfc_group_is_external(protocol)) {
        return nfc_group_get_listener(protocol);
    }
    return nfc_listeners_api[protocol];
}
