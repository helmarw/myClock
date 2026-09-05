#include "digit.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr uint8_t sA = 0;
constexpr uint8_t sB = 1;
constexpr uint8_t sC = 2;
constexpr uint8_t sD = 3;
constexpr uint8_t sE = 4;
constexpr uint8_t sF = 5;
constexpr uint8_t sG = 6;
constexpr int segHeight = 6;
constexpr int segWidth = segHeight;
constexpr int16_t displayHeightMax = 31;

constexpr uint8_t digitBits[] = {
    0b11111100, // 0 ABCDEF--
    0b01100000, // 1 -BC-----
    0b11011010, // 2 AB-DE-G-
    0b11110010, // 3 ABCD--G-
    0b01100110, // 4 -BC--FG-
    0b10110110, // 5 A-CD-FG-
    0b10111110, // 6 A-CDEFG-
    0b11100000, // 7 ABC-----
    0b11111110, // 8 ABCDEFG-
    0b11110110, // 9 ABCD-FG-
};
}

Digit::Digit(MatrixDisplay* display, uint8_t value, int16_t xo, int16_t yo, uint16_t color)
    : display_(display), value_(value), color_(color), bg_(MatrixDisplay::color565(0, 0, 0)),
      xOffset_(xo), yOffset_(yo) {}

void Digit::animDelay() {
    vTaskDelay(pdMS_TO_TICKS(animSpeedMs_));
}

void Digit::drawPixel(int16_t x, int16_t y, uint16_t color) {
    display_->drawPixel(xOffset_ + x, displayHeightMax - (y + yOffset_), color);
}

void Digit::drawLine(int16_t x, int16_t y, int16_t x2, int16_t y2, uint16_t color) {
    display_->drawLine(xOffset_ + x, displayHeightMax - (y + yOffset_),
                       xOffset_ + x2, displayHeightMax - (y2 + yOffset_), color);
}

void Digit::drawFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    display_->fillRect(xOffset_ + x, displayHeightMax - (y + yOffset_), w, h, color);
}

void Digit::DrawColon(uint16_t color) {
    drawFillRect(-3, segHeight - 1, 2, 2, color);
    drawFillRect(-3, segHeight + 1 + 3, 2, 2, color);
}

void Digit::drawSeg(uint8_t seg) {
    switch (seg) {
        case sA: drawLine(1, segHeight * 2 + 2, segWidth, segHeight * 2 + 2, color_); break;
        case sB: drawLine(segWidth + 1, segHeight * 2 + 1, segWidth + 1, segHeight + 2, color_); break;
        case sC: drawLine(segWidth + 1, 1, segWidth + 1, segHeight, color_); break;
        case sD: drawLine(1, 0, segWidth, 0, color_); break;
        case sE: drawLine(0, 1, 0, segHeight, color_); break;
        case sF: drawLine(0, segHeight * 2 + 1, 0, segHeight + 2, color_); break;
        case sG: drawLine(1, segHeight + 1, segWidth, segHeight + 1, color_); break;
        default: break;
    }
}

void Digit::Draw(uint8_t value, uint16_t color) {
    if (value > 9) return;
    color_ = color;
    const uint8_t pattern = digitBits[value];
    if (pattern & (1U << 7)) drawSeg(sA);
    if (pattern & (1U << 6)) drawSeg(sB);
    if (pattern & (1U << 5)) drawSeg(sC);
    if (pattern & (1U << 4)) drawSeg(sD);
    if (pattern & (1U << 3)) drawSeg(sE);
    if (pattern & (1U << 2)) drawSeg(sF);
    if (pattern & (1U << 1)) drawSeg(sG);
    value_ = value;
}

void Digit::Morph2() {
    for (int i = 0; i <= segWidth; ++i) {
        if (i < segWidth) {
            drawPixel(segWidth - i, segHeight * 2 + 2, color_);
            drawPixel(segWidth - i, segHeight + 1, color_);
            drawPixel(segWidth - i, 0, color_);
        }
        drawLine(segWidth + 1 - i, 1, segWidth + 1 - i, segHeight, bg_);
        drawLine(segWidth - i, 1, segWidth - i, segHeight, color_);
        animDelay();
    }
}

void Digit::Morph3() {
    for (int i = 0; i <= segWidth; ++i) {
        drawLine(i, 1, i, segHeight, bg_);
        drawLine(1 + i, 1, 1 + i, segHeight, color_);
        animDelay();
    }
}

void Digit::Morph4() {
    for (int i = 0; i < segWidth; ++i) {
        drawPixel(segWidth - i, segHeight * 2 + 2, bg_);
        drawPixel(0, segHeight * 2 + 1 - i, color_);
        drawPixel(1 + i, 0, bg_);
        animDelay();
    }
}

void Digit::Morph5() {
    for (int i = 0; i < segWidth; ++i) {
        drawPixel(segWidth + 1, segHeight + 2 + i, bg_);
        drawPixel(segWidth - i, segHeight * 2 + 2, color_);
        drawPixel(segWidth - i, 0, color_);
        animDelay();
    }
}

void Digit::Morph6() {
    for (int i = 0; i <= segWidth; ++i) {
        drawLine(segWidth - i, 1, segWidth - i, segHeight, color_);
        if (i > 0) drawLine(segWidth - i + 1, 1, segWidth - i + 1, segHeight, bg_);
        animDelay();
    }
}

void Digit::Morph7() {
    for (int i = 0; i <= segWidth + 1; ++i) {
        drawLine(i - 1, 1, i - 1, segHeight, bg_);
        drawLine(i, 1, i, segHeight, color_);
        drawLine(i - 1, segHeight * 2 + 1, i - 1, segHeight + 2, bg_);
        drawLine(i, segHeight * 2 + 1, i, segHeight + 2, color_);
        drawPixel(1 + i, 0, bg_);
        drawPixel(1 + i, segHeight + 1, bg_);
        animDelay();
    }
}

void Digit::Morph8() {
    for (int i = 0; i <= segWidth; ++i) {
        drawLine(segWidth - i, segHeight * 2 + 1, segWidth - i, segHeight + 2, color_);
        if (i > 0) drawLine(segWidth - i + 1, segHeight * 2 + 1, segWidth - i + 1, segHeight + 2, bg_);
        drawLine(segWidth - i, 1, segWidth - i, segHeight, color_);
        if (i > 0) drawLine(segWidth - i + 1, 1, segWidth - i + 1, segHeight, bg_);
        if (i < segWidth) {
            drawPixel(segWidth - i, 0, color_);
            drawPixel(segWidth - i, segHeight + 1, color_);
        }
        animDelay();
    }
}

void Digit::Morph9() {
    for (int i = 0; i <= segWidth + 1; ++i) {
        drawLine(i - 1, 1, i - 1, segHeight, bg_);
        drawLine(i, 1, i, segHeight, color_);
        animDelay();
    }
}

void Digit::Morph0() {
    for (int i = 0; i <= segWidth; ++i) {
        if (value_ == 1) {
            drawLine(segWidth - i, segHeight * 2 + 1, segWidth - i, segHeight + 2, color_);
            if (i > 0) drawLine(segWidth - i + 1, segHeight * 2 + 1, segWidth - i + 1, segHeight + 2, bg_);
            drawLine(segWidth - i, 1, segWidth - i, segHeight, color_);
            if (i > 0) drawLine(segWidth - i + 1, 1, segWidth - i + 1, segHeight, bg_);
            if (i < segWidth) drawPixel(segWidth - i, segHeight * 2 + 2, color_);
            if (i < segWidth) drawPixel(segWidth - i, 0, color_);
        }
        if (value_ == 2) {
            drawLine(segWidth - i, segHeight * 2 + 1, segWidth - i, segHeight + 2, color_);
            if (i > 0) drawLine(segWidth - i + 1, segHeight * 2 + 1, segWidth - i + 1, segHeight + 2, bg_);
            drawPixel(1 + i, segHeight + 1, bg_);
            if (i < segWidth) drawPixel(segWidth + 1, segHeight + 1 - i, color_);
        }
        if (value_ == 3) {
            drawLine(segWidth - i, segHeight * 2 + 1, segWidth - i, segHeight + 2, color_);
            if (i > 0) drawLine(segWidth - i + 1, segHeight * 2 + 1, segWidth - i + 1, segHeight + 2, bg_);
            drawLine(segWidth - i, 1, segWidth - i, segHeight, color_);
            if (i > 0) drawLine(segWidth - i + 1, 1, segWidth - i + 1, segHeight, bg_);
            drawPixel(segWidth - i, segHeight + 1, bg_);
        }
        if (value_ == 5) {
            if (i < segWidth) {
                if (i > 0) drawLine(1 + i, segHeight * 2 + 1, 1 + i, segHeight + 2, bg_);
                drawLine(2 + i, segHeight * 2 + 1, 2 + i, segHeight + 2, color_);
            }
        }
        if (value_ == 5 || value_ == 9) {
            if (i < segWidth) drawPixel(segWidth - i, segHeight + 1, bg_);
            if (i < segWidth) drawPixel(0, segHeight - i, color_);
        }
        animDelay();
    }
}

void Digit::Morph1() {
    for (int i = 0; i <= segWidth + 1; ++i) {
        drawLine(i - 1, 1, i - 1, segHeight, bg_);
        drawLine(i, 1, i, segHeight, color_);
        drawLine(i - 1, segHeight * 2 + 1, i - 1, segHeight + 2, bg_);
        drawLine(i, segHeight * 2 + 1, i, segHeight + 2, color_);
        drawPixel(1 + i, segHeight * 2 + 2, bg_);
        drawPixel(1 + i, 0, bg_);
        drawPixel(1 + i, segHeight + 1, bg_);
        animDelay();
    }
}

void Digit::Morph(uint8_t newValue) {
    if (newValue > 9 || newValue == value_) return;
    switch (newValue) {
        case 0: Morph0(); break;
        case 1: Morph1(); break;
        case 2: Morph2(); break;
        case 3: Morph3(); break;
        case 4: Morph4(); break;
        case 5: Morph5(); break;
        case 6: Morph6(); break;
        case 7: Morph7(); break;
        case 8: Morph8(); break;
        case 9: Morph9(); break;
        default: break;
    }
    value_ = newValue;
}
