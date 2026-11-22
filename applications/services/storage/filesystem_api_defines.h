#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FSAM_READ = (1 << 0),
    FSAM_WRITE = (1 << 1),
    FSAM_READ_WRITE = FSAM_READ | FSAM_WRITE,
} FS_AccessMode;

typedef enum {
    FSOM_OPEN_EXISTING = 1,
    FSOM_OPEN_ALWAYS = 2,
    FSOM_OPEN_APPEND = 4,
    FSOM_CREATE_NEW = 8,
    FSOM_CREATE_ALWAYS = 16,
} FS_OpenMode;

typedef enum {
    FSE_OK,
    FSE_NOT_READY,
    FSE_EXIST,
    FSE_NOT_EXIST,
    FSE_INVALID_PARAMETER,
    FSE_DENIED,
    FSE_INVALID_NAME,
    FSE_INTERNAL,
    FSE_NOT_IMPLEMENTED,
    FSE_ALREADY_OPEN,
} FS_Error;

typedef enum {
    FSF_DIRECTORY = (1 << 0),
} FS_Flags;

typedef struct File File;

typedef struct {
    uint8_t flags;
    uint64_t size;
    uint32_t date;
} FileInfo;

const char* filesystem_api_error_get_desc(FS_Error error_id);

bool file_info_is_dir(const FileInfo* file_info);

#ifdef __cplusplus
}
#endif
