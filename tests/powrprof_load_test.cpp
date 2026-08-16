#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <powrprof.h>
#include <iostream>
#include <string>

#pragma comment(lib, "PowrProf.lib")

int wmain() {
    wchar_t module_path[MAX_PATH]{};
    HMODULE mod = GetModuleHandleW(L"powrprof.dll");
    if (!mod) {
        std::wcerr << L"powrprof.dll not loaded\n";
        return 2;
    }
    GetModuleFileNameW(mod, module_path, MAX_PATH);
    std::wcout << L"Loaded powrprof: " << module_path << L"\n";

    wchar_t exe_path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring exe_dir(exe_path);
    const auto slash = exe_dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) exe_dir.resize(slash + 1);

    std::wstring loaded(module_path);
    if (loaded.rfind(exe_dir, 0) != 0) {
        std::wcerr << L"powrprof.dll was not loaded from the application directory\n";
        return 3;
    }

    LONG status = CallNtPowerInformation(SystemBatteryState, nullptr, 0, nullptr, 0);
    std::wcout << L"Status: 0x" << std::hex << static_cast<unsigned long>(status) << L"\n";
    return 0;
}
