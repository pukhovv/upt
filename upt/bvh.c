#include "upt/bvh.h"
#include "upt/mesh.h"
#include "upt/shapes.h"
#include "upt/surfaces.h"
#include "upt/transform.h"
#include <float.h>
#include <limits.h>
#include <string.h>

typedef float Slabs __attribute__((vector_size(16)));
typedef int SlabMask __attribute__((vector_size(16)));
#ifdef __AVX__
typedef float ChildBox __attribute__((vector_size(32)));
typedef int ChildMask __attribute__((vector_size(32)));
enum { CHILDREN = 8, LEVELS = 3 };
#else
typedef Slabs ChildBox;
typedef SlabMask ChildMask;
enum { CHILDREN = 4, LEVELS = 2 };
#endif

// Past SPLIT_DEPTH, binary splits halve the range and bound the traversal stack.
enum { SHAPE_LEAF_SIZE = 4, MESH_LEAF_SIZE = 2, LEAF_BITS = 3, LEAF_MASK = (1 << LEAF_BITS) - 1,
       SPLIT_DEPTH = 32,
       TRAVERSAL_DEPTH = LEVELS * (SPLIT_DEPTH + CHAR_BIT * sizeof(size_t)) };

typedef struct WideNode {
	_Alignas(64) ChildBox bounds[2][3];
	size_t child[CHILDREN];
} WideNode;

typedef struct MeshPart {
	Shape *object;
	Mesh *mesh;
} MeshPart;

typedef struct TriangleRef {
	MeshPart const *part;
	size_t index;
} TriangleRef;

typedef struct Entry {
	union {
		Shape *shape;
		MeshPart const *part;
	};
	Aabb bounds;
	size_t index;
} Entry;

typedef struct Bvh {
	Shape base;
	Entry *entries;
	WideNode *nodes;
	Aabb bounds;
	size_t count, finite, root;
	Mesh *mesh;
	MeshPart *parts;
	TriangleRef *triangles;
	Shape **visit;
	size_t nvisit;
	Shape *batch;
	Instance *instance;
} Bvh;

static float center(Aabb const *box, int axis)
{
	return 0.5f * box->corners[0][axis] + 0.5f * box->corners[1][axis];
}

static void extend(Aabb *box, Aabb const *other)
{
	for (int a = 0; a < 3; ++a) {
		if (other->corners[0][a] < box->corners[0][a])
			box->corners[0][a] = other->corners[0][a];
		if (other->corners[1][a] > box->corners[1][a])
			box->corners[1][a] = other->corners[1][a];
	}
}

enum { NBINS = 12 };

typedef struct Bin {
	Aabb bounds;
	size_t count;
} Bin;

static void bin_add(Bin *bin, Aabb const *box, size_t count)
{
	if (!bin->count)
		bin->bounds = *box;
	else
		extend(&bin->bounds, box);
	bin->count += count;
}

static double box_area(Aabb const *box)
{
	double x = (double)box->corners[1][0] - box->corners[0][0];
	double y = (double)box->corners[1][1] - box->corners[0][1];
	double z = (double)box->corners[1][2] - box->corners[0][2];
	return x * y + y * z + z * x;
}

static double bin_cost(Bin const *bin)
{
	return box_area(&bin->bounds) * bin->count;
}

static int bin_index(Aabb const *box, int axis, float lo, double scale)
{
	double bin = ((double)center(box, axis) - lo) * scale;
	if (!(bin > 0))
		return 0;
	return bin < NBINS ? (int)bin : NBINS - 1;
}

static size_t surface_split(Entry *entries, size_t begin, size_t end, float const lo[3],
			    float const hi[3])
{
	double best = INFINITY, scale[3];
	int axis = 0, boundary = 0;
	for (int a = 0; a < 3; ++a) {
		double span = (double)hi[a] - lo[a];
		if (!(span > 0) || !isfinite(span))
			continue;
		scale[a] = NBINS / span;
		Bin bins[NBINS] = {0};
		for (size_t i = begin; i < end; ++i) {
			Aabb const *box = &entries[i].bounds;
			bin_add(&bins[bin_index(box, a, lo[a], scale[a])], box, 1);
		}
		double right_cost[NBINS];
		Bin right = {0};
		for (int b = NBINS - 1; b > 0; --b) {
			if (bins[b].count)
				bin_add(&right, &bins[b].bounds, bins[b].count);
			right_cost[b] = right.count ? bin_cost(&right) : INFINITY;
		}
		Bin left = {0};
		for (int b = 0; b < NBINS - 1; ++b) {
			if (!bins[b].count)
				continue;
			bin_add(&left, &bins[b].bounds, bins[b].count);
			double cost = bin_cost(&left) + right_cost[b + 1];
			if (cost < best) {
				best = cost;
				axis = a;
				boundary = b + 1;
			}
		}
	}
	if (!boundary)
		return begin;
	size_t mid = begin;
	for (size_t i = begin; i < end; ++i) {
		if (bin_index(&entries[i].bounds, axis, lo[axis], scale[axis]) < boundary) {
			Entry tmp = entries[mid];
			entries[mid++] = entries[i];
			entries[i] = tmp;
		}
	}
	return mid;
}

typedef struct Range {
	Aabb bounds;
	float lo[3], hi[3];
} Range;

static Range describe(Entry const *entries, size_t begin, size_t end)
{
	Range range = {.bounds = entries[begin].bounds};
	for (int a = 0; a < 3; ++a)
		range.lo[a] = range.hi[a] = center(&range.bounds, a);
	for (size_t i = begin + 1; i < end; ++i) {
		Aabb const *box = &entries[i].bounds;
		extend(&range.bounds, box);
		for (int a = 0; a < 3; ++a) {
			float c = center(box, a);
			if (c < range.lo[a])
				range.lo[a] = c;
			if (c > range.hi[a])
				range.hi[a] = c;
		}
	}
	return range;
}

static size_t split(Bvh *bvh, size_t begin, size_t end, unsigned depth, Range const *range)
{
	if (depth >= SPLIT_DEPTH)
		return begin + (end - begin) / 2;
	float const *lo = range->lo, *hi = range->hi;
	Entry *entries = bvh->entries;
	size_t mid = (bvh->mesh || bvh->parts) ? surface_split(entries, begin, end, lo, hi) : begin;
	if (mid != begin)
		return mid;
	int axis = 0;
	for (int a = 1; a < 3; ++a)
		if (hi[a] - lo[a] > hi[axis] - lo[axis])
			axis = a;
	float point = 0.5f * lo[axis] + 0.5f * hi[axis];
	for (size_t i = begin; i < end; ++i) {
		if (center(&entries[i].bounds, axis) < point) {
			Entry tmp = entries[mid];
			entries[mid++] = entries[i];
			entries[i] = tmp;
		}
	}
	return mid == begin || mid == end ? begin + (end - begin) / 2 : mid;
}

static void subdivide(Bvh *bvh, size_t begin, size_t end, Range const *range, unsigned depth,
		      unsigned levels, size_t leaf_size, size_t endpoints[CHILDREN + 1], unsigned *count)
{
	if (!levels || end - begin <= leaf_size) {
		endpoints[(*count)++] = end;
		return;
	}
	size_t mid = split(bvh, begin, end, depth, range);
	for (size_t lo = begin, hi = mid; lo < end; lo = hi, hi = end) {
		Range child = describe(bvh->entries, lo, hi);
		subdivide(bvh, lo, hi, &child, depth + 1, levels - 1, leaf_size, endpoints,
			  count);
	}
}

static size_t build(Bvh *bvh, size_t begin, size_t end, Range const *range, unsigned depth,
		    size_t leaf_size, size_t *nnodes)
{
	if (end - begin <= leaf_size)
		return (begin << LEAF_BITS) | (end - begin);
	size_t index = (*nnodes)++;
	WideNode *node = &bvh->nodes[index];
	size_t endpoints[CHILDREN + 1] = {begin};
	unsigned count = 1;
	subdivide(bvh, begin, end, range, depth, LEVELS, leaf_size, endpoints, &count);
	unsigned lane = 0;
	for (; lane + 1 < count; ++lane) {
		size_t lo = endpoints[lane], hi = endpoints[lane + 1];
		Range child = describe(bvh->entries, lo, hi);
		for (int c = 0; c < 2; ++c)
			for (int a = 0; a < 3; ++a)
				node->bounds[c][a][lane] = child.bounds.corners[c][a];
		node->child[lane] = build(bvh, lo, hi, &child, depth + LEVELS, leaf_size, nnodes);
	}
	for (; lane < CHILDREN; ++lane) {
		for (int a = 0; a < 3; ++a) {
			node->bounds[0][a][lane] = INFINITY;
			node->bounds[1][a][lane] = -INFINITY;
		}
		node->child[lane] = SIZE_MAX;
	}
	return index << LEAF_BITS;
}

static void build_tree(Bvh *bvh)
{
	size_t count = bvh->finite, nnodes = 0;
	size_t leaf_size = (bvh->mesh || bvh->parts) ? MESH_LEAF_SIZE : SHAPE_LEAF_SIZE;
	Range range = describe(bvh->entries, 0, count);
	bvh->bounds = range.bounds;
	if (count > leaf_size) {
		if (count - 1 > SIZE_MAX / sizeof(*bvh->nodes)) {
			fprintf(stderr, "too many BVH entries\n");
			exit(EXIT_FAILURE);
		}
		bvh->nodes = aligned_alloc(_Alignof(WideNode), (count - 1) * sizeof(*bvh->nodes));
		if (!bvh->nodes) {
			perror("BVH nodes");
			exit(EXIT_FAILURE);
		}
	}
	bvh->root = build(bvh, 0, count, &range, 0, leaf_size, &nnodes);
}

static inline Slabs select_slabs(SlabMask mask, Slabs a, Slabs b)
{
	return (Slabs)(((SlabMask)a & mask) | ((SlabMask)b & ~mask));
}

static bool bounds_hit(Aabb const *box, Slabs origin, Slabs inv, SlabMask sign,
		       float tmin, float tmax)
{
	Slabs a, b;
	// Overlap the loads so both stay inside the six-float box.
	memcpy(&a, box, sizeof(a));
	memcpy(&b, (char const *)box + sizeof(*box) - sizeof(b), sizeof(b));
	b = __builtin_shufflevector(b, b, 1, 2, 3, 0);
	a = (a - origin) * inv;
	b = (b - origin) * inv;
	Slabs near = select_slabs(sign, b, a);
	Slabs far = select_slabs(sign, a, b) * (1.0f + 4.0f * FLT_EPSILON);
	// A parallel ray on a slab face produces NaN; leave that endpoint unclipped.
	Slabs min = {tmin, tmin, tmin, tmin}, max = {tmax, tmax, tmax, tmax};
	near = select_slabs(near > min, near, min);
	far = select_slabs(far < max, far, max);
	float enter = near[0] > near[1] ? near[0] : near[1];
	float leave = far[0] < far[1] ? far[0] : far[1];
	enter = near[2] > enter ? near[2] : enter;
	leave = far[2] < leave ? far[2] : leave;
	return !(enter > leave);
}

static inline ChildBox choose_child(ChildMask mask, ChildBox a, ChildBox b)
{
	return (ChildBox)(((ChildMask)a & mask) | ((ChildMask)b & ~mask));
}

static unsigned children_hit(WideNode const *node, Slabs origin, Slabs inv,
			     Slabs far_inv, SlabMask sign, float tmin, float tmax, ChildBox *distance)
{
	ChildBox enter = (ChildBox){0} + tmin, leave = (ChildBox){0} + tmax;
	for (int a = 0; a < 3; ++a) {
		unsigned corner = sign[a] & 1;
		ChildBox near = (node->bounds[corner][a] - origin[a]) * inv[a];
		ChildBox far = (node->bounds[1 - corner][a] - origin[a]) * far_inv[a];
		if (tmin < 0)
			far *= 1.0f + 4.0f * FLT_EPSILON;
		enter = choose_child(near > enter, near, enter);
		leave = choose_child(far < leave, far, leave);
	}
	*distance = enter;
	ChildMask mask = ~(enter > leave);
	#if defined(__AVX__)
	return __builtin_ia32_movmskps256((ChildBox)mask);
#elif defined(__SSE__)
	return __builtin_ia32_movmskps((ChildBox)mask);
#else
	return (mask[0] & 1) | (mask[1] & 2) | (mask[2] & 4) | (mask[3] & 8);
#endif
}

static void intersect(Entry const *entry, HitRecord *candidate, HitRecord *hit, size_t *closest,
		      float tmin, float *tmax)
{
	if (!ShapeHit(entry->shape, candidate, tmin, *tmax))
		return;
	if (candidate->t < *tmax || (candidate->t == *tmax && entry->index < *closest)) {
		*hit = *candidate;
		*tmax = candidate->t;
		*closest = entry->index;
	}
}

__attribute__((always_inline)) static inline bool traverse(Shape *shape, HitRecord *hit, float tmin,
							   float tmax, bool any)
{
	Bvh *bvh = (Bvh *)shape;
	Mesh *mesh = bvh->mesh;
	bool triangles = mesh || bvh->parts;
	MeshPart const *owner = NULL;
	Slabs origin = {hit->ray.o.x, hit->ray.o.y, hit->ray.o.z, 0};
	Slabs inv = 1.0f / (Slabs){hit->ray.d.x, hit->ray.d.y, hit->ray.d.z, 1};
	SlabMask sign = (SlabMask)inv < 0;
	Slabs far_inv = tmin < 0 ? inv : inv * (1.0f + 6.0f * FLT_EPSILON);
	bool in_tree = bvh->finite && bounds_hit(&bvh->bounds, origin, inv, sign, tmin, tmax);
	HitRecord candidate;
	MeshRay ray;
	TriangleHit nearest = {0};
	if (triangles) {
		if (!in_tree)
			return false;
		ray = MeshRayPrepare(&hit->ray);
	} else if (!any)
		candidate = *hit;
	size_t closest = SIZE_MAX;
	struct Pending { size_t ref; float distance; } pending[TRAVERSAL_DEPTH];
	size_t top = 0;
	for (size_t ref = in_tree ? bvh->root : SIZE_MAX; ref != SIZE_MAX;) {
		size_t n = ref >> LEAF_BITS, count = ref & LEAF_MASK;
		if (!count) {
			WideNode const *node = &bvh->nodes[n];
			ChildBox distance;
			unsigned bits = children_hit(node, origin, inv, far_inv, sign, tmin, tmax, &distance);
			if (bits && !(bits & (bits - 1))) {
				size_t child = node->child[__builtin_ctz(bits)];
				if (child != SIZE_MAX) {
					ref = child;
					continue;
				}
			}
			struct Pending first = {SIZE_MAX, INFINITY};
			for (; bits; bits &= bits - 1) {
				unsigned lane = __builtin_ctz(bits);
				size_t child = node->child[lane];
				if (child == SIZE_MAX)
					continue;
				struct Pending item = {child, distance[lane]};
				if (first.ref == SIZE_MAX)
					first = item;
				else if (!any && item.distance < first.distance) {
					pending[top++] = first;
					first = item;
				} else
					pending[top++] = item;
			}
			if (first.ref != SIZE_MAX) {
				ref = first.ref;
				continue;
			}
		}
		for (size_t i = n; i < n + count; ++i) {
			if (triangles) {
				MeshPart const *part = bvh->parts ? bvh->triangles[i].part : NULL;
				Mesh *geometry = part ? part->mesh : mesh;
				size_t index = part ? bvh->triangles[i].index : bvh->entries[i].index;
				if (any) {
					if (MeshTriangleOccluded(geometry, index, &ray, tmin, tmax))
						return true;
				} else {
					TriangleHit triangle =
					    MeshTriangleHit(geometry, index, &ray, tmin, tmax);
					bool first = part != owner ? !owner || part < owner : index < closest;
					if (triangle.valid &&
					    (triangle.t < tmax || (triangle.t == tmax && first))) {
						nearest = triangle;
						tmax = triangle.t;
						closest = index;
						owner = part;
					}
				}
				continue;
			}
			Entry const *entry = &bvh->entries[i];
			if (!bounds_hit(&entry->bounds, origin, inv, sign, tmin, tmax))
				continue;
			if (any) {
				if (ShapeOccluded(entry->shape, &hit->ray, tmin, tmax))
					return true;
			} else
				intersect(entry, &candidate, hit, &closest, tmin, &tmax);
		}
		while (!any && top && pending[top - 1].distance > tmax)
			--top;
		ref = top ? pending[--top].ref : SIZE_MAX;
	}
	for (size_t i = bvh->finite; i < bvh->count; ++i) {
		if (any) {
			if (ShapeOccluded(bvh->entries[i].shape, &hit->ray, tmin, tmax))
				return true;
		} else
			intersect(&bvh->entries[i], &candidate, hit, &closest, tmin, &tmax);
	}
	if (triangles && closest != SIZE_MAX) {
		hit->t = nearest.t;
		hit->u = nearest.u;
		hit->v = nearest.v;
		hit->primitive = closest;
		if (owner) {
			MeshComplete(owner->mesh, hit);
			hit->object = owner->object;
		}
	}
	return closest != SIZE_MAX;
}

bool BvhHit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	return traverse(shape, hit, tmin, tmax, false);
}

bool BvhOccluded(Shape *shape, Ray const *ray, float tmin, float tmax)
{
	HitRecord hit = {.ray = *ray};
	return traverse(shape, &hit, tmin, tmax, true);
}

Shape *BvhBuild(Shape *const *shapes, size_t count)
{
	Bvh *bvh = xmalloc(sizeof(*bvh));
	*bvh = (Bvh){.base = {.Hit = BvhHit, .Bounds = BvhBounds, .Occluded = BvhOccluded},
		     .count = count};
	if (!count)
		return &bvh->base;
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
	if (bvh->finite)
		build_tree(bvh);
	return &bvh->base;
}

void BvhFree(Shape *shape)
{
	Bvh *bvh = (Bvh *)shape;
	if (bvh->batch)
		BvhFree(bvh->batch);
	free(bvh->instance);
	free(bvh->visit);
	free(bvh->nodes);
	free(bvh->triangles);
	free(bvh->parts);
	free(bvh->entries);
	free(bvh);
}

Shape *BvhBuildMesh(Mesh *mesh)
{
	Bvh *bvh = xcalloc(1, sizeof(*bvh));
	bvh->base = (Shape){.Hit = BvhHit, .Bounds = BvhBounds, .Occluded = BvhOccluded};
	bvh->mesh = mesh;
	bvh->count = bvh->finite = mesh->ntriangles;
	if (!bvh->count)
		return &bvh->base;
	bvh->entries = xmalloc(bvh->count * sizeof(*bvh->entries));
	for (size_t i = 0; i < bvh->count; ++i) {
		bvh->entries[i].index = i;
		MeshTriangleBounds(mesh, i, &bvh->entries[i].bounds);
	}
	build_tree(bvh);
	return &bvh->base;
}

bool BvhBounds(Shape *shape, Aabb *bounds)
{
	Bvh *this = (Bvh *)shape;
	if (!this->finite || this->finite != this->count)
		return false;
	*bounds = this->bounds;
	return true;
}

bool BvhVisit(Shape *shape, void (*visit)(Shape *, void *), void *context)
{
	if (shape->Hit != BvhHit)
		return false;
	Bvh *this = (Bvh *)shape;
	if (this->mesh || this->parts)
		return false;
	if (this->visit) {
		for (size_t i = 0; i < this->nvisit; ++i)
			visit(this->visit[i], context);
	} else {
		for (size_t i = 0; i < this->count; ++i)
			visit(this->entries[i].shape, context);
	}
	return true;
}

static bool batch_hit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	Instance *this = (Instance *)shape;
	HitRecord local = *hit;
	local.ray.o = TransformPoint(this->inverse, hit->ray.o);
	local.ray.d = TransformVector(this->inverse, hit->ray.d);
	if (!BvhHit(this->child, &local, tmin, tmax))
		return false;
	InstanceComplete(this, hit, &local);
	return true;
}

static Mesh *static_mesh(Shape *shape)
{
	if (shape->Hit == InstanceHit)
		shape = ((Instance *)shape)->child;
	if (shape->Hit != MeshHit)
		return NULL;
	Mesh *mesh = (Mesh *)shape;
	return mesh->material && mesh->material->Apply != LampApply && mesh->ntriangles
		   ? mesh
		   : NULL;
}

Shape *BvhBuildScene(Shape *const *shapes, size_t count)
{
	enum { MIN_PARTS = 4, MIN_TRIANGLES = 4096 };
	Shape *source = BvhBuild(shapes, count);
	if (count < MIN_PARTS)
		return source;
	size_t first = 0, largest = 0;
	for (size_t i = 0; i < count; ++i) {
		Mesh *mesh = static_mesh(shapes[i]);
		if (mesh && mesh->ntriangles > largest) {
			first = i;
			largest = mesh->ntriangles;
		}
	}
	if (!largest)
		return source;
	bool transformed = shapes[first]->Hit == InstanceHit;
	Instance *instance = transformed ? (Instance *)shapes[first] : NULL;
	Mesh **parts = xcalloc(count, sizeof(*parts));
	size_t nparts = 0, triangles = 0;
	Bin group = {0};
	double area = 0;
	for (size_t i = 0; i < count; ++i) {
		Mesh *mesh = static_mesh(shapes[i]);
		if (!mesh || shapes[i]->Hit != shapes[first]->Hit ||
		    (instance && memcmp(&((Instance *)shapes[i])->transform, &instance->transform,
					sizeof(instance->transform))))
			continue;
		if (mesh->ntriangles > SIZE_MAX - triangles) {
			free(parts);
			return source;
		}
		Aabb bounds;
		if (!MeshBounds(&mesh->base, &bounds))
			continue;
		parts[i] = mesh;
		++nparts;
		triangles += mesh->ntriangles;
		area += box_area(&bounds);
		bin_add(&group, &bounds, 1);
	}
	// Separate bounds already cull well when meshes occupy different regions.
	if (nparts < MIN_PARTS || triangles < MIN_TRIANGLES ||
	    !(area > 1.5 * box_area(&group.bounds))) {
		free(parts);
		return source;
	}
	Bvh *flat = xcalloc(1, sizeof(*flat));
	flat->base = (Shape){.Hit = BvhHit, .Bounds = BvhBounds, .Occluded = BvhOccluded};
	flat->parts = xcalloc(nparts, sizeof(*flat->parts));
	flat->finite = flat->count = triangles;
	flat->entries = xcalloc(triangles, sizeof(*flat->entries));
	Shape **remaining = xcalloc(count - nparts + 1, sizeof(*remaining));
	size_t n = 0, j = 0, k = 0;
	for (size_t i = 0; i < count; ++i) {
		Mesh *mesh = parts[i];
		if (!mesh) {
			remaining[n++] = shapes[i];
			continue;
		}
		MeshPart *part = &flat->parts[k++];
		*part = (MeshPart){shapes[i], mesh};
		for (size_t t = 0; t < mesh->ntriangles; ++t) {
			Entry *entry = &flat->entries[j++];
			*entry = (Entry){.part = part, .index = t};
			MeshTriangleBounds(mesh, t, &entry->bounds);
		}
	}
	free(parts);
	build_tree(flat);
	flat->triangles = xcalloc(triangles, sizeof(*flat->triangles));
	for (size_t i = 0; i < triangles; ++i)
		flat->triangles[i] = (TriangleRef){flat->entries[i].part, flat->entries[i].index};
	free(flat->entries);
	flat->entries = NULL;
	Instance *wrapper = NULL;
	if (instance) {
		wrapper = InstanceCreate(&flat->base, instance->transform);
		wrapper->base.Hit = batch_hit;
	}
	remaining[n++] = wrapper ? &wrapper->base : &flat->base;
	Bvh *scene = (Bvh *)BvhBuild(remaining, n);
	free(remaining);
	scene->batch = &flat->base;
	scene->instance = wrapper;

	// Keep emitter enumeration in the same order as the unbatched BVH.
	Bvh *original = (Bvh *)source;
	scene->visit = xcalloc(count, sizeof(*scene->visit));
	scene->nvisit = count;
	for (size_t i = 0; i < count; ++i)
		scene->visit[i] = original->entries[i].shape;
	BvhFree(source);
	return &scene->base;
}
