#pragma once

#include "upt/transform.h"

typedef struct Light {
	Shape *object, *leaf;
	Material *material;
	Transform transform, inverse;
	double *cdf;
	struct LightTriangle *triangles;
	double area;
} Light;

typedef struct LightSample {
	float3 direction, radiance;
	float distance, pdf;
} LightSample;

void LightsBuild(Scene *scene);
void LightsFree(Scene *scene);
bool LightSampleOne(Scene const *scene, float3 point, LightSample *sample);
float LightPdf(Scene const *scene, float3 point, HitRecord const *hit);
float MisWeight(float a, float b);

size_t LightCount(Scene const *scene);
