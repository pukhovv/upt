#include "upt/volume.h"

float VolumeLength(Volume const *this, Ray ray, float tmax, float *begin)
{
	float o[3] = {ray.o.x, ray.o.y, ray.o.z}, d[3] = {ray.d.x, ray.d.y, ray.d.z};
	float lo = 0, hi = tmax;
	for (int i = 0; i < 3; ++i) {
		if (d[i] == 0) {
			if (o[i] < this->bounds.corners[0][i] || o[i] > this->bounds.corners[1][i])
				return 0;
			continue;
		}
		float a = (this->bounds.corners[0][i] - o[i]) / d[i];
		float b = (this->bounds.corners[1][i] - o[i]) / d[i];
		lo = fmaxf(lo, fminf(a, b));
		hi = fminf(hi, fmaxf(a, b));
		if (hi <= lo)
			return 0;
	}
	*begin = lo;
	return (hi - lo) * sqrtf(float3_dot(ray.d, ray.d));
}

static float3 transmittance(Volume const *this, float distance)
{
	return (float3){expf(-(this->absorption.x + this->scattering) * distance),
			expf(-(this->absorption.y + this->scattering) * distance),
			expf(-(this->absorption.z + this->scattering) * distance)};
}

float3 VolumeTransmittance(Volume const *this, Ray ray, float tmax)
{
	if (!this)
		return (float3){1, 1, 1};
	float begin;
	return transmittance(this, VolumeLength(this, ray, tmax, &begin));
}

bool VolumeSample(Volume const *this, Ray ray, float tmax, float *t, float3 *weight)
{
	float begin, length = VolumeLength(this, ray, tmax, &begin);
	*weight = (float3){1, 1, 1};
	if (length <= 0)
		return false;
	if (this->scattering == 0) {
		*weight = transmittance(this, length);
		return false;
	}
	// RGB channel-mixture distance sampling; both event and escape PDFs are averaged.
	float extinction[3] = {this->absorption.x + this->scattering,
			       this->absorption.y + this->scattering,
			       this->absorption.z + this->scattering};
	unsigned channel = (unsigned)(3 * rand_uniform());
	float distance = -log1pf(-rand_uniform()) / extinction[channel];
	bool scatter = distance < length;
	float3 tr = transmittance(this, fminf(distance, length));
	float pdf = scatter
			? (extinction[0] * tr.x + extinction[1] * tr.y + extinction[2] * tr.z) / 3
			: (tr.x + tr.y + tr.z) / 3;
	*weight = pdf > 0 ? float3_scale(tr, (scatter ? this->scattering : 1) / pdf) : (float3){0};
	if (scatter)
		*t = begin + distance / sqrtf(float3_dot(ray.d, ray.d));
	return scatter;
}

float PhaseEval(float3 incoming, float3 outgoing, float g)
{
	float cosine = fmaxf(-1, fminf(1, float3_dot(incoming, outgoing)));
	float d = 1 + g * g - 2 * g * cosine;
	return (1 - g * g) / (4 * M_PI * d * sqrtf(d));
}

float3 PhaseSample(float3 incoming, float g, float *pdf)
{
	float u = rand_uniform(), cosine;
	if (fabsf(g) < .001f) {
		// Algebraic HG inversion without cancellation at g=0.
		float a = 2 * u - 1, d = 1 + g * a;
		cosine = (a + .5f * g * (a * a + 3) + g * g * a + .5f * g * g * g * (a * a - 1)) /
			 (d * d);
	} else {
		float s = (1 - g * g) / (1 - g + 2 * g * u);
		cosine = fmaxf(-1, fminf(1, (1 + g * g - s * s) / (2 * g)));
	}
	float phi = 2 * M_PI * rand_uniform(), sine = sqrtf(fmaxf(0, 1 - cosine * cosine));
	float3 x, y;
	float3_basis(incoming, &x, &y);
	float3 direction = float3_norm(float3_add(
	    float3_scale(incoming, cosine),
	    float3_add(float3_scale(x, sine * cosf(phi)), float3_scale(y, sine * sinf(phi)))));
	*pdf = PhaseEval(incoming, direction, g);
	return direction;
}
