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
- **Crash after spending upgrade points** (2026-11-11): the game's only setjmp (0x38BCE0, returning to 0x3776B9 in
  0x377680, a Lua-style protected call) is longjmp'd to after a script error; 0x3776B9 was no entry. Fix: new
  recompiler option `[xbe] continuations = [...]` (tools\recomp main.cpp, analysis.cpp) makes it an entry and a
  continuation, and codegen_emit.cpp now gives the try/X86Resume wrapper to a function that holds or falls into a
  continuation. Set in game.toml. User confirmed the crash is gone.
- 0x45C836 is `IDirectSoundBuffer_Use3DVoiceData` (XACT's buffer path at 0x453DB4, twin of the stream call at
  0x453D74), not a stream adjuster: it had been a no-op, so buffers never took XACT's 3D voice data. Named in game.toml.

### Audio fixed (2026-11-11, user confirmed: gunfire, explosions, every dialogue line, skips cut cleanly)
- **Submixes** (`dsound.cpp`): XACT sends 3D sounds (gunfire, explosions) through a DSBCAPS_MIXIN (0x2000) buffer
  chosen with SetOutputBuffer. The host ignored it, so they were silent. Now a source's dry sound feeds its submix's
  input and the submix plays after its sources.
- **Async FlushEx** (flags & 1): XACT stops a cue with FlushEx(ASYNC) while its reads are still landing. Packets
  Process'd after it now come back flushed (`flushing` state, cleared by Pause, PauseEx, SetFormat, Flush). Before,
  a skipped briefing line kept playing under the next.
- **Dialogue silent ~50%**: mission dialogue streams from `R4_TUNNEL.xwb` etc. (cue names in the .xsb, e.g.
  `R4_S0B_E1_1_LEARY`; the `I-*.xwb` banks are registered but not used here). XACT's packet setup (0x45970B, read
  submit 0x457FA7) marks a packet with no data left 0xDEAD and then asks 0x450695 whether any read is still pending
  (iosb Status 0x103). The host finished reads inside NtReadFile, so for a wave that fits in ONE read the answer was
  no, XACT cleared its "packets in flight" flag and never Process'd the data: the cue ended silently. Fix in
  `files.cpp`: a read on a handle opened without FILE_SYNCHRONOUS_IO (options 0x30) with no event and no APC
  returns STATUS_PENDING and completes ~2 ms later on a worker thread (`FinishLate`), as on the console.
- Also: stream GetStatus now reports DSSTREAMSTATUS PLAYING/PAUSED/STARVED (XACT ends a streamed cue when PLAYING
  drops), and Pause(3) = pause without starting the voice.
- Display: `aspect` setting (`runtime\app`, 0 auto, 1 4:3, 2 16:9). This game defaults to 4:3 (`game_info.cpp`):
  pillarboxed, and the console reports a 4:3 TV. User confirmed it looks right.
- Overlay: the title/ESRB screens are near-still XMVs (~8 fps, uneven gaps), which flickered with the 250 ms
  timeout. The ESRB movie holds its still picture ~3 s without frames, so any timeout flickers. Now the overlay stays
  on from UpdateOverlay until EnableOverlay(FALSE), no timeout; the log confirms the game calls EnableOverlay(0) at
  the end of every movie (finish handler 0x2F44C0, call at 0x2F44D0) and menus show after.
- Temporary diagnostics: all removed (2026-11-10, and the sound ones 2026-11-11). Kept on purpose: `main.cpp` DumpThreads (F3) logs thread headers
  at report level and 768 stack words; crash log names the recompiled function (`dispatch::GuestAt`); an unhandled
  C++ throw also logs its text read as bytes ("thrown (bytes)"), since this game throws char*.
  To trace file opens again, temporarily redefine LOG_FIRST as LOGR at the top of `files.cpp`.
- Reference project `Reference\SCDA-XBERecomp-3` is an older snapshot of this SDK (Splinter Cell DA, Bink not XMV);
  its exception code matched ours, so the fixes above are new. Its hooks are game-specific.
- Testing without the user: launch hidden, `Start-Sleep`, then `PostMessage` WM_KEYDOWN VK_F3 to dump threads.
- Frame capture for the user: `run_capture.bat` sets XBE_CAPTURE_DIR; F3 writes `out\build\release\capture\<time>\
  frame.txt` (every draw: shader, state, first vertices, constants) plus targets and shaders.

## PC features done (2026-11-11, user confirmed each)
- **F10 settings overlay** (`runtime\app\menu.cpp`, Consolas sized to the monitor, laid out after the SCDA build's
  from the user's screenshots): tabs Recomp Settings / Mouse and Keyboard / Controller (Q/E switch), rows stepped
  with arrows, key rows with two slots (click then press; right click or Delete clears; Esc cancels), Back /
  Defaults (the tab shown) / Apply. Changes go to a copy until Apply; the controller reads idle while it is open.
  The exact SCDA look could be checked against `Reference\SCDA-XBERecomp-3\src\app\menu.cpp`.
- **Keyboard and mouse** (`runtime\core\keyboard.cpp`, `src\game\mouse.cpp`): keys and mouse buttons press
  controller port 0 through GameInfo::key_actions (rebindable, saved as [keys] in settings.toml; Slow Walk pushes the
  keyboard's stick 50%, 35% is in the game's dead zone). Mouse look is direct: raw counts (WM_INPUT) go into the
  PlayerController's Rotation, viewport +0x34 (UPlayer::Actor) +0x124 (Pitch, Yaw), 4 units a count at 1.0, scaled
  by the camera's FOV / 75 when aiming. The game's own path (UEngine::InputEvent IK_MouseX/Y after UpdateInput
  0x2F4D00) had a console turn curve: flicks accelerated. That location is checked against the world camera's
  rotation (camera scene node +0x1E0) and searched for again if the camera does not follow. The cached
  `hdd\Z\System\xboxuser.ini` gets MouseAccelThreshold=0 and MouseSmoothingMode=0 at each start (PatchUserIni).
  User confirmed: movement, keys, mouse (feels much better), slow walk. Known: the mouse turns the view behind the
  pause menu (fixed: see game menus below).
- **Game menus with the mouse, phase 1** (user confirmed): a game menu is open when no 3D is drawn (front end) or
  LevelInfo.Pauser is set (pause menu; found by name through Actor.Level, aim_assist.cpp GamePaused);
  input::SetGameMenu frees the cursor and stops mouse look, and keyboard.cpp maps left click to A, right click to
  B, the wheel to D-pad up and down there. The menus are not Unreal's: Sidecar (CSidecarUI, CElement, behaviors:
  scripts in UI/XBox/*.pkg with messages InputMessage, NOTIFY_selected / deselected, BUTTON_action and so on,
  miSelectedItem, GetChildByPosition). CElement::Draw 0x36B170 (+0x30 visible byte, +4 behaviour count, +8
  behaviours, each vtable[0] draw, then the children in +0x14), CElement::Send 0x36B2A0, CElement::Create
  0x36AD30.
- **Weapon wheel with the mouse** (GameInfo::radial_menu_analog = 3, keyboard.cpp RadialOpen; user: "works
  beautifully"): while the key bound to Y (Switch Weapon) is held, raw mouse movement points the left stick (held to
  120 counts so turning back swings straight over, 25 dead) instead of looking; the movement keys are left out.
- **Default keys** are the user's own layout (2026-11-12, game_info.cpp key_actions).
- **Menus have their own keys** (core/keyboard.cpp MergeKeyboard, while input::SetGameMenu): A Enter / Space / a click
  on an item, B Esc / Backspace / right click, X X, Y Y, the shoulders (tabs, pages) Q E, D-pad arrows and wheel;
  play's bindings are left out there. Keys held as menu and play switch count only once let go (Esc that opened the
  pause menu closed it at once). Enter and a click also press Start on the title page (only before any menu item was
  ever told selected: the mission select sends no selection, and took A and Start as two presses). Esc in the pause
  menu: B, then Start 250 ms later when fewer than 8 new elements were drawn (B did nothing: the first page). Every
  pause page is made afresh with new elements and the HUD's lists linger beneath, so recognising the first page by
  element or by box failed. User confirmed all of it.
- **Button icons follow the device** (input::UsingPad: the last of a key, a mouse move or click, or a controller
  button, trigger or stick well off centre): replacements under `textures\replacement\@keyboard\` form a group
  (texturedumper SetPackGroup, any `@name` folder), on while the keyboard and mouse are used. `tools\make_key_icons.ps1`
  draws them at 4x: the menus' atlas 128x128_cee43c3773341320 (A X B / Start Y / L Black / White R) as Enter X Esc /
  Esc Y / Q Tab / Z E, the HUD's A 32x32_e4dd91ad26fcd422 as F. User confirmed the switch. The tutorial boxes use
  T-UIAAC.utx's buttons_xbox_* (32x32 DXT5, format 15; their A is the HUD's): keys worked out from the package (the
  level 0 data is the export's last 1034 bytes but 10) and play's icons (the HUD's A, the tutorial buttons) are drawn by the game with the player's keys
  (src\game\key_icons.cpp, GDI+): at the first frame and on a rebind, saved over the @keyboard images, which the pack
  watcher loads again. The menus' atlas stays make_key_icons.ps1's fixed keys (the user wants menu navigation safe from
  odd bindings). User confirmed a rebind showing.
- **Rumble only while the controller is in use**; switching to keyboard or mouse stops its motors (user confirmed).
- **Text and file patches** (GameInfo::patch_file, kernel/files.cpp): a disc file's stand-in in `overrides\` beside the
  exe is read instead, written from the game's patch the first time the file is opened; the cache drive's (Z:) copy
  is patched in place (the game copies `UI\XBox\*.pkg` there and reads that). text.cpp: strINTX.pkg's strings are
  type 4, length, UTF-16, so a string keeps its length (shorter ones padded): "PLEASE PRESS ENTER TO BEGIN", "Use Q
  and E to view" (the Personnel Jacket's pages). User confirmed the title text.
- **The mission select with the mouse** (menus.cpp FollowSelectorRow; user confirmed): its roles and missions rows send
  no selection message. Their highlight (the white corners) is a child of the row that slides onto the selected tile
  (roles 00F204D4 pos 7, ~62 px a tile; missions 00F882E4 pos 10, ~86 px), found by an F9 probe logging elements that
  jump. Found each frame: siblings of one size in a line plus a sibling 0.8 to 1.6 tiles in size whose centre is in
  one. Hover presses Left / Right (Up / Down for a column) in a burst for the distance, then checks once the
  selector stands still (waiting for it each step was slow); a burst that leaves it where it was marks the tile locked.
  With such a row on the page a click off its tiles does nothing. A moves from roles to missions, Esc back.
- **Game menus with the mouse, phase 2** (`src/game/menus.cpp`; user: "good enough to move forward"): hover selects
  the item under the cursor, a click picks it, clicks off the items do nothing. Each element's box comes from the
  screen space draws made while it is on top of a stack kept in a CElement::Draw hook (d3d::OnScreenDraw gives each
  XYZRHW draw's box in 640x480 page pixels); element +0x2C its place among its siblings. The selection: Send type
  0x17 with 0x12C (selected) or 0x12D (deselected) in its third word, sent to the item and then down through all
  inside it (the first receiver of a message is the item); type 0x15 tells the main menu's foot line which text to
  show. Searches of all memory for a number or an element pointer moving with Up and Down found nothing; logging
  the rarer messages (not 2 8 9 B 13: every frame or broadcast) found it. Hover presses Up / Down (Left / Right for
  a row) through input::PressMenuButtons, 2 frames held and 1 let go (a press held into the next looked like one
  long hold; 1 frame was sometimes not seen), one step at a time, each once answered. A step answered that leaves it
  no nearer (a disabled row: the menu skips it and wraps), or 3 unanswered, gives the row up until the cursor goes
  to another (it looped endlessly). States kept per item, so a page left and come back to needs no new message; a
  page whose selection was never told gets one Down when the mouse moves. Could be faster: the steps are the limit.
- **Auto-Sighting, Acquisition Icon and Target Zoom** (overlay rows in the Mouse and Keyboard and Controller tabs;
  mouse.cpp GameOptionsSync, aim_assist.cpp; user confirmed 2026-11-14):
  - Auto-Sighting (Settings::aim_assist) and Acquisition Icon (Settings::acquisition_icon) are the game's own Game
    Options: the player's options object (PlayerOptions: controller -> 0xEF670 -> 0x1AF00 -> 0x3DB80) +0x84, one bit
    each, not in the menu's order: Auto Sighting 0x01, Acquisition Icon 0x08 (0x3F all on; found by toggling each in
    the menu). Synced both ways every frame from the UpdateInput hook (menus too): an overlay change is written into
    the option, a menu change or a profile loading comes back into the setting; at first sight the setting wins.
    Auto Sighting off stops the lock's pull and snapping at the source (user, after trying it on the Xbox too).
  - Removed 2026-11-14: our own Aim Assist off, which undid the lock's pull after each tick (aim led while firing,
    the lock's turn 0x22520 skipped). The game's option does it fully. The lock's 30 Hz stepping (recoil.cpp
    0x32DB0) stays, for Auto Sighting on.
  - Target Zoom: with a target held (SelectedTarget +0xA0C, the red dot's, or LockTarget +0xA04 on the
    AACPlayerController, found by name) the game zooms by its distance, Auto Sighting on or off. Off, the projection
    is drawn at the angle from before the target (ZoomHold). The mouse's turn follows that angle either way. Zoom
    Sensitivity (on/off, Strength 10-100%) slows it by (zoomed / before)^strength; off by default. User confirmed.
    OPEN (2026-11-14, M249, Target Zoom off): the zoom flips and sometimes sticks. Probed: the M249's sights (and its
    bipod, the sighted pose) zoom to 45 deg (AACTuning.ini WP_Sighted fZoomFOV=45; the M16A2's 55), no target
    reported; a target held zooms on to ~25 deg. ZoomHold's flaw: with a target it shows the angle from before the
    target (usually 75: the sights' own zoom hidden too, hence the flipping), and a target taken mid-zoom freezes
    that base (stuck at 45 after the bipod is lifted, until the target is lost). DesiredFOV, FovAngle, DefaultFOV,
    AutoZoom are not in the name table (native only); ZoomAmount, ZoomRate are, not on the controller. Planned fix
    (not built): with a target held or let go within 1.5 s, show max(game fov, wanted), wanted = the weapon's sights
    angle while aiming (pawn +0x601 == 3; learned per weapon (Pawn.Weapon) as the lowest angle while aiming with no
    target for 1.5 s; unknown yet: the game's angle) else the normal angle; only zooming deeper is held back, so
    nothing freezes. g_shown_fov = that for the mouse's turn.
    Tried 2026-11-14 and reverted (user: "still randomly decides the zoom level", either the default angle when not
    aiming or zoomed): +0x601 == 3 is likely not a reliable "aiming" (3 is also firing; the sights may not set it),
    and the learned angle depends on the order the player aims. Next idea: tell the target's zoom apart by the game's
    own call that sets the target zoom (find the code writing the camera FOV toward the target's angle) rather than
    by angles and states.
  - This build keeps variable names in memory but not function names. Name entries: index, flags 0, hash link, text;
    objects: +0x20 name, +0x24 class; classes: +0x2C super, +0x38 first field, fields chained at +0x30, a property's
    byte offset at +0x44 (learned from Actor.Rotation, 0x124).
  - Release builds drop LOGI: diagnostics must use LOGR (one session of data was lost to this).
- **Recoil System** tabled 2026-11-14 (GameInfo::weapon_tuning not set: no row, the game's values always): the mouse
  trigger squeeze made it unneeded. Its code (tuning.cpp, files.cpp's patching from the disc) stays for later.
- **Widescreen** (`src\game\widescreen.cpp`): FCameraSceneNode::UpdateMatrices 0x1F3E50 builds the projection with
  FPerspectiveMatrix 0x1F1AC0 (call returns to 0x1F42F7); M[0][0] *= 0.75 on 16:9 (Hor+).
- **Field of View**: the world camera (75 deg, first UpdateMatrices of each Swap) gets M00 and M11 / FovScale; the
  weapon camera (47 deg, second) keeps its angle. Its node class is also the menus' camera, so order tells them apart.
- **2D that follows the world** (names over heads, the sun's flare; screen2d.cpp Moved): a new draw the size of one of
  last frame's a little way off is the same thing moved, and its run is centred (anchored by thirds it jumped side
  to side on 16:9; user confirmed fixed). But the lens flare is placed through the camera's projection, already widened: narrowed
  again its core sat apart from the sun (two suns in 16:9). Additive draws that follow the world are kept as placed;
  name tags (the game's own 4:3 sums, normal blending) are narrowed (user confirmed 2026-11-13). F9's frame trace
  also logs each 2D draw's placing for three frames ("2d trace:"). A new additive piece (no match last frame) is
  world from its first frame: placed by its third it flashed a second sun for a frame as the sun came into view or a
  fast turn moved a piece past kMoved (desert map; user confirmed gone 2026-11-13).
  Frame3D's hold (10 frames after the last 3D draw) also ends after 250 ms, and a frame with no 3D that draws a 2D
  layer over the whole screen is 2D at once: a checkpoint's first loading frame (trace h5) came within the hold and
  stayed up stretched 16:9 while the load ran (user confirmed 2026-11-13).
- **HUD on 16:9** (`runtime\gpu\screen2d.cpp`, GameInfo::screen_2d): the HUD is fixed function draws through an
  orthographic projection. Each frame's such draws are cut into runs in draw order (touching pieces: a 3x3 box, then
  its text); a run is narrowed to 4:3 about the side it sits on or the centre (applied to that draw's wvp the next
  frame, matched by position in the order). Draws 85%+ of the width still fill. Menus and movies (no 3D) are shown
  4:3. The subtitle box spans the full width in the 4:3 layout, behind the ammo panel and compass.
- **Hide HUD**: the same canvas draws are dropped in 3D frames (not full width ones).
- **Skip Intros** (`src\game\intro.cpp`): the four boot movies (table 0x5EDA70) are each a call of 0x300470; the
  per-frame 0x2F4540 returns 0 for them, only while one of those calls runs (user confirmed 2026-11-13): the Roles and
  Extras films step 0x2F4540 without a 0x300470 call, so with the boot count alone they were ended too.

- **SMAA** (`d3d12.cpp` Smaa, iryoku/smaa MIT in smaa_shader.h / smaa_textures.h): three full screen passes over
  the shown frame before the blit.
- **MSAA** (Settings::msaa 2/4/8): back buffer sized targets and their depth are multisampled (Target::samples).
  Every read goes through `Single(t, state)`: a one sample copy made again once drawn since (colour by
  ResolveSubresource, depth by drawing sample 0 with kDepthMsHlsl). Pipelines, blits and clears take the target's
  sample count; copies into a multisampled target are drawn. Applied at the first present (the device is made
  after LoadSettings). `run_debug.bat` (XBE_D3D12_DEBUG) copies the debug layer's errors into the log.
- **2D placement, both kinds**: screen space (XYZRHW, a briefing's text is a draw a letter) and canvas draws go
  through one `Decide` in draw order: a frame whose first 2D draw spans the width is a menu over the 3D view
  (pause, briefing, objectives) and stays 4:3 centred, not hidden; else runs of touching draws (a word space
  across) take one anchor, matched to last frame by box so typed out text does not flicker.
  End screens (user confirmed on two deaths, 2026-11-13; Mission Accomplished not yet seen since): the death screen's
  first draw was the HUD's subtitle (9-slice pieces each short of the width), so the screen was placed as HUD (header
  left, prompts right, typed lines split). A band (across, not full height: its thin divider line) anywhere in the
  frame now starts the menu; draws before it stay HUD; a full screen layer does not count. A menu is held into the
  next frame while a band shows. Held on any across draw instead, play after a cutscene (a full screen layer every
  frame) stayed a menu and bunched the HUD. Log: "2d: a band across ..." and "kept a menu"; F9 traces list across draws.
- **Ambient occlusion** (SSAO/GTAO, user confirmed): kAoHlsl in d3d12.cpp from the reference, depth reversed (z
  buffer: view z = -m32 / (depth - (1 - m22)) from the camera projection; w buffer from the draws' zparams). Armed
  in widescreen.cpp with the world camera's projection (d3d::ArmOcclusion); d3d8.cpp runs it, once 48+ draws are
  in, before the first of: a screen space draw (frames with no weapon have no clear after the world), the depth clear
  before the weapon; else at the second camera (cutscenes). A "second depth clear" rule was tried and ran it on the
  sky pass. F9 logs one frame's draw order ("frame trace:"), letters at BeforeDraw. AO Strength (default 50%: the
  user found 50 to 75% right, more too much).
  Particles held back (user confirmed the tunnel smoke, 2026-11-13): smoke and dust (DrawVerticesUP, blended, no
  depth write, not additive) drawn while the armed world draws are kept (HeldDraw: state and vertices) and drawn
  after the occlusion (ReleaseParticles), or before a depth clear, screen space draw, target change or Swap. Picking
  AO's moment from the order (before the first particle, then once 16 vegetation draws were in) failed per map: the
  tunnel drew smoke before its last depth writers, the desert a muzzle puff before its bushes. Additive draws
  (flashes) go at once. A replayed draw marks all simple states (gpu::MarkAllSimple): the render thread sends only
  marked ones, and without it the smoke took the world's blend and textures. Desert bush flicker not re-checked.
  Later the same day (user confirmed): a flat draw (no depth test, D3DRS_ZENABLE off or D3DCMP_ALWAYS, nor write;
  this game's HUD, trace letter f) also ends the world: frames with no weapon and no screen space draw (training
  map) had no AO at all once particles stopped setting it off. Once something is held, additive particles are held
  too, in order (the objective star: a dark star, then its additive glow, came out black). DrawUp uploads its
  vertices after BeforeDraw: releasing held particles rewrote the shared UP ring header, and the flying objective
  star took a smoke puff's vertices.
  Fog (user confirmed 2026-11-13, a snowy mission): AO showed through distance fog. The world draws' fog (start, end,
  density, mode, recorded in UploadConstants with the scene depth) goes to occlusion_apply, which fades the darkening
  by the same fog factor the surfaces get. This game: linear, 1000 to 10000. Vertex program fog (no distances) is
  logged.
- **Frame rate** (`src\game\recoil.cpp`, user confirmed 2026-11-12 at 200 FPS, Aim Assist on and off): the game
  steps several things once a call with no regard for frame time, tuned for 30 FPS. Each now runs on a fixed 30 Hz
  clock with a delta time of 1/30 (zero or more steps a frame), eased between steps. GameInfo::max_fps = 0; the
  Max Frame Rate default is the monitor's refresh rate (runtime Settings::fps_cap = kFpsCapRefresh).
  - Recoil: the pawn's 0x928F0 (from its tick 0x9DA10) springs rRecoil (pawn +0x8E4, ints) into vRecoil (+0x8F0,
    pitch yaw floats). The view gets none of it through the game (0x22390 is given zeros); AimAfterTick turns the
    view to sit kGain 1.2 x vRecoil (measured at 30 FPS) off the aim, past the Aim Assist off logic.
  - Sway: the pawn's 0x92850 sets vWaver (+0x908) from the weapon's sway, a spring too (6x as fast at 200 FPS). It
    still goes through the look axes, the way Marksmanship reaches it. The player input 0x33050 resets the axes
    (controller +0x4F8 pitch, +0x504 turn) each frame and takes back last frame's add (+0x9FC, +0xA00); 0x22390 adds
    this frame's, so the view turns by the change times delta time. We give it the change scaled by 1/30 over the
    input's delta time. Scaling the whole value put the scale's jitter into the change (shook); turning the view by
    the sway directly fed back into it (shook).
  - Aim lock (Aim Assist on): AACPlayerInput's 0x32DB0 picks a target (0x32CA0) and pulls the axes (0x22520, its
    only caller, writes only the axes). Stepped at 30 Hz, its pull given again each frame until the next step. It
    aims from the view plus the axes, so it is shown the axes with the sway as 30 FPS has it. Aim Assist off: 0x22520
    is skipped (aim_assist.cpp kSkipLockTurn), so mouse.cpp's undoing after the tick no longer runs (it ate the sway).

- **Stance on the keyboard** (`src\game\stance.cpp`, GameInfo::pad; user confirmed 2026-11-12; the controller is left as
  the Xbox had it). The game keeps the stance asked for (D-pad up/down: C, X), but a left stick past ~87% is running,
  which stands the pawn while it lasts (crouch walk 60, running 220); crouch asked for while running is given up. The
  keys pushed the stick all the way, so a crouch never held. With the keyboard in use: crouched (pawn bIsCrouched
  dword +0x310, bit 0x40; 0x20 asked, 0x04 slow or still, 0x200 prone) or a crouch just asked for, the stick goes to
  70%; down twice within 400 ms (or down while crouched) holds movement at nothing until prone (bIsProne dword
  +0x560 changes, 0x77 prone) or 700 ms, then a full stick crawls; up twice from prone sends the second up 1150 ms
  after the first (the flags say crouched at once and the animation plays after; the game cut straight to standing;
  1150 is the user's own timing). FindProperty / PropertyOffset (aim_assist.cpp) look up properties by name.

- **Free Look** (key action "Free Look", PadTarget::Game, Alt left or right; mouse.cpp FreeLookCamera, widescreen.cpp;
  user confirmed): held, the mouse turns an offset (99 deg across, 60 up/down at most) added to both camera nodes'
  rotation as UpdateMatrices runs, not the controller's Rotation; let go, it eases back (45 ms time constant). The
  weapon camera turns so the gun moves on screen as the world does (its lens is 47 deg against the world's 75+: the
  same angle overshot): atan(tan a x tan(weapon fov/2) / tan(world fov/2) x the world's FovScale and zoom hold), from
  the nodes' own FOV (+0x1EC) each frame (the last built projections held a menu camera's for a while). It is capped
  at 35 deg left, 60 right, 20 up/down of weapon turn, then the gun follows (further, the first person arms show
  their unmodelled backs). The game's own FreeLook button is Unreal's "W and S look up and down", not this (tried:
  xboxuser.ini's Ctrl is set back to unbound). Runtime: PadTarget::Game, input::ActionHeld(index).
  - Not done (user, for later): with the gun lowered near a friendly, small turns show the arms. bLowerWeapon,
    TempLowerWeapon and BusyTempLowering are not properties on the controller, pawn or weapon; lowering looks like a
    weapon state (BusyTempLowering, FS_BusyLoweringWeapon): read the weapon's state frame to turn the weapon camera
    off meanwhile. The game's lowering pose might also serve to lower the gun out of view at wide angles. The game also
    has a second lowered pose with the weapon empty (the user's screenshots): held low across the body, it showed no
    arms' backs; an idea: put the gun in that pose while Free Look turns.
    Parked 2026-11-13 after a hunt: the pawn's weapon readiness byte (+0x601; 1 empty and low, 2 up, 3 up aiming
    or firing, 4 a moment while loading, 6 lowered near a friendly; +0x602 the other end of a change) is a state
    machine's value (transitions in tables, e.g. 0x88410, reached through function pointers, no direct callers). It
    picks the third person movement sets (0x29BC50 and its neighbours: "LgFr" names at 2 and 3, "No" otherwise;
    name constants at 0x759000), but setting it to 1 changed nothing seen: the first person pose comes with the
    transition's animation. Next: a hardware watchpoint on +0x601 to catch what writes 6 near a friendly, and call
    that. Weapon and pawn states (StateFrame +0xC) did not differ between the poses; the animation natives are not
    in the function table (scripts compiled native). aim_assist.cpp's AllNames reads the whole name table at once.
- **Cutscenes** (`src\game\cutscene.cpp`; user confirmed): in a mission's scenes the controller has no pawn, or its
  ViewTarget is not the pawn; the mouse's counts are dropped and Free Look is off meanwhile (also on death and
  loading). bMatineeIsRunning, bInterpolating, bCinematicView, bHideHUD are not on this game's controller, pawn or
  level.

- **Movement modes** (overlay Mouse and Keyboard > Movement; Settings slow_walk_mode, sprint_mode, lean_mode,
  aim_mode, 0 Hold 1 Toggle; GameInfo::movement_modes; stance.cpp; user confirmed 2026-11-13; keyboard only, the
  controller as the Xbox had it). Slow Walk Toggle is the runtime's (keyboard.cpp). What the game does, measured:
  - Sprint: the left stick's click starts one; clicks while sprinting are ignored (ten in a row did nothing); a sprint
    ends as the stick slows or stops. Pawn bIsSprinting word +0x570: 0x08 walking, 0x0F sprinting (GroundSpeed stays
    220, speed ~330). Hold: a click when held and moving and not sprinting (retried each 400 ms); let go (or aiming),
    the stick eases to a slow walk for 250 ms, then nothing for a moment (600 ms at most). Sprinting or wanting it,
    the stick goes all the way (Slow Walk's half ended the sprint). Sprint from a lean: upright first, then the sprint.
    Crouch (or a double tap's prone) with Sprint held: Hold's sprint is blocked (g_sprint_blocked) until Sprint is
    pressed again or let go; it had clicked the sprint back on at once and stood the player up (user confirmed).
  - Lean (D-pad left/right, Q E): LeanDirection +0x578, 0 left, 1 upright, 2 right; the other way stands up; walking
    fast ends it. Toggle: the same key again sends the other way; Hold: leaning with the key up sends it. A lean asked
    for, and while leaning, the stick goes no further than a slow walk (as for a crouch). From a sprint the game spends
    the lean press ending the sprint: the lean is sent again once it has.
  - Aim (Tweak Lock, left trigger, right mouse; the game aims while held): Toggle holds the trigger for the game,
    off out of play. The game raises no sights while sprinting: aiming ends the sprint as letting go of Hold does.

- **Grass Distance** (`src\game\drawdistance.cpp`, Settings::grass_distance 100-400%, GameInfo::grass_distance; user
  confirmed live): the map's actors are XLevel +0x30 (data, count, max; the LevelInfo first); each TerrainInfo's
  DecoLayers (DecorationLayer, 0x64 bytes) fades over FadeoutRadius (+0x2C Min, +0x30 Max; 0 to 5000 on the
  training map), scaled from the kept originals once a map and on a change (twice a second cost frames). Named for
  grass alone so other distances can sit beside it. IsA / StructField (aim_assist.cpp) for classes and structs.
  - Not done (parked 2026-11-13): distant soldiers lose part of the torso (the Xbox too: their low detail mesh). A
    soldier (AACNPCPawn) is one SkeletalMesh shared by all, plus two CharAttachment static meshes (helmet, weapon?),
    none culled. No LODBias, CullDistance (actors) or LODStrength, LODLevels, MinLOD, LODDistance (the mesh) exist as
    properties in this build: the detail level is picked in compiled code. To do: find it (what mesh data a distant
    soldier's draw reads) and keep level 0 further. aim_assist.cpp has NameText / ClassName (index to text) for probes.
  - The rainy obstacle course is CPU bound (resolution, AA, AO change nothing): ~150-180 uncapped, 300+ in places,
    without this code too (the user compared). The game's grass and particles: for a later performance pass.

- **Menu prompts as buttons** (menus.cpp; user confirmed 2026-11-13): d3d::ScreenDraw now tells each screen draw's
  stage 0 texture (size from its Format), its FVF texture coordinates, and the first two vertices' raw floats. The
  menus draw through a vertex shader, 48 byte vertices, texture coordinates at +32: the button sheet's draws (128x128,
  quarter cells) are the prompts, its cell the button (A X B / Start Y / left trigger Black / White right trigger);
  ones drawn off the page (hidden tabs) left out. A click on a prompt or its label presses it (input::PressMenuAnalog,
  new, for the face buttons and triggers); a tabbed page (Q and E shown) takes no click elsewhere as A (any page
  with prompts did, and the main menu, its selection not yet told, took no first click). On a tabbed page with
  an X (the Personnel Jacket) a click on the selected row presses X, Edit. A click used so is not A again until the
  button is up (the opened page took it as A and closed). Editing there (X, then the tabs' Q and E gone) the cursor is
  not followed: the field's arrows looked like a selector row and stepped through the values.

- **Look Spring** (mouse.cpp; user confirmed 2026-11-13): the game's Controller option eases the view level while
  moving with the look axes idle: 0x1C26B, inside the controller's rotation update 0x1C240, tests the player's options
  object (0x3DB80) +0x78 bit 0x02. The mouse never feeds the axes, so it pulled against the mouse: with the keyboard
  and mouse in use the bit is cleared around that code and put back at once (the shown and saved option stays the
  player's); the controller has it as set. Off by default: the first time play runs with this build it is cleared
  once in the profile (reached from the controller: 0xEF670, 0x1AF00, 0x3DB80) and look_spring_default_applied is
  saved, so turning it on sticks (once per install, not per profile; xboxuser.ini's bSnapToLevel is not it).

- **Persistent Bodies** (`src\game\bodies.cpp`, Settings::persistent_bodies, overlay Recomp Settings > General: Off /
  32 / 64 / 128 / All, default Off; user confirmed 2026-11-13): a dying soldier's AGP_Pawn.DeadCleanup (0xAB480) calls
  AGP_GameSinglePlayer.AddPendingDestroyPawn (0x2B7410, Level.Game vtable +0x13C; ecx a frame, +4 the game), which
  trims PendingDestroyAGPPawnsList (+0x60C data, +0x610 count) while the count is over MaxPendingDestroyAGPPawns
  (+0x618, 0 in play: one body), destroying the oldest (0x169570 at 0x2B74C5), then appends the new one. The hook
  raises +0x618 to keep - 1 for that call only and puts the game's value back. Found with a probe of the actor list and
  a ULevel::DestroyActor (0x1AE490) hook logging callers. Idea from the 007FRWL recomp (keeps 32 bodies and items).
  Not checked: dropped weapons (DeadCleanup also destroys the pawn's +0x4D8 and +0x4D4 actors), many bodies' cost.

- **Profile name keyboard** (menus.cpp NameEntry/KeyFor, core/keyboard.cpp SetTextEntry/TakeTyped; user confirmed
  2026-11-13): told by its prompts L R X Y Start all shown. Its shoulders are Back and Space, not tabs (a click had
  pressed X, Shift, as on the Personnel Jacket). Letter, digit, '.' and '-' keys are queued and typed by stepping the
  selection to the key (the list's 38 same-size items in reading order) and pressing A; Backspace, Space, Shift,
  Delete and Enter press Back, Space, Shift, Clear and Done. Its icons are @typing's sheet (make_key_icons.ps1),
  switched on in @keyboard's place by mouse.cpp. Later (user tabled): instant typing by sending the page's own
  character message (F9 probe on the keyboard to find it) instead of stepping.
- **Click skips movies** (intro.cpp ClickSkips, user confirmed): a left click over the window with no menu drawn
  ends a movie in 0x2F4540 as the boot skip does.
- **Release icons**: release.bat runs make_key_icons.ps1 into the zip's textures\replacement\@keyboard (the menus'
  sheet and @typing); a fresh install had shown the Xbox buttons in every menu.

- **Pause On Focus Loss** (`runtime\app\focus_pause.cpp`, Settings::pause_on_focus_loss, default on, overlay
  Recomp Settings > General; user confirmed in a cutscene, picture and voice in step, 2026-11-13): at D3DDevice_Swap
  the game thread waits while the window is in the background or minimized (a hidden window never pauses).
  audio::SetPaused stops the XAudio2 voice in place; kernel::HoldGuestTime stops the guest's clocks: GuestNow is
  steady time less the holds (a seqlock), read by the tick count thread, interrupt time, KeQueryPerformanceCounter,
  x86_rdtsc and DirectSound's sample time. From the SCDA recomp (which left the sample time out). Kernel wait
  timeouts stay on real time. Not tried: play, menus, minimizing (same code path).

- **HDR Output** (`runtime\gpu\d3d12.cpp`, Settings::hdr_output default on, overlay Display; user confirmed bright,
  2026-11-13, display 440 nits peak, SDR white 240): from the BLACK recomp. UpdateHdrDisplay each present finds the
  window's monitor (FindOutput, DXGI_OUTPUT_DESC1: G2084/P2020 = Windows HDR on; MaxLuminance, capped 4000;
  SdrWhiteNits from DisplayConfig), a stale factory remade. ResizeSwap switches the swap chain to R10G10B10A2 with
  colour space G2084_P2020 (falls back to BGRA/sRGB). DrawFrame then draws the frame (BlitFrame into an R16G16B16A16
  target) and ImGui (into a BGRA panel target), and kHdrHlsl's hdr10 pass puts both into the back buffer: gamma 2.2
  to linear, inverse Reinhard on luminance up to the peak, SDR white at Windows' level, panel at SDR white, BT.2020,
  PQ. The overlay hint says when the display has no Windows HDR (app::HdrDisplay).

- **Recoil System** (`src\game\tuning.cpp`, Settings::weapon_tuning 0 Original / 1 Revised, overlay General; user
  confirmed 2026-11-13): System\AACTuning.ini holds each weapon's RecoilPitch / RecoilYaw (Min, Max), fRecoilFalloff,
  fWaverLimit / fWaverStrength, fJitter, fWeaponAcc, fire rates. The game copies it to Z:\System\ and reads that
  copy; text.cpp's patch hook hands it to PatchWeaponTuning, which on Revised puts weapon_tuning.ini's lines (beside
  the exe: only the changed keys, same section names; a key given twice replaces all its lines) over the game's.
  files.cpp now patches a Z: copy from the disc's file of the same path (not the copy), so Original truly restores and
  Revised is made from clean each start. Read once at start. The test file sets zero recoil on all 11 weapons; the
  user's own values go there. For a public release weapon_tuning.ini can ship (only the user's numbers).
- **Mouse trigger squeeze** (`src\game\trigger.cpp`, stance.cpp StancePad; user confirmed 2026-11-13, "as smooth as
  the controller, with the precision of a mouse"): Fire is the right trigger's pressure, a shot at 80 of 255
  (TriggerSensitivity=80), and a fast rise jerks the muzzle down and sideways (HumanController.UpdateTriggerVelocity;
  probes found the kick, view recoil, vWaver and TASOffset (pawn +0x250) unchanged between a slow and a hard pull: the
  jerk is the rise, seen only with Revised's zero recoil). On the keyboard and mouse in play Fire now goes at once to
  76, then rises 200 a second (through 80 ~20 ms after the click) to full; let go, 0. The pad's trigger is untouched.

## Future: PC features the user wants (paused 2026-11-10)
Still to do (2026-11-12): the lowered gun under Free Look (see Free Look), a controller combo to open the overlay, overlay navigation for non-Xbox pads (ImGui's backend reads XInput pad 0 only).

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

## 2026-11-15 fixes (user confirmed)
- **Night vision, scopes, binoculars and 2D placement** (screen2d Decide): a first draw across the screen starts a menu
  only as a band; a full screen first layer (NV, scope) keeps play's HUD 16:9 and Hide HUD working. A full screen first
  layer continues last frame's menu, and a frame begun by one with no piece in the corners (g_corner: the HUD's ammo,
  compass) holds the menu into the next: the briefing map over its backdrop stays 4:3.
- **Held particles** (d3d8.cpp): each keeps its stage texture headers and is drawn with them put back for the replay
  (the game clears an internal reference bit, 0x00080000 of Common, between; NV beams were flat green blocks). Never
  held: draws without a depth test, and fixed function draws under an orthographic projection (screen2d's 2D; the
  briefing map's tunnel picture). Held from kMinWorld (8) draws after arming.
- **AO timing**: a flat or screen draw sets AO off at kMinWorld draws once a blended no-zwrite draw was seen (small
  worlds: briefing room, tunnels), and a blended depth-writing draw with particles held sets it off too (the tunnel's
  haze flattened the floor's AO).
- **Scroll wheel scope** (stance.cpp): the game zooms only at a slow walk or standing, so a zoom while moving is kept
  back, the sprint eased out as for aiming, the stick at a slow walk, and sent once slowed; a held sprint waits for its
  key again after.
- **Night vision overlay without its HUD shapes**: textures\replacement\@hidehud\256x256_12c86152af72d766_nv_clean.png
  (made from the dump: the two shapes' alpha filled by neighbour averaging, 4x), group on with Hide HUD or 16:9
  (mouse.cpp). It is game data: only in the build folder, not the repo or zip; a release needs it made on the player's
  PC. The binoculars' and a scope's dumps (256x256_1e0001d4be4e9b83, 256x256_3b9bd624ac58b47d) are for 16:9 scopes next.
- **Overlays made on the player's PC** (texturedumper AddMaker/Make, called from d3d12 LatchPackKey; recipes in
  src\game\overlays.cpp, registered from mouse.cpp): the first time a listed texture is seen with replacement on, its
  level 0 is decoded to BGRA, remade and saved under textures\replacement\ (@hidehud night vision, @wide M24, ACOG, M82
  scopes and binoculars), then the pack watcher loads it; nothing from the game ships. The earlier hand-made images are
  in _to_delete\overlays_handmade. User confirmed the binoculars, M24 and M82 made in play.
- **Developer section** (settings.toml `developer_mode = true`, written back only while on; never in the zip): God Mode
  (session only) holds Pawn.Health at 250 and BleedHealth at 0 (src\game\devmode.cpp, whose header lists what failed:
  top-up to 100, 10000 flips the view, ReducedDamageType not honoured). At 250 the view narrows; grenades can kill.
  Next step for real invulnerability: catch TakeDamage in the script call path. Probes kept in aim_assist.cpp:
  LogProperties, FindClass.
- **First use stutter** (d3d12.cpp, 2026-11-18): every frame more than 30 ms over the usual logs a `hitch:` line (shader
  compiles, pipeline builds, new textures, disc reads, session totals; LogHitch, kernel::TakeDiscReads). The cause was
  shader compiles (~20 ms) and pipeline builds (~15 ms) mid frame. Now: cache\shaders.bin (every compiled shader, all
  read at start) and cache\pipelines.bin (the driver's ID3D12PipelineLibrary, saved every 10 s while new ones go in),
  and new draw shaders and pipelines compiled and built on 1-3 worker threads (CompileAsync, TakeShader, EnsurePipeline),
  the draw left out until they are back. After it a playtest logged compile 0 ms and pipelines 1 ms; the slow frames
  left (~40 in a level, nothing of ours in them) are the game's own work. User: much smoother. The filled cache from
  before is in _to_delete\cache_2026-11-18.
- **Radar shifting with binoculars and night vision** (screen2d GroupDrawn): a group of pieces in a bottom corner is
  never world following (the radar moved as the view changed and was centred, kept so). By group, as the subtitle
  box's pieces are as low and as wide but join across the screen. User confirmed.
- **Open, logged for later:** on one mission's start (a forest hill, "Move up with Alpha"), the objective star shows as
  a small dot high in the sky, only at the start; later stars are right. The _test build (older) does it too.
- **AO in thick fog** (kAoHlsl occlusion_apply): the fade is the fog's visibility cubed, as the darkening belongs to the
  surface under the fog, not the haze over it (a sandstorm map, linear fog -700 to 4000: soldiers half in it showed dark
  outlines). User: better.
