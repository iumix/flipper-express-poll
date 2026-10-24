#pragma once

#include "../worker/express_poll_worker.h"

#include <gui/view.h>

typedef struct PollView PollView;

/** Called on the GUI thread when the user presses OK. */
typedef void (*PollViewToggleCallback)(void* context);

PollView* poll_view_alloc(void);
void poll_view_free(PollView* poll_view);
View* poll_view_get_view(PollView* poll_view);

void poll_view_set_toggle_callback(
    PollView* poll_view,
    PollViewToggleCallback callback,
    void* context);

/** Replace the displayed status. running selects the Start/Stop button label. */
void poll_view_update(PollView* poll_view, const ExpressPollStatus* status, bool running);
