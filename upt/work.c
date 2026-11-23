#include "upt/work.h"
#include "upt/util.h"
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

struct Workers {
	pthread_barrier_t barrier;
	unsigned nthreads, count;
	atomic_uint next;
	atomic_bool stopped;
	bool (*run)(void *, unsigned);
	void *context;
	pthread_t threads[];
};

static void run(Workers *workers)
{
	unsigned i;
	while ((i = atomic_fetch_add_explicit(&workers->next, 1, memory_order_relaxed)) <
	       workers->count) {
		if (!workers->run(workers->context, i)) {
			atomic_store_explicit(&workers->stopped, true, memory_order_relaxed);
			atomic_store_explicit(&workers->next, workers->count, memory_order_relaxed);
			break;
		}
	}
}

static void *worker(void *context)
{
	Workers *workers = context;
	for (;;) {
		pthread_barrier_wait(&workers->barrier);
		if (!workers->count)
			return NULL;
		run(workers);
		pthread_barrier_wait(&workers->barrier);
	}
}

Workers *WorkersInit(unsigned threads, unsigned limit)
{
	if (!threads) {
		long online = sysconf(_SC_NPROCESSORS_ONLN);
		threads = online > 0 ? online : 1;
	}
	if (threads > limit)
		threads = limit;
	if (threads <= 1)
		return NULL;
	Workers *workers = xcalloc(1, sizeof(*workers) + (threads - 1) * sizeof(pthread_t));
	workers->nthreads = threads - 1;
	atomic_init(&workers->next, 0);
	atomic_init(&workers->stopped, false);
	int error = pthread_barrier_init(&workers->barrier, NULL, threads);
	for (unsigned i = 0; !error && i < workers->nthreads; ++i)
		error = pthread_create(&workers->threads[i], NULL, worker, workers);
	if (error) {
		fprintf(stderr, "workers: %s\n", strerror(error));
		exit(EXIT_FAILURE);
	}
	return workers;
}

bool WorkersRun(Workers *workers, unsigned count, bool (*run_row)(void *, unsigned), void *context)
{
	if (!count)
		return true;
	if (!workers) {
		for (unsigned i = 0; i < count; ++i)
			if (!run_row(context, i))
				return false;
		return true;
	}
	workers->count = count;
	workers->run = run_row;
	workers->context = context;
	atomic_store_explicit(&workers->next, 0, memory_order_relaxed);
	atomic_store_explicit(&workers->stopped, false, memory_order_relaxed);
	pthread_barrier_wait(&workers->barrier);
	run(workers);
	pthread_barrier_wait(&workers->barrier);
	return !atomic_load_explicit(&workers->stopped, memory_order_relaxed);
}

void WorkersFree(Workers *workers)
{
	if (!workers)
		return;
	workers->count = 0;
	pthread_barrier_wait(&workers->barrier);
	for (unsigned i = 0; i < workers->nthreads; ++i)
		pthread_join(workers->threads[i], NULL);
	pthread_barrier_destroy(&workers->barrier);
	free(workers);
}
