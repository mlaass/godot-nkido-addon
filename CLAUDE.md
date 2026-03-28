# CLAUDE.md

## Project Overview

**Nkido** is a GDExtension addon that embeds the Enkido audio synthesis system (Akkado compiler + Cedar VM) into Godot 4.6. It provides a `NkidoPlayer` node for real-time audio synthesis from Akkado source code, with parameter binding from GDScript and an editor inspector UI.

The Akkado language and Cedar VM are developed in the sibling repo at `~/workspace/enkido`. This project only contains the Godot integration layer.

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
- `ENKIDO_PATH` — path to enkido repo (default: `../enkido`)

Cedar and Akkado source files are compiled directly into the shared library (not linked as separate static libs). Warning suppression (`-w`) is applied to cedar/akkado sources to avoid noise from godot-cpp's stricter warning flags.

## Project Structure

```
godot-nkido-addon/
├── CMakeLists.txt                    # Build system
├── project.godot                     # Godot project config
├── addons/nkido/
│   ├── nkido.gdextension            # GDExtension manifest
│   ├── plugin.cfg                   # Editor plugin registration
│   ├── nkido_plugin.gd              # @tool EditorPlugin
│   ├── nkido_inspector.gd           # @tool EditorInspectorPlugin (transport, params, syntax highlighting)
│   ├── bin/                         # Compiled .so output (gitignored)
│   └── src/                         # C++ extension sources
│       ├── register_types.cpp/h     # GDExtension entry point, class registration, singleton
│       ├── nkido_engine.cpp/h       # NkidoEngine singleton (global BPM, sample rate)
│       ├── nkido_player.cpp/h       # NkidoPlayer node (compile, play, params, state inits)
│       ├── nkido_audio_stream.cpp/h # AudioStream subclass (creates playback instances)
│       └── nkido_audio_stream_playback.cpp/h  # AudioStreamPlayback (_mix with ring buffer)
├── example/
│   ├── Main.tscn                    # Demo scene
│   └── Main.gd                     # Demo script
└── docs/                            # PRD and design docs
```

## Architecture

```
NkidoEngine (Object singleton)
  - global BPM, sample rate from AudioServer

NkidoPlayer (Node)
  - owns unique_ptr<cedar::VM>
  - compile(): akkado::compile() -> apply_state_inits() -> vm->load_program()
  - creates internal AudioStreamPlayer child (hidden, not serialized)
  │
  └─ NkidoAudioStream (AudioStream, Resource)
       - back-pointer to NkidoPlayer
       - _instantiate_playback() creates NkidoAudioStreamPlayback
       │
       └─ NkidoAudioStreamPlayback (AudioStreamPlayback, RefCounted)
            - 4096-frame ring buffer bridging Cedar's 128-sample blocks to Godot's variable-size _mix() requests
            - calls vm->process_block() to fill ring buffer
            - copies requested frames to output
```

### Threading Model

- **Main thread**: `compile()`, `set_param()`, `trigger_button()`, inspector interactions
- **Audio thread**: `_mix()` -> `process_block()`
- **No mutexes needed** — Cedar provides lock-free APIs:
  - `load_program()`: triple-buffer swap at block boundary
  - `set_param()`: lock-free atomic writes via EnvMap
  - `process_block()`: reads from current program buffer

### Lazy Initialization

`ensure_initialized()` creates the AudioStreamPlayer and wires up the audio stream on first use. This is needed because `_ready()` may not fire in the editor context. Called from `compile()` and `play()`.

### State Init Application

After compilation, `apply_state_inits()` processes the compiler's `StateInitData` vector (copied from the WASM reference pattern at `enkido/web/wasm/enkido_wasm.cpp:675-727`):
- **SequenceProgram**: copies sequence event pointers, calls `vm->init_sequence_program_state()`
- **PolyAlloc**: calls `vm->init_poly_state()`
- **Timeline**: creates `TimelineState`, copies breakpoints

## NkidoPlayer API

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `source` | String | "" | Akkado source code (multiline) |
| `bpm` | float | 120.0 | Per-player BPM (0 = use global) |
| `crossfade_blocks` | int | 3 | Hot-swap crossfade duration (1-10) |
| `autoplay` | bool | false | Compile and play on `_ready()` |
| `bus` | StringName | "Master" | Audio bus (dropdown populated via `_validate_property`) |

### Methods

```gdscript
# Compilation
compile() -> bool           # Compiles source, loads into VM
get_diagnostics() -> Array  # [{line, column, message}, ...]

# Playback
play()                      # Auto-compiles if needed (via inspector)
stop()
pause()                     # Toggles pause state
is_playing() -> bool

# Parameters
set_param(name, value, slew_ms=20.0)
get_param(name) -> float
trigger_button(name)        # Sets to 1.0, auto-releases after 2 frames
get_param_decls() -> Array  # [{name, type, default, min, max, options}, ...]
```

### Signals

```gdscript
compilation_finished(success: bool, errors: Array)
params_changed(params: Array)
playback_started()
playback_stopped()
```

## Inspector Plugin

GDScript-based `EditorInspectorPlugin` (`nkido_inspector.gd`):

- **Transport**: Play (auto-compiles) and Stop buttons
- **Status label**: Shows compile success/error with line numbers
- **Source editor**: `CodeEdit` replacing default `source` property with:
  - Monospaced font (Fira Code preferred) with OpenType `calt`/`clig` ligatures enabled via `FontVariation`
  - `CodeHighlighter` with syntax coloring for DSP builtins, params, patterns, strings, numbers, comments
- **Parameter controls**: Auto-generated from `get_param_decls()` after compilation:
  - `continuous` -> HSlider
  - `button` -> Button (calls `trigger_button()`)
  - `toggle` -> CheckButton
  - `select` -> OptionButton

## Conventions

- Class prefix: `Nkido` (not `Enkido`)
- Addon directory: `addons/nkido/`
- C++ sources in `addons/nkido/src/`
- GDScript inspector/plugin files at `addons/nkido/` root
- All Godot-facing classes in `namespace godot`
- No git commit attribution lines
