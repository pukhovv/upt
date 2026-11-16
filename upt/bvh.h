#pragma once

#include "upt/ray_core.h"

// Borrows child shapes; rebuild after geometry changes.
Shape *BvhBuild(Shape *const *shapes, size_t count);
void BvhFree(Shape *shape);
