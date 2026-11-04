#pragma once

// Reusable "operation in progress" fullscreen page for the Fox Android Companion FAP.
//
// Shows the right-facing fox logo on the left, a (newline-split) title beside it, and a progress bar
// below that fills in quarters. Intended for operations whose duration is known up front (e.g. a fixed
// SubGHz RX window), so the user sees a determinate bar and knows the Flipper is busy. Call
// fox_progress_set() with 0.0..1.0 as the operation proceeds; the bar is drawn quantised to quarters.
//
//   FoxProgress* p = fox_progress_begin("SubGHz\nOperation\nin Progress");
//   ... loop: fox_progress_set(p, elapsed / total); ...
//   fox_progress_end(p);   // the page disappears
//
// Lightweight: one GUI view port, redrawn on each set(). Safe to pass NULL to set()/end().

typedef struct FoxProgress FoxProgress;

// Open the page. `title` may contain '\n' to split into lines drawn beside the logo. The pointer must
// stay valid until fox_progress_end() (pass a string literal or a buffer you keep alive).
FoxProgress* fox_progress_begin(const char* title);

// Update progress, 0.0..1.0. Drawn quantised to quarter steps. Cheap; call it as often as you like.
void fox_progress_set(FoxProgress* p, float frac);

// True once the user pressed Back on the page. The caller should poll this in its loop and stop early
// if set (keeping whatever it has captured so far). Safe to pass NULL.
bool fox_progress_cancelled(FoxProgress* p);

// Tear the page down (it disappears, the desktop/next screen returns).
void fox_progress_end(FoxProgress* p);
