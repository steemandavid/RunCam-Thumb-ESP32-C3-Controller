#pragma once

#ifndef UNIT_TEST

#include "config.h"
#include "system_state.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

class OledDisplay {
public:
    bool begin();
    void render(const SystemState& state);

private:
    Adafruit_SSD1306 display_{OLED_WIDTH, OLED_HEIGHT, &Wire, -1};
};

#endif // UNIT_TEST
