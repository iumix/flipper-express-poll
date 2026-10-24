#pragma once

#include "scenes/express_poll_scene.h"
#include "settings/express_poll_settings.h"
#include "views/poll_view.h"
#include "worker/express_poll_worker.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <notification/notification_messages.h>

#define EXPRESS_POLL_APP_NAME    "Apple Pay Express Poll"
#define EXPRESS_POLL_APP_AUTHOR  "iumix"
#define EXPRESS_POLL_APP_VERSION "0.1"

typedef enum {
    ExpressPollViewSubmenu,
    ExpressPollViewVariableItemList,
    ExpressPollViewByteInput,
    ExpressPollViewWidget,
    ExpressPollViewPoll,
} ExpressPollView;

typedef enum {
    /* Values below 100 are reserved for submenu indices. */
    ExpressPollCustomEventWorkerUpdate = 100,
    ExpressPollCustomEventToggle,
    ExpressPollCustomEventCodeEntered,
} ExpressPollCustomEvent;

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    Submenu* submenu;
    VariableItemList* variable_item_list;
    ByteInput* byte_input;
    Widget* widget;
    PollView* poll_view;

    ExpressPollWorker* worker;
    ExpressPollSettings settings;

    ExpressPollTech poll_tech; /**< Which technology the Poll scene should start. */
    uint8_t code_input[2]; /**< Big-endian system code being edited. */
    uint32_t seen_detect_count; /**< For detection feedback on the GUI thread. */
} ExpressPollApp;

/** Persist settings, logging on failure. */
void express_poll_app_save_settings(ExpressPollApp* app);
