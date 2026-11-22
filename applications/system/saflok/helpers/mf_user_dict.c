#include "mf_user_dict.h"

#include <toolbox/keys_dict.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <furi/furi.h>

#define SAFLOK_APP_FOLDER                    EXT_PATH("saflok")
#define SAFLOK_APP_MF_CLASSIC_DICT_USER_PATH (SAFLOK_APP_FOLDER "/assets/mf_classic_dict_user.nfc")

struct MfUserDict {
    size_t keys_num;
    MfClassicKey* keys_arr;
};

MfUserDict* mf_user_dict_alloc(size_t max_keys_to_load) {
    MfUserDict* userDict = malloc(sizeof(MfUserDict));

    KeysDict* dict = keys_dict_alloc(
        SAFLOK_APP_MF_CLASSIC_DICT_USER_PATH, KeysDictModeOpenAlways, sizeof(MfClassicKey));

    size_t dict_keys_num = keys_dict_get_total_keys(dict);
    userDict->keys_num = MIN(max_keys_to_load, dict_keys_num);

    if(userDict->keys_num > 0) {
        userDict->keys_arr = malloc(userDict->keys_num * sizeof(MfClassicKey));
        for(size_t i = 0; i < userDict->keys_num; i++) {
            bool key_loaded =
                keys_dict_get_next_key(dict, userDict->keys_arr[i].data, sizeof(MfClassicKey));
            furi_assert(key_loaded);
        }
    }
    keys_dict_free(dict);

    return userDict;
}

void mf_user_dict_free(MfUserDict* userDict) {
    furi_assert(userDict);

    if(userDict->keys_num > 0) {
        free(userDict->keys_arr);
    }
    free(userDict);
}

size_t mf_user_dict_get_keys_cnt(MfUserDict* userDict) {
    furi_assert(userDict);

    return userDict->keys_num;
}

void mf_user_dict_get_key_str(MfUserDict* userDict, uint32_t index, FuriString* str) {
    furi_assert(userDict);
    furi_assert(str);
    furi_assert(index < userDict->keys_num);
    furi_assert(userDict->keys_arr);

    furi_string_reset(str);
    for(size_t i = 0; i < sizeof(MfClassicKey); i++) {
        furi_string_cat_printf(str, "%02X", userDict->keys_arr[index].data[i]);
    }
}

bool mf_user_dict_delete_key(MfUserDict* userDict, uint32_t index) {
    furi_assert(userDict);
    furi_assert(index < userDict->keys_num);
    furi_assert(userDict->keys_arr);

    KeysDict* dict = keys_dict_alloc(
        SAFLOK_APP_MF_CLASSIC_DICT_USER_PATH, KeysDictModeOpenAlways, sizeof(MfClassicKey));

    bool key_delete_success =
        keys_dict_delete_key(dict, userDict->keys_arr[index].data, sizeof(MfClassicKey));
    keys_dict_free(dict);

    if(key_delete_success) {
        userDict->keys_num--;
    }

    return key_delete_success;
}
