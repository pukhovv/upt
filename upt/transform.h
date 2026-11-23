#pragma once

#include "upt/shapes.h"

typedef struct Transform {
	float3 x, y, z, o; // Columns of the affine matrix.
} Transform;

static inline Transform TransformIdentity(void)
{
	return (Transform){.x = {1, 0, 0}, .y = {0, 1, 0}, .z = {0, 0, 1}};
}

static inline Transform TransformRotation(float3 axis, float angle)
{
	float c = cosf(angle), s = sinf(angle), d = 1 - c;
	return (Transform){.x = {c + axis.x * axis.x * d, axis.y * axis.x * d + axis.z * s,
				 axis.z * axis.x * d - axis.y * s},
			   .y = {axis.x * axis.y * d - axis.z * s, c + axis.y * axis.y * d,
				 axis.z * axis.y * d + axis.x * s},
			   .z = {axis.x * axis.z * d + axis.y * s, axis.y * axis.z * d - axis.x * s,
				 c + axis.z * axis.z * d}};
}

static inline float3 TransformVector(Transform t, float3 v)
{
	return float3_add(float3_scale(t.x, v.x),
			  float3_add(float3_scale(t.y, v.y), float3_scale(t.z, v.z)));
}

static inline float3 TransformPoint(Transform t, float3 p)
{
	return float3_add(TransformVector(t, p), t.o);
}

static inline Transform TransformCompose(Transform a, Transform b)
{
	return (Transform){TransformVector(a, b.x), TransformVector(a, b.y),
			   TransformVector(a, b.z), TransformPoint(a, b.o)};
}

typedef struct Instance {
	Shape base;
	Shape *child; // Borrowed, including for nested instances.
	Transform transform, inverse;
} Instance;

typedef struct Motion {
	float3 translation, axis;
	float angle; // Radians over [0, 1], about the instance origin in parent space.
} Motion;

typedef struct MovingInstance {
	Instance instance;
	Motion motion;
} MovingInstance;

MovingInstance *MovingInstanceCreate(Shape *child, Transform transform, Motion motion);
bool MovingInstanceHit(Shape *shape, HitRecord *hit, float tmin, float tmax);
bool MovingInstanceOccluded(Shape *shape, Ray const *ray, float tmin, float tmax);
bool MovingInstanceBounds(Shape *shape, Aabb *bounds);

Instance *InstanceCreate(Shape *child, Transform transform);
bool InstanceHit(Shape *shape, HitRecord *hit, float tmin, float tmax);
bool InstanceBounds(Shape *shape, Aabb *bounds);

bool InstanceOccluded(Shape *shape, Ray const *ray, float tmin, float tmax);

bool TransformInvert(Transform transform, Transform *inverse);
