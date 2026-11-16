#include "upt/bvh.h"
#include "upt/shapes.h"
#include <float.h>

typedef struct Entry {
	Shape *shape;
	Aabb bounds;
	size_t index;
} Entry;

typedef struct Node {
	Aabb bounds;
	size_t skip, begin, end;
} Node;

typedef struct Bvh {
	Shape base;
	Entry *entries;
	Node *nodes;
	size_t count, finite, nnodes;
} Bvh;

static float center(Aabb const *box, int axis)
{
	return 0.5f * box->corners[0][axis] + 0.5f * box->corners[1][axis];
}

static void build(Bvh *bvh, size_t begin, size_t end)
{
	Node *node = &bvh->nodes[bvh->nnodes++];
	node->bounds = bvh->entries[begin].bounds;
	float lo[3], hi[3];
	for (int axis = 0; axis < 3; ++axis)
		lo[axis] = hi[axis] = center(&node->bounds, axis);
	for (size_t i = begin + 1; i < end; ++i) {
		Aabb const *box = &bvh->entries[i].bounds;
		for (int axis = 0; axis < 3; ++axis) {
			if (box->corners[0][axis] < node->bounds.corners[0][axis])
				node->bounds.corners[0][axis] = box->corners[0][axis];
			if (box->corners[1][axis] > node->bounds.corners[1][axis])
				node->bounds.corners[1][axis] = box->corners[1][axis];
			float c = center(box, axis);
			if (c < lo[axis]) lo[axis] = c;
			if (c > hi[axis]) hi[axis] = c;
		}
	}
	node->begin = begin;
	node->end = end;
	if (end - begin > 4) {
		int axis = 0;
		for (int a = 1; a < 3; ++a)
			if (hi[a] - lo[a] > hi[axis] - lo[axis]) axis = a;
		float split = 0.5f * lo[axis] + 0.5f * hi[axis];
		size_t mid = begin;
		for (size_t i = begin; i < end; ++i) {
			if (center(&bvh->entries[i].bounds, axis) < split) {
				Entry tmp = bvh->entries[mid];
				bvh->entries[mid++] = bvh->entries[i];
				bvh->entries[i] = tmp;
			}
		}
		if (mid == begin || mid == end)
			mid = begin + (end - begin) / 2;
		node->end = begin;
		build(bvh, begin, mid);
		build(bvh, mid, end);
	}
	node->skip = bvh->nnodes;
}

static bool bounds_hit(Aabb const *box, float const origin[3], float const inv[3],
		       int const sign[3], float near, float far)
{
	for (int axis = 0; axis < 3; ++axis) {
		float a = (box->corners[sign[axis]][axis] - origin[axis]) * inv[axis];
		float b = (box->corners[1 - sign[axis]][axis] - origin[axis]) * inv[axis];
		// Widen the far distance for roundoff; comparisons leave NaNs unclipped.
		b *= 1.0f + 4.0f * FLT_EPSILON;
		near = a > near ? a : near;
		far = b < far ? b : far;
		if (near > far) return false;
	}
	return true;
}

static void intersect(Entry const *entry, HitRecord *candidate, HitRecord *hit, size_t *closest,
		      float tmin, float *tmax)
{
	if (!ShapeHit(entry->shape, candidate, tmin, *tmax))
		return;
	if (candidate->t < *tmax ||
	    (candidate->t == *tmax && entry->index < *closest)) {
		*hit = *candidate;
		*tmax = candidate->t;
		*closest = entry->index;
	}
}

static bool BvhHit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	Bvh *bvh = (Bvh *)shape;
	float origin[3] = {hit->ray.o.x, hit->ray.o.y, hit->ray.o.z};
	float inv[3] = {1.0f / hit->ray.d.x, 1.0f / hit->ray.d.y, 1.0f / hit->ray.d.z};
	int sign[3] = {signbit(inv[0]) != 0, signbit(inv[1]) != 0, signbit(inv[2]) != 0};
	HitRecord candidate = *hit;
	size_t closest = SIZE_MAX;
	// Children cover the root; avoid testing its box as well.
	for (size_t n = bvh->nnodes > 1 ? 1 : 0; n < bvh->nnodes;) {
		Node const *node = &bvh->nodes[n];
		if (!bounds_hit(&node->bounds, origin, inv, sign, tmin, tmax)) {
			n = node->skip;
			continue;
		}
		++n;
		for (size_t i = node->begin; i < node->end; ++i)
			intersect(&bvh->entries[i], &candidate, hit, &closest, tmin, &tmax);
	}
	for (size_t i = bvh->finite; i < bvh->count; ++i)
		intersect(&bvh->entries[i], &candidate, hit, &closest, tmin, &tmax);
	return closest != SIZE_MAX;
}

Shape *BvhBuild(Shape *const *shapes, size_t count)
{
	Bvh *bvh = xmalloc(sizeof(*bvh));
	*bvh = (Bvh){.base = {.Hit = BvhHit}, .count = count};
	if (!count) return &bvh->base;
	bvh->entries = xmalloc(count * sizeof(*bvh->entries));
	for (size_t i = 0; i < count; ++i) {
		Entry entry = {.shape = shapes[i], .index = i};
		bvh->entries[i] = entry;
		bool bounded = shapes[i]->Bounds && shapes[i]->Bounds(shapes[i], &entry.bounds);
		for (int axis = 0; bounded && axis < 3; ++axis)
			bounded = isfinite(entry.bounds.corners[0][axis]) &&
				  isfinite(entry.bounds.corners[1][axis]) &&
				  entry.bounds.corners[0][axis] <= entry.bounds.corners[1][axis];
		if (bounded) {
			bvh->entries[i] = bvh->entries[bvh->finite];
			bvh->entries[bvh->finite++] = entry;
		}
	}
	if (bvh->finite) {
		bvh->nodes = xmalloc((2 * bvh->finite - 1) * sizeof(*bvh->nodes));
		build(bvh, 0, bvh->finite);
	}
	return &bvh->base;
}

void BvhFree(Shape *shape)
{
	Bvh *bvh = (Bvh *)shape;
	free(bvh->nodes);
	free(bvh->entries);
	free(bvh);
}
