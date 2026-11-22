#pragma once

typedef struct FoxProgress FoxProgress;

FoxProgress* fox_progress_begin(const char* title);

void fox_progress_set(FoxProgress* p, float frac);

bool fox_progress_cancelled(FoxProgress* p);

void fox_progress_end(FoxProgress* p);
