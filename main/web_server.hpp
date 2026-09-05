#pragma once

#include "app_config.hpp"

class MatrixDisplay;

bool web_server_start(ClockConfig* config, MatrixDisplay* display);

// True while an OTA firmware upload is actively being written.
// app_main uses this to replace the normal clock display with a stable
// update indication so flash writes do not look like a display fault.
bool web_server_ota_in_progress();
