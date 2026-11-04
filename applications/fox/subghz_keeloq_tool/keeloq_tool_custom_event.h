#pragma once
/* Custom events for the KeeLoq Tool .fap. Values kept clear of the per-scene event
   ranges (keys 0-101, key_edit 200-218, bf2 0-3, decrypt 0xD2-0xD4) to avoid any
   collision while a scene that also sends raw numeric events is active. */
typedef enum {
    KeeloqToolCustomEventViewBack = 0xE0,
} KeeloqToolCustomEvent;
