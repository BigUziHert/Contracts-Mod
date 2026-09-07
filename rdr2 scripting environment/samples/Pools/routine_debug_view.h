#pragma once

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

// Display-only formatting. The caller supplies a read-only snapshot; no native or
// routine controller is invoked here. `inside` describes an interior, not radius.
namespace RoutineDebugView
{
struct Snapshot
{
    bool active = false, targetExists = false, targetDead = false;
    bool hasDestination = false, fallback = false, destinationOpen = false, destinationValid = false;
    bool loaded = false, taskActive = false, inside = false;
    int minute = 0, nextMinute = -1;
    float playerDistance = 0, destinationDistance = 0, wanderRadius = 0;
    float x = 0, y = 0, z = 0;
    const char* town = "Unknown";
    const char* occupation = "Unknown";
    const char* doing = "Waiting";
    const char* destination = "None";
    const char* nextDestination = "None";
};
using Lines = std::array<std::string, 10>;

inline std::string Label(const char* text, std::size_t limit, const char* fallback)
{
    if (!text || !*text) text = fallback;
    std::string result;
    result.reserve(limit);
    std::size_t index = 0;
    for (; index < limit && text[index]; ++index)
    {
        const unsigned char c = static_cast<unsigned char>(text[index]);
        result += c < 32 || c == 127 ? ' ' : static_cast<char>(c);
    }
    if (text[index] && limit >= 3) result.replace(limit - 3, 3, "...");
    return result;
}
inline std::string Time(int minute)
{
    const int normalized = (minute % 1440 + 1440) % 1440;
    char text[6]{};
    std::snprintf(text, sizeof(text), "%02d:%02d", normalized / 60, normalized % 60);
    return text;
}
inline std::string Number(float value, bool nonnegative = true)
{
    // Bound both string length and invalid/uninitialized native values. A missing
    // measurement is shown explicitly instead of manufacturing a zero distance.
    if (!std::isfinite(value) || std::fabs(value) > 999999.0f || (nonnegative && value < 0)) return "--";
    char text[20]{};
    std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(value == 0 ? 0.0f : value));
    return text;
}
inline Lines Format(const Snapshot& snapshot)
{
    Lines lines{};
    lines[0] = "BOUNTY DEBUG [F8 hide]  " + Time(snapshot.minute);
    if (!snapshot.active)
    {
        lines[1] = "No active routine. Press U for a contract.";
        lines[2] = "Red markers: all authored routine stops.";
        return lines;
    }
    lines[1] = Label(snapshot.town, 20, "Unknown") + " | " + Label(snapshot.occupation, 20, "Unknown");
    if (snapshot.targetDead)
    {
        lines[2] = snapshot.targetExists ? "Target: Deceased" : "Target: Deceased (body missing)";
        lines[3] = "Routine stopped.";
        if (snapshot.targetExists)
        {
            lines[4] = "Body: " + Number(snapshot.playerDistance) + " m away";
            lines[5] = "XYZ: " + Number(snapshot.x, false) + ", " + Number(snapshot.y, false) + ", " + Number(snapshot.z, false);
        }
        return lines;
    }
    if (!snapshot.targetExists)
    {
        lines[2] = "Target: Missing";
        lines[3] = "Live routine status is unavailable.";
        return lines;
    }
    lines[2] = "Target: " + Number(snapshot.playerDistance) + " m away";
    lines[3] = "Doing: " + Label(snapshot.doing, 36, "Waiting");
    lines[4] = snapshot.hasDestination
        ? "Stop: " + Label(snapshot.destination, 24, "None") + (snapshot.fallback ? " [fallback]" : "") +
            " | " + Number(snapshot.destinationDistance) + " m"
        : "Stop: None";
    lines[5] = "Next planned: " + Label(snapshot.nextDestination, 22, "None");
    if (snapshot.nextMinute != -1) lines[5] += " @ " + Time(snapshot.nextMinute);
    lines[6] = "Plans may change with hours / access.";
    lines[7] = "Wander: " + Number(snapshot.wanderRadius) + " m | " + (snapshot.inside ? "Indoors" : "Outdoors");
    lines[8] = std::string("Loaded ") + (snapshot.loaded ? "Y" : "N") + " | Routine " + (snapshot.taskActive ? "Y" : "N") +
        " | Open " + (snapshot.hasDestination ? (snapshot.destinationOpen ? "Y" : "N") : "-") +
        " | Valid " + (snapshot.hasDestination ? (snapshot.destinationValid ? "Y" : "N") : "-");
    lines[9] = "XYZ: " + Number(snapshot.x, false) + ", " + Number(snapshot.y, false) + ", " + Number(snapshot.z, false);
    return lines;
}
}
