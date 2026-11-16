#include "upt/surfaces.h"
#include "upt/float3.h"
#include <math.h>
#include <stdlib.h>

DEF_MATERIAL_APPLY(Mirror)
{
	Ray refl = {.o = RayAt(hit->ray, hit->t), .d = float3_refl(hit->ray.d, hit->sn)};
	hit->ray = refl;
	*color = float3_mul(*color, this->color);
	return true;
}

DEF_MATERIAL_APPLY(Lamp)
{
	float cosine = -float3_dot(hit->sn, hit->ray.d);
	*color = float3_scale(*color, 0.4 + 0.6 * cosine);
	*color = float3_mul(*color, this->color);
	return false;
}

static float fresnel(float cosine)
{
	float x = 1.0f - fminf(1.0f, fabsf(cosine));
	return 0.04f + 0.96f * x * x * x * x * x;
}

DEF_MATERIAL_APPLY(Lambert)
{
	float3 u, v;
	float3_basis(hit->sn, &u, &v);

	float phi = 2.0f * M_PI * rand_uniform();
	float r = sqrtf(rand_uniform());
	float x = r * cosf(phi);
	float y = r * sinf(phi);
	float z = sqrtf(fmaxf(0, 1 - x * x - y * y));

	hit->ray.o = RayAt(hit->ray, hit->t);
	hit->ray.d = float3_add(float3_add(float3_scale(u, x), float3_scale(v, y)),
				float3_scale(hit->sn, z));
	*color = float3_mul(*color, this->color);
	return true;
}

bool CheckerApply(Material const *material, HitRecord *hit, float3 *color)
{
	Checker const *checker = (Checker const *)material;
	float3 p = float3_scale(RayAt(hit->ray, hit->t), checker->scale);
	int parity = (int)floorf(p.x) + (int)floorf(p.y) + (int)floorf(p.z);
	Lambert diffuse = {.color = parity & 1 ? checker->b : checker->a};
	return LambertApply(&diffuse, hit, color);
}

bool CoatedApply(Material const *material, HitRecord *hit, float3 *color)
{
	Coated const *coated = (Coated const *)material;
	if (rand_uniform() < fresnel(float3_dot(hit->ray.d, hit->sn))) {
		Mirror mirror = {.color = {1, 1, 1}};
		return MirrorApply(&mirror, hit, color);
	}
	Lambert diffuse = {.color = coated->color};
	return LambertApply(&diffuse, hit, color);
}

bool MarbleApply(Material const *material, HitRecord *hit, float3 *color)
{
	Marble const *marble = (Marble const *)material;
	float3 p = float3_scale(RayAt(hit->ray, hit->t), marble->scale);
	float wave = p.x + 0.6f * p.z + 0.5f * sinf(p.y + 1.5f * sinf(p.z));
	float vein = 0.5f + 0.5f * sinf(wave + 0.2f * sinf(4 * wave));
	vein *= vein;
	vein *= vein;
	vein *= vein;
	Coated coat = {.color = float3_add(float3_scale(marble->a, 1 - vein),
					 float3_scale(marble->b, vein))};
	return CoatedApply(&coat.base, hit, color);
}

DEF_MATERIAL_APPLY(Phong)
{
	float phi = 2.0f * M_PI * rand_uniform();
	float cost = powf(1 - rand_uniform(), 1.0f / (this->pexp + 1));
	float sint = sqrtf(fmaxf(0, 1 - cost * cost));
	float x = cosf(phi) * sint;
	float y = sinf(phi) * sint;
	float z = cost;

	float3 u, v;
	float3 w =
	    float3_sub(hit->ray.d, float3_scale(hit->sn, 2.0f * float3_dot(hit->ray.d, hit->sn)));
	float3_basis(w, &u, &v);
	hit->ray.o = RayAt(hit->ray, hit->t);
	hit->ray.d =
	    float3_add(float3_add(float3_scale(u, x), float3_scale(v, y)), float3_scale(w, z));

	float cosine = float3_dot(hit->ray.d, hit->sn);
	if (cosine <= 0) {
		*color = (float3){0};
		return false;
	}
	*color = float3_mul(*color, float3_scale(this->color,
		(this->pexp + 2) / (this->pexp + 1) * cosine));
	return true;
}

DEF_MATERIAL_APPLY(Glass)
{
	float signed_cosine = float3_dot(hit->ray.d, hit->sn);
	// shortcut: disjoint homogeneous glass solids; track ray media for nested glass.
	if (signed_cosine > 0) {
		float distance = hit->t * sqrtf(float3_dot(hit->ray.d, hit->ray.d));
		float3 attenuation = {powf(this->transmittance.x, distance),
				      powf(this->transmittance.y, distance),
				      powf(this->transmittance.z, distance)};
		*color = float3_mul(*color, attenuation);
	}
	float eta = signed_cosine < 0 ? 1.0f / 1.5f : 1.5f;
	float3 normal = signed_cosine < 0 ? hit->sn : float3_scale(hit->sn, -1);
	float cosine = fminf(1, fabsf(signed_cosine));
	float sq = 1 - eta * eta * (1 - cosine * cosine);
	if (sq <= 0 || rand_uniform() < fresnel(eta > 1 ? sqrtf(sq) : cosine)) {
		Mirror mirror = {.color = {1, 1, 1}};
		return MirrorApply(&mirror, hit, color);
	}
	float3 direction = float3_add(float3_scale(hit->ray.d, eta),
				      float3_scale(normal, eta * cosine - sqrtf(sq)));
	hit->ray.o = RayAt(hit->ray, hit->t);
	hit->ray.d = direction;
	*color = float3_scale(*color, eta * eta);
	return true;
}
