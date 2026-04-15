#include "borderless_hooks.h"
#include "common.h"

#include <windows.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <cwchar>

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DllCanUnloadNowFn = HRESULT(WINAPI*)(void);
using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
using DllRegisterServerFn = HRESULT(WINAPI*)(void);
using DllUnregisterServerFn = HRESULT(WINAPI*)(void);
using GetdfDIJoystickFn = LPCDIDATAFORMAT(WINAPI*)(void);

INIT_ONCE g_real_dinput8_once = INIT_ONCE_STATIC_INIT;
HMODULE g_real_dinput8 = nullptr;

DirectInput8CreateFn g_direct_input8_create = nullptr;
DllCanUnloadNowFn g_dll_can_unload_now = nullptr;
DllGetClassObjectFn g_dll_get_class_object = nullptr;
DllRegisterServerFn g_dll_register_server = nullptr;
DllUnregisterServerFn g_dll_unregister_server = nullptr;
GetdfDIJoystickFn g_getdf_dijoystick = nullptr;

FARPROC ResolveExport(const char* export_name) {
    FARPROC proc = GetProcAddress(g_real_dinput8, export_name);
    if (proc == nullptr) {
        shogun2::Log("GetProcAddress failed for %s: %lu", export_name, GetLastError());
    }
    return proc;
}

BOOL CALLBACK LoadRealDInput8(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t system_directory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_directory, static_cast<UINT>(std::size(system_directory)));
    if (length == 0 || length >= std::size(system_directory)) {
        shogun2::Log("GetSystemDirectoryW failed: %lu", GetLastError());
        return FALSE;
    }

    wchar_t dll_path[MAX_PATH] = {};
    if (swprintf_s(dll_path, std::size(dll_path), L"%ls\\dinput8.dll", system_directory) < 0) {
        shogun2::Log("swprintf_s failed while building dinput8 path");
        return FALSE;
    }

    g_real_dinput8 = LoadLibraryW(dll_path);
    if (g_real_dinput8 == nullptr) {
        shogun2::Log("LoadLibraryW failed for %ls: %lu", dll_path, GetLastError());
        return FALSE;
    }

    g_direct_input8_create = reinterpret_cast<DirectInput8CreateFn>(ResolveExport("DirectInput8Create"));
    g_dll_can_unload_now = reinterpret_cast<DllCanUnloadNowFn>(ResolveExport("DllCanUnloadNow"));
    g_dll_get_class_object = reinterpret_cast<DllGetClassObjectFn>(ResolveExport("DllGetClassObject"));
    g_dll_register_server = reinterpret_cast<DllRegisterServerFn>(ResolveExport("DllRegisterServer"));
    g_dll_unregister_server = reinterpret_cast<DllUnregisterServerFn>(ResolveExport("DllUnregisterServer"));
    g_getdf_dijoystick = reinterpret_cast<GetdfDIJoystickFn>(ResolveExport("GetdfDIJoystick"));

    shogun2::Log("Loaded real dinput8 from %ls", dll_path);
    return TRUE;
}

bool EnsureRealDInput8Loaded() {
    PVOID context = nullptr;
    if (!InitOnceExecuteOnce(&g_real_dinput8_once, &LoadRealDInput8, nullptr, &context)) {
        shogun2::Log("InitOnceExecuteOnce failed: %lu", GetLastError());
        return false;
    }

    return g_real_dinput8 != nullptr;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE instance,
    DWORD version,
    REFIID riid,
    LPVOID* out,
    LPUNKNOWN outer) {
    if (!EnsureRealDInput8Loaded() || g_direct_input8_create == nullptr) {
        return E_FAIL;
    }

    return g_direct_input8_create(instance, version, riid, out, outer);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    if (!EnsureRealDInput8Loaded() || g_dll_can_unload_now == nullptr) {
        return S_FALSE;
    }

    return g_dll_can_unload_now();
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
    if (!EnsureRealDInput8Loaded() || g_dll_get_class_object == nullptr) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    return g_dll_get_class_object(clsid, riid, out);
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    if (!EnsureRealDInput8Loaded() || g_dll_register_server == nullptr) {
        return E_FAIL;
    }

    return g_dll_register_server();
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    if (!EnsureRealDInput8Loaded() || g_dll_unregister_server == nullptr) {
        return E_FAIL;
    }

    return g_dll_unregister_server();
}

extern "C" LPCDIDATAFORMAT WINAPI GetdfDIJoystick() {
    if (!EnsureRealDInput8Loaded() || g_getdf_dijoystick == nullptr) {
        return nullptr;
    }

    return g_getdf_dijoystick();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        shogun2::SetSelfModule(module);
        DisableThreadLibraryCalls(module);
        shogun2::StartHookInstaller();
    } else if (reason == DLL_PROCESS_DETACH) {
        shogun2::ShutdownHooks();
    }

    return TRUE;
}
