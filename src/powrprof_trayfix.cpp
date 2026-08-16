#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winnt.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdint>

#include "powrprof_exports_generated.h"

using ShellNotifyIconWFn = BOOL (WINAPI*)(DWORD, PNOTIFYICONDATAW);

static ShellNotifyIconWFn g_shell_notify = nullptr;
static LONG g_monitor_started = 0;
static INIT_ONCE g_system_powrprof_once = INIT_ONCE_STATIC_INIT;
static HMODULE g_system_powrprof = nullptr;

extern "C" IMAGE_DOS_HEADER __ImageBase;
extern "C" void* g_powrprof_exports[POWRPROF_EXPORT_COUNT] = {};

static void AppendTestMarker(const wchar_t* state) {
    wchar_t marker_path[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableW(
        L"MAAEND_TRAYFIX_TEST_MARKER", marker_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;

    HANDLE file = CreateFileW(
        marker_path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(file, state,
              static_cast<DWORD>(wcslen(state) * sizeof(wchar_t)),
              &written, nullptr);
    static constexpr wchar_t newline[] = L"\r\n";
    WriteFile(file, newline,
              static_cast<DWORD>((ARRAYSIZE(newline) - 1) * sizeof(wchar_t)),
              &written, nullptr);
    CloseHandle(file);
}

static BOOL CALLBACK InitSystemPowrProf(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t system_dir[MAX_PATH]{};
    const UINT len = GetSystemDirectoryW(system_dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        AppendTestMarker(L"system-powrprof-systemdir-failed");
        return TRUE;
    }

    wchar_t path[MAX_PATH]{};
    const int written = _snwprintf_s(
        path, ARRAYSIZE(path), _TRUNCATE, L"%s\\powrprof.dll", system_dir);
    if (written < 0) {
        AppendTestMarker(L"system-powrprof-path-failed");
        return TRUE;
    }

    HMODULE loaded = LoadLibraryW(path);
    if (!loaded) {
        AppendTestMarker(L"system-powrprof-load-failed");
        return TRUE;
    }

    if (loaded == reinterpret_cast<HMODULE>(&__ImageBase)) {
        AppendTestMarker(L"system-powrprof-resolved-self");
        return TRUE;
    }

    wchar_t loaded_path[MAX_PATH]{};
    if (GetModuleFileNameW(loaded, loaded_path, MAX_PATH) != 0 &&
        _wcsicmp(loaded_path, path) == 0) {
        g_system_powrprof = loaded;
        AppendTestMarker(L"system-powrprof-loaded");
    } else {
        AppendTestMarker(L"system-powrprof-wrong-module");
    }
    return TRUE;
}

static HMODULE GetSystemPowrProf() {
    InitOnceExecuteOnce(&g_system_powrprof_once, InitSystemPowrProf, nullptr, nullptr);
    return g_system_powrprof;
}

extern "C" void* TrayFixResolveExport(unsigned int index) {
    if (index >= POWRPROF_EXPORT_COUNT) {
        AppendTestMarker(L"proxy-index-invalid");
        return nullptr;
    }

    void* cached = InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(&g_powrprof_exports[index]),
        nullptr,
        nullptr);
    if (cached) return cached;

    HMODULE system_powrprof = GetSystemPowrProf();
    if (!system_powrprof) {
        AppendTestMarker(L"proxy-system-module-missing");
        RaiseFailFastException(nullptr, nullptr, 0);
        return nullptr;
    }

    FARPROC proc = GetProcAddress(system_powrprof, kPowrProfExportNames[index]);
    if (!proc) {
        AppendTestMarker(L"proxy-export-missing");
        RaiseFailFastException(nullptr, nullptr, 0);
        return nullptr;
    }

    void* resolved = reinterpret_cast<void*>(proc);
    void* previous = InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(&g_powrprof_exports[index]),
        resolved,
        nullptr);
    return previous ? previous : resolved;
}

static bool IsRundll32Process() {
    wchar_t path[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;

    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"rundll32.exe") == 0;
}

static bool PatchImport(
    const char* dll_name,
    const char* function_name,
    void* replacement,
    void** original_out) {

    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char* imported_dll = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(imported_dll, dll_name) != 0) continue;

        auto* orig = desc->OriginalFirstThunk
            ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk)
            : reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);

        for (; orig->u1.AddressOfData; ++orig, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;

            auto* import_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                base + orig->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import_name->Name),
                            function_name) != 0) {
                continue;
            }

            DWORD old_protect = 0;
            if (!VirtualProtect(&iat->u1.Function, sizeof(ULONG_PTR),
                                PAGE_READWRITE, &old_protect)) {
                return false;
            }

            if (original_out && !*original_out) {
                *original_out = reinterpret_cast<void*>(iat->u1.Function);
            }
            iat->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);

            DWORD ignored = 0;
            VirtualProtect(&iat->u1.Function, sizeof(ULONG_PTR),
                           old_protect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &iat->u1.Function,
                                  sizeof(ULONG_PTR));
            return true;
        }
    }

    return false;
}

static void StartMonitor(HWND hwnd, UINT uid) {
    if (InterlockedCompareExchange(&g_monitor_started, 1, 0) != 0) return;

    wchar_t dll_path[MAX_PATH]{};
    if (!GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),
                            dll_path, MAX_PATH)) {
        InterlockedExchange(&g_monitor_started, 0);
        return;
    }

    wchar_t system_dir[MAX_PATH]{};
    const UINT system_len = GetSystemDirectoryW(system_dir, MAX_PATH);
    if (system_len == 0 || system_len >= MAX_PATH) {
        InterlockedExchange(&g_monitor_started, 0);
        return;
    }

    const DWORD pid = GetCurrentProcessId();
    const unsigned long long hwnd_value = static_cast<unsigned long long>(
        reinterpret_cast<uintptr_t>(hwnd));

    wchar_t command_line[2048]{};
    const int written = _snwprintf_s(
        command_line, ARRAYSIZE(command_line), _TRUNCATE,
        L"\"%s\\rundll32.exe\" \"%s\",TrayFixMonitor %lu %llx %u",
        system_dir, dll_path, static_cast<unsigned long>(pid),
        hwnd_value, static_cast<unsigned int>(uid));
    if (written < 0) {
        InterlockedExchange(&g_monitor_started, 0);
        return;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command_line, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        InterlockedExchange(&g_monitor_started, 0);
        return;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    AppendTestMarker(L"monitor-started");
}

static BOOL WINAPI HookShellNotifyIconW(DWORD message, PNOTIFYICONDATAW data) {
    if (!g_shell_notify) return FALSE;

    const BOOL result = g_shell_notify(message, data);
    if (data && message == NIM_ADD) {
        AppendTestMarker(L"hook-seen");
        StartMonitor(data->hWnd, data->uID);
    }
    return result;
}

static DWORD WINAPI PatchWorker(void*) {
    const bool hooked = PatchImport(
        "shell32.dll", "Shell_NotifyIconW",
        reinterpret_cast<void*>(&HookShellNotifyIconW),
        reinterpret_cast<void**>(&g_shell_notify));
    if (hooked) AppendTestMarker(L"hook-installed");
    return 0;
}

extern "C" __declspec(dllexport) void CALLBACK TrayFixMonitor(
    HWND, HINSTANCE, LPSTR command_line, int) {

    unsigned long pid = 0;
    unsigned long long hwnd_value = 0;
    unsigned int uid = 0;
    if (!command_line ||
        sscanf_s(command_line, "%lu %llx %u", &pid, &hwnd_value, &uid) != 3) {
        AppendTestMarker(L"monitor-parse-failed");
        return;
    }

    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (parent) {
        WaitForSingleObject(parent, INFINITE);
        CloseHandle(parent);
    }

    HMODULE shell32 = LoadLibraryExW(
        L"shell32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!shell32) shell32 = LoadLibraryW(L"shell32.dll");

    auto notify = shell32
        ? reinterpret_cast<ShellNotifyIconWFn>(
              GetProcAddress(shell32, "Shell_NotifyIconW"))
        : nullptr;

    if (!notify) {
        AppendTestMarker(L"monitor-shell32-failed");
        if (shell32) FreeLibrary(shell32);
        return;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(hwnd_value));
    nid.uID = static_cast<UINT>(uid);
    notify(NIM_DELETE, &nid);
    AppendTestMarker(L"monitor-cleanup-called");
    FreeLibrary(shell32);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        if (!IsRundll32Process()) {
            HANDLE worker = CreateThread(nullptr, 0, PatchWorker, nullptr, 0, nullptr);
            if (worker) CloseHandle(worker);
        }
    }
    return TRUE;
}
