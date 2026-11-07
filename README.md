# GrayHorizon FH5 1.656.386.0 Source Package

## Attribution and license

This project is based on **GrayHorizon FH5 VR** by GrayHorizon:

Copyright 2026 GrayHorizon  
Original project: <https://github.com/GrayHorizon-mod/Forza-Horizon-VR>

This package is a modified compatibility build for Forza Horizon 5 version
`1.656.386.0`. Changes include the 1.656 camera compatibility work, OpenXR
pose handling, fixed headset view behavior, and synthetic mouse-look.

The original GrayHorizon project is licensed under the **PolyForm Noncommercial
License 1.0.0**, not the Apache 2.0 License. See [`LICENSE.md`](LICENSE.md)
for the complete terms. Distribution and use must comply with that license,
including its noncommercial restriction and required copyright notice.

Third-party components retain their own licenses. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). This source package does
not grant rights to Forza Horizon 5 or any Microsoft/Xbox game files.

This folder contains the source and technical notes for the custom compatibility build targeting:

**Forza Horizon 5 - version 1.656.386.0**

The work was tested with an Oculus/Meta headset through SteamVR OpenXR.

## Source files

- `xinput1_3.cpp` - the x64 proxy DLL source.
- `vtable_patch.cpp` - compatibility/vtable patching source used while adapting the core.
- `README_FIX_ES.md` - the original investigation report and offset notes.
- `TECHNICAL.md`, `SETTINGS.md`, and `THIRD_PARTY_NOTICES.md` - GrayHorizon documentation.

## Build requirements

- Windows x64.
- Visual Studio Build Tools with the MSVC x64 toolchain.
- The original GrayHorizon/OpenXR development files.
- A compatible `gh5vr_core.dll` base for patching.

Build the proxy from a Developer Command Prompt or after loading `vcvars64.bat`:

```bat
cd newmod
cl /nologo /LD /O2 /EHsc xinput1_3.cpp /link /OUT:xinput1_3.dll
```

The source is intentionally Windows/MSVC-specific because it uses Win32 DLL proxying,
`user32.dll` mouse input, process memory protection, and x64 function hooks.

## What was changed

### Game compatibility

The unmodified GrayHorizon 1.2.1 core expected a newer FH5 build. The compatibility work:

1. Patched the version gate for FH5 1.656.386.0.
2. Rebuilt the camera/vtable compatibility layer for the 1.656 camera objects.
3. Added a proxy `xinput1_3.dll` that loads `gh5vr_core.dll` and forwards XInput calls to the system DLL.
4. Installed the 1.656 camera update and stereo update calls from the proxy.

The game camera objects changed layout between versions. Important 1.656 observations included:

- Multi-camera manager vtable: `0x71BD5C8`.
- Director vtable: `0x71B66F0`.
- Presentation vtable: `0x71BD388`.
- Active camera vtable: `0x71BD348`.
- Core manager global: `core + 0xD3F00`.
- Core presentation camera global: `core + 0xD9678`.

These offsets are version-specific and must not be reused blindly on another game build.

### OpenXR and SteamVR behavior

The original problem was that head rotation moved the whole SteamVR screen instead of turning
the FH5 cockpit camera. The proxy now:

1. Hooks the resolved `xrLocateViews` function used by the core/SteamVR.
2. Reads the real headset quaternion from the standard OpenXR view layout.
3. Supports the standard aligned `XrView` stride of 64 bytes used by this SteamVR path.
4. Returns identity view orientation to keep the game image fixed to the headset.
5. Converts changes in headset yaw/pitch into synthetic relative mouse movement.
6. Holds synthetic right mouse button while the game is in cockpit view.

The standard view pose was found at offset `+16`; writing the second view with a 56-byte stride
caused `XR_ERROR_POSE_INVALID`, so the final implementation uses a 64-byte stride.

### Mouse-look

The FH5 camera only turns while right mouse is held. The proxy uses `mouse_event` to reproduce
that behavior. The final sensitivity is approximately half of the first stable test value:

```cpp
SENS_X = -45.0f;
SENS_Y = -45.0f;
```

After focus changes, Windows/SteamVR can lose injected button state. The proxy attempts to restore
it automatically and also exposes:

**Ctrl+Alt+F10** - reassert synthetic right mouse button while in cockpit.

## Configuration used for the final test

```ini
per_eye=1
canted=2
stable_camera=1
sync_game_camera=1
resolution=auto
```

Per-eye canted rendering is required for the proper stereo presentation. The earlier test with
`per_eye=0` and `canted=0` reduced apparent zoom but produced a single wide image that behaved
like a floating screen in SteamVR. The final package therefore uses real per-eye rendering.

## Safety and scope

- Do not modify Forza Horizon 5 executable or data files.
- Install only the mod files into the game directory.
- Keep a backup before replacing an existing GrayHorizon installation.
- This build is for FH5 `1.656.386.0` only.
- Online use is at the user's own risk because the mod hooks camera/rendering and injects mouse input.
