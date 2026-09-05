#include "matrix_display.hpp"

#include <algorithm>
#include <cstring>
#include <cstdlib>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "matrix";

namespace {
constexpr uint8_t PX_COLOR_DEPTH = 8;
constexpr uint8_t PX_COLOR_STEP = 256 / PX_COLOR_DEPTH;     // 32
constexpr uint8_t PX_COLOR_HALF_STEP = PX_COLOR_STEP / 2;   // 16
constexpr uint8_t PX_COLOR_THIRD_STEP = PX_COLOR_STEP / 3;  // 10
constexpr uint8_t PX_COLOR_TWO_THIRD_STEP = PX_COLOR_THIRD_STEP * 2; // 20
}

uint16_t MatrixDisplay::color565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

bool MatrixDisplay::begin() {
    clear();

    gpio_config_t io{};
    io.intr_type = GPIO_INTR_DISABLE;
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << PIN_LAT) |
                      (1ULL << PIN_A) |
                      (1ULL << PIN_B) |
                      (1ULL << PIN_C) |
                      (1ULL << PIN_D) |
                      (1ULL << PIN_OE);
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));

    gpio_set_level(static_cast<gpio_num_t>(PIN_A), 0);
    gpio_set_level(static_cast<gpio_num_t>(PIN_B), 0);
    gpio_set_level(static_cast<gpio_num_t>(PIN_C), 0);
    gpio_set_level(static_cast<gpio_num_t>(PIN_D), 0);
    gpio_set_level(static_cast<gpio_num_t>(PIN_LAT), 0);
    gpio_set_level(static_cast<gpio_num_t>(PIN_OE), 1); // display off while starting

    for (uint8_t y = 0; y < HEIGHT; ++y) {
        row_offset_[y] = (y % ROW_PATTERN) * SEND_BUFFER_SIZE + SEND_BUFFER_SIZE - 1;
    }

    spi_bus_config_t bus{};
    bus.mosi_io_num = PIN_MOSI;
    bus.miso_io_num = -1; // old code named GPIO19 as MISO, but it is actually P_D in this wiring
    bus.sclk_io_num = PIN_CLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = SEND_BUFFER_SIZE;

    esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return false;
    }

    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = 20 * 1000 * 1000; // exactly as PxMatrix_mod.h
    dev.mode = 0;
    dev.spics_io_num = -1; // GPIO21 remains row-select P_B; no hardware CS
    dev.queue_size = 1;

    err = spi_bus_add_device(SPI3_HOST, &dev, &spi_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG,
             "64x32 matrix: LAT=%d A=%d B=%d C=%d D=%d OE=%d CLK=%d MOSI=%d, 1/16 scan, 20 MHz",
             PIN_LAT, PIN_A, PIN_B, PIN_C, PIN_D, PIN_OE, PIN_CLK, PIN_MOSI);
    return true;
}

void MatrixDisplay::setOutputEnabled(bool enabled) {
    output_enabled_ = enabled;
    // HUB75 OE is active-low. Force HIGH immediately in both transitions;
    // when enabling, the next normal refresh cycle will pull OE LOW for the
    // configured on-time. This avoids briefly showing a stale/partial row.
    gpio_set_level(static_cast<gpio_num_t>(PIN_OE), 1);
}

void MatrixDisplay::start_refresh_task() {
    // Original sketch called display.display(30) every 2 ms.
    // Pin to core 1 so WiFi normally remains on core 0.
    xTaskCreatePinnedToCore(refresh_task_entry, "matrix_refresh", 4096, this, 20, nullptr, 1);
}

void MatrixDisplay::refresh_task_entry(void* arg) {
    auto* self = static_cast<MatrixDisplay*>(arg);
    TickType_t last = xTaskGetTickCount();
    while (true) {
        self->refresh_once(30);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(2));
    }
}

void MatrixDisplay::clear() {
    std::memset(buffer_, 0, sizeof(buffer_));
}

void MatrixDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
    const uint8_t r = static_cast<uint8_t>(((((color >> 11) & 0x1F) * 527) + 23) >> 6);
    const uint8_t g = static_cast<uint8_t>(((((color >> 5) & 0x3F) * 259) + 33) >> 6);
    const uint8_t b = static_cast<uint8_t>((((color & 0x1F) * 527) + 23) >> 6);
    fillMatrixBuffer(x, y, r, g, b);
}

void MatrixDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    const int16_t dx = std::abs(x1 - x0);
    const int16_t sx = x0 < x1 ? 1 : -1;
    const int16_t dy = -std::abs(y1 - y0);
    const int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void MatrixDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    for (int16_t yy = y; yy < y + h; ++yy) {
        for (int16_t xx = x; xx < x + w; ++xx) {
            drawPixel(xx, yy, color);
        }
    }
}

void MatrixDisplay::fillMatrixBuffer(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;

    // Old PxMatrix flips X before mapping it into the serial buffer.
    x = WIDTH - 1 - x;

    const uint8_t vert_index_in_buffer = (y % ROWS_PER_BUFFER) / ROW_PATTERN;
    const uint8_t which_buffer = y / ROWS_PER_BUFFER;
    const uint8_t x_byte = x / 8;
    const uint8_t which_panel = x_byte / PANEL_WIDTH_BYTES; // always 0 for one 64-wide panel
    const uint8_t in_row_byte_offset = x_byte % PANEL_WIDTH_BYTES;
    constexpr uint8_t panels_width = 1;
    constexpr uint8_t row_sets_per_buffer = ROWS_PER_BUFFER / ROW_PATTERN; // 1

    const int32_t total_offset_r = static_cast<int32_t>(row_offset_[y])
        - in_row_byte_offset
        - PANEL_WIDTH_BYTES * (row_sets_per_buffer * (panels_width * which_buffer + which_panel)
                               + vert_index_in_buffer);
    const int32_t total_offset_g = total_offset_r - PATTERN_COLOR_BYTES;
    const int32_t total_offset_b = total_offset_g - PATTERN_COLOR_BYTES;

    if (total_offset_b < 0 || total_offset_r >= ACTIVE_BUFFER_BYTES) return;

    const uint8_t bit_select = x % 8;
    const uint8_t bit = static_cast<uint8_t>(1U << bit_select);

    for (uint8_t this_color = 0; this_color < COLOR_DEPTH; ++this_color) {
        const uint8_t threshold = static_cast<uint8_t>(this_color * PX_COLOR_STEP + PX_COLOR_HALF_STEP);

        if (r > threshold) buffer_[this_color][total_offset_r] |= bit;
        else               buffer_[this_color][total_offset_r] &= static_cast<uint8_t>(~bit);

        const uint8_t g_plane = static_cast<uint8_t>((this_color + PX_COLOR_THIRD_STEP) % COLOR_DEPTH);
        if (g > threshold) buffer_[g_plane][total_offset_g] |= bit;
        else               buffer_[g_plane][total_offset_g] &= static_cast<uint8_t>(~bit);

        const uint8_t b_plane = static_cast<uint8_t>((this_color + PX_COLOR_TWO_THIRD_STEP) % COLOR_DEPTH);
        if (b > threshold) buffer_[b_plane][total_offset_b] |= bit;
        else               buffer_[b_plane][total_offset_b] &= static_cast<uint8_t>(~bit);
    }
}

void MatrixDisplay::set_mux(uint8_t value) {
    gpio_set_level(static_cast<gpio_num_t>(PIN_A), value & 0x01 ? 1 : 0);
    esp_rom_delay_us(2);
    gpio_set_level(static_cast<gpio_num_t>(PIN_B), value & 0x02 ? 1 : 0);
    esp_rom_delay_us(2);
    gpio_set_level(static_cast<gpio_num_t>(PIN_C), value & 0x04 ? 1 : 0);
    esp_rom_delay_us(2);
    gpio_set_level(static_cast<gpio_num_t>(PIN_D), value & 0x08 ? 1 : 0);
    esp_rom_delay_us(2);
}

bool MatrixDisplay::transmit(const uint8_t* data, size_t len) {
    spi_transaction_t t{};
    t.length = len * 8;
    t.tx_buffer = data;
    const esp_err_t err = spi_device_polling_transmit(spi_, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void MatrixDisplay::latch(uint16_t show_time_us) {
    gpio_set_level(static_cast<gpio_num_t>(PIN_LAT), 1);
    gpio_set_level(static_cast<gpio_num_t>(PIN_LAT), 0);
    if (!output_enabled_) {
        gpio_set_level(static_cast<gpio_num_t>(PIN_OE), 1);
        return;
    }
    gpio_set_level(static_cast<gpio_num_t>(PIN_OE), 0);
    esp_rom_delay_us(show_time_us);
    gpio_set_level(static_cast<gpio_num_t>(PIN_OE), 1);
}

void MatrixDisplay::refresh_once(uint16_t show_time_us) {
    if (!output_enabled_) {
        gpio_set_level(static_cast<gpio_num_t>(PIN_OE), 1);
        return;
    }
    if (show_time_us < 10) show_time_us = 10;
    const uint16_t on_time = static_cast<uint16_t>(show_time_us * static_cast<uint16_t>(brightness_) / 255U);

    for (uint8_t row = 0; row < ROW_PATTERN; ++row) {
        set_mux(row);
        transmit(&buffer_[display_color_][row * SEND_BUFFER_SIZE], SEND_BUFFER_SIZE);
        latch(on_time);
    }

    ++display_color_;
    if (display_color_ >= COLOR_DEPTH) display_color_ = 0;
}
