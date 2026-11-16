#pragma once

#include "upt/shapes.h"

static inline void place_on_surf(Sphere *a, Sphere const *b)
{
	float rs = a->r + b->r;
	float dx = a->o.x - b->o.x;
	float dz = a->o.z - b->o.z;
	a->o.y = b->o.y + sqrtf(rs * rs - dx * dx - dz * dz) + 0.0002;
}

Scene SceneAtelier_init(void);
