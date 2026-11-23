#pragma once

#include "upt/ray_core.h"

Scene SceneRead(char const *path);
void SceneOwn(Scene *scene, void *ptr, void (*destroy)(void *));
void SceneFree(Scene *scene);
