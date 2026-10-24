#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) ExpressPollScene##id,
typedef enum {
#include "express_poll_scene_config.h"
    ExpressPollSceneNum,
} ExpressPollScene;
#undef ADD_SCENE

extern const SceneManagerHandlers express_poll_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "express_poll_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "express_poll_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "express_poll_scene_config.h"
#undef ADD_SCENE
