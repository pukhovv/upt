#pragma once

#include "upt/float3.h"
#include "upt/util.h"
#include <stdbool.h>

typedef float3 Color;
typedef struct Ray Ray;
typedef struct HitRecord HitRecord;
typedef struct Shape Shape;
typedef struct Material Material;
typedef struct Camera Camera;
typedef struct Scene Scene;
typedef struct Texture Texture;

struct Ray {
	float3 o, d;
	float width, spread;
	float time; // Shutter coordinate in [0, 1], constant along a path.
};

static inline float3 RayAt(Ray const r, float t)
{
	return float3_add(r.o, float3_scale(r.d, t));
}

static inline Ray RaySpawn(float3 point, float3 normal, float3 direction)
{
	float side = float3_dot(normal, direction) >= 0 ? 1 : -1;
	float3 offset = float3_scale(normal, side * 0.0001f);
	float3 origin = float3_add(point, offset);
	if (offset.x)
		origin.x = nextafterf(origin.x, offset.x > 0 ? INFINITY : -INFINITY);
	if (offset.y)
		origin.y = nextafterf(origin.y, offset.y > 0 ? INFINITY : -INFINITY);
	if (offset.z)
		origin.z = nextafterf(origin.z, offset.z > 0 ? INFINITY : -INFINITY);
	return (Ray){.o = origin, .d = direction};
}

struct HitRecord {
	Ray ray;
	float3 sn;
	float t;
	Material *material;
	float3 gn, dpdu, dpdv;
	float u, v;
	size_t primitive;
	Shape *object, *leaf;
	float eta, footprint;
};

enum { BSDF_SPECULAR = 1, BSDF_TRANSMIT = 2 };
typedef struct BsdfSample {
	float3 direction, weight;
	float pdf;
	unsigned flags;
} BsdfSample;

typedef struct BsdfEval {
	float3 f;
	float pdf;
} BsdfEval;

struct Material {
	bool (*Apply)(Material const *mat, HitRecord const *hit, BsdfSample *sample);
	BsdfEval (*Eval)(Material const *mat, HitRecord const *hit, float3 wi);
	Texture *texture, *bump, *roughness_map;
	float bump_scale;
};

typedef struct Aabb {
	float corners[2][3];
} Aabb;

struct Shape {
	bool (*Hit)(Shape *shape, HitRecord *hit, float tmin, float tmax);
	// NULL or false leaves the shape outside the finite hierarchy.
	bool (*Bounds)(Shape *shape, Aabb *bounds);
	bool (*Occluded)(Shape *shape, Ray const *ray, float tmin, float tmax);
};

struct Camera {
	float3 pos, dir, axis_x, axis_y;
	float ratio; // res_x / res_y
	float fov;   // axis_y in rad
	float spread;
	float shutter_open, shutter_close;
};

static inline void CameraSetScreen(Camera *cam, float fov, float ratio)
{
	cam->fov = fov;
	cam->ratio = ratio;
	cam->spread = 0;
	cam->shutter_open = cam->shutter_close = 0;
}

static inline void CameraLookAt(Camera *cam, float3 const eye, float3 const at, float3 const up)
{
	cam->pos = eye;
	cam->dir = float3_norm(float3_sub(at, eye));
	cam->axis_x = float3_norm(float3_cross(cam->dir, up));
	cam->axis_y = float3_cross(cam->axis_x, cam->dir);

	float scale = 2.0f * tanf(cam->fov / 2.0f);
	cam->axis_x = float3_scale(cam->axis_x, cam->ratio * scale);
	cam->axis_y = float3_scale(cam->axis_y, scale);
}

static inline Ray CameraCast(Camera const *cam, float x, float y)
{
	Ray r = {.o = cam->pos,
		 .d = float3_add(cam->dir, float3_add(float3_scale(cam->axis_x, x),
						      float3_scale(cam->axis_y, y)))};
	r.d = float3_norm(r.d);
	r.spread = cam->spread;
	r.time = cam->shutter_open;
	if (cam->shutter_close > cam->shutter_open)
		r.time += (cam->shutter_close - cam->shutter_open) * rand_uniform();
	return r;
}

struct Scene {
	Shape **shapes;
	size_t nshapes;
	Camera camera;
	float3 sky_color;
	struct Resource *resources;
	size_t nresources;
	struct Light *lights;
	size_t nlights;
	struct Environment *environment;
	struct Volume *volume;
};
