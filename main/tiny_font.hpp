#pragma once

#include <cstdint>
#include <string>

class MatrixDisplay;

// Simple 3x5 ASCII font used for the two information rows. It deliberately
// avoids pulling the whole Adafruit-GFX stack into the native ESP-IDF port.
int tiny_text_width(const char* text);
void tiny_draw_text(MatrixDisplay& display, int x, int y, const char* text, uint16_t color);
void tiny_draw_text_centered(MatrixDisplay& display, int y, const char* text, uint16_t color);

// Converts common UTF-8 characters used in German weather descriptions into
// displayable ASCII while preserving upper/lower case.
std::string tiny_ascii_text(const char* utf8);
