// GrayHorizon FH5 1.656 - nuevo mod proxy para 1.656.
// Carga el core parcheado (gh5vr_core.dll) y engancha el update de camara
// llamando a FUN_18000F230 / FUN_18000DF70 con el objeto de presentacion.
//
// Compilar x64:
//   cl /LD /O2 /EHsc xinput1_3.cpp /link /OUT:xinput1_3.dll
//
// Instalar en la carpeta del juego:
//   xinput1_3.dll       = este proxy
//   gh5vr_core.dll      = xinput1_3_compat1656_v5.dll renombrado
//   openxr_loader.dll   = el del zip de GrayHorizon
//   gh5vr.ini           = el del zip de GrayHorizon

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#pragma comment(lib, "user32.lib")

// Minimal XInput types (avoids a linkage clash with the SDK's xinput.h).
typedef struct { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger; SHORT sThumbLX; SHORT sThumbLY; SHORT sThumbRX; SHORT sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
typedef struct { WORD wLeftMotorSpeed; WORD wRightMotorSpeed; } XINPUT_VIBRATION;
typedef struct { BYTE Type; BYTE SubType; WORD Flags; XINPUT_GAMEPAD Gamepad; XINPUT_VIBRATION Vibration; } XINPUT_CAPABILITIES;
typedef struct { BYTE BatteryType; BYTE BatteryLevel; } XINPUT_BATTERY_INFORMATION;
typedef struct { WORD VirtualKey; WORD Unicode; WORD Flags; BYTE UserIndex; BYTE HidCode; } XINPUT_KEYSTROKE;

// ---------------------------------------------------------------------------
// XInput forwarding to the real system DLL.
// ---------------------------------------------------------------------------
typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* PFN_XInputSetState)(DWORD, XINPUT_VIBRATION*);
typedef DWORD(WINAPI* PFN_XInputGetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef void (WINAPI* PFN_XInputEnable)(BOOL);
typedef DWORD(WINAPI* PFN_XInputGetBatteryInformation)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
typedef DWORD(WINAPI* PFN_XInputGetKeystroke)(DWORD, DWORD, XINPUT_KEYSTROKE*);

static HMODULE g_sys = NULL;
static PFN_XInputGetState g_getstate = NULL;
static PFN_XInputSetState g_setstate = NULL;
static PFN_XInputGetCapabilities g_getcaps = NULL;
static PFN_XInputEnable g_enable = NULL;
static PFN_XInputGetBatteryInformation g_getbattery = NULL;
static PFN_XInputGetKeystroke g_getkeystroke = NULL;

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD i, XINPUT_STATE* s) {
    return g_getstate ? g_getstate(i, s) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI XInputSetState(DWORD i, XINPUT_VIBRATION* v) {
    return g_setstate ? g_setstate(i, v) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD i, DWORD f, XINPUT_CAPABILITIES* c) {
    return g_getcaps ? g_getcaps(i, f, c) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" __declspec(dllexport) void WINAPI XInputEnable(BOOL e) {
    if (g_enable) g_enable(e);
}
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetBatteryInformation(DWORD i, BYTE t, XINPUT_BATTERY_INFORMATION* b) {
    return g_getbattery ? g_getbattery(i, t, b) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetKeystroke(DWORD i, DWORD f, XINPUT_KEYSTROKE* k) {
    return g_getkeystroke ? g_getkeystroke(i, f, k) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetStateEx(DWORD i, XINPUT_STATE* s) {
    typedef DWORD(WINAPI* F)(DWORD, XINPUT_STATE*);
    F f = g_sys ? (F)GetProcAddress(g_sys, (LPCSTR)100) : NULL;
    return f ? f(i, s) : ERROR_DEVICE_NOT_CONNECTED;
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static FILE* g_log = NULL;
static void Log(const char* fmt, ...) {
    if (!g_log) {
        g_log = fopen("gh5vr_new.log", "a");
        if (!g_log) return;
    }
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap); fflush(g_log);
}

// ---------------------------------------------------------------------------
// Camera setters installed into the core's globals.
// The core calls these with (camera, matrix/pointer) or (camera, floats).
// Offsets are the 1.656 presentation-object fields.  They can be calibrated
// from gh5vr_new.log (matrices are copied verbatim).
// ---------------------------------------------------------------------------
static void* g_pres = NULL;
static int g_exc_logged = 0;
static int g_set1_logged = 0;
static int g_set2_logged = 0;
static int g_fov_logged = 0;

static void SetMatrix1(void* cam, void* m) {
    if (!cam || !m) return;
    memcpy((char*)cam + 0x10, m, 64);   // getter returns [cam+0x10]
    if (!g_set1_logged) {
        g_set1_logged = 1;
        float* f = (float*)m;
        Log("SetMatrix1 cam=%p m=%p [%.4f %.4f %.4f %.4f ...]\n", cam, m, f[0], f[1], f[2], f[3]);
    }
}
static void SetMatrix2(void* cam, void* m) {
    if (!cam || !m) return;
    memcpy((char*)cam + 0x50, m, 64);   // second matrix (calibrate)
    if (!g_set2_logged) {
        g_set2_logged = 1;
        float* f = (float*)m;
        Log("SetMatrix2 cam=%p m=%p [%.4f %.4f %.4f %.4f ...]\n", cam, m, f[0], f[1], f[2], f[3]);
    }
}
static void SetFov(void* cam, float a, float b, float c, float d) {
    if (!g_fov_logged) {
        g_fov_logged = 1;
        Log("SetFov cam=%p %.4f %.4f %.4f %.4f\n", cam, a, b, c, d);
    }
}
static void SetJitter(void* cam, int v) {
    (void)cam; (void)v;
}
static void NoOp(void) {}

// ---------------------------------------------------------------------------
// Core hook thread.
// ---------------------------------------------------------------------------
typedef void (*DF70)(void*, float, float, float);  // rcx, xmm1, xmm2
typedef void (*F230)(void*, int);                  // rcx, edx
typedef void (*E0C0)(void*, float, char, char);     // manager update

// ---------------------------------------------------------------------------
// OpenXR head pose hook + head-driven mouse look.
//
// The core reads the headset pose through xrLocateViews().  We read the real
// pose for ourselves, then force the orientation the core sees to identity so
// its own (mis-offset for 1.656) head rotation does not warp the picture.
// The head rotation is sent to the game as "hold right mouse + move mouse",
// which is how FH5 turns the cockpit camera.  Holding the right button keeps
// the camera where the head points instead of snapping back to centre.
// ---------------------------------------------------------------------------
#pragma pack(push, 4)
typedef struct { float x, y, z, w; } XrQuat;
typedef struct { float x, y, z; } XrVec3;
typedef struct { XrQuat orientation; XrVec3 position; } XrPose;
typedef struct { float left, right, up, down; } XrFov;
typedef struct { XrPose pose; XrFov fov; } XrView;
#pragma pack(pop)

typedef int32_t (*PFN_XrLocateViews)(uint64_t, const void*, void*, uint32_t, uint32_t*, XrView*);

static PFN_XrLocateViews g_realLocateViews = NULL;   // the core's original xrLocateViews
static volatile LONG g_hookReady = 0;
static volatile float g_headQ[4] = {0.0f, 0.0f, 0.0f, 1.0f};
static volatile LONG g_poseSeq = 0;
static HINSTANCE g_hinst = NULL;

static int InOurModule(void* p) {
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)p, &m)) return 0;
    return ((HINSTANCE)m == g_hinst);
}

// Pixels of synthetic mouse movement per radian of head rotation.
// Negative signs: head left/up must turn the game camera left/up.
static const float SENS_X = -45.0f;
static const float SENS_Y = -45.0f;

static int32_t OurXrLocateViews(uint64_t session, const void* info, void* state,
                                uint32_t cap, uint32_t* count, XrView* views) {
    if (!g_realLocateViews) return -1;
    int32_t r = g_realLocateViews(session, info, state, cap, count, views);
    if (r >= 0 && views && count) {
        uint32_t n = *count;
        if (n > cap) n = cap;
        if (n > 0) {
            // GrayHorizon normally passes its compact 44-byte view layout,
            // but SteamVR can return the standard OpenXR layout (pose starts
            // at +16, 56 bytes per view).  Accept either; the old code only
            // checked the compact form, which left mouse-look with seq=0.
            unsigned char* raw = (unsigned char*)views;
            float q[4] = {0, 0, 0, 1};
            int qoff = -1;
            int stride = 0;
            for (int candidate = 0; candidate < 2 && qoff < 0; candidate++) {
                int off = candidate ? 16 : 0;
                float t[4];
                memcpy(t, raw + off, sizeof(t));
                float qn = sqrtf(t[0] * t[0] + t[1] * t[1] +
                                 t[2] * t[2] + t[3] * t[3]);
                if (qn > 0.5f && qn < 1.5f) {
                    q[0] = t[0] / qn; q[1] = t[1] / qn;
                    q[2] = t[2] / qn; q[3] = t[3] / qn;
                    qoff = off;
                    // Standard XrView has a 16-byte (type/next) header,
                    // 28-byte pose and 16-byte FOV, rounded to 64 bytes.
                    stride = candidate ? 64 : 44;
                }
            }
            static LONG rawLogs = 0;
            LONG rawNo = InterlockedIncrement(&rawLogs);
            if (rawNo <= 3) {
                float a[4], b[4];
                memcpy(a, raw, 16); memcpy(b, raw + 16, 16);
                Log("xr raw #%ld: +0 %.4f %.4f %.4f %.4f | +16 %.4f %.4f %.4f %.4f\n",
                    rawNo, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
            }
            if (qoff >= 0) {
                g_headQ[0] = q[0]; g_headQ[1] = q[1];
                g_headQ[2] = q[2]; g_headQ[3] = q[3];
                LONG seq = InterlockedIncrement(&g_poseSeq);
                if (seq == 1) {
                    Log("xr hook: first valid pose q %.3f %.3f %.3f %.3f views=%u offset=%d stride=%d\n",
                        q[0], q[1], q[2], q[3], n, qoff, stride);
                }
                // Freeze the view orientation: the picture stays square to the
                // eye, and the head movement is applied to the game camera.
                for (uint32_t i = 0; i < n; i++) {
                    unsigned char* v = raw + i * stride + qoff;
                    float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                    memcpy(v, identity, sizeof(identity));
                }
            }
        }
    }
    return r;
}

// The core resolves xrLocateViews once and stores the pointer in its own data
// at core+0x25A0A0.  Its per-frame locate call reads that slot, so replacing
// the slot is enough - the OpenXR loader itself is left untouched.
static void* g_poseSlot = NULL;

// Replace every writable copy of the real xrLocateViews pointer (the core's
// slot, the OpenXR dispatch tables and any cached copies) with our function.
static int PatchAllCopies(void* real) {
    int hits = 0;
    uintptr_t a = 0;
    const size_t CHUNK = 1u << 20;
    while (a < 0x7fffffffffffULL) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi))) break;
        int usable = (mbi.State == MEM_COMMIT) &&
                     (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE |
                                     PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) &&
                     !(mbi.Protect & PAGE_GUARD);
        if (usable) {
            uintptr_t s = (uintptr_t)mbi.BaseAddress;
            size_t total = mbi.RegionSize;
            size_t done = 0;
            unsigned char needle = (unsigned char)((uintptr_t)real & 0xff);
            while (done + 8 <= total) {
                size_t len = total - done;
                if (len > CHUNK) len = CHUNK;
                unsigned char* buf = (unsigned char*)(s + done);
                __try {
                    unsigned char* q = buf;
                    unsigned char* end = buf + len;
                    while ((q = (unsigned char*)memchr(q, needle, end - q)) != NULL) {
                        uintptr_t at = (uintptr_t)q;
                        if ((at & 7) == 0 && *(uintptr_t*)q == (uintptr_t)real &&
                            !InOurModule(q)) {
                            DWORD old = 0;
                            if (VirtualProtect((LPVOID)at, 8, PAGE_READWRITE, &old)) {
                                *(uintptr_t*)at = (uintptr_t)&OurXrLocateViews;
                                VirtualProtect((LPVOID)at, 8, old, &old);
                                hits++;
                            }
                        }
                        q++;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
                if (done + len >= total) break;
                done += len - 8;    // overlap: never split a pointer
            }
        }
        if (mbi.RegionSize == 0) break;
        a = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return hits;
}

static void* g_locateEntry = NULL;
static void* g_locateTramp = NULL;

// Copy the first n bytes of target into executable memory and jump back.
static void* MakeTrampoline(void* target, int n) {
    unsigned char* t = (unsigned char*)VirtualAlloc(NULL, n + 12,
                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return NULL;
    memcpy(t, target, n);
    t[n] = 0x48; t[n + 1] = 0xB8;
    *(uintptr_t*)(t + n + 2) = (uintptr_t)target + n;
    t[n + 10] = 0xFF; t[n + 11] = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), t, n + 12);
    return t;
}

static int InstallInlineHook(void* target, void* hookFn, int n) {
    unsigned char* p = (unsigned char*)target;
    DWORD old = 0;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    p[0] = 0x48; p[1] = 0xB8;
    *(uintptr_t*)(p + 2) = (uintptr_t)hookFn;
    p[10] = 0xFF; p[11] = 0xE0;
    for (int i = 12; i < n; i++) p[i] = 0x90;
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return 1;
}

// SteamVR's xrLocateViews prologue (checked before patching):
//   mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi / push rdi
static int HookRealLocate(void* entry) {
    static const unsigned char magic[16] = {
        0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18, 0x57};
    if (g_locateEntry == entry) return 1;
    __try {
        if (memcmp(entry, magic, sizeof(magic)) != 0) return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    void* t = MakeTrampoline(entry, 16);
    if (!t) return 0;
    g_realLocateViews = (PFN_XrLocateViews)t;
    if (!InstallInlineHook(entry, (void*)&OurXrLocateViews, 16)) return 0;
    g_locateEntry = entry;
    g_locateTramp = t;
    Log("pose hook: real xrLocateViews %p hooked (trampoline %p)\n", entry, t);
    return 1;
}

typedef int32_t (*PFN_GIPA_DIAG)(uint64_t, const char*, void**);

static void DiagResolve(HMODULE core, void* cur) {
    void* gipaPtr = *(void**)((uintptr_t)core + 0x259ff8);
    uint64_t inst = *(uint64_t*)((uintptr_t)core + 0x25a0c0);
    if (!gipaPtr || !inst) {
        Log("diag: gipa=%p inst=%p (not ready)\n", gipaPtr, (void*)inst);
        return;
    }
    PFN_GIPA_DIAG g = (PFN_GIPA_DIAG)gipaPtr;
    const char* names[] = {
        "xrLocateViews", "xrLocateSpace", "xrCreateReferenceSpace",
        "xrEnumerateViewConfigurationViews", "xrWaitFrame",
        "xrBeginFrame", "xrEndFrame", "xrCreateSession"};
    Log("diag: gipa=%p inst=%p slot=%p\n", gipaPtr, (void*)inst, cur);
    for (int i = 0; i < 8; i++) {
        void* p = NULL;
        int32_t r = g(inst, names[i], &p);
        unsigned char b[8] = {0};
        __try { if (p) memcpy(b, p, 8); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("diag: %-32s r=%d p=%p bytes=%02x%02x%02x%02x%02x%02x%02x%02x\n",
            names[i], r, p, b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7]);
    }
    Log("diag: slots %p %p %p %p %p %p %p %p\n",
        *(void**)((uintptr_t)core+0x25a080), *(void**)((uintptr_t)core+0x25a088),
        *(void**)((uintptr_t)core+0x25a090), *(void**)((uintptr_t)core+0x25a098),
        *(void**)((uintptr_t)core+0x25a0a0), *(void**)((uintptr_t)core+0x25a0a8),
        *(void**)((uintptr_t)core+0x25a0b0), *(void**)((uintptr_t)core+0x25a0b8));
}

static void InstallPoseHook(void) {
    HMODULE core = GetModuleHandleA("gh5vr_core.dll");
    if (!core) return;
    void** slot = (void**)((uintptr_t)core + 0x25a0a0);
    void* cur = *slot;
    if (!cur || cur == (void*)&OurXrLocateViews) return;

    // The pointer must point at executable code; ignore half-written slots.
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(cur, &mbi, sizeof(mbi))) return;
    if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return;

    Log("pose hook: core=%p slot=%p real=%p\n", (void*)core, (void*)slot, cur);
    DiagResolve(core, cur);
    int inlineHooked = HookRealLocate(cur);
    if (!inlineHooked) {
        g_realLocateViews = (PFN_XrLocateViews)cur;
        Log("pose hook: real prologue mismatch, using pointer copies only\n");
    }

    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return;
    *slot = (void*)&OurXrLocateViews;
    VirtualProtect(slot, sizeof(void*), old, &old);

    g_poseSlot = slot;
    int copies = PatchAllCopies(cur);
    InterlockedExchange(&g_hookReady, 1);
    Log("pose hook installed (inline=%d, extra copies=%d)\n", inlineHooked, copies);
}

static DWORD WINAPI XrThread(LPVOID) {
    Log("pose hook thread start\n");
    for (;;) {
        if (!g_hookReady) {
            InstallPoseHook();
        } else if (g_poseSlot && *(void**)g_poseSlot != (void*)&OurXrLocateViews) {
            // The core re-resolved its functions (VR restart): take it again.
            void* cur = *(void**)g_poseSlot;
            if (cur) {
                Log("pose hook: slot rewritten to %p, re-patching\n", cur);
                if (!HookRealLocate(cur)) g_realLocateViews = (PFN_XrLocateViews)cur;
                DWORD old = 0;
                if (VirtualProtect(g_poseSlot, sizeof(void*), PAGE_READWRITE, &old)) {
                    *(void**)g_poseSlot = (void*)&OurXrLocateViews;
                    VirtualProtect(g_poseSlot, sizeof(void*), old, &old);
                }
                int copies = PatchAllCopies(cur);
                Log("pose hook: re-patched %d extra copies\n", copies);
            }
        }
        Sleep(1000);
    }
}

static DWORD WINAPI MouseLookThread(LPVOID) {
    uintptr_t core = 0;
    while (!core) {
        core = (uintptr_t)GetModuleHandleA("gh5vr_core.dll");
        if (!core) Sleep(500);
    }
    while (!g_hookReady) Sleep(500);
    Log("mouse-look: started core=%p\n", (void*)core);

    bool rightDown = false;
    bool haveRef = false;
    float lastYaw = 0.0f, lastPitch = 0.0f;
    double accX = 0.0, accY = 0.0;
    LONG lastSeq = 0, staleTicks = 0;
    int lastMode = -1;
    bool wasFocused = false;
    bool hotkeyWasDown = false;

    for (;;) {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        bool focused = (pid == GetCurrentProcessId());
        bool cockpit = (*(volatile char*)(core + 0xd396c) == 0);
        // Sending RIGHTDOWN can make SteamVR briefly become the foreground
        // window.  Do not release the game look button during that transition;
        // cockpit state is the reliable lifetime for mouse-look.
        bool active = cockpit && (focused || rightDown);

        bool hotkeyDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                          (GetAsyncKeyState(VK_MENU) & 0x8000) &&
                          (GetAsyncKeyState(VK_F10) & 0x8000);
        if (hotkeyDown && !hotkeyWasDown && cockpit) {
            // Reassert the button after returning from another window. Do not
            // reset haveRef: the current head direction must remain stable.
            mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
            rightDown = true;
            Log("mouse-look: right down reasserted by Ctrl+Alt+F10\n");
        }
        hotkeyWasDown = hotkeyDown;

        int mode = (focused ? 1 : 0) | (cockpit ? 2 : 0);
        if (mode != lastMode) {
            lastMode = mode;
            Log("mouse-look: focused=%d cockpit=%d\n", focused ? 1 : 0, cockpit ? 1 : 0);
        }

        if (active && !rightDown) {
            mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
            rightDown = true;
            haveRef = false;
            accX = accY = 0.0;
            Log("mouse-look: right down\n");
        }
        // Windows/SteamVR may clear injected button state while another
        // window has focus. Reassert RIGHTDOWN when FH5 becomes foreground
        // again, without resetting the camera reference.
        if (focused && !wasFocused && cockpit && rightDown) {
            mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
            Log("mouse-look: right down reasserted after focus\n");
        }
        if (!cockpit && rightDown) {
            mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
            rightDown = false;
            haveRef = false;
            Log("mouse-look: right up\n");
        }
        wasFocused = focused;

        LONG seq = g_poseSeq;
        bool fresh = (seq != lastSeq);
        lastSeq = seq;
        if (active && !fresh) {
            if (++staleTicks > 300) {  // ~5 s without a pose update
                staleTicks = 0;
                Log("mouse-look: WARNING no head pose updates (seq=%ld)\n", (long)seq);
            }
        } else {
            staleTicks = 0;
        }

        if (active && fresh) {
            float x = g_headQ[0], y = g_headQ[1], z = g_headQ[2], w = g_headQ[3];
            float yaw = atan2f(2.0f * (w * y + x * z), 1.0f - 2.0f * (y * y + z * z));
            float sp = 2.0f * (w * x - y * z);
            if (sp > 1.0f) sp = 1.0f;
            if (sp < -1.0f) sp = -1.0f;
            float pitch = asinf(sp);

            if (!haveRef) {
                lastYaw = yaw;
                lastPitch = pitch;
                haveRef = true;
            } else {
                float dyaw = yaw - lastYaw;
                float dpitch = pitch - lastPitch;
                lastYaw = yaw;
                lastPitch = pitch;
                // Only discard a true runtime recenter, not a normal quick
                // head turn (which can easily exceed 0.5 radian).
                if (fabsf(dyaw) > 2.5f || fabsf(dpitch) > 2.5f) {
                    // Recenter jump from the runtime: drop the delta.
                    accX = accY = 0.0;
                    Log("mouse-look: pose jump yaw=%.3f pitch=%.3f (ignored)\n", yaw, pitch);
                } else {
                    // Dead zone to stop micro jitter.
                    if (fabsf(dyaw) < 0.0005f) dyaw = 0.0f;
                    if (fabsf(dpitch) < 0.0005f) dpitch = 0.0f;
                    accX += (double)dyaw * SENS_X;
                    accY += (double)dpitch * SENS_Y;
                    int dx = (int)accX;
                    int dy = (int)accY;
                    if (dx != 0) accX -= dx;
                    if (dy != 0) accY -= dy;
                    if (dx != 0 || dy != 0) {
                        mouse_event(MOUSEEVENTF_MOVE, dx, dy, 0, 0);
                    }
                    static int deltaLogs = 0;
                    if (deltaLogs < 20 && (dyaw != 0.0f || dpitch != 0.0f)) {
                        deltaLogs++;
                        Log("look delta yaw=%.5f pitch=%.5f mouse=%d,%d\n",
                            dyaw, dpitch, dx, dy);
                    }
                }
            }
        }
        Sleep(8);
    }
}

static DWORD WINAPI MainThread(LPVOID) {
    Log("NEWMOD start pid=%lu\n", GetCurrentProcessId());

    // Start the extra threads here, after the loader lock is gone.
    HANDLE hx = CreateThread(NULL, 0, XrThread, NULL, 0, NULL);
    HANDLE hm = CreateThread(NULL, 0, MouseLookThread, NULL, 0, NULL);
    Log("threads: xr=%p mouse=%p err=%lu\n", (void*)hx, (void*)hm, GetLastError());
    if (hx) CloseHandle(hx);
    if (hm) CloseHandle(hm);

    // Wait for the core to be loadable and loaded.
    HMODULE core = NULL;
    for (int i = 0; i < 60 && !core; i++) {
        core = GetModuleHandleA("gh5vr_core.dll");
        if (!core) {
            core = LoadLibraryA("gh5vr_core.dll");
            if (!core) Sleep(500);
        }
    }
    if (!core) {
        Log("core not loaded (err=%lu)\n", GetLastError());
        return 0;
    }
    uintptr_t base = (uintptr_t)core;
    Log("core=%p\n", (void*)base);

    // Wait until the core has found the presentation camera.
    uintptr_t pres = 0;
    for (int i = 0; i < 1500 && !pres; i++) {
        pres = *(uintptr_t*)(base + 0xd9678);
        if (!pres) Sleep(200);
    }
    if (!pres) {
        Log("presentation camera not found\n");
        return 0;
    }
    g_pres = (void*)pres;
    Log("presentation=%p\n", (void*)pres);

    // Install our setters and configuration.
    *(uintptr_t*)(base + 0xd9660) = (uintptr_t)&SetMatrix1;
    *(uintptr_t*)(base + 0xd9668) = (uintptr_t)&SetMatrix2;
    *(uintptr_t*)(base + 0xd8d18) = (uintptr_t)&SetFov;
    *(uintptr_t*)(base + 0xd8d20) = (uintptr_t)&SetJitter;
    *(uintptr_t*)(base + 0xd8d10) = (uintptr_t)&NoOp;
    *(int*)(base + 0xcf0b0) = 1;       // update condition
    *(unsigned char*)(base + 0x259b24) = 1;  // per-eye flag
    *(float*)(base + 0xcf68c) = 0.1f;    // near
    *(float*)(base + 0xcf6c8) = 10000.0f; // far
    Log("setters installed, fov written\n");

    F230 f230 = (F230)(base + 0xf230);
    E0C0 e0c0 = (E0C0)(base + 0xe0c0);
    uint64_t frames = 0;
    for (;;) {
        uintptr_t cur = *(uintptr_t*)(base + 0xd9678);
        if (cur) pres = cur;
        uintptr_t manager = *(uintptr_t*)(base + 0xd3f00);
        __try {
            if (manager) e0c0((void*)manager, 0.016f, 0, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (g_exc_logged < 3) { g_exc_logged++; Log("e0c0 exception code=%lu\n", GetExceptionCode()); }
        }
        __try {
            f230((void*)pres, 0);
            frames++;
            if ((frames % 300) == 0) {
                Log("update frames=%llu fov=%.4f %.4f %.4f %.4f\n",
                    (unsigned long long)frames,
                    *(float*)(base + 0xcf0d0), *(float*)(base + 0xcf0d4),
                    *(float*)(base + 0xcf0d8), *(float*)(base + 0xcf0dc));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (g_exc_logged < 3) { g_exc_logged++; Log("f230 exception code=%lu at frame %llu\n", GetExceptionCode(), (unsigned long long)frames); }
        }
        Sleep(16);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        g_hinst = hinst;
        g_sys = LoadLibraryA("C:\\Windows\\System32\\XINPUT1_3.dll");
        if (g_sys) {
            g_getstate = (PFN_XInputGetState)GetProcAddress(g_sys, "XInputGetState");
            g_setstate = (PFN_XInputSetState)GetProcAddress(g_sys, "XInputSetState");
            g_getcaps = (PFN_XInputGetCapabilities)GetProcAddress(g_sys, "XInputGetCapabilities");
            g_enable = (PFN_XInputEnable)GetProcAddress(g_sys, "XInputEnable");
            g_getbattery = (PFN_XInputGetBatteryInformation)GetProcAddress(g_sys, "XInputGetBatteryInformation");
            g_getkeystroke = (PFN_XInputGetKeystroke)GetProcAddress(g_sys, "XInputGetKeystroke");
        }
        HANDLE h = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
