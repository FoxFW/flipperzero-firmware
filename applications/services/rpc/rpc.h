#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RPC_BUFFER_SIZE (1024)

#define RECORD_RPC "rpc"

typedef struct Rpc Rpc;

typedef struct RpcSession RpcSession;

typedef void (*RpcSendBytesCallback)(void* context, uint8_t* bytes, size_t bytes_len);

typedef void (*RpcBufferIsEmptyCallback)(void* context);

typedef void (*RpcSessionClosedCallback)(void* context);

typedef void (*RpcSessionTerminatedCallback)(void* context);

typedef enum {
    RpcOwnerUnknown = 0,
    RpcOwnerBle,
    RpcOwnerUsb,
    RpcOwnerUart,
    RpcOwnerCount,
} RpcOwner;

RpcOwner rpc_session_get_owner(RpcSession* session);

void rpc_wait_for_subsystems_ready(void);

RpcSession* rpc_session_open(Rpc* rpc, RpcOwner owner);

void rpc_session_close(RpcSession* session);

void rpc_session_set_context(RpcSession* session, void* context);

void rpc_session_set_send_bytes_callback(RpcSession* session, RpcSendBytesCallback callback);

void rpc_session_set_buffer_is_empty_callback(
    RpcSession* session,
    RpcBufferIsEmptyCallback callback);

void rpc_session_set_close_callback(RpcSession* session, RpcSessionClosedCallback callback);

void rpc_session_set_terminated_callback(
    RpcSession* session,
    RpcSessionTerminatedCallback callback);

size_t rpc_session_feed(RpcSession* session, const uint8_t* buffer, size_t size, uint32_t timeout);

size_t rpc_session_get_available_size(RpcSession* session);

bool fox_rpc_usb_session_is_active(void);
uint32_t fox_rpc_usb_session_active_since(void);

#ifdef __cplusplus
}
#endif
