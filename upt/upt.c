#include "upt/float3.h"
#include "uvideomem/uvideomem.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "upt/ray_core.h"
#include "upt/shapes.h"
#include "upt/bvh.h"
#include "upt/surfaces.h"
#include "upt/scenes.h"

struct Scene Scene1_init()
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
	for (size_t i = 0; i < nsph; ++i) {
		shapes[i]->Bounds = SphereBounds;
	}
	shapes[0] = BvhBuild(shapes, nsph);

	Scene scene = {.shapes = shapes, .nshapes = 1, .camera = cam, .sky_color = sky_color};
	return scene;
}

static inline float display_channel(float radiance)
{
	float x = fmaxf(0, radiance);
	// Narkowicz's filmic fit: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
	x = fminf(1, (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
	return x <= 0.0031308f ? 12.92f * x : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

static inline uint32_t display_pixel(float3 radiance)
{
	return float3_topixel((float3){display_channel(radiance.x),
				     display_channel(radiance.y),
				     display_channel(radiance.z)});
}

static inline void PTSceneTrace(Scene *scene, Ray const *ray, float3 *color)
{
	float3 rayclr = {1.0f, 1.0f, 1.0f};
	HitRecord hit;
	hit.ray = *ray;
	bool cast = true;

	for (unsigned int depth = 0; cast; ++depth) {
		if (!ShapeListHit(scene->shapes, scene->nshapes, &hit)) {
			rayclr = float3_mul(rayclr, scene->sky_color);
			break;
		}
		cast = hit.material->Apply(hit.material, &hit, &rayclr);
		if (cast) {
			hit.ray = RaySpawn(hit.ray.o, hit.sn, hit.ray.d);
			if (depth >= 8) {
				float survival = fminf(0.95f, fmaxf(rayclr.x, fmaxf(rayclr.y, rayclr.z)));
				if (rand_uniform() >= survival) {
					rayclr = (float3){0};
					break;
				}
				rayclr = float3_scale(rayclr, 1 / survival);
			}
		}
	}
	*color = rayclr;
}

__attribute__((noinline)) static void upt_render(Scene *scene, uint32_t *vmem,
					      unsigned int width, unsigned int height,
					      unsigned long long nframes)
{
	int const nsamples = 8;
	float3 *mean = calloc((size_t)width * height, sizeof(*mean));
	if (!mean) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	for (unsigned long long frame = 0; !nframes || frame < nframes; ++frame) {
		float weight = 1.0f / (float)(frame + 1);
		for (unsigned int y = 0; y < height; ++y) {
			for (unsigned int x = 0; x < width; ++x) {
				float3 rayclr = {0};
				for (int i = 0; i < nsamples; ++i) {
					SampleBegin((uint32_t)((size_t)y * width + x), frame, i);
					float fx =
					    ((float)x + rand_uniform()) / width -
					    0.5f;
					float fy = ((float)y + rand_uniform()) /
						       height -
						   0.5f;
					Ray ray = CameraCast(&scene->camera, fx, fy);
					float3 tmpclr;
					PTSceneTrace(scene, &ray, &tmpclr);
					rayclr = float3_add(rayclr, tmpclr);
				}
				rayclr = float3_scale(rayclr, 1.0f / nsamples);
				size_t pixel = (size_t)width * y + x;
				mean[pixel] = float3_add(mean[pixel],
					float3_scale(float3_sub(rayclr, mean[pixel]), weight));
				vmem[pixel] = display_pixel(mean[pixel]);
			}
#ifdef UPT_WITH_GUI
			if (!nframes && !uvideomem_present(vmem + (size_t)y * width, y, frame))
				goto done;
#endif
		}
	}
#ifdef UPT_WITH_GUI
done:
#endif
	free(mean);
}

#ifdef UPT_WITH_GUI
static void upt_main(void *context, unsigned int width, unsigned int height)
{
	Scene *scene = context;
	float ratio = (float)width / height;
	scene->camera.axis_x = float3_scale(scene->camera.axis_x, ratio / scene->camera.ratio);
	scene->camera.ratio = ratio;
	uint32_t *pixels = calloc((size_t)width * height, sizeof(*pixels));
	if (!pixels) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	upt_render(scene, pixels, width, height, 0);
	free(pixels);
}
#endif

static double monotonic_seconds(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return now.tv_sec + now.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	bool gui = argc > 1 && !strcmp(argv[1], "--gui");
#ifdef UPT_WITH_GUI
	if (argc == 1) gui = true;
#endif
	if ((argc != 2 && argc != 3 && !(argc == 1 && gui)) ||
	    (argc == 3 && strcmp(argv[2], "original") && strcmp(argv[2], "atelier"))) {
		fprintf(stderr, "Usage: %s N [original|atelier]\n       %s --gui [original|atelier]\n",
			argv[0], argv[0]);
		return EXIT_FAILURE;
	}
	if (gui) {
#ifdef UPT_WITH_GUI
		Scene scene = argc == 3 && !strcmp(argv[2], "atelier") ? SceneAtelier_init() : Scene1_init();
		int result = uvideomem_init(upt_main, &scene);
		BvhFree(scene.shapes[0]);
		free(scene.shapes);
		return result;
#else
		fprintf(stderr, "GUI is disabled; rebuild with -DUPT_WITH_GUI=ON.\n");
		return EXIT_FAILURE;
#endif
	}
	char *end;
	errno = 0;
	unsigned long long nframes = strtoull(argv[1], &end, 10);
	if (errno == ERANGE || argv[1][0] < '0' || argv[1][0] > '9' || *end || !nframes) {
		fprintf(stderr, "Frame count must be a positive integer.\n");
		return EXIT_FAILURE;
	}
	uint32_t *vmem = calloc(UVIDEOMEM_W * UVIDEOMEM_H, sizeof(*vmem));
	if (!vmem) {
		perror("calloc");
		return EXIT_FAILURE;
	}
	double start = monotonic_seconds();
	Scene scene = argc == 3 && !strcmp(argv[2], "atelier") ? SceneAtelier_init() : Scene1_init();
	upt_render(&scene, vmem, UVIDEOMEM_W, UVIDEOMEM_H, nframes);
	double elapsed = monotonic_seconds() - start;
	uint32_t checksum = 2166136261u;
	unsigned char const *pixels = (unsigned char const *)vmem;
	for (size_t i = 0; i < UVIDEOMEM_W * UVIDEOMEM_H * sizeof(*vmem); ++i)
		checksum = (checksum ^ pixels[i]) * 16777619u;
	printf("Rendered %llu frames in %.6f seconds (%.3f FPS), checksum %08" PRIx32 "\n", nframes,
	       elapsed, elapsed > 0.0 ? nframes / elapsed : 0.0, checksum);
	BvhFree(scene.shapes[0]);
	free(scene.shapes);
	free(vmem);
	return EXIT_SUCCESS;
}
