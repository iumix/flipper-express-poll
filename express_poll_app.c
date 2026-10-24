#include "express_poll_app_i.h"

#define TAG "ExpressPoll"

static bool express_poll_app_custom_event_callback(void* context, uint32_t event) {
    ExpressPollApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool express_poll_app_back_event_callback(void* context) {
    ExpressPollApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

void express_poll_app_save_settings(ExpressPollApp* app) {
    if(!express_poll_settings_save(&app->settings)) {
        FURI_LOG_E(TAG, "Settings not saved");
    }
}

static ExpressPollApp* express_poll_app_alloc(void) {
    ExpressPollApp* app = malloc(sizeof(ExpressPollApp));

    express_poll_settings_load(&app->settings);
    app->seen_detect_count = 0;
    app->worker = express_poll_worker_alloc();

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&express_poll_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, express_poll_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, express_poll_app_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ExpressPollViewSubmenu, submenu_get_view(app->submenu));

    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        ExpressPollViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));

    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ExpressPollViewByteInput, byte_input_get_view(app->byte_input));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ExpressPollViewWidget, widget_get_view(app->widget));

    app->poll_view = poll_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ExpressPollViewPoll, poll_view_get_view(app->poll_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static void express_poll_app_free(ExpressPollApp* app) {
    /* Scenes stop the worker on exit; this is a safety net. */
    express_poll_worker_free(app->worker);

    view_dispatcher_remove_view(app->view_dispatcher, ExpressPollViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, ExpressPollViewVariableItemList);
    view_dispatcher_remove_view(app->view_dispatcher, ExpressPollViewByteInput);
    view_dispatcher_remove_view(app->view_dispatcher, ExpressPollViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, ExpressPollViewPoll);

    submenu_free(app->submenu);
    variable_item_list_free(app->variable_item_list);
    byte_input_free(app->byte_input);
    widget_free(app->widget);
    poll_view_free(app->poll_view);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t express_poll_app(void* p) {
    UNUSED(p);
    ExpressPollApp* app = express_poll_app_alloc();

    scene_manager_next_scene(app->scene_manager, ExpressPollSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    express_poll_app_free(app);
    return 0;
}
