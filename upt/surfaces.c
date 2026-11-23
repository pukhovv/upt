#include "upt/surfaces.h"
#include "upt/texture.h"

static float3 albedo(Material const *mat, HitRecord const *hit, float3 color)
{
	return mat->texture ? mat->texture->Eval(mat->texture, hit) : color;
}

static float3 hemisphere(float3 n, float cosine, float phi)
{
	float3 u, v;
	float3_basis(n, &u, &v);
	float sine = sqrtf(fmaxf(0, 1 - cosine * cosine));
	return float3_add(float3_scale(n, cosine), float3_add(float3_scale(u, sine * cosf(phi)),
							      float3_scale(v, sine * sinf(phi))));
}

static float fresnel(float cosine)
{
	float x = 1 - fminf(1, fabsf(cosine));
	return .04f + .96f * x * x * x * x * x;
}

#define ZERO_EVAL(name)                                                                            \
	BsdfEval name##Eval(Material const *mat, HitRecord const *hit, float3 wi)                  \
	{                                                                                          \
		(void)mat;                                                                         \
		(void)hit;                                                                         \
		(void)wi;                                                                          \
		return (BsdfEval){0};                                                              \
	}

ZERO_EVAL(Mirror)
ZERO_EVAL(Lamp)
ZERO_EVAL(Glass)

DEF_MATERIAL_APPLY(Mirror)
{
	Mirror const *this = (Mirror const *)mat;
	*sample = (BsdfSample){.direction = float3_refl(hit->ray.d, hit->sn),
			       .weight = albedo(mat, hit, this->color),
			       .pdf = 1,
			       .flags = BSDF_SPECULAR};
	return float3_dot(sample->direction, hit->gn) * float3_dot(hit->ray.d, hit->gn) < 0;
}

DEF_MATERIAL_APPLY(Lamp)
{
	(void)mat;
	(void)hit;
	(void)sample;
	return false;
}

float3 MaterialEmission(Material const *mat, HitRecord const *hit)
{
	if (mat->Apply != LampApply || float3_dot(hit->gn, hit->ray.d) >= 0)
		return (float3){0};
	return albedo(mat, hit, ((Lamp const *)mat)->color);
}

DEF_MATERIAL_EVAL(Lambert)
{
	Lambert const *this = (Lambert const *)mat;
	float cosine = fmaxf(0, float3_dot(hit->sn, wi));
	return (BsdfEval){float3_scale(albedo(mat, hit, this->color), cosine > 0 ? 1 / M_PI : 0),
			  cosine / M_PI};
}

DEF_MATERIAL_APPLY(Lambert)
{
	Lambert const *this = (Lambert const *)mat;
	sample->direction = hemisphere(hit->sn, sqrtf(rand_uniform()), 2 * M_PI * rand_uniform());
	sample->weight = albedo(mat, hit, this->color);
	sample->pdf = fmaxf(0, float3_dot(hit->sn, sample->direction)) / M_PI;
	sample->flags = 0;
	return sample->pdf > 0 &&
	       float3_dot(sample->direction, hit->gn) * float3_dot(hit->ray.d, hit->gn) < 0;
}

static Lambert checker(Checker const *this, HitRecord const *hit)
{
	float3 p = float3_scale(RayAt(hit->ray, hit->t), this->scale);
	float parity = fmodf(fabsf(floorf(p.x) + floorf(p.y) + floorf(p.z)), 2);
	return (Lambert){.color = parity ? this->b : this->a};
}

DEF_MATERIAL_APPLY(Checker)
{
	Lambert m = checker((Checker const *)mat, hit);
	return LambertApply(&m.base, hit, sample);
}

DEF_MATERIAL_EVAL(Checker)
{
	Lambert m = checker((Checker const *)mat, hit);
	return LambertEval(&m.base, hit, wi);
}

DEF_MATERIAL_APPLY(Coated)
{
	Coated const *this = (Coated const *)mat;
	float f = fresnel(float3_dot(hit->ray.d, hit->sn));
	if (rand_uniform() < f) {
		Mirror m = {.color = {1, 1, 1}};
		bool ok = MirrorApply(&m.base, hit, sample);
		sample->pdf = f;
		return ok;
	}
	Lambert m = {.base = {.texture = mat->texture}, .color = this->color};
	bool ok = LambertApply(&m.base, hit, sample);
	sample->pdf *= 1 - f;
	return ok;
}

DEF_MATERIAL_EVAL(Coated)
{
	Coated const *this = (Coated const *)mat;
	Lambert m = {.base = {.texture = mat->texture}, .color = this->color};
	BsdfEval value = LambertEval(&m.base, hit, wi);
	float p = 1 - fresnel(float3_dot(hit->ray.d, hit->sn));
	value.pdf *= p;
	value.f = float3_scale(value.f, p);
	return value;
}

static Coated marble(Marble const *this, HitRecord const *hit)
{
	float3 p = float3_scale(RayAt(hit->ray, hit->t), this->scale);
	float wave = p.x + .6f * p.z + .5f * sinf(p.y + 1.5f * sinf(p.z));
	float vein = .5f + .5f * sinf(wave + .2f * sinf(4 * wave));
	vein *= vein;
	vein *= vein;
	vein *= vein;
	return (Coated){
	    .color = float3_add(float3_scale(this->a, 1 - vein), float3_scale(this->b, vein))};
}

DEF_MATERIAL_APPLY(Marble)
{
	Coated m = marble((Marble const *)mat, hit);
	return CoatedApply(&m.base, hit, sample);
}

DEF_MATERIAL_EVAL(Marble)
{
	Coated m = marble((Marble const *)mat, hit);
	return CoatedEval(&m.base, hit, wi);
}

DEF_MATERIAL_EVAL(Phong)
{
	Phong const *this = (Phong const *)mat;
	float c = fmaxf(0, float3_dot(wi, float3_refl(hit->ray.d, hit->sn)));
	float power = powf(c, this->pexp);
	return (BsdfEval){
	    float3_scale(albedo(mat, hit, this->color),
			 float3_dot(hit->sn, wi) > 0 ? (this->pexp + 2) * power / (2 * M_PI) : 0),
	    (this->pexp + 1) * power / (2 * M_PI)};
}

DEF_MATERIAL_APPLY(Phong)
{
	Phong const *this = (Phong const *)mat;
	sample->direction =
	    hemisphere(float3_refl(hit->ray.d, hit->sn), powf(rand_uniform(), 1 / (this->pexp + 1)),
		       2 * M_PI * rand_uniform());
	BsdfEval f = PhongEval(mat, hit, sample->direction);
	sample->pdf = f.pdf;
	if (sample->pdf <= 0 || float3_dot(hit->sn, sample->direction) <= 0)
		return false;
	sample->weight = float3_scale(f.f, float3_dot(hit->sn, sample->direction) / sample->pdf);
	sample->flags = 0;
	return float3_dot(sample->direction, hit->gn) * float3_dot(hit->ray.d, hit->gn) < 0;
}

DEF_MATERIAL_APPLY(Glass)
{
	Glass const *this = (Glass const *)mat;
	float signed_cosine = float3_dot(hit->ray.d, hit->gn);
	float ior = this->ior > 0 ? this->ior : 1.5f;
	float eta = hit->eta > 0 ? hit->eta : signed_cosine < 0 ? 1 / ior : ior;
	float3 n = signed_cosine < 0 ? hit->gn : float3_scale(hit->gn, -1);
	float cosine = fminf(1, fabsf(signed_cosine));
	float sq = 1 - eta * eta * (1 - cosine * cosine), cost = sqrtf(fmaxf(0, sq));
	float f = 1;
	if (eta == 1)
		f = 0;
	else if (sq > 0) {
		float rs = (eta * cosine - cost) / (eta * cosine + cost);
		float rp = (cosine - eta * cost) / (cosine + eta * cost);
		f = .5f * (rs * rs + rp * rp);
	}
	*sample = (BsdfSample){.flags = BSDF_SPECULAR};
	if (rand_uniform() < f) {
		sample->direction = float3_refl(hit->ray.d, n);
		sample->weight = (float3){1, 1, 1};
		sample->pdf = f;
	} else {
		sample->direction = float3_norm(float3_add(float3_scale(hit->ray.d, eta),
							   float3_scale(n, eta * cosine - cost)));
		sample->weight = (float3){eta * eta, eta * eta, eta * eta};
		sample->pdf = 1 - f;
		sample->flags |= BSDF_TRANSMIT;
	}
	return true;
}

static float ggx_d(float cosine, float a)
{
	float d = cosine * cosine * (a * a - 1) + 1;
	return a * a / (M_PI * d * d);
}

static float ggx_g(float cosine, float a)
{
	return 2 * cosine / (cosine + sqrtf(a * a + (1 - a * a) * cosine * cosine));
}

static float ggx_alpha(Ggx const *this, HitRecord const *hit)
{
	float r = this->base.roughness_map
		      ? this->base.roughness_map->Eval(this->base.roughness_map, hit).x
		      : this->roughness;
	r = fmaxf(0, fminf(1, r));
	return fmaxf(.001f, r * r);
}

static BsdfEval ggx_eval(Ggx const *this, HitRecord const *hit, float3 wi, float a)
{
	float3 wo = float3_scale(hit->ray.d, -1);
	float ci = float3_dot(hit->sn, wi), co = float3_dot(hit->sn, wo);
	if (ci <= 0 || co <= 0)
		return (BsdfEval){0};
	float3 h = float3_norm(float3_add(wi, wo));
	float ch = fmaxf(0, float3_dot(hit->sn, h)), wh = fabsf(float3_dot(wo, h));
	float d = ggx_d(ch, a);
	float pdf = wh > 0 ? d * ch / (4 * wh) : 0;
	float t = powf(1 - wh, 5);
	float3 c = albedo(&this->base, hit, this->color);
	float3 f = float3_add(c, float3_scale(float3_sub((float3){1, 1, 1}, c), t));
	return (BsdfEval){float3_scale(f, d * ggx_g(ci, a) * ggx_g(co, a) / (4 * ci * co)), pdf};
}

DEF_MATERIAL_EVAL(Ggx)
{
	Ggx const *this = (Ggx const *)mat;
	return ggx_eval(this, hit, wi, ggx_alpha(this, hit));
}

DEF_MATERIAL_APPLY(Ggx)
{
	Ggx const *this = (Ggx const *)mat;
	float a = ggx_alpha(this, hit), u = rand_uniform();
	float3 h =
	    hemisphere(hit->sn, sqrtf((1 - u) / (1 + (a * a - 1) * u)), 2 * M_PI * rand_uniform());
	sample->direction = float3_refl(hit->ray.d, h);
	BsdfEval f = ggx_eval(this, hit, sample->direction, a);
	sample->pdf = f.pdf;
	if (sample->pdf <= 0)
		return false;
	sample->weight =
	    float3_scale(f.f, fmaxf(0, float3_dot(hit->sn, sample->direction)) / sample->pdf);
	sample->flags = 0;
	return float3_dot(sample->direction, hit->gn) * float3_dot(hit->ray.d, hit->gn) < 0;
}
