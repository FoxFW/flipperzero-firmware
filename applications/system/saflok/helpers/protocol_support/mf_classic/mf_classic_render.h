#pragma once

#include <nfc/protocols/mf_classic/mf_classic.h>

#include "../saflok_protocol_support_render_common.h"

void saflok_render_mf_classic_info(
    const MfClassicData* data,
    SaflokProtocolFormatType format_type,
    FuriString* str);

void saflok_render_mf_classic_dump(const MfClassicData* data, FuriString* str);
