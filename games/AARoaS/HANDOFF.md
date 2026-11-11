# Handoff: America's Army: Rise of a Soldier (Xbox) static recompilation

Read this first. It records what an earlier Claude session (in the Claude app, which had file access to this
folder but couldn't run commands) found and set up. Nothing has been built or run yet.

## Goal

Turn the original Xbox game *America's Army: Rise of a Soldier* (USA) into a native 64-bit Windows exe using the
**Xbox XBE Recomp** SDK in this folder, then fix whatever stops it from booting and playing.

## Folder layout (root = `C:\Users\evper\Desktop\AA-RoaS Recomp`)

- Root: the **Xbox XBE Recomp SDK** (v0.1.0): `cmake\XbeRecomp.cmake`, `runtime\` (kernel/D3D8/DSound/XAPI/XNet
  reimplementations, D3D12 renderer, XAudio2), `tools\recomp` (x86 to C++ recompiler), `tools\symbols`
  (`xbe_symbols` signature scanner, `xbe2pe.py` for Ghidra), `xdk\{4831,5659,5849}` (layouts and signatures),
  `templates\game`. Read the root `README.md` for how the SDK works.
- `Game\`: the extracted game: `default.xbe`, `update.xbe`, `dashupdate.xbe`, data folders (`MapsXBoxL`,
  `System`, `SystemXBoxW`, `TexturesXBoxW`, `SoundsXBox`, `MoviesXBox`, `UI`, `UIX`, ...), plus the full disc
  image `America's Army - Rise of a Soldier (USA).xiso.iso` (~2 GB) and `extract-xiso.exe`.
- `games\AARoaS\`: **the game project created for this port** (see below).

## What's known about default.xbe

- Title: "America's Army:RoaS". Base 0x10000. It's Unreal Engine 2 based.
- **All XDK libraries are build 5849**: XAPILIB, D3D8, D3DX8, XGRAPHC, XBOXKRNL, XONLINES, XMV, XVOICE,
  XVOCREC, VOICMAIL, XACTENG, UIX, DSOUND, LIBCP, LIBC. `xdk\5849\signatures.toml` and `layout.toml`
  exist, so `xbe_symbols` should work. The 5849 layout was proven on Splinter Cell Double Agent.
- Sections: `.text` 0x11000 (size 0x3AAF8C, large), D3D, D3DX, XGRPH, XONLINE, XNET, XMV, XACTENG, DSOUND,
  WMADEC, XPP, `.rdata`, `.data`, XON_RD, `.data1`, DOLBY, plus `$$XTIMAGE`/`$$XSIMAGE`/`.XTLID`.
- Likely trouble spots: XONLINE/XNET (Xbox Live), XACTENG (XACT audio, `.xsb`/`.xwb` banks), WMADEC (WMA
  decoder), XMV (movies are `.xmv`, not Bink), XVOICE. Expect unmatched symbols in these, which become trapping
  stubs.

## The game project: `games\AARoaS\`

Created from `templates\game`. Because it sits two levels under the SDK root, its `CMakeLists.txt` finds the SDK
automatically through `../../cmake/XbeRecomp.cmake`; no `sdk\` copy is needed.

- `CMakeLists.txt`: `project(aaroas)`, `add_xbe_image(AARoaS default.xbe game.toml ${GAME_PARTITIONS} game)`.
- `build.bat`: template with `GAME_TITLE`/`GAME_EXE=AARoaS` set. Two changes:
  1. On first run it creates a **junction `assets` pointing to `..\..\Game`** (`mklink /J`), so game files aren't
     copied. The runtime finds `assets` by walking up from the exe dir.
  2. Removed `>nul` from `cmake --preset release` and the `xbe_symbols` build so their errors show.
- `run_build.bat`: sets `CI=1` (skips all prompts), runs `build.bat`, writes **all output to
  `build_log.txt`**, ends with a line `BUILD_EXIT <code>`.
- `run_game.bat`: runs `out\build\release\AARoaS.exe`, logs to `run_log.txt` (`RUN_EXIT <code>`). The game's
  own log (prefix `aaroas`) and `CrashDumps\` are written beside the exe.
- `config\game.toml`: **placeholder**. `build.bat` replaces it on the first build via
  `xbe_symbols assets\default.xbe <sdk>\xdk --library AARoaS.dll --out config\game.toml`. The rebuild only
  regenerates it if `[symbols]` is missing.
- `src\common\game_info.cpp`: `app::kGame` filled in (title, name `AARoaS`, log `aaroas`,
  images `{{"default.xbe","AARoaS"}}`), all hooks null.
- `src\game\hooks.cpp`: empty template. Per-function overrides go here (`X86_FUNC(sub_XXXXXXXX)`, call
  `__imp__sub_XXXXXXXX` for the original).
- `release.bat`: `GAME_EXE=AARoaS`. Not needed until a release zip is wanted.

## The user's PC toolchain (verified)

- **Visual Studio 2026 Community (v18.11.1)**, not 2022. `build.bat` uses `vswhere -latest`, so it should still
  find it. Install path: `C:\Program Files\Microsoft Visual Studio\18\Community`.
- clang 22.1.3 (VS's LLVM, `VC\Tools\Llvm\x64\bin`), CMake 4.3.1, Ninja present, Git 2.56.1.
- These are newer than the SDK was likely tested with: watch for new Clang errors/warnings and CMake 4
  policy errors in fetched deps (Zydis v4.1.1, toml++ v3.4.0, imgui v1.91.0, SDL3 release-3.4.16). CMake 4 rejects
  `cmake_minimum_required` below 3.5; if a dep trips that, `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` in
  `CMakePresets.json` cacheVariables is the usual fix.
- In a plain terminal, VS tools aren't on PATH; `build.bat` runs `VsDevCmd.bat` itself.

## Plan / next steps

1. From `games\AARoaS`, run `run_build.bat` (or `build.bat` with `CI=1`). Expect the first full build to take a
   long time (the script estimates 30 to 90 minutes). Check `build_log.txt` as it runs.
2. After `xbe_symbols` writes `config\game.toml`, review its reported misses/ambiguous names (left at 0 means a
   trapping stub). Fill in what can be found, e.g. by loading `python ..\..\tools\symbols\xbe2pe.py
   config\game.toml game.exe` output in Ghidra, or add `[[function]]`/`[[switch]]`/`data_ranges` overrides if
   analysis goes wrong.
3. Fix compile errors (SDK runtime vs. Clang 22, generated code), then build until `out\build\release\AARoaS.exe`
   exists.
4. Run it with `run_game.bat`, read the `aaroas` log / crash dumps, and fix boot problems with hooks in
   `src\game\hooks.cpp` or `[[hook]]` entries in the config. Work toward: menus, then a training map, then
   missions.
5. Have the user test controls, audio and video. Only they can judge whether gameplay is correct.

## Progress (2026-11-10)

- **Builds and boots.** Full build ~5 min. Intro XMV movies play with audio and video, no flicker.
- `NoDefaultCurrentDirectoryInExePath=1` is set on this PC: `cmd` won't run a `.bat`/`.exe` from the current folder
  by bare name. All scripts now call by full path (`%~dp0...`). Keep that in any new script.
- `>nul` is now really removed from the configure and `xbe_symbols` steps in `build.bat`.
- **D3DDevice_UpdateOverlay** (0x3C37B0, 5 args, no 5849 signature) wrote NV2A registers and crashed. Hooked in
  `src\game\hooks.cpp` to call a new SDK function `d3d::PresentOverlay` (`runtime\xdk\d3d8.cpp/.h`): it decodes the
  YUY2 frame to a linear X8R8G8B8 surface, and `D3DDevice_Swap` shows that surface instead of the back buffer while
  overlay updates keep coming (250 ms timeout; `EnableOverlay(FALSE)` ends it). `EnableOverlay` HLE moved next to it.
- **0x38B8D8** (`_except_handler3`, only reached via pushed SEH handler) added to `[functions]` in `config\game.toml`;
  it caused a trap ~80 s in.
- Still open: the ~50 missing symbols from `xbe_symbols` (mostly DirectSound 3D/buffer calls, a few D3D, sockets,
  Bink) trap if called; not yet reached. Menus beyond the movies not yet checked.
- Close a running `AARoaS.exe` before rebuilding, or linking fails with "permission denied".
- `build.bat` now touches `AARoaS.dll` after each build: a runtime-only change relinks only the exe, and the exe then
  rebuilt the DLL itself at launch (its "compiler" folder flow).

### Later the same day: boot to the first loading screen
- The white screen after the movies was the game's loading-screen thread (0x302FC0) spinning while the main thread
  hung. Its unused background textures are null on purpose.
- **SDK fix, exceptions** (`runtime\kernel\xboxkrnl.cpp`, `CallHandler`): NT's dispatcher links a marker frame on
  fs:[0] around each handler; MSVC's `_UnwindNestedFrames`/`_JumpToContinuation` rely on it. Without it a catch linked
  the catching frame to itself and the next exception looped forever. Now a marker (host thunk returning
  ContinueSearch) is pushed per handler call.
- **SDK fix, files** (`runtime\kernel\files.cpp`, `NtSetInformationFile` rename): Windows can't rename a file held
  open by `fopen`; the game copies disc files to `Z:\Cache.tmp` and renames them into the Z: cache, so every cache
  write failed and Unreal threw "ConfigNotFound". The file is now closed around the rename and reopened. Also honours
  ReplaceIfExists. After this the title/ESRB screens appear.
- **XACT clock**: `[[hook]]` at 0x455316 (`mov eax,[0xFE80200C]`) calls `ReadSampleTime` (in `game.toml`).
- **`CDirectSoundStream_SetFrequency = 0x45C266`** set by hand (thunk 0x45C836, 2 args).
- Thrown Unreal errors are `char*` (narrow), not TCHAR: decode exception params[1] -> object -> char*.
- ~~Stop at ~92 s, UIX~~ fixed, see below.

### Later still: to the title screen (no crash for 90 s+)
- **UIX init** failed with `XONLINE_E_NOT_INITIALIZED` (0x80150005) from `XOnlineGetUsers` (0x3F2E56): the runtime's
  XOnlineStartup is a stub. Hooked in `hooks.cpp` to return no accounts.
- `0x30740` added to `[functions]` (called through a pointer after UIX setup).
- **Recompiler** (`tools\recomp\codegen_int.cpp`): added 16 bit `pushf`/`popf` (66 9C / 66 9D).
- **Coroutines**: the game's scripted state code yields and resumes via a hand-written dispatcher at 0xEE130 and
  globals 0x67C210..0x67C224 (845 yield sites). Replaced 0xEE130 in `hooks.cpp`: [0x67C214] now points at a host
  thunk that throws back to the dispatcher frame, which runs the exit once. Resume points are already function entries.
- **DirectSound names added by hand** (XACT calls them on the runtime's own voices): `IDirectSound_CreateSoundStream`
  0x45DEF5, `CDirectSoundStream_SetOutputBuffer` 0x45CF69, and SetPitch/SetLFO/GetVoiceProperties/Set3DVoiceData
  (+ stream Use3DVoiceData) for buffer wrappers 0x45C69E.. and stream thunks 0x45C857.. (see the comment in game.toml).
  `0x45C836` (stream Use3DVoiceData via the IDirectSoundStream adjuster) is a no-op hook.
  Lesson: a no-op is wrong for getters (GetVoiceProperties left a count uninitialised and XACT walked off the stack).
- **SDK**: crash log now names the recompiled function containing the faulting host address
  (`dispatch::GuestAt`, used in `main.cpp`).
- ~~Dark/flat menu~~ fixed, see below.

### Main menu renders (user confirmed controller input and menu audio work)
- **SDK, vertex shader handles** (`gpu/state.h` `IsShaderHandle`, used in vertex.cpp, shaders.cpp, d3d12.cpp,
  d3d8.cpp): this game passes FVF codes with bit 0 set (0x45, 0x145, 0x345). A shader handle is now "bit 0 set and
  above 0xFFFF"; anything else is an FVF.
- **SDK, immediate mode** (`d3d8.cpp` `EndPrimitive`): begin/end primitives under an XYZRHW FVF were drawn as 3D.
  Now drawn with their slot layout (handle 0xFFFFFFF1) and `State::im_pretransformed`, which `Pretransformed()` honours.
  The menu UI is drawn this way.
- **`D3DDevice_SetPalette = 0x3BEB40`** by hand (no 5849 signature; stores device+0xF98, CDevice_Palettes). The font
  and UI pieces are P8.
- Colour order of immediate-mode diffuse (4UB, 0x1940) looks right in the captures.

### In-game: training map renders (attract demo, captured frame looks right)
- **`D3DDevice_BeginPush = 0x3C3080`, `D3DDevice_EndPush = 0x3C30A0`** by hand. The game writes raw push buffer
  (begin 0x17FC + inline array 0x1818) for terrain/skinned meshes; the original BeginPush flushed state through
  0x3C9840, which reads the device's vertex shader at +0x794 that the host's SetVertexShader never fills.
- **`DirectSoundUseFullHRTF` set to 0** (left to the original at 0x45AF2D): it only fills XACT's 3D function table at
  0x47952C; the host's no-op left it null and the 3D sound pass called through null.
- Left alone on purpose: many unnamed D3D helpers the game calls (resource/lock/XG code that stays recompiled).
  Name one only when a crash leads there; check first whether it reads device fields the host's HLE doesn't keep.
- User played: first mission completed, second started; controls, audio and performance good.
- Overlay: the title/ESRB screens are near-still XMVs (~8 fps, uneven gaps), which flickered with the 250 ms
  timeout. The ESRB movie holds its still picture ~3 s without frames, so any timeout flickers. Now the overlay stays
  on from UpdateOverlay until EnableOverlay(FALSE), no timeout; the log confirms the game calls EnableOverlay(0) at
  the end of every movie (finish handler 0x2F44C0, call at 0x2F44D0) and menus show after.
- Temporary diagnostics: all removed (2026-11-10). Kept on purpose: `main.cpp` DumpThreads (F3) logs thread headers
  at report level and 768 stack words; crash log names the recompiled function (`dispatch::GuestAt`); an unhandled
  C++ throw also logs its text read as bytes ("thrown (bytes)"), since this game throws char*.
  To trace file opens again, temporarily redefine LOG_FIRST as LOGR at the top of `files.cpp`.
- Reference project `Reference\SCDA-XBERecomp-3` is an older snapshot of this SDK (Splinter Cell DA, Bink not XMV);
  its exception code matched ours, so the fixes above are new. Its hooks are game-specific.
- Testing without the user: launch hidden, `Start-Sleep`, then `PostMessage` WM_KEYDOWN VK_F3 to dump threads.

## Future: PC features the user wants (paused 2026-11-10)

Mouse and keyboard, 16:9 widescreen, resolutions up to 4K, FoV, SMAA, ambient occlusion, and a settings overlay.
- **Our SDK today**: no in-game overlay (`runtime\app` has no menu.cpp), though Dear ImGui is linked and the window
  procedure already forwards input to it. Settings come from a TOML file beside the exe (`runtime\app\settings.cpp`):
  display mode, width/height, render_percent (render scale), fps_cap, vsync, anisotropic filtering, texture
  replacement/dumping, audio output, plus game options through `kGame.options`.
- **Reference `Reference\SCDA-XBERecomp-3`** (MIT licence, keep its notice when copying): F10 ImGui settings overlay
  (`src\app\menu.cpp`, settings.cpp, translations, cursor.cpp), raw-input mouse/keyboard with rebinding
  (`src\core\input.cpp`, `src\game\mouse.cpp`), SMAA (`src\gpu\smaa_shader.h`, smaa_textures.h), ambient occlusion
  and FoV (`src\game\ambient_occlusion.cpp`, fov.cpp), widescreen/HUD (`src\versus\widescreen.cpp`, `src\game\hud.cpp`),
  and Unreal object/property lookup by name (`src\game\unreal.cpp`). Its renderer is an older snapshot of ours, so
  port pieces by adapting them, not by copying files over.
- Generic parts (overlay, SMAA, resolution) belong in the SDK. Game parts (FoV, mouse look, 16:9 HUD) need this
  game's own addresses: both games are Unreal Engine 2, so the reference's approach carries over, its addresses don't.
- Version control: local git repository at the SDK root (branch `main`, first commit f61e420, 2026-11-10), placeholder
  author "AA-RoaS Recomp <aaroas@localhost>" set for this repo only, no remote. Not tracked: `Game\`, `Reference\`,
  `generated\`, `out\`, captures and logs (see the two .gitignore files). Never push game data anywhere.

## The user's preferences

- Wants scoped access: commands run only in this project folder, with approval for anything unfamiliar. They
  declined desktop-control "computer use".
- **Don't delete files.** If something must go, move it to a `_to_delete\` folder and say so.
- Don't edit `Game\` (their original extracted files). Generated code (`generated\`) and build output (`out\`)
  are fine to regenerate.
- Keep updates short; tell them when they need to click or test something.
