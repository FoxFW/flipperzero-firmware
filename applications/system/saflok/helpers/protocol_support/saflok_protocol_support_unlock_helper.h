#include "../../saflok_app_i.h"

typedef enum {
    SaflokSceneReadMenuStateCardSearch,
    SaflokSceneReadMenuStateCardFound,
} SaflokSceneUnlockReadState;

void saflok_unlock_helper_setup_from_state(SaflokApp* app);
void saflok_unlock_helper_card_detected_handler(SaflokApp* app);
