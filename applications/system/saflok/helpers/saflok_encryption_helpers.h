#ifndef _SAFLOK_ENCRYPTION_HELPERS_H_
#define _SAFLOK_ENCRYPTION_HELPERS_H_

#include "../saflok_app_i.h"
#include <furi.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <storage/storage.h>

bool decrypt_data(uint8_t* encrypted, uint8_t* decrypted);
bool encrypt_data(uint8_t* decrypted, uint8_t* encrypted);

bool saflok_decrypt(uint8_t* encrypted, uint8_t* decrypted_out, size_t out_len);
bool saflok_encrypt(uint8_t* decrypted, uint8_t* encrypted);

uint8_t saflok_get_sub_function(uint8_t* encrypted);
bool saflok_set_sub_function(SaflokApp* app, uint8_t sub_function);

uint8_t saflok_get_key_level(uint8_t* encrypted);
bool saflok_set_key_level(SaflokApp* app, uint8_t key_level);

bool saflok_get_led_warning(uint8_t* encrypted);
bool saflok_set_led_warning(SaflokApp* app, bool warning);

uint8_t saflok_get_key_id(uint8_t* encrypted);
bool saflok_set_key_id(SaflokApp* app, uint8_t key_id);

uint16_t saflok_get_key_record(uint8_t* encrypted);
bool saflok_set_key_record(SaflokApp* app, uint16_t key_record);

bool saflok_get_opening_key(uint8_t* encrypted);
bool saflok_set_opening_key(SaflokApp* app, bool opening_key);

uint8_t* saflok_get_pass_levels(uint8_t* encrypted);
bool saflok_set_pass_levels(SaflokApp* app, uint16_t pass_levels_mask);

uint16_t saflok_get_sequence_combo(uint8_t* encrypted);
bool saflok_set_sequence_combo(SaflokApp* app, uint16_t sequence_combo);

bool saflok_get_override_deadbolt(uint8_t* encrypted);
bool saflok_set_override_deadbolt(SaflokApp* app, bool override);

uint8_t saflok_get_restrictions(uint8_t* encrypted);
bool saflok_set_restrictions(SaflokApp* app, uint8_t restrictions);

SaflokDateTime saflok_get_date(uint8_t* encrypted, SaflokDateType dateType);
bool saflok_set_date(SaflokApp* app, SaflokDateTime date, SaflokDateType dateType);

SaflokDateTime saflok_get_creation_date(uint8_t* encrypted);
bool saflok_set_creation_date(SaflokApp* app, SaflokDateTime date);

SaflokDateTime saflok_get_expiration_date(uint8_t* encrypted);
bool saflok_set_expiration_date(SaflokApp* app, SaflokDateTime date);

uint16_t saflok_get_property_number(uint8_t* encrypted);
bool saflok_set_property_number(SaflokApp* app, uint16_t property_num);

#endif
