#include "FastLED.h"
// hardware defines
#define USE_HARDWARE_SWITCH false // To control on/off state and brightness using GPIO/Pushbutton, set this value to true.
//For GPIO based on/off and brightness control, pull the following pins to ground using 10k resistor
#define PIN_HWSWITCH_ON 4 // on and brightness up
#define PIN_HWSWITCH_OFF 5 // off and brightness down

// hue light defines
#define LIGHT_VERSION 3.1
#define LIGHT_NAME_MAX_LENGTH 32 // Longer name will get stripped

typedef struct {
    uint8_t colors[3];
    uint8_t bri;
    uint8_t sat;
    uint8_t colorMode;
    bool lightState;
    uint16_t ct;
    int hue;
    float stepLevel[3];
    float currentColors[3], x, y;
} wifilight_state_t;


extern wifilight_state_t lights[];


void wifilight_init(CRGBSet& user_leds);

void wifilight_update();
