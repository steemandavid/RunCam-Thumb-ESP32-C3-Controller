#pragma once

// WiFi
#define WIFI_SSID              "RunCam-Controller"
#define WIFI_PASSWORD          "runcam1234"
#define WIFI_CHANNEL           6

// GPIO — Camera UART (UART1)
#define GPIO_UART_TX           3
#define GPIO_UART_RX           4
#define CAMERA_UART_NUM        1

// GPIO — OLED (hardwired, do not reassign)
#define GPIO_OLED_SDA          5
#define GPIO_OLED_SCL          6
#define OLED_I2C_ADDR          0x3C
#define OLED_WIDTH             72
#define OLED_HEIGHT            40

// GPIO — Control
#define GPIO_ARM_PIN           2
#define GPIO_STATUS_LED        8

// Serial
#define RUNCAM_BAUD_RATE       115200
#define RUNCAM_RESPONSE_TIMEOUT_MS  500
#define RUNCAM_MAX_RETRIES     3

// Flight logic
#define ARM_DEBOUNCE_MS        50
#define AUTO_STOP_DURATION_MS  300000

// Default camera settings
#define DEFAULT_RESOLUTION      3   // 1080p
#define DEFAULT_FPS             0   // 120fps
#define DEFAULT_FOV             0   // Wide
#define DEFAULT_VIDEO_FORMAT    1   // PAL
#define DEFAULT_EIS             1   // enabled
#define DEFAULT_LOOP_RECORDING  0   // disabled
#define DEFAULT_AUTO_START_REC  0   // disabled
#define DEFAULT_SHARPNESS       1   // Medium
#define DEFAULT_EXPOSURE        0   // 0 EV
#define DEFAULT_WHITE_BALANCE   0   // Auto
#define DEFAULT_CONTRAST        1   // Medium
#define DEFAULT_SATURATION      1   // Medium
#define DEFAULT_HUE             0   // 0

// NVS
#define NVS_NAMESPACE           "runcam"

// WebSocket
#define WS_STATUS_INTERVAL_MS  1000

// OLED
#define OLED_REFRESH_INTERVAL_MS  500
