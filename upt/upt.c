#include "upt/light.h"
#include "upt/render.h"
#include "upt/scene.h"
#include "upt/scenes/builtin.h"
#include "uvideomem/uvideomem.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Options {
	bool gui;
	unsigned width, height, warmup, threads;
	unsigned long long frames;
	double seconds;
	char const *scene, *output;
} Options;

static bool read_options(int argc, char **argv, Options *options)
{
	char const *program = argv[0];
	*options = (Options){
	    .width = UVIDEOMEM_W, .height = UVIDEOMEM_H, .threads = 1, .scene = "original"};
	while (argc > 1 && !strncmp(argv[1], "--", 2)) {
		if (!strcmp(argv[1], "--gui")) {
			options->gui = true;
			--argc;
			++argv;
			continue;
		}
		if (!strcmp(argv[1], "--seconds")) {
			if (argc < 3)
				goto usage;
			char *end;
			errno = 0;
			options->seconds = strtod(argv[2], &end);
			if (errno || !argv[2][0] || *end || !isfinite(options->seconds) ||
			    options->seconds <= 0)
				goto usage;
			argc -= 2;
			argv += 2;
			continue;
		}
		bool threads = !strcmp(argv[1], "--threads");
		if (argc < 3 || (!threads && strcmp(argv[1], "--warmup")))
			goto usage;
		char *end;
		errno = 0;
		unsigned long n = strtoul(argv[2], &end, 10);
		if (errno || !argv[2][0] || *end || n > (threads ? 1024 : 100000) ||
		    argv[2][0] == '-')
			goto usage;
		if (threads)
			options->threads = n;
		else
			options->warmup = n;
		argc -= 2;
		argv += 2;
	}
#ifdef UPT_WITH_GUI
	if (argc == 1 && !options->seconds)
		options->gui = true;
#endif
	if (options->gui) {
		if (argc > 2 || options->seconds)
			goto usage;
		if (argc == 2)
			options->scene = argv[1];
		return true;
	}
	char *end;
	if (!options->seconds) {
		if (argc < 2)
			goto usage;
		errno = 0;
		options->frames = strtoull(argv[1], &end, 10);
		if (errno == ERANGE || argv[1][0] < '0' || argv[1][0] > '9' || *end ||
		    !options->frames || options->frames > ULLONG_MAX - options->warmup) {
			fprintf(stderr, "Frame count must be a positive integer.\n");
			return false;
		}
		--argc;
		++argv;
	}
	if (argc > 5 || argc == 3)
		goto usage;
	if (argc >= 2)
		options->scene = argv[1];
	if (argc >= 4) {
		unsigned *dimensions[] = {&options->width, &options->height};
		for (int i = 0; i < 2; ++i) {
			errno = 0;
			unsigned long v = strtoul(argv[2 + i], &end, 10);
			if (errno || *end || !v || v > 16384 || argv[2 + i][0] == '-')
				goto usage;
			*dimensions[i] = v;
		}
	}
	if (argc == 5)
		options->output = argv[4];
	return true;
usage:
	fprintf(stderr,
		"Usage: %s [--threads N] [--warmup N] N [scene.pbrt|original|atelier [width height "
		"[image.ppm]]]\n"
		"       %s [--threads N] --seconds S [scene [width height [image.ppm]]]\n"
		"       %s [--threads N] --gui [scene.pbrt|original|atelier]\n"
		"Seconds: wall-time budget including scene loading; partial passes are saved.\n"
		"Threads: 0 uses all online CPUs; the default is 1.\n",
		program, program, program);
	return false;
}

int main(int argc, char **argv)
{
	Options options;
	if (!read_options(argc, argv, &options))
		return EXIT_FAILURE;
#ifndef UPT_WITH_GUI
	if (options.gui) {
		fprintf(stderr, "GUI is disabled; rebuild with -DUPT_WITH_GUI=ON.\n");
		return EXIT_FAILURE;
	}
#endif
	double deadline = options.seconds ? MonotonicSeconds() + options.seconds : 0;
	Scene scene = !strcmp(options.scene, "original")  ? SceneOriginal_init()
		      : !strcmp(options.scene, "atelier") ? SceneAtelier_init()
							  : SceneRead(options.scene);
	LightsBuild(&scene);
	int result;
#ifdef UPT_WITH_GUI
	if (options.gui)
		result = RenderGui(&scene, options.threads);
	else
#endif
		result = RenderHeadless(&scene, options.width, options.height, options.frames,
					options.warmup, options.threads, deadline, options.output);
	SceneFree(&scene);
	return result;
}
