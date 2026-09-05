#pragma once

#include <cstdint>
#include "matrix_display.hpp"

class Digit {
public:
    Digit(MatrixDisplay* display, uint8_t value, int16_t xo, int16_t yo, uint16_t color);

    void Draw(uint8_t value, uint16_t color);
    void Morph(uint8_t newValue);
    uint8_t Value() const { return value_; }
    void DrawColon(uint16_t color);

private:
    MatrixDisplay* display_;
    uint8_t value_;
    uint16_t color_;
    uint16_t bg_;
    int16_t xOffset_;
    int16_t yOffset_;
    int animSpeedMs_ = 30;

    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void drawFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawLine(int16_t x, int16_t y, int16_t x2, int16_t y2, uint16_t color);
    void drawSeg(uint8_t seg);

    void Morph0();
    void Morph1();
    void Morph2();
    void Morph3();
    void Morph4();
    void Morph5();
    void Morph6();
    void Morph7();
    void Morph8();
    void Morph9();
    void animDelay();
};
