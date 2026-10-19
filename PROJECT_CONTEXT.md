# BeatStudio — Project Context

> Keep this file up to date. When starting a new session, point Dom to this file for full context.

---

## What is BeatStudio?

A fork of LMMS (Linux MultiMedia Studio) — a full-featured open source DAW written in C++/Qt.
Repo: `https://github.com/adenman/beatstudio.git`

---

## Goal

Make BeatStudio feel like a pro DAW. Features should match or exceed FL Studio quality.

---

## Completed Features

### Humanize (2026-04-24)
**Goal:** Break the mathematical precision of MIDI to make it sound like a real human played it — just like FL Studio's humanize feature.

**What was implemented:**
- `MidiClip::humanizeNotes(const NoteVector& notes, int timingAmount, int velocityAmount)`
  - Randomly offsets note timing by ±timingAmount ticks
  - Randomly offsets note velocity by ±velocityAmount
  - Clamps timing to ≥0, velocity to 1–200
  - Calls `rearrangeAllNotes()` and emits `dataChanged()` after
- `MidiClipView::humanizeSelection()` — right-click context menu on any melody clip → "Humanize"
  - Two QInputDialog prompts: timing variation (ticks) + velocity variation (0–100)
  - Full undo support via journal checkpoints

**Files changed:**
- `include/MidiClip.h` — added `humanizeNotes()` declaration
- `src/tracks/MidiClip.cpp` — added `humanizeNotes()` implementation
- `include/MidiClipView.h` — added `humanizeSelection()` declaration
- `src/gui/clips/MidiClipView.cpp` — added `humanizeSelection()` implementation + context menu entry

**Status:** Shipped and pushed. Also available in the Piano Roll wrench menu (`961a78c`).

### Native VST3 hosting (2026-10-19, branch `feature/vst3`)
**Goal:** BeatStudio only hosted VST2 (Vestige/VstEffect). Add native VST3 instruments + effects on Windows.

**Design:**
- `plugins/Vst3Base/` — shared static lib `vst3base`:
  - `Vst3Host` — in-process host for one VST3 class (load module, buses, process, MIDI, state, editor window)
  - `Vst3PluginPanel` — picker combo (installed VST3s) + Browse + "Show plugin window" button
  - `Vst3SubPluginFeatures` — lists installed VST3s in the instrument browser / Add Effect dialog
  - Steinberg VST3 SDK v3.8.0 (MIT) fetched by CMake FetchContent with pinned SHA256; only hosting sources built (`vst3sdk` target, warnings off)
- `plugins/Vst3Instrument/` (`vst3instrument`) and `plugins/Vst3Effect/` (`vst3effect`)
- Windows only for now (`if(NOT LMMS_BUILD_WIN32) return()`); editors embed via HWND

**What works (by design, needs testing on a real Windows box):**
- Notes → VST3 note events; pitch bend / CC / aftertouch → parameters via `IMidiMapping`
- Tempo, time signature, play state, song position in `ProcessContext`
- Plugin editor in its own window owned by the main window, HiDPI scale, plugin-driven + user resize
- Component + controller state saved as base64 in the project (`vst3path`, `vst3class`, `componentstate`, `controllerstate`)
- Parameter edits from the plugin GUI forwarded to the audio thread; processor output params back to the GUI

**Known limitations / next steps:**
- [ ] Plugin parameters are not exposed as BeatStudio automation lanes yet
- [ ] No latency compensation; no sidechain/aux buses (main stereo in/out only)
- [ ] In-process: a crashing plugin takes BeatStudio down (VST2 runs out-of-process)
- [ ] Program changes / VST3 presets (.vstpreset) not wired
- [ ] Linux/macOS editors not implemented

**Local syntax-check setup (no Windows box needed):** zig's clang (`python3 -m ziglang c++ -target x86_64-windows-gnu`) +
Windows Qt 6.8 headers via `aqtinstall` in `/tmp/qt`; see `/tmp/chk/cc.sh` (recreate if /tmp was wiped).

### CI note
Since the April recorder work, BeatStudio needs Qt6 + Qt Multimedia (`WANT_QT6` defaults ON). The upstream CI
jobs were never updated: Linux/macOS/mingw configure fails. The MSVC job was fixed on `feature/vst3`
(`modules: qtmultimedia`) and produces the Windows installer artifact.

---

## Pending / Next Steps

- [ ] Test VST3 hosting on Windows, then merge `feature/vst3` into `main`
- [ ] VST3 parameter automation
- [ ] Consider a "strength" slider UI instead of two separate dialogs
- [ ] Explore more humanization axes: note length variation, slight pitch microtuning

---

## Repo Structure (key paths)

```
beatstudio/
├── include/          # Header files
│   ├── Note.h        # Note data model (pos, volume, key, length)
│   ├── MidiClip.h    # MIDI clip / note container
│   └── MidiClipView.h
├── src/
│   ├── core/         # Engine core
│   ├── tracks/
│   │   └── MidiClip.cpp
│   └── gui/
│       └── clips/
│           └── MidiClipView.cpp
├── plugins/          # Instrument + effect plugins
└── data/             # Themes, icons, presets
```

---

## Build

C++/Qt/CMake project. See `INSTALL.txt` and `CMakeLists.txt` for build instructions.

---

## GitHub Auth

- **User:** adenman
- **Token:** Stored in TOOLS.md in the OpenClaw workspace
- **Note:** Token in the git remote URL works for push (verified 2026-10-19).
