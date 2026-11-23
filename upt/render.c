#include "upt/render.h"
#include "upt/trace.h"
#include "uvideomem/uvideomem.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
	atomic_uint next;
	atomic_bool stopped;
	bool *ready;
	pthread_mutex_t mutex;
	pthread_cond_t condition;
} RenderTask;

static void render_row(RenderTask *task, unsigned y)
{
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
}

static void *render_worker(void *context)
{
	RenderTask *task = context;
	while (!atomic_load(&task->stopped)) {
		unsigned y = atomic_fetch_add(&task->next, 1);
		if (y >= task->height)
			break;
		render_row(task, y);
		if (task->ready) {
			pthread_mutex_lock(&task->mutex);
			task->ready[y] = true;
			pthread_cond_signal(&task->condition);
			pthread_mutex_unlock(&task->mutex);
		}
	}
	return NULL;
}

__attribute__((noinline)) static double render(Scene *scene, uint32_t *vmem, unsigned width,
					       unsigned height, unsigned long long nframes,
					       unsigned warmup, unsigned threads)
{
	if (!threads) {
		long online = sysconf(_SC_NPROCESSORS_ONLN);
		threads = online > 0 ? online : 1;
	}
	if (threads > height)
		threads = height;
	float ratio = (float)width / height;
	scene->camera.axis_x = float3_scale(scene->camera.axis_x, ratio / scene->camera.ratio);
	scene->camera.ratio = ratio;
	float3 *mean = xcalloc((size_t)width * height, sizeof(*mean));
	RenderTask task = {.scene = scene,
			   .pixels = vmem,
			   .mean = mean,
			   .width = width,
			   .height = height,
			   .mutex = PTHREAD_MUTEX_INITIALIZER,
			   .condition = PTHREAD_COND_INITIALIZER};
	atomic_init(&task.next, 0);
	atomic_init(&task.stopped, false);
	pthread_t *workers = threads > 1 ? xmalloc(threads * sizeof(*workers)) : NULL;
#ifdef UPT_WITH_GUI
	if (!nframes && threads > 1)
		task.ready = xcalloc(height, sizeof(*task.ready));
#endif
	double start = 0, elapsed;
	scene->camera.spread =
	    sqrtf(float3_dot(scene->camera.axis_y, scene->camera.axis_y)) / height;
	for (unsigned long long frame = 0; !nframes || frame < nframes + warmup; ++frame) {
		if (frame == warmup)
			start = monotonic_seconds();
		task.frame = frame;
		if (threads == 1) {
			for (unsigned y = 0; y < height; ++y) {
				render_row(&task, y);
#ifdef UPT_WITH_GUI
				if (!nframes &&
				    !uvideomem_present(vmem + (size_t)y * width, y, frame))
					goto done;
#endif
			}
			continue;
		}
		atomic_store(&task.next, 0);
		if (task.ready)
			memset(task.ready, 0, height * sizeof(*task.ready));
		for (unsigned i = 0; i < threads; ++i) {
			int error = pthread_create(&workers[i], NULL, render_worker, &task);
			if (error) {
				fprintf(stderr, "pthread_create: %s\n", strerror(error));
				exit(EXIT_FAILURE);
			}
		}
#ifdef UPT_WITH_GUI
		if (!nframes) {
			for (unsigned y = 0; y < height; ++y) {
				pthread_mutex_lock(&task.mutex);
				while (!task.ready[y])
					pthread_cond_wait(&task.condition, &task.mutex);
				pthread_mutex_unlock(&task.mutex);
				if (!uvideomem_present(vmem + (size_t)y * width, y, frame)) {
					atomic_store(&task.stopped, true);
					break;
				}
			}
		}
#endif
		for (unsigned i = 0; i < threads; ++i)
			pthread_join(workers[i], NULL);
		if (atomic_load(&task.stopped))
			break;
	}
#ifdef UPT_WITH_GUI
done:
#endif
	elapsed = monotonic_seconds() - start;
	pthread_cond_destroy(&task.condition);
	pthread_mutex_destroy(&task.mutex);
	free(task.ready);
	free(workers);
	free(mean);
	return elapsed;
}

#ifdef UPT_WITH_GUI
typedef struct GuiRender {
	Scene *scene;
	unsigned threads;
} GuiRender;

static void render_gui(void *context, unsigned int width, unsigned int height)
{
	GuiRender *task = context;
	Scene *scene = task->scene;
	uint32_t *pixels = xcalloc((size_t)width * height, sizeof(*pixels));
	render(scene, pixels, width, height, 0, 0, task->threads);
	free(pixels);
}

int RenderGui(Scene *scene, unsigned threads)
{
	GuiRender task = {.scene = scene, .threads = threads};
	return uvideomem_init(render_gui, &task);
}
#endif

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
	double elapsed = render(scene, pixels, width, height, frames, warmup, threads);
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
