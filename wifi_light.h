//
// Created by koorj on 03.06.2021.
//

#pragma once

#include <FS.h>                         // file system manager?

// wifi settings
IPAddress address ( 192,  168,   0,  95); // choose an unique IP Adress
IPAddress gateway ( 192,  168,   0,   1); // Router IP
IPAddress submask (255, 255, 255,   0);

#define LIGHT_VERSION 3.0
#define PWM_CHANNELS 3
#define LIGHT_NAME_MAX_LENGTH 32 // Longer name will get stripped
#define ENTERTAINMENT_TIMEOUT 1500 // millis

struct state {
    uint8_t colors[PWM_CHANNELS], bri = 100, sat = 254, colorMode = 2;
    bool lightState;
    int ct = 200, hue;
    float stepLevel[PWM_CHANNELS], currentColors[PWM_CHANNELS], x, y;
};


void wifi_light_init();