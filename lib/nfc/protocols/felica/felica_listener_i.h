#include "felica_listener.h"

#include <nfc/protocols/nfc_generic_event.h>

#define FELICA_LISTENER_READ_BLOCK_COUNT_MAX  (4U)
#define FELICA_LISTENER_READ_BLOCK_COUNT_MIN  (1U)
#define FELICA_LISTENER_WRITE_BLOCK_COUNT_MAX (2U)
#define FELICA_LISTENER_WRITE_BLOCK_COUNT_MIN (1U)

#define FELICA_MC_SP_REG_ALL_RW_BYTES_0_1    (0U)
#define FELICA_MC_ALL_BYTE                   (2U)
#define FELICA_MC_SYS_OP                     (3U)
#define FELICA_MC_RF_PRM                     (4U)
#define FELICA_MC_CKCKV_W_MAC_A              (5U)
#define FELICA_MC_SP_REG_R_RESTR_BYTES_6_7   (6U)
#define FELICA_MC_SP_REG_W_RESTR_BYTES_8_9   (8U)
#define FELICA_MC_SP_REG_W_MAC_A_BYTES_10_11 (10U)
#define FELICA_MC_STATE_W_MAC_A              (12U)
#define FELICA_MC_RESERVED_13                (13U)
#define FELICA_MC_RESERVED_14                (14U)
#define FELICA_MC_RESERVED_15                (15U)

typedef enum {
    Felica_ListenerStateIdle,
    Felica_ListenerStateActivated,
} FelicaListenerState;

typedef struct FURI_PACKED {
    uint8_t code;
    uint16_t system_code;
    uint8_t request_code;
    uint8_t time_slot;
} FelicaListenerPollingHeader;

typedef struct {
    uint8_t length;
    uint8_t response_code;
    FelicaIDm idm;
    FelicaPMm pmm;
} FelicaListenerPollingResponseHeader;

typedef struct FURI_PACKED {
    FelicaListenerPollingResponseHeader header;
    uint16_t optional_request_data;
} FelicaListenerPollingResponse;

typedef struct {
    uint8_t length;
    union {
        FelicaCommandHeader header;
        FelicaListenerPollingHeader polling;
    };
} FelicaListenerGenericRequest;

typedef struct {
    FelicaListenerGenericRequest base;
    FelicaBlockListElement list[];
} FelicaListenerRequest;

typedef FelicaListenerRequest FelicaListenerReadRequest;
typedef FelicaListenerRequest FelicaListenerWriteRequest;

typedef struct {
    FelicaBlockData blocks[FELICA_LISTENER_WRITE_BLOCK_COUNT_MAX];
} FelicaListenerWriteBlockData;

typedef void (*FelicaCommandWriteBlockHandler)(
    FelicaListener* instance,
    const uint8_t block_number,
    const FelicaBlockData* data_block);

typedef void (*FelicaCommanReadBlockHandler)(
    FelicaListener* instance,
    const uint8_t block_number,
    const uint8_t resp_data_index,
    FelicaListenerReadCommandResponse* response);

struct FelicaListener {
    Nfc* nfc;
    FelicaData* data;
    FelicaListenerState state;
    FelicaAuthentication auth;
    FelicaBlockData mc_shadow;
    FelicaPMm pmm;

    uint8_t request_size_buf;
    uint8_t block_list_size;
    uint8_t requested_blocks[FELICA_LISTENER_READ_BLOCK_COUNT_MAX];
    uint8_t mac_calc_start;
    bool rc_written;
    uint8_t mode;

    uint8_t current_system_idx;

    BitBuffer* tx_buffer;
    BitBuffer* rx_buffer;

    NfcGenericEvent generic_event;
    NfcGenericCallback callback;
    void* context;
};

void felica_listener_reset(FelicaListener* instance);

void felica_wcnt_increment(FelicaData* data);

bool felica_listener_select_system_by_idm(FelicaListener* instance, const FelicaIDm* request_idm);

FelicaIDm felica_listener_get_current_idm(const FelicaListener* instance);

bool felica_listener_check_block_list_size(
    FelicaListener* instance,
    FelicaListenerGenericRequest* request);

const FelicaBlockListElement* felica_listener_block_list_item_get_first(
    FelicaListener* instance,
    const FelicaListenerRequest* request);

const FelicaBlockListElement* felica_listener_block_list_item_get_next(
    FelicaListener* instance,
    const FelicaBlockListElement* prev_item);

const FelicaListenerWriteBlockData* felica_listener_get_write_request_data_pointer(
    const FelicaListener* const instance,
    const FelicaListenerGenericRequest* const generic_request);

bool felica_listener_validate_write_request_and_set_sf(
    FelicaListener* instance,
    const FelicaListenerWriteRequest* const request,
    const FelicaListenerWriteBlockData* const data,
    FelicaListenerWriteCommandResponse* response);

bool felica_listener_validate_read_request_and_set_sf(
    FelicaListener* instance,
    const FelicaListenerReadRequest* const request,
    FelicaCommandResponseHeader* resp_header);

FelicaCommanReadBlockHandler felica_listener_get_read_block_handler(const uint8_t block_number);

FelicaCommandWriteBlockHandler felica_listener_get_write_block_handler(const uint8_t block_number);

FelicaError
    felica_listener_frame_exchange(const FelicaListener* instance, const BitBuffer* tx_buffer);
