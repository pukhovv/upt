#pragma once

#include "upt/ray_core.h"

typedef struct Volume {
	Aabb bounds;
	float3 absorption;
	float scattering, g;
} Volume;

float VolumeLength(Volume const *volume, Ray ray, float tmax, float *begin);
float3 VolumeTransmittance(Volume const *volume, Ray ray, float tmax);
bool VolumeSample(Volume const *volume, Ray ray, float tmax, float *t, float3 *weight);
float PhaseEval(float3 incoming, float3 outgoing, float g);
float3 PhaseSample(float3 incoming, float g, float *pdf);
