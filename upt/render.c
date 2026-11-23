#include "upt/render.h"
#include "upt/trace.h"
#include "upt/work.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double monotonic_seconds(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return now.tv_sec + now.tv_nsec / 1e9;
}

static inline float display_channel(float radiance)
{
	float x = fmaxf(0, radiance);
	// Narkowicz's filmic fit:
	// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
	x = fminf(1, (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
	return x <= 0.0031308f ? 12.92f * x : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

static inline uint32_t display_pixel(float3 radiance)
{
	return float3_topixel((float3){display_channel(radiance.x), display_channel(radiance.y),
				       display_channel(radiance.z)});
}

typedef struct RenderTask {
	Scene *scene;
	uint32_t *pixels;
	float3 *mean;
	unsigned width, height;
	unsigned long long frame;
	bool (*present)(uint32_t const *, unsigned, unsigned long long);
} RenderTask;

static bool render_row(void *context, unsigned y)
{
	RenderTask *task = context;
	Scene *scene = task->scene;
	uint32_t *vmem = task->pixels;
	float3 *mean = task->mean;
	unsigned width = task->width, height = task->height;
	unsigned long long frame = task->frame;
	int const nsamples = 8;
	float weight = 1.0f / (float)(frame + 1);
	for (unsigned int x = 0; x < width; ++x) {
		float3 rayclr = {0};
		for (int i = 0; i < nsamples; ++i) {
			SampleBegin((uint32_t)((size_t)y * width + x), frame, i);
			float fx = ((float)x + rand_uniform()) / width - 0.5f;
			float fy = ((float)y + rand_uniform()) / height - 0.5f;
			Ray ray = CameraCast(&scene->camera, fx, fy);
			float3 tmpclr = SceneTrace(scene, ray);
			rayclr = float3_add(rayclr, tmpclr);
		}
		rayclr = float3_scale(rayclr, 1.0f / nsamples);
		size_t pixel = (size_t)width * y + x;
		mean[pixel] =
		    float3_add(mean[pixel], float3_scale(float3_sub(rayclr, mean[pixel]), weight));
		vmem[pixel] = display_pixel(mean[pixel]);
	}
	return !task->present || task->present(vmem + (size_t)y * width, y, frame);
}

__attribute__((noinline)) double
Render(Scene *scene, uint32_t *vmem, unsigned width, unsigned height, unsigned long long nframes,
       unsigned warmup, unsigned threads,
       bool (*present)(uint32_t const *, unsigned, unsigned long long))
{
	float ratio = (float)width / height;
	scene->camera.axis_x = float3_scale(scene->camera.axis_x, ratio / scene->camera.ratio);
	scene->camera.ratio = ratio;
	scene->camera.spread =
	    sqrtf(float3_dot(scene->camera.axis_y, scene->camera.axis_y)) / height;
	float3 *mean = xcalloc((size_t)width * height, sizeof(*mean));
	RenderTask task = {.scene = scene,
			   .pixels = vmem,
			   .mean = mean,
			   .width = width,
			   .height = height,
			   .present = present};
	Workers *workers = WorkersInit(threads, height);
	double start = 0;
	for (unsigned long long frame = 0; !nframes || frame < nframes + warmup; ++frame) {
		if (frame == warmup)
			start = monotonic_seconds();
		task.frame = frame;
		if (!WorkersRun(workers, height, render_row, &task))
			break;
	}
	double elapsed = monotonic_seconds() - start;
	WorkersFree(workers);
	free(mean);
	return elapsed;
}

static bool write_image(char const *path, uint32_t const *pixels, unsigned width, unsigned height)
{
	FILE *image = fopen(path, "wb");
	if (!image) {
		perror(path);
		return false;
	}
	fprintf(image, "P6\n%u %u\n255\n", width, height);
	for (size_t i = 0; i < (size_t)width * height; ++i) {
		float3 c = float3_frompixel(pixels[i]);
		unsigned char rgb[] = {c.x * 255 + .5f, c.y * 255 + .5f, c.z * 255 + .5f};
		fwrite(rgb, 1, 3, image);
	}
	bool ok = !ferror(image);
	if (fclose(image))
		ok = false;
	if (!ok)
		perror(path);
	return ok;
}

int RenderHeadless(Scene *scene, unsigned width, unsigned height, unsigned long long frames,
		   unsigned warmup, unsigned threads, char const *output)
{
	uint32_t *pixels = xcalloc((size_t)width * height, sizeof(*pixels));
	double elapsed = Render(scene, pixels, width, height, frames, warmup, threads, NULL);
	int result = EXIT_FAILURE;
	if (output && !write_image(output, pixels, width, height))
		goto done;
	uint32_t checksum = 2166136261u;
	unsigned char const *bytes = (unsigned char const *)pixels;
	for (size_t i = 0; i < (size_t)width * height * sizeof(*pixels); ++i)
		checksum = (checksum ^ bytes[i]) * 16777619u;
	printf("Rendered %llu frames in %.6f seconds (%.3f FPS), checksum %08" PRIx32 "\n", frames,
	       elapsed, elapsed > 0.0 ? frames / elapsed : 0.0, checksum);
	result = EXIT_SUCCESS;
done:
	free(pixels);
	return result;
}
