#pragma once

/* App-side installer for the on-demand Sub-GHz decode-plugin loader. Call once
 * at subghz app startup; it registers an implementation of the libsubghz hook
 * (lib/subghz/protocols/decode_plugin.h) that loads decode_<name>.fal from the
 * SD plugins dir and dispatches get_string to it. FoxFW2.0 2026-10-21. */

void subghz_decode_plugin_loader_install(void);

void subghz_decode_plugin_loader_flush(void);
