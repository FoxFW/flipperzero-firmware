#pragma once
#include <gui/view.h>

#define SGRID_IDX_READ          0
#define SGRID_IDX_SAVED         1
#define SGRID_IDX_READRAW       2
#define SGRID_IDX_ADDMAN        3
#define SGRID_IDX_RADIOSETTINGS 4
#define SGRID_IDX_FREQANA       5
#define SGRID_IDX_MODANA        6
#define SGRID_IDX_JAMMER        7
#define SGRID_IDX_SUBRATT       8
#define SGRID_IDX_TPMS          9
#define SGRID_IDX_PROTOCOLS     10
#define SGRID_IDX_MODLIST       11
#define SGRID_IDX_KEELOQ        12
#define SGRID_IDX_KEELOQBF      13
#define SGRID_IDX_READDUAL      14
#define SGRID_BTN_COUNT         15

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzStartGrid SubGhzStartGrid;

typedef void (*SubGhzStartGridCallback)(void* context, uint32_t event);

SubGhzStartGrid* subghz_start_grid_alloc(void);
void subghz_start_grid_free(SubGhzStartGrid* instance);
View* subghz_start_grid_get_view(SubGhzStartGrid* instance);
void subghz_start_grid_set_callback(SubGhzStartGrid* instance,
                                     SubGhzStartGridCallback callback,
                                     void* context);
void subghz_start_grid_set_visible(SubGhzStartGrid* instance,
                                    uint8_t btn_idx,
                                    bool visible);
void subghz_start_grid_set_selected(SubGhzStartGrid* instance,
                                     uint8_t btn_idx);

#ifdef __cplusplus
}
#endif
