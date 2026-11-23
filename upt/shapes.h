#pragma once

#include "upt/bvh.h"

#define ALLOC_SHAPE(name)                                                                          \
	({                                                                                         \
		name *_ptr = xmalloc(sizeof(*_ptr));                                               \
		_ptr->base =                                                                       \
		    (Shape){.Hit = name##Hit, .Bounds = name##Bounds, .Occluded = name##Occluded}; \
		_ptr;                                                                              \
	})

typedef struct Sphere {
	Shape base;
	Material *material;
	float r;
	float3 o;
} Sphere;
bool SphereHit(Shape *shape, HitRecord *hit, float tmin, float tmax);
bool SphereOccluded(Shape *shape, Ray const *ray, float tmin, float tmax);
bool SphereBounds(Shape *shape, Aabb *bounds);

typedef struct Plane {
	Shape base;
	Material *material;
	float3 normal; // Unit normal; dot(normal, point) = offset.
	float offset;
} Plane;

bool PlaneHit(Shape *shape, HitRecord *hit, float tmin, float tmax);
bool PlaneOccluded(Shape *shape, Ray const *ray, float tmin, float tmax);
bool PlaneBounds(Shape *shape, Aabb *bounds);

bool ShapeListHit(Shape *const *shapes, size_t count, HitRecord *hit);

static inline bool SphereIntersect(Shape *shape, HitRecord *hit, float tmin, float tmax,
				   bool complete)
{
	Sphere *this = (Sphere *)shape;
	Ray ray = hit->ray;
	// Small, distant spheres lose the discriminant to cancellation in float.
	double x = (double)ray.o.x - this->o.x;
	double y = (double)ray.o.y - this->o.y;
	double z = (double)ray.o.z - this->o.z;
	double dx = ray.d.x, dy = ray.d.y, dz = ray.d.z;
	double a = dx * dx + dy * dy + dz * dz;
	double half_b = dx * x + dy * y + dz * z;
	double c = x * x + y * y + z * z - (double)this->r * this->r;
	double discr = half_b * half_b - a * c;
	if (discr < 0.0f)
		return false;
	discr = sqrt(discr);
	double t = (-half_b - discr) / a;
	if (t < tmin)
		t = (-half_b + discr) / a;
	if (t < tmin || t > tmax)
		return false;
	if (!complete)
		return true;
	hit->object = hit->leaf = shape;
	hit->t = t;
	hit->sn = float3_norm(float3_sub(RayAt(ray, hit->t), this->o));
	hit->gn = hit->sn;
	hit->dpdu = hit->dpdv = (float3){0};
	if (this->material &&
	    (this->material->texture || this->material->bump || this->material->roughness_map)) {
		hit->u = atan2f(hit->sn.z, hit->sn.x) / (2 * M_PI) + .5f;
		hit->v = acosf(fmaxf(-1, fminf(1, hit->sn.y))) / M_PI;
		float s = sqrtf(hit->sn.x * hit->sn.x + hit->sn.z * hit->sn.z);
		if (s > 1e-6f) {
			hit->dpdu =
			    float3_scale((float3){-hit->sn.z, 0, hit->sn.x}, 2 * M_PI * this->r);
			hit->dpdv = float3_scale(
			    (float3){hit->sn.y * hit->sn.x / s, -s, hit->sn.y * hit->sn.z / s},
			    M_PI * this->r);
		} else
			float3_basis(hit->sn, &hit->dpdu, &hit->dpdv);
	}
	hit->material = this->material;
	return true;
}

static inline bool SphereHitInline(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	return SphereIntersect(shape, hit, tmin, tmax, true);
}

static inline bool ShapeOccluded(Shape *shape, Ray const *ray, float tmin, float tmax)
{
	if (shape->Occluded == BvhOccluded)
		return BvhOccluded(shape, ray, tmin, tmax);
	if (shape->Occluded)
		return shape->Occluded(shape, ray, tmin, tmax);
	HitRecord hit = {.ray = *ray};
	return shape->Hit(shape, &hit, tmin, tmax);
}

static inline bool ShapeHit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
#ifdef UPT_INLINE_SPHERES
	if (shape->Hit == SphereHit)
		return SphereHitInline(shape, hit, tmin, tmax);
#endif
	if (shape->Hit == BvhHit)
		return BvhHit(shape, hit, tmin, tmax);
	return shape->Hit(shape, hit, tmin, tmax);
}
