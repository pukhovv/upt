#pragma once

#include <stdbool.h>

typedef struct Workers Workers;

Workers *WorkersInit(unsigned threads, unsigned limit);
bool WorkersRun(Workers *workers, unsigned count, bool (*run)(void *, unsigned), void *context);
void WorkersFree(Workers *workers);
