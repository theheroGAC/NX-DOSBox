# DOSBox SVN standalone Vita/Switch port analysis

**Inspected checkout:** `joncampbell123/dosbox-svn`, shallow clone at `f948a1b` (`2026-01-01`). This report began as an upstream architecture analysis. Clock/audio service seams, an SDL-backed input/event-pump seam, and an initial backend-neutral renderer-to-video-service contract have since been added. There are still no Vita/Switch-specific backends or packaged builds.

## Summary

This is actual DOSBox SVN: its CPU, DOS, BIOS, VGA/SVGA, device, shell and mixer subsystems are present. The upstream Unix application requires SDL 1.2 and its graphics, event, mapper and startup code are intertwined with that frontend. The repository did not contain standalone Vita or Switch targets when inspected. A port needs bounded host-service interfaces and native application entry points while retaining the current PC frontend and emulator core; it is not a fresh emulation core or a Libretro port.

Keep Vita interpreter-only. Build Switch with the interpreter first; consider dynrec only after target interpreter operation has been verified. None of those console milestones has been reached yet.

## Relevant upstream components

| Responsibility | Upstream source |
|---|---|
| Startup/configuration and event loop | `src/gui/sdlmain.cpp`, `src/dosbox.cpp` |
| CPU interpreter and optional dynamic cores | `src/cpu/core_normal.cpp`, `core_full.cpp`, `core_simple.cpp`, `core_prefetch.cpp`, `core_dynrec.cpp` |
| DOS, filesystem, BIOS and shell | `src/dos/`, `src/ints/`, `src/shell/` |
| VGA/SVGA and scaling | `src/hardware/vga*.cpp`, `src/gui/render.cpp`, `render_scalers.cpp` |
| Mixer and sound devices | `src/hardware/mixer.cpp` plus Sound Blaster, AdLib/OPL, speaker, Tandy, GUS and other devices |
| SDL window/events/input mapper | `src/gui/sdlmain.cpp`, `src/gui/sdl_mapper.cpp` |
| Host paths and local mounts | `src/misc/cross.cpp`, `src/dos/drive_local.cpp`, DOS mount/filesystem sources |

## Work completed so far

### Timing and mixer boundary (partial)

`include/platform.h` defines common clock, delay, audio-device and normalized input entry points. `src/platform/sdl_platform.cpp` supplies the existing PC SDL 1.2 implementations. `include/timer.h`, `src/dosbox.cpp`, MIDI SysEx pacing, serial debug timing and the DOS mixer route through those services. Core CPU auto-cycle/status updates and DOS program-name updates also route through `Platform_UpdateStatus()`. Mixer shutdown closes its backend. Automake and the upstream explicit-source Visual C++ project list the PC adapter.

### Input and event-pump boundary (partial)

The DOS emulation loop now requests event pumping via `Platform_PumpEvents()`. The SDL adapter forwards that request to the SDL frontend's `GFX_Events()`. SDL frontend mouse events and focus-release handling call the normalized platform input methods, which forward to DOS mouse, keyboard and joystick APIs. The SDL mapper also routes its keyboard and joystick outputs through the same interface. The existing mappings and DOS-device APIs are retained; this is an interface seam only. SDL event polling, controller discovery, touch, mapper UI and presentation are still SDL-specific, and there is no target input implementation. The existing Visual C++ project already lists the SDL frontend sources; this work adds the platform service adapter and its shared headers to its explicit file list.

### Renderer/video-service boundary (initial)

`include/platform_video.h` now owns the video callback, output-format flags and palette-entry type independently of SDL declarations. The scaler tables and `src/gui/render.cpp` use these backend-neutral definitions; the renderer obtains output mode, RGB/palette conversion, output dimensions, shader setup and writable frame buffer via `PlatformVideo_*` calls. These describe the scaled output surface and its lifetime, leaving DOS/VGA mode generation and the existing scaler intact. Core CPU, renderer and DOS shell status updates use `Platform_UpdateStatus()` instead of `GFX_SetTitle`; the SDL frontend implements that hook under its SDL-specific `SDL_SetStatusTitle()` name. SDL-only GFX declarations and compatibility aliases live in `include/sdl_video.h`, while `include/video.h` is reduced to a shared-contract wrapper. The PC SDL adapter forwards the service calls to the existing SDL frontend, so this remains a boundary extraction rather than native display support. SDL startup/window/surface code, presentation, reset/fullscreen/focus handling, and SDL GUI/mapper paths remain SDL-specific. There is no Vita/Switch implementation of the video service yet.

### Host mutex and CD-image boundary (initial)

`include/platform.h` now also exposes an opaque mutex handle with `Platform_MutexCreate/Destroy/Lock/Unlock`, implemented by the SDL adapter on `SDL_mutex`. The CD-image player in `src/dos/cdrom_image.cpp` and the Windows direct-sector CD player use those services instead of SDL mutexes, so `src/dos/cdrom.h` and the image/cue path no longer include SDL headers. The shared CD header keeps the physical-drive handle opaque, and only the SDL CD backend in `src/dos/cdrom.cpp` interprets it. Physical drive enumeration is exposed as `CDROM_GetDriveCount()`/`CDROM_GetDriveName()` implemented by that backend, so `MOUNT -cd` in `src/dos/dos_programs.cpp` no longer needs SDL and a target without an optical drive can report zero drives. This is required groundwork because CD-image audio is the CD path a console target actually uses; physical CD-ROM remains an SDL-only backend that a target build would exclude.

### Current build/validation status

The legacy bootstrap `autoreconf -fi` could not start because the available Perl environment cannot load `Autom4te/ChannelDefs.pm`. There is no generated `configure` or Makefile in this checkout. No C++ compiler is available in PATH; Vita/Switch cross-compilers and packaging tools were also not found, and `VITASDK` is unset. Source audits found SDL references retained in the frontend/mapper and the physical CD-ROM backend; the CD-image/cue path, the DOS `MOUNT -cd` listing and the common renderer contract no longer need SDL headers, GFX frame-buffer entry points are isolated in the SDL adapter/frontend, and the `SDL_net` paths are already compile-time optional. `git diff --check` passes; the Visual C++ project XML and required platform adapter/header entries validate, and PowerShell source audits confirm the shared headers contain no SDL/GFX frontend references and that core CPU/DOS sources no longer call those graphics entry points directly. The legacy `visualc_net` project is updated, but the DOSBox-X derived `vs2015` solution was deliberately left untouched and still lacks `src/platform/sdl_platform.cpp`, so building it against this tree would fail to link the platform services. There has been no full PC compile or runtime test; none of the input, audio, video, mutex or CD-image changes have been compiled or exercised. Do not claim the refactored PC application works until a real build/runtime validation succeeds.

## Phase ledger

| Phase | Status | Outstanding exit criteria |
|---|---|---|
| 1 — Repository analysis | COMPLETE | Initial architecture/dependency inventory documented |
| 2 — PC baseline and host seams | PARTIAL | Fix bootstrap environment; compile and run the SDL PC app; test timing, mixer, event pump, input and video service paths |
| 3 — Vita bootstrap | NOT STARTED | Vita entry/filesystem/display/input/audio startup, interpreter DOS prompt, VPK and hardware test |
| 4 — Switch bootstrap | BUILT, NOT DEVICE-VERIFIED | libnx entry/filesystem/display/input/audio startup, interpreter DOS prompt, NRO and hardware test |
| 5 — Native video/audio/input | NOT STARTED | Native Vita and libnx backends, dynamic VGA-mode presentation, buffering and controls; initial renderer boundary extracted but no target backend exists |
| 6 — Game launch and media | NOT STARTED | Frontend/session handoff and target-tested DOS executable/image mounts |
| 7 — Archives | NOT STARTED | Select a supported licensed ZIP path and validate mounts; archive launch was not found upstream |
| 8 — Game library/frontend | NOT STARTED | Native library UI, metadata, settings and return flow |
| 9 — Save states | NOT STARTED | Complete tested serialization for all active core/device state, or intentionally omit |
| 10 — Performance | NOT STARTED | Target profiling after stable interpreter operation |
| 11 — Switch dynrec | NOT STARTED | Evaluate after interpreter is known to work; maintain interpreter fallback |
| 12 — Packaging | NOT STARTED | Inspect installable VPK/NRO and release artifacts |

## SDL/host dependencies still blocking a native target

- `configure.ac` mandates SDL 1.2. `sdlmain.cpp` owns SDL init, window/surface management, presentation, input polling, mouse capture, pacing, startup/configuration and shutdown.
- The emulator-loop event-pump seam still ends at `GFX_Events()` in the SDL application; a native target must supply its own pump rather than link that frontend unchanged.
- `sdl_mapper.cpp` still discovers/polls SDL joysticks and has SDL event/UI dependencies. Input API routing does not make those sources SDK independent.
- Only the renderer's mode negotiation and frame-buffer subset now crosses `PlatformVideo_*`; the SDL adapter forwards it to `GFX_*`. SDL presentation, frontend lifecycle, reset/fullscreen/focus and GUI/mapper screen paths remain SDL-specific. No console implementation of the video service exists yet, so target presentation/mode testing remains outstanding.
- SDL uses also remain in the physical CD-ROM backend (`src/dos/cdrom.cpp` and `cdrom_ioctl_linux.cpp`) and the MSCDEX interface selection, plus the optional `SDL_sound` cue audio behind `C_SDL_SOUND`. The CD-image/cue path used by console targets is now SDL-free, and `MOUNT -cd` only calls the `CDROM_GetDrive*` backend. `SDL_net` needs no refactor: `src/hardware/ipx.cpp`, `ipxserver.cpp` and the `C_MODEM` serial network code are already fully wrapped in `#if C_IPX` / `#if C_MODEM`, which `configure.ac` only defines when SDL_net is found, so a target build simply compiles them out. Physical CD-ROM and host serial/process facilities remain candidates to disable or separately adapt for initial console builds.
- POSIX/Win32 filesystem, directory, time, process, ioctl and memory-protection assumptions remain scattered across the source tree and require target-specific audit.

## Functional scope and limitations

The upstream DOS shell, disk-image/local-drive code, CPU interpreter, BIOS, VGA/SVGA and sound emulation form a useful foundation, but code presence is not target-tested feature support. The existing SDL mapper is not a native touchscreen overlay. No ZIP/7z/DOSZ game archive workflow, native library browser, complete emulator-wide save-state serializer, VPK/NRO, or Vita/Switch SDK integration was implemented in this work. No Libretro integration was identified in the initial repository search.

A safe continuation order is: repair/validate the legacy PC build, finish extracting frontend lifecycle/title and native mode-presentation design, establish one interpreter-first console bootstrap, then test video mode changes, audio, key-down/up, controller and storage before adding library/archive/packaging features. Target SDK availability and console hardware behavior have not been established.

## Console ports: actual build state (2026-09-30)

The sections above describe the situation before the console ports existed. This is
the state after `ports/switch` and `ports/common` were written.

### Toolchains on the build machine

| Target | Location | Compiler |
|---|---|---|
| Switch | `C:\devkitPro` (`DEVKITPRO`, `DEVKITPRO/devkitA64`) | `aarch64-none-elf-g++` 16.1.0, libnx |
| PS Vita | `C:\msys64\usr\local\vitasdk` (`VITASDK`) | `arm-vita-eabi-g++` 15.2.0 |
| Build driver | `C:\msys64\mingw64\bin\ninja.exe` | MSYS2 sh + native ninja |

`ports/switch/out/dosbox.nro` is the Switch artifact (about 2.96 MB, `PIE`,
static libnx, no undefined symbols). `ports/vita/` is still empty: the Vita
backend has not been written, and its SDK in this tree no longer ships the
classic control APIs (`sceCtrlPadOpen` and friends), so it has to be written
against `sceCtrlPeekBufferPositive`/`sceCtrlReadBufferPositive`,
`sceDisplaySetFrameBuf` and `sceAudioOut*`.

### Building

```
cd ports/switch && ./ninja.sh          # incremental: only changed objects
cd ports/switch && make                # same source list, plain make
```

Two build traps cost time and are now handled in the scripts:

- `TMP`/`TEMP`/`TMPDIR` must point inside the project, otherwise the cross GCC
  writes temporary files to `C:\WINDOWS\` and fails.
- An inherited `DEVKITPRO` pointing at a path that does not exist (an MSYS
  `/opt/devkitpro`, for example) makes `switch.specs` expand to a mangled linker
  script path. `ninja.sh` now validates the location and falls back to the real
  one.
- The generated `build.ninja` must depend on the `.nacp` **order-only**
  (`build out/dosbox.nro: nro out/dosbox.elf | out/dosbox.nacp`). `elf2nro`
  takes exactly two positional arguments, so a normal dependency is passed as a
  third one and is taken as the output file, which silently writes the NRO over
  the `.nacp` and leaves `out/dosbox.nro` stale.

### Platform seams already implemented

`include/platform.h`, `include/platform_video.h`, `include/sdl_video.h`,
`src/platform/sdl_platform.cpp`, `src/gui/sdlmain.cpp`, `src/gui/midi.cpp`,
`src/ints/bios_keyboard.cpp`, `src/dos/cdrom*.cpp`, `src/dos/dos_mscdex.cpp`,
`src/misc/setup.cpp`, `include/cross.h`, `configure.ac` and
`src/platform/visualc/config.h` all gained console-conditional paths;
`ports/common/` holds the port-independent parts (console mapper, entry-point
glue, OSD canvas/launcher/virtual keyboard/overlay) and `ports/switch/` holds the
libnx backend.

### What is verified and what is not

Verified by building: the NRO compiles and links, has no undefined symbols and
embeds no SDL entry point. Fixed along the way, all found from the device
reporting a crash: `Config_Add_Target()` registered a section with a NULL init
function that `Section::ExecuteInit()` then called; `PlatformVideo_SetSize()`
returned 0, which the core reports as "Failed to create a rendering output"; the
audren memory pool count ignored the four wavebufs a voice owns (now pinned with
`static_assert`).

Not verified: nothing has been executed on real hardware from this machine's
side. The report that the launcher appears but no button responds was traced to
a real bug and fixed: this libnx only opens the HID service from
`padConfigureInput()`, while `padInitializeDefault()` merely fills in the id
mask, so every button stayed at zero. `SwitchPlatform_InputInit()` now calls
`hidInitialize()` and `padConfigureInput()` first, and there is no
`hidScanInput` in this libnx at all - `padUpdate()` is the only HID query, so it
is called from a single `refresh_pad()` helper. The port traces the first button
transitions to `sdmc:/dosbox/startup.log` so controller state can be read from
the device itself.

Two more device-reported problems were fixed the same way. The overlays were
only ever drawn by the launcher, so the virtual keyboard opened its state machine
but painted nothing and the machine could not be typed at; they are now composited
in `PlatformVideo_EndUpdate()` onto the mirrored frame. And the core only pumps
events when the interrupt queue has nothing due, which is not a rate a controller
can be sampled at, so the input is now read once per presented frame as well.

Playback was silent for a different reason: every wavebuf was queued with
`start_sample_offset` and `end_sample_offset` left at zero, and the renderer
plays the `[start, end)` sample range literally, so each buffer was consumed
without a single sample being played. The `audren-simple` and `hwopus-decoder`
examples in switch-examples set `end_sample_offset` for exactly this reason. The
offsets now cover the whole buffer, a `static_assert` pins the four wavebufs one
voice owns, the audio thread falls back to the default priority instead of
disabling the backend, and the audren results (init, update, renderer start, and
the format that was opened) go to `sdmc:/dosbox/startup.log`, because a dead
backend is otherwise invisible from the device.

The pointer was unusable too, and that was a rate problem rather than a speed
constant: `Platform_PumpEvents()` runs from the emulator main loop every time the
interrupt queue is empty, thousands of times per second, and the right stick
moved the emulated mouse a fixed amount per call, so the cursor crossed the
screen in a blink and its speed followed the emulated CPU load. The movement is
now sampled every 8 ms and scaled by the time that actually passed, which makes
the speed independent of the call rate; `[sdl] sensitivity` (100 by default) is
the knob in `dosbox.conf`.

The video path assembles the picture in the linear mirror and presents it once:
scaling into the framebuffer, reading it back for the mirror and then blitting an
overlay over it cost three buffer swaps per emulator frame, and the middle one
showed a frame without the overlay, which is what made the on screen keyboard
flicker. `OSD_DrawOverlaysInto()` therefore draws into a frame the caller has
already started, and the caller presents the composed result.

The launcher also understands disk images now: a folder containing `.img`,
`.ima`, `.bin`, `.iso`, `.cue`, `.vfd` or `.dsk` files gets an extra
`>>> MOUNT n DISK IMAGES` row, and picking it (or a single image) mounts the
folder as C: and hands the images to `IMGMOUNT` as D:, which is what a floppy
set or a CD needs to keep a writable drive for saves. The images are taken in
sorted file name order, which is what makes "Disk 1" the first floppy. Paths with
spaces survive because each `-c` argument is a single quoted token in the
`CommandLine` parser.

Picking a folder is also the last step: the launcher works out what to start and
passes it to the port as extra `-c` arguments. The order is `autoexec.bat` (which
`boot` already runs), then `start.txt`/`dosbox-start.txt` and `start.bat` as the
user's override, then a classic installer name, then a single executable in the
folder, and for a floppy set it reads the FAT12 root directory of the first image
straight off the file to find `INSTALL`/`SETUP` on the disk. What it settled on
is shown next to the path, so a wrong guess is visible before pressing A. A
`start.txt` with one command per line overrides everything, which is the escape
hatch for the games that need something nobody can guess.

The virtual keyboard had a real bug worth recording: it derived the DOS key from
an ASCII value by arithmetic on `KBD_KEYS`, but that enum is laid out like the
physical keyboard (`q,w,e,r,t,y,u,i,o,p,a,s,d,...`), not alphabetically. Typing
`d` produced `f`, and letters before `a` in the alphabet landed on the function
keys - which the core rejects with `E_Exit`. It is an explicit table now, and
shift is sent as a real shift key around the base key instead of being folded
into the character, because the BIOS keyboard is what turns that into `:` or `?`.

### Starting a second game from the library (2026-10-01)

"After pressing Minus+Plus and starting another game, the game does not start
and an error comes out" had three separate causes, all of them state that the
first session left behind for the second one. They are worth recording because
they are the price of reusing the process instead of exec'ing a new one:

* `DriveManager::driveInfos[].disks` kept the drives of the finished session.
  The drives themselves are deleted with the DOS module, so the first
  `IMGMOUNT` of the next game appended its images behind a freed drive, and
  `InitializeDrive()` then handed that freed pointer to `Drives[]` and called
  `Activate()` on it - a virtual call through freed memory. `DriveManager::Init()`
  now starts the lists empty. This is the one that looked like "the game does not
  start, an error appears" for a game on floppy or CD images.
* The `AUTOEXEC` module was never destroyed, so the lines its `AutoexecObject`s
  had put into the file static `autoexec_strings` stayed there. The second game's
  `AUTOEXEC.BAT` therefore still contained the first game's `mount c` / `imgmount`
  / start lines, which ran first: the old folder was mounted as C: and the old
  start command ran, followed by "already mounted" errors for the new game's own
  commands. The module now has a destroy function, exactly like the other
  hardware modules, so a session only boots itself.
* `PROGRAMS_MakeFile()` appended one entry per internal program per session and
  never reused a slot, so after about fifteen games the machine stopped with
  "program size too large" - and every session added another copy of `Z:\MOUNT.COM`
  and friends pointing at the old slots. A name that is registered again now keeps
  its slot (and replaces its virtual file).

Two smaller ones were fixed with them. Leaving a session is done by throwing an
int out of the input pump, which unwinds through `PlatformVideo_EndUpdate()`:
the video update flag is now cleared *before* the pump runs, or a session ended
that way could leave the next one without a frame buffer. And the console mapper
bind table (and the touchscreen edge state) is reset for every session, because
it is filled again by the modules as they initialise; without that the second
game registered behind the first one's entries and lost host combinations like
Ctrl+F5 once the table filled up.

The library rows themselves are wired now too: the automatic scan runs once per
session behind its progress screen (B skips the walk, Y quits), and A starts a
scanned game while Plus browses into its folder.

Two more, both found from the device reporting "I press keys and nothing is
typed". The controller poll reported the *state* of every mapped button, so it
produced a key release for all twenty of them on every poll; the BIOS keyboard
buffer is a 32 entry ring and each code costs an interrupt, so it was full within
a frame and real keypresses were dropped as "buffer full". Only transitions are
reported now, which is also the only thing a physical keyboard ever produces.
And the boot commands cannot go through the command line: its parser understands
one level of quoting and then *removes* the quotes, so `-c "mount c a folder with
spaces"` reaches `MOUNT` as four separate words. They are written to a generated
`sdmc:/dosbox/boot.conf` instead, where `Section_line` keeps the line verbatim and
the quote aware parser in `DOS_Shell::Execute` hands `MOUNT` one path. The
command line remains as a fallback for the case where the SD card cannot be
written, and it only works for paths without spaces.

### In-game menu and software shaders (2026-10-01)

Minus + Plus used to end the session outright. On a console that button is also
the *only* button left for everything a PC player finds in a window or in
dosbox.conf, so it now opens a pause menu instead: `OVERLAY_PAUSE` in
`ports/common/osd_keyboard.cpp`, reached through `OSD_OpenPauseMenu()` and
toggled from the same controller frame the overlay state machine already reads.
The exit path is unchanged underneath (main.cpp still catches the int the input
pump throws), but the port now hands it to the menu as a callback
(`OSD_SetExitSessionFn` + `SwitchPlatform_ExitSession`) instead of throwing
straight out of `pump_npad()`, so the shared OSD never has to know how a console
ends a session.

The menu deliberately contains only what this emulator can really do:

| row | how it is applied |
| --- | --- |
| Resume / Virtual keyboard / Statistics / Controls | the existing overlays |
| Volume | new `MIXER_SetMasterVolume()`/`MIXER_GetMasterVolume()` in `src/hardware/mixer.cpp`, applied to every channel at once |
| CPU speed | `CPU_CycleMax` / `CPU_CycleAutoAdjust` / `CPU_CyclePercUsed` presets (300 .. 50000, plus auto), the same globals Ctrl+F11/F12 change |
| Frame skip, Scaler, Aspect ratio | the `[render]` section, changed the way the PC property editor does it: `HandleInputline()` plus `ExecuteDestroy(false)`/`ExecuteInit(false)`, so `RENDER_Init()` sees the change and recreates the video surface only when the picture really changed |
| Shader | software shader stage in the Switch blit (below) |
| State slot / Save state / Load state | the save state framework in `include/savestate.h` (see *Save states* below) |
| Exit to game library | `OSD_SetExitSessionFn` callback, port throws the int main.cpp catches |

One row is still missing on purpose: a **GLSL shader** needs an OpenGL output
this port does not have, so the shader row offers software effects instead and
never pretends to load a `.glsl` file.

The values are read back from the running machine every time the menu is drawn
(the config section, the mixer, the core globals), so it opens showing reality
rather than its own idea of it. Left/right step a value and repeat while held,
which a setting like the volume needs to be usable at all.

**Shaders.** DOSBox-X's shader method is a GLSL fragment shader stage:
`output=opengl` plus the `glshader` / `shader` setting in `[render]`, a list of
built-in shaders (sharp, crt, crt-geom, scan2x, ...) and `.glsl` files that can
be swapped at runtime from its menu. That method cannot be lifted into this port
as it stands: there is no OpenGL output here, only the software scaler and a
libnx framebuffer. What the port does instead is the equivalent stage done in
software while the scaler output is fitted to the screen, in the blit loop of
`PlatformVideo_EndUpdate()`:

* `none` - the plain nearest neighbour copy that was there before.
* `smooth` - bilinear, what makes a 320x200 game look like a picture.
* `scanlines` - one dark line out of two, the way a 15 kHz CRT draws 200 line
  modes.
* `aperture` - the RGB columns of an aperture grille, phase-aligned with the
  screen so the pattern cannot crawl.
* `crt` - the mask, the scanlines and a three tap horizontal phosphor glow.
* `crt-lottes` - Timothy Lottes' shader (the one libretro ships as `crt-lottes`
  and the Amiga ports carry as their CRT-Lottes filter), as a software stage:
  see below.

The names and the selection live in the shared OSD (`OSD_ShaderName()`,
`OSD_ShaderSelect()`, `OSD_ShaderSelected()` in `ports/common/osd_draw.cpp`), so
the menu and the video back end cannot disagree about what "scanlines" means, and
the pattern is implemented in `Switch_Shade()` / `Switch_Blend4()` /
`Switch_Glow()` with factors scaled to 0..256 so a multiply and a shift replace a
divide. `none` costs nothing beyond the copy; the heavier ones cost a few
operations per presented pixel, which is the price of not having a GPU stage.

**The Lottes shader in software.** The reference implementation is a GLSL ES
2.0 fragment shader (the Switch port of UAE4ALL2 carries it as `s_lottes_fs`):
`Warp` for the barrel distortion, `Fetch` for a nearest texel converted to
linear light, `Horz3`/`Horz5` for the horizontal beam of a phosphor dot
(`hardPix = -3`), `Scan`/`Tri` for the three scanline taps (`hardScan = -8`),
`Mask` for the diagonal RGB triad (`maskDark = 0.5`, `maskLight = 1.5`), and
`ToSrgb` at the end. Per presented pixel that is eleven texture fetches and a
dozen `pow()`, which a Switch core cannot afford while it is also emulating a
486, so the work is split by resolution:

* the **warp, the conversion to linear light and the horizontal beam** are a
  pre-pass at the *input* resolution (a quarter of the output's pixels) into a
  buffer the main loop samples. Same arithmetic, and the main loop's eleven
  fetches become three.
* the **main loop** keeps what has to happen at output resolution: the three
  vertical taps with their scanline weights (three `exp2()` per row, not per
  pixel), the diagonal mask anchored to the screen so it cannot crawl, and the
  trip back to sRGB through two 256 entry tables instead of `pow()`.

Two departures are deliberate and documented in the source: the beam is three
taps rather than five (the two outer weights at `hardPix = -3` are 2^-12, below
one eight bit step) and the pre-pass resamples with nearest neighbour, so the
warp quantises at most a fraction of one input pixel. The parameters are the
reference ones, letter for letter.

**The pause menu pauses.** It used to dim a machine that kept running, which had
two costs that showed up on the device: the compositing cost the emulator a few
milliseconds per frame (the audio buffers ran dry, audible as crackling), and
every row that changes something touched the renderer *from inside it*, because
the input pump is deliberately called from the presentation path (the core only
pumps when its interrupt queue is empty). Changing the scaler or the frame skip
from the menu therefore ran `RENDER_Init()` while `RENDER_EndUpdate()` was still
walking through the surface it had just freed - a use-after-free, and the crash
that came with it.

The pump now takes keyboard and mouse events from the presentation path but
hands the overlays to the emulator's own call site, which is between two
instructions; the pause menu then blocks there, so the machine really stops. The
frame the last update left in the mirror stays on screen behind the panel, the
audio backend fills silence instead of draining buffers no emulated device is
refilling, and a state saved from the menu cannot land in the middle of a sound
card DMA transfer.

Two smaller fixes went in with it: the virtual keyboard's own header has said
"B close" since it was written, but the handler never looked at B (or X), so a
player who opened it inside a game had only the on-screen CLOSE key to get out;
and the session reminder - what is starting, what was just loaded - now lasts
3.5 seconds instead of 12, long enough to read and short enough to play through.
The pause menu also names the game the slots belong to, because the slot files
are named after their number (`slotNN.dsv`) and not after the game.

### Save states (2026-10-01)

DOSBox-X saves to numbered slots (its menu, `[F11/F12]+S` to save, `[F11/F12]+L`
to load, a selectable slot index, one file per slot) and writes the whole machine
into the file: main memory, the CPU and FPU registers, and the state of every
device that can be mid-operation. The file header records the machine
configuration and the load path refuses a state whose memory size does not
match, which is why "Memory size mismatch" exists as a DOSBox-X error.

This tree had none of that, so the framework and the first blocks were written
here. `include/savestate.h` and `src/misc/savestate.cpp` (picked up by the port
build automatically: `misc` is in `CORE_DIRS`) hold the file format, the slot
bookkeeping and a registry of blocks:

```
"DOSBOXSV"           8 bytes
version              4 bytes   refuses a state from another format
main memory size     4 bytes   refuses a state from another machine
machine type         4 bytes
video memory         4 bytes
records:  tag[4] + size[4] + payload, repeated in the order they were written
end:      "END " + 0
```

A block is one function that reads or writes its own state, registered next to
the state it serialises, so adding a subsystem is a change in one file:

```c
static void SaveState_PIC(SaveState &state) { ... }
SAVESTATE_BLOCK(pic, "PIC ", "programmable interrupt controller", SaveState_PIC);
```

The same handler serves both directions (`state.Saving()`), and every read is
bounded by the record size in the file, so a truncated record fails the load
instead of walking off the end of the file. A record this build does not know is
skipped: a state from a newer build still loads for the parts it shares.

What is in a state today:

| block | tag | content |
| --- | --- | --- |
| main memory | `MEM ` | the whole conventional/extended pool, plus the A20 gate |
| processor | `CPU ` | all general registers, segment registers and their cached bases, flags (`FillFlags()` first), CR0/CPL/PMODE, the six system registers (GDT/IDT/LDT bases and limits), the cycle counters and the HLT state |
| floating point | `FPU ` | the eight 80-bit registers, the tag word, status word, top; the control word goes back through `FPU_SetCW()` |
| video card | `VGA ` | every register file (sequencer, attribute, CRTC, graphics, S3, SVGA banks, Hercules, Tandy, DAC), `vga.config` (the expanded masks the memory writes go through), the video memory and its expanded copy, the text font and cursor |

Loading a VGA state recalculates the derived half instead of trusting the file:
`VGA_SetupHandlers()` for the page handlers and the bank window, the DAC lookup
table and the palette the port shows from the colour entries, then
`VGA_SetupDrawing(0)` for the line geometry, the draw function and the vertical
timer. That is the same thing the card does after the writes that produced the
state, and it is why a loaded screen comes back with the right mode and timing
rather than with the one the port happened to be in.

Loading a CPU state clears the TLB (`PAGING_ClearTLB()`) and marks the lazy flags
as unknown, because both are caches of things the state just replaced.

The slots are `slotNN.dsv` in the folder the launcher selected for the game
(`<game folder>/savestates`, created on demand), so each game owns its ten slots
rather than sharing them with everything else on the card. The pause menu's
slot row shows the date and size of the file it points at, its footer shows the
name of the game those slots belong to (`SAVESTATE_SetGameName()`, taken from
the folder the launcher mounted) next to the directory, and a refused load says
why in the footer: the framework reports memory size, machine type and video
memory mismatches before it touches the machine.

What is *not* in a state yet, and is therefore whatever the running session
happened to have: PIC, PIT, DMA, keyboard, mouse, CMOS, the sound devices, the
joystick and the DOS shell's own bookkeeping (open files, current directory).
Those are the next blocks, one file each. Until they exist a load is best used
right after starting the game rather than an hour into one, and the state is
written while the machine runs (the menu does not pause it), which is the same
moment DOSBox-X's hotkey saves from.

What already worked before any of this, and still does, is the game's own save:
the folder the game runs from is mounted as C: straight from the SD card, so
files a game writes are on the card and survive the session.

## The mixer is on the emulator thread (2026-10-01)

`MIXER_Mix` is a timer tick handler: the samples of `mixer.work` are produced by
the emulator thread between two emulated instructions, and the audio thread's
job is to hand what is there to the hardware. Everything that slows an emulator
frame down therefore shows up in the *sound* first, and the two symptoms of one
cause are very different:

| symptom | cause |
| --- | --- |
| crackling under a heavy shader | the frame is late, so the mixer is late, and a shortfall is a click |
| a session that slows down and then stops | `mixer.needed` updated by two threads at once, so the emulator mixes more and more samples per tick |

The second one was a bug in this port and not a lack of cycles: the audio thread
called `MIXER_CallBack` without taking the lock `MIXER_Mix` takes, so
`mixer.needed` and `mixer.pos` were read and written from both threads. It only
shows up on a game that keeps the mixer busy, which is why it read as "audio with
many sprites freezes". Three changes, in order of importance:

1. the callback runs under `Platform_AudioLock()`, the same lock the emulator
   side mixes under (the equivalent of SDL's `SDL_LockAudio`);
2. `MIXER_Mix` caps `mixer.needed` at `MIXER_BUFSIZE`: more than a full ring
   cannot be played, so a counter that ran away is capped instead of being paid
   for once per tick for the rest of the session;
3. the audio thread pre-fills every buffer with the last sample it handed over.
   When the mixer is short, `MIXER_CallBack` returns without writing a byte, and
   the buffer still holds whatever was in it four frames ago; a held sample keeps
   the seam at zero, which is a hiccup instead of a click.

The mixer's cushion is forced to 60 ms of prebuffer (the stock is 25) before the
sections are initialised, so a dosbox.conf written with the stock value cannot put
the clicks back. The price is about one frame of audio latency.

## The Lottes shader has a budget, not a fixed cost (2026-10-01)

`crt-lottes` is eleven texture fetches and a dozen `pow()` per presented pixel in
the original. On the Switch there is no GL context behind the port - only a
linear framebuffer - so it is a software stage on the same core that emulates a
486 and feeds the mixer. The pre-pass at input resolution and the blocks below
are the two concessions that make it survive at all, and the clock is what ties
them to the machine they run on:

* the pass measures itself (`Platform_GetTicks()` around it) against a 4 ms
  budget, which is about a quarter of a 60 Hz frame on the core that is also
  doing the emulation;
* over budget it doubles the block it builds the picture out of (2, 4, 8 screen
  pixels), and under half the budget for four seconds it halves it again. The
  hysteresis is deliberately lopsided: a shader that oscillates between two
  resolutions is worse to look at than one that is a step too coarse;
* the starting block comes from the screen size, so a docked 1080p console never
  pays for the first seconds of a pass it cannot afford;
* 8 is a floor, not a look. At that size the pass is a few tens of thousands of
  blocks and cannot take the frame away from the game however slow the machine
  is. A shader that can stop a session is the one failure a console port cannot
  have.

What stays expensive whatever the block: the pre-pass, which is proportional to
the *DOS mode*, not to the console screen. A 320x200 game costs about a
millisecond; a 640x480 one costs four to five. That is the ceiling this build
runs into before the blocks matter at all.

## The library is a tab per kind of thing (2026-10-01)

The launcher used to open at `sdmc:/` with the scanned games written in front of
the card's folders, which is a file browser with a bookmark list at the top. It
now opens on the library, split the way the Amiga port splits its tabs:

| tab | rows | what A does |
| --- | --- | --- |
| `GAMES` | every folder the walk decided is a game | mounts the folder as C: and runs what the folder starts |
| `DISKS` | every disk image on the card (`.img .ima .bin .iso .cue .vfd .dsk`) | mounts that image and starts the program found on it (disk 1 is read directly, no mount needed to look) |
| `PROGRAMS` | every `.exe/.com/.bat/.cmd` inside a folder the walk recognised as a game | mounts its folder and runs exactly that program |
| `FOLDERS` | the card itself | the old browser, for everything the other three could not decide |

L/R (or the stick) switch tab and each tab remembers its row; B unwinds one level
at a time - a tab, then a folder, then the launcher - so there is always a way
back to the library. `+` opens the folder a row lives in.

The three flat tabs are built from the walk, which runs once per session: a game
folder is found, not descended into, but it is queued a second time to be read
for its disks and programs. Programs are collected only from folders the walk
already decided are games: in a plain data folder an `.exe` is a tool, and a tab
of tools is a tab nobody opens.

## The Lottes shader is gone (2026-10-01)

`crt-lottes` was eleven texture fetches and a dozen `pow()` per presented pixel
on a machine with no GL context behind the framebuffer, on the same core that
emulates a 486 and feeds the mixer. Every software approximation of it was
either too slow to be usable or visibly wrong - blocks where the input upscale
was nearest neighbour, a staircase where the shadow mask should have been a
triad - and the honest version of it is a GPU stage. The preset is removed rather
than left as the worst-looking one in the list; `none`, `smooth`, `scanlines`,
`aperture` and `crt` remain, and they are per pixel loops the emulator can
afford.

What stays from the attempt: the pre-pass lesson (an effect at input resolution
is a quarter of the pixels of one at output resolution) and the budget lesson
(the mixer shares the thread with the scaler, so an effect's cost is also the
sound's cost).

## Tick handlers outlived the machine (2026-10-01)

"Play a game, go back to the library, play another one" froze the second game
within a second, and it was not a leak but arithmetic: the tick handler list in
`pic.cpp` is a process wide one, `TIMER_Init`/`MIXER_Init` add to it and nothing
ever took anything out. The second session therefore ran the dead session's
`MIXER_Mix` *and* its own, twice per tick - and `MIXER_Mix` mixes `needed`
samples and then adds a tick, so two calls per tick grow `mixer.needed` by two
ticks' worth every tick. After a second it was mixing tens of thousands of
samples per tick, and the emulator was 100% busy inside the mixer.

Three changes, in the same spirit as the `DriveManager::Init()` and
`PROGRAMS_MakeFile()` fixes this port already had for the same reason:

* `TIMER_ClearTickHandlers()` empties the list, and the port calls it from
  `SwitchPlatform_ResetSession()`, which is where a console tears a session down;
* `MIXER_Stop()` removes its own handler, so the core is correct without the port
  asking;
* `SwitchPlatform_ResetSession()` also clears the two video flags an exception
  unwinds past (`in_video_update`, `video_updating`) and the renderer callback.

## A launcher that owns the controller (2026-10-01)

R is the statistics overlay on a handheld and R is also "next tab" in a library,
so the library took the controller: `OSD_SetGlobalShortcuts(false)` makes the
in-game global bindings (stick click, R, Minus+Plus) inert while a full screen
menu owns the screen, and the launcher sets it around its own loop.

Leaving a game also got a button of its own: ZL, in the pause menu header and
with nothing else bound to it anywhere, plus the last row of the menu for the
people who scroll.

## The BIOS answers the media change request (2026-10-01)

INT 13h AH=16h ("has the medium changed?") had no case at all in this tree. DOS
asks it as soon as it sees a floppy drive, and an installer uses it to decide it
must ask for the next disk - so a game mounted as a set of images printed
"Insert disk 2" and stopped, with the rest of the set behind the drive's disk
list and nothing to move it along (the only thing that ever did was Ctrl+F4).

`case 0x16` now answers it: with more than one image in the drive's list it
inserts the next one (`DriveManager::CycleDisks()`, plus the cache reset a real
swap needs) and reports "changed", exactly as a person swapping a floppy would.
`BIOS_SetAutoDiskSwap()` turns it on, and the port turns it on only for a game
the launcher mounted as a whole set - someone who mounted one image still swaps
it themselves. A counter caps it at 200 changes per drive so a guest that polls
the question in a loop cannot walk the set for ever.

The DISKS tab now mounts the whole set from any of its rows; the browser still
mounts just the image that was selected there. Auto-starting the game *after* the
installer is only automatic when the launcher can name it: `start.txt` in the
folder wins, otherwise an installer on disk 1 is run, otherwise a single program
is run. Reading what an installer is about to put on the disk is not something a
launcher can do without running it.

## The OSD font is anti aliased, and every scale has a rig (2026-10-02)

The face was already a real one - Fira Sans, baked into `ports/common/osd_font.h`
by `gen_font.ps1` so that nothing .ttf has to be read or shipped at run time - but
it was baked the way a console ROM font is: one bit per pixel, one size, and 2x
and 3x drawn by magnifying the 1x glyphs with whole pixels. On a 720p panel that
is a stair step on every edge, and a title was four pixels of stem blown up to
eight hard pixels of stem. Most of what read as "retro" was that, not the face.

It is now eight bits of coverage per pixel, and one rig per scale the UI actually
draws at: 12, 24 and 36 pixels, each rasterised on its own with the face's own
hinting (`AntiAliasGridFit`), so 2x text has the stem widths, the joins and the
spacing of a 24 pixel face rather than twice the pixels of a 12 pixel one. Each
glyph is stored as a box around its ink with the bearing and the top row that
place it, and the rig's advance with it:

| rig | face | cell rows | baseline row | ink bytes |
| --- | --- | --- | --- | --- |
| 1x | 12px | 15 | 10 | 4.5 KB |
| 2x | 24px | 30 | 20 | 15.7 KB |
| 3x | 36px | 45 | 30 | 35 KB |

The contract the layout code depends on is unchanged, which is the point of the
cell column: a rig of scale s still has a cell of 15\*s rows with the baseline on
row 10\*s, so `OSD_FONT_HEIGHT` and every `(h - OSD_FONT_HEIGHT * scale) / 2` in
`osd_keyboard.cpp` and `osd_menu.cpp` kept working untouched
with a face half again as detailed underneath them. At 1x the measured ascent
(10) and descent (4) are the same numbers the 1-bit bake produced.

Drawing is a blend, not a fill: `OSD_BlendPixel()` moves the pixel it is given
that fraction of the way to the text colour, in the canvas' own channel order,
which is why an anti-aliased bake is worth nothing if the blit is a stamp. The pen
and `OSD_TextWidth()` both take the rig and the magnification from `OSD_FontRig()`
rather than working them out separately - a disagreement between the two is what
walks a row of text out of the panel it was measured for. A scale past the table
falls back to magnifying the largest rig, which is the old behaviour and now only
reachable by a caller that asks for something the UI does not use.

Two tools came with it. `preview_font.ps1` renders a mock of the library screen
from the header - geometry, colours and all - plus a 3x magnification of the
header, into `sheet.png`/`zoom.png`, so a change of face or of size is judged
without a build and without a console. `gen_font.ps1` now takes the pixel size
and the list of scales, and reports the ascent, the descent and the ink of every
rig it bakes. At 3x three glyphs (the parentheses) reach one row above the nominal
cell, because rounding a 0.83em ascender three times does not land on the same
row as multiplying it by three; the generator allows up to three rows of that and
reports how many glyphs do it, since a row of a parenthesis above its cell is
invisible where clipping it is not.

## What a freeze leaves behind (2026-10-02)

A hang is the one failure a console cannot describe: no exception, no error
applet, the picture stays on the last frame that was drawn and `startup.log` ends
wherever it happened to end. Two probes now write into that log.

`trace_heartbeat()`, called from `Platform_PumpEvents()`, writes a beat every
five seconds with the guest's program counter in it:

```
[dosbox] alive 7: pumps=91043 cs:ip=0E5A:12C4
```

That is enough to separate the two cases a frozen screen cannot. Beats that stop
altogether mean the host is stuck inside the emulator - a file call that never
returns, a lock taken twice - and the last line before the silence is where.
Beats that keep coming with the same `cs:ip` mean the *emulated machine* is
spinning in place, and the address says which code is spinning. A guest waiting on
a key is not a freeze: its `cs:ip` moves, since DOS polls the keyboard through the
BIOS in a loop.

INT 13h AH=16h (the automatic disk swap) now writes to the card as well as to
`LOG_MSG`, which on a console goes to the debug channel nobody reads. An
automatic image change under a program that is halfway through an install is the
first thing to rule out when one of those freezes at the destination prompt: it is
the only part of the disk path in this tree that does something a real machine
did not do.

The first answer from the device already ruled out half of the cases: with the
screen apparently frozen, Minus + Plus still opens the pause menu. The pump runs
from the emulator's own loop, so the host is not stuck inside a call - a blocked
file write would have taken the pump with it. What is left is either the emulated
machine spinning in place (an installer re-asking the same question is
indistinguishable from a hang on the screen, and cs:ip repeating across beats says
which) or a picture that is not being presented. Both heartbeats, the one in the
pump and the one in the pause menu's own loop, land in the same file, and the beat
carries whether an overlay was up so a menu left open is not read as a stop.

`localDrive::FileCreate()` also writes the name of every file it creates, and of
the ones it cannot create, to that file. A DOS program that dies while writing
leaves the last name it managed; a program that never got that far leaves no
`DOS: create` line at all, which is the difference between a file problem and an
input or loop problem, and it is not a difference the screen can show.

The destination prompt itself is (mostly) not this port's business - free space
for a `MOUNT`ed folder is DOSBox's own 262 MB default, and it was checked. What is
this port's business is that a folder holding both an installer and the program it
installs used to start the installer *every* time, so the game the player already
had on the card was behind a wall that re-asks for a destination drive. A folder
with exactly one program that is not an installer now starts that program, and the
installer stays a row of the PROGRAMS tab.

## Rows are as tall as the text in them (2026-10-02)

Three complaints came back from the device that all had the same shape: a row
sized by a constant that was right for the face it was written for and wrong for
the one in the tree.

- The library list draws its rows at 56 pixels instead of 52. A library row is a
  name at scale 2 (30 rows) with the folder at scale 1 (15) under it, ending 51
  rows below the top of the row, inside a highlight 46 pixels tall - so the
  second line printed through the bottom of the bar and the bar read as too
  short for its own text. The footer moved up to `height - 44` to keep its hold on
  the bottom row's tag.
- A program row (the PROGRAMS tab) was drawn at scale 3 *and* given the folder
  line the flat tabs need: 66 pixels of text in a 52 pixel row, which is what
  turned a column of installers into a wall of EXE with `INSTALL.EXE` printed on
  top of the file under it. Big text is for the rows that start a game, and those
  are one line; a program row is a two line row at scale 2 like every other row
  that knows where it lives.
- The pause menu is nineteen rows, and it was drawing all of them at 28 pixels -
  two pixels shorter than the cell of the 2x label in each one, so every selected
  label stuck out of the highlight behind it. It is now a window of eleven rows of
  46 pixels with a scrollbar, scrolled by the selection (`pause_scroll_into_view()`,
  clamped once in the draw rather than at every place the cursor moves).

About scrolls with the analogue stick, not only the D-pad: the overlay's up and
down edges are taken from the D-pad *or* the stick, the way the launcher already
reads it, since a panel that answers one of the two reads as stuck. Its line pitch
comes from the font too (a heading is a 30 row cell, not the 28 it was drawn in),
its scroll limit is found by walking the same pitches the draw walks - a count of
lines divided by a line height is only right while every line is the same height -
and it draws a scrollbar, because a panel that scrolls and does not say so reads as
a panel with something missing at the bottom.

## The frame that closes an overlay belongs to the overlay (2026-10-02)

The library and the overlay state machine each keep their own button edges, so a
press was being read twice: `OSD_UpdateOverlays()` closed About on B or A, and the
same frame then reached the library, which was still looking at the same press. B
closed the panel *and* left the tab, and A closed it and started whatever row
happened to be behind it - which is why opening About on a tab looked like it
started a game when it was closed.

The library now swallows the frame that closed an overlay: `ui_consumed` is true
when the overlay was up at the top of the frame and is gone at the end of it, and
every library binding tests `ui_busy || ui_consumed`. A press is either the
overlay's or the library's, never both.

## The pointer walks to the right on its own (2026-10-02)

A long game of Monkey Island ended with the cursor against the right edge of the
screen, which is not the game's doing and not the player's: a stick does not rest
where the hardware calls zero. There are always a few thousand units of offset,
and it grows with use and with the temperature the console is at, so a stick that
has been left alone is reported well off centre. The mouse path took the hardware
zero, subtracted a fixed 6000 unit deadzone and moved the cursor with whatever
was left, which for an offset past that deadzone is a constant crawl.

`stick_centre()` now tracks the resting position instead of assuming it. A reading
that is already close to the tracked centre is folded into it slowly, one that is
clearly being held out of the way (12000 units or more) is left alone, so a
deliberate movement can never be adapted away - and the tracked centre is what the
deadzone, the emulated joystick axes and the menu's analogue input are all measured
from, which is what stops the launcher list from scrolling by itself as well. The
step is per millisecond rather than per call, because the same helper runs from
three cadences: the pump (thousands of times a second) for the joystick stick, a
fixed 8 ms period for the mouse stick, and the menu's frame rate.

## Putting a game on the card over the network (2026-10-02)

A Switch has no card slot to take out and no cable to a desktop, so a game that is
not already on the SD card can only get there over the network. The library now
runs a small FTP server while it is on screen, the same feature the Amiga port has
(`switch_ftp.cpp`, adapted from it), and it is driven from the shared launcher
through `OSD_SetFTPServer()` so the UI stays port agnostic.

- Minus toggles it. The footer carries the `- FTP` hint and, under the hints,
  the line that matters: `FTP ftp://192.168.1.42:5000`, with the number of
  connected clients when there is one. An address is the one thing a client cannot
  guess and a handheld has nowhere else to show it.
- It lives exactly as long as the library: started when the library opens if it
  was on in an earlier visit, stopped before a game starts. A game wants the whole
  console and an upload wants a server that does not vanish mid-file.
- Passive mode only, port 5000, up to eight sessions, one accept thread and a
  detached thread per client. An active connection would have the console open a
  port on the desktop, which the routers in between routinely refuse.
- The virtual root holds one entry, `sdmc`, and a session lands in
  `/sdmc/dosbox`: the folder the launcher itself browses. Every path resolves
  under `sdmc:` and `..` can never climb above the root, so a client cannot ask
  for anything this port does not own.
- The socket service is initialised once and never taken down, and stopping wakes
  every session with `shutdown()` while each thread closes its own descriptor: a
  descriptor closed under a thread that is still writing is a descriptor number a
  later connection can be given.
- A session that changed the card (STOR, APPE, DELE, MKD, RMD, RNTO) sets a flag
  when it ends. The library consumes it and rebuilds the open tab, and re-runs the
  whole card scan when the GAMES tab is the one on screen, because the rows of that
  tab *are* the scan.

Not yet exercised from a device: the first test is an FTP client against an
address the header prints.

## Stopping the server is part of closing the app (2026-10-02)

The first report from the device was that leaving the FTP server on and closing
the app crashed. The library is where the app is closed from, so "stop the
server" sits on the way out of the process, and the first version was written as
if stopping were a courtesy rather than the last thing the threads see. What it
did wrong, in the order it mattered:

- The accept thread's socket was closed by the stopping thread while the accept
  thread was inside `select()` on it. Nothing may close a descriptor another
  thread is using; the accept thread is now joined first, and only then is the
  socket closed.
- The sessions were woken and then given a bounded second to leave on their own,
  so the app could reach `appletExit()` with a thread still inside the network
  stack. Sessions are now *joined*: when the stop returns, no thread of the
  server exists. Their descriptors are shut down and each thread closes its own,
  which is also what keeps a reused descriptor number out of the hands of the
  stopper.
- The per-client threads were detached, so nobody could join them even in
  principle, and a session slot went live *before* its thread handle was written
  - a stop in that window would have joined garbage.
- The socket service was initialised on every toggle and never taken down, which
  leaves one service session behind per toggle. It is opened with the server and
  given back with it now (`socketInitializeDefault()` in start, `socketExit()` in
  stop, every failure path included), and only once nothing can be using a socket
  any more.
- Every wait in the server was a long one: a fifteen second `select()` on the
  passive listener, a sixty second `recv` on the control connection, fifteen
  second timeouts on a transfer. A stop that missed one of those left a thread
  inside the network stack for that long, so all of them are 250 ms slices in a
  loop that checks the server's own flag, with a total deadline that ends a
  transfer a client has abandoned.
- The library never asked the console whether the app was being closed. A "Close"
  from the HOME menu waited for the player to leave the library himself, which he
  never does; the system stops waiting long before that and the app is then closed
  from outside, error screen and all. The launcher now polls the applet once per
  frame (`OSD_SetExitPollFn`) and leaves the way its own quit button does.

`SwitchPlatform_ResetSession()` stops the server too, because every path that ends
a session - the applet exit request included - goes through it, and `main.cpp`
stops it once more before the exit sequence. The traces around all of this
(`ftp: stopping`, `ftp: %d session(s) woken and joined`, `ftp: stopped, every
session joined`, `exit: ftp`) are what the next report from the device will be
read from: the last line in `sdmc:/dosbox/startup.log` says how far the exit got.

### What the second report added (2026-10-02)

The exit itself stopped crashing, but the error screen came back when the *next*
app was launched - any app. That is the shape of a fault report this console
defers: the applet that died is already gone, so the report is shown by the next
applet that comes up. Two things were left behind by the process, and both are
what an applet must not leave behind:

- The socket driver holds bsd sessions and registers them with nifm, so as long
  as it is open the console's network module is working for a process that may be
  on its way out. `socketExit()` in the stop closes that, and the storage device
  is committed (`fsdevCommitDevice("sdmc")`) in the same place: a client's upload
  and the log are on the card before anything else is allowed to look at it.
- The log's descriptor stayed open until the process died. It is a plain
  descriptor now (`open`/`write`, not a `FILE`, which also removes the shared
  stdio buffer three threads were writing through) and the exit sequence closes
  it (`SwitchPlatform_TraceClose()`).

The rest of the exit is unchanged and still traced (`exit: ftp`, `exit: card
committed`, `exit: audio`, `exit: video`, `exit: overlays`, `exit: done`), so the
last line of the log says how far the next run got.

## The credits roll, and every line is centred (2026-10-02)

The About panel used to be a list that stepped one line per d-pad press, drawn
from a fixed left inset with a scrollbar thumb on the right. It read as a text
file in a box: the headings had no relationship with the text under them, and
nobody found the last lines because nothing said there were any.

It is a roll now, the way the console build it is modelled on draws its credits:

- The position is a distance into the list in pixels and it is advanced from
  `Platform_GetTicks()`, not from a frame count, so the roll runs at the same
  pace whatever it is drawn over (the library loop, or the pause loop while the
  machine stands still behind the panel). A frame that took a long time over - a
  load, a resume from sleep - is capped at 100 ms instead of jumping the text.
- The list wraps, and the wrap needs no thumb: a scrollbar describes an end, and
  a roll that comes back around does not have one. A three line gap after the
  thanks keeps the last line from meeting the title. The thumb is gone with it.
- Every line is centred with `OSD_TextCentre()`. The two column pitches still
  come from the font (a heading is scale 2 and twice as tall), and the layout
  walks the list once to find the length of the whole roll.
- The d-pad walks the pace instead of stepping lines, holding it keeps walking,
  and the pace eases back to 1.0 as soon as nothing is held, so a line slowed
  down to be read does not leave the roll slow. `Y` stops the roll where it is;
  `B` (and `A`, and `X`) still close the panel or step back to the menu it was
  opened from. The footer says all of that and shows the pace on its right while
  it is not the default one - a roll that has been stopped has to say so.

The one thing that made a pixel scroll harder than a line scroll was the clip: a
line half out of the band would have been drawn over the header or the footer,
because the OSD clips to the canvas and not to a panel. Instead of a new clip
rectangle in the shared drawing code, the roll draws into a canvas one band tall
whose pixel pointer starts at the inside of the panel: the existing per-pixel
bounds checks then cut the line exactly at the band. This is also why the widths
were measured with the baked font before choosing the band inset: the longest
line is 550 px at scale 2 (a section heading) against a 1064 px band on the
narrowest canvas, so centring cannot run a line under the panel border.

## The scan says what it is doing (2026-10-02)

The progress screen had a bar, a percentage and the count of what had been found
so far. `SEARCHING` sat up in the header next to the two roots being walked, far
from the bar, so a card with many folders showed a bar that said the machine was
busy without saying what it was busy with. `Scanning in progress...` is drawn
centred 54 px above the bar while the walk is running; the frame that holds the
finished bar keeps the header's `Library ready` and the count alone.

## The controller profile default is called Default (2026-10-02)

The first preset was named "Default (Desert Strike)", which described the game it
was tested with rather than what the mapping is, and made the profile list read as
a list of games. It is `Default` now, and the comments in `pad_mapping.h` and
`pad_mapping.cpp` follow it; `controls.cfg` is unaffected, since the file stores
the preset index and not the name.

## The port is called NX-DOSBox (2026-10-03)

Every string a player can read now says NX-DOSBox instead of DOSBox: the
launcher's title row, the About header and its first credit line, the pause row
that leaves the emulator, the two FTP greeting lines, the `--version` banner and
the two fatal error labels. The artifact follows: `TARGET` is `NX-DOSBox`, so the
build writes `out/NX-DOSBox.nro` and `nacptool` stamps the same name into the
NACP that the Homebrew menu shows.

Two things are deliberately unchanged. The card layout still starts at
`sdmc:/dosbox` (`sdmc:/dosbox/games`, `savestates/`, `boot.conf`, `dosbox.conf`,
`startup.log`): renaming a folder on a console means an install in the field
loses its library and its save states, and the name is a label, not a path. The
splash keeps `src/gui/dosbox_splash.h` untouched - it is the upstream DOSBox
artwork, drawn from a baked 640x400 RLE image, and it is the one place where the
DOSBox name is honest about what this is a port of. The credit lines that name
the upstream team, the copyright and the DOSBox-X fork stay as they are for the
same reason.

## The classic entry is DOSBox itself (2026-10-03)

The library now carries the escape hatch it was missing: a row pinned under the
`GAMES` title, `Classic DOSBox - SD card on C:`, that starts nothing and mounts
the card itself. A commits `sdmc:/` as C: with an empty image list and an empty
boot list, so the machine comes up on its own shell with the card reachable as
C: and every command left to the user - which is what the port offers when the
automatic boot guessed the wrong program. Because the row is a choice and not a
game, three places had to be told about it: the row drawing (green, disk icon,
`DOS` tag, its own second line), `launcher_choice_count`/`rank` (it counts, it is
selectable) and the opening cursor, which a new `launcher_first_choice()` puts on
the first real game so that A on the first frame does not open a prompt.

The session it produces is deliberately not a normal game session. `main.cpp`
recognises the card mount and keeps the shared `sdmc:/dosbox/savestates`
(creating `sdmc:/savestates` for a boot that has no game folder would be a folder
nobody asked for), and the reminder says `CLASSIC DOSBOX - C: IS THE SD CARD -
PRESS L3 TO TYPE` instead of `NO START PROGRAM FOUND`, because the two cases look
identical on screen. The DOS shell's own welcome box also carries the name now:
`SHELL_STARTUP_BEGIN` reads `Welcome to NX-DOSBox %-8s`, with three spaces taken
out of the row's padding so the 70 column frame still lines up.

## The settings the library never had (2026-10-03)

The keys that only a hand edited dosbox.conf could reach are a fifth tab now:
`SETTINGS`, with machine, memory size, CPU type and core, sound card, FM
synthesis mode, PC speaker, Tandy, Disney, Gravis Ultrasound, scaler, EMS, XMS
and UMB. Every value string in the table is one the core accepts - the
`Set_values()` lists in `src/dosbox.cpp` and the `strcasecmp` chains in
`sblaster.cpp` are the authority - because what the tab produces is a config file
the emulator parses at the next start: a value the core would refuse is a row
that silently does nothing. `core` deliberately offers no `dynamic`, since this
build has no recompiling core to fall back on.

The tab writes `sdmc:/dosbox/settings.conf` and `main.cpp` parses it after the
usual dosbox.conf, so the visible menu is the last word for the keys it manages;
the file only appears once a row has actually been changed, which is what leaves
a hand written dosbox.conf in charge of everything the tab does not cover. The
write happens on the change, not at start up, and the tab is driven with left and
right (the shoulder buttons still switch tab, and `A` steps a value forward the
way the pause menu's adjustable rows do). Because the launcher runs before the
configuration is parsed, a value chosen here applies to the game started from the
same visit to the library - there is no restart between the choice and the
machine it affects.

Two lists stay short on purpose. The in game pause menu keeps its eleven scalers
because it is walked during play; the tab carries all eighteen the core has
(hq2x, the 2xSaI family, advinterp and the rest), because that is where the
choice is made on purpose and where it is remembered. Aspect is not a row here:
the port already forces it on when a dosbox.conf leaves it at the DOSBox default,
and the pause menu changes it live, so a second place to set it would only be a
second place to disagree with.

## The settings say how they are changed (2026-10-03)

A SETTINGS row used to show its value on a second line and a static `< >` tag on
the right. The value is the tag now: the row reads `Machine` on the left and
`< ega >` on the right, the live string wrapped in the arrows that change it,
orange while the row is under the cursor and blue otherwise. The second line is
gone because it showed the same value the tag now shows - a setting row finally
reads like every other row, a name and the one thing the row is about. The room
the name may take is fitted against the real tag width instead of the fixed 88
pixels the old tag reserved, because `< pentium_mmx_slow >` and `< svga_paradise >`
are wider than four characters.

## The mixer, one channel at a time (2026-10-03)

The pause menu's `Volume` row moves every channel at once. The per-channel
levels MIXER.COM has always offered were reachable only by typing at the DOS
prompt; they are rows under `Volume` now, one for each channel the running
machine actually has. The names are the build's own - `SB`, `FM`, `CMS`, `GUS`,
`SPKR`, `TANDY`, `TANDYDAC`, `DISNEY`, `CDAUDIO`, `TSF_MIDI` - and each row is
looked up with `MIXER_FindChannel()` when the menu opens, so a channel the
current configuration does not create is a row that is not there instead of a
slider that does nothing. Left/right step a channel 5% at a time, read back
through `volmain` when the row is drawn, so a level set from MIXER.COM shows up
here as the level it is. The row table, the scroll window and the scrollbar are
sized from what was found, which is what keeps the list honest when the set of
channels changes between two games.

## The port's own commentary is gone (2026-10-03)

Every comment this port wrote in its own sources has been removed: `ports/common`
and `ports/switch`, the shell scripts that drive the two build systems, the
PowerShell tools and the generator's own emission of comments into
`osd_font.h`. What stays is the licence: the GPL header at the top of each port
file is the DOSBox Team's copyright and is untouched, and nothing under `src/`
or `include/` was touched at all - those files are upstream DOSBox SVN. A
token-level check of every C++ file (string literals and the numbers outside
comments, compared before and after) found no code lost to the cleanup.
