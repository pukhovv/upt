#include "upt/shapes.h"
#include <float.h>

bool SphereHit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	return SphereHitInline(shape, hit, tmin, tmax);
}

bool PlaneHit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	Plane *this = (Plane *)shape;
	float denominator = float3_dot(this->normal, hit->ray.d);
	if (denominator == 0.0f)
		return false;
	float t = (this->offset - float3_dot(this->normal, hit->ray.o)) / denominator;
	if (t < tmin || t > tmax)
		return false;
	hit->object = hit->leaf = shape;
	hit->t = t;
	hit->gn = hit->sn = this->normal;
	float3_basis(hit->sn, &hit->dpdu, &hit->dpdv);
	float3 p = RayAt(hit->ray, hit->t);
	hit->u = float3_dot(p, hit->dpdu);
	hit->v = float3_dot(p, hit->dpdv);
	hit->material = this->material;
	return true;
}

bool PlaneBounds(Shape *shape, Aabb *bounds)
{
	(void)shape;
	(void)bounds;
	return false;
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
	Sphere *this = (Sphere *)shape;
	float c[3] = {this->o.x, this->o.y, this->o.z};
	float radius = fabsf(this->r);
	for (int axis = 0; axis < 3; ++axis) {
		bounds->corners[0][axis] = nextafterf(c[axis] - radius, -INFINITY);
		bounds->corners[1][axis] = nextafterf(c[axis] + radius, INFINITY);
	}
	return true;
}

bool SphereOccluded(Shape *shape, Ray const *ray, float tmin, float tmax)
{
	HitRecord hit = {.ray = *ray};
	return SphereIntersect(shape, &hit, tmin, tmax, false);
}

bool PlaneOccluded(Shape *shape, Ray const *ray, float tmin, float tmax)
{
	Plane *this = (Plane *)shape;
	float d = float3_dot(this->normal, ray->d);
	if (d == 0)
		return false;
	float t = (this->offset - float3_dot(this->normal, ray->o)) / d;
	return t >= tmin && t <= tmax;
}
