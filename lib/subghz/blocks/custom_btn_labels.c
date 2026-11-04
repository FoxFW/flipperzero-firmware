#include "custom_btn_i.h"
#include <string.h>

typedef struct {
    const char* proto;
    const char* up;
    const char* down;
    const char* left;
    const char* right;
} SubGhzBtnLabelEntry;

static const SubGhzBtnLabelEntry label_table[] = {

    { "KIA/HYU V0",   "Lock",  "Unlock", "Trunk",  "Panic"  },
    { "KIA/HYU V1",   "Lock",  "Unlock", "Trunk",  "Panic"  },
    { "KIA/HYU V2",   "Lock",  "Unlock", "Trunk",  "Panic"  },
    { "KIA/HYU V3/V4","Lock",  "Unlock", "Trunk",  "Panic"  },
    { "KIA/HYU V5",   "Lock",  "Unlock", "Trunk",  "Horn"   },
    { "KIA/HYU V6",   "Lock",  "Unlock", "Trunk",  "Panic"  },
    { "Kia V7",       "Lock",  "Unlock", "Trunk",  "Panic"  },

    { "VAG GROUP",    "Lock",  "Unlock", "Trunk",  "Panic"  },
    { "Porsche AG",   "Lock",  "Unlock", "Trunk",  "Panic"  },

    { "PSA GROUP",    "Lock",  "Unlock", "Trunk",  "Trunk"  },
    { "PSA OLD",      "Lock",  "Unlock", "Trunk",  "Trunk"  },

    { "FORD V0",      "Lock",  "Unlock", "Trunk",  NULL     },

    { "MARELLI",      "Lock",  "Unlock", "Trunk",  NULL     },
    { "FIAT SPA",     "Lock",  "Unlock", "Trunk",  "Panic"  },

    { "Chrysler",     "Lock",  "Unlock", NULL,     NULL     },

    { "SUBARU",       "Lock",  "Unlock", "Trunk",  "Panic"  },
    { "SUZUKI",       "Panic", "Trunk",  "Lock",   "Unlock" },
    { "Mazda V0",     "Lock",  "Unlock", "Trunk",  "Panic"  },
    { "MazdaSiemens", "Lock",  "Unlock", "Trunk",  "Panic"  },
    { "Mitsubishi V0","Lock",  "Unlock", NULL,     NULL     },
    { "Mitsubishi V0-a","Lock","Unlock", "Trunk",  "Panic"  },

    { "Scher-Khan",   "Lock",  "Unlock", "Trunk",  "Start"  },
    { "Star Line",    "Lock",  "Unlock", "Trunk",  "Start"  },
    { "Sheriff CFM",  "Lock",  "Unlock", "Trunk",  "Panic"  },

    { "Hitag-2",      "Lock",  "Unlock", NULL,     NULL     },

    { NULL, NULL, NULL, NULL, NULL }
};

const char* subghz_custom_btn_get_label_for_proto(
    const char* proto_name, uint8_t btn_dir) {

    if(!proto_name || btn_dir == 0 || btn_dir > 4) return NULL;

    for(const SubGhzBtnLabelEntry* e = label_table; e->proto; e++) {
        if(strcmp(e->proto, proto_name) == 0) {
            switch(btn_dir) {
            case 1: return e->up;
            case 2: return e->down;
            case 3: return e->left;
            case 4: return e->right;
            }
        }
    }
    return NULL;
}
