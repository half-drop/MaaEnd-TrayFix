#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <powrprof.h>
#include <cstdio>

using GetPwrCapabilitiesFn = BOOLEAN (WINAPI*)(PSYSTEM_POWER_CAPABILITIES);
using PowerDeterminePlatformRoleFn = POWER_PLATFORM_ROLE (WINAPI*)();
using PowerDeterminePlatformRoleExFn = POWER_PLATFORM_ROLE (WINAPI*)(ULONG);

int wmain() {
    HMODULE module = LoadLibraryW(L"powrprof.dll");
    if (!module) {
        std::printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    wprintf(L"Loaded powrprof: %ls\n", path);

    auto get_caps = reinterpret_cast<GetPwrCapabilitiesFn>(
        GetProcAddress(module, "GetPwrCapabilities"));
    auto role = reinterpret_cast<PowerDeterminePlatformRoleFn>(
        GetProcAddress(module, "PowerDeterminePlatformRole"));
    auto role_ex = reinterpret_cast<PowerDeterminePlatformRoleExFn>(
        GetProcAddress(module, "PowerDeterminePlatformRoleEx"));

    if (!get_caps || !role || !role_ex) {
        std::printf("Missing NVIDIA-relevant PowrProf export(s)\n");
        return 2;
    }

    SYSTEM_POWER_CAPABILITIES caps{};
    const BOOLEAN caps_ok = get_caps(&caps);
    const auto role_value = role();
    const auto role_ex_value = role_ex(2);

    std::printf(
        "GetPwrCapabilities=%u role=%d roleEx=%d\n",
        static_cast<unsigned>(caps_ok),
        static_cast<int>(role_value),
        static_cast<int>(role_ex_value));

    if (!caps_ok) return 3;
    if (role_value < PlatformRoleUnspecified || role_value >= PlatformRoleMaximum) return 4;
    if (role_ex_value < PlatformRoleUnspecified || role_ex_value >= PlatformRoleMaximum) return 5;
    return 0;
}
