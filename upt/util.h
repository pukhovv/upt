#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Sampler {
	uint32_t key, dimension, index;
} Sampler;
extern _Thread_local Sampler upt_sampler __attribute__((tls_model("local-exec")));

static inline uint32_t sample_hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	return x ^ (x >> 16);
}

static inline void SampleBegin(uint32_t pixel, uint64_t pass, unsigned int index)
{
	upt_sampler = (Sampler){.key = sample_hash(pixel ^ sample_hash((uint32_t)pass) ^
						   sample_hash((uint32_t)(pass >> 32)) ^ 1u),
				.index = index};
}

static inline float rand_uniform(void)
{
	uint32_t key = sample_hash(upt_sampler.key + upt_sampler.dimension++ * 0x9e3779b9u);
	unsigned int stratum = (upt_sampler.index * ((key >> 3) | 1) + key) & 7;
	// 21 jitter bits leave room for the three-bit stratum without rounding into its neighbour.
	float jitter =
	    (sample_hash(key ^ ((upt_sampler.index + 1) * 0x85ebca6bu)) >> 11) * 0x1p-21f;
	return (stratum + jitter) * 0.125f;
}

static inline void *xmalloc(size_t size)
{
	void *ptr = malloc(size);
	assert(ptr);
	return ptr;
}

static inline void *xcalloc(size_t count, size_t size)
{
	void *ptr = calloc(count, size);
	if (!ptr) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	return ptr;
}
