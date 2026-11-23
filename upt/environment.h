#pragma once

#include "upt/texture.h"

typedef struct Environment {
	Image *image;
	double *cdf;
	double total;
} Environment;

Environment *EnvironmentRead(char const *path);
void EnvironmentFree(void *environment);
bool EnvironmentActive(Scene const *scene);
float3 EnvironmentEval(Scene const *scene, float3 direction);
float EnvironmentPdf(Scene const *scene, float3 direction);
float3 EnvironmentSample(Scene const *scene, float *pdf);
