#pragma once

#include "upt/ray_core.h"

double Render(Scene *scene, uint32_t *pixels, unsigned width, unsigned height,
	      unsigned long long frames, unsigned warmup, unsigned threads,
	      bool (*present)(uint32_t const *, unsigned, unsigned long long));
int RenderHeadless(Scene *scene, unsigned width, unsigned height, unsigned long long frames,
		   unsigned warmup, unsigned threads, char const *output);
#ifdef UPT_WITH_GUI
int RenderGui(Scene *scene, unsigned threads);
#endif
