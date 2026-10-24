#include "../express_poll_app_i.h"
#include "../felica/felica_systems.h"

#include <stdio.h>

typedef enum {
    FelicaIndexStart,
    FelicaIndexSystemCode,
} FelicaIndex;

static void express_poll_scene_felica_submenu_callback(void* context, uint32_t index) {
    ExpressPollApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void express_poll_scene_felica_on_enter(void* context) {
    ExpressPollApp* app = context;
    Submenu* submenu = app->submenu;

    char code[FELICA_SYSTEM_CODE_STR_SIZE];
    felica_system_code_format(app->settings.system_code, code);
    /* Submenu copies the label, so a stack buffer is fine. */
    char label[32];
    snprintf(label, sizeof(label), "System Code: %s", code);

    submenu_set_header(submenu, "FeliCa Polling");
    submenu_add_item(
        submenu,
        "Start Polling",
        FelicaIndexStart,
        express_poll_scene_felica_submenu_callback,
        app);
    submenu_add_item(
        submenu, label, FelicaIndexSystemCode, express_poll_scene_felica_submenu_callback, app);
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, ExpressPollSceneFelica));

    view_dispatcher_switch_to_view(app->view_dispatcher, ExpressPollViewSubmenu);
}

bool express_poll_scene_felica_on_event(void* context, SceneManagerEvent event) {
    ExpressPollApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    scene_manager_set_scene_state(app->scene_manager, ExpressPollSceneFelica, event.event);
    switch(event.event) {
    case FelicaIndexStart:
        /* The Poll scene reads poll_tech to decide which technology to start. */
        app->poll_tech = ExpressPollTechFelica;
        scene_manager_next_scene(app->scene_manager, ExpressPollScenePoll);
        return true;
    case FelicaIndexSystemCode:
        scene_manager_next_scene(app->scene_manager, ExpressPollSceneSystemCode);
        return true;
    default:
        return false;
    }
}

void express_poll_scene_felica_on_exit(void* context) {
    ExpressPollApp* app = context;
    submenu_reset(app->submenu);
}
