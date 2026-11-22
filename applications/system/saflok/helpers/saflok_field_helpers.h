#ifndef _SAFLOK_FIELD_HELPERS_H_
#define _SAFLOK_FIELD_HELPERS_H_

#include "../saflok_app_i.h"
#include <furi.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <storage/storage.h>

extern const SaflokDay weekdays[11];
extern const SaflokSubFunction sub_functions[7];
extern const char* field_labels[14];
extern const SaflokKeyLevel key_levels[16];

void saflok_normalize_date(uint8_t *day, uint8_t *month, uint16_t *year);
bool saflok_validate_field_value(uint32_t value, uint32_t min, uint32_t max);

uint8_t saflok_packed_sub_function(SaflokApp* app);
bool saflok_pack_sub_function(uint8_t sub_function, SaflokApp* app);
bool saflok_pack_current_sub_function(SaflokApp* app);
bool saflok_unpack_sub_function(uint8_t* decoded_data, SaflokApp* app);

uint8_t saflok_packed_key_level(SaflokApp* app);
bool saflok_pack_key_level(uint8_t key_level, SaflokApp* app);
bool saflok_pack_current_key_level(SaflokApp* app);
bool saflok_unpack_key_level(uint8_t* decoded_data, SaflokApp* app);

bool saflok_packed_led_warning(SaflokApp* app);
bool saflok_pack_led_warning(bool warning, SaflokApp* app);
bool saflok_pack_current_led_warning(SaflokApp* app);
bool saflok_unpack_led_warning(uint8_t* decoded_data, SaflokApp* app);

uint8_t saflok_packed_key_id(SaflokApp* app);
bool saflok_pack_key_id(uint8_t key_id, SaflokApp* app);
bool saflok_pack_current_key_id(SaflokApp* app);
bool saflok_unpack_key_id(uint8_t* decoded_data, SaflokApp* app);

uint16_t saflok_packed_key_record(SaflokApp* app);
bool saflok_pack_key_record(uint16_t key_record, SaflokApp* app);
bool saflok_pack_current_key_record(SaflokApp* app);
bool saflok_unpack_key_record(uint8_t* decoded_data, SaflokApp* app);

bool saflok_packed_opening_key(SaflokApp* app);
bool saflok_pack_opening_key(bool opening_key, SaflokApp* app);
bool saflok_pack_current_opening_key(SaflokApp* app);
bool saflok_unpack_opening_key(uint8_t* decoded_data, SaflokApp* app);

uint8_t* saflok_packed_pass_levels(SaflokApp* app);
bool saflok_pack_pass_levels(uint8_t* pass_levels, SaflokApp* app);
bool saflok_pack_current_pass_levels(SaflokApp* app);
bool saflok_unpack_pass_levels(uint8_t* decoded_data, SaflokApp* app);

uint16_t saflok_packed_sequence_combo(SaflokApp* app);
bool saflok_pack_sequence_combo(uint16_t sequence_combo, SaflokApp* app);
bool saflok_pack_current_sequence_combo(SaflokApp* app);
bool saflok_unpack_sequence_combo(uint8_t* decoded_data, SaflokApp* app);

bool saflok_packed_override_deadbolt(SaflokApp* app);
bool saflok_pack_override_deadbolt(bool override, SaflokApp* app);
bool saflok_pack_current_override_deadbolt(SaflokApp* app);
bool saflok_unpack_override_deadbolt(uint8_t* decoded_data, SaflokApp* app);

Restrictions* saflok_packed_restrictions(SaflokApp* app);
bool saflok_pack_restrictions(Restrictions* restrictions, SaflokApp* app);
bool saflok_pack_current_restrictions(SaflokApp* app);
bool saflok_unpack_restrictions(uint8_t* decoded_data, SaflokApp* app);

SaflokDateTime saflok_packed_creation_date(SaflokApp* app);
bool saflok_pack_creation_date(SaflokDateTime date, SaflokApp* app);
bool saflok_pack_current_creation_date(SaflokApp* app);
bool saflok_unpack_creation_date(uint8_t* decoded_data, SaflokApp* app);

SaflokDateTime saflok_packed_expiration_date(SaflokApp* app);
bool saflok_pack_expiration_date(SaflokDateTime date, SaflokApp* app);
bool saflok_pack_current_expiration_date(SaflokApp* app);
bool saflok_unpack_expiration_date(uint8_t* decoded_data, SaflokApp* app);

uint16_t saflok_packed_property_number(SaflokApp* app);
bool saflok_pack_property_number(uint16_t property_num, SaflokApp* app);
bool saflok_pack_current_property_number(SaflokApp* app);
bool saflok_unpack_property_number(uint8_t* decoded_data, SaflokApp* app);

uint8_t saflok_packed_checksum(SaflokApp* app);
bool saflok_pack_checksum(uint8_t checksum, SaflokApp* app);
bool saflok_pack_current_checksum(SaflokApp* app);
bool saflok_unpack_checksum(uint8_t* decoded_data, SaflokApp* app);

bool repack_saflok_data(uint8_t* output, SaflokData* data);

bool saflok_create_battery_disconnect_key(SaflokApp* app);
bool saflok_create_clock_date_key(SaflokApp* app);
bool saflok_create_clock_run_key(SaflokApp* app);
bool saflok_create_clock_time_key(SaflokApp* app);
bool saflok_create_deadbolt_switch_status_key(SaflokApp* app);
bool saflok_create_disable_latch_key(SaflokApp* app);
bool saflok_create_e2_changes_key(SaflokApp* app);
bool saflok_create_emergency_key(SaflokApp* app);
bool saflok_create_enable_latch_key(SaflokApp* app);
bool saflok_create_eprom_version_key(SaflokApp* app);
bool saflok_create_erase_lock_memory_key(SaflokApp* app);
bool saflok_create_interrogation_key(SaflokApp* app);
bool saflok_create_knob_switch_status_key(SaflokApp* app);
bool saflok_create_latch_switch_status_key(SaflokApp* app);
bool saflok_create_led_diagnostics_key(SaflokApp* app);
bool saflok_create_low_battery_status_key(SaflokApp* app);
bool saflok_create_primary_program_key(SaflokApp* app);
bool saflok_create_resequence_key(SaflokApp* app);
bool saflok_create_secondary_program_key(SaflokApp* app);

void saflok_update_mf_classic_from_packed_data(SaflokApp* app);

#endif
