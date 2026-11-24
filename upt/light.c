#include "upt/light.h"
#include "upt/bvh.h"
#include "upt/environment.h"
#include "upt/mesh.h"
#include "upt/surfaces.h"

float MisWeight(float a, float b)
{
	if (a <= 0)
		return 0;
	float ratio = b / a;
	return 1 / (1 + ratio * ratio);
}

typedef struct Collect {
	Scene *scene;
	Transform transform;
	Shape *object;
	bool moving;
} Collect;

typedef struct LightTriangle {
	float3 p[3], n;
} LightTriangle;

static float3 vertex(Light const *light, size_t triangle, int v)
{
	Mesh *mesh = (Mesh *)light->leaf;
	return TransformPoint(light->transform, mesh->p[mesh->indices[triangle][v]]);
}

static void collect(Shape *shape, void *context)
{
	Collect *c = context;
	if (shape->Hit == InstanceHit || shape->Hit == MovingInstanceHit) {
		Instance *instance = (Instance *)shape;
		Collect child = {c->scene, TransformCompose(c->transform, instance->transform),
				 c->object ? c->object : shape,
				 c->moving || shape->Hit == MovingInstanceHit};
		collect(instance->child, &child);
		return;
	}
	if (BvhVisit(shape, collect, c))
		return;
	Material *mat = shape->Hit == MeshHit	  ? ((Mesh *)shape)->material
			: shape->Hit == SphereHit ? ((Sphere *)shape)->material
						  : NULL;
	if (!mat || mat->Apply != LampApply)
		return;
	if (c->moving) {
		fprintf(stderr, "moving area lights are unsupported\n");
		exit(EXIT_FAILURE);
	}
	Scene *scene = c->scene;
	Shape *object = c->object ? c->object : shape;
	for (size_t i = 0; i < scene->nlights; ++i)
		if (scene->lights[i].object == object && scene->lights[i].leaf == shape) {
			fprintf(stderr, "multiple copies of one emitter inside an instanced group "
					"are unsupported\n");
			exit(EXIT_FAILURE);
		}
	scene->lights = realloc(scene->lights, (scene->nlights + 1) * sizeof(Light));
	if (!scene->lights)
		exit(EXIT_FAILURE);
	Light *light = &scene->lights[scene->nlights++];
	*light = (Light){.object = c->object ? c->object : shape,
			 .leaf = shape,
			 .material = mat,
			 .transform = c->transform};
	if (!TransformInvert(light->transform, &light->inverse))
		exit(EXIT_FAILURE);
	if (shape->Hit == MeshHit) {
		Mesh *mesh = (Mesh *)shape;
		light->cdf = xmalloc(mesh->ntriangles * sizeof(double));
		light->triangles = xcalloc(mesh->ntriangles, sizeof(*light->triangles));
		bool reflected = float3_dot(light->transform.x,
					    float3_cross(light->transform.y, light->transform.z)) < 0;
		for (size_t i = 0; i < mesh->ntriangles; ++i) {
			LightTriangle *triangle = &light->triangles[i];
			for (int j = 0; j < 3; ++j)
				triangle->p[j] = vertex(light, i, j);
			float3 e1 = float3_sub(triangle->p[1], triangle->p[0]);
			float3 e2 = float3_sub(triangle->p[2], triangle->p[0]);
			float3 n = float3_cross(e1, e2);
			float length = sqrtf(float3_dot(n, n));
			light->area += .5f * length;
			light->cdf[i] = light->area;
			if (length > 0) {
				triangle->n = float3_scale(n, 1 / length);
				if (reflected)
					triangle->n = float3_scale(triangle->n, -1);
			}
		}
	}
}

void LightsBuild(Scene *scene)
{
	Collect c = {scene, TransformIdentity(), NULL, false};
	for (size_t i = 0; i < scene->nshapes; ++i)
		collect(scene->shapes[i], &c);
}

void LightsFree(Scene *scene)
{
	for (size_t i = 0; i < scene->nlights; ++i) {
		free(scene->lights[i].cdf);
		free(scene->lights[i].triangles);
	}
	free(scene->lights);
}

static float jacobian(Transform t, float3 n)
{
	float3 u, v;
	float3_basis(n, &u, &v);
	float3 cross = float3_cross(TransformVector(t, u), TransformVector(t, v));
	return sqrtf(float3_dot(cross, cross));
}

size_t LightCount(Scene const *scene)
{
	return scene->nlights + EnvironmentActive(scene);
}

bool LightSampleOne(Scene const *scene, float3 point, LightSample *sample)
{
	size_t count = LightCount(scene);
	if (!count)
		return false;
	size_t i = (size_t)(rand_uniform() * count);
	if (i == scene->nlights) {
		sample->direction = EnvironmentSample(scene, &sample->pdf);
		sample->pdf /= count;
		sample->distance = INFINITY;
		sample->radiance = EnvironmentEval(scene, sample->direction);
		return sample->pdf > 0;
	}
	Light const *light = &scene->lights[i];
	float3 p, n;
	float area;
	if (light->leaf->Hit == MeshHit) {
		Mesh *mesh = (Mesh *)light->leaf;
		if (light->area <= 0)
			return false;
		double target = rand_uniform() * light->area;
		size_t lo = 0, hi = mesh->ntriangles - 1;
		while (lo < hi) {
			size_t mid = lo + (hi - lo) / 2;
			if (light->cdf[mid] <= target)
				lo = mid + 1;
			else
				hi = mid;
		}
		float r = sqrtf(rand_uniform()), v = rand_uniform();
		LightTriangle const *triangle = &light->triangles[lo];
		p = float3_add(float3_scale(triangle->p[0], 1 - r),
			       float3_add(float3_scale(triangle->p[1], r * (1 - v)),
					  float3_scale(triangle->p[2], r * v)));
		n = triangle->n;
		area = light->area;
	} else {
		Sphere *sphere = (Sphere *)light->leaf;
		float z = 1 - 2 * rand_uniform(), phi = 2 * M_PI * rand_uniform(),
		      r = sqrtf(fmaxf(0, 1 - z * z));
		float3 local_n = {r * cosf(phi), z, r * sinf(phi)};
		p = TransformPoint(light->transform,
				   float3_add(sphere->o, float3_scale(local_n, sphere->r)));
		float3 u, v;
		float3_basis(local_n, &u, &v);
		n = float3_norm(float3_cross(TransformVector(light->transform, v),
					     TransformVector(light->transform, u)));
		// Inverse-transpose orientation remains outward under reflected transforms.
		float det = float3_dot(light->transform.x,
				       float3_cross(light->transform.y, light->transform.z));
		if (det < 0)
			n = float3_scale(n, -1);
		area = 4 * M_PI * sphere->r * sphere->r * jacobian(light->transform, local_n);
	}
	float3 delta = float3_sub(p, point);
	float distance2 = float3_dot(delta, delta);
	sample->distance = sqrtf(distance2);
	if (sample->distance <= .0002f || area <= 0)
		return false;
	sample->direction = float3_scale(delta, 1 / sample->distance);
	float cosine = -float3_dot(n, sample->direction);
	if (cosine <= 0)
		return false;
	sample->pdf = distance2 / (cosine * area * LightCount(scene));
	HitRecord hit = {
	    .ray = {.o = point, .d = sample->direction}, .t = sample->distance, .gn = n};
	sample->radiance = MaterialEmission(light->material, &hit);
	return sample->pdf > 0;
}

float LightPdf(Scene const *scene, float3 point, HitRecord const *hit)
{
	for (size_t i = 0; i < scene->nlights; ++i) {
		Light const *light = &scene->lights[i];
		if (light->object != hit->object || light->leaf != hit->leaf)
			continue;
		float area = light->area;
		if (light->leaf->Hit == SphereHit) {
			Sphere *sphere = (Sphere *)light->leaf;
			float3 p = TransformPoint(light->inverse, RayAt(hit->ray, hit->t));
			float3 n = float3_norm(float3_sub(p, sphere->o));
			area = 4 * M_PI * sphere->r * sphere->r * jacobian(light->transform, n);
		}
		float3 delta = float3_sub(RayAt(hit->ray, hit->t), point);
		float d2 = float3_dot(delta, delta);
		float cosine =
		    d2 > 0 ? -float3_dot(hit->gn, float3_scale(delta, 1 / sqrtf(d2))) : 0;
		return cosine > 0 && area > 0 ? d2 / (cosine * area * LightCount(scene)) : 0;
	}
	return 0;
}
