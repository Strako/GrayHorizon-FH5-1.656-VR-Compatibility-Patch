#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdint.h>

static FILE* g_log = NULL;
static void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap); fflush(g_log);
}

static HMODULE FindMod() {
    HMODULE mods[256];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) return NULL;
    DWORD n = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < n; i++) {
        wchar_t path[MAX_PATH];
        if (!GetModuleFileNameW(mods[i], path, MAX_PATH)) continue;
        if (wcsstr(path, L"Forza Horizon 5") && wcsstr(path, L"XINPUT1_3.dll")) return mods[i];
    }
    return NULL;
}

static DWORD WINAPI ThreadProc(LPVOID) {
    g_log = fopen("C:\\Users\\404\\Desktop\\vtable_patch.log", "a");
    uintptr_t game = (uintptr_t)GetModuleHandleA(NULL);
    HMODULE mod = FindMod();
    Log("PATCH START game=%p mod=%p\n", (void*)game, (void*)mod);
    if (!game || !mod) { Log("missing module\n"); fclose(g_log); return 0; }

    // Original presentation camera vtable in FH5 1.656.
    uintptr_t vt = game + 0x71BD388;
    uintptr_t hook2 = (uintptr_t)mod + 0xFB10;   // FUN_18000fb10
    uintptr_t hook10 = (uintptr_t)mod + 0xFB50;  // FUN_18000fb50
    DWORD old = 0;
    if (!VirtualProtect((LPVOID)vt, 0x1000, PAGE_EXECUTE_READWRITE, &old)) {
        Log("VirtualProtect failed %lu\n", GetLastError());
        fclose(g_log); return 0;
    }
    uintptr_t* slots = (uintptr_t*)vt;
    Log("slot2 old=%p slot10 old=%p\n", (void*)slots[2], (void*)slots[10]);
    slots[2] = hook2;
    slots[10] = hook10;
    FlushInstructionCache(GetCurrentProcess(), (LPVOID)vt, 0x1000);
    VirtualProtect((LPVOID)vt, 0x1000, old, &old);
    Log("patched slot2=%p slot10=%p\n", (void*)hook2, (void*)hook10);
    fclose(g_log);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        HANDLE h = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
