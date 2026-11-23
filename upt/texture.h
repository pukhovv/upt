#pragma once

#include "upt/transform.h"

struct Texture {
	float3 (*Eval)(Texture const *texture, HitRecord const *hit);
};

typedef struct Pattern {
	Texture base;
	float3 a, b;
	float scale;
	bool noise;
} Pattern;

typedef struct Image {
	Texture base;
	float3 *pixels;
	unsigned width, height;
	float3 *levels[15];
	unsigned nlevels;
} Image;

typedef struct Noise {
	float value;
	float3 gradient;
} Noise;

Texture *TextureConstant(float3 value);
Texture *TextureMix(Texture *a, Texture *b, Texture *amount);
Texture *TextureScale(Texture *a, Texture *b);
Texture *TextureMap(Texture *source, Transform inverse, float uscale, float vscale, float udelta,
		    float vdelta);

Noise NoiseEval(float3 p);
Pattern *PatternCreate(float3 a, float3 b, float scale, bool noise);
Image *ImageRead(char const *path);
void ImageBuildMips(Image *image);
void ImageFree(void *image);
float3 ImageEval(Texture const *texture, HitRecord const *hit);
void SurfacePrepare(HitRecord *hit);
