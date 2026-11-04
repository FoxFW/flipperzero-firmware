#pragma once

// Best-effort hang watchdog for the companion FAP.
//
// The FAP is one-shot: it runs a single operation and exits so the Flipper's loader is free again.
// If an operation ever *blocks* (a radio/UART/storage call that never returns), the main thread never
// reaches its exit, the FAP stays resident, and the Flipper appears frozen. This watchdog guards
// against that: wrap an operation with start()/stop(), and kick() from any long-running loop so the
// watchdog knows work is still progressing. If no kick arrives within the window (the op is hung), it
// writes a HANG LOG to the SD (apps_data/fox_companion/hang_log.txt) and resets the Flipper, which
// frees the stuck loader. A normally-returning op calls stop() and never trips it.
//
//   fox_watchdog_start("RX 433920000 2 4000 ...");
//   ... op runs; long loops call fox_watchdog_kick(); ...
//   fox_watchdog_stop();
//
// `what` must stay valid until stop() (pass the launch args / command string).

void fox_watchdog_start(const char* what);
void fox_watchdog_kick(void);
void fox_watchdog_stop(void);
