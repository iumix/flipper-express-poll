#include "../express_poll_app_i.h"
#include "../felica/felica_systems.h"

#include <stdio.h>

typedef enum {
    SystemCodeIndexSuica,
    SystemCodeIndexOctopus,
    SystemCodeIndexCustom,
} SystemCodeIndex;

static const uint16_t system_code_presets[] = {
    [SystemCodeIndexSuica] = FELICA_SYSTEM_CODE_SUICA,
    [SystemCodeIndexOctopus] = FELICA_SYSTEM_CODE_OCTOPUS,
};

static void express_poll_scene_system_code_submenu_callback(void* context, uint32_t index) {
    ExpressPollApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void add_preset(ExpressPollApp* app, SystemCodeIndex index) {
    const uint16_t code = system_code_presets[index];
    char code_str[FELICA_SYSTEM_CODE_STR_SIZE];
    felica_system_code_format(code, code_str);
    char label[40];
    snprintf(label, sizeof(label), "%s %s", code_str, felica_system_name(code));
    submenu_add_item(
        app->submenu, label, index, express_poll_scene_system_code_submenu_callback, app);
}

void express_poll_scene_system_code_on_enter(void* context) {
    ExpressPollApp* app = context;
    Submenu* submenu = app->submenu;

    add_preset(app, SystemCodeIndexSuica);
    add_preset(app, SystemCodeIndexOctopus);

    char code_str[FELICA_SYSTEM_CODE_STR_SIZE];
    felica_system_code_format(app->settings.custom_system_code, code_str);
    char label[32];
    snprintf(label, sizeof(label), "Custom: %s", code_str);
    submenu_add_item(
        submenu,
        label,
        SystemCodeIndexCustom,
        express_poll_scene_system_code_submenu_callback,
        app);

    felica_system_code_format(app->settings.system_code, code_str);
    snprintf(label, sizeof(label), "Current: %s", code_str);
    submenu_set_header(submenu, label);

    SystemCodeIndex selected = SystemCodeIndexCustom;
    for(size_t i = 0; i < sizeof(system_code_presets) / sizeof(system_code_presets[0]); i++) {
        if(system_code_presets[i] == app->settings.system_code) selected = i;
    }
    submenu_set_selected_item(submenu, selected);

    view_dispatcher_switch_to_view(app->view_dispatcher, ExpressPollViewSubmenu);
}

bool express_poll_scene_system_code_on_event(void* context, SceneManagerEvent event) {
    ExpressPollApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == SystemCodeIndexCustom) {
        scene_manager_next_scene(app->scene_manager, ExpressPollSceneCustomCode);
        return true;
    }
    if(event.event < sizeof(system_code_presets) / sizeof(system_code_presets[0])) {
        app->settings.system_code = system_code_presets[event.event];
        express_poll_app_save_settings(app);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void express_poll_scene_system_code_on_exit(void* context) {
    ExpressPollApp* app = context;
    submenu_reset(app->submenu);
}
