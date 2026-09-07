#pragma once

#include <array>
#include <string>

// DrawTextToScreen is supplied by the existing card renderer. This helper draws
// only routine text; the caller retains its portrait, reward and final draw order.
namespace RoutineCard
{
inline void Draw(const std::array<std::string, 6>& lines)
{
    // Invalid/unprepared plans have an empty first line. Never draw a title or old
    // habit fields from a partially cleared cache when there is no valid identity.
    if (lines[0].empty()) return;
    DrawTextToScreen("USUAL HAUNTS", .49f, .343f, .30f, 255, 255, 255, 255);
    for (std::size_t index = 0; index < lines.size(); ++index)
        DrawTextToScreen(lines[index].c_str(), .49f, .375f + static_cast<float>(index) * .027f,
            .28f, 255, 255, 255, 255);
    DrawTextToScreen("Visits may vary.", .49f, .545f, .25f, 255, 255, 255, 255);
}
}
