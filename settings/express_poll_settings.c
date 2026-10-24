#include "express_poll_settings.h"

#include "../felica/felica_frames.h"
#include "../felica/felica_systems.h"

#include <furi.h>
#include <flipper_format/flipper_format.h>
#include <storage/storage.h>

#define TAG "ExpressPollSettings"

/* /data resolves to /ext/apps_data/<appid>/ for external apps. */
#define SETTINGS_PATH     APP_DATA_PATH("settings.txt")
#define SETTINGS_FILETYPE "Apple Pay Express Poll Settings"
#define SETTINGS_VERSION  1U

#define KEY_SYSTEM_CODE  "System code"
#define KEY_CUSTOM_CODE  "Custom system code"
#define KEY_INTERVAL_MS  "Poll interval ms"
#define KEY_REQUEST_CODE "Request code"
#define KEY_READ_BALANCE "Read balance"
#define KEY_FEEDBACK     "Feedback"

#define DEFAULT_INTERVAL_INDEX 1U

const uint32_t express_poll_settings_intervals_ms[EXPRESS_POLL_INTERVAL_COUNT] = {
    50,
    100,
    250,
    500,
    1000,
};

void express_poll_settings_set_defaults(ExpressPollSettings* settings) {
    furi_check(settings);
    settings->system_code = FELICA_SYSTEM_CODE_SUICA;
    settings->custom_system_code = FELICA_SYSTEM_CODE_WILDCARD;
    settings->interval_index = DEFAULT_INTERVAL_INDEX;
    settings->request_code = FELICA_FRAME_RC_SYSTEM_CODE;
    settings->read_balance = true;
    settings->feedback = true;
}

uint32_t express_poll_settings_interval_ms(const ExpressPollSettings* settings) {
    furi_check(settings);
    uint8_t index = settings->interval_index < EXPRESS_POLL_INTERVAL_COUNT ?
                        settings->interval_index :
                        DEFAULT_INTERVAL_INDEX;
    return express_poll_settings_intervals_ms[index];
}

static bool read_system_code(FlipperFormat* ff, const char* key, FuriString* tmp, uint16_t* out) {
    flipper_format_rewind(ff);
    return flipper_format_read_string(ff, key, tmp) &&
           felica_system_code_parse(furi_string_get_cstr(tmp), out);
}

static bool read_u32(FlipperFormat* ff, const char* key, uint32_t* out) {
    flipper_format_rewind(ff);
    return flipper_format_read_uint32(ff, key, out, 1);
}

static bool read_bool(FlipperFormat* ff, const char* key, bool* out) {
    flipper_format_rewind(ff);
    return flipper_format_read_bool(ff, key, out, 1);
}

void express_poll_settings_load(ExpressPollSettings* settings) {
    furi_check(settings);
    express_poll_settings_set_defaults(settings);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* tmp = furi_string_alloc();

    do {
        if(!flipper_format_file_open_existing(ff, SETTINGS_PATH)) {
            FURI_LOG_I(TAG, "No settings file, using defaults");
            break;
        }

        uint32_t version = 0;
        if(!flipper_format_read_header(ff, tmp, &version) ||
           furi_string_cmp_str(tmp, SETTINGS_FILETYPE) != 0 || version != SETTINGS_VERSION) {
            FURI_LOG_W(TAG, "Unsupported settings file, using defaults");
            break;
        }

        /* Each key is optional; a missing or invalid one keeps its default. */
        uint16_t code;
        if(read_system_code(ff, KEY_SYSTEM_CODE, tmp, &code)) settings->system_code = code;
        if(read_system_code(ff, KEY_CUSTOM_CODE, tmp, &code)) settings->custom_system_code = code;

        uint32_t value;
        if(read_u32(ff, KEY_INTERVAL_MS, &value)) {
            for(uint8_t i = 0; i < EXPRESS_POLL_INTERVAL_COUNT; i++) {
                if(express_poll_settings_intervals_ms[i] == value) settings->interval_index = i;
            }
        }
        if(read_u32(ff, KEY_REQUEST_CODE, &value) &&
           (value == FELICA_FRAME_RC_NONE || value == FELICA_FRAME_RC_SYSTEM_CODE)) {
            settings->request_code = (uint8_t)value;
        }

        bool flag;
        if(read_bool(ff, KEY_READ_BALANCE, &flag)) settings->read_balance = flag;
        if(read_bool(ff, KEY_FEEDBACK, &flag)) settings->feedback = flag;
    } while(false);

    furi_string_free(tmp);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

bool express_poll_settings_save(const ExpressPollSettings* settings) {
    furi_check(settings);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool saved = false;

    char code[FELICA_SYSTEM_CODE_STR_SIZE];
    char custom[FELICA_SYSTEM_CODE_STR_SIZE];
    felica_system_code_format(settings->system_code, code);
    felica_system_code_format(settings->custom_system_code, custom);
    uint32_t interval = express_poll_settings_interval_ms(settings);
    uint32_t request_code = settings->request_code;

    do {
        if(!flipper_format_file_open_always(ff, SETTINGS_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, SETTINGS_FILETYPE, SETTINGS_VERSION)) break;
        if(!flipper_format_write_string_cstr(ff, KEY_SYSTEM_CODE, code)) break;
        if(!flipper_format_write_string_cstr(ff, KEY_CUSTOM_CODE, custom)) break;
        if(!flipper_format_write_uint32(ff, KEY_INTERVAL_MS, &interval, 1)) break;
        if(!flipper_format_write_uint32(ff, KEY_REQUEST_CODE, &request_code, 1)) break;
        if(!flipper_format_write_bool(ff, KEY_READ_BALANCE, &settings->read_balance, 1)) break;
        if(!flipper_format_write_bool(ff, KEY_FEEDBACK, &settings->feedback, 1)) break;
        saved = true;
    } while(false);

    if(!saved) FURI_LOG_E(TAG, "Failed to save settings");

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return saved;
}
