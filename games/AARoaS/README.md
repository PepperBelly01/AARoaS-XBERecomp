# America's Army: Rise of a Soldier - XBE Recomp

An unofficial native PC port of the original Xbox version of America's Army: Rise of a Soldier (USA), with extended graphics options and other enhancements.

This port was created using the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

> [!NOTE]
> This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build.

## Installation

- Put your game files in the `assets` folder. This can be an ISO, or the extracted data files.
- Run `AARoaS.exe`. The first start compiles the game's code for your PC, this only happens once.
- Recomp settings can be adjusted in the Settings overlay while the game is running, opened with <kbd>F10</kbd>. They are saved in `settings.toml` beside the exe.

## Features

### Display
- **Display Mode** - Switch between Fullscreen, Borderless and Windowed.
- **Resolution** - Lists every mode your monitor reports, replacing the original 640x480.
- **Render Resolution** - Renders the game at a percentage of the output resolution, for supersampling or downscaling.
- **Aspect Ratio** - 4:3 as on the Xbox, or 16:9, which shows more to the sides and spreads the HUD out to the screen's corners, while menus, briefings and the map stay 4:3 in the middle.
- **16:9 Scopes and Binoculars** - In 16:9 the M24, M82 and M4 ACOG scopes and the binoculars are remade to be round instead of stretched, and night vision drops the HUD shapes painted into its overlay. They are made from your own game's textures the first time each is seen; 4:3 keeps the originals.
- **Field of View** - Sets the horizontal field of view in play. Aiming zooms in from it as usual.
- **Max Frame Rate** - Your monitor's refresh rate, 30 to 240 FPS, or unlimited, replacing the Xbox's 30 FPS limit. Recoil, weapon sway and the aim lock keep their original 30 FPS timing at any frame rate.
- **V-Sync** - Reduces screen tearing.
- **FPS Counter** - Displays the frame rate on screen.
- **Hide HUD** - Hides the HUD in play, for screenshots, through night vision, scopes and binoculars too. <kbd>H</kbd> (Toggle HUD, rebindable) switches it in play.
- **HDR Output** - HDR10 on displays with Windows HDR on: highlights such as the sun, fire and flashes reach your display's peak brightness.
- **Pause On Focus Loss** - The game and its sound pause while the window is in the background.

### Graphics
- **Multisample Antialiasing (MSAA)** - 2x, 4x or 8x MSAA smooths the edges of 3D shapes while keeping the image sharp.
- **Subpixel Morphological Antialiasing (SMAA)** - Smooths jagged edges across the whole frame, fences and foliage included. Can be combined with MSAA.
- **Anisotropic Filtering** - Forces the selected anisotropic filtering level on all textures.
- **Ambient Occlusion** - Adds soft contact shadows where surfaces meet, SSAO or GTAO, with adjustable strength. It fades with the fog and stays under smoke, dust and night vision's light.
- **Grass Distance** - Grass reaches 1x (the game's own) up to 4x further before it fades out.

### Audio
- **Output** - Automatic, Stereo, 5.1 Surround or Mono.

### General
- **Skip Intros** - Skips the logo and rating movies at start up. The Roles films and the Extras screen's films still play.
- **Persistent Bodies** - Keeps the bodies of fallen soldiers on the map: 32, 64, 128 or all of them, instead of only the last one.

### Mouse and Keyboard
- **Mouse Look** - Raw mouse input that turns the view directly, with separate horizontal and vertical sensitivity and inverted axes.
- **Rebindable Keys** - Two keys or mouse buttons per action. In-game prompts show the keys you have bound.
- **Free Look** - Hold <kbd>Alt</kbd> to look around while the weapon stays pointed ahead.
- **Trigger Squeeze** - The fire button squeezes the trigger like the controller's, rather than pulling it in one go, so a shot does not jerk the weapon's sway.
- **Movement Modes** - Slow Walk, Sprint, Lean and Aim each work as Hold or Toggle, and combine smoothly while moving: aiming, or scoping with the scroll wheel, eases you out of a sprint and brings the sights up as you slow, and crouching ends a held sprint. Double tap crouch to go prone.
- **Clickable Menus** - The menus' button prompts can be clicked with the mouse.
- **Auto-Sighting** and **Acquisition Icon** - The game's own options, set from the overlay and kept in step with its menu.
- **Target Zoom** - Turns off the game's zoom on a sighted enemy, and **Zoom Sensitivity** sets how much your aim slows down while it zooms.

### Controller
- Supports Xbox controller with hot plugging and rumble. The controller plays as on the original Xbox.
- **Vibration** and **Inverted Axes** can be set separately from the mouse. Button prompts switch between keys and controller buttons with whichever you last used.

## Known Issues

- With Target Zoom off, the M249's zoom level can be inconsistent when sighting a target.
- On the some maps, ambient occlusion can show faint seams on the terrain.
- A scope, the binoculars or night vision may show its original 4:3 look for a moment the first time it is used, while its 16:9 version is made.

### Textures
Set in `settings.toml` (`custom_textures`, `dump_textures`, `texture_dump_format`).
- **Texture Replacement** - Replaces the game's textures with the `.dds` or `.png` images in the `textures\replacement` folder beside the exe.
- **Texture Dumping** - Saves every texture the game uses into the `textures\dumps` folder beside the exe, as PNG or DDS.

### Miscellaneous
- **Boot Splash** - Place a `splash.png` (or `.bmp`) beside the exe to display a splash screen at launch.

## System Requirements

### Minimum
- **OS**
  - Windows 10 64-bit
- **GPU** with support for Direct3D 12

## Building

### 1. Clone the Repository
Clone this repository using [Git](https://git-scm.com/).

### 2. Add the Required Game Files
This project lives in the SDK's `games` folder. `build.bat` links `..\..\Game` (the extracted game) as `assets` on the first run, so nothing needs copying. To use another copy, delete the `assets` link and put the files there instead.

### 3. Install Dependencies
Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two).

### 4. Build the Project
Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the settings overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.
