# CLAUDE.md

## Project Overview

**Nkido** is a GDExtension addon that embeds the Nkido audio synthesis system (Akkado compiler + Cedar VM) into Godot. It provides `NkidoAudioStream` — a custom `AudioStream` resource for real-time audio synthesis from Akkado source code, with parameter binding from GDScript, sample/soundfont loading, and an editor UI with bottom panel and inspector.

Users attach a `NkidoAudioStream` to a standard `AudioStreamPlayer` node. The Akkado language and Cedar VM are developed in the sibling repo at `~/workspace/nkido`. This project only contains the Godot integration layer.

## Build Commands

```bash
# Configure (first time or after CMake changes)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j$(nproc)

# Output: addons/nkido/bin/libnkido.linux.template_debug.x86_64.so
```

### Dependency paths

CMake variables (override with `-D`):
- `GODOT_CPP_PATH` — path to godot-cpp (default: `../godot-cpp`)
- `NKIDO_PATH` — path to nkido repo (default: `../nkido`)

Cedar and Akkado source files are compiled directly into the shared library (not linked as separate static libs). Warning suppression (`-w`) is applied to cedar/akkado sources to avoid noise from godot-cpp's stricter warning flags.

## Project Structure

```
godot-nkido-addon/
├── CMakeLists.txt                    # Build system (cross-platform)
├── project.godot                     # Godot project config
├── addons/nkido/
│   ├── nkido.gdextension            # GDExtension manifest (Linux/Windows/macOS)
│   ├── plugin.cfg                   # Editor plugin registration
│   ├── nkido_plugin.gd              # @tool EditorPlugin (inspector + bottom panel)
│   ├── nkido_inspector.gd           # @tool EditorInspectorPlugin (transport, params, code editor)
│   ├── nkido_bottom_panel.gd        # @tool bottom panel (full editor, waveform, params)
│   ├── bin/                         # Compiled binaries (gitignored)
│   └── src/                         # C++ extension sources
│       ├── register_types.cpp/h     # GDExtension entry point, class registration
│       ├── nkido_audio_stream.cpp/h # NkidoAudioStream (VM, compile, params, samples, soundfonts)
│       └── nkido_audio_stream_playback.cpp/h  # AudioStreamPlayback (_mix, ring buffer, waveform)
├── example/
│   ├── Main.tscn                    # Demo scene (AudioStreamPlayer + NkidoAudioStream)
│   └── Main.gd                     # Demo script
├── docs/                            # PRD and design docs
└── .github/workflows/               # CI/CD (build.yml, release.yml)
```

## Architecture

```
AudioStreamPlayer (standard Godot node)
  │
  └─ NkidoAudioStream (AudioStream, Resource)
       - owns unique_ptr<cedar::VM>
       - compile(): akkado::compile() -> resolve_sample_ids() -> apply_state_inits() -> vm->load_program()
       - sample/soundfont loading via cedar::SampleBank and cedar::SoundFontRegistry
       - source_file: optional .akk file path (file takes priority over inline source)
       │
       └─ NkidoAudioStreamPlayback (AudioStreamPlayback, RefCounted)
            - 4096-frame ring buffer bridging Cedar's 128-sample blocks to Godot's variable-size _mix() requests
            - calls vm->process_block() to fill ring buffer
            - copies requested frames to output
            - lock-free waveform buffer for visualization
```

### Threading Model

- **Main thread**: `compile()`, `set_param()`, `trigger_button()`, `load_sample()`, inspector interactions
- **Audio thread**: `_mix()` -> `process_block()`, button release processing
- **No mutexes needed** — Cedar provides lock-free APIs:
  - `load_program()`: triple-buffer swap at block boundary
  - `set_param()`: lock-free atomic writes via EnvMap
  - `process_block()`: reads from current program buffer

### State Init Application

After compilation, `apply_state_inits()` processes the compiler's `StateInitData` vector:
- **SequenceProgram**: copies sequence event pointers, calls `vm->init_sequence_program_state()`
- **PolyAlloc**: calls `vm->init_poly_state()`
- **Timeline**: creates `TimelineState`, copies breakpoints

### Sample Resolution

`resolve_sample_ids()` maps sample names in pattern events to SampleBank IDs before state init. Uses `sequence_sample_mappings` from `CompileResult` — each mapping specifies (seq_idx, event_idx, sample_name, bank, variant) and the resolved ID is written into `events[event_idx].values[0]`.

## NkidoAudioStream API

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `source` | String | "" | Akkado source code (multiline) |
| `source_file` | String | "" | Path to .akk file (takes priority over `source`) |
| `bpm` | float | 120.0 | BPM for this stream |
| `crossfade_blocks` | int | 3 | Hot-swap crossfade duration (1-10) |

### Methods

```gdscript
# Compilation
compile() -> bool                    # Compiles source, loads into VM
get_diagnostics() -> Array           # [{line, column, message}, ...]
is_compiled() -> bool

# Sample loading
load_sample(name, path) -> bool      # Load audio file (WAV/OGG/FLAC/MP3) into SampleBank
load_soundfont(name, path) -> bool   # Load SF2 file into SoundFontRegistry
clear_samples()
clear_soundfonts()
get_loaded_samples() -> Array        # [{name, id, frames, channels, sample_rate}, ...]
get_loaded_soundfonts() -> Array     # [{id, preset_count}, ...]
get_required_samples() -> Array      # [{name, bank, variant}, ...] from last compile

# Parameters
set_param(name, value, slew_ms=20.0)
get_param(name) -> float
trigger_button(name)                 # Sets to 1.0, auto-releases after 2 blocks
get_param_decls() -> Array           # [{name, type, default, min, max, options}, ...]

# Visualization
get_waveform_data() -> PackedFloat32Array  # 1024 frames, L/R interleaved
```

### Signals

```gdscript
compilation_finished(success: bool, errors: Array)
params_changed(params: Array)
```

## Editor Plugin

### Inspector (`nkido_inspector.gd`)
- Activates for `AudioStreamPlayer` nodes with a `NkidoAudioStream` stream
- **Transport**: Play (auto-compiles) and Stop buttons
- **Status label**: Shows compile success/error with line numbers
- **Source editor**: `CodeEdit` with syntax highlighting, error gutter markers
- **File support**: When `source_file` is set, shows filename + Reload button
- **Parameter controls**: Auto-generated from `get_param_decls()` after compilation

### Bottom Panel (`nkido_bottom_panel.gd`)
- Full-width code editor with syntax highlighting and error markers
- Toolbar: Compile, Play, Stop, BPM spinner
- Parameters panel (sliders, buttons, toggles, dropdowns)
- Waveform visualization (~30 FPS)

### Plugin (`nkido_plugin.gd`)
- Registers inspector plugin and bottom panel
- `_handles()`: activates for AudioStreamPlayer with NkidoAudioStream
- `_edit()`: sets player on bottom panel, makes it visible

## Conventions

- Class prefix: `Nkido`
- Addon directory: `addons/nkido/`
- C++ sources in `addons/nkido/src/`
- GDScript inspector/plugin files at `addons/nkido/` root
- All Godot-facing classes in `namespace godot`
- No git commit attribution lines
