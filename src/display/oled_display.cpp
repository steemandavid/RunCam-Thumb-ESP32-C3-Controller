#ifndef UNIT_TEST

#include "display/oled_display.h"
#include "config.h"
#include <cstdio>

bool OledDisplay::begin() {
    Wire.begin(GPIO_OLED_SDA, GPIO_OLED_SCL);
    if (!display_.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        return false;
    }
    display_.setTextColor(SSD1306_WHITE);
    display_.clearDisplay();
    display_.display();
    return true;
}

void OledDisplay::render(const SystemState& state) {
    display_.clearDisplay();

    switch (state.flightState) {
        case FlightState::IDLE:
            if (!state.cameraCommsOk) {
                display_.setTextSize(2);
                display_.setCursor((OLED_WIDTH - 4 * 12) / 2, 12);
                display_.print("ERR!");
            } else {
                display_.setTextSize(2);
                display_.setCursor((OLED_WIDTH - 4 * 12) / 2, 12);
                display_.print("IDLE");
                display_.setTextSize(1);
                display_.setCursor((OLED_WIDTH - 11 * 6) / 2, 2);
                display_.print("192.168.4.1");
            }
            break;

        case FlightState::RECORDING: {
            // Counter at size 2 in the top visible band (GDDRAM y=12 → physical top)
            char timeBuf[8];
            snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu",
                (unsigned long)(state.recordingSeconds / 60),
                (unsigned long)(state.recordingSeconds % 60));
            display_.setTextSize(2);
            display_.setCursor((OLED_WIDTH - 5 * 12) / 2, 12);
            display_.print(timeBuf);

            // "REC" label at size 1 in the bottom visible band (GDDRAM y=0 → physical bottom)
            display_.setTextSize(1);
            display_.setCursor((OLED_WIDTH - 3 * 6) / 2, 2);
            display_.print("REC");
            break;
        }
    }

    display_.display();
}

#endif // UNIT_TEST
