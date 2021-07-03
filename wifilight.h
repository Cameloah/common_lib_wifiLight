#include "FastLED.h"

typedef struct {
    CRGB color;                     // new color
    CRGB* currentColor;             // pointer to one led representing the color of that virtual hue-light
    int stepLevel;                  // amount of transition in every loop

    int lightNr;                    // hue light-nr
    bool lightState;                // true = On, false = Off
    uint8_t colorMode;              // 1 = xy, 2 = ct, 3 = hue/sat

    uint8_t bri;                    //brightness (1 - 254)
    int hue;                        // 0 - 65635
    uint8_t sat;                    // 0 - 254
    float x;                        // 0 - 1  x-coordinate of CIE color space
    float y;                        // 0 - 1  y-coordinate of CIE color space
    int ct;                         //color temperatur in mired (500 mired/2000 K - 153 mired/6500 K)
}wifilight_state;

extern wifilight_state* hue_lights;
void hue_main_init();

void hue_main_update();
