#include "fox_ch_update.h"

#include <furi.h>
#include <storage/storage.h>
#include <loader/loader.h>
#include <toolbox/tar/tar_archive.h>

#include <string.h>
#include <stdio.h>

#define TAG "FoxUpdate"

// Recursively find the first *.fuf update manifest under dir.
static bool fox_find_fuf(Storage* storage, const char* dir, char* out, size_t outsz) {
    File* d = storage_file_alloc(storage);
    FileInfo info;
    char name[128];
    bool found = false;
    if(storage_dir_open(d, dir)) {
        while(!found && storage_dir_read(d, &info, name, sizeof(name))) {
            char child[256];
            snprintf(child, sizeof(child), "%s/%s", dir, name);
            if(info.flags & FSF_DIRECTORY) {
                found = fox_find_fuf(storage, child, out, outsz);
                continue;
            }
            size_t len = strlen(name);
            if(len >= 4 && strcmp(name + len - 4, ".fuf") == 0) {
                snprintf(out, outsz, "%s", child);
                found = true;
            }
        }
    }
    storage_dir_close(d);
    storage_file_free(d);
    return found;
}

// Extract an on-SD FoxFW update .tar into /ext/update/foxfw_<name>/, find its .fuf manifest, and
// hand off to the built-in updater_app (which validates, flashes and reboots) — the same mechanism
// as fox_update_downloader's installer. Returns 0 on handoff, negative on error.
int32_t fox_update_channel(const char* tar_path) {
    if(!tar_path || tar_path[0] == '\0') return -1;

    Storage* storage = furi_record_open(RECORD_STORAGE);

    FileInfo st;
    if(storage_common_stat(storage, tar_path, &st) != FSE_OK || st.size == 0) {
        furi_record_close(RECORD_STORAGE);
        return -2; // download missing / empty
    }

    // derive a safe package dir from the tar's basename
    const char* base = strrchr(tar_path, '/');
    base = base ? base + 1 : tar_path;
    char pkg[96];
    snprintf(pkg, sizeof(pkg), "foxfw_%s", base);
    for(char* p = pkg; *p; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if(!ok) *p = '_';
    }
    char pkg_dir[256];
    snprintf(pkg_dir, sizeof(pkg_dir), "/ext/update/%s", pkg);
    storage_simply_mkdir(storage, "/ext/update");
    storage_simply_mkdir(storage, pkg_dir);

    TarArchive* ar = tar_archive_alloc(storage);
    bool ok = tar_archive_open(ar, tar_path, TarOpenModeRead) &&
              tar_archive_unpack_to(ar, pkg_dir, NULL);
    tar_archive_free(ar);
    if(!ok) {
        furi_record_close(RECORD_STORAGE);
        return -3; // open/extract failed (corrupt download)
    }

    char manifest[256] = {0};
    fox_find_fuf(storage, pkg_dir, manifest, sizeof(manifest));
    furi_record_close(RECORD_STORAGE);
    if(manifest[0] == '\0') return -4; // no .fuf in package

    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(loader, "updater_app", manifest, LoaderDeferredLaunchFlagGui);
    furi_record_close(RECORD_LOADER);
    return 0; // updater_app will validate, flash and reboot
}
