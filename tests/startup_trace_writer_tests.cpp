#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>

struct Vector3 { float x, y, z; };
#include "../rdr2 scripting environment/samples/Pools/startup_trace.h"
static struct { int ped = 77; } ownedPed;
static struct { int photoSlot = 3, photoDownload = -123; } C;
static const char* lastPhotoStage = "none";
#include "startup_trace_writer_under_test.h"

static unsigned checks = 0;
static void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
static std::wstring TracePath()
{
    wchar_t path[MAX_PATH]{};
    Check(GetModuleFileNameW(nullptr, path, MAX_PATH) != 0, "test executable has a module path");
    wchar_t* slash = std::wcsrchr(path, L'\\');
    Check(slash != nullptr, "test log stays beside the executable in tmp/tests");
    *(slash + 1) = 0;
    return std::wstring(path) + L"BountyContracts-startup-trace.log";
}
static std::string Read(const std::wstring& path)
{
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return {};
    std::string text;
    char buffer[1024];
    for (std::size_t count; (count = fread(buffer, 1, sizeof(buffer), file)) != 0;) text.append(buffer, count);
    fclose(file);
    return text;
}
int main()
{
    const auto path = TracePath();
    const auto before = Read(path);
    StartupTrace::Record("disabled");
    Check(Read(path) == before, "an unset sink never writes a file");
    startupTraceSession = 777;
    StartupTrace::sink = WriteStartupTrace;
    Vector3 point{12.5f, -4.25f, 9.0f};
    StartupTrace::Record("unit_before_native", 0xAABBCCDDu, 88, &point, 7, "test_location");
    const auto first = Read(path).substr(before.size());
    Check(first.find("trace-v1 build=pool-preflight-v1 pid=") != std::string::npos, "writer publishes protocol, build and process identity");
    Check(first.find("session=777 stage=unit_before_native detail=test_location") != std::string::npos, "first stage is readable before any later call or process exit");
    Check(first.find("model=AABBCCDD ped=88 freePeds=7 hasPoint=1 point=12.500,-4.250,9.000") != std::string::npos, "event metadata and signed coordinates survive formatting");
    Check(first.find("owned=77 slot=3 download=-123 photoStage=none") != std::string::npos, "known ownership/photo state is logged without game natives");
    StartupTrace::Record("unit_after_native");
    const auto both = Read(path);
    Check(both.substr(before.size(), first.size()) == first && both.find("stage=unit_after_native", before.size() + first.size()) != std::string::npos,
        "each stage appends and closes instead of replacing prior crash breadcrumbs");
    Check(both.find("freePeds=-1 hasPoint=0", before.size() + first.size()) != std::string::npos, "unknown capacity is explicit rather than fabricated zero");
    startupTraceSession = 888;
    StartupTrace::Record("module_start");
    Check(Read(path).find("session=888 stage=module_start", both.size()) != std::string::npos, "a reload can be distinguished from the prior module session");
    StartupTrace::sink = nullptr;
    const auto finished = Read(path);
    StartupTrace::Record("disabled_again");
    Check(Read(path) == finished, "removing the sink prevents further writes");
    std::printf("Startup trace writer: %u checks passed.\n", checks);
}
