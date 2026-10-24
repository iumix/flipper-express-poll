#include "express_poll_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
static void (*const express_poll_on_enter_handlers[])(void*) = {
#include "express_poll_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
static bool (*const express_poll_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "express_poll_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
static void (*const express_poll_on_exit_handlers[])(void* context) = {
#include "express_poll_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers express_poll_scene_handlers = {
    .on_enter_handlers = express_poll_on_enter_handlers,
    .on_event_handlers = express_poll_on_event_handlers,
    .on_exit_handlers = express_poll_on_exit_handlers,
    .scene_num = ExpressPollSceneNum,
};
