#include "saflok_protocol_support.h"

#include "../../saflok_app_i.h"

#include "saflok_protocol_support_defs.h"
#include "saflok_protocol_support_gui_common.h"

typedef void (*SaflokProtocolSupportCommonOnEnter)(SaflokApp* app);

typedef bool (*SaflokProtocolSupportCommonOnEvent)(SaflokApp* app, SceneManagerEvent event);

typedef void (*SaflokProtocolSupportCommonOnExit)(SaflokApp* app);

typedef struct {
    SaflokProtocolSupportCommonOnEnter on_enter;
    SaflokProtocolSupportCommonOnEvent on_event;
    SaflokProtocolSupportCommonOnExit on_exit;
} SaflokProtocolSupportCommonSceneBase;

static const SaflokProtocolSupportCommonSceneBase saflok_protocol_support_scenes[];

void saflok_protocol_support_on_enter(SaflokProtocolSupportScene scene, void* context) {
    furi_assert(scene < SaflokProtocolSupportSceneCount);
    furi_assert(context);

    SaflokApp* app = context;
    saflok_protocol_support_scenes[scene].on_enter(app);
}

bool saflok_protocol_support_on_event(SaflokProtocolSupportScene scene, void* context, SceneManagerEvent event) {
    furi_assert(scene < SaflokProtocolSupportSceneCount);
    furi_assert(context);

    SaflokApp* app = context;
    return saflok_protocol_support_scenes[scene].on_event(app, event);
}

void saflok_protocol_support_on_exit(SaflokProtocolSupportScene scene, void* context) {
    furi_assert(scene < SaflokProtocolSupportSceneCount);
    furi_assert(context);

    SaflokApp* app = context;
    saflok_protocol_support_scenes[scene].on_exit(app);
}

bool saflok_protocol_support_has_feature(NfcProtocol protocol, SaflokProtocolFeature feature) {
    return saflok_protocol_support[protocol]->features & feature;
}
static const SaflokProtocolSupportCommonSceneBase
    saflok_protocol_support_scenes[SaflokProtocolSupportSceneCount] = {
};
