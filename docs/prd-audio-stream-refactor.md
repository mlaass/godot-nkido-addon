# PRD: Custom AudioStream Architecture Refactor

> **Status: DECIDED** — Option A chosen (modified). NkidoPlayer removed. NkidoEngine removed. Prerequisite for v2.

**Date:** 2026-03-29
**Prerequisite:** v1 MVP complete

---

## 1. Overview

### 1.1 Problem Statement

The current Nkido audio architecture works but doesn't follow Godot's custom AudioStream pattern. Today:

- **NkidoPlayer** (Node) owns everything: the Cedar VM, compilation state, parameter declarations, and an internal AudioStreamPlayer child
- **NkidoAudioStream** is a thin wrapper with a back-pointer to NkidoPlayer — it holds no data and can't function independently
- **NkidoAudioStreamPlayback** also holds a back-pointer to NkidoPlayer, reaching through it to access the VM

This creates tight coupling: the AudioStream and Playback are meaningless without the specific NkidoPlayer instance that created them. This conflicts with Godot's AudioStream pattern where:

- The **AudioStream** is the data container (a Resource, refcounted, potentially loadable from disk)
- The **AudioStreamPlayback** is the stateful playback engine, self-contained
- Standard **AudioStreamPlayer** nodes bind to AudioStream resources

### 1.2 Current Architecture

```
NkidoPlayer (Node)
  ├─ vm_: unique_ptr<cedar::VM>          ← owns everything
  ├─ source_: String
  ├─ compiled_: bool
  ├─ param_decls_: vector<ParamDecl>
  ├─ compile(), set_param(), play(), stop()
  │
  ├─ stream_player_: AudioStreamPlayer*  ← internal hidden child
  │   └─ stream: NkidoAudioStream
  │       ├─ player_: NkidoPlayer*       ← back-pointer (no own data)
  │       └─ _instantiate_playback()
  │           └─ NkidoAudioStreamPlayback
  │               ├─ player_: NkidoPlayer*  ← back-pointer
  │               ├─ ring_buffer_[4096]
  │               └─ _mix() → player_->get_vm()->process_block()
```

### 1.3 Target Architecture

NkidoAudioStream becomes the sole user-facing class — a proper AudioStream Resource that owns the Cedar VM, source code, compilation state, and parameters. Users assign it to standard AudioStreamPlayer nodes.

NkidoPlayer and NkidoEngine are removed entirely. No convenience wrappers, no global singletons. Each NkidoAudioStream is fully self-contained.

### 1.4 Goals

- Align with Godot's custom AudioStream API pattern (AudioStream as data container, Playback as self-contained mixer)
- Eliminate back-pointer coupling between AudioStream/Playback and NkidoPlayer
- Remove NkidoPlayer — users use standard AudioStreamPlayer nodes
- Remove NkidoEngine singleton — all configuration is per-stream
- Lay groundwork for v2 features (samples, `.akk` files, editor enhancements)

### 1.5 Non-Goals

- Adding new features (samples, .akk files, bottom panel — those are v2)
- Multi-platform support
- Preserving the NkidoPlayer or NkidoEngine API

---

## 2. Architecture Candidates (Resolved)

Two ownership models were considered. **Option A (modified) was chosen.**

### 2.1 Option A: Stream Owns VM (Chosen)

NkidoAudioStream becomes the real object — holds source code, compiles, owns the VM. NkidoPlayer is removed entirely.

```
NkidoAudioStream (AudioStream, Resource)
  ├─ source_: String
  ├─ vm_: unique_ptr<cedar::VM>
  ├─ compiled_: bool
  ├─ bpm_: float
  ├─ crossfade_blocks_: int
  ├─ param_decls_: vector<ParamDecl>
  ├─ last_compile_result_: CompileResult
  ├─ pending_button_releases_: map<string, int>
  │
  ├─ compile() -> bool
  ├─ set_param(name, value, slew_ms)
  ├─ get_param(name) -> float
  ├─ trigger_button(name)
  ├─ get_param_decls() -> Array
  ├─ get_diagnostics() -> Array
  ├─ is_compiled() -> bool
  │
  ├─ _instantiate_playback() -> NkidoAudioStreamPlayback
  ├─ _get_length() -> 0.0
  └─ _is_monophonic() -> true

NkidoAudioStreamPlayback (AudioStreamPlayback)
  ├─ stream_: Ref<NkidoAudioStream>    ← forward ref (not back-pointer)
  ├─ ring_buffer_[4096]
  ├─ temp_left_/right_[128]
  └─ _mix() → stream_->get_vm()->process_block()
```

**Modification from original Option A:** NkidoPlayer is not kept as a wrapper — it is removed. Users interact with NkidoAudioStream directly via standard AudioStreamPlayer nodes (e.g., `$AudioStreamPlayer.stream.compile()`).

### 2.2 Option B: Player Owns VM, Clean Bridge (Rejected)

Rejected because it doesn't follow Godot's AudioStream pattern, doesn't enable standalone stream use, and doesn't help with `.akk` Resource files.

---

## 3. Detailed Design

### 3.1 NkidoAudioStream Changes

**Moves in from NkidoPlayer:**
- `source_: String` property
- `vm_: std::unique_ptr<cedar::VM>` (created in constructor)
- `compiled_: bool`
- `last_compile_result_: akkado::CompileResult`
- `param_decls_: std::vector<akkado::ParamDecl>`
- `crossfade_blocks_: int`
- `bpm_: float`
- `pending_button_releases_: std::unordered_map<std::string, int>`

**Methods moved in:**
- `compile() -> bool` — compiles source, applies state inits, loads program
- `apply_state_inits()` — sequence, poly, timeline initialization
- `set_param(name, value, slew_ms)`
- `get_param(name) -> float`
- `trigger_button(name)` — sets to 1.0, marks for audio-thread release after N blocks
- `get_param_decls() -> Array`
- `get_diagnostics() -> Array`
- `is_compiled() -> bool`
- `get_vm() -> cedar::VM*` (for playback access)

**BPM:** Each stream has its own `bpm_` property (default 120.0). No global fallback. Sample rate is read from `AudioServer::get_singleton()->get_mix_rate()`.

**Existing methods kept:**
- `_instantiate_playback()` — creates NkidoAudioStreamPlayback with `Ref<NkidoAudioStream>(this)`
- `_get_length()` — returns 0.0 (streaming)
- `_is_monophonic()` — returns true

**Signals:**
- `compilation_finished(success: bool, errors: Array)`
- `params_changed(params: Array)`

**Button release mechanism:** `trigger_button()` sets the param to 1.0 and adds the name to `pending_button_releases_` with a block countdown. The Playback checks this map during `_mix()` and decrements; when the count reaches 0, it calls `vm->set_param(name, 0.0)` directly from the audio thread (safe — EnvMap uses atomics).

### 3.2 NkidoAudioStreamPlayback Changes

**Replace:**
- `player_: NkidoPlayer*` → `stream_: Ref<NkidoAudioStream>`

**_mix() changes:**
- `player_->get_vm()` → `stream_->get_vm()`
- `player_->is_compiled()` → `stream_->is_compiled()`
- BPM read from `stream_->get_bpm()`
- Process pending button releases from `stream_->pending_button_releases_`

Ring buffer, block size, temp buffers — all unchanged.

### 3.3 NkidoPlayer — Removed

NkidoPlayer is deleted entirely. All its functionality now lives on NkidoAudioStream.

Users create a standard AudioStreamPlayer node in the scene tree and assign a NkidoAudioStream resource to its `stream` property.

### 3.4 NkidoEngine — Removed

NkidoEngine singleton is deleted. Its responsibilities are redistributed:
- **Global BPM** → removed. Each NkidoAudioStream has its own `bpm_` property.
- **Sample rate** → NkidoAudioStream reads from `AudioServer::get_singleton()->get_mix_rate()` directly.

### 3.5 register_types.cpp Changes

```cpp
void initialize_nkido_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    cedar::init();
    GDREGISTER_CLASS(NkidoAudioStream);
    GDREGISTER_CLASS(NkidoAudioStreamPlayback);
    // NkidoPlayer and NkidoEngine removed
}
```

### 3.6 Inspector Plugin Changes

The inspector plugin (`nkido_inspector.gd`) is updated to handle AudioStreamPlayer nodes that have a NkidoAudioStream as their stream:

- `_can_handle(object)` → returns true when object is an AudioStreamPlayer and `object.stream is NkidoAudioStream`
- Accesses the stream via `object.stream` for source editing, transport, and parameters
- Play/Stop buttons delegate to the AudioStreamPlayer node (not the stream)

### 3.7 Example Scene Changes

- `example/Main.tscn` — replace NkidoPlayer nodes with AudioStreamPlayer nodes, each with a NkidoAudioStream resource
- `example/Main.gd` — access streams via `$Player.stream`, call compile/set_param on the stream, call play/stop on the AudioStreamPlayer node

### 3.8 Threading Model (Unchanged)

- **Main thread**: `compile()`, `set_param()`, `trigger_button()`, inspector interactions — all on NkidoAudioStream now
- **Audio thread**: `_mix()` → `stream_->get_vm()->process_block()`
- **No mutexes needed** — Cedar's lock-free APIs unchanged

---

## 4. File-Level Changes

| File | Change |
|------|--------|
| `addons/nkido/src/nkido_audio_stream.h` | Major rewrite — add VM, source, compilation, params, signals, button release |
| `addons/nkido/src/nkido_audio_stream.cpp` | Major rewrite — move compile/state-init/param logic from player |
| `addons/nkido/src/nkido_audio_stream_playback.h` | Moderate — replace `NkidoPlayer*` with `Ref<NkidoAudioStream>`, add button release processing |
| `addons/nkido/src/nkido_audio_stream_playback.cpp` | Moderate — update `_mix()` to use stream reference, process button releases |
| `addons/nkido/src/nkido_player.h` | **Delete** |
| `addons/nkido/src/nkido_player.cpp` | **Delete** |
| `addons/nkido/src/nkido_engine.h` | **Delete** |
| `addons/nkido/src/nkido_engine.cpp` | **Delete** |
| `addons/nkido/src/register_types.cpp` | Remove NkidoPlayer/NkidoEngine registration, remove singleton |
| `addons/nkido/nkido_inspector.gd` | Update `_can_handle()` to target AudioStreamPlayer with NkidoAudioStream, access stream via `object.stream` |
| `addons/nkido/nkido_plugin.gd` | No changes |
| `example/Main.tscn` | Replace NkidoPlayer with AudioStreamPlayer + NkidoAudioStream |
| `example/Main.gd` | Update to use `$Player.stream.compile()` pattern |
| `CMakeLists.txt` | Remove nkido_player and nkido_engine source files |

---

## 5. Resolved Questions

### 5.1 Which architecture: Option A or Option B?

**Option A (modified).** Stream owns VM. NkidoPlayer removed entirely. NkidoEngine removed entirely.

### 5.2 Should NkidoPlayer be kept, removed, or made optional?

**Removed.** Users use standard AudioStreamPlayer + NkidoAudioStream. No convenience wrapper.

### 5.3 VM statefulness on stop/restart

**Deferred to implementation.** Cedar VM has running state (oscillator phases, envelope positions, sequence counters). The simplest behavior is reset on stop — matches synthesizer mental model. Can be revisited if users request state preservation.

### 5.4 Resource sharing semantics

Each NkidoAudioStream instance has its own VM. If two AudioStreamPlayers reference the same NkidoAudioStream instance, the VM is single-producer — two playbacks calling `process_block()` concurrently would corrupt state. Mitigation: `_is_monophonic()` returns true, which tells Godot to only allow one active playback per stream instance. Document that each AudioStreamPlayer should have its own NkidoAudioStream instance.

### 5.5 Scope: standalone PRD or fold into v2?

**Standalone effort, prerequisite to v2.** This refactoring is completed before v2 work begins.

---

## 6. Implementation Phases

### Phase 1: Move Compilation & VM to NkidoAudioStream, Remove NkidoPlayer & NkidoEngine

**Goal:** NkidoAudioStream becomes the sole user-facing class. NkidoPlayer and NkidoEngine are deleted.

**Files:**
- `nkido_audio_stream.h/cpp` — add VM, source, compilation, params, signals, button release
- `nkido_audio_stream_playback.h/cpp` — replace back-pointer with stream Ref, process button releases
- `nkido_player.h/cpp` — **delete**
- `nkido_engine.h/cpp` — **delete**
- `register_types.cpp` — remove NkidoPlayer/NkidoEngine, remove singleton
- `CMakeLists.txt` — remove deleted source files
- `nkido_inspector.gd` — target AudioStreamPlayer with NkidoAudioStream
- `example/Main.tscn` + `example/Main.gd` — update to new pattern

**Verification:**
1. Build succeeds: `cmake --build build -j$(nproc)`
2. Open example scene in Godot editor
3. AudioStreamPlayer nodes with NkidoAudioStream resources are visible
4. Inspector shows source editor, transport, parameter controls when AudioStreamPlayer is selected
5. Compile + play produces audio
6. Hot-swap works (edit source while playing, recompile)
7. Parameter sliders work
8. Button triggers work (auto-release after ~2 blocks)
9. Stop/play cycle works
10. `grep -r "NkidoPlayer" addons/nkido/src/` returns nothing
11. `grep -r "NkidoEngine" addons/nkido/src/` returns nothing

### Phase 2: Clean Up & Harden

**Goal:** Remove dead code, verify no back-pointer patterns remain.

**Files:**
- All `nkido_*.h/cpp` — verify no raw pointer back-references
- Remove any `NkidoPlayer` or `NkidoEngine` references from comments/documentation

**Verification:**
1. `grep -r "NkidoPlayer" addons/nkido/` returns only documentation/PRD references
2. `grep -r "NkidoEngine" addons/nkido/` returns only documentation/PRD references
3. All Phase 1 verification steps still pass

---

## 7. Edge Cases

| Scenario | Expected Behavior |
|----------|-------------------|
| Compile with empty source | `compile()` returns false, error signal emitted |
| Play before compile | Silence (no program loaded) |
| Multiple rapid recompiles while playing | Cedar's triple-buffer handles hot-swap (unchanged) |
| AudioStreamPlayer removed from tree during playback | Audio stops (Godot handles this) |
| Two AudioStreamPlayers with same NkidoAudioStream instance | `_is_monophonic()` prevents concurrent playback |
| Two AudioStreamPlayers with separate NkidoAudioStream instances | Independent (each has own VM) |
| trigger_button() called when no playback active | Button release map populated; releases processed when playback starts |

---

## 8. Testing Strategy

All testing is manual in the Godot editor:

1. **Build test**: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)`
2. **Basic playback**: Open example scene → compile → play → hear audio
3. **Hot-swap**: While playing, modify source in inspector → recompile → audio transitions smoothly
4. **Parameters**: After compile, move sliders → audio responds in real-time
5. **Button trigger**: Press button param → hear triggered envelope, auto-releases
6. **Stop/restart**: Stop → play → audio restarts
7. **Error handling**: Enter invalid source → compile fails → status shows error → previous audio continues if was playing
8. **Inspector stability**: Select/deselect AudioStreamPlayer nodes → inspector rebuilds correctly
9. **Multiple streams**: Two AudioStreamPlayers with different NkidoAudioStream instances → both play independently
