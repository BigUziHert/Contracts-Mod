#pragma once

// Included after the script's bounded WaitUntil helper. No entity is created here.
// Source precedents and flag limitations are recorded in docs/target-routines.md.
namespace RoutineSpawn
{
struct Diagnostic
{
    const char* check = "none";
    Vector3 candidate{}, projected{};
    float ground = 0;
};
inline Diagnostic diagnostic;
inline bool Reject(const char* check) { diagnostic.check = check; return false; }

inline bool Loaded(const Vector3& point)
{
    const Vector3 low(point.x - 2.0f, point.y - 2.0f, point.z - 3.0f);
    const Vector3 high(point.x + 2.0f, point.y + 2.0f, point.z + 3.0f);
    return ENTITY::HAS_COLLISION_LOADED_AROUND_POSITION(point) && PATH::IS_NAVMESH_LOADED_IN_AREA(low, high);
}

inline bool ClearBody(const Vector3& point, Ped ignore)
{

    // spd_sheriffoftumbleweed.c:1924 tests a destination while ignoring its traveller.
    if (MISC::IS_POSITION_OCCUPIED(point, .5f, false, true, true, false, false, ignore, true)) return Reject("occupied");
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
        if (SHAPETEST::GET_SHAPE_TEST_RESULT(probe, &hit, &end, &normal, &entity) != 2) return Reject("clearance_pending");
        if (hit) return Reject("clearance_blocked");
    }
    return true;
}

inline bool ValidateSurface(const Vector3& anchor, float radius, float heightTolerance,
    Vector3 safe, Vector3& out, Ped ignore, float groundTolerance)
{
    diagnostic.projected = safe;
    const float dx = safe.x - anchor.x, dy = safe.y - anchor.y;
    if (!std::isfinite(safe.x) || !std::isfinite(safe.y) || !std::isfinite(safe.z) ||
        dx * dx + dy * dy > radius * radius || std::fabs(safe.z - anchor.z) > heightTolerance) return Reject("anchor_bounds");
    if (!Loaded(safe)) return Reject("collision_or_nav_unloaded");
    float ground = 0;
    Vector3 normal;
    if (!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(Vector3(safe.x, safe.y, anchor.z + heightTolerance + 1.0f), &ground, &normal))
        return Reject("ground_unavailable");
    diagnostic.ground = ground;
    if (!std::isfinite(ground) || !std::isfinite(normal.z) || normal.z < .75f ||
        std::fabs(ground - anchor.z) > heightTolerance || ground > anchor.z + .75f ||
        std::fabs(ground - safe.z) > groundTolerance) return Reject("ground_height_or_slope");
    safe.z = ground;
    if (!Loaded(safe)) return Reject("ground_collision_or_nav_unloaded");
    if (!INTERIOR::IS_COLLISION_MARKED_OUTSIDE(safe) || INTERIOR::GET_INTERIOR_FROM_COLLISION(safe)) return Reject("interior");
    float water = 0;
    if (WATER::GET_WATER_HEIGHT(Vector3(safe.x, safe.y, safe.z + 2.0f), &water) && water >= safe.z - .2f) return Reject("water");
    if (!ClearBody(safe, ignore)) return false;
    out = safe;
    diagnostic.check = "ok";
    return true;
}

inline bool Validate(const Vector3& anchor, float radius, float heightTolerance,
    const Vector3& candidate, Vector3& out, Ped ignore = 0)
{
    diagnostic = {};
    diagnostic.candidate = candidate;
    if (!Loaded(candidate)) return Reject("collision_or_nav_unloaded");
    Vector3 safe;
    // This selects a candidate; it is not an idempotent validity query.
    if (!PATH::GET_SAFE_COORD_FOR_PED(candidate, false, &safe, 0)) return Reject("no_safe_coord");
    return ValidateSurface(anchor, radius, heightTolerance, safe, out, ignore, 1.0f);
}

inline bool ValidatePoint(const Vector3& anchor, float radius, float heightTolerance,
    const Vector3& point, Ped ignore = 0)
{
    diagnostic = {};
    diagnostic.candidate = point;
    Vector3 checked;
    // A previously accepted ground point is checked directly. Do not choose a new
    // nav coordinate and reject this one merely because that answer moved sideways.
    return ValidateSurface(anchor, radius, heightTolerance, point, checked, ignore, .35f);
}

inline bool Find(const Vector3& anchor, float radius, float heightTolerance, unsigned seed, Vector3& out, Ped ignore = 0)
{
    const float offsets[][2] = { {.5f, 0}, {0, .5f}, {-.5f, 0}, {0, -.5f} };
    for (unsigned attempt = 0; attempt < 5; ++attempt)
    {
        Vector3 candidate = anchor;
        if (attempt < 4)
        {
            const auto& offset = offsets[(seed + attempt) % 4];
            candidate.x += offset[0] * radius;
            candidate.y += offset[1] * radius;
        }
        if (Validate(anchor, radius, heightTolerance, candidate, out, ignore)) return true;
    }
    return false;
}

inline bool EnsureLoaded(const Vector3& anchor)
{
    if (Loaded(anchor)) return PlayerAvailable();
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
    if (ownsScene) STREAMING::LOAD_SCENE_STOP();
    if (!loaded) return Reject("stream_timeout_or_interrupted");
    return PlayerAvailable();
}

inline bool Prepare(const Vector3& anchor, float radius, float heightTolerance, unsigned seed, Vector3& out)
{
    return EnsureLoaded(anchor) && Find(anchor, radius, heightTolerance, seed, out);
}
}
