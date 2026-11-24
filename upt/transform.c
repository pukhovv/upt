#include "upt/transform.h"

static float3 normal(Transform inverse, float3 n)
{
	return float3_norm(
	    (float3){float3_dot(inverse.x, n), float3_dot(inverse.y, n), float3_dot(inverse.z, n)});
}

bool TransformInvert(Transform transform, Transform *result)
{
	float3 a = float3_cross(transform.y, transform.z);
	float3 b = float3_cross(transform.z, transform.x);
	float3 c = float3_cross(transform.x, transform.y);
	float det = float3_dot(transform.x, a);
	if (!isfinite(det) || fabsf(det) < 1e-20f)
		return false;
	a = float3_scale(a, 1 / det);
	b = float3_scale(b, 1 / det);
	c = float3_scale(c, 1 / det);
	Transform inverse = {.x = {a.x, b.x, c.x}, .y = {a.y, b.y, c.y}, .z = {a.z, b.z, c.z}};
	inverse.o = float3_scale(TransformVector(inverse, transform.o), -1);
	*result = inverse;
	return true;
}

static void instance_init(Instance *this, Shape *child, Transform transform)
{
	Transform inverse;
	if (!TransformInvert(transform, &inverse)) {
		fprintf(stderr, "singular instance transform\n");
		exit(EXIT_FAILURE);
	}

	this->child = child;
	this->transform = transform;
	this->inverse = inverse;
}

Instance *InstanceCreate(Shape *child, Transform transform)
{
	Instance *this = ALLOC_SHAPE(Instance);
	instance_init(this, child, transform);
	return this;
}

bool InstanceHit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	Instance *this = (Instance *)shape;
	HitRecord local = *hit;
	local.ray.o = TransformPoint(this->inverse, hit->ray.o);
	local.ray.d = TransformVector(this->inverse, hit->ray.d);
	if (!ShapeHit(this->child, &local, tmin, tmax))
		return false;
	local.object = shape;
	InstanceComplete(this, hit, &local);
	return true;
}

void InstanceComplete(Instance const *this, HitRecord *hit, HitRecord *local)
{
	local->ray = hit->ray;
	local->sn = normal(this->inverse, local->sn);
	local->gn = normal(this->inverse, local->gn);
	local->dpdu = TransformVector(this->transform, local->dpdu);
	local->dpdv = TransformVector(this->transform, local->dpdv);
	*hit = *local;
}

bool InstanceBounds(Shape *shape, Aabb *bounds)
{
	Instance *this = (Instance *)shape;
	Aabb box;
	if (!this->child->Bounds || !this->child->Bounds(this->child, &box))
		return false;
	for (int axis = 0; axis < 3; ++axis) {
		bounds->corners[0][axis] = INFINITY;
		bounds->corners[1][axis] = -INFINITY;
	}
	for (int corner = 0; corner < 8; ++corner) {
		float3 p = {box.corners[corner & 1][0], box.corners[(corner >> 1) & 1][1],
			    box.corners[(corner >> 2) & 1][2]};
		p = TransformPoint(this->transform, p);
		float v[3] = {p.x, p.y, p.z};
		for (int axis = 0; axis < 3; ++axis) {
			bounds->corners[0][axis] = fminf(bounds->corners[0][axis], v[axis]);
			bounds->corners[1][axis] = fmaxf(bounds->corners[1][axis], v[axis]);
		}
	}
	for (int axis = 0; axis < 3; ++axis) {
		bounds->corners[0][axis] = nextafterf(bounds->corners[0][axis], -INFINITY);
		bounds->corners[1][axis] = nextafterf(bounds->corners[1][axis], INFINITY);
	}
	return true;
}

bool InstanceOccluded(Shape *shape, Ray const *ray, float tmin, float tmax)
{
	Instance *this = (Instance *)shape;
	Ray local = *ray;
	local.o = TransformPoint(this->inverse, ray->o);
	local.d = TransformVector(this->inverse, ray->d);
	return ShapeOccluded(this->child, &local, tmin, tmax);
}

MovingInstance *MovingInstanceCreate(Shape *child, Transform transform, Motion motion)
{
	if (motion.angle != 0) {
		float length = float3_dot(motion.axis, motion.axis);
		if (!(length > 0) || !isfinite(length)) {
			fprintf(stderr, "invalid motion axis\n");
			exit(EXIT_FAILURE);
		}
		motion.axis = float3_norm(motion.axis);
	}
	MovingInstance *this = xmalloc(sizeof(*this));
	*this = (MovingInstance){.instance.base = {.Hit = MovingInstanceHit,
						   .Bounds = MovingInstanceBounds,
						   .Occluded = MovingInstanceOccluded},
				 .motion = motion};
	instance_init(&this->instance, child, transform);
	return this;
}

static Instance instance_at(MovingInstance const *this, float time)
{
	time = fmaxf(0, fminf(1, time));
	Instance at = this->instance;
	if (time == 0)
		return at;
	Transform r = TransformRotation(this->motion.axis, this->motion.angle * time);
	Transform rt = {
	    .x = {r.x.x, r.y.x, r.z.x}, .y = {r.x.y, r.y.y, r.z.y}, .z = {r.x.z, r.y.z, r.z.z}};
	at.transform = TransformCompose(r, at.transform);
	at.transform.o =
	    float3_add(this->instance.transform.o, float3_scale(this->motion.translation, time));
	at.inverse = TransformCompose(this->instance.inverse, rt);
	at.inverse.o = float3_scale(TransformVector(at.inverse, at.transform.o), -1);
	return at;
}

bool MovingInstanceHit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	Instance at = instance_at((MovingInstance *)shape, hit->ray.time);
	if (!InstanceHit(&at.base, hit, tmin, tmax))
		return false;
	hit->object = shape;
	return true;
}

bool MovingInstanceOccluded(Shape *shape, Ray const *ray, float tmin, float tmax)
{
	Instance at = instance_at((MovingInstance *)shape, ray->time);
	return InstanceOccluded(&at.base, ray, tmin, tmax);
}

bool MovingInstanceBounds(Shape *shape, Aabb *bounds)
{
	MovingInstance *this = (MovingInstance *)shape;
	if (!InstanceBounds(&this->instance.base, bounds))
		return false;
	float delta[3] = {this->motion.translation.x, this->motion.translation.y,
			  this->motion.translation.z};
	if (this->motion.angle != 0) {
		// A sphere about the pivot bounds every rotation, including intermediate poses.
		float radius = 0;
		float3 center = this->instance.transform.o;
		for (int i = 0; i < 8; ++i) {
			float3 p = {bounds->corners[i & 1][0], bounds->corners[(i >> 1) & 1][1],
				    bounds->corners[(i >> 2) & 1][2]};
			float3 v = float3_sub(p, center);
			radius = fmaxf(radius, sqrtf(float3_dot(v, v)));
		}
		radius *= 1 + 8 * 0x1p-23f;
		*bounds =
		    (Aabb){.corners = {{center.x - radius, center.y - radius, center.z - radius},
				       {center.x + radius, center.y + radius, center.z + radius}}};
	}
	for (int i = 0; i < 3; ++i) {
		bounds->corners[0][i] =
		    nextafterf(bounds->corners[0][i] + fminf(0, delta[i]), -INFINITY);
		bounds->corners[1][i] =
		    nextafterf(bounds->corners[1][i] + fmaxf(0, delta[i]), INFINITY);
	}
	return true;
}
