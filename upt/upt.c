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
	unsigned width, height, warmup;
	unsigned long long frames;
	char const *scene, *output;
} Options;

static bool read_options(int argc, char **argv, Options *options)
{
	char const *program = argv[0];
	*options = (Options){.width = UVIDEOMEM_W, .height = UVIDEOMEM_H, .scene = "original"};
	if (argc > 3 && !strcmp(argv[1], "--warmup")) {
		char *end;
		errno = 0;
		unsigned long n = strtoul(argv[2], &end, 10);
		if (errno || !argv[2][0] || *end || n > 100000 || argv[2][0] == '-')
			goto usage;
		options->warmup = n;
		argc -= 2;
		argv += 2;
	}
	options->gui = argc > 1 && !strcmp(argv[1], "--gui");
#ifdef UPT_WITH_GUI
	if (argc == 1)
		options->gui = true;
#endif
	if ((argc < 2 && !(argc == 1 && options->gui)) || argc > 6 || argc == 4 ||
	    (options->gui && argc > 3))
		goto usage;
	if (argc >= 3)
		options->scene = argv[2];
	if (options->gui)
		return true;
	char *end;
	errno = 0;
	options->frames = strtoull(argv[1], &end, 10);
	if (errno == ERANGE || argv[1][0] < '0' || argv[1][0] > '9' || *end || !options->frames ||
	    options->frames > ULLONG_MAX - options->warmup) {
		fprintf(stderr, "Frame count must be a positive integer.\n");
		return false;
	}
	if (argc >= 5) {
		unsigned *dimensions[] = {&options->width, &options->height};
		for (int i = 0; i < 2; ++i) {
			errno = 0;
			unsigned long v = strtoul(argv[3 + i], &end, 10);
			if (errno || *end || !v || v > 16384 || argv[3 + i][0] == '-')
				goto usage;
			*dimensions[i] = v;
		}
	}
	if (argc == 6)
		options->output = argv[5];
	return true;
usage:
	fprintf(
	    stderr,
	    "Usage: %s [--warmup N] N [scene.pbrt|original|atelier [width height [image.ppm]]]\n"
	    "       %s --gui [scene.pbrt|original|atelier]\n",
	    program, program);
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
	Scene scene = !strcmp(options.scene, "original")  ? SceneOriginal_init()
		      : !strcmp(options.scene, "atelier") ? SceneAtelier_init()
							  : SceneRead(options.scene);
	LightsBuild(&scene);
	int result;
#ifdef UPT_WITH_GUI
	if (options.gui)
		result = RenderGui(&scene);
	else
#endif
		result = RenderHeadless(&scene, options.width, options.height, options.frames,
					options.warmup, options.output);
	SceneFree(&scene);
	return result;
}
