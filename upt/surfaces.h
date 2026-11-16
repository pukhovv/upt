#pragma once

#include "upt/ray_core.h"

#define DEF_MATERIAL_APPLY(name) bool name##Apply(name const *this, HitRecord *hit, float3 *color)

#define ALLOC_MATERIAL(name)                                                                       \
	({                                                                                         \
		name *_ptr = xmalloc(sizeof(*_ptr));                                               \
		_ptr->base.Apply = (void *)name##Apply;                                            \
		_ptr;                                                                              \
	})

typedef struct Mirror {
	Material base;
	float3 color;
} Mirror;
DEF_MATERIAL_APPLY(Mirror);

typedef struct Lamp {
	Material base;
	float3 color;
} Lamp;
DEF_MATERIAL_APPLY(Lamp);

typedef struct Lambert {
	Material base;
	float3 color;
} Lambert;
DEF_MATERIAL_APPLY(Lambert);

typedef struct Glass {
	Material base;
	float3 transmittance; // Fraction transmitted through one unit of glass.
} Glass;
DEF_MATERIAL_APPLY(Glass);

typedef struct Phong {
	Material base;
	float3 color;
	float pexp;
} Phong;
DEF_MATERIAL_APPLY(Phong);

typedef struct Checker {
	Material base;
	float3 a, b;
	float scale;
} Checker;
bool CheckerApply(Material const *material, HitRecord *hit, float3 *color);

typedef struct Coated {
	Material base;
	float3 color;
} Coated;
bool CoatedApply(Material const *material, HitRecord *hit, float3 *color);

typedef struct Marble {
	Material base;
	float3 a, b;
	float scale;
} Marble;
bool MarbleApply(Material const *material, HitRecord *hit, float3 *color);
