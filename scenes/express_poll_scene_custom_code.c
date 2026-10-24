#include "../express_poll_app_i.h"

static void express_poll_scene_custom_code_input_callback(void* context) {
    ExpressPollApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ExpressPollCustomEventCodeEntered);
}

void express_poll_scene_custom_code_on_enter(void* context) {
    ExpressPollApp* app = context;

    /* ByteInput edits hex bytes in place; every 16-bit value is a valid code. */
    app->code_input[0] = (uint8_t)(app->settings.custom_system_code >> 8);
    app->code_input[1] = (uint8_t)app->settings.custom_system_code;

    byte_input_set_header_text(app->byte_input, "System code (hex)");
    byte_input_set_result_callback(
        app->byte_input,
        express_poll_scene_custom_code_input_callback,
        NULL,
        app,
        app->code_input,
        sizeof(app->code_input));

    view_dispatcher_switch_to_view(app->view_dispatcher, ExpressPollViewByteInput);
}

bool express_poll_scene_custom_code_on_event(void* context, SceneManagerEvent event) {
    ExpressPollApp* app = context;
    if(event.type != SceneManagerEventTypeCustom ||
       event.event != ExpressPollCustomEventCodeEntered) {
        return false;
    }

    const uint16_t code = (uint16_t)((app->code_input[0] << 8) | app->code_input[1]);
    app->settings.custom_system_code = code;
    app->settings.system_code = code;
    express_poll_app_save_settings(app);

    scene_manager_search_and_switch_to_previous_scene(app->scene_manager, ExpressPollSceneStart);
    return true;
}

void express_poll_scene_custom_code_on_exit(void* context) {
    ExpressPollApp* app = context;
    byte_input_set_result_callback(app->byte_input, NULL, NULL, NULL, NULL, 0);
    byte_input_set_header_text(app->byte_input, "");
}
