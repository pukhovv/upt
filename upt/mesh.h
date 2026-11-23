#pragma once

#include "upt/shapes.h"

typedef struct Mesh {
	Shape base;
	Material *material;
	float3 *p, *n;
	float (*uv)[2];
	uint32_t (*indices)[3];
	size_t nvertices, ntriangles;
	Shape *tree;
} Mesh;

typedef struct MeshRay {
	double o[3], sx, sy, dz;
	int kx, ky, kz;
} MeshRay;

typedef struct TriangleHit {
	float t, u, v;
	unsigned valid;
} TriangleHit;

Mesh *MeshCreate(float3 *p, float3 *n, float (*uv)[2], uint32_t (*indices)[3], size_t nvertices,
		 size_t ntriangles, Material *material);
Mesh *MeshReadPly(char const *path, Material *material);
void MeshFree(void *mesh);
bool MeshHit(Shape *shape, HitRecord *hit, float tmin, float tmax);
bool MeshBounds(Shape *shape, Aabb *bounds);
MeshRay MeshRayPrepare(Ray const *ray);
TriangleHit MeshTriangleHit(Mesh const *mesh, size_t triangle, MeshRay const *ray, float tmin,
			    float tmax);
bool MeshTriangleOccluded(Mesh const *mesh, size_t triangle, MeshRay const *ray, float tmin,
			  float tmax);
bool MeshTriangleBounds(Mesh const *mesh, size_t triangle, Aabb *bounds);

bool MeshOccluded(Shape *shape, Ray const *ray, float tmin, float tmax);
