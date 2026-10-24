#include "poll_view.h"

#include "../felica/felica_systems.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>

struct PollView {
    View* view;
    PollViewToggleCallback callback;
    void* context;
};

typedef struct {
    ExpressPollStatus status;
    bool running;
} PollViewModel;

static const char* poll_view_result_str(ExpressPollResult result) {
    switch(result) {
    case ExpressPollResultSuccess:
        return "OK";
    case ExpressPollResultTimeout:
        return "timeout";
    case ExpressPollResultCrcError:
        return "CRC err";
    case ExpressPollResultFrameError:
        return "bad data";
    case ExpressPollResultTransportError:
        return "NFC err";
    case ExpressPollResultNone:
        break;
    }
    return "-";
}

static void poll_view_hex(const uint8_t* data, size_t len, char* out, size_t out_size) {
    size_t pos = 0;
    for(size_t i = 0; i < len && pos + 3 <= out_size; i++) {
        pos += snprintf(out + pos, out_size - pos, "%02X", data[i]);
    }
}

static void poll_view_status_line(const PollViewModel* model, char* out, size_t out_size) {
    const ExpressPollStatus* status = &model->status;
    const char* phase;

    if(!model->running) {
        phase = "Stopped";
    } else if(status->phase == ExpressPollPhaseReading) {
        phase = status->tech == ExpressPollTechEcp ? "Selecting" : "Reading";
    } else if(status->phase == ExpressPollPhasePresent) {
        phase = "Present";
    } else if(status->has_card) {
        phase = "Removed";
    } else {
        phase = "Searching";
    }

    if(status->poll_count == 0) {
        snprintf(out, out_size, "%s", phase);
    } else {
        snprintf(out, out_size, "%s: %s", phase, poll_view_result_str(status->last_result));
    }
}

static void poll_view_balance_line(const ExpressPollStatus* status, char* out, size_t out_size) {
    char code[FELICA_SYSTEM_CODE_STR_SIZE];
    felica_system_code_format(status->identified_system_code, code);

    switch(status->balance_state) {
    case ExpressPollBalanceOk: {
        char value[BALANCE_STR_SIZE];
        balance_format(status->decoder, status->balance, value, sizeof(value));
        snprintf(out, out_size, "Bal: %s", value);
        break;
    }
    case ExpressPollBalanceNotRead:
        snprintf(out, out_size, "Reading balance...");
        break;
    case ExpressPollBalanceDisabled:
        if(status->identified) {
            snprintf(out, out_size, "Sys %s (bal. off)", code);
        } else {
            snprintf(out, out_size, "Balance read off");
        }
        break;
    case ExpressPollBalanceUnsupported:
        if(status->identified) {
            snprintf(out, out_size, "Sys %s: no decoder", code);
        } else {
            snprintf(out, out_size, "System unknown");
        }
        break;
    case ExpressPollBalanceServiceMissing:
        snprintf(out, out_size, "Bal: no service");
        break;
    case ExpressPollBalanceNoResponse:
        snprintf(out, out_size, "Bal: no response");
        break;
    case ExpressPollBalanceCardError:
        snprintf(out, out_size, "Bal: card err %02X/%02X", status->status1, status->status2);
        break;
    case ExpressPollBalanceInvalid:
        snprintf(out, out_size, "Bal: invalid data");
        break;
    }
}

static void poll_view_draw_callback(Canvas* canvas, void* model_ptr) {
    const PollViewModel* model = model_ptr;
    const ExpressPollStatus* status = &model->status;
    char line[32];

    const bool ecp = status->tech == ExpressPollTechEcp;

    canvas_clear(canvas);

    /* Header line. */
    canvas_set_font(canvas, FontPrimary);
    if(ecp) {
        canvas_draw_str(canvas, 0, 9, "Express Card");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 76, 9, "ECP A");
    } else {
        felica_system_code_format(status->polled_system_code, line);
        canvas_draw_str(canvas, 0, 9, line);
        const char* name = felica_system_name(status->polled_system_code);
        canvas_set_font(canvas, FontSecondary);
        if(name) canvas_draw_str(canvas, 42, 9, name);
    }

    poll_view_status_line(model, line, sizeof(line));
    canvas_draw_str(canvas, 0, 20, line);
    if(status->poll_count) {
        snprintf(line, sizeof(line), "#%lu", (unsigned long)status->poll_count);
        canvas_draw_str_aligned(canvas, 128, 20, AlignRight, AlignBottom, line);
    }

    if(status->has_card && ecp) {
        char hex[ISO14443_3A_MAX_UID_SIZE * 2 + 1] = {0};
        poll_view_hex(status->nfca.uid, status->nfca.uid_len, hex, sizeof(hex));
        snprintf(line, sizeof(line), "UID %s", hex);
        canvas_draw_str(canvas, 0, 30, line);
        snprintf(
            line,
            sizeof(line),
            "ATQA %02X%02X SAK %02X",
            status->nfca.atqa[1],
            status->nfca.atqa[0],
            status->nfca.sak);
        canvas_draw_str(canvas, 0, 39, line);
        const char* selection;
        switch(status->emv_result) {
        case EmvSelectOk:
            selection = "App selected";
            break;
        case EmvSelectNotAttempted:
            selection = "Selecting app...";
            break;
        case EmvSelectUnsupported:
            selection = "ISO-DEP unsupported";
            break;
        case EmvSelectTimeout:
            selection = "Select timed out";
            break;
        case EmvSelectProtocolError:
            selection = "Invalid select reply";
            break;
        case EmvSelectTransportError:
            selection = "Select NFC error";
            break;
        case EmvSelectNoApplication:
            selection = "No payment app";
            break;
        case EmvSelectCancelled:
            selection = "Selection stopped";
            break;
        case EmvSelectRejected:
            snprintf(line, sizeof(line), "Select rejected %04X", status->emv_status_word);
            selection = line;
            break;
        default:
            selection = "Selection unavailable";
            break;
        }
        canvas_draw_str(canvas, 0, 48, selection);
    } else if(status->has_card) {
        char hex[FELICA_FRAME_IDM_LEN * 2 + 1] = {0};
        poll_view_hex(status->card.idm, FELICA_FRAME_IDM_LEN, hex, sizeof(hex));
        snprintf(line, sizeof(line), "IDm %s", hex);
        canvas_draw_str(canvas, 0, 30, line);
        poll_view_hex(status->card.pmm, FELICA_FRAME_PMM_LEN, hex, sizeof(hex));
        snprintf(line, sizeof(line), "PMm %s", hex);
        canvas_draw_str(canvas, 0, 39, line);
        poll_view_balance_line(status, line, sizeof(line));
        canvas_draw_str(canvas, 0, 48, line);
    } else if(model->running) {
        canvas_draw_str(canvas, 0, 32, "Hold iPhone, Watch or");
        canvas_draw_str(canvas, 0, 41, ecp ? "payment card near back." : "card near the back.");
    } else {
        canvas_draw_str(canvas, 0, 36, "Press OK to start polling.");
    }

    elements_button_center(canvas, model->running ? "Stop" : "Start");
}

static bool poll_view_input_callback(InputEvent* event, void* context) {
    PollView* poll_view = context;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(poll_view->callback) poll_view->callback(poll_view->context);
        return true;
    }
    return false;
}

PollView* poll_view_alloc(void) {
    PollView* poll_view = malloc(sizeof(PollView));
    poll_view->view = view_alloc();
    poll_view->callback = NULL;
    poll_view->context = NULL;
    view_allocate_model(poll_view->view, ViewModelTypeLocking, sizeof(PollViewModel));
    view_set_context(poll_view->view, poll_view);
    view_set_draw_callback(poll_view->view, poll_view_draw_callback);
    view_set_input_callback(poll_view->view, poll_view_input_callback);
    return poll_view;
}

void poll_view_free(PollView* poll_view) {
    furi_check(poll_view);
    view_free(poll_view->view);
    free(poll_view);
}

View* poll_view_get_view(PollView* poll_view) {
    furi_check(poll_view);
    return poll_view->view;
}

void poll_view_set_toggle_callback(
    PollView* poll_view,
    PollViewToggleCallback callback,
    void* context) {
    furi_check(poll_view);
    poll_view->callback = callback;
    poll_view->context = context;
}

void poll_view_update(PollView* poll_view, const ExpressPollStatus* status, bool running) {
    furi_check(poll_view);
    furi_check(status);
    with_view_model(
        poll_view->view,
        PollViewModel * model,
        {
            model->status = *status;
            model->running = running;
        },
        true);
}
