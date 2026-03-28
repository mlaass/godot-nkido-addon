# PRD: Nkido v2 — Samples, Files, Editor, Cross-Platform

**Date:** 2026-03-28
**Prerequisite:** v1 MVP (Phases 1–3) is complete.

---

## 1. Overview

### 1.1 Context

The v1 MVP delivers a working Akkado synthesis pipeline in Godot: compile, play, parameter binding, hot-swap, and an inspector plugin. However, it is synthesis-only (no samples), inline-source-only (no files), single-platform (Linux), and the editor is limited to the inspector panel width.

### 1.2 Goals (v2)

- **Sample loading**: Load WAV/OGG/FLAC/MP3 audio files and SF2 SoundFonts for use in Akkado patches
- **`.akk` file workflow**: Load Akkado source from files, with editor integration and auto-recompile on change
- **Enhanced editor**: Bottom panel with full-width code editing, error markers, autocomplete, waveform visualization
- **Cross-platform**: Windows x86_64, macOS arm64/x86_64, CI/CD pipeline, release workflow

### 1.3 Non-Goals (v2)

- Spatial audio (AudioStreamPlayer3D integration)
- Web/HTML5 or mobile export
- Recording / bouncing audio to files
- Akkado `import` statements (cross-file module resolution)
- Networked/multiplayer audio sync
- Asset library publishing (packaging only — submission is manual)

---

## 2. User Experience

### 2.1 Sample-Based Drum Pattern

```gdscript
extends Node2D

func _ready():
    # Load samples globally (once, shared across all players)
    NkidoEngine.load_sample("bd", "res://samples/kick.wav")
    NkidoEngine.load_sample("sd", "res://samples/snare.wav")
    NkidoEngine.load_sample("hh", "res://samples/hihat.wav")

    var drums: NkidoPlayer = $Drums
    drums.source = '''
        velocity = param("velocity", 0.8, 0, 1)
        pat("bd ~ sd ~ bd bd sd ~") |> samp(%, velocity) |> out(%, %)
        pat("hh*8") |> samp(%, velocity * 0.5) |> out(%, %)
    '''
    drums.compile()
    drums.play()
```

### 2.2 SoundFont Instrument

```gdscript
extends Node2D

func _ready():
    NkidoEngine.load_soundfont("piano", "res://soundfonts/grand_piano.sf2")

    var keys: NkidoPlayer = $Keys
    keys.source = '''
        pat("[c4, e4, g4] [d4, f4, a4] [e4, g4, b4]")
        |> sf("piano", 0, %, 0.8)
        |> out(%, %)
    '''
    keys.compile()
    keys.play()
```

### 2.3 `.akk` File Workflow

```
res://audio/
├── ambient_forest.akk
├── combat_music.akk
└── ui_sounds.akk
```

```gdscript
# In the inspector: set source_file to "res://audio/combat_music.akk"
# Or from script:
var music: NkidoPlayer = $Music
music.source_file = "res://audio/combat_music.akk"
music.compile()  # Reads from file
music.play()
```

### 2.4 Bottom Panel Editor

```
+------------------------------------------------------------------+
| Nkido  |  combat_music.akk  |  [Compile] [Play] [Stop] | BPM:72 |
+------------------------------------------------------------------+
| 1  intensity = param("intensity", 0, 0, 1)                      |
| 2                                                                |
| 3  pad = osc("saw", 110) |> lpf(%, 200 + intensity * 3000)     |
| 4  lead = osc("sqare", 440) * intensity                         |
|         ~~~~~~~~                                                 |
|         ^ Unknown oscillator type "sqare". Did you mean "square"?|
| 5                                                                |
| 6  (pad * 0.5 + lead * 0.3) |> out(%, %)                       |
+----------------------------------+-------------------------------+
| Parameters                       |  [~~~ Waveform ~~~~~~~~~~~]  |
|  intensity  [========|--] 0.45   |  [~~~~~~~~~~~~~~~~~~~~~~~ ]  |
+----------------------------------+-------------------------------+
```

---

## 3. Architecture Changes

### 3.1 Updated Class Diagram

```
NkidoEngine (Object singleton)
  - global_bpm_: float
  + sample_bank_: cedar::SampleBank          [NEW]
  + sample_registry_: akkado::SampleRegistry  [NEW]
  + soundfont_registry_: cedar::SoundFontRegistry  [NEW]
  + decoded_cache_: map<String, DecodedAudio>  [NEW]
  │
  + load_sample(name, path) -> bool           [NEW]
  + load_soundfont(name, path) -> bool        [NEW]
  + clear_samples()                           [NEW]
  + clear_soundfonts()                        [NEW]
  + get_loaded_samples() -> Array             [NEW]
  + get_loaded_soundfonts() -> Array          [NEW]

NkidoPlayer (Node)
  + source_file_: String                      [NEW]
  │
  + compile():
  │   1. Read source from source_file if set  [NEW]
  │   2. Sync samples from NkidoEngine into VM SampleBank  [NEW]
  │   3. akkado::compile(code, sample_registry)  [CHANGED]
  │   4. Resolve sample IDs in state_inits    [NEW]
  │   5. apply_state_inits()
  │   6. vm->load_program()
  │
  + get_required_samples() -> Array           [NEW]
```

### 3.2 Sample Loading Flow

```
User calls NkidoEngine.load_sample("bd", "res://samples/kick.wav")
  │
  ├─ Godot FileAccess reads file bytes
  ├─ cedar::AudioDecoder::decode() → DecodedAudio
  ├─ Store in decoded_cache_ (keyed by name)
  ├─ SampleBank.load_sample(name, data, frames, channels, rate) → id
  └─ SampleRegistry.register_sample(name, id)

User calls NkidoPlayer.compile()
  │
  ├─ Read source (from source_file or source property)
  ├─ akkado::compile(code, filename, &sample_registry)
  │     └─ Compiler resolves sample("bd") to ID at compile time
  ├─ Sync required samples into VM's SampleBank:
  │     For each required_sample in CompileResult:
  │       Copy decoded audio from NkidoEngine cache → vm.sample_bank()
  ├─ Resolve sample IDs in state_inits events
  ├─ apply_state_inits()
  └─ vm->load_program(bytecode)
```

### 3.3 SoundFont Loading Flow

```
User calls NkidoEngine.load_soundfont("piano", "res://soundfonts/grand.sf2")
  │
  ├─ Godot FileAccess reads file bytes
  ├─ SoundFontRegistry.load_from_memory(data, size, name, sample_bank)
  │     ├─ Parses SF2 via TinySoundFont
  │     ├─ Extracts samples → SampleBank (auto-assigned IDs)
  │     └─ Stores zone metadata, envelopes, presets
  └─ Returns sf_id (0-based)

On compile(), sf() references in Akkado resolve via the SoundFontRegistry
```

### 3.4 `.akk` File Resolution

```
NkidoPlayer.source_file = "res://audio/combat_music.akk"
  │
  compile():
  ├─ If source_file is set:
  │   ├─ Open via Godot FileAccess (supports res://, user://)
  │   ├─ Read UTF-8 text → use as source code
  │   └─ Pass filename to akkado::compile for diagnostics
  └─ Else: use source property as before
```

---

## 4. API Reference

### 4.1 NkidoEngine — New Methods

```gdscript
# Sample loading (WAV, OGG, FLAC, MP3)
func load_sample(name: String, path: String) -> bool
    # Loads audio file from Godot resource path into global sample bank.
    # Returns true on success. Overwrites if name already exists.

func load_soundfont(name: String, path: String) -> bool
    # Loads SF2 file. Returns true on success.
    # Extracts samples into sample bank, stores zone metadata.

func clear_samples()
    # Removes all loaded samples. Does not affect running playback.

func clear_soundfonts()
    # Removes all loaded SoundFonts.

func get_loaded_samples() -> Array
    # Returns [{name: String, id: int, frames: int, channels: int, sample_rate: float}, ...]

func get_loaded_soundfonts() -> Array
    # Returns [{name: String, id: int, preset_count: int}, ...]
```

### 4.2 NkidoPlayer — New/Changed

```gdscript
# New property
var source_file: String = ""
    # Path to .akk file (res://, user://, or absolute).
    # When set, compile() reads source from this file.
    # source_file takes priority over source property.

# New method
func get_required_samples() -> Array
    # Returns sample names required by the last compilation.
    # [{name: String, bank: String, variant: int}, ...]
    # Useful for checking which samples need to be loaded.
```

### 4.3 Signals — Unchanged

All existing signals remain. No new signals added.

---

## 5. Implementation Phases

### Phase 4: Sample Loading & SoundFont Support

**Goal:** Load audio samples and SoundFonts, use them in Akkado patterns.

#### 5.1 NkidoEngine Sample API

Add sample loading to the `NkidoEngine` singleton.

**C++ changes to `nkido_engine.h`:**
- Add members: `cedar::SampleBank sample_bank_`, `akkado::SampleRegistry sample_registry_`, `cedar::SoundFontRegistry soundfont_registry_`
- Add decoded audio cache: `std::unordered_map<std::string, cedar::DecodedAudio> decoded_cache_`
- Bind new methods to ClassDB

**C++ changes to `nkido_engine.cpp`:**
- `load_sample()`: Use `godot::FileAccess::get_file_as_bytes()` to read file, call `cedar::AudioDecoder::decode()`, store in SampleBank and cache, register in SampleRegistry
- `load_soundfont()`: Read bytes via FileAccess, call `soundfont_registry_.load_from_memory()` passing `sample_bank_`
- `clear_samples()` / `clear_soundfonts()`: Clear respective storage
- `get_loaded_samples()` / `get_loaded_soundfonts()`: Return metadata arrays
- Expose `sample_bank()`, `sample_registry()`, `soundfont_registry()` accessors for NkidoPlayer

#### 5.2 NkidoPlayer Sample Integration

Modify compilation flow to use samples.

**C++ changes to `nkido_player.cpp`:**
- In `compile()`:
  1. Get NkidoEngine singleton, access its `sample_registry()`
  2. Pass `&sample_registry` to `akkado::compile(code, filename, &sample_registry)`
  3. After compilation, iterate `CompileResult::required_samples_extended`
  4. For each required sample, copy decoded audio from NkidoEngine's cache into `vm_->sample_bank()`
  5. Resolve sample IDs in state_inits (iterate `SequenceSampleMapping`, look up IDs in VM's sample bank, write to event values)
  6. Then apply_state_inits() and load_program() as before

#### 5.3 CMake Changes

No CMake changes needed — Cedar's sample_bank, audio_decoder, and soundfont sources are already compiled into the shared library.

**Files:**

| File | Change |
|------|--------|
| `addons/nkido/src/nkido_engine.h` | Add SampleBank, SampleRegistry, SoundFontRegistry members and methods |
| `addons/nkido/src/nkido_engine.cpp` | Implement load_sample, load_soundfont, clear, get_loaded |
| `addons/nkido/src/nkido_player.cpp` | Pass SampleRegistry to compile, sync samples, resolve IDs |
| `addons/nkido/src/nkido_player.h` | Add get_required_samples declaration |

**Verification:**
1. From GDScript: `NkidoEngine.load_sample("bd", "res://samples/kick.wav")` → returns true
2. `NkidoEngine.get_loaded_samples()` → shows loaded sample metadata
3. Akkado source `pat("bd sd bd sd") |> samp(%, 0.8) |> out(%, %)` → compile → play → hear drum pattern
4. Load SF2: `NkidoEngine.load_soundfont("piano", "res://sf2/piano.sf2")` → use in Akkado → hear piano

---

### Phase 5: `.akk` File Support

**Goal:** Load Akkado source from `.akk` files with editor integration.

#### 5.4 `source_file` Property

**C++ changes to `nkido_player.h/cpp`:**
- Add `String source_file_` member
- Bind `set_source_file` / `get_source_file` with `PROPERTY_HINT_FILE, "*.akk"`
- In `compile()`: if `source_file_` is set, read file via `FileAccess::open()` and use as source
- Pass filename to `akkado::compile()` for diagnostic source locations

**Edge case:** If both `source_file` and `source` are set, `source_file` takes priority. If `source_file` points to a missing file, emit `compilation_finished(false, [{message: "File not found: ..."}])`.

#### 5.5 Inspector File Picker

**GDScript changes to `nkido_inspector.gd`:**
- Show current `source_file` path with a file picker button below the source editor
- When `source_file` is set, show file contents in the CodeEdit (read-only or editable with save-back)
- Add a "Reload" button to re-read the file

#### 5.6 Auto-Recompile on File Change (Editor Only)

**GDScript changes to `nkido_inspector.gd` or `nkido_plugin.gd`:**
- In the editor, use `EditorFileSystem`'s `filesystem_changed` signal or a polling timer
- When the `.akk` file's modification time changes, trigger recompile on the associated NkidoPlayer
- Only active in editor, not at runtime

**Files:**

| File | Change |
|------|--------|
| `addons/nkido/src/nkido_player.h` | Add source_file_ member, accessors |
| `addons/nkido/src/nkido_player.cpp` | File reading in compile(), property binding |
| `addons/nkido/nkido_inspector.gd` | File picker UI, reload button |
| `addons/nkido/nkido_plugin.gd` | File change detection (editor only) |

**Verification:**
1. Set `source_file = "res://audio/test.akk"` in inspector → compile → hear audio
2. Edit `test.akk` externally → editor auto-recompiles → hear updated audio
3. Drag `.akk` file from FileSystem dock onto NkidoPlayer's source_file property
4. Missing file → clear error message in compilation_finished signal

---

### Phase 6: Enhanced Editor Experience

**Goal:** Full-featured bottom panel editor with error markers, autocomplete, and waveform visualization.

#### 5.7 Bottom Panel

**New file `nkido_bottom_panel.gd`:**
- `EditorPlugin.add_control_to_bottom_panel()` from `nkido_plugin.gd`
- Full-width CodeEdit with the same syntax highlighting as the inspector
- Toolbar: Compile, Play, Stop buttons + BPM field + selected player name
- Syncs with currently selected NkidoPlayer node:
  - `EditorPlugin.edit()` callback sets the active player
  - Source changes in bottom panel write back to the player's source property (or source_file)
- Right side panel or collapsible section for parameter controls
- Panel visibility toggles with NkidoPlayer selection

#### 5.8 Error Markers

**Changes to CodeEdit (both inspector and bottom panel):**
- After `compilation_finished(false, errors)`:
  - Clear previous markers
  - For each error with line/column: call `CodeEdit.set_line_as_error(line, true)` (Godot 4.x built-in)
  - Show error tooltip on hover via `CodeEdit` gutter
- On successful compile: clear all error markers

#### 5.9 Autocomplete

**Changes to CodeEdit:**
- Register a code completion provider via `CodeEdit.add_code_completion_option()`
- Keyword sources:
  - DSP builtins from the existing highlighter keyword list (osc, lpf, etc.)
  - Loaded sample names from `NkidoEngine.get_loaded_samples()`
  - Parameter names from `get_param_decls()` (after first compile)
  - Akkado language keywords (param, button, toggle, dropdown, pat, etc.)
- Trigger on typing or Ctrl+Space

#### 5.10 Waveform Visualization

**C++ changes to `nkido_audio_stream_playback.h/cpp`:**
- Add a small circular buffer (e.g., 1024 frames) of recent output, readable from main thread
- `get_waveform_data() -> PackedFloat32Array` — returns recent L/R interleaved samples
- Use `std::atomic` for read/write pointers (main thread reads, audio thread writes)

**GDScript in bottom panel:**
- A custom `Control` node that draws the waveform via `_draw()` + `draw_polyline()`
- Timer-driven refresh (e.g., 30 FPS) calling `get_waveform_data()` and redrawing
- Simple oscilloscope view: time on X, amplitude on Y

#### 5.11 Custom Node Icon

**New file `addons/nkido/icons/NkidoPlayer.svg`:**
- SVG icon following Godot's icon style (16x16, rounded, consistent line weight)
- Audio/synthesis themed (waveform or speaker symbol)
- Register via `_get_plugin_icon()` override in `nkido_plugin.gd`

**Files:**

| File | Change |
|------|--------|
| `addons/nkido/nkido_bottom_panel.gd` | New — bottom panel editor |
| `addons/nkido/nkido_plugin.gd` | Register bottom panel, icon, edit() callback |
| `addons/nkido/nkido_inspector.gd` | Error markers, autocomplete, simplified (transport moves to bottom panel) |
| `addons/nkido/icons/NkidoPlayer.svg` | New — custom node icon |
| `addons/nkido/src/nkido_audio_stream_playback.h` | Waveform buffer, get_waveform_data() |
| `addons/nkido/src/nkido_audio_stream_playback.cpp` | Write to waveform buffer in _mix() |
| `addons/nkido/src/nkido_player.h` | Expose get_waveform_data() to GDScript |
| `addons/nkido/src/nkido_player.cpp` | Bind get_waveform_data() |

**Verification:**
1. Select NkidoPlayer → bottom panel appears with full-width editor
2. Type invalid code → error markers appear on correct lines
3. Type `osc(` → autocomplete suggests oscillator types
4. Play audio → waveform visualization shows real-time output
5. NkidoPlayer shows custom icon in scene tree

---

### Phase 7: Cross-Platform Builds & CI/CD

**Goal:** Build for Linux, Windows, and macOS with automated CI.

#### 5.12 CMake Cross-Platform

**Changes to `CMakeLists.txt`:**
- Platform detection: Linux (GCC/Clang), Windows (MSVC or MinGW), macOS (AppleClang)
- Platform-specific output naming:
  - Linux: `libnkido.linux.template_debug.x86_64.so`
  - Windows: `nkido.windows.template_debug.x86_64.dll`
  - macOS: `libnkido.macos.template_debug.universal.dylib` (arm64 + x86_64 fat binary)
- Platform-specific link flags (e.g., `-framework CoreAudio` on macOS if needed)
- Release build configuration (`CMAKE_BUILD_TYPE=Release` with LTO)

#### 5.13 `.gdextension` Updates

**Changes to `addons/nkido/nkido.gdextension`:**
```ini
[libraries]
linux.debug.x86_64 = "res://addons/nkido/bin/libnkido.linux.template_debug.x86_64.so"
linux.release.x86_64 = "res://addons/nkido/bin/libnkido.linux.template_release.x86_64.so"
windows.debug.x86_64 = "res://addons/nkido/bin/nkido.windows.template_debug.x86_64.dll"
windows.release.x86_64 = "res://addons/nkido/bin/nkido.windows.template_release.x86_64.dll"
macos.debug = "res://addons/nkido/bin/libnkido.macos.template_debug.universal.dylib"
macos.release = "res://addons/nkido/bin/libnkido.macos.template_release.universal.dylib"
```

#### 5.14 GitHub Actions CI

**New file `.github/workflows/build.yml`:**
- Build matrix: `{os: [ubuntu-latest, windows-latest, macos-latest], build_type: [Debug, Release]}`
- Steps: checkout repo + submodules, install dependencies, configure CMake, build, upload artifact
- godot-cpp built as part of CMake (already a dependency)
- Enkido sources compiled in-tree (already in CMakeLists.txt)

#### 5.15 Release Workflow

**New file `.github/workflows/release.yml`:**
- Triggered on tag push (`v*`)
- Runs build matrix for all platforms (Release only)
- Packages `addons/nkido/` directory with all platform binaries
- Creates GitHub Release with the archive attached

**Files:**

| File | Change |
|------|--------|
| `CMakeLists.txt` | Cross-platform detection, output naming, release config |
| `addons/nkido/nkido.gdextension` | Add Windows + macOS library paths |
| `.github/workflows/build.yml` | New — CI build matrix |
| `.github/workflows/release.yml` | New — tagged release workflow |

**Verification:**
1. CI passes on all 3 platforms
2. Download release artifact → drop `addons/nkido/` into a fresh Godot project → NkidoPlayer loads
3. Test on Windows: compile, play, hear audio
4. Test on macOS: compile, play, hear audio

---

## 6. Edge Cases

### Sample Loading
- **Unsupported format:** `load_sample()` returns false, prints error to Godot console. Supported: WAV (PCM 16/24/32, float), OGG Vorbis, FLAC, MP3.
- **Duplicate name:** Overwrites previous sample with same name. Running playback continues using old data until next compile.
- **Missing sample at compile time:** Compilation succeeds but `required_samples` array lists unresolved names. Audio plays silence for missing samples.
- **Large files:** Decoding is synchronous on main thread. Files >10 MB may cause a frame hitch. Async loading can be added in a future version.

### `.akk` Files
- **File not found:** `compile()` returns false, emits error with "File not found: path".
- **Both source and source_file set:** `source_file` takes priority. `source` property is ignored but preserved.
- **Binary/corrupt file:** UTF-8 decode fails → compile error.
- **File changes during playback:** Auto-recompile triggers hot-swap with crossfade (same as manual recompile).

### Cross-Platform
- **macOS universal binary:** Built with `-arch arm64 -arch x86_64`. If one arch fails, the build fails.
- **Windows path separators:** Godot's `FileAccess` handles this transparently with `res://` paths.

---

## 7. Testing Strategy

### Phase 4 Testing (Samples)
1. **WAV loading:** Load mono and stereo WAV files at various sample rates → verify playback pitch is correct
2. **Format support:** Load OGG, FLAC, MP3 → verify all decode correctly
3. **SF2 loading:** Load a General MIDI SF2 → play notes at different pitches → verify zone selection
4. **Pattern samples:** `pat("bd sd hh sd") |> samp(%, 0.8)` → hear correct samples on each step
5. **Missing sample:** Reference unloaded sample → verify silence, no crash
6. **Hot-swap with samples:** Recompile while playing sample pattern → smooth crossfade

### Phase 5 Testing (Files)
1. **File compile:** Set source_file, compile → works identically to inline source
2. **Auto-recompile:** Edit .akk file → save → verify recompile fires in editor
3. **Missing file:** Set source_file to nonexistent path → verify clean error
4. **Priority:** Set both source and source_file → verify source_file wins

### Phase 6 Testing (Editor)
1. **Bottom panel:** Select NkidoPlayer → panel appears. Deselect → panel content clears.
2. **Error markers:** Introduce typo → verify red marker on correct line
3. **Autocomplete:** Type `osc(` → verify suggestions appear
4. **Waveform:** Play audio → verify waveform updates in real-time, stops when audio stops

### Phase 7 Testing (Cross-Platform)
1. **CI:** Push to repo → all matrix jobs pass
2. **Windows:** Open Godot project with addon → NkidoPlayer compiles and plays
3. **macOS:** Same verification on macOS (both Intel and Apple Silicon)
4. **Release:** Tag a version → verify release artifact downloads and works

---

## 8. Open Questions

1. **Sample memory model:** Each NkidoPlayer's VM has its own SampleBank. Samples are copied from NkidoEngine's cache on compile. For large sample sets, this duplicates memory. Is this acceptable for v2, with shared SampleBank as a future optimization?

2. **`.akk` file editing:** Should the bottom panel write changes back to the `.akk` file on disk, or only to the in-memory source? Writing to disk risks conflicting with external editors.

3. **Waveform buffer thread safety:** The proposed approach uses a lock-free ring buffer (audio thread writes, main thread reads). Occasional torn reads would cause visual glitches but no crashes. Is this acceptable?

4. **macOS code signing:** Pre-built dylibs need to be signed or users must disable Gatekeeper. Should the CI pipeline include ad-hoc signing?

5. **godot-cpp version pinning:** Should we pin to a specific godot-cpp commit/tag for reproducible builds, or track a branch?

---

## 9. Future Work (post-v2)

- **Shared SampleBank:** Eliminate per-VM sample copies for memory efficiency
- **Async sample loading:** Background thread decoding for large files
- **Akkado `import` statements:** Cross-file module resolution
- **Spatial audio:** AudioStreamPlayer3D integration for 3D positioning
- **Recording:** Bounce audio output to WAV file
- **Web export:** Emscripten/WASM build target
- **Visual patching:** Node-based alternative to text coding
- **Sample browser:** Inspector panel for auditioning loaded samples
