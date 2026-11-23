#include "upt/environment.h"

static float luminance(float3 c)
{
	return .2126f * c.x + .7152f * c.y + .0722f * c.z;
}

Environment *EnvironmentRead(char const *path)
{
	Environment *this = xcalloc(1, sizeof(*this));
	this->image = ImageRead(path);
	unsigned w = this->image->width, h = this->image->height;
	this->cdf = xmalloc((size_t)w * h * sizeof(double));
	for (unsigned y = 0; y < h; ++y) {
		double solid_angle = (2 * M_PI / w) * (cos(M_PI * y / h) - cos(M_PI * (y + 1) / h));
		for (unsigned x = 0; x < w; ++x) {
			size_t i = (size_t)y * w + x;
			this->total += luminance(this->image->pixels[i]) * solid_angle;
			this->cdf[i] = this->total;
		}
	}
	return this;
}

void EnvironmentFree(void *ptr)
{
	Environment *this = ptr;
	ImageFree(this->image);
	free(this->cdf);
	free(this);
}

bool EnvironmentActive(Scene const *scene)
{
	return luminance(scene->sky_color) > 0;
}

static void coordinates(float3 d, float *u, float *v)
{
	*u = atan2f(d.z, d.x) / (2 * M_PI) + .5f;
	*v = atan2f(hypotf(d.x, d.z), d.y) / M_PI;
}

float3 EnvironmentEval(Scene const *scene, float3 direction)
{
	Environment *this = scene->environment;
	if (!this)
		return scene->sky_color;
	HitRecord hit = {0};
	coordinates(direction, &hit.u, &hit.v);
	float half_row = .5f / this->image->height;
	hit.v = fmaxf(half_row, fminf(1 - half_row, hit.v));
	return float3_mul(scene->sky_color, ImageEval(&this->image->base, &hit));
}

float EnvironmentPdf(Scene const *scene, float3 direction)
{
	Environment *this = scene->environment;
	if (!this || this->total <= 0)
		return 1 / (4 * M_PI);
	float u, v;
	coordinates(direction, &u, &v);
	unsigned w = this->image->width, h = this->image->height;
	unsigned x = (unsigned)((u - floorf(u)) * w), y = (unsigned)fminf(h - 1, floorf(v * h));
	return luminance(this->image->pixels[(size_t)y * w + x]) / this->total;
}

float3 EnvironmentSample(Scene const *scene, float *pdf)
{
	Environment *this = scene->environment;
	double phi, cosine;
	if (!this || this->total <= 0) {
		phi = 2 * M_PI * rand_uniform();
		cosine = 1 - 2 * rand_uniform();
	} else {
		unsigned w = this->image->width, h = this->image->height;
		size_t lo = 0, hi = (size_t)w * h - 1;
		double target = rand_uniform() * this->total;
		while (lo < hi) {
			size_t mid = lo + (hi - lo) / 2;
			if (this->cdf[mid] <= target)
				lo = mid + 1;
			else
				hi = mid;
		}
		unsigned x = lo % w, y = lo / w;
		phi = 2 * M_PI * ((x + rand_uniform()) / w - .5f);
		double a = cos(M_PI * y / h), b = cos(M_PI * (y + 1) / h);
		cosine = a + (b - a) * ((double)rand_uniform() + 0x1p-25);
	}
	double sine = sqrt(fmax(0, 1 - cosine * cosine));
	float3 direction = {sine * cos(phi), cosine, sine * sin(phi)};
	*pdf = EnvironmentPdf(scene, direction);
	return direction;
}
