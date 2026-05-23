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

### Program Loading

The compiler emits `[main | block bodies]` bytecode with a `block_table`. `compile()` uses `vm->load_program_with_blocks(bytecode, block_table, main_instruction_count)` so FOREACH_EVENT subprograms, `loop(N){…}`, `when()`, and shared `fn` `BLOCK_CALL` dispatch work.

### State Init Application

`apply_state_inits()` processes the compiler's `StateInitData` vector. All six types are handled:
- **SequenceProgram**: copies sequence event pointers, calls `vm->init_sequence_program_state()`. If `iter_n > 0`, follows with `init_sequence_iter_state()`.
- **PolyAlloc**: calls `vm->init_poly_state()` with `release_seconds`, `prop_count`, `prop_defaults` (custom record-suffix props per voice).
- **Timeline**: creates `TimelineState`, copies breakpoints.
- **ExtendedParams**: `init_extended_params()` for builtins with >5 params (constants + buffer refs per slot).
- **ForeachAlloc**: `init_foreach_state()` for FOREACH_EVENT instances (VOICE_POOL / PER_ITERATION / SHARED allocators). VOICE_POOL reuses the poly_* fields.
- **SoundfontEvents**: `init_soundfont_voice_event_state()` for event-driven SF2 voice pools.
- **EventTransform**: `init_sequence_program_state()` with empty sequences (output buffer sized from `total_events`).

After state inits, `init_midi_sources()` walks `required_midi_sources` and calls `vm->init_midi_queue_state()` per entry.

### Sample Resolution

`resolve_sample_ids()` does two passes before program load:
1. **Pattern events** — walks `state_inits[].sequence_sample_mappings`, writes resolved sample id to `events[event_idx].values[mapping.value_slot]` (slot supports merged sample polyrhythms like `[bd, hh]`).
2. **Scalar `sample("name")` refs** — walks `scalar_sample_mappings`, patches the PUSH_CONST instruction's `state_id` immediate with the resolved id.

### Asset Auto-Loading

`auto_load_required_assets(result)` runs at compile time, before sample-id resolution:
- `required_uris` — file://, res://, user:// paths are read via `FileAccess::get_file_as_bytes()` and routed by `UriKind` into `SampleBank` / `SoundFontRegistry` / `WavetableBankRegistry`. Network schemes (http, github, bundled, blob) are warned and skipped.
- `required_soundfonts` — declared by literal `soundfont("name.sf2", …)` calls.
- `required_wavetables` — declared by `wt_load("name", "path")`.
- Legacy `sample_pack` Resource is still loaded as a fallback.

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

# Sample / asset loading
load_sample(name, path) -> bool      # Load audio file (WAV/OGG/FLAC/MP3) into SampleBank
load_soundfont(name, path) -> bool   # Load SF2 file into SoundFontRegistry
load_wavetable(name, path) -> bool   # Load WAV into WavetableBankRegistry
clear_samples()
clear_soundfonts()
get_loaded_samples() -> Array        # [{name, id, frames, channels, sample_rate}, ...]
get_loaded_soundfonts() -> Array     # [{id, preset_count}, ...]
get_required_samples() -> Array      # [{name, bank, variant}, ...] from last compile
get_required_soundfonts() -> Array   # [{filename, preset_index}, ...]
get_required_wavetables() -> Array   # [{name, path, id}, ...]
get_required_uris() -> Array         # [{uri, kind}, ...] (kind: sample_bank|soundfont|wavetable|sample)
get_required_midi_sources() -> Array # [{state_id, kind, name_or_path, channel_filter, loop, tempo_mode}, ...]
get_midi_cc_routes() -> Array        # [{param_name, cc_num, channel_filter, scale, bias, slew_ms}, ...]
get_viz_decls() -> Array             # [{name, type, state_id, options_json, …}, ...]

# Parameters
set_param(name, value, slew_ms=20.0)
get_param(name) -> float
trigger_button(name)                 # Sets to 1.0, auto-releases after 2 blocks
get_param_decls() -> Array           # [{name, type, default, min, max, options}, ...]

# Visualization
get_waveform_data() -> PackedFloat32Array  # 1024 frames, L/R interleaved

# Audio input (INPUT opcode)
set_input_buffers(left: PackedFloat32Array, right: PackedFloat32Array)
```

`cc_num` sentinels in `get_midi_cc_routes()`: `0..127` = CC number, `-1` = pitch-bend, `-2` = channel aftertouch. GDScript host is responsible for wiring Godot's MIDI input → `set_param(name, value, slew_ms)`; the addon surfaces the routes but does not auto-wire `InputEventMIDI`.

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
- Visualizations list (read-only — names + types from `get_viz_decls()`; rendering not yet wired)
- MIDI CC routes list (read-only — `cc#N → param_name` from `get_midi_cc_routes()`)
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
