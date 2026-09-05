#pragma once

#include <cstdint>
#include <cstddef>
#include "driver/spi_master.h"

class MatrixDisplay {
public:
    static constexpr int WIDTH = 64;
    static constexpr int HEIGHT = 32;

    // Existing myClock / MH-ET LIVE D1 mini wiring: DO NOT CHANGE.
    static constexpr int PIN_LAT = 26;
    static constexpr int PIN_A   = 22;
    static constexpr int PIN_B   = 21;
    static constexpr int PIN_C   = 5;
    static constexpr int PIN_D   = 19;
    static constexpr int PIN_OE  = 16;

    // Existing PxMatrix_mod SPI wiring.
    static constexpr int PIN_CLK  = 18;
    static constexpr int PIN_MOSI = 23;

    bool begin();
    void start_refresh_task();

    // Immediately enable/disable the physical LED output. When disabled, OE is
    // held HIGH so the matrix stays completely dark even if flash writes stall
    // the refresh task during OTA. The framebuffer itself is preserved.
    void setOutputEnabled(bool enabled);
    bool outputEnabled() const { return output_enabled_; }

    void clear();
    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

    static uint16_t color565(uint8_t r, uint8_t g, uint8_t b);
    void setBrightness(uint8_t brightness) { brightness_ = brightness; }
    uint8_t brightness() const { return brightness_; }

private:
    static constexpr uint8_t COLOR_DEPTH = 8;
    static constexpr uint8_t ROW_PATTERN = 16;
    static constexpr uint8_t ROWS_PER_BUFFER = HEIGHT / 2;
    static constexpr uint8_t PANEL_WIDTH_BYTES = WIDTH / 8;
    static constexpr uint8_t PATTERN_COLOR_BYTES = (HEIGHT / ROW_PATTERN) * (WIDTH / 8); // 16
    static constexpr uint16_t SEND_BUFFER_SIZE = PATTERN_COLOR_BYTES * 3;                 // 48
    static constexpr uint16_t ACTIVE_BUFFER_BYTES = ROW_PATTERN * SEND_BUFFER_SIZE;       // 768

    // Same 8-level color interlacing as the old PxMatrix v1.3 modification.
    uint8_t buffer_[COLOR_DEPTH][ACTIVE_BUFFER_BYTES]{};
    uint16_t row_offset_[HEIGHT]{};
    uint8_t display_color_ = 0;
    volatile uint8_t brightness_ = 255;
    volatile bool output_enabled_ = true;

    spi_device_handle_t spi_ = nullptr;

    void fillMatrixBuffer(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b);
    void set_mux(uint8_t value);
    void latch(uint16_t show_time_us);
    void refresh_once(uint16_t show_time_us);
    bool transmit(const uint8_t* data, size_t len);

    static void refresh_task_entry(void* arg);
};
