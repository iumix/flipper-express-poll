#include "../express_poll_app_i.h"

static const char about_text[] =
    "\e#" EXPRESS_POLL_APP_NAME "\n"
    "v" EXPRESS_POLL_APP_VERSION " by " EXPRESS_POLL_APP_AUTHOR "\n\n"
    "Sends NFC-F (FeliCa) Polling with a chosen system code, e.g. to wake "
    "an Express Transit card in Apple Wallet.\n\n"
    "0x0003 Suica/ICOCA/PASMO\n"
    "0x8008 Octopus\n\n"
    "Balance is read with read-only commands (Read Without Encryption) "
    "when the system is known. Unknown or protected services are reported, "
    "never guessed.\n\n"
    "No payments, no writes, no keys.\n"
    "Not affiliated with Apple.";

void express_poll_scene_about_on_enter(void* context) {
    ExpressPollApp* app = context;
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, about_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, ExpressPollViewWidget);
}

bool express_poll_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void express_poll_scene_about_on_exit(void* context) {
    ExpressPollApp* app = context;
    widget_reset(app->widget);
}
