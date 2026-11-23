#include "upt/texture.h"
#include <ctype.h>
#include <string.h>

typedef struct Constant {
	Texture base;
	float3 value;
} Constant;

static float3 ConstantEval(Texture const *texture, HitRecord const *hit)
{
	(void)hit;
	return ((Constant const *)texture)->value;
}

Texture *TextureConstant(float3 value)
{
	Constant *this = xmalloc(sizeof(*this));
	*this = (Constant){.base = {.Eval = ConstantEval}, .value = value};
	return &this->base;
}

typedef struct Mix {
	Texture base;
	Texture *a, *b, *amount;
} Mix;

static float3 MixEval(Texture const *texture, HitRecord const *hit)
{
	Mix const *this = (Mix const *)texture;
	float t = fmaxf(0, fminf(1, this->amount->Eval(this->amount, hit).x));
	if (t == 0)
		return this->a->Eval(this->a, hit);
	if (t == 1)
		return this->b->Eval(this->b, hit);
	float3 a = this->a->Eval(this->a, hit), b = this->b->Eval(this->b, hit);
	return float3_add(float3_scale(a, 1 - t), float3_scale(b, t));
}

Texture *TextureMix(Texture *a, Texture *b, Texture *amount)
{
	Mix *this = xmalloc(sizeof(*this));
	*this = (Mix){.base = {.Eval = MixEval}, .a = a, .b = b, .amount = amount};
	return &this->base;
}

typedef struct Product {
	Texture base;
	Texture *a, *b;
} Product;

static float3 ProductEval(Texture const *texture, HitRecord const *hit)
{
	Product const *this = (Product const *)texture;
	float3 a = this->a->Eval(this->a, hit), b = this->b->Eval(this->b, hit);
	return float3_mul(a, b);
}

Texture *TextureScale(Texture *a, Texture *b)
{
	Product *this = xmalloc(sizeof(*this));
	*this = (Product){.base = {.Eval = ProductEval}, .a = a, .b = b};
	return &this->base;
}

typedef struct Mapped {
	Texture base;
	Texture *source;
	Transform inverse;
	float uscale, vscale, udelta, vdelta;
} Mapped;

static float3 MappedEval(Texture const *texture, HitRecord const *hit)
{
	Mapped const *this = (Mapped const *)texture;
	HitRecord mapped = *hit;
	mapped.ray.o = TransformPoint(this->inverse, hit->ray.o);
	mapped.ray.d = TransformVector(this->inverse, hit->ray.d);
	mapped.u = hit->u * this->uscale + this->udelta;
	mapped.v = hit->v * this->vscale + this->vdelta;
	mapped.footprint *= fmaxf(fabsf(this->uscale), fabsf(this->vscale));
	return this->source->Eval(this->source, &mapped);
}

Texture *TextureMap(Texture *source, Transform inverse, float uscale, float vscale, float udelta,
		    float vdelta)
{
	Mapped *this = xmalloc(sizeof(*this));
	*this = (Mapped){.base = {.Eval = MappedEval},
			 .source = source,
			 .inverse = inverse,
			 .uscale = uscale,
			 .vscale = vscale,
			 .udelta = udelta,
			 .vdelta = vdelta};
	return &this->base;
}

static float hash_cell(float x, float y, float z)
{
	uint32_t a, b, c;
	memcpy(&a, &x, sizeof(a));
	memcpy(&b, &y, sizeof(b));
	memcpy(&c, &z, sizeof(c));
	return (sample_hash(a ^ sample_hash(b ^ sample_hash(c))) >> 8) * 0x1p-24f;
}

Noise NoiseEval(float3 p)
{
	float cell[3] = {floorf(p.x), floorf(p.y), floorf(p.z)};
	float t[3] = {p.x - cell[0], p.y - cell[1], p.z - cell[2]};
	float w[3], d[3];
	for (int a = 0; a < 3; ++a) {
		w[a] = t[a] * t[a] * (3 - 2 * t[a]);
		d[a] = 6 * t[a] * (1 - t[a]);
	}
	float value = 0, gradient[3] = {0};
	for (int i = 0; i < 8; ++i) {
		int x = i & 1, y = (i >> 1) & 1, z = (i >> 2) & 1;
		float v = hash_cell(cell[0] + x, cell[1] + y, cell[2] + z);
		float a = x ? w[0] : 1 - w[0], b = y ? w[1] : 1 - w[1], c = z ? w[2] : 1 - w[2];
		value += v * a * b * c;
		gradient[0] += v * (x ? d[0] : -d[0]) * b * c;
		gradient[1] += v * a * (y ? d[1] : -d[1]) * c;
		gradient[2] += v * a * b * (z ? d[2] : -d[2]);
	}
	return (Noise){value, {gradient[0], gradient[1], gradient[2]}};
}

static float3 PatternEval(Texture const *texture, HitRecord const *hit)
{
	Pattern const *this = (Pattern const *)texture;
	float3 p = float3_scale(RayAt(hit->ray, hit->t), this->scale);
	float t = this->noise ? NoiseEval(p).value
			      : fmodf(fabsf(floorf(p.x) + floorf(p.y) + floorf(p.z)), 2);
	return float3_add(float3_scale(this->a, 1 - t), float3_scale(this->b, t));
}

Pattern *PatternCreate(float3 a, float3 b, float scale, bool noise)
{
	Pattern *this = xmalloc(sizeof(*this));
	*this = (Pattern){
	    .base = {.Eval = PatternEval}, .a = a, .b = b, .scale = scale, .noise = noise};
	return this;
}

static float3 bilinear(float3 const *pixels, unsigned width, unsigned height, HitRecord const *hit)
{
	float x = (hit->u - floorf(hit->u)) * width - .5f;
	float y = (hit->v - floorf(hit->v)) * height - .5f;
	int ix = floorf(x), iy = floorf(y);
	float fx = x - ix, fy = y - iy;
	float3 result = {0};
	for (int j = 0; j < 2; ++j)
		for (int i = 0; i < 2; ++i) {
			unsigned u = (ix + i + width) % width;
			unsigned v = (iy + j + height) % height;
			float weight = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
			result =
			    float3_add(result, float3_scale(pixels[(size_t)v * width + u], weight));
		}
	return result;
}

void ImageBuildMips(Image *this)
{
	this->levels[0] = this->pixels;
	this->nlevels = 1;
	unsigned w = this->width, h = this->height;
	while ((w > 1 || h > 1) && this->nlevels < 15) {
		unsigned nw = (w + 1) / 2, nh = (h + 1) / 2;
		float3 *pixels = xmalloc((size_t)nw * nh * sizeof(float3));
		float3 const *previous = this->levels[this->nlevels - 1];
		for (unsigned y = 0; y < nh; ++y)
			for (unsigned x = 0; x < nw; ++x) {
				float3 value = {0};
				for (unsigned j = 0; j < 2; ++j)
					for (unsigned i = 0; i < 2; ++i) {
						unsigned px = 2 * x + i < w ? 2 * x + i : w - 1;
						unsigned py = 2 * y + j < h ? 2 * y + j : h - 1;
						value = float3_add(value,
								   previous[(size_t)py * w + px]);
					}
				pixels[(size_t)y * nw + x] = float3_scale(value, .25f);
			}
		this->levels[this->nlevels++] = pixels;
		w = nw;
		h = nh;
	}
}

float3 ImageEval(Texture const *texture, HitRecord const *hit)
{
	Image const *this = (Image const *)texture;
	float footprint = fmaxf(this->width, this->height) * hit->footprint;
	if (footprint <= 1 || this->nlevels <= 1)
		return bilinear(this->pixels, this->width, this->height, hit);
	float lod = fmaxf(0, log2f(fmaxf(1, footprint)));
	unsigned nlevels = this->nlevels ? this->nlevels : 1;
	lod = fminf(lod, nlevels - 1);
	unsigned level = (unsigned)lod, next = level + 1 < nlevels ? level + 1 : level;
	unsigned w = this->width, h = this->height;
	for (unsigned i = 0; i < level; ++i) {
		w = (w + 1) / 2;
		h = (h + 1) / 2;
	}
	float3 a = bilinear(this->nlevels ? this->levels[level] : this->pixels, w, h, hit);
	float3 b = bilinear(this->nlevels ? this->levels[next] : this->pixels,
			    next == level ? w : (w + 1) / 2, next == level ? h : (h + 1) / 2, hit);
	return float3_add(float3_scale(a, 1 - (lod - level)), float3_scale(b, lod - level));
}

static unsigned ppm_number(FILE *f)
{
	int c;
	do {
		c = fgetc(f);
		if (c == '#')
			while ((c = fgetc(f)) != EOF && c != '\n') {
			}
	} while (c != EOF && isspace(c));
	ungetc(c, f);
	unsigned value;
	if (fscanf(f, "%u", &value) != 1)
		return UINT32_MAX;
	return value;
}

Image *ImageRead(char const *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		perror(path);
		exit(EXIT_FAILURE);
	}
	char magic[3];
	if (fscanf(f, "%2s", magic) != 1 || (strcmp(magic, "P3") && strcmp(magic, "P6")))
		goto invalid;
	unsigned w = ppm_number(f), h = ppm_number(f), max = ppm_number(f);
	if (!w || !h || w > 16384 || h > 16384 || max != 255)
		goto invalid;
	int separator = fgetc(f);
	if (!isspace(separator))
		goto invalid;
	Image *this = xmalloc(sizeof(*this));
	*this = (Image){.base = {.Eval = ImageEval},
			.width = w,
			.height = h,
			.pixels = xmalloc((size_t)w * h * sizeof(float3))};
	for (size_t i = 0; i < (size_t)w * h; ++i) {
		float rgb[3];
		for (int c = 0; c < 3; ++c) {
			unsigned v = magic[1] == '3' ? ppm_number(f) : (unsigned)fgetc(f);
			if (v > 255)
				goto invalid;
			float x = v / 255.0f;
			rgb[c] = x <= .04045f ? x / 12.92f : powf((x + .055f) / 1.055f, 2.4f);
		}
		this->pixels[i] = (float3){rgb[0], rgb[1], rgb[2]};
	}
	fclose(f);
	ImageBuildMips(this);
	return this;
invalid:
	fprintf(stderr, "%s: expected 8-bit P3/P6 PPM image\n", path);
	exit(EXIT_FAILURE);
}

void ImageFree(void *ptr)
{
	Image *this = ptr;
	for (unsigned i = 1; i < this->nlevels; ++i)
		free(this->levels[i]);
	free(this->pixels);
	free(this);
}

void SurfacePrepare(HitRecord *hit)
{
	Material const *mat = hit->material;
	if (!mat->texture && !mat->bump && !mat->roughness_map)
		return;
	if (float3_dot(hit->dpdu, hit->dpdu) == 0) {
		float3_basis(hit->sn, &hit->dpdu, &hit->dpdv);
		hit->u = atan2f(hit->sn.z, hit->sn.x) / (2 * M_PI) + .5f;
		hit->v = acosf(fmaxf(-1, fminf(1, hit->sn.y))) / M_PI;
	}
	float du_length = sqrtf(float3_dot(hit->dpdu, hit->dpdu));
	float dv_length = sqrtf(float3_dot(hit->dpdv, hit->dpdv));
	float incidence = fmaxf(.05f, fabsf(float3_dot(hit->gn, hit->ray.d)));
	hit->footprint /= fmaxf(1e-8f, fminf(du_length, dv_length) * incidence);
	if (!mat->bump)
		return;
	float du, dv;
	if (mat->bump->Eval == PatternEval && ((Pattern *)mat->bump)->noise) {
		Pattern *p = (Pattern *)mat->bump;
		Noise noise = NoiseEval(float3_scale(RayAt(hit->ray, hit->t), p->scale));
		float3 gradient = float3_scale(noise.gradient, p->scale * (p->b.x - p->a.x));
		du = float3_dot(gradient, hit->dpdu);
		dv = float3_dot(gradient, hit->dpdv);
	} else {
		HitRecord shifted = *hit;
		float eps = .001f, height = mat->bump->Eval(mat->bump, hit).x;
		shifted.u += eps;
		shifted.ray.o = float3_add(shifted.ray.o, float3_scale(hit->dpdu, eps));
		du = (mat->bump->Eval(mat->bump, &shifted).x - height) / eps;
		shifted = *hit;
		shifted.v += eps;
		shifted.ray.o = float3_add(shifted.ray.o, float3_scale(hit->dpdv, eps));
		dv = (mat->bump->Eval(mat->bump, &shifted).x - height) / eps;
	}
	float3 u = float3_add(hit->dpdu, float3_scale(hit->sn, mat->bump_scale * du));
	float3 v = float3_add(hit->dpdv, float3_scale(hit->sn, mat->bump_scale * dv));
	float3 n = float3_cross(u, v);
	if (float3_dot(n, n) > 0) {
		n = float3_norm(n);
		hit->sn = float3_dot(n, hit->gn) < 0 ? float3_scale(n, -1) : n;
	}
}
