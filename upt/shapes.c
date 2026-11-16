#include "upt/shapes.h"
#include <float.h>

bool SphereHit(Shape *shape, HitRecord *hit, float near, float far)
{
	return SphereHitInline(shape, hit, near, far);
}

bool PlaneHit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	Plane *plane = (Plane *)shape;
	float denominator = float3_dot(plane->normal, hit->ray.d);
	if (denominator == 0.0f)
		return false;
	float t = (plane->offset - float3_dot(plane->normal, hit->ray.o)) / denominator;
	if (t < tmin || t > tmax)
		return false;
	hit->t = t;
	hit->sn = plane->normal;
	hit->material = plane->material;
	return true;
}

bool ShapeListHit(Shape *const *shapes, size_t count, HitRecord *hit)
{
	float tmax = FLT_MAX;
	HitRecord cur_hit = *hit;
	bool was_hit = false;
	for (size_t i = 0; i < count; ++i) {
		if (!ShapeHit(shapes[i], &cur_hit, 0.0001f, tmax))
			continue;
		if (cur_hit.t < tmax) {
			*hit = cur_hit;
			tmax = cur_hit.t;
			was_hit = true;
		}
	}
	return was_hit;
}

bool SphereBounds(Shape *shape, Aabb *bounds)
{
	Sphere *sphere = (Sphere *)shape;
	float c[3] = {sphere->o.x, sphere->o.y, sphere->o.z};
	float radius = fabsf(sphere->r);
	for (int axis = 0; axis < 3; ++axis) {
		bounds->corners[0][axis] = nextafterf(c[axis] - radius, -INFINITY);
		bounds->corners[1][axis] = nextafterf(c[axis] + radius, INFINITY);
	}
	return true;
}
