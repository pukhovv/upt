#include "upt/util.h"
#include <time.h>

_Thread_local Sampler upt_sampler;

double MonotonicSeconds(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return now.tv_sec + now.tv_nsec / 1e9;
}
