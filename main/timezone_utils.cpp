#include "timezone_utils.hpp"

#include <cstring>

namespace {
struct MapEntry {
    const char* iana;
    const char* posix;
};

// Keep the table intentionally small and explicit. ESP-IDF/newlib does not
// ship the IANA zoneinfo database, so an IANA name needs a POSIX rule here.
// Aliases that share the same civil-time rules are mapped to the same string.
static constexpr MapEntry kMap[] = {
    {"UTC", "UTC0"},
    {"Etc/UTC", "UTC0"},
    {"Etc/GMT", "UTC0"},

    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Malta", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Budapest", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Luxembourg", "CET-1CEST,M3.5.0,M10.5.0/3"},

    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},

    {"America/New_York", "EST5EDT,M3.2.0/2,M11.1.0/2"},
    {"America/Chicago", "CST6CDT,M3.2.0/2,M11.1.0/2"},
    {"America/Denver", "MST7MDT,M3.2.0/2,M11.1.0/2"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0/2,M11.1.0/2"},
    {"America/Phoenix", "MST7"},

    {"Asia/Tokyo", "JST-9"},
    {"Asia/Singapore", "SGT-8"},
    {"Asia/Dubai", "GST-4"},

    {"Australia/Sydney", "AEST-10AEDT,M10.1.0/2,M4.1.0/3"},
};

static constexpr TimezoneChoice kChoices[] = {
    {"Europe/Berlin", "Europe/Berlin (Deutschland)"},
    {"Europe/Malta", "Europe/Malta"},
    {"Europe/London", "Europe/London"},
    {"Europe/Paris", "Europe/Paris"},
    {"Europe/Rome", "Europe/Rome"},
    {"Europe/Madrid", "Europe/Madrid"},
    {"UTC", "UTC"},
    {"America/New_York", "America/New_York"},
    {"America/Chicago", "America/Chicago"},
    {"America/Denver", "America/Denver"},
    {"America/Los_Angeles", "America/Los_Angeles"},
    {"Asia/Tokyo", "Asia/Tokyo"},
    {"Australia/Sydney", "Australia/Sydney"},
};
}

bool timezone_iana_to_posix(const char* iana, char* out, size_t out_size) {
    if (!iana || !out || out_size == 0) return false;
    for (const auto& item : kMap) {
        if (std::strcmp(iana, item.iana) == 0) {
            std::strncpy(out, item.posix, out_size - 1);
            out[out_size - 1] = '\0';
            return true;
        }
    }
    return false;
}

const TimezoneChoice* timezone_choices(size_t& count) {
    count = sizeof(kChoices) / sizeof(kChoices[0]);
    return kChoices;
}
