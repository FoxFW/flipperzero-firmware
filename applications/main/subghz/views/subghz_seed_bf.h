#pragma once

#include <gui/view.h>
#include "../helpers/subghz_custom_event.h"

typedef struct SubGhzViewSeedBf SubGhzViewSeedBf;

typedef void (*SubGhzViewSeedBfCallback)(SubGhzCustomEvent event, void* context);

SubGhzViewSeedBf* subghz_view_seed_bf_alloc(void);
void subghz_view_seed_bf_free(SubGhzViewSeedBf* instance);
View* subghz_view_seed_bf_get_view(SubGhzViewSeedBf* instance);

void subghz_view_seed_bf_set_callback(
    SubGhzViewSeedBf* instance,
    SubGhzViewSeedBfCallback callback,
    void* context);

/* Drive the running progress bar. tested/total scale the bar; elapsed_sec is shown under it. */
void subghz_view_seed_bf_update(
    SubGhzViewSeedBf* instance,
    uint32_t tested,
    uint32_t total,
    uint32_t elapsed_sec);

/* Switch to the final result screen: success shows the recovered seed, failure the explanation. */
void subghz_view_seed_bf_set_result(SubGhzViewSeedBf* instance, bool success, const char* result);

void subghz_view_seed_bf_reset(SubGhzViewSeedBf* instance);
