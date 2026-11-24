#pragma once

#include "upt/environment.h"
#include "upt/light.h"
#include "upt/surfaces.h"
#include "upt/volume.h"

typedef struct Medium {
	Shape *object, *leaf;
	Glass const *glass;
} Medium;

static inline float medium_ior(Glass const *glass)
{
	return glass ? (glass->ior > 0 ? glass->ior : 1.5f) : 1;
}

static inline float3 medium_attenuation(Glass const *glass, float distance)
{
	if (!glass)
		return (float3){1, 1, 1};
	return (float3){powf(glass->transmittance.x, distance),
			powf(glass->transmittance.y, distance),
			powf(glass->transmittance.z, distance)};
}

static inline float3 SceneTrace(Scene *scene, Ray current)
{
	float3 radiance = {0}, beta = {1, 1, 1}, previous = current.o;
	float previous_pdf = 0, eta_scale = 1;
	bool specular = true;
	Medium media[32];
	size_t nmedia = 0;
	for (unsigned depth = 0;; ++depth) {
		HitRecord hit = {.ray = current};
		bool found = ShapeListHit(scene->shapes, scene->nshapes, &hit);
		if (scene->volume && !nmedia) {
			float t;
			float3 weight;
			bool scatter = VolumeSample(scene->volume, current,
						    found ? hit.t : INFINITY, &t, &weight);
			beta = float3_mul(beta, weight);
			if (float3_dot(beta, beta) == 0)
				break;
			if (scatter) {
				float3 point = RayAt(current, t);
				LightSample light;
				if (LightSampleOne(scene, point, &light)) {
					Ray shadow = {
					    .o = point, .d = light.direction, .time = current.time};
					bool blocked = false;
					for (size_t i = 0; i < scene->nshapes && !blocked; ++i)
						blocked = ShapeOccluded(scene->shapes[i], &shadow,
									0, light.distance - .0002f);
					if (!blocked) {
						float phase = PhaseEval(current.d, light.direction,
									scene->volume->g);
						float3 tr = VolumeTransmittance(
						    scene->volume, shadow, light.distance);
						float scale =
						    phase * MisWeight(light.pdf, phase) / light.pdf;
						radiance = float3_add(
						    radiance,
						    float3_scale(
							float3_mul(beta,
								   float3_mul(tr, light.radiance)),
							scale));
					}
				}
				previous = point;
				float3 direction =
				    PhaseSample(current.d, scene->volume->g, &previous_pdf);
				current = (Ray){.o = point,
						.d = direction,
						.width = current.width + current.spread * t,
						.spread = fmaxf(current.spread, .15f),
						.time = current.time};
				specular = false;
				goto roulette;
			}
		}
		if (!found) {
			float weight =
			    specular || !EnvironmentActive(scene)
				? 1
				: MisWeight(previous_pdf,
					    EnvironmentPdf(scene, current.d) / LightCount(scene));
			radiance = float3_add(
			    radiance,
			    float3_scale(float3_mul(beta, EnvironmentEval(scene, current.d)),
					 weight));
			break;
		}
		Glass const *medium = nmedia ? media[nmedia - 1].glass : NULL;
		float distance = hit.t * sqrtf(float3_dot(current.d, current.d));
		beta = float3_mul(beta, medium_attenuation(medium, distance));
		hit.footprint = current.width + current.spread * distance;
		float radius = hit.footprint;
		float3 emission = MaterialEmission(hit.material, &hit);
		float weight = specular || float3_dot(emission, emission) == 0
				   ? 1
				   : MisWeight(previous_pdf, LightPdf(scene, previous, &hit));
		radiance = float3_add(radiance, float3_scale(float3_mul(beta, emission), weight));
		SurfacePrepare(&hit);
		if (float3_dot(hit.sn, hit.ray.d) > 0)
			hit.sn = float3_scale(hit.sn, -1);
		bool glass = hit.material->Apply == GlassApply;
		bool entering = float3_dot(hit.gn, current.d) < 0;
		if (glass) {
			if ((entering && nmedia == 32) ||
			    (!entering && (!nmedia || media[nmedia - 1].object != hit.object ||
					   media[nmedia - 1].leaf != hit.leaf)))
				break; // Closed, properly nested boundaries; camera starts in air.
			Glass const *next = entering	 ? (Glass const *)hit.material
					    : nmedia > 1 ? media[nmedia - 2].glass
							 : NULL;
			hit.eta = medium_ior(medium) / medium_ior(next);
		}
		float3 point = RayAt(hit.ray, hit.t);
		LightSample light;
		if (!glass && hit.material->Apply != MirrorApply &&
		    hit.material->Apply != LampApply && LightSampleOne(scene, point, &light)) {
			float cosine = fmaxf(0, float3_dot(hit.sn, light.direction));
			if (cosine > 0 &&
			    float3_dot(light.direction, hit.gn) * float3_dot(hit.ray.d, hit.gn) <
				0) {
				Ray shadow = RaySpawn(point, hit.gn, light.direction);
				shadow.time = current.time;
				float length =
				    light.distance -
				    float3_dot(float3_sub(shadow.o, point), light.direction);
				bool blocked = false;
				for (size_t i = 0; i < scene->nshapes && !blocked; ++i)
					blocked = ShapeOccluded(scene->shapes[i], &shadow, 0,
								length - .0002f);
				if (!blocked) {
					BsdfEval f = hit.material->Eval(hit.material, &hit, light.direction);
					if (float3_dot(f.f, f.f) > 0) {
						float scale =
						    cosine * MisWeight(light.pdf, f.pdf) / light.pdf;
						float3 value = float3_mul(
						    f.f, float3_mul(
							     light.radiance,
							     float3_mul(medium_attenuation(medium, length),
									VolumeTransmittance(
									    nmedia ? NULL : scene->volume,
									    shadow, length))));
						radiance = float3_add(
						    radiance, float3_scale(float3_mul(beta, value), scale));
					}
				}
			}
		}
		BsdfSample sample;
		if (!hit.material->Apply(hit.material, &hit, &sample))
			break;
		beta = float3_mul(beta, sample.weight);
		if (sample.flags & BSDF_TRANSMIT) {
			if (entering)
				media[nmedia++] =
				    (Medium){hit.object, hit.leaf, (Glass const *)hit.material};
			else
				--nmedia;
			eta_scale /= hit.eta * hit.eta;
		}
		previous = point;
		previous_pdf = sample.pdf;
		specular = sample.flags & BSDF_SPECULAR;
		float spread = current.spread;
		if (!specular)
			spread = fmaxf(spread, .15f);
		else if (sample.flags & BSDF_TRANSMIT)
			spread *= hit.eta;
		current = RaySpawn(point, hit.gn, sample.direction);
		current.time = hit.ray.time;
		current.width = radius;
		current.spread = spread;
	roulette:
		if (depth >= 8) {
			float survival =
			    fminf(.95f, eta_scale * fmaxf(beta.x, fmaxf(beta.y, beta.z)));
			if (rand_uniform() >= survival)
				break;
			beta = float3_scale(beta, 1 / survival);
		}
	}
	return radiance;
}
