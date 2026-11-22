#include "saflok_protocol_support_base.h"

#include <nfc/protocols/nfc_protocol.h>

#include "iso14443_3a/iso14443_3a.h"
#include "mf_classic/mf_classic.h"

const SaflokProtocolSupportBase* saflok_protocol_support[NfcProtocolNum] = {
    [NfcProtocolIso14443_3a] = &saflok_protocol_support_iso14443_3a,
    [NfcProtocolMfClassic] = &saflok_protocol_support_mf_classic,
};
