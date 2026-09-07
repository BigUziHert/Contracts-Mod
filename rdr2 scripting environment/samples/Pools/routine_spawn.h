#pragma once

// Included after the script's bounded WaitUntil helper. No entity is created here.
// Source precedents and flag limitations are recorded in docs/target-routines.md.
namespace RoutineSpawn
{
inline bool Loaded(const Vector3& point)
{
    const Vector3 low(point.x - 2.0f, point.y - 2.0f, point.z - 3.0f);
    const Vector3 high(point.x + 2.0f, point.y + 2.0f, point.z + 3.0f);
    return ENTITY::HAS_COLLISION_LOADED_AROUND_POSITION(point) && PATH::IS_NAVMESH_LOADED_IN_AREA(low, high);
}

inline bool ClearBody(const Vector3& point, Ped ignore)
{

    // spd_sheriffoftumbleweed.c:1924 tests a destination while ignoring its traveller.
    if (MISC::IS_POSITION_OCCUPIED(point, .5f, false, true, true, false, false, ignore, true)) return false;
    // short_update.c:87166-87173 uses outside + a synchronous mask-87 collision probe.
    // Test five upright lines, not just the ground below the ped's feet.
    const float offsets[][2] = { {0, 0}, {.3f, 0}, {-.3f, 0}, {0, .3f}, {0, -.3f} };
    for (const auto& offset : offsets)
    {
        Vector3 low(point.x + offset[0], point.y + offset[1], point.z + .25f);
        Vector3 high(low.x, low.y, point.z + 1.9f), end, normal;
        BOOL hit = false;
        Entity entity = 0;
        const auto probe = SHAPETEST::START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE(low, high, 87, ignore, 0);
        if (SHAPETEST::GET_SHAPE_TEST_RESULT(probe, &hit, &end, &normal, &entity) != 2 || hit) return false;
    }
    return true;
}

inline bool Validate(const Vector3& anchor, float radius, float heightTolerance,
    const Vector3& candidate, Vector3& out, Ped ignore = 0)
{
    if (!Loaded(candidate)) return false;
    Vector3 safe;
    // property_use_core.c:56908 uses false,0. Ground, height and outdoor tests remain separate.
    if (!PATH::GET_SAFE_COORD_FOR_PED(candidate, false, &safe, 0)) return false;
    const float dx = safe.x - anchor.x, dy = safe.y - anchor.y;
    if (!std::isfinite(safe.x) || !std::isfinite(safe.y) || !std::isfinite(safe.z) ||
        dx * dx + dy * dy > radius * radius || std::fabs(safe.z - anchor.z) > heightTolerance) return false;
    float ground = 0;
    Vector3 normal;
    if (!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(Vector3(safe.x, safe.y, anchor.z + heightTolerance + 1.0f), &ground, &normal) ||
        !std::isfinite(ground) || !std::isfinite(normal.z) || normal.z < .75f ||
        std::fabs(ground - anchor.z) > heightTolerance || ground > anchor.z + .75f ||
        std::fabs(ground - safe.z) > 1.0f) return false;
    safe.z = ground;
    if (!Loaded(safe) || !INTERIOR::IS_COLLISION_MARKED_OUTSIDE(safe) || INTERIOR::GET_INTERIOR_FROM_COLLISION(safe)) return false;
    float water = 0;
    if (WATER::GET_WATER_HEIGHT(Vector3(safe.x, safe.y, safe.z + 2.0f), &water) && water >= safe.z - .2f) return false;
    if (!ClearBody(safe, ignore)) return false;
    out = safe;
    return true;
}

inline bool Prepare(const Vector3& anchor, float radius, float heightTolerance, unsigned seed, Vector3& out)
{
    // Do not start over an active global loader. The API has no ownership handle;
    // another script replacing our request during a yield remains an engine limitation.
    bool ownsScene = false;
    if (!Loaded(anchor) && !STREAMING::IS_LOAD_SCENE_ACTIVE())
        ownsScene = STREAMING::LOAD_SCENE_START_SPHERE(anchor, 50.0f, 0) != 0;
    PATH::ADD_NAVMESH_REQUIRED_REGION(anchor.x, anchor.y, 50.0f);
    bool loaded = WaitUntil(3000, [&] {
        STREAMING::REQUEST_COLLISION_AT_COORD(anchor);
        return Loaded(anchor);
    });
    bool found = false;
    if (loaded && PlayerAvailable())
    {
        // Rotate four small offsets, then retry the exact sourced anchor as the fallback.
        const float offsets[][2] = { {.5f, 0}, {0, .5f}, {-.5f, 0}, {0, -.5f} };
        for (unsigned attempt = 0; attempt < 5 && !found; ++attempt)
        {
            Vector3 candidate = anchor;
            if (attempt < 4)
            {
                const auto& offset = offsets[(seed + attempt) % 4];
                candidate.x += offset[0] * radius;
                candidate.y += offset[1] * radius;
            }
            found = Validate(anchor, radius, heightTolerance, candidate, out);
        }
    }
    if (ownsScene) STREAMING::LOAD_SCENE_STOP();
    return found && PlayerAvailable();
}
}
