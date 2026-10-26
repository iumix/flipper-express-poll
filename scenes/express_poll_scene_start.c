#include "../express_poll_app_i.h"

typedef enum {
    StartIndexFelica,
    StartIndexEcp,
    StartIndexTmoney,
    StartIndexSettings,
    StartIndexAbout,
} StartIndex;

static void express_poll_scene_start_submenu_callback(void* context, uint32_t index) {
    ExpressPollApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void express_poll_scene_start_on_enter(void* context) {
    ExpressPollApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_add_item(
        submenu,
        "FeliCa Polling",
        StartIndexFelica,
        express_poll_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu, "ECP Polling", StartIndexEcp, express_poll_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu,
        "T-Money Polling",
        StartIndexTmoney,
        express_poll_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu, "Settings", StartIndexSettings, express_poll_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "About", StartIndexAbout, express_poll_scene_start_submenu_callback, app);
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, ExpressPollSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, ExpressPollViewSubmenu);
}

bool express_poll_scene_start_on_event(void* context, SceneManagerEvent event) {
    ExpressPollApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    static const ExpressPollScene next[] = {
        [StartIndexFelica] = ExpressPollSceneFelica,
        [StartIndexEcp] = ExpressPollScenePoll,
        [StartIndexTmoney] = ExpressPollScenePoll,
        [StartIndexSettings] = ExpressPollSceneSettings,
        [StartIndexAbout] = ExpressPollSceneAbout,
    };
    if(event.event >= sizeof(next) / sizeof(next[0])) return false;

    /* The Poll scene reads poll_tech to decide which technology to start. */
    if(event.event == StartIndexEcp) app->poll_tech = ExpressPollTechEcp;
    if(event.event == StartIndexTmoney) app->poll_tech = ExpressPollTechTmoney;

    scene_manager_set_scene_state(app->scene_manager, ExpressPollSceneStart, event.event);
    scene_manager_next_scene(app->scene_manager, next[event.event]);
    return true;
}

void express_poll_scene_start_on_exit(void* context) {
    ExpressPollApp* app = context;
    submenu_reset(app->submenu);
}
