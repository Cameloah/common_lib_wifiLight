#include <WebServer.h>
#include <ESP_WiFiManager.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include "wifilight.h"



// define details of virtual hue-lights -- adapt to your needs!
char lightName[LIGHT_NAME_MAX_LENGTH] = "Hue SK9822 FastLED strip";   //max 32 characters!!!
uint8_t HUE_LightsCount = 2;                     //number of emulated hue-lights
uint8_t HUE_PixelPerLight = 15;                  //number of leds forming one emulated hue-light
uint8_t HUE_TransitionLeds = 1;                 //number of 'space'-leds inbetween the emulated hue-lights; pixelCount must be divisible by this value
uint8_t HUE_ColorCorrectionRGB[3] = {100, 100, 100};  // light multiplier in percentage /R, G, B/

// hue variables
uint8_t scene;
uint8_t startup;
bool hwSwitch = false;
uint8_t pin_hws_on = PIN_HWSWITCH_ON;
uint8_t pin_hws_off = PIN_HWSWITCH_OFF;
uint16_t dividedLightsArray[30];

wifilight_state_t lights[10];

// wifi settings
bool useDhcp = false;
IPAddress address ( 192,  168,   0,  82);     // choose an unique IP Adress
IPAddress gateway ( 192,  168,   0,   1);     // Router IP
IPAddress submask(255, 255, 255,   0);
byte mac[6]; // to hold  the wifi mac address
WebServer websrv(80);
Preferences Conf;
WiFiClient net;


void Log(String msg) {
    //Serial.println(msg);
}

String WebLog(String message) {

    message += "URI: " + websrv.uri();
    message += "\r\n Method: " + (websrv.method() == HTTP_GET) ? "GET" : "POST";
    message += "\r\n Arguments: " + websrv.args(); + "\r\n";
    for (uint8_t i = 0; i < websrv.args(); i++) {
        message += " " + websrv.argName(i) + ": " + websrv.arg(i) + " \r\n";
    }

    Log(message);
    return message;
}

void convertHue(uint8_t light) // convert hue / sat values from HUE API to RGB
{
    double      hh, p, q, t, ff, s, v;
    long        i;

    s = lights[light].sat / 255.0;
    v = lights[light].bri / 255.0;

    if (s <= 0.0) {      // < is bogus, just shuts up warnings
        lights[light].colors[0] = v;
        lights[light].colors[1] = v;
        lights[light].colors[2] = v;
        return;
    }
    hh = lights[light].hue;
    if (hh >= 65535.0) hh = 0.0;
    hh /= 11850, 0;
    i = (long)hh;
    ff = hh - i;
    p = v * (1.0 - s);
    q = v * (1.0 - (s * ff));
    t = v * (1.0 - (s * (1.0 - ff)));

    switch (i) {
        case 0:
            lights[light].colors[0] = v * 255.0;
            lights[light].colors[1] = t * 255.0;
            lights[light].colors[2] = p * 255.0;
            break;
        case 1:
            lights[light].colors[0] = q * 255.0;
            lights[light].colors[1] = v * 255.0;
            lights[light].colors[2] = p * 255.0;
            break;
        case 2:
            lights[light].colors[0] = p * 255.0;
            lights[light].colors[1] = v * 255.0;
            lights[light].colors[2] = t * 255.0;
            break;

        case 3:
            lights[light].colors[0] = p * 255.0;
            lights[light].colors[1] = q * 255.0;
            lights[light].colors[2] = v * 255.0;
            break;
        case 4:
            lights[light].colors[0] = t * 255.0;
            lights[light].colors[1] = p * 255.0;
            lights[light].colors[2] = v * 255.0;
            break;
        case 5:
        default:
            lights[light].colors[0] = v * 255.0;
            lights[light].colors[1] = p * 255.0;
            lights[light].colors[2] = q * 255.0;
            break;
    }

}

void convertXy(uint8_t light) // convert CIE xy values from HUE API to RGB
{
    int optimal_bri = lights[light].bri;
    if (optimal_bri < 5) {
        optimal_bri = 5;
    }
    float Y = lights[light].y;
    float X = lights[light].x;
    float Z = 1.0f - lights[light].x - lights[light].y;

    // sRGB D65 conversion
    float r =  X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
    float g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
    float b =  X * 0.0557f - Y * 0.2040f + Z * 1.0570f;


    // Apply gamma correction
    r = r <= 0.0031308f ? 12.92f * r : (1.0f + 0.055f) * pow(r, (1.0f / 2.4f)) - 0.055f;
    g = g <= 0.0031308f ? 12.92f * g : (1.0f + 0.055f) * pow(g, (1.0f / 2.4f)) - 0.055f;
    b = b <= 0.0031308f ? 12.92f * b : (1.0f + 0.055f) * pow(b, (1.0f / 2.4f)) - 0.055f;

    // Apply multiplier for white correction
    r = r * HUE_ColorCorrectionRGB[0] / 100;
    g = g * HUE_ColorCorrectionRGB[1] / 100;
    b = b * HUE_ColorCorrectionRGB[2] / 100;

    if (r > b && r > g) {
        // red is biggest
        if (r > 1.0f) {
            g = g / r;
            b = b / r;
            r = 1.0f;
        }
    }
    else if (g > b && g > r) {
        // green is biggest
        if (g > 1.0f) {
            r = r / g;
            b = b / g;
            g = 1.0f;
        }
    }
    else if (b > r && b > g) {
        // blue is biggest
        if (b > 1.0f) {
            r = r / b;
            g = g / b;
            b = 1.0f;
        }
    }

    r = r < 0 ? 0 : r;
    g = g < 0 ? 0 : g;
    b = b < 0 ? 0 : b;

    lights[light].colors[0] = (int) (r * optimal_bri); lights[light].colors[1] = (int) (g * optimal_bri); lights[light].colors[2] = (int) (b * optimal_bri);
}

void convertCt(uint8_t light) // convert ct (color temperature) value from HUE API to RGB
{
    int hectemp = 10000 / lights[light].ct;
    int r, g, b;
    if (hectemp <= 66) {
        r = 255;
        g = 99.4708025861 * log(hectemp) - 161.1195681661;
        b = hectemp <= 19 ? 0 : (138.5177312231 * log(hectemp - 10) - 305.0447927307);
    } else {
        r = 329.698727446 * pow(hectemp - 60, -0.1332047592);
        g = 288.1221695283 * pow(hectemp - 60, -0.0755148492);
        b = 255;
    }

    r = r > 255 ? 255 : r;
    g = g > 255 ? 255 : g;
    b = b > 255 ? 255 : b;

    // Apply multiplier for white correction
    r = r * HUE_ColorCorrectionRGB[0] / 100;
    g = g * HUE_ColorCorrectionRGB[1] / 100;
    b = b * HUE_ColorCorrectionRGB[2] / 100;

    lights[light].colors[0] = r * (lights[light].bri / 255.0f); lights[light].colors[1] = g * (lights[light].bri / 255.0f); lights[light].colors[2] = b * (lights[light].bri / 255.0f);
}

void infoLight(CRGB color, CRGBSet& leds) {

    // Flash the strip in the selected color. White = booted, green = WLAN connected, red = WLAN could not connect
    for (int i = 0; i < 30; i++) {
        leds = color;
        FastLED.show();
        leds.fadeToBlackBy(10);
    }
    leds = CRGB(CRGB::Black);
    FastLED.show();
}

void processLightdata(uint8_t light, float transitiontime) { // calculate the step level of every RGB channel for a smooth transition in requested transition time
    if (lights[light].colorMode == 1 && lights[light].lightState == true) {
        convertXy(light);
    } else if (lights[light].colorMode == 2 && lights[light].lightState == true) {
        convertCt(light);
    } else if (lights[light].colorMode == 3 && lights[light].lightState == true) {
        convertHue(light);
    }
    for (uint8_t i = 0; i < 3; i++) {
        if (lights[light].lightState) {
            lights[light].stepLevel[i] = ((float)lights[light].colors[i] - lights[light].currentColors[i]) / transitiontime;
        } else {
            lights[light].stepLevel[i] = lights[light].currentColors[i] / transitiontime;
        }
    }
}

void saveState() {

    String Output;
    DynamicJsonDocument json(1024);
    JsonObject light;

    for (uint8_t i = 0; i < HUE_LightsCount; i++) {
        light = json.createNestedObject((String)i);
        light["on"] = lights[i].lightState;
        light["bri"] = lights[i].bri;
        if (lights[i].colorMode == 1) {
            light["x"] = lights[i].x;
            light["y"] = lights[i].y;
        } else if (lights[i].colorMode == 2) {
            light["ct"] = lights[i].ct;
        } else if (lights[i].colorMode == 3) {
            light["hue"] = lights[i].hue;
            light["sat"] = lights[i].sat;
        }
    }

    serializeJson(json, Output);
    Conf.putString("StateJson", Output);
}

void restoreState() {

    String Input;
    DynamicJsonDocument json(1024);
    DeserializationError error;
    JsonObject values;
    const char* key;
    int lightId;

    Input = Conf.getString("StateJson");

    error = deserializeJson(json, Input);
    if (error) {
        Log("Failed to parse config file");
        return;
    }
    for (JsonPair state : json.as<JsonObject>()) {
        key = state.key().c_str();
        lightId = atoi(key);
        values = state.value();
        lights[lightId].lightState = values["on"];
        lights[lightId].bri = (uint8_t)values["bri"];
        if (values.containsKey("x")) {
            lights[lightId].x = values["x"];
            lights[lightId].y = values["y"];
            lights[lightId].colorMode = 1;
        } else if (values.containsKey("ct")) {
            lights[lightId].ct = values["ct"];
            lights[lightId].colorMode = 2;
        } else {
            if (values.containsKey("hue")) {
                lights[lightId].hue = values["hue"];
                lights[lightId].colorMode = 3;
            }
            if (values.containsKey("sat")) {
                lights[lightId].sat = (uint8_t) values["sat"];
                lights[lightId].colorMode = 3;
            }
        }
    }

    Log("Restored previous state.");
}

void saveConfig() {

    String Output;
    DynamicJsonDocument json(1024);
    JsonArray addr, gw, mask;

    json["name"] = lightName;
    json["startup"] = startup;
    json["scene"] = scene;
    json["on"] = pin_hws_on;
    json["off"] = pin_hws_off;
    json["hw"] = hwSwitch;
    json["dhcp"] = useDhcp;
    json["lightsCount"] = HUE_LightsCount;
    json["pixelCount"] = HUE_PixelPerLight;
    json["transLeds"] = HUE_TransitionLeds;
    json["rpct"] = HUE_ColorCorrectionRGB[0];
    json["gpct"] = HUE_ColorCorrectionRGB[1];
    json["bpct"] = HUE_ColorCorrectionRGB[2];
    addr = json.createNestedArray("addr");
    addr.add(address[0]);
    addr.add(address[1]);
    addr.add(address[2]);
    addr.add(address[3]);
    gw = json.createNestedArray("gw");
    gw.add(gateway[0]);
    gw.add(gateway[1]);
    gw.add(gateway[2]);
    gw.add(gateway[3]);
    mask = json.createNestedArray("mask");
    mask.add(submask[0]);
    mask.add(submask[1]);
    mask.add(submask[2]);
    mask.add(submask[3]);

    serializeJson(json, Output);
    Conf.putString("ConfJson", Output);

    Log("saveConfig: " + Output);
}

bool loadConfig() {

    String Input;
    DynamicJsonDocument json(1024);
    DeserializationError error;

    Input = Conf.getString("ConfJson");

    Log("loadConfig: " + Input);

    error = deserializeJson(json, Input);
    if (error) {
        //Serial.println("Failed to parse config file");
        return false;
    }

    strcpy(lightName, json["name"]);
    startup = json["startup"].as<uint8_t>();
    scene  = json["scene"].as<uint8_t>();
    pin_hws_on = (uint8_t) json["on"];
    pin_hws_off = (uint8_t) json["off"];
    hwSwitch = json["hw"];
    HUE_LightsCount = json["lightsCount"].as<uint16_t>();
    HUE_PixelPerLight = json["pixelCount"].as<uint16_t>();
    HUE_TransitionLeds = json["transLeds"].as<uint8_t>();

    if (json.containsKey("rpct")) {
        HUE_ColorCorrectionRGB[0] = json["rpct"].as<uint8_t>();
        HUE_ColorCorrectionRGB[1] = json["gpct"].as<uint8_t>();
        HUE_ColorCorrectionRGB[2] = json["bpct"].as<uint8_t>();
    }
    useDhcp = json["dhcp"];
    address = {json["addr"][0], json["addr"][1], json["addr"][2], json["addr"][3]};
    submask = {json["mask"][0], json["mask"][1], json["mask"][2], json["mask"][3]};
    gateway = {json["gw"][0], json["gw"][1], json["gw"][2], json["gw"][3]};

    return true;
}

void websrvDetect() {
    char macString[32] = {0};
    sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    DynamicJsonDocument root(1024);
    root["name"] = lightName;
    root["lights"] = HUE_LightsCount;
    root["protocol"] = "native_multi";
    root["modelid"] = "LST002";
    root["type"] = "ws2812_strip";
    root["mac"] = String(macString);
    root["version"] = LIGHT_VERSION;
    String output;
    serializeJson(root, output);
    websrv.send(200, "text/plain", output);
}

void websrvStateGet() {
    uint8_t light = websrv.arg("light").toInt() - 1;
    DynamicJsonDocument root(1024);
    root["on"] = lights[light].lightState;
    root["bri"] = lights[light].bri;
    JsonArray xy = root.createNestedArray("xy");
    xy.add(lights[light].x);
    xy.add(lights[light].y);
    root["ct"] = lights[light].ct;
    root["hue"] = lights[light].hue;
    root["sat"] = lights[light].sat;
    if (lights[light].colorMode == 1)
        root["colormode"] = "xy";
    else if (lights[light].colorMode == 2)
        root["colormode"] = "ct";
    else if (lights[light].colorMode == 3)
        root["colormode"] = "hs";
    String output;
    serializeJson(root, output);
    websrv.send(200, "text/plain", output);
}

void websrvStatePut() {
    bool stateSave = false;
    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, websrv.arg("plain"));

    if (error) {
        websrv.send(404, "text/plain", "FAIL. " + websrv.arg("plain"));
    } else {
        for (JsonPair state : root.as<JsonObject>()) {
            const char* key = state.key().c_str();
            int light = atoi(key) - 1;
            JsonObject values = state.value();
            int transitiontime = 50;

            if (values.containsKey("xy")) {
                lights[light].x = values["xy"][0];
                lights[light].y = values["xy"][1];
                lights[light].colorMode = 1;
            } else if (values.containsKey("ct")) {
                lights[light].ct = values["ct"];
                lights[light].colorMode = 2;
            } else {
                if (values.containsKey("hue")) {
                    lights[light].hue = values["hue"];
                    lights[light].colorMode = 3;
                }
                if (values.containsKey("sat")) {
                    lights[light].sat = values["sat"];
                    lights[light].colorMode = 3;
                }
            }

            if (values.containsKey("on")) {
                if (values["on"]) {
                    lights[light].lightState = true;
                } else {
                    lights[light].lightState = false;
                }
                if (startup == 0) {
                    stateSave = true;
                }
            }

            if (values.containsKey("bri")) {
                lights[light].bri = values["bri"];
            }

            if (values.containsKey("bri_inc")) {
                lights[light].bri += (int) values["bri_inc"];
                if (lights[light].bri > 255) lights[light].bri = 255;
                else if (lights[light].bri < 1) lights[light].bri = 1;
            }

            if (values.containsKey("transitiontime")) {
                transitiontime = values["transitiontime"];
            }

            if (values.containsKey("alert") && values["alert"] == "select") {
                if (lights[light].lightState) {
                    lights[light].currentColors[0] = 0; lights[light].currentColors[1] = 0; lights[light].currentColors[2] = 0;
                } else {
                    lights[light].currentColors[1] = 126; lights[light].currentColors[2] = 126;
                }
            }
            processLightdata(light, transitiontime);
        }
        String output;
        serializeJson(root, output);
        websrv.send(200, "text/plain", output);
        if (stateSave) {
            saveState();
        }
    }
}

void websrvReset() {
    websrv.send(200, "text/html", "reset");
    Log("Restart");
    delay(1000);
    esp_restart();
}

void websrvRoot() {

    Log("StateRoot: " + websrv.uri());

    if (websrv.arg("section").toInt() == 1) {
        websrv.arg("name").toCharArray(lightName, LIGHT_NAME_MAX_LENGTH);
        startup = websrv.arg("startup").toInt();
        scene = websrv.arg("scene").toInt();
        HUE_LightsCount = websrv.arg("lightscount").toInt();
        HUE_PixelPerLight = websrv.arg("pixelcount").toInt();
        HUE_TransitionLeds = websrv.arg("transitionleds").toInt();
        HUE_ColorCorrectionRGB[0] = websrv.arg("rpct").toInt();
        HUE_ColorCorrectionRGB[1] = websrv.arg("gpct").toInt();
        HUE_ColorCorrectionRGB[2] = websrv.arg("bpct").toInt();
        hwSwitch = websrv.hasArg("hwswitch") ? websrv.arg("hwswitch").toInt() : 0;
        pin_hws_on = websrv.arg("on").toInt();
        pin_hws_off = websrv.arg("off").toInt();

        saveConfig();
    } else if (websrv.arg("section").toInt() == 2) {
        useDhcp = (!websrv.hasArg("disdhcp")) ? 1 : websrv.arg("disdhcp").toInt();
        if (websrv.hasArg("disdhcp")) {
            address.fromString(websrv.arg("addr"));
            gateway.fromString(websrv.arg("gw"));
            submask.fromString(websrv.arg("sm"));
        }
        saveConfig();
    }

    String htmlContent = "<!DOCTYPE html> <html> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>Hue Light</title> <link href=\"https://fonts.googleapis.com/icon?family=Material+Icons\" rel=\"stylesheet\"> <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css\"> <link rel=\"stylesheet\" href=\"https://cerny.in/nouislider.css\"/> </head> <body> <div class=\"wrapper\"> <nav class=\"nav-extended row deep-purple\"> <div class=\"nav-wrapper col s12\"> <a href=\"#\" class=\"brand-logo\">DiyHue</a> <ul id=\"nav-mobile\" class=\"right hide-on-med-and-down\" style=\"position: relative;z-index: 10;\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\"><i class=\"material-icons left\">language</i>GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\"><i class=\"material-icons left\">description</i>Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\" ><i class=\"material-icons left\">question_answer</i>Slack channel</a></li> </ul> </div> <div class=\"nav-content\"> <ul class=\"tabs tabs-transparent\"> <li class=\"tab\"><a class=\"active\" href=\"#test1\">Home</a></li> <li class=\"tab\"><a href=\"#test2\">Preferences</a></li> <li class=\"tab\"><a href=\"#test3\">Network settings</a></li> </ul> </div> </nav> <ul class=\"sidenav\" id=\"mobile-demo\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\">GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\">Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\" >Slack channel</a></li> </ul> <div class=\"container\"> <div class=\"section\"> <div id=\"test1\" class=\"col s12\"> <form> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s10\"> <label for=\"power\">Power</label> <div id=\"power\" class=\"switch section\"> <label> Off <input type=\"checkbox\" name=\"pow\" id=\"pow\" value=\"1\"> <span class=\"lever\"></span> On </label> </div> </div> </div> <div class=\"row\"> <div class=\"col s12 m10\"> <label for=\"bri\">Brightness</label> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\"/> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"hue\">Color</label> <div> <canvas id=\"hue\" width=\"320px\" height=\"320px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"ct\">Color Temp</label> <div> <canvas id=\"ct\" width=\"320px\" height=\"50px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> </form> </div> <div id=\"test2\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s12\"> <label for=\"name\">Light Name</label> <input type=\"text\" id=\"name\" name=\"name\"> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"startup\">Default Power:</label> <select name=\"startup\" id=\"startup\"> <option value=\"0\">Last State</option> <option value=\"1\">On</option> <option value=\"2\">Off</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"scene\">Default Scene:</label> <select name=\"scene\" id=\"scene\"> <option value=\"0\">Relax</option> <option value=\"1\">Read</option> <option value=\"2\">Concentrate</option> <option value=\"3\">Energize</option> <option value=\"4\">Bright</option> <option value=\"5\">Dimmed</option> <option value=\"6\">Nightlight</option> <option value=\"7\">Savanna sunset</option> <option value=\"8\">Tropical twilight</option> <option value=\"9\">Arctic aurora</option> <option value=\"10\">Spring blossom</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"pixelcount\" class=\"col-form-label\">Pixel count</label> <input type=\"number\" id=\"pixelcount\" name=\"pixelcount\"> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"lightscount\" class=\"col-form-label\">Lights count</label> <input type=\"number\" id=\"lightscount\" name=\"lightscount\"> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"transitionleds\">Transition leds:</label> <select name=\"transitionleds\" id=\"transitionleds\"> <option value=\"0\">0</option> <option value=\"2\">2</option> <option value=\"4\">4</option> <option value=\"6\">6</option> <option value=\"8\">8</option> <option value=\"10\">10</option> <option value=\"12\">12</option> <option value=\"14\">14</option> <option value=\"16\">16</option> <option value=\"18\">18</option> <option value=\"20\">20</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"rpct\" class=\"form-label\">Red multiplier</label> <input type=\"number\" id=\"rpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"rpct\" value=\"\"/> </div> <div class=\"col s4 m3\"> <label for=\"gpct\" class=\"form-label\">Green multiplier</label> <input type=\"number\" id=\"gpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"gpct\" value=\"\"/> </div> <div class=\"col s4 m3\"> <label for=\"bpct\" class=\"form-label\">Blue multiplier</label> <input type=\"number\" id=\"bpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"bpct\" value=\"\"/> </div> </div> <div class=\"row\"><label class=\"control-label col s10\">HW buttons:</label> <div class=\"col s10\"> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"hwswitch\" id=\"hwswitch\" value=\"1\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"on\">On Pin</label> <input type=\"number\" id=\"on\" name=\"on\"> </div> <div class=\"col s4 m3\"> <label for=\"off\">Off Pin</label> <input type=\"number\" id=\"off\" name=\"off\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> <div id=\"test3\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"2\"> <div class=\"row\"> <div class=\"col s12\"> <label class=\"control-label\">Manual IP assignment:</label> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"disdhcp\" id=\"disdhcp\" value=\"0\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s12 m3\"> <label for=\"addr\">Ip</label> <input type=\"text\" id=\"addr\" name=\"addr\"> </div> <div class=\"col s12 m3\"> <label for=\"sm\">Submask</label> <input type=\"text\" id=\"sm\" name=\"sm\"> </div> <div class=\"col s12 m3\"> <label for=\"gw\">Gateway</label> <input type=\"text\" id=\"gw\" name=\"gw\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> </div> </div> </div> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/jquery/3.4.1/jquery.min.js\"></script> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js\"></script> <script src=\"https://cerny.in/nouislider.js\"></script> <script src=\"https://cerny.in/diyhue.js\"></script> </body> </html>";

    websrv.send(200, "text/html", htmlContent);
    if (websrv.args()) {
        websrvReset();
    }
}

void websrvConfig() {

    DynamicJsonDocument root(1024);
    String output;

    root["name"] = lightName;
    root["scene"] = scene;
    root["startup"] = startup;
    root["hw"] = hwSwitch;
    root["on"] = pin_hws_on;
    root["off"] = pin_hws_off;
    root["hwswitch"] = (int)hwSwitch;
    root["lightscount"] = HUE_LightsCount;
    for (uint8_t i = 0; i < HUE_LightsCount; i++) {
        root["dividedLight_" + String(i)] = (int)dividedLightsArray[i];
    }
    root["pixelcount"] = HUE_PixelPerLight;
    root["transitionleds"] = HUE_TransitionLeds;
    root["rpct"] = HUE_ColorCorrectionRGB[0];
    root["gpct"] = HUE_ColorCorrectionRGB[1];
    root["bpct"] = HUE_ColorCorrectionRGB[2];
    root["disdhcp"] = (int)!useDhcp;
    root["addr"] = (String)address[0] + "." + (String)address[1] + "." + (String)address[2] + "." + (String)address[3];
    root["gw"] = (String)gateway[0] + "." + (String)gateway[1] + "." + (String)gateway[2] + "." + (String)gateway[3];
    root["sm"] = (String)submask[0] + "." + (String)submask[1] + "." + (String)submask[2] + "." + (String)submask[3];

    serializeJson(root, output);
    websrv.send(200, "text/plain", output);
    Log("Config: " + output);
}

void websrvNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += websrv.uri();
    message += "\nMethod: ";
    message += (websrv.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += websrv.args();
    message += "\n";
    Serial.println(message);
    websrv.send(404, "text/plain", WebLog("File Not Found\n\n"));
}

void wifilight_init(CRGBSet& user_leds) {

    // initiate file system and load config if available
    Conf.begin("HueLED", false);
    if (loadConfig() == false) {
        Log("No config data. Creating config data.");
        saveConfig();
        loadConfig();
    }
    restoreState();

    // initialize wifi
    ESP_WiFiManager wifiManager;
    if (!useDhcp) {
        wifiManager.setSTAStaticIPConfig(address, gateway, submask);
    }
    wifiManager.autoConnect("New ESP32 Light");
    if (useDhcp) {
        address = WiFi.localIP();
        gateway = WiFi.gatewayIP();
        submask = WiFi.subnetMask();
    }

    infoLight(CRGB::White, user_leds);
    while (WiFi.status() != WL_CONNECTED) {
        infoLight(CRGB::Red, user_leds);
        delay(500);
    }
    // Show that we are connected
    infoLight(CRGB::Green, user_leds);

    WiFi.macAddress(mac);         //gets the mac-address

    if (hwSwitch == true) {
        pinMode(pin_hws_on, INPUT);
        pinMode(pin_hws_off, INPUT);
    }

    websrv.on("/state", HTTP_PUT, websrvStatePut);
    websrv.on("/state", HTTP_GET, websrvStateGet);
    websrv.on("/detect", websrvDetect);

    websrv.on("/config", websrvConfig);
    websrv.on("/", websrvRoot);
    websrv.on("/reset", websrvReset);
    websrv.onNotFound(websrvNotFound);

    websrv.on("/text_sensor/light_id", []() {
        websrv.send(200, "text/plain", "1");
    });

    websrv.begin();

    Log("Up and running.");
}

void wifilight_update() {
    websrv.handleClient();

    String debug_output = "State: " + String(lights[1].lightState);
    debug_output += ", RGB: " + String(lights[1].colors[0]);
    debug_output += " " + String(lights[1].colors[1]);
    debug_output += " " + String(lights[1].colors[2]);
    debug_output += ", current RGB: " + String(lights[1].currentColors[0]);
    debug_output += " " + String(lights[1].currentColors[1]);
    debug_output += " " + String(lights[1].currentColors[2]);
    debug_output += ", xy: " + String(lights[1].x);
    debug_output += " " + String(lights[1].y);
    debug_output += ", Bri: " + String(lights[1].bri);
    debug_output += ", ct: " + String(lights[1].ct);
    debug_output += ", hue: " + String(lights[1].hue);
    debug_output += ", steps: " + String(lights[1].stepLevel[0]);
    debug_output += " " + String(lights[1].stepLevel[1]);
    debug_output += " " + String(lights[1].stepLevel[2]);

    Log(debug_output);
}
