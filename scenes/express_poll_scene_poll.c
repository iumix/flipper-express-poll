#include "../express_poll_app_i.h"

static void express_poll_scene_poll_worker_callback(void* context) {
    /* NFC thread: only enqueue, never touch views here. */
    ExpressPollApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ExpressPollCustomEventWorkerUpdate);
}

static void express_poll_scene_poll_toggle_callback(void* context) {
    ExpressPollApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ExpressPollCustomEventToggle);
}

static void express_poll_scene_poll_refresh(ExpressPollApp* app) {
    ExpressPollStatus status;
    express_poll_worker_get_status(app->worker, &status);
    const bool running = express_poll_worker_is_running(app->worker);

    /* Show the configured tech/code even before the first start. */
    if(!running && status.poll_count == 0) {
        status.tech = app->poll_tech;
        status.polled_system_code = app->settings.system_code;
    }

    if(status.detect_count != app->seen_detect_count) {
        app->seen_detect_count = status.detect_count;
        if(running && app->settings.feedback) {
            notification_message(app->notifications, &sequence_success);
        }
    }

    poll_view_update(app->poll_view, &status, running);
}

static void express_poll_scene_poll_start(ExpressPollApp* app) {
    const ExpressPollWorkerConfig config = {
        .tech = app->poll_tech,
        .system_code = app->settings.system_code,
        .request_code = app->settings.request_code,
        .interval_ms = express_poll_settings_interval_ms(&app->settings),
        .read_balance = app->settings.read_balance,
    };
    app->seen_detect_count = 0;
    express_poll_worker_start(app->worker, &config);
    notification_message(app->notifications, &sequence_blink_start_cyan);
    express_poll_scene_poll_refresh(app);
}

static void express_poll_scene_poll_stop(ExpressPollApp* app) {
    if(!express_poll_worker_is_running(app->worker)) return;
    express_poll_worker_stop(app->worker);
    notification_message(app->notifications, &sequence_blink_stop);
    express_poll_scene_poll_refresh(app);
}

void express_poll_scene_poll_on_enter(void* context) {
    ExpressPollApp* app = context;

    express_poll_worker_set_callback(app->worker, express_poll_scene_poll_worker_callback, app);
    poll_view_set_toggle_callback(app->poll_view, express_poll_scene_poll_toggle_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ExpressPollViewPoll);

    /* "Start Polling" starts immediately; OK toggles afterwards. */
    express_poll_scene_poll_start(app);
}

bool express_poll_scene_poll_on_event(void* context, SceneManagerEvent event) {
    ExpressPollApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ExpressPollCustomEventWorkerUpdate) {
            express_poll_scene_poll_refresh(app);
            return true;
        }
        if(event.event == ExpressPollCustomEventToggle) {
            if(express_poll_worker_is_running(app->worker)) {
                express_poll_scene_poll_stop(app);
            } else {
                express_poll_scene_poll_start(app);
            }
            return true;
        }
    }
    /* Back falls through to the scene manager; on_exit stops the worker. */
    return false;
}

void express_poll_scene_poll_on_exit(void* context) {
    ExpressPollApp* app = context;
    express_poll_scene_poll_stop(app);
    poll_view_set_toggle_callback(app->poll_view, NULL, NULL);
}
