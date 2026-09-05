#pragma once

#include <cstddef>

struct TimezoneChoice {
    const char* iana;
    const char* label;
};

// Converts supported IANA names (e.g. Europe/Berlin) to a POSIX TZ string
// understood by ESP-IDF/newlib. Returns false for an unsupported name.
bool timezone_iana_to_posix(const char* iana, char* out, size_t out_size);

// Choices shown in the manual web UI. The automatic browser mode can use
// additional aliases handled by timezone_iana_to_posix().
const TimezoneChoice* timezone_choices(size_t& count);
