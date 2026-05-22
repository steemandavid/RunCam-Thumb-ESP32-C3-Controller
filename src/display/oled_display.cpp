#ifndef UNIT_TEST

#include "display/oled_display.h"
#include "config.h"

bool OledDisplay::begin() {
    Wire.begin(GPIO_OLED_SDA, GPIO_OLED_SCL);
    if (!display_.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        return false;
    }
    display_.setTextSize(1);
    display_.setTextColor(SSD1306_WHITE);
    display_.clearDisplay();
    return true;
}

void OledDisplay::render(const SystemState& state) {
    display_.clearDisplay();
    display_.setCursor(0, 0);

    const char* row1 = "";
    const char* row2 = "";

    if (!state.preflightDone) {
        row1 = "RunCam Ctrl";
        row2 = "Init...";
    } else if (!state.preflightPassed && state.cameraCommsOk) {
        row1 = "PREFLT FAIL";
        row2 = state.preflightFailItems;
    } else {
        switch (state.flightState) {
            case FlightState::IDLE:
                if (!state.cameraCommsOk) {
                    row1 = "CAM ERROR";
                    row2 = "Check UART";
                } else {
                    row1 = "IDLE";
                    row2 = "192.168.4.1";
                }
                break;
            case FlightState::ARMED:
                row1 = "ARMED";
                row2 = "Starting...";
                break;
            case FlightState::RECORDING: {
                static char recLine[13];
                static char stopLine[13];
                uint32_t rem = state.autoStopSeconds > state.recordingSeconds
                    ? state.autoStopSeconds - state.recordingSeconds : 0;
                snprintf(recLine, sizeof(recLine), "REC %02lu:%02lu",
                    state.recordingSeconds / 60, state.recordingSeconds % 60);
                snprintf(stopLine, sizeof(stopLine), "Stop %lu:%02lu",
                    rem / 60, rem % 60);
                row1 = recLine;
                row2 = stopLine;
                break;
            }
            case FlightState::STOPPING:
                row1 = "Stopping";
                row2 = "";
                break;
        }
    }

    display_.println(row1);
    display_.println(row2);
    display_.display();
}

#endif // UNIT_TEST
