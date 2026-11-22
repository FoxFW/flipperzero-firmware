#pragma once

#include <gui/scene_manager.h>
#include <lib/nfc/protocols/nfc_protocol.h>

#include "saflok_protocol_support_common.h"

void saflok_protocol_support_on_enter(SaflokProtocolSupportScene scene, void* context);

bool saflok_protocol_support_on_event(
    SaflokProtocolSupportScene scene,
    void* context,
    SceneManagerEvent event);

void saflok_protocol_support_on_exit(SaflokProtocolSupportScene scene, void* context);

bool saflok_protocol_support_has_feature(NfcProtocol protocol, SaflokProtocolFeature feature);
