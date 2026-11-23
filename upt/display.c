#include "upt/render.h"
#include "uvideomem/uvideomem.h"

typedef struct GuiRender {
	Scene *scene;
	unsigned threads;
} GuiRender;

static void render_gui(void *context, unsigned width, unsigned height)
{
	GuiRender *task = context;
	uint32_t *pixels = xcalloc((size_t)width * height, sizeof(*pixels));
	Render(task->scene, pixels, width, height, 0, 0, task->threads, 0, uvideomem_present);
	free(pixels);
}

int RenderGui(Scene *scene, unsigned threads)
{
	GuiRender task = {.scene = scene, .threads = threads};
	return uvideomem_init(render_gui, &task);
}
