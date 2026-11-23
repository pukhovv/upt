#pragma once

#include "upt/ray_core.h"

int RenderHeadless(Scene *scene, unsigned width, unsigned height, unsigned long long frames,
		   unsigned warmup, unsigned threads, char const *output);
#ifdef UPT_WITH_GUI
int RenderGui(Scene *scene, unsigned threads);
#endif
