#include "felica_systems.h"

#include <stdio.h>

typedef struct {
    uint16_t code;
    const char* name;
} FelicaSystemName;

/* Names taken from the transit-polling reference tables. Short labels for 128px. */
static const FelicaSystemName felica_system_names[] = {
    {0x0003, "Suica/ICOCA/PASMO"},
    {0x8008, "Octopus"},
    {0xFE00, "Common Area"},
    {0x811D, "Rakuten Edy"},
    {0x12FC, "NFC Type 3/NDEF"},
    {0x88B4, "FeliCa Lite-S"},
    {0x957A, "FeliCa Secure ID"},
    {0xFEE1, "FeliCa Plug"},
    {0x8620, "Blackboard"},
    {0x80DE, "IruCa"},
    {0x83EE, "ayuca"},
    {0x8592, "PASPY"},
    {0x865E, "SAPICA"},
    {0x8B5D, "Ryuto"},
    {0x8FC1, "OKICA"},
    {0x93EC, "N+ Card"},
    {0x90B7, "Kartu Multi Trip"},
    {0x0102, "Legacy EZ-Link"},
    {0xFFFF, "Any System"},
};

const char* felica_system_name(uint16_t system_code) {
    for(size_t i = 0; i < sizeof(felica_system_names) / sizeof(felica_system_names[0]); i++) {
        if(felica_system_names[i].code == system_code) return felica_system_names[i].name;
    }
    return NULL;
}

bool felica_system_code_is_wildcard(uint16_t system_code) {
    return (system_code & 0xFF00U) == 0xFF00U || (system_code & 0x00FFU) == 0x00FFU;
}

void felica_system_code_format(uint16_t system_code, char out[FELICA_SYSTEM_CODE_STR_SIZE]) {
    snprintf(out, FELICA_SYSTEM_CODE_STR_SIZE, "0x%04X", system_code);
}

static int hex_value(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool felica_system_code_parse(const char* text, uint16_t* system_code) {
    if(!text || !system_code) return false;

    while(*text == ' ')
        text++;
    if(text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;

    uint32_t value = 0;
    size_t digits = 0;
    for(; hex_value(*text) >= 0; text++) {
        if(++digits > 4) return false;
        value = (value << 4) | (uint32_t)hex_value(*text);
    }
    while(*text == ' ')
        text++;
    if(digits == 0 || *text != '\0') return false;

    *system_code = (uint16_t)value;
    return true;
}
