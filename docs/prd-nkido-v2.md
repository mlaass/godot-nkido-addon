# PRD: Nkido v2 — Samples, Files, Editor, Cross-Platform

> **Status: DRAFT**

**Date:** 2026-03-28 (updated 2026-03-29)
**Prerequisites:**
- v1 MVP (Phases 1–3) is complete
- AudioStream architecture refactor is complete (see `prd-audio-stream-refactor.md`)

---

## 1. Overview

### 1.1 Context

The v1 MVP delivers a working Akkado synthesis pipeline in Godot. The AudioStream refactor restructures the architecture so that NkidoAudioStream is the sole user-facing class — a proper AudioStream Resource owning the Cedar VM, compilation state, and parameters. Users assign it to standard AudioStreamPlayer nodes. NkidoPlayer and NkidoEngine have been removed.

Post-refactor architecture:
- **NkidoAudioStream** (Resource) — owns VM, source, compilation, parameters, signals
- **NkidoAudioStreamPlayback** — self-contained mixer with `Ref<NkidoAudioStream>`
- **Standard AudioStreamPlayer** — Godot's built-in node for playback, bus routing, autoplay

However, the system is synthesis-only (no samples), inline-source-only (no files), single-platform (Linux), and the editor is limited to the inspector panel width.

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
- Global BPM singleton (each stream has its own BPM)
- Shared SampleBank across streams (each VM owns its samples independently)

---

## 2. User Experience

### 2.1 Sample-Based Drum Pattern

```gdscript
extends Node2D

func _ready():
    var drums: NkidoAudioStream = $Drums.stream  # $Drums is AudioStreamPlayer
    drums.load_sample("bd", "res://samples/kick.wav")
    drums.load_sample("sd", "res://samples/snare.wav")
    drums.load_sample("hh", "res://samples/hihat.wav")

    drums.source = '''
        velocity = param("velocity", 0.8, 0, 1)
        pat("bd ~ sd ~ bd bd sd ~") |> samp(%, velocity) |> out(%, %)
        pat("hh*8") |> samp(%, velocity * 0.5) |> out(%, %)
    '''
    drums.compile()
    $Drums.play()
```

### 2.2 SoundFont Instrument

```gdscript
extends Node2D

func _ready():
    var keys: NkidoAudioStream = $Keys.stream  # $Keys is AudioStreamPlayer
    keys.load_soundfont("piano", "res://soundfonts/grand_piano.sf2")

    keys.source = '''
        pat("[c4, e4, g4] [d4, f4, a4] [e4, g4, b4]")
        |> sf("piano", 0, %, 0.8)
        |> out(%, %)
    '''
    keys.compile()
    $Keys.play()
```

### 2.3 `.akk` File Workflow

```
res://audio/
├── ambient_forest.akk
├── combat_music.akk
└── ui_sounds.akk
```

```gdscript
# In the inspector: set source_file on the NkidoAudioStream resource
# Or from script:
var music: NkidoAudioStream = $Music.stream
music.source_file = "res://audio/combat_music.akk"
music.compile()  # Reads from file
$Music.play()
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

### 3.1 Post-Refactor Class Diagram (v2 additions marked)

```
NkidoAudioStream (AudioStream, Resource)
  - source_: String
  - source_file_: String                      [NEW — Phase 5]
  - vm_: unique_ptr<cedar::VM>
  - compiled_: bool
  - bpm_: float
  - crossfade_blocks_: int
  - param_decls_: vector<ParamDecl>
  - last_compile_result_: CompileResult
  - pending_button_releases_: map<string, int>
  │
  + load_sample(name, path) -> bool           [NEW — Phase 4]
  + load_soundfont(name, path) -> bool        [NEW — Phase 4]
  + clear_samples()                           [NEW — Phase 4]
  + clear_soundfonts()                        [NEW — Phase 4]
  + get_loaded_samples() -> Array             [NEW — Phase 4]
  + get_loaded_soundfonts() -> Array          [NEW — Phase 4]
  + get_required_samples() -> Array           [NEW — Phase 4]
  │
  + compile() -> bool
  + set_param(name, value, slew_ms)
  + get_param(name) -> float
  + trigger_button(name)
  + get_param_decls() -> Array
  + get_diagnostics() -> Array
  + is_compiled() -> bool
  + get_waveform_data() -> PackedFloat32Array [NEW — Phase 6]
  │
  signals:
  + compilation_finished(success, errors)
  + params_changed(params)

NkidoAudioStreamPlayback (AudioStreamPlayback, RefCounted)
  - stream_: Ref<NkidoAudioStream>
  - ring_buffer_[4096]
  - temp_left_/right_[128]
  - waveform_buffer_[1024]                    [NEW — Phase 6]
  │
  + _mix() → stream_->get_vm()->process_block()
```

### 3.2 Sample Loading Flow

```
stream.load_sample("bd", "res://samples/kick.wav")
  │
  ├─ Godot FileAccess reads file bytes
  ├─ cedar::AudioDecoder::decode() → DecodedAudio
  └─ vm_->sample_bank().load_sample(name, data, frames, channels, rate) → id

stream.compile()
  │
  ├─ Read source (from source_file or source property)
  ├─ Build akkado::SampleRegistry from vm_->sample_bank() names
  ├─ akkado::compile(code, filename, &sample_registry)
  │     └─ Compiler resolves sample("bd") to ID at compile time
  ├─ Resolve sample IDs in state_inits (iterate SequenceSampleMapping,
  │     look up IDs in vm_->sample_bank(), write to event values)
  ├─ apply_state_inits()
  └─ vm_->load_program(bytecode)
```

Note: Samples are loaded directly into the VM's SampleBank. No intermediate cache or copy step — each stream owns its samples independently.

### 3.3 SoundFont Loading Flow

```
stream.load_soundfont("piano", "res://soundfonts/grand.sf2")
  │
  ├─ Godot FileAccess reads file bytes
  └─ vm_->soundfont_registry().load_from_memory(data, size, name, vm_->sample_bank())
        ├─ Parses SF2 via TinySoundFont
        ├─ Extracts samples → VM's SampleBank (auto-assigned IDs)
        └─ Stores zone metadata, envelopes, presets

On compile(), sf() references in Akkado resolve via vm_->soundfont_registry()
```

### 3.4 `.akk` File Resolution

```
stream.source_file = "res://audio/combat_music.akk"

compile():
  ├─ If source_file is set:
  │   ├─ Open via Godot FileAccess (supports res://, user://)
  │   ├─ Read UTF-8 text → use as source code
  │   └─ Pass filename to akkado::compile for diagnostics
  └─ Else: use source property as before
```

---

## 4. API Reference

### 4.1 NkidoAudioStream — New Methods

```gdscript
# Sample loading (WAV, OGG, FLAC, MP3)
func load_sample(name: String, path: String) -> bool
    # Loads audio file from Godot resource path into this stream's VM SampleBank.
    # Returns true on success. Overwrites if name already exists.

func load_soundfont(name: String, path: String) -> bool
    # Loads SF2 file into this stream's VM SoundFontRegistry.
    # Returns true on success.
    # Extracts samples into VM's SampleBank, stores zone metadata.

func clear_samples()
    # Removes all loaded samples from this stream's VM. Does not affect running playback.

func clear_soundfonts()
    # Removes all loaded SoundFonts from this stream's VM.

func get_loaded_samples() -> Array
    # Returns [{name: String, id: int, frames: int, channels: int, sample_rate: float}, ...]

func get_loaded_soundfonts() -> Array
    # Returns [{name: String, id: int, preset_count: int}, ...]

func get_required_samples() -> Array
    # Returns sample names required by the last compilation.
    # [{name: String, bank: String, variant: int}, ...]
    # Useful for checking which samples need to be loaded.
```

### 4.2 NkidoAudioStream — New Properties

```gdscript
var source_file: String = ""
    # Path to .akk file (res://, user://, or absolute).
    # When set, compile() reads source from this file.
    # source_file takes priority over source property.
```

### 4.3 NkidoAudioStream — Waveform

```gdscript
func get_waveform_data() -> PackedFloat32Array
    # Returns recent L/R interleaved samples from the playback's waveform buffer.
    # For visualization only. May contain occasional torn reads (visual glitch, no crash).
```

### 4.4 Signals — Unchanged

All existing signals remain. No new signals added.

---

## 5. Phase 4: Sample Loading & SoundFont Support

**Goal:** Load audio samples and SoundFonts, use them in Akkado patterns.

### 5.1 Sample Loading API

Add sample loading methods to NkidoAudioStream.

**C++ changes to `nkido_audio_stream.h`:**
- Bind new methods: `load_sample`, `load_soundfont`, `clear_samples`, `clear_soundfonts`, `get_loaded_samples`, `get_loaded_soundfonts`, `get_required_samples`
- No new member variables needed — samples live in `vm_->sample_bank()` and SoundFonts in `vm_->soundfont_registry()`

**C++ changes to `nkido_audio_stream.cpp`:**
- `load_sample()`: Use `godot::FileAccess::get_file_as_bytes()` to read file, call `cedar::AudioDecoder::decode()`, load decoded PCM into `vm_->sample_bank().load_sample()`
- `load_soundfont()`: Read bytes via FileAccess, call `vm_->soundfont_registry().load_from_memory()` passing `vm_->sample_bank()`
- `clear_samples()` / `clear_soundfonts()`: Clear respective storage on the VM
- `get_loaded_samples()` / `get_loaded_soundfonts()`: Query VM's sample bank / soundfont registry for metadata
- `get_required_samples()`: Return `last_compile_result_.required_samples_extended`

### 5.2 Compilation with Samples

Modify the existing `compile()` method to use samples.

**C++ changes to `nkido_audio_stream.cpp` `compile()`:**
1. Read source (from `source_file_` or `source_` property)
2. Build `akkado::SampleRegistry` from `vm_->sample_bank()` (iterate loaded sample names → IDs)
3. Call `akkado::compile(code, filename, &sample_registry)` — compiler resolves sample names to IDs
4. Iterate `CompileResult::state_inits` for SequenceProgram entries with `is_sample_pattern == true`:
   - For each `SequenceSampleMapping`, look up sample IDs in `vm_->sample_bank()`, write to event values
5. `apply_state_inits()` and `vm_->load_program()` as before

### 5.3 CMake Changes

No CMake changes needed — Cedar's sample_bank, audio_decoder, and soundfont sources are already compiled into the shared library.

**Files:**

| File | Change |
|------|--------|
| `addons/nkido/src/nkido_audio_stream.h` | Add sample/soundfont method declarations |
| `addons/nkido/src/nkido_audio_stream.cpp` | Implement load_sample, load_soundfont, clear, get_loaded, get_required_samples; update compile() for sample registry |

**Verification:**
1. From GDScript: `$Player.stream.load_sample("bd", "res://samples/kick.wav")` → returns true
2. `$Player.stream.get_loaded_samples()` → shows loaded sample metadata
3. Akkado source `pat("bd sd bd sd") |> samp(%, 0.8) |> out(%, %)` → compile → play → hear drum pattern
4. Load SF2: `$Player.stream.load_soundfont("piano", "res://sf2/piano.sf2")` → use in Akkado → hear piano

---

## 6. Phase 5: `.akk` File Support

**Goal:** Load Akkado source from `.akk` files with editor integration.

### 6.1 `source_file` Property

**C++ changes to `nkido_audio_stream.h/cpp`:**
- Add `String source_file_` member
- Bind `set_source_file` / `get_source_file` with `PROPERTY_HINT_FILE, "*.akk"`
- In `compile()`: if `source_file_` is set, read file via `FileAccess::open()` and use as source
- Pass filename to `akkado::compile()` for diagnostic source locations

**Autoplay interaction:** When AudioStreamPlayer has `autoplay=true` and the stream's `source_file` is set, `_instantiate_playback()` can auto-compile if not already compiled. If the file is missing, emit `compilation_finished(false, [{message: "File not found: <path>"}])`.

**Edge case:** If both `source_file` and `source` are set, `source_file` takes priority. If `source_file` points to a missing file, emit `compilation_finished(false, [{message: "File not found: ..."}])`.

### 6.2 Inspector File Picker

**GDScript changes to `nkido_inspector.gd`:**
- Show current `source_file` path with a file picker button below the source editor
- When `source_file` is set, show file contents in the CodeEdit (editable, writes back to disk on change)
- Add a "Reload" button to re-read the file

### 6.3 Auto-Recompile on File Change (Editor Only)

**GDScript changes to `nkido_inspector.gd` or `nkido_plugin.gd`:**
- In the editor, connect to `EditorFileSystem.filesystem_changed` signal
- When the `.akk` file's modification time changes, trigger recompile on the associated NkidoAudioStream
- Only active in editor, not at runtime

**Files:**

| File | Change |
|------|--------|
| `addons/nkido/src/nkido_audio_stream.h` | Add source_file_ member, accessors |
| `addons/nkido/src/nkido_audio_stream.cpp` | File reading in compile(), property binding |
| `addons/nkido/nkido_inspector.gd` | File picker UI, reload button, write-back to disk |
| `addons/nkido/nkido_plugin.gd` | File change detection via EditorFileSystem (editor only) |

**Verification:**
1. Set `source_file` on NkidoAudioStream resource in inspector → compile → hear audio
2. Edit `test.akk` externally → editor auto-recompiles → hear updated audio
3. Edit in bottom panel → changes written back to `.akk` file on disk
4. Missing file → clear error message in compilation_finished signal
5. Set AudioStreamPlayer `autoplay = true` + stream's `source_file` → play scene → audio starts automatically

---

## 7. Phase 6: Enhanced Editor Experience

**Goal:** Full-featured bottom panel editor with error markers, autocomplete, and waveform visualization.

### 7.1 Bottom Panel

**New file `nkido_bottom_panel.gd`:**

Extends `VBoxContainer`. Managed by `nkido_plugin.gd` via `add_control_to_bottom_panel()`.

**Class structure:**
- `active_stream: NkidoAudioStream` — currently bound stream
- `active_player: AudioStreamPlayer` — the AudioStreamPlayer node owning the stream
- `code_edit: CodeEdit` — full-width source editor with same syntax highlighting as inspector
- `toolbar: HBoxContainer` — Compile, Play, Stop buttons + BPM SpinBox + stream name label
- `params_panel: VBoxContainer` — auto-generated parameter controls (left side of split)
- `waveform_view: Control` — real-time waveform display (right side of split)

**Signal connections:**
- `NkidoPlugin._edit(object)` → calls `bottom_panel.set_player(player)` when an AudioStreamPlayer with NkidoAudioStream is selected
- `NkidoPlugin._handles(object)` → returns true for AudioStreamPlayer instances whose stream is NkidoAudioStream
- `code_edit.text_changed` → writes back to `active_stream.source` (or to `source_file` on disk if set)
- `active_stream.compilation_finished` → updates error markers and parameter controls
- `active_stream.params_changed` → rebuilds parameter UI
- Play/Stop buttons → call `active_player.play()` / `active_player.stop()`

**Lifecycle:**
- AudioStreamPlayer selected in scene tree → `_edit()` fires → bottom panel binds to stream, loads source, shows panel
- Node deselected → bottom panel unbinds, clears source and parameters
- Multiple rapid selections → debounce via `set_deferred()` to avoid partial state

**Source sync model:** Both the inspector and bottom panel can edit the stream's source. Changes propagate through the NkidoAudioStream's `source` property — last write wins. When `source_file` is set, both editors write changes back to the `.akk` file on disk.

**Panel visibility:** Toggles with AudioStreamPlayer selection via `make_bottom_panel_item_visible()`.

### 7.2 Error Markers

**Changes to CodeEdit (both inspector and bottom panel):**
- Add a custom gutter for error indicators via `CodeEdit.add_gutter()`
- After `compilation_finished(false, errors)`:
  - Clear previous gutter icons
  - For each error with line/column: call `set_line_gutter_icon(line, gutter_idx, error_icon)` and `set_line_gutter_color(line, gutter_idx, Color.RED)`
  - Show error tooltip on hover via gutter click callback
- On successful compile: clear all gutter error markers

### 7.3 Autocomplete

**Changes to CodeEdit:**
- Register a code completion provider via `CodeEdit.add_code_completion_option()`
- Keyword sources:
  - DSP builtins from the existing highlighter keyword list (osc, lpf, etc.)
  - Loaded sample names from `stream.get_loaded_samples()`
  - Parameter names from `get_param_decls()` (after first compile)
  - Akkado language keywords (param, button, toggle, dropdown, pat, etc.)
- Trigger on typing or Ctrl+Space

### 7.4 Waveform Visualization

**C++ changes to `nkido_audio_stream_playback.h/cpp`:**
- Add a small circular buffer (e.g., 1024 frames) of recent output, readable from main thread
- Use `std::atomic` for read/write pointers (main thread reads, audio thread writes)

**C++ changes to `nkido_audio_stream.h/cpp`:**
- `get_waveform_data() -> PackedFloat32Array` — queries the current playback's waveform buffer, returns recent L/R interleaved samples

**GDScript in bottom panel:**
- A custom `Control` node that draws the waveform via `_draw()` + `draw_polyline()`
- Timer-driven refresh (e.g., 30 FPS) calling `stream.get_waveform_data()` and redrawing
- Simple oscilloscope view: time on X, amplitude on Y

### 7.5 Custom Inspector for AudioStreamPlayer

**Changes to `nkido_inspector.gd`:**
- `_can_handle(object)` → returns true when object is an AudioStreamPlayer and `object.stream is NkidoAudioStream`
- Presents NkidoAudioStream controls (source editor, transport, parameters) at the top level of the AudioStreamPlayer inspector, not nested inside the resource
- Play/Stop buttons delegate to the AudioStreamPlayer node

**Files:**

| File | Change |
|------|--------|
| `addons/nkido/nkido_bottom_panel.gd` | New — bottom panel editor |
| `addons/nkido/nkido_plugin.gd` | Register bottom panel, _edit()/_handles() for AudioStreamPlayer with NkidoAudioStream |
| `addons/nkido/nkido_inspector.gd` | Error markers via gutter API, autocomplete, custom AudioStreamPlayer handling |
| `addons/nkido/nkido.gdextension` | Add `[icons]` section if applicable |
| `addons/nkido/src/nkido_audio_stream_playback.h` | Waveform buffer, atomic read/write pointers |
| `addons/nkido/src/nkido_audio_stream_playback.cpp` | Write to waveform buffer in _mix() |
| `addons/nkido/src/nkido_audio_stream.h` | get_waveform_data() declaration |
| `addons/nkido/src/nkido_audio_stream.cpp` | get_waveform_data() implementation (queries playback) |

**Verification:**
1. Select AudioStreamPlayer with NkidoAudioStream → bottom panel appears with full-width editor
2. Type invalid code → error markers appear on correct lines (gutter icons)
3. Type `osc(` → autocomplete suggests oscillator types
4. Play audio → waveform visualization shows real-time output
5. Inspector shows NkidoAudioStream controls at top level (not nested in resource)

---

## 8. Phase 7: Cross-Platform Builds & CI/CD

**Goal:** Build for Linux, Windows, and macOS with automated CI.

### 8.1 CMake Cross-Platform

**Changes to `CMakeLists.txt`:**
- Platform detection: Linux (GCC/Clang), Windows (MSVC or MinGW), macOS (AppleClang)
- Platform-specific output naming:
  - Linux: `libnkido.linux.template_debug.x86_64.so`
  - Windows: `nkido.windows.template_debug.x86_64.dll`
  - macOS: `libnkido.macos.template_debug.universal.dylib` (arm64 + x86_64 fat binary)
- Platform-specific link flags (e.g., `-framework CoreAudio` on macOS if needed)
- Release build configuration (`CMAKE_BUILD_TYPE=Release` with LTO)

### 8.2 `.gdextension` Updates

**Changes to `addons/nkido/nkido.gdextension`:**
```ini
[configuration]
entry_symbol = "nkido_library_init"
compatibility_minimum = "4.6"

[libraries]
linux.debug.x86_64 = "res://addons/nkido/bin/libnkido.linux.template_debug.x86_64.so"
linux.release.x86_64 = "res://addons/nkido/bin/libnkido.linux.template_release.x86_64.so"
windows.debug.x86_64 = "res://addons/nkido/bin/nkido.windows.template_debug.x86_64.dll"
windows.release.x86_64 = "res://addons/nkido/bin/nkido.windows.template_release.x86_64.dll"
macos.debug = "res://addons/nkido/bin/libnkido.macos.template_debug.universal.dylib"
macos.release = "res://addons/nkido/bin/libnkido.macos.template_release.universal.dylib"
```

### 8.3 GitHub Actions CI

**New file `.github/workflows/build.yml`:**
- Build matrix: `{os: [ubuntu-latest, windows-latest, macos-latest], build_type: [Debug, Release]}`
- Steps: checkout repo + submodules, install dependencies, configure CMake, build, upload artifact
- godot-cpp built as part of CMake (already a dependency)
- Nkido sources compiled in-tree (already in CMakeLists.txt)
- CI is build-only for v2 — no runtime or headless load tests

### 8.4 Release Workflow

**New file `.github/workflows/release.yml`:**
- Triggered on tag push (`v*`)
- Runs build matrix for all platforms (Release only)
- Packages `addons/nkido/` directory with all platform binaries
- Creates GitHub Release with the archive attached

**Files:**

| File | Change |
|------|--------|
| `CMakeLists.txt` | Cross-platform detection, output naming, release config |
| `addons/nkido/nkido.gdextension` | Update compatibility_minimum to 4.6, add Windows + macOS library paths |
| `.github/workflows/build.yml` | New — CI build matrix (compile-only) |
| `.github/workflows/release.yml` | New — tagged release workflow |

**Verification:**
1. CI passes (compiles successfully) on all 3 platforms
2. Download release artifact → drop `addons/nkido/` into a fresh Godot project → NkidoAudioStream loads
3. Test on Windows: compile, play, hear audio
4. Test on macOS: compile, play, hear audio

---

## 9. Edge Cases

### Sample Loading
- **Unsupported format:** `load_sample()` returns false, prints error to Godot console. Supported: WAV (PCM 16/24/32, float), OGG Vorbis, FLAC, MP3.
- **Duplicate name:** Overwrites previous sample with same name. Running playback continues using old data until next compile.
- **Missing sample at compile time:** Compilation succeeds but `required_samples` array lists unresolved names. Audio plays silence for missing samples.
- **Large files:** Decoding is synchronous on main thread. Files >10 MB may cause a frame hitch. Async loading can be added in a future version.
- **Per-stream memory:** Each NkidoAudioStream loads samples into its own VM SampleBank independently. If multiple streams use the same samples, the audio data is duplicated in memory. Acceptable for v2 — shared SampleBank is future work.

### `.akk` Files
- **File not found:** `compile()` returns false, emits error with "File not found: path".
- **Both source and source_file set:** `source_file` takes priority. `source` property is ignored but preserved.
- **Binary/corrupt file:** UTF-8 decode fails → compile error.
- **File changes during playback:** Auto-recompile triggers hot-swap with crossfade (same as manual recompile).
- **Write-back conflicts:** Bottom panel writes changes to `.akk` files on disk. If an external editor modifies the file simultaneously, last write wins. The auto-recompile watcher detects the external change and reloads.

### Editor
- **Rapid AudioStreamPlayer selection switching:** Bottom panel uses `set_deferred()` to debounce binding changes. Intermediate selections are discarded — only the final selected player is bound.
- **Large source files in CodeEdit:** CodeEdit handles large text natively. Syntax highlighting and autocomplete operate on visible lines only — no performance concern for typical Akkado files (<500 lines).
- **Panel resize:** Bottom panel contents use `size_flags_horizontal = SIZE_EXPAND_FILL` so CodeEdit and parameter controls resize proportionally. Waveform view has a minimum width to remain readable.
- **AudioStreamPlayer without NkidoAudioStream:** Inspector and bottom panel only activate when the selected AudioStreamPlayer's stream is a NkidoAudioStream instance. Other stream types are ignored.

### Cross-Platform
- **macOS universal binary:** Built with `-arch arm64 -arch x86_64`. If one arch fails, the build fails.
- **Windows path separators:** Godot's `FileAccess` handles this transparently with `res://` paths.

---

## 10. Testing Strategy

### Phase 4 Testing (Samples)
1. **WAV loading:** Load mono and stereo WAV files at various sample rates → verify playback pitch is correct
2. **Format support:** Load OGG, FLAC, MP3 → verify all decode correctly
3. **SF2 loading:** Load a General MIDI SF2 → play notes at different pitches → verify zone selection
4. **Pattern samples:** `pat("bd sd hh sd") |> samp(%, 0.8)` → hear correct samples on each step
5. **Missing sample:** Reference unloaded sample → verify silence, no crash
6. **Hot-swap with samples:** Recompile while playing sample pattern → smooth crossfade

### Phase 5 Testing (Files)
1. **File compile:** Set source_file on stream, compile → works identically to inline source
2. **Auto-recompile:** Edit .akk file → save → verify recompile fires in editor
3. **Write-back:** Edit in bottom panel → verify .akk file on disk is updated
4. **Missing file:** Set source_file to nonexistent path → verify clean error
5. **Priority:** Set both source and source_file → verify source_file wins
6. **Autoplay + file:** Set AudioStreamPlayer `autoplay=true` and stream's `source_file` → play scene → audio starts automatically

### Phase 6 Testing (Editor)
1. **Bottom panel:** Select AudioStreamPlayer with NkidoAudioStream → panel appears. Deselect → panel content clears.
2. **Error markers:** Introduce typo → verify red gutter icon on correct line
3. **Autocomplete:** Type `osc(` → verify suggestions appear
4. **Waveform:** Play audio → verify waveform updates in real-time, stops when audio stops
5. **Rapid selection:** Click between multiple AudioStreamPlayers quickly → panel settles on last selected
6. **Inspector:** NkidoAudioStream controls appear at top level of AudioStreamPlayer inspector (not nested)

### Phase 7 Testing (Cross-Platform)
1. **CI:** Push to repo → all matrix jobs compile successfully
2. **Windows:** Open Godot project with addon → NkidoAudioStream compiles and plays
3. **macOS:** Same verification on macOS (both Intel and Apple Silicon)
4. **Release:** Tag a version → verify release artifact downloads and works

---

## 11. Resolved Decisions

1. **VM ownership model:** VM-per-stream. Each NkidoAudioStream owns its own Cedar VM with independent SampleBank, SoundFontRegistry, BPM, and parameters. No shared state between streams. Rationale: matches Cedar's architecture (one VM = one program), zero Cedar changes needed, natural Godot AudioStream model, failure isolation.

2. **Architecture ordering:** AudioStream refactor (see `prd-audio-stream-refactor.md`) is completed as a prerequisite before v2 work begins. This avoids implementing v2 features against the old architecture and then rewriting for the refactored one.

3. **NkidoPlayer removed:** Users use standard AudioStreamPlayer nodes with NkidoAudioStream resources. No convenience wrapper. Rationale: simplicity, Godot idiom, less API surface.

4. **NkidoEngine removed:** No global singleton. BPM is per-stream. Sample rate read from AudioServer directly. Rationale: everything is per-stream, no shared mutable state.

5. **SoundFont loading:** SF2 files are loaded directly into each VM's SoundFontRegistry (not through a global registry). No intermediate caching or copy step.

6. **SampleRegistry scope:** Built per-compile from the VM's SampleBank names. Not a global registry. Each stream resolves sample names against its own loaded samples.

7. **`.akk` file editing:** Bottom panel writes changes back to `.akk` files on disk, matching Godot's convention for resource editing (shaders, scripts).

---

## 12. Open Questions

1. **Waveform buffer thread safety:** The proposed approach uses a lock-free ring buffer (audio thread writes, main thread reads). Occasional torn reads would cause visual glitches but no crashes. Is this acceptable?

2. **macOS code signing:** Pre-built dylibs need to be signed or users must disable Gatekeeper. Should the CI pipeline include ad-hoc signing?

3. **godot-cpp version pinning:** Should we pin to a specific godot-cpp commit/tag for reproducible builds, or track a branch?

4. **Auto-compile on playback start:** When AudioStreamPlayer starts playback (autoplay or `play()`) and the NkidoAudioStream is not yet compiled, should `_instantiate_playback()` trigger auto-compilation? Or should users always call `compile()` explicitly?

---

## 13. Future Work (post-v2)

- **Shared SampleBank:** Reference-counted sample storage shared across VMs for memory efficiency
- **Async sample loading:** Background thread decoding for large files
- **Akkado `import` statements:** Cross-file module resolution
- **Spatial audio:** AudioStreamPlayer3D integration for 3D positioning
- **Recording:** Bounce audio output to WAV file
- **Web export:** Emscripten/WASM build target
- **Visual patching:** Node-based alternative to text coding
- **Sample browser:** Inspector panel for auditioning loaded samples
- **ResourceFormatLoader for `.akk`:** Load `.akk` files as NkidoAudioStream resources via Godot's ResourceLoader
- **Global BPM utility:** Optional autoload script for synchronized tempo across streams
