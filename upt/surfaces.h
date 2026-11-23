#pragma once

#include "upt/ray_core.h"

#define DEF_MATERIAL_APPLY(name)                                                                   \
	bool name##Apply(Material const *mat, HitRecord const *hit, BsdfSample *sample)
#define DEF_MATERIAL_EVAL(name)                                                                    \
	BsdfEval name##Eval(Material const *mat, HitRecord const *hit, float3 wi)

#define ALLOC_MATERIAL(name)                                                                       \
	({                                                                                         \
		name *_ptr = xmalloc(sizeof(*_ptr));                                               \
		_ptr->base = (Material){.Apply = name##Apply, .Eval = name##Eval};                 \
		_ptr;                                                                              \
	})

typedef struct Mirror {
	Material base;
	float3 color;
} Mirror;
DEF_MATERIAL_APPLY(Mirror);
DEF_MATERIAL_EVAL(Mirror);

typedef struct Lamp {
	Material base;
	float3 color;
} Lamp;
DEF_MATERIAL_APPLY(Lamp);
DEF_MATERIAL_EVAL(Lamp);

typedef struct Lambert {
	Material base;
	float3 color;
} Lambert;
DEF_MATERIAL_APPLY(Lambert);
DEF_MATERIAL_EVAL(Lambert);

typedef struct Glass {
	Material base;
	float3 transmittance; // Fraction transmitted through one unit of glass.
	float ior;	      // Zero selects the original 1.5 default.
} Glass;
DEF_MATERIAL_APPLY(Glass);
DEF_MATERIAL_EVAL(Glass);

typedef struct Phong {
	Material base;
	float3 color;
	float pexp;
} Phong;
DEF_MATERIAL_APPLY(Phong);
DEF_MATERIAL_EVAL(Phong);

typedef struct Checker {
	Material base;
	float3 a, b;
	float scale;
} Checker;
DEF_MATERIAL_APPLY(Checker);
DEF_MATERIAL_EVAL(Checker);

typedef struct Coated {
	Material base;
	float3 color;
} Coated;
DEF_MATERIAL_APPLY(Coated);
DEF_MATERIAL_EVAL(Coated);

typedef struct Marble {
	Material base;
	float3 a, b;
	float scale;
} Marble;
DEF_MATERIAL_APPLY(Marble);
DEF_MATERIAL_EVAL(Marble);

typedef struct Ggx {
	Material base;
	float3 color;
	float roughness;
} Ggx;
DEF_MATERIAL_APPLY(Ggx);
DEF_MATERIAL_EVAL(Ggx);

float3 MaterialEmission(Material const *mat, HitRecord const *hit);
