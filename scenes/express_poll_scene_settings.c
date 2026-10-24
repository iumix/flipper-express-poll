#include "../express_poll_app_i.h"

#include <stdio.h>

static const char* const off_on_text[] = {"Off", "On"};

static const char* const request_code_text[] = {"None", "Sys code"};
static const uint8_t request_code_values[] = {
    FELICA_FRAME_RC_NONE,
    FELICA_FRAME_RC_SYSTEM_CODE,
};

static void interval_changed(VariableItem* item) {
    ExpressPollApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.interval_index = index;

    char text[12];
    snprintf(
        text, sizeof(text), "%lu ms", (unsigned long)express_poll_settings_intervals_ms[index]);
    variable_item_set_current_value_text(item, text);
}

static void request_code_changed(VariableItem* item) {
    ExpressPollApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.request_code = request_code_values[index];
    variable_item_set_current_value_text(item, request_code_text[index]);
}

static void read_balance_changed(VariableItem* item) {
    ExpressPollApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.read_balance = index == 1;
    variable_item_set_current_value_text(item, off_on_text[index]);
}

static void feedback_changed(VariableItem* item) {
    ExpressPollApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.feedback = index == 1;
    variable_item_set_current_value_text(item, off_on_text[index]);
}

static void add_item(
    ExpressPollApp* app,
    const char* label,
    uint8_t count,
    VariableItemChangeCallback callback,
    uint8_t index) {
    VariableItem* item =
        variable_item_list_add(app->variable_item_list, label, count, callback, app);
    variable_item_set_current_value_index(item, index);
    /* Reuse the change callback to render the initial value text. */
    callback(item);
}

void express_poll_scene_settings_on_enter(void* context) {
    ExpressPollApp* app = context;
    ExpressPollSettings* settings = &app->settings;

    uint8_t interval_index =
        settings->interval_index < EXPRESS_POLL_INTERVAL_COUNT ? settings->interval_index : 0;
    uint8_t request_index = settings->request_code == FELICA_FRAME_RC_NONE ? 0 : 1;

    add_item(app, "Poll interval", EXPRESS_POLL_INTERVAL_COUNT, interval_changed, interval_index);
    add_item(app, "Request code", 2, request_code_changed, request_index);
    add_item(app, "Read balance", 2, read_balance_changed, settings->read_balance ? 1 : 0);
    add_item(app, "LED/vibro", 2, feedback_changed, settings->feedback ? 1 : 0);

    view_dispatcher_switch_to_view(app->view_dispatcher, ExpressPollViewVariableItemList);
}

bool express_poll_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void express_poll_scene_settings_on_exit(void* context) {
    ExpressPollApp* app = context;
    express_poll_app_save_settings(app);
    variable_item_list_reset(app->variable_item_list);
}
