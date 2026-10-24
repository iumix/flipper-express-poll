#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EXPRESS_POLL_INTERVAL_COUNT 5U

typedef struct {
    uint16_t system_code; /**< Code sent in every Polling command. */
    uint16_t custom_system_code; /**< Last user-entered custom code. */
    uint8_t interval_index; /**< Index into express_poll_settings_intervals_ms. */
    uint8_t request_code; /**< Polling RC: 0x00 none, 0x01 system code. */
    bool read_balance;
    bool feedback; /**< LED/vibration on detection. */
} ExpressPollSettings;

extern const uint32_t express_poll_settings_intervals_ms[EXPRESS_POLL_INTERVAL_COUNT];

void express_poll_settings_set_defaults(ExpressPollSettings* settings);

/** Load from SD card; falls back to defaults on any error. */
void express_poll_settings_load(ExpressPollSettings* settings);

/** Save to SD card; returns false on failure. */
bool express_poll_settings_save(const ExpressPollSettings* settings);

uint32_t express_poll_settings_interval_ms(const ExpressPollSettings* settings);
