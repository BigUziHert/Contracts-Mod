// Compile with tests/keyboard_winapi_stub on the include path. This runs the
// actual keyboard.cpp with a deterministic 32-bit clock, without Windows/RDR2.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

#include "../rdr2 scripting environment/samples/Pools/keyboard.cpp"

static DWORD nowMs = 0;
static unsigned checks = 0;

DWORD GetTickCount() { return nowMs; }

static void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", description);
        std::exit(EXIT_FAILURE);
    }
}

static void Event(DWORD key, bool up)
{
    OnKeyboardMessage(key, 1, 0, false, false, !up, up);
}

static void CheckUnseenAndResetKeys()
{
    constexpr DWORD key = 0x49;
    for (DWORD time : {DWORD(0), DWORD(1), std::numeric_limits<DWORD>::max(), DWORD(20)})
    {
        nowMs = time;
        Check(!IsKeyDown(key), "unseen key is not down near startup/wrap");
        Check(!IsKeyDownLong(key), "unseen key is not long-down near startup/wrap");
        Check(!IsKeyJustUp(key), "unseen key has no release");
    }
    nowMs = 0;
    Event(key, false);
    Check(IsKeyDown(key), "a real event at tick zero is valid");
    ResetKeyState(key);
    Check(!IsKeyDown(key) && !IsKeyDownLong(key) && !IsKeyJustUp(key),
        "reset key does not become a phantom key-down at tick zero");
}

static void CheckReleaseWindow(DWORD start)
{
    constexpr DWORD key = 0x55;
    nowMs = start;
    Event(key, true);
    Check(IsKeyJustUp(key, false), "release is visible immediately");
    Check(IsKeyJustUp(key, false), "nonexclusive reads do not consume release");
    Check(!IsKeyDown(key) && !IsKeyDownLong(key), "released key is not down");
    nowMs = start + DWORD(99);
    Check(IsKeyJustUp(key, false), "release is visible at 99 ms");
    nowMs = start + DWORD(100);
    Check(!IsKeyJustUp(key, false), "100 ms release boundary is exclusive");

    nowMs = start;
    Event(key, true);
    Check(IsKeyJustUp(key), "exclusive read consumes a valid release");
    Check(!IsKeyJustUp(key, false), "consumed release is unavailable");
    Check(!IsKeyDown(key) && !IsKeyDownLong(key), "consuming release does not create key-down");
}

static void CheckDownWindows(DWORD start)
{
    constexpr DWORD key = 0x50;
    nowMs = start;
    Event(key, false);
    Check(IsKeyDown(key) && IsKeyDownLong(key), "key-down is visible immediately");
    Check(!IsKeyJustUp(key), "key-down is not a release");
    nowMs = start + DWORD(4999);
    Check(IsKeyDown(key), "normal key-down is visible at 4999 ms");
    nowMs = start + DWORD(5000);
    Check(!IsKeyDown(key), "5000 ms normal boundary is exclusive");
    Check(IsKeyDownLong(key), "long key-down survives normal expiration");
    nowMs = start + DWORD(29999);
    Check(IsKeyDownLong(key), "long key-down is visible at 29999 ms");
    nowMs = start + DWORD(30000);
    Check(!IsKeyDownLong(key), "30000 ms long boundary is exclusive");
    Event(key, false);
    Check(IsKeyDown(key), "repeat event refreshes key-down window");
    ResetKeyState(key);
}

static void CheckKeyBounds()
{
    nowMs = 1000;
    constexpr DWORD lastValid = 254;
    Event(lastValid, true);
    Check(IsKeyJustUp(lastValid), "last supported virtual-key index works");
    for (DWORD key : {DWORD(255), DWORD(256), std::numeric_limits<DWORD>::max()})
    {
        Event(key, true);
        ResetKeyState(key);
        Check(!IsKeyDown(key) && !IsKeyDownLong(key) && !IsKeyJustUp(key),
            "out-of-range events and queries are ignored");
    }
}

int main()
{
    CheckUnseenAndResetKeys();
    CheckReleaseWindow(1000);
    CheckReleaseWindow(std::numeric_limits<DWORD>::max() - 25);
    CheckDownWindows(1000);
    CheckDownWindows(std::numeric_limits<DWORD>::max() - 4000);
    CheckKeyBounds();
    std::printf("All %u keyboard checks passed (real implementation, including DWORD wrap).\n", checks);
}
