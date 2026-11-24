#pragma once

#include "upt/ray_core.h"

// Borrows child shapes; rebuild after geometry changes.
Shape *BvhBuildScene(Shape *const *shapes, size_t count);
Shape *BvhBuild(Shape *const *shapes, size_t count);
bool BvhHit(Shape *shape, HitRecord *hit, float tmin, float tmax);
bool BvhOccluded(Shape *shape, Ray const *ray, float tmin, float tmax);
bool BvhBounds(Shape *shape, Aabb *bounds);
void BvhFree(Shape *shape);

struct Mesh;
Shape *BvhBuildMesh(struct Mesh *mesh);

bool BvhVisit(Shape *shape, void (*visit)(Shape *, void *), void *context);
