#include "upt/bvh.h"
#include "upt/scenes/builtin.h"
#include "upt/shapes.h"
#include "upt/surfaces.h"
#include "uvideomem/uvideomem.h"

static inline void place_on_surf(Sphere *a, Sphere const *b)
{
	float rs = a->r + b->r;
	float dx = a->o.x - b->o.x;
	float dz = a->o.z - b->o.z;
	a->o.y = b->o.y + sqrtf(rs * rs - dx * dx - dz * dz) + 0.0002;
}

Scene SceneOriginal_init(void)
{
	float3 const sky_color = {0.34f, 0.3f, 0.4f};

	Camera cam;
	CameraSetScreen(&cam, 3.14f / 3.0, (float)UVIDEOMEM_W / (float)UVIDEOMEM_H);
	CameraLookAt(&cam, (float3){0.0f, 0.0f, -0.4f}, (float3){0.0f, 0.0f, 0.0f},
		     (float3){0.0f, -1.0f, 0.0f});

	Lambert *lambert1 = ALLOC_MATERIAL(Lambert);
	lambert1->color = (float3){0.8f, 0.8f, 0.8f};
	Lambert *lambert2 = ALLOC_MATERIAL(Lambert);
	lambert2->color = (float3){0.9f, 0.7f, 0.7f};
	Lambert *lambert3 = ALLOC_MATERIAL(Lambert);
	lambert3->color = (float3){0.7f, 0.4f, 1.0f};
	Lamp *lamp1 = ALLOC_MATERIAL(Lamp);
	lamp1->color = (float3){1.0f, 0.8f, 0.25f};
	Lamp *lamp2 = ALLOC_MATERIAL(Lamp);
	lamp2->color = (float3){0.9f, 0.7f, 0.8f};
	Lamp *lamp3 = ALLOC_MATERIAL(Lamp);
	lamp3->color = (float3){1.0f, 1.0f, 1.0f};
	Mirror *mirror1 = ALLOC_MATERIAL(Mirror);
	mirror1->color = (float3){0.95f, 0.95f, 0.95f};
	Glass *glass1 = ALLOC_MATERIAL(Glass);
	glass1->ior = 1.5f;
	glass1->transmittance = (float3){0.95f, 0.95f, 0.95f};
	Phong *phong1 = ALLOC_MATERIAL(Phong);
	phong1->color = (float3){0.95f, 0.95f, 0.95f};
	phong1->pexp = 3.0f;
	Phong *phong2 = ALLOC_MATERIAL(Phong);
	phong2->color = (float3){0.95f, 0.65f, 0.85f};
	phong2->pexp = 16.0f;
	Phong *phong3 = ALLOC_MATERIAL(Phong);
	phong3->color = (float3){0.70f, 1.0f, 0.95f};
	phong3->pexp = 80.0f;

	size_t nsph = 18;
	Sphere *sph[nsph];
	for (size_t i = 0; i < nsph; ++i)
		sph[i] = ALLOC_SHAPE(Sphere);

	size_t sp = 0;
	sph[sp]->r = M_SQRT2 * 100.0f;
	sph[sp]->o = (float3){1.0f, -106.0f, 100.0f};
	sph[sp]->material = (Material *)lambert1;
	sp++;

	sph[sp]->r = 0.5f;
	sph[sp]->o = (float3){1.5f, 0.0f, 5.0f};
	sph[sp]->material = (Material *)lamp1;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.6f;
	sph[sp]->o = (float3){3.0f, 0.0f, 7.0f};
	sph[sp]->material = (Material *)lamp2;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.8f;
	sph[sp]->o = (float3){-1.0f, 0.0f, 4.0f};
	sph[sp]->material = (Material *)lambert2;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 2.2f;
	sph[sp]->o = (float3){-2.5f, 0.0f, 7.5f};
	sph[sp]->material = (Material *)mirror1;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.8f;
	sph[sp]->o = (float3){-3.0f, 1.5f, 5.0f};
	sph[sp]->material = (Material *)lamp3;
	sp++;

	sph[sp]->r = 1.5f;
	sph[sp]->o = (float3){3.0f, 0.0f, 9.0f};
	sph[sp]->material = (Material *)mirror1;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.4f;
	sph[sp]->o = (float3){1.5f, 0.35f, 2.2f};
	sph[sp]->material = (Material *)glass1;
	sp++;

	sph[sp]->r = 0.6f;
	sph[sp]->o = (float3){-3.2f, 1.5f, 5.2f};
	sph[sp]->material = (Material *)phong1;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.6f;
	sph[sp]->o = (float3){-2.0f, 1.5f, 6.0f};
	sph[sp]->material = (Material *)phong2;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.6f;
	sph[sp]->o = (float3){-1.0f, 1.5f, 7.0f};
	sph[sp]->material = (Material *)phong3;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.5f;
	sph[sp]->o = (float3){0.3f, 1.5f, 6.5f};
	sph[sp]->material = (Material *)lambert3;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.4f;
	sph[sp]->o = (float3){-1.0f, 1.5f, 5.8f};
	sph[sp]->material = (Material *)lamp1;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.35f;
	sph[sp]->o = (float3){1.5f, 1.5f, 4.5f};
	sph[sp]->material = (Material *)glass1;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.4f;
	sph[sp]->o = (float3){2.7f, 1.5f, 5.0f};
	sph[sp]->material = (Material *)glass1;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.5f;
	sph[sp]->o = (float3){0.4f, 1.5f, 5.2f};
	sph[sp]->material = (Material *)mirror1;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.8f;
	sph[sp]->o = (float3){1.5f, 0.0f, 7.0f};
	sph[sp]->material = (Material *)lambert2;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	sph[sp]->r = 0.4f;
	sph[sp]->o = (float3){0.3f, 1.5f, 4.0f};
	sph[sp]->material = (Material *)phong2;
	place_on_surf(sph[sp], sph[0]);
	sp++;

	assert(sp == nsph);
	Shape **shapes = xmalloc(sizeof(*shapes) * nsph);
	for (size_t i = 0; i < nsph; ++i)
		shapes[i] = &sph[i]->base;
	shapes[0] = BvhBuild(shapes, nsph);

	Scene scene = {.shapes = shapes, .nshapes = 1, .camera = cam, .sky_color = sky_color};
	return scene;
}
