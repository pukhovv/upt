#include "upt/scene.h"
#include "upt/bvh.h"
#include "upt/environment.h"
#include "upt/light.h"
#include "upt/mesh.h"
#include "upt/surfaces.h"
#include "upt/texture.h"
#include "upt/transform.h"
#include "upt/volume.h"
#include "uvideomem/uvideomem.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <string.h>

typedef struct Resource {
	void *ptr;
	void (*destroy)(void *);
} Resource;

typedef struct Param {
	char key[64], text[256];
	double *values;
	size_t count;
	bool used;
} Param;

typedef struct Parser {
	FILE *file;
	char const *path;
	char token[256];
	bool quoted;
	unsigned line;
	Param params[16];
	size_t nparams;
	struct {
		char name[256];
		Texture *texture;
	} textures[32];
	size_t ntextures;
} Parser;

static _Noreturn void fail(Parser *p, char const *message)
{
	fprintf(stderr, "%s:%u: %s (%s)\n", p->path, p->line, message, p->token);
	exit(EXIT_FAILURE);
}

static void next(Parser *p)
{
	int c;
	do {
		c = fgetc(p->file);
		if (c == '#')
			while ((c = fgetc(p->file)) != EOF && c != '\n') {
			}
		if (c == '\n')
			++p->line;
	} while (c != EOF && isspace(c));
	p->quoted = c == '"';
	if (p->quoted)
		c = fgetc(p->file);
	size_t n = 0;
	while (c != EOF &&
	       (p->quoted ? c != '"' : !isspace(c) && c != '[' && c != ']' && c != '#')) {
		if (n + 1 == sizeof(p->token))
			fail(p, "token too long");
		p->token[n++] = c;
		c = fgetc(p->file);
	}
	if (p->quoted && c != '"')
		fail(p, "unterminated string");
	if (!p->quoted && !n && (c == '[' || c == ']'))
		p->token[n++] = c;
	else if (!p->quoted && c != EOF)
		ungetc(c, p->file);
	p->token[n] = 0;
}

static double number(Parser *p)
{
	char *end;
	errno = 0;
	double value = strtod(p->token, &end);
	if (!p->token[0] || p->quoted || *end || errno || !isfinite(value) || fabs(value) > FLT_MAX)
		fail(p, "expected finite number");
	next(p);
	return value;
}

static float3 vector(Parser *p)
{
	float x = number(p), y = number(p), z = number(p);
	return (float3){x, y, z};
}

static void params(Parser *p)
{
	p->nparams = 0;
	while (p->quoted) {
		if (p->nparams == 16 || strlen(p->token) >= sizeof(p->params[0].key))
			fail(p, "too many parameters or long parameter name");
		Param *a = &p->params[p->nparams++];
		*a = (Param){0};
		strcpy(a->key, p->token);
		for (size_t i = 0; i + 1 < p->nparams; ++i)
			if (!strcmp(p->params[i].key, a->key))
				fail(p, "duplicate parameter");
		next(p);
		bool array = !strcmp(p->token, "[");
		if (array)
			next(p);
		size_t capacity = 0;
		do {
			if (p->quoted) {
				if (a->count || a->text[0])
					fail(p, "expected one string");
				strcpy(a->text, p->token);
				next(p);
			} else {
				if (a->count == capacity) {
					capacity = capacity ? 2 * capacity : 16;
					a->values =
					    realloc(a->values, capacity * sizeof(*a->values));
					if (!a->values)
						fail(p, "out of memory");
				}
				a->values[a->count++] = number(p);
			}
		} while (array && strcmp(p->token, "]"));
		if (array)
			next(p);
	}
}

static Param *parameter(Parser *p, char const *key, size_t count)
{
	for (size_t i = 0; i < p->nparams; ++i) {
		Param *a = &p->params[i];
		if (!strcmp(a->key, key)) {
			if (a->used || (count && a->count != count))
				fail(p, "duplicate parameter or incorrect value count");
			a->used = true;
			return a;
		}
	}
	return NULL;
}

static float scalar(Parser *p, char const *key, float fallback)
{
	Param *a = parameter(p, key, 1);
	return a ? a->values[0] : fallback;
}

static float3 color(Parser *p, char const *key, float3 fallback)
{
	Param *a = parameter(p, key, 3);
	return a ? (float3){a->values[0], a->values[1], a->values[2]} : fallback;
}

static Texture *texture(Parser *p, char const *key)
{
	Param *a = parameter(p, key, 0);
	if (!a)
		return NULL;
	for (size_t i = 0; i < p->ntextures; ++i)
		if (!strcmp(p->textures[i].name, a->text))
			return p->textures[i].texture;
	fail(p, "undefined texture");
	return NULL;
}

static float3 texture_color(Parser *p, char const *name, float3 fallback)
{
	char key[64];
	snprintf(key, sizeof(key), "rgb %s", name);
	Param *rgb = parameter(p, key, 3);
	snprintf(key, sizeof(key), "float %s", name);
	Param *value = parameter(p, key, 1);
	if (rgb && value)
		fail(p, "conflicting texture values");
	return rgb     ? (float3){rgb->values[0], rgb->values[1], rgb->values[2]}
	       : value ? (float3){value->values[0], value->values[0], value->values[0]}
		       : fallback;
}

static Texture *texture_input(Parser *p, Scene *scene, char const *name, float3 fallback)
{
	char key[64];
	snprintf(key, sizeof(key), "texture %s", name);
	Texture *input = texture(p, key);
	if (input)
		return input;
	input = TextureConstant(texture_color(p, name, fallback));
	SceneOwn(scene, input, NULL);
	return input;
}

void SceneOwn(Scene *scene, void *ptr, void (*destroy)(void *))
{
	scene->resources = realloc(scene->resources, (scene->nresources + 1) * sizeof(Resource));
	if (!scene->resources) {
		perror("realloc");
		exit(EXIT_FAILURE);
	}
	scene->resources[scene->nresources++] = (Resource){ptr, destroy};
}

void SceneFree(Scene *scene)
{
	LightsFree(scene);
	BvhFree(scene->shapes[0]);
	free(scene->shapes);
	for (size_t i = scene->nresources; i > 0; --i) {
		Resource r = scene->resources[i - 1];
		if (r.destroy)
			r.destroy(r.ptr);
		else
			free(r.ptr);
	}
	free(scene->resources);
}

static bool moving(Motion motion)
{
	return motion.angle != 0 || motion.translation.x != 0 || motion.translation.y != 0 ||
	       motion.translation.z != 0;
}

static Shape *instance_create(Shape *child, Transform transform, Motion motion)
{
	if (moving(motion))
		return &MovingInstanceCreate(child, transform, motion)->instance.base;
	return &InstanceCreate(child, transform)->base;
}

static void add_shape(Scene *scene, Shape *shape, void (*destroy)(void *), Transform transform,
		      Motion motion)
{
	scene->shapes = realloc(scene->shapes, (scene->nshapes + 1) * sizeof(*scene->shapes));
	if (!scene->shapes)
		exit(EXIT_FAILURE);
	SceneOwn(scene, shape, destroy);
	Transform identity = TransformIdentity();
	if (moving(motion) || memcmp(&identity, &transform, sizeof(identity))) {
		Shape *instance = instance_create(shape, transform, motion);
		SceneOwn(scene, instance, NULL);
		shape = instance;
	}
	scene->shapes[scene->nshapes++] = shape;
}

static void free_tree(void *tree)
{
	BvhFree(tree);
}

static void filename(Parser *p, char const *name, char path[1024])
{
	char const *slash = strrchr(p->path, '/');
	int prefix = slash ? (int)(slash - p->path + 1) : 0;
	int n = name[0] == '/' ? snprintf(path, 1024, "%s", name)
			       : snprintf(path, 1024, "%.*s%s", prefix, p->path, name);
	if (n < 0 || n >= 1024)
		fail(p, "path too long");
}

Scene SceneRead(char const *path)
{
	Parser p = {.path = path, .line = 1, .file = fopen(path, "r")};
	if (!p.file) {
		perror(path);
		exit(EXIT_FAILURE);
	}
	Scene scene = {0};
	float3 eye = {0, 1, -5}, at = {0, 1, 0}, up = {0, 1, 0};
	CameraSetScreen(&scene.camera, M_PI / 4, (float)UVIDEOMEM_W / UVIDEOMEM_H);
	Lambert *initial = ALLOC_MATERIAL(Lambert);
	initial->color = (float3){0.5f, 0.5f, 0.5f};
	SceneOwn(&scene, initial, NULL);
	Material *material = &initial->base;
	typedef struct State {
		Material *material;
		Transform transform;
		Motion motion;
	} State;
	Transform transform = TransformIdentity();
	Motion motion = {0};
	State stack[32];
	struct {
		char name[256];
		Shape *shape;
	} objects[32];
	size_t nobjects = 0, object_begin = 0;
	bool defining = false;
	size_t object_depth = 0;
	size_t depth = 0;
	next(&p);
	while (p.token[0]) {
		char command[256], type[256];
		strcpy(command, p.token);
		next(&p);
		if (!strcmp(command, "WorldBegin") || !strcmp(command, "Identity")) {
			transform = TransformIdentity();
			motion = (Motion){0};
			continue;
		}
		if (!strcmp(command, "Translate") || !strcmp(command, "Scale") ||
		    !strcmp(command, "Rotate")) {
			Transform t = TransformIdentity();
			if (!strcmp(command, "Translate"))
				t.o = vector(&p);
			else if (!strcmp(command, "Scale")) {
				float3 v = vector(&p);
				t.x.x = v.x;
				t.y.y = v.y;
				t.z.z = v.z;
			} else {
				float angle = number(&p) * M_PI / 180;
				float3 axis = vector(&p);
				if (float3_dot(axis, axis) == 0)
					fail(&p, "zero rotation axis");
				axis = float3_norm(axis);
				t = TransformRotation(axis, angle);
			}
			transform = TransformCompose(transform, t);
			continue;
		}
		if (!strcmp(command, "ObjectBegin")) {
			if (defining || nobjects == 32 || depth == 32 || !p.quoted)
				fail(&p, "invalid ObjectBegin");
			for (size_t i = 0; i < nobjects; ++i)
				if (!strcmp(objects[i].name, p.token))
					fail(&p, "duplicate object");
			strcpy(objects[nobjects].name, p.token);
			next(&p);
			stack[depth++] = (State){material, transform, motion};
			object_depth = depth;
			object_begin = scene.nshapes;
			defining = true;
			continue;
		}
		if (!strcmp(command, "ObjectEnd")) {
			if (!defining || depth != object_depth || scene.nshapes == object_begin)
				fail(&p, "invalid ObjectEnd");
			Shape *tree =
			    BvhBuild(scene.shapes + object_begin, scene.nshapes - object_begin);
			SceneOwn(&scene, tree, free_tree);
			objects[nobjects++].shape = tree;
			scene.nshapes = object_begin;
			State state = stack[--depth];
			material = state.material;
			transform = state.transform;
			motion = state.motion;
			defining = false;
			continue;
		}
		if (!strcmp(command, "ObjectInstance")) {
			size_t i = 0;
			while (i < nobjects && strcmp(objects[i].name, p.token))
				++i;
			if (!p.quoted || i == nobjects)
				fail(&p, "undefined object");
			Shape *instance = instance_create(objects[i].shape, transform, motion);
			add_shape(&scene, instance, NULL, TransformIdentity(), (Motion){0});
			next(&p);
			continue;
		}
		if (!strcmp(command, "LookAt")) {
			eye = vector(&p);
			at = vector(&p);
			up = vector(&p);
			continue;
		}
		if (!strcmp(command, "AttributeBegin")) {
			if (depth == 32)
				fail(&p, "attribute stack overflow");
			stack[depth++] = (State){material, transform, motion};
			continue;
		}
		if (!strcmp(command, "AttributeEnd")) {
			if (!depth || (defining && depth == object_depth))
				fail(&p, "unmatched AttributeEnd");
			State state = stack[--depth];
			material = state.material;
			transform = state.transform;
			motion = state.motion;
			continue;
		}
		char texture_name[256] = "";
		if (!strcmp(command, "Texture")) {
			if (!p.quoted || p.ntextures == 32)
				fail(&p, "invalid texture declaration");
			strcpy(texture_name, p.token);
			next(&p);
			if (!p.quoted || (strcmp(p.token, "spectrum") && strcmp(p.token, "float")))
				fail(&p, "unsupported texture value type");
			next(&p);
		}
		if (!p.quoted)
			fail(&p, "expected quoted type");
		strcpy(type, p.token);
		next(&p);
		params(&p);
		if (!strcmp(command, "Motion") && !strcmp(type, "rigid")) {
			motion.translation = color(&p, "vector3 translation", (float3){0});
			motion.axis = color(&p, "vector3 axis", (float3){0, 1, 0});
			motion.angle = scalar(&p, "float angle", 0) * (M_PI / 180);
			float length = float3_dot(motion.axis, motion.axis);
			if (motion.angle != 0 && (!(length > 0) || !isfinite(length)))
				fail(&p, "invalid motion axis");
		} else if (!strcmp(command, "Volume") && !strcmp(type, "homogeneous")) {
			if (scene.volume)
				fail(&p, "only one global volume is supported");
			Volume *volume = xcalloc(1, sizeof(*volume));
			float3 lo = color(&p, "point3 p0", (float3){-100, -100, -100});
			float3 hi = color(&p, "point3 p1", (float3){100, 100, 100});
			volume->bounds =
			    (Aabb){.corners = {{lo.x, lo.y, lo.z}, {hi.x, hi.y, hi.z}}};
			volume->absorption = color(&p, "rgb sigma_a", (float3){0});
			volume->scattering = scalar(&p, "float sigma_s", 0);
			volume->g = scalar(&p, "float g", 0);
			if (lo.x >= hi.x || lo.y >= hi.y || lo.z >= hi.z ||
			    volume->absorption.x < 0 || volume->absorption.y < 0 ||
			    volume->absorption.z < 0 || volume->scattering < 0 ||
			    fabsf(volume->g) >= .99f ||
			    !isfinite(volume->absorption.x + volume->scattering) ||
			    !isfinite(volume->absorption.y + volume->scattering) ||
			    !isfinite(volume->absorption.z + volume->scattering))
				fail(&p, "invalid homogeneous volume");
			scene.volume = volume;
			SceneOwn(&scene, volume, NULL);
		} else if (!strcmp(command, "Texture")) {
			Texture *result;
			void (*destroy)(void *) = NULL;
			if (!strcmp(type, "checkerboard") || !strcmp(type, "noise")) {
				float3 a = color(&p, "rgb tex1", (float3){0, 0, 0});
				float3 b = color(&p, "rgb tex2", (float3){1, 1, 1});
				float scale = scalar(&p, "float scale", 1);
				result = &PatternCreate(a, b, scale, !strcmp(type, "noise"))->base;
			} else if (!strcmp(type, "constant")) {
				result =
				    TextureConstant(texture_color(&p, "value", (float3){1, 1, 1}));
			} else if (!strcmp(type, "mix") || !strcmp(type, "scale")) {
				float3 first =
				    !strcmp(type, "scale") ? (float3){1, 1, 1} : (float3){0};
				Texture *a = texture_input(&p, &scene, "tex1", first);
				Texture *b = texture_input(&p, &scene, "tex2", (float3){1, 1, 1});
				result = !strcmp(type, "scale")
					     ? TextureScale(a, b)
					     : TextureMix(a, b,
							  texture_input(&p, &scene, "amount",
									(float3){.5f, .5f, .5f}));
			} else if (!strcmp(type, "map")) {
				Texture *source = texture(&p, "texture tex");
				Transform inverse;
				if (!source || !TransformInvert(transform, &inverse))
					fail(&p, "invalid texture mapping");
				result = TextureMap(source, inverse, scalar(&p, "float uscale", 1),
						    scalar(&p, "float vscale", 1),
						    scalar(&p, "float udelta", 0),
						    scalar(&p, "float vdelta", 0));
			} else if (!strcmp(type, "imagemap")) {
				Param *a = parameter(&p, "string filename", 0);
				if (!a || !a->text[0])
					fail(&p, "missing texture filename");
				char imagepath[1024];
				filename(&p, a->text, imagepath);
				result = &ImageRead(imagepath)->base;
				destroy = ImageFree;
			} else
				fail(&p, "unsupported texture");
			for (size_t i = 0; i < p.ntextures; ++i)
				if (!strcmp(p.textures[i].name, texture_name))
					fail(&p, "duplicate texture");
			strcpy(p.textures[p.ntextures].name, texture_name);
			p.textures[p.ntextures++].texture = result;
			SceneOwn(&scene, result, destroy);
		} else if (!strcmp(command, "Camera") && !strcmp(type, "perspective")) {
			float fov = scalar(&p, "float fov", 45);
			if (!(fov > 0 && fov < 180))
				fail(&p, "invalid field of view");
			scene.camera.fov = fov * M_PI / 180;
			scene.camera.shutter_open = scalar(&p, "float shutteropen", 0);
			scene.camera.shutter_close = scalar(&p, "float shutterclose", 0);
			if (scene.camera.shutter_open < 0 || scene.camera.shutter_close > 1 ||
			    scene.camera.shutter_close < scene.camera.shutter_open)
				fail(&p, "invalid shutter interval");
		} else if (!strcmp(command, "Material")) {
			float3 c = color(&p, "rgb reflectance", (float3){0.5f, 0.5f, 0.5f});
			if (!strcmp(type, "diffuse")) {
				Lambert *m = ALLOC_MATERIAL(Lambert);
				m->color = c;
				material = &m->base;
			} else if (!strcmp(type, "coateddiffuse")) {
				Coated *m = ALLOC_MATERIAL(Coated);
				m->color = c;
				material = &m->base;
			} else if (!strcmp(type, "conductor")) {
				Ggx *m = ALLOC_MATERIAL(Ggx);
				m->color = c;
				m->roughness = scalar(&p, "float roughness", .2f);
				m->base.roughness_map = texture(&p, "texture roughness");
				if (m->roughness < 0 || m->roughness > 1)
					fail(&p, "invalid roughness");
				material = &m->base;
			} else if (!strcmp(type, "dielectric")) {
				Glass *m = ALLOC_MATERIAL(Glass);
				m->transmittance =
				    color(&p, "rgb transmittance", (float3){1, 1, 1});
				m->ior = scalar(&p, "float eta", 1.5f);
				if (!(m->ior > 0) || m->transmittance.x < 0 ||
				    m->transmittance.x > 1 || m->transmittance.y < 0 ||
				    m->transmittance.y > 1 || m->transmittance.z < 0 ||
				    m->transmittance.z > 1)
					fail(&p, "invalid dielectric parameters");
				material = &m->base;
			} else
				fail(&p, "unsupported material");
			SceneOwn(&scene, material, NULL);
			material->texture = texture(&p, "texture reflectance");
			material->bump = texture(&p, "texture displacement");
			material->bump_scale = scalar(&p, "float bumpscale", 1);
		} else if (!strcmp(command, "AreaLightSource") && !strcmp(type, "diffuse")) {
			Lamp *m = ALLOC_MATERIAL(Lamp);
			m->color = color(&p, "rgb L", (float3){1, 1, 1});
			material = &m->base;
			SceneOwn(&scene, m, NULL);
		} else if (!strcmp(command, "LightSource") && !strcmp(type, "infinite")) {
			scene.sky_color = color(&p, "rgb L", (float3){1, 1, 1});
			Param *image = parameter(&p, "string filename", 0);
			if (scene.environment)
				fail(&p, "only one environment is supported");
			if (image) {
				char imagepath[1024];
				filename(&p, image->text, imagepath);
				scene.environment = EnvironmentRead(imagepath);
				SceneOwn(&scene, scene.environment, EnvironmentFree);
			}
		} else if (!strcmp(command, "Shape")) {
			if (!strcmp(type, "trianglemesh")) {
				Param *points = parameter(&p, "point3 P", 0);
				Param *indices = parameter(&p, "integer indices", 0);
				Param *normals = parameter(&p, "normal3 N", 0);
				if (!normals)
					normals = parameter(&p, "normal N", 0);
				Param *uv = parameter(&p, "point2 uv", 0);
				if (!points || !indices || !points->count || points->count % 3 ||
				    !indices->count || indices->count % 3 ||
				    (normals && normals->count != points->count) ||
				    (uv && uv->count != points->count / 3 * 2))
					fail(&p, "invalid mesh arrays");
				size_t nv = points->count / 3, nt = indices->count / 3;
				uint32_t(*tri)[3] = xmalloc(nt * sizeof(*tri));
				for (size_t i = 0; i < indices->count; ++i) {
					double value = indices->values[i];
					if (value < 0 || value >= nv || floor(value) != value)
						fail(&p, "invalid vertex index");
					tri[i / 3][i % 3] = value;
				}
				float3 *positions = xmalloc(nv * sizeof(*positions));
				float3 *normal = normals ? xmalloc(nv * sizeof(*normal)) : NULL;
				float(*texcoords)[2] = uv ? xmalloc(nv * sizeof(*texcoords)) : NULL;
				for (size_t i = 0; i < nv; ++i) {
					positions[i] = (float3){points->values[3 * i],
								points->values[3 * i + 1],
								points->values[3 * i + 2]};
					if (normal)
						normal[i] = (float3){normals->values[3 * i],
								     normals->values[3 * i + 1],
								     normals->values[3 * i + 2]};
					if (texcoords) {
						texcoords[i][0] = uv->values[2 * i];
						texcoords[i][1] = uv->values[2 * i + 1];
					}
				}
				Mesh *m =
				    MeshCreate(positions, normal, texcoords, tri, nv, nt, material);
				add_shape(&scene, &m->base, MeshFree, transform, motion);
			} else if (!strcmp(type, "plymesh")) {
				Param *a = parameter(&p, "string filename", 0);
				if (!a || !a->text[0])
					fail(&p, "missing mesh filename");
				char meshpath[1024];
				filename(&p, a->text, meshpath);
				Mesh *m = MeshReadPly(meshpath, material);
				add_shape(&scene, &m->base, MeshFree, transform, motion);
			} else if (!strcmp(type, "sphere")) {
				Sphere *s = ALLOC_SHAPE(Sphere);
				s->r = scalar(&p, "float radius", 1);
				if (!(s->r > 0))
					fail(&p, "invalid sphere radius");
				s->o = (float3){0};
				s->material = material;
				add_shape(&scene, &s->base, NULL, transform, motion);
			} else
				fail(&p, "unsupported shape");
		} else
			fail(&p, "unsupported directive or type");
		for (size_t i = 0; i < p.nparams; ++i) {
			if (!p.params[i].used)
				fail(&p, p.params[i].key);
			free(p.params[i].values);
		}
	}
	if (depth || defining)
		fail(&p, "unclosed attribute block");
	fclose(p.file);
	// PBRT's raster y grows downward; CameraLookAt uses the supplied up as axis_y.
	CameraLookAt(&scene.camera, eye, at, float3_scale(up, -1));
	Shape *root = BvhBuild(scene.shapes, scene.nshapes);
	free(scene.shapes);
	scene.shapes = xmalloc(sizeof(*scene.shapes));
	scene.shapes[0] = root;
	scene.nshapes = 1;
	return scene;
}
