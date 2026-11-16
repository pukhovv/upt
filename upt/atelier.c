#include "upt/scenes.h"
#include "upt/bvh.h"
#include "upt/surfaces.h"
#include "uvideomem/uvideomem.h"

static Sphere *sphere(float3 center, float radius, Material *material)
{
	Sphere *s = ALLOC_SHAPE(Sphere);
	s->base.Bounds = SphereBounds;
	s->o = center;
	s->r = radius;
	s->material = material;
	return s;
}

static Shape *standing(Material *material, float x, float z, float radius)
{
	Sphere *s = sphere((float3){x, radius + 0.0002f, z}, radius, material);
	return &s->base;
}

Scene SceneAtelier_init(void)
{
	Coated *slate = ALLOC_MATERIAL(Coated);
	slate->color = (float3){0.30f, 0.34f, 0.40f};
	Lambert *ivory = ALLOC_MATERIAL(Lambert);
	ivory->color = (float3){0.85f, 0.81f, 0.69f};
	Lambert *red = ALLOC_MATERIAL(Lambert);
	red->color = (float3){0.85f, 0.17f, 0.085f};
	Phong *copper = ALLOC_MATERIAL(Phong);
	copper->color = (float3){0.95f, 0.58f, 0.33f};
	copper->pexp = 160;
	Phong *petrol = ALLOC_MATERIAL(Phong);
	petrol->color = (float3){0.24f, 0.72f, 0.73f};
	petrol->pexp = 24;
	Phong *rough = ALLOC_MATERIAL(Phong);
	rough->color = petrol->color;
	rough->pexp = 3;
	Phong *polished = ALLOC_MATERIAL(Phong);
	polished->color = petrol->color;
	polished->pexp = 128;
	Mirror *silver = ALLOC_MATERIAL(Mirror);
	silver->color = (float3){0.95f, 0.97f, 0.99f};
	Glass *glass = ALLOC_MATERIAL(Glass);
	glass->transmittance = (float3){0.96f, 0.99f, 1.0f};
	Lamp *warm = ALLOC_MATERIAL(Lamp);
	warm->color = (float3){1.5f, 1.17f, 0.60f};
	Lamp *white = ALLOC_MATERIAL(Lamp);
	white->color = (float3){1.44f, 1.455f, 1.5f};
	Lamp *cyan = ALLOC_MATERIAL(Lamp);
	cyan->color = (float3){0.42f, 1.17f, 1.5f};
	Checker *check = ALLOC_MATERIAL(Checker);
	check->a = (float3){0.055f, 0.065f, 0.08f};
	check->b = (float3){0.92f, 0.87f, 0.73f};
	check->scale = 2.0f;
	Lambert *plum = ALLOC_MATERIAL(Lambert);
	plum->color = (float3){0.45f, 0.12f, 0.28f};
	Lambert *teal = ALLOC_MATERIAL(Lambert);
	teal->color = (float3){0.12f, 0.34f, 0.38f};
	Lambert *chalk = ALLOC_MATERIAL(Lambert);
	chalk->color = (float3){0.60f, 0.62f, 0.57f};
	Mirror *gold = ALLOC_MATERIAL(Mirror);
	gold->color = (float3){1.0f, 0.76f, 0.32f};
	Coated *jade = ALLOC_MATERIAL(Coated);
	jade->color = (float3){0.035f, 0.24f, 0.16f};
	Coated *wine = ALLOC_MATERIAL(Coated);
	wine->color = (float3){0.48f, 0.025f, 0.065f};
	Coated *porcelain = ALLOC_MATERIAL(Coated);
	porcelain->color = (float3){0.90f, 0.85f, 0.72f};
	Marble *stone = ALLOC_MATERIAL(Marble);
	stone->a = (float3){0.86f, 0.88f, 0.79f};
	stone->b = (float3){0.055f, 0.085f, 0.11f};
	stone->scale = 7;
	Marble *rose = ALLOC_MATERIAL(Marble);
	rose->a = (float3){0.72f, 0.28f, 0.23f};
	rose->b = (float3){0.95f, 0.80f, 0.60f};
	rose->scale = 5;
	Glass *amber = ALLOC_MATERIAL(Glass);
	amber->transmittance = (float3){0.99f, 0.80f, 0.42f};

	enum { ARC = 72, ORBIT = 128, COUNT = ARC + ORBIT + 51 };
	Shape *shapes[COUNT];
	size_t n = 0;
	Plane *floor = ALLOC_SHAPE(Plane);
	floor->material = (Material *)slate;
	floor->normal = (float3){0, 1, 0};
	floor->offset = 0;
	shapes[n++] = &floor->base;
	shapes[n++] = standing((Material *)silver, 4.0f, 11.0f, 3.4f);
	shapes[n++] = standing((Material *)copper, -5.0f, 10.0f, 2.3f);
	shapes[n++] = standing((Material *)glass, 1.4f, 1.0f, 1.6f);
	shapes[n++] = standing((Material *)check, -4.5f, 0.0f, 1.35f);
	shapes[n++] = &sphere((float3){-7.8f, 4.7f, 4.6f}, 1.25f, (Material *)warm)->base;
	shapes[n++] = &sphere((float3){2.5f, 8.2f, 11.0f}, 1.1f, (Material *)white)->base;
	shapes[n++] = standing((Material *)cyan, -7.0f, 14.0f, 0.7f);
	shapes[n++] = standing((Material *)wine, -0.9f, -2.8f, 0.7f);
	shapes[n++] = standing((Material *)gold, 5.0f, -1.0f, 0.75f);
	shapes[n++] = standing((Material *)amber, 6.6f, 0.5f, 0.55f);
	shapes[n++] = standing((Material *)jade, -7.0f, -2.0f, 0.55f);
	shapes[n++] = standing((Material *)check, 0.6f, 3.2f, 0.7f);
	shapes[n++] = standing((Material *)stone, -2.5f, 4.5f, 0.85f);
	shapes[n++] = standing((Material *)rose, 0.1f, 5.2f, 0.65f);

	Material *satellites[] = {(Material *)porcelain, (Material *)jade, (Material *)amber,
				 (Material *)stone, (Material *)gold, (Material *)wine};
	for (int group = 0; group < 2; ++group) {
		float x = group ? 4.0f : -5.0f;
		float z = group ? 11.0f : 10.0f;
		float radius = group ? 3.4f : 2.3f;
		for (int i = 0; i < 12; ++i) {
			float t = 2.0f * M_PI * (i + 0.17f * group) / 12;
			float r = 0.26f + 0.045f * ((i * 5 + group) % 8);
			float distance = 2 * sqrtf(radius * r) + 0.2f;
			shapes[n++] = standing(satellites[(i + group) % 6],
					       x + distance * cosf(t), z + distance * sinf(t), r);
		}
	}

	// A ground-level crescent makes contacts and small silhouettes easy to inspect.
	for (int i = 0; i < ARC; ++i) {
		float t = 3.29f + 2.69f * i / (ARC - 1);
		float r = 0.13f + 0.045f * (1.0f + sinf(2 * t));
		Material *m = i % 12 == 0 ? (Material *)warm :
			      i % 4 == 0 ? (Material *)gold :
			      i % 3 == 0 ? (Material *)jade : (Material *)porcelain;
		shapes[n++] = standing(m, 11 * cosf(t), 7 + 9 * sinf(t), r);
	}
	// This tilted orbit crosses the reflective and refractive silhouettes.
	for (int i = 0; i < ORBIT; ++i) {
		float t = 2.0f * M_PI * i / ORBIT;
		float r = 0.14f + 0.025f * cosf(3 * t);
		Material *m = i % 16 == 0 ? (Material *)cyan :
			      i % 8 == 0 ? (Material *)porcelain :
			      i % 4 == 0 ? (Material *)red : (Material *)copper;
		shapes[n++] = &sphere((float3){9 * cosf(t), 4.6f + 3.8f * sinf(t),
					    9 + 2.5f * sinf(t)}, r, m)->base;
	}

	Material *gloss[] = {(Material *)rough, (Material *)petrol, (Material *)polished};
	for (int i = 0; i < 3; ++i)
		shapes[n++] = standing(gloss[i], -2.4f + 0.95f * i, 3.0f, 0.38f);
	for (int i = 0; i < 5; ++i) {
		float r = 0.06f + 0.045f * i;
		shapes[n++] = standing(i & 1 ? (Material *)glass : (Material *)ivory,
				       2.3f + 0.5f * i, -2.4f, r);
	}

	shapes[n++] = &sphere((float3){-126, 14, 24}, 112, (Material *)plum)->base;
	shapes[n++] = &sphere((float3){109, 9, 30}, 100, (Material *)teal)->base;
	shapes[n++] = &sphere((float3){-4, 16, 170}, 140, (Material *)chalk)->base;
	shapes[n++] = &sphere((float3){-3, 24, 0}, 9, (Material *)white)->base;
	assert(n == COUNT);

	Shape **root = xmalloc(sizeof(*root));
	root[0] = BvhBuild(shapes, n);
	Camera camera;
	CameraSetScreen(&camera, 0.72f, (float)UVIDEOMEM_W / UVIDEOMEM_H);
	CameraLookAt(&camera, (float3){10, 14, -15}, (float3){0, 1.6f, 7},
		     (float3){0, -1, 0});
	return (Scene){.shapes = root, .nshapes = 1, .camera = camera,
		       .sky_color = {0.72f, 0.78f, 0.96f}};
}
