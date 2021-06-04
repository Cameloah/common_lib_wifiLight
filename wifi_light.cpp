//
// Created by koorj on 03.06.2021.
//

#include "wifi_light.h"

state light;
bool inTransition, entertainmentRun, useDhcp = true;
byte mac[6], packetBuffer[8];
unsigned long lastEPMillis;

// settings
char lightName[LIGHT_NAME_MAX_LENGTH] = "New DIY esp32 RGB light";
uint8_t scene = 0, startup = false, onPin = 18, offPin = 19;
bool hwSwitch = false;
uint8_t rgb_multiplier[] = {100, 100, 100}; // light multiplier in percentage /R, G, B/

void apply_scene(uint8_t new_scene) {
    if ( new_scene == 1) {
        light.bri = 254; light.ct = 346; light.colorMode = 2; convert_ct();
    } else if ( new_scene == 2) {
        light.bri = 254; light.ct = 233; light.colorMode = 2; convert_ct();
    }  else if ( new_scene == 3) {
        light.bri = 254; light.ct = 156; light.colorMode = 2; convert_ct();
    }  else if ( new_scene == 4) {
        light.bri = 77; light.ct = 367; light.colorMode = 2; convert_ct();
    }  else if ( new_scene == 5) {
        light.bri = 254; light.ct = 447; light.colorMode = 2; convert_ct();
    }  else if ( new_scene == 6) {
        light.bri = 1; light.x = 0.561; light.y = 0.4042; light.colorMode = 1; convert_xy();
    }  else if ( new_scene == 7) {
        light.bri = 203; light.x = 0.380328; light.y = 0.39986; light.colorMode = 1; convert_xy();
    }  else if ( new_scene == 8) {
        light.bri = 112; light.x = 0.359168; light.y = 0.28807; light.colorMode = 1; convert_xy();
    }  else if ( new_scene == 9) {
        light.bri = 142; light.x = 0.267102; light.y = 0.23755; light.colorMode = 1; convert_xy();
    }  else if ( new_scene == 10) {
        light.bri = 216; light.x = 0.393209; light.y = 0.29961; light.colorMode = 1; convert_xy();
    }  else {
        light.bri = 144; light.ct = 447; light.colorMode = 2; convert_ct();
    }
}

void processLightdata(float transitiontime) {
    if (light.colorMode == 1 && light.lightState == true) {
        convert_xy();
    } else if (light.colorMode == 2 && light.lightState == true) {
        convert_ct();
    } else if (light.colorMode == 3 && light.lightState == true) {
        convert_hue();
    }
    transitiontime *= 16;
    for (uint8_t color = 0; color < PWM_CHANNELS; color++) {
        if (light.lightState) {
            light.stepLevel[color] = (light.colors[color] - light.currentColors[color]) / transitiontime;
        } else {
            light.stepLevel[color] = light.currentColors[color] / transitiontime;
        }
    }
}

void lightEngine() {
    for (uint8_t color = 0; color < PWM_CHANNELS; color++) {
        if (light.lightState) {
            if (light.colors[color] != light.currentColors[color] ) {
                inTransition = true;
                light.currentColors[color] += light.stepLevel[color];
                if ((light.stepLevel[color] > 0.0f && light.currentColors[color] > light.colors[color]) || (light.stepLevel[color] < 0.0f && light.currentColors[color] < light.colors[color])) light.currentColors[color] = light.colors[color];
                analogWrite(pins[color], (int)(light.currentColors[color] * 4.0));
            }
        } else {
            if (light.currentColors[color] != 0) {
                inTransition = true;
                light.currentColors[color] -= light.stepLevel[color];
                if (light.currentColors[color] < 0.0f) light.currentColors[color] = 0;
                analogWrite(pins[color], (int)(light.currentColors[color] * 4.0));
            }
        }
    }
    if (inTransition) {
        delay(6);
        inTransition = false;
    } else if (hwSwitch == true) {
        if (digitalRead(onPin) == HIGH) {
            int i = 0;
            while (digitalRead(onPin) == HIGH && i < 30) {
                delay(20);
                i++;
            }
            if (i < 30) {
                // there was a short press
                light.lightState = true;
            }
            else {
                // there was a long press
                light.bri += 56;
                if (light.bri > 254) {
                    // don't increase the brightness more then maximum value
                    light.bri = 254;
                }
            }
            processLightdata(4);
        } else if (digitalRead(offPin) == HIGH) {
            int i = 0;
            while (digitalRead(offPin) == HIGH && i < 30) {
                delay(20);
                i++;
            }
            if (i < 30) {
                // there was a short press
                light.lightState = false;
            }
            else {
                // there was a long press
                light.bri -= 56;
                if (light.bri < 1) {
                    // don't decrease the brightness less than minimum value.
                    light.bri = 1;
                }
            }
            processLightdata(4);
        }
    }
}


void saveState() {
    DynamicJsonDocument json(1024);
    json["on"] = light.lightState;
    json["bri"] = light.bri;
    if (light.colorMode == 1) {
        JsonArray xy = json.createNestedArray("xy");
        xy.add(light.x);
        xy.add(light.y);
    } else if (light.colorMode == 2) {
        json["ct"] = light.ct;
    } else if (light.colorMode == 3) {
        json["hue"] = light.hue;
        json["sat"] = light.sat;
    }
    File stateFile = SPIFFS.open("/state.json", "w");
    serializeJson(json, stateFile);
}


void restoreState() {
    File stateFile = SPIFFS.open("/state.json", "r");
    if (!stateFile) {
        saveState();
        return;
    }

    DynamicJsonDocument json(1024);
    DeserializationError error = deserializeJson(json, stateFile.readString());
    if (error) {
        //Serial.println("Failed to parse config file");
        return;
    }
    light.lightState = json["on"];
    light.bri = (uint8_t)json["bri"];
    if (json.containsKey("xy")) {
        light.x = json["xy"][0];
        light.y = json["xy"][1];
        light.colorMode = 1;
    } else if (json.containsKey("ct")) {
        light.ct = json["ct"];
        light.colorMode = 2;
    } else {
        if (json.containsKey("hue")) {
            light.hue = json["hue"];
            light.colorMode = 3;
        }
        if (json.containsKey("sat")) {
            light.sat = (uint8_t) json["sat"];
            light.colorMode = 3;
        }
    }
}


bool saveConfig() {
    DynamicJsonDocument json(1024);
    json["name"] = lightName;
    json["startup"] = startup;
    json["scene"] = scene;
    json["r"] = pins[0];
    json["g"] = pins[1];
    json["b"] = pins[2];
    json["rpct"] = rgb_multiplier[0];
    json["gpct"] = rgb_multiplier[1];
    json["bpct"] = rgb_multiplier[2];
    json["on"] = onPin;
    json["off"] = offPin;
    json["hw"] = hwSwitch;
    json["dhcp"] = useDhcp;
    JsonArray addr = json.createNestedArray("addr");
    addr.add(address[0]);
    addr.add(address[1]);
    addr.add(address[2]);
    addr.add(address[3]);
    JsonArray gw = json.createNestedArray("gw");
    gw.add(gateway[0]);
    gw.add(gateway[1]);
    gw.add(gateway[2]);
    gw.add(gateway[3]);
    JsonArray mask = json.createNestedArray("mask");
    mask.add(submask[0]);
    mask.add(submask[1]);
    mask.add(submask[2]);
    mask.add(submask[3]);
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        //Serial.println("Failed to open config file for writing");
        return false;
    }

    serializeJson(json, configFile);
    return true;
}

bool loadConfig() {
    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) {
        //Serial.println("Create new file with default values");
        return saveConfig();
    }

    size_t size = configFile.size();
    if (size > 1024) {
        //Serial.println("Config file size is too large");
        return false;
    }

    if (configFile.size() > 1024) {
        Serial.println("Config file size is too large");
        return false;
    }

    DynamicJsonDocument json(1024);
    DeserializationError error = deserializeJson(json, configFile.readString());
    if (error) {
        //Serial.println("Failed to parse config file");
        return false;
    }

    strcpy(lightName, json["name"]);
    startup = (uint8_t) json["startup"];
    scene  = (uint8_t) json["scene"];
    pins[0] = (uint8_t) json["r"];
    pins[1] = (uint8_t) json["g"];
    pins[2] = (uint8_t) json["b"];
    rgb_multiplier[0] = (uint8_t) json["rpct"];
    rgb_multiplier[1] = (uint8_t) json["gpct"];
    rgb_multiplier[2] = (uint8_t) json["bpct"];
    onPin = (uint8_t) json["on"];
    offPin = (uint8_t) json["off"];
    hwSwitch = json["hw"];
    useDhcp = json["dhcp"];
    address = {json["addr"][0], json["addr"][1], json["addr"][2], json["addr"][3]};
    submask = {json["mask"][0], json["mask"][1], json["mask"][2], json["mask"][3]};
    gateway = {json["gw"][0], json["gw"][1], json["gw"][2], json["gw"][3]};
    return true;
}

void handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
}

void wifi_light_init() {

    // init file system
    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount file system");
        return;
    }

    if (!loadConfig()) {
        Serial.println("Failed to load config");
    } else {
        Serial.println("Config loaded");
    }

    if (startup == 1) {
        light.lightState = true;
    }

    if (startup == 0) {
        restoreState();
    } else {
        apply_scene(scene);
    }

    processLightdata(4);
    if (light.lightState) {
        for (uint8_t i = 0; i < 200; i++) {
            lightEngine();
        }
    }
}