# PRD: Custom AudioStream Architecture Refactor

> **Status: NOT STARTED** — Architecture refactoring to align NkidoAudioStream with Godot's custom AudioStream pattern

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

### 1.3 Proposed Direction

Restructure so NkidoAudioStream becomes a proper data-bearing AudioStream resource following Godot's custom AudioStream pattern. Eliminate back-pointers from AudioStream/Playback to NkidoPlayer.

**[OPEN QUESTION]** — Two candidate architectures are under consideration. See Section 6.

### 1.4 Goals

- Align with Godot's custom AudioStream API pattern (AudioStream as data container, Playback as self-contained mixer)
- Eliminate back-pointer coupling between AudioStream/Playback and NkidoPlayer
- Preserve all existing user-facing API and behavior (compile, play, stop, params, hot-swap, inspector)
- Lay groundwork for `.akk` file resources (v2 Phase 5)

### 1.5 Non-Goals

- Changing the public NkidoPlayer GDScript API (signals, methods, properties)
- Modifying Cedar VM or Akkado compiler internals
- Adding new features (samples, .akk files, bottom panel — those are v2)
- Multi-platform support
- Breaking the inspector plugin

---

## 2. Architecture Candidates

Two ownership models are under consideration. Both eliminate the back-pointer anti-pattern.

### 2.1 Option A: Stream Owns VM

NkidoAudioStream becomes the real object — holds source code, compiles, owns the VM. NkidoPlayer becomes a thin convenience wrapper.

```
NkidoAudioStream (AudioStream, Resource)
  ├─ source_: String
  ├─ vm_: unique_ptr<cedar::VM>
  ├─ compiled_: bool
  ├─ param_decls_: vector<ParamDecl>
  ├─ compile() -> bool
  ├─ set_param(name, value, slew_ms)
  ├─ get_param_decls() -> Array
  └─ _instantiate_playback() -> NkidoAudioStreamPlayback

NkidoAudioStreamPlayback (AudioStreamPlayback)
  ├─ stream_: Ref<NkidoAudioStream>    ← forward ref (not back-pointer)
  ├─ ring_buffer_[4096]
  ├─ temp_left_/right_[128]
  └─ _mix() → stream_->get_vm()->process_block()

NkidoPlayer (Node) — convenience wrapper
  ├─ audio_stream_: Ref<NkidoAudioStream>  ← owns the resource
  ├─ stream_player_: AudioStreamPlayer*     ← internal child
  ├─ delegates: compile(), play(), stop(), set_param(), etc.
  └─ handles: autoplay, bus, button releases, signals
```

**Pros:**
- Follows Godot's AudioStream pattern exactly
- NkidoAudioStream can work standalone with standard AudioStreamPlayer nodes
- Natural path to `.akk` Resource files (v2 Phase 5)
- AudioStreamPlayback references the stream via a proper Ref<>, not a raw pointer

**Cons:**
- Larger refactoring scope — compilation logic moves from NkidoPlayer to NkidoAudioStream
- Resource semantics: if loaded from ResourceLoader, multiple references share the same VM (may need `duplicate()` or `local_to_scene`)
- AudioStream is RefCounted, VM is unique_ptr — ownership semantics need care

### 2.2 Option B: Player Owns VM, Clean Bridge

Keep VM on NkidoPlayer but restructure the bridge so AudioStream/Playback don't use raw back-pointers. Instead, the Playback receives a VM pointer at instantiation time.

```
NkidoPlayer (Node)
  ├─ vm_: unique_ptr<cedar::VM>          ← still owns VM
  ├─ source_, compiled_, param_decls_
  ├─ compile(), set_param(), play(), stop()
  │
  ├─ stream_player_: AudioStreamPlayer*
  │   └─ stream: NkidoAudioStream
  │       ├─ vm_ptr_: cedar::VM*         ← set by NkidoPlayer (not back-pointer)
  │       ├─ bpm_callback_: Callable     ← for audio-thread BPM sync
  │       └─ _instantiate_playback()
  │           └─ NkidoAudioStreamPlayback
  │               ├─ vm_: cedar::VM*     ← copied from stream at instantiation
  │               ├─ ring_buffer_[4096]
  │               └─ _mix() → vm_->process_block()
```

**Pros:**
- Smaller refactoring — compilation logic stays on NkidoPlayer
- No Resource ownership complications
- Cleaner than current back-pointer approach

**Cons:**
- Doesn't follow Godot's AudioStream pattern (stream has no meaningful data)
- NkidoAudioStream still can't work standalone
- Doesn't help with `.akk` Resource files

### **[OPEN QUESTION]: Which architecture to adopt?**

Option A is recommended if the goal includes future standalone stream use and `.akk` resources. Option B is recommended if the goal is minimal cleanup with no API surface changes.

---

## 3. Impact Assessment

| Component | Option A | Option B |
|-----------|----------|----------|
| `nkido_audio_stream.h/cpp` | **Major rewrite** — gains VM, source, compilation | **Moderate** — gains VM pointer, loses back-pointer |
| `nkido_audio_stream_playback.h/cpp` | **Moderate** — Ref<Stream> replaces raw Player* | **Moderate** — VM* replaces Player* back-pointer |
| `nkido_player.h/cpp` | **Major rewrite** — becomes delegation wrapper | **Minor** — sets VM ptr on stream instead of player ptr |
| `nkido_engine.h/cpp` | **Stays** | **Stays** |
| `register_types.cpp` | **Stays** (same classes registered) | **Stays** |
| `nkido_inspector.gd` | **Stays** (calls same NkidoPlayer API) | **Stays** |
| `nkido_plugin.gd` | **Stays** | **Stays** |
| `example/Main.tscn/gd` | **Stays** (uses NkidoPlayer) | **Stays** |
| `CMakeLists.txt` | **Stays** (same source files) | **Stays** |

---

## 4. Detailed Design (Option A)

This section details the recommended Option A architecture. Skip if Option B is chosen.

### 4.1 NkidoAudioStream Changes

**Moves in from NkidoPlayer:**
- `source_: String` property
- `vm_: std::unique_ptr<cedar::VM>` (created in constructor)
- `compiled_: bool`
- `last_compile_result_: akkado::CompileResult`
- `param_decls_: std::vector<akkado::ParamDecl>`
- `crossfade_blocks_: int`
- `bpm_: float`

**Methods moved in:**
- `compile() -> bool` — compiles source, applies state inits, loads program
- `apply_state_inits()` — sequence, poly, timeline initialization
- `set_param(name, value, slew_ms)`
- `get_param(name) -> float`
- `get_param_decls() -> Array`
- `get_diagnostics() -> Array`
- `is_compiled() -> bool`
- `get_vm() -> cedar::VM*` (for playback access)
- `get_effective_bpm() -> float` (checks local then global singleton)

**Existing methods kept:**
- `_instantiate_playback()` — creates NkidoAudioStreamPlayback with `Ref<NkidoAudioStream>(this)`
- `_get_length()` — returns 0.0 (streaming)
- `_is_monophonic()` — returns true

**Signals (new on NkidoAudioStream):**
- `compilation_finished(success: bool, errors: Array)`
- `params_changed(params: Array)`

### 4.2 NkidoAudioStreamPlayback Changes

**Replace:**
- `player_: NkidoPlayer*` → `stream_: Ref<NkidoAudioStream>`

**_mix() changes:**
- `player_->get_vm()` → `stream_->get_vm()`
- `player_->is_compiled()` → `stream_->is_compiled()`
- `player_->get_effective_bpm()` → `stream_->get_effective_bpm()`

Ring buffer, block size, temp buffers — all unchanged.

### 4.3 NkidoPlayer Changes

**Removes:**
- `vm_: unique_ptr<cedar::VM>` — now on NkidoAudioStream
- `source_: String` — now on NkidoAudioStream
- `compiled_: bool` — now on NkidoAudioStream
- `last_compile_result_` — now on NkidoAudioStream
- `param_decls_` — now on NkidoAudioStream
- `compile()` implementation — delegates to `audio_stream_->compile()`
- `apply_state_inits()` — moves to NkidoAudioStream

**Keeps:**
- `audio_stream_: Ref<NkidoAudioStream>` — owns the stream resource
- `stream_player_: AudioStreamPlayer*` — internal child node
- `bus_: StringName` — audio bus routing
- `autoplay_: bool` — compile+play on `_ready()`
- `pending_button_releases_: map` — button release timing (needs `_process()`)
- `ensure_initialized()` — lazy init of AudioStreamPlayer child
- `play()`, `stop()`, `pause()` — delegate to stream_player_
- `trigger_button()` — delegates set_param + manages release timer

**Delegation pattern:**
```cpp
bool NkidoPlayer::compile() {
    ensure_initialized();
    return audio_stream_->compile();
}

void NkidoPlayer::set_param(const String &name, float value, float slew_ms) {
    if (audio_stream_.is_valid()) {
        audio_stream_->set_param(name, value, slew_ms);
    }
}
```

**Signal forwarding:**
NkidoPlayer connects to NkidoAudioStream's signals and re-emits them so existing GDScript code and the inspector continue working unchanged.

### 4.4 Threading Model (Unchanged)

- **Main thread**: `compile()`, `set_param()`, `trigger_button()` — all on NkidoAudioStream now
- **Audio thread**: `_mix()` → `stream_->get_vm()->process_block()`
- **No mutexes needed** — Cedar's lock-free APIs unchanged

### 4.5 Initialization Flow

```
NkidoPlayer._ready()
  └─ ensure_initialized()
      ├─ audio_stream_ = Ref<NkidoAudioStream>.instantiate()
      ├─ stream_player_ = memnew(AudioStreamPlayer)
      ├─ add_child(stream_player_)
      ├─ stream_player_->set_stream(audio_stream_)
      ├─ stream_player_->set_bus(bus_)
      ├─ audio_stream_->set_sample_rate(engine->get_sample_rate())
      ├─ audio_stream_->set_crossfade_blocks(crossfade_blocks_)
      └─ connect audio_stream_ signals → NkidoPlayer signal forwarding
```

---

## 5. File-Level Changes

| File | Change |
|------|--------|
| `addons/nkido/src/nkido_audio_stream.h` | Major rewrite — add VM, source, compilation, params, signals |
| `addons/nkido/src/nkido_audio_stream.cpp` | Major rewrite — move compile/state-init/param logic from player |
| `addons/nkido/src/nkido_audio_stream_playback.h` | Moderate — replace `NkidoPlayer*` with `Ref<NkidoAudioStream>` |
| `addons/nkido/src/nkido_audio_stream_playback.cpp` | Moderate — update `_mix()` to use stream reference |
| `addons/nkido/src/nkido_player.h` | Major rewrite — remove VM/compile state, add delegation |
| `addons/nkido/src/nkido_player.cpp` | Major rewrite — delegation + signal forwarding |
| `addons/nkido/src/nkido_engine.h/cpp` | No changes |
| `addons/nkido/src/register_types.cpp` | No changes (same classes) |
| `addons/nkido/nkido_inspector.gd` | No changes (calls NkidoPlayer API which still exists) |
| `addons/nkido/nkido_plugin.gd` | No changes |
| `example/Main.tscn` | No changes |
| `example/Main.gd` | No changes |

---

## 6. Open Questions

### 6.1 Which architecture: Option A or Option B?

Option A (stream owns VM) is recommended for cleaner Godot alignment and v2 readiness. Option B (clean bridge) is a smaller refactoring if the only goal is removing back-pointers.

### 6.2 Should NkidoPlayer be kept, removed, or made optional?

Three possibilities:
- **Keep** — NkidoPlayer stays as the primary user-facing node, delegates to NkidoAudioStream internally. Existing scenes and scripts don't change.
- **Remove** — Users use NkidoAudioStream directly with standard AudioStreamPlayer. Breaks all existing usage.
- **Both coexist** — NkidoAudioStream works standalone with AudioStreamPlayer AND NkidoPlayer exists as a convenience wrapper. Most flexible but more API surface.

**Recommended:** Keep NkidoPlayer as convenience wrapper (no breaking changes), with NkidoAudioStream usable standalone as a bonus.

### 6.3 VM statefulness on stop/restart

Cedar VM has running state (oscillator phases, envelope positions, sequence counters). When playback stops and restarts:
- **Reset** — VM state resets, synth restarts from zero. Simple, matches synthesizer mental model.
- **Preserve** — VM keeps running or state is preserved across stop/start. Sequences continue.
- **Configurable** — Property to choose behavior.

### 6.4 Resource sharing semantics

If NkidoAudioStream is a Resource, what happens if two AudioStreamPlayers reference the same instance? The VM is single-producer — two playbacks calling `process_block()` concurrently would corrupt state. Options:
- Mark as `local_to_scene = true` (each scene instance gets a duplicate)
- Document that sharing is unsupported
- Override `_duplicate()` to deep-copy the VM

### 6.5 Scope: standalone PRD or fold into v2?

This refactoring could be:
- A standalone effort (this PRD) done before v2 work begins
- Folded into v2 as a prerequisite phase before Phase 4 (samples) and Phase 5 (.akk files)

---

## 7. Implementation Phases

### Phase 1: Move Compilation & VM to NkidoAudioStream

**Goal:** NkidoAudioStream becomes the data owner. NkidoPlayer delegates.

**Files:**
- `nkido_audio_stream.h/cpp` — add VM, source, compilation, params, signals
- `nkido_player.h/cpp` — remove ownership, add delegation
- `nkido_audio_stream_playback.h/cpp` — replace back-pointer with stream Ref

**Verification:**
1. Build succeeds: `cmake --build build -j$(nproc)`
2. Open example scene in Godot editor
3. Inspector still shows source editor, transport, parameter controls
4. Compile + play produces audio
5. Hot-swap works (edit source while playing, recompile)
6. Parameter sliders work
7. Button triggers work
8. Stop/play cycle works

### Phase 2: Clean Up & Harden

**Goal:** Remove dead code, ensure thread safety invariants, add documentation.

**Files:**
- All `nkido_*.h/cpp` — remove any remaining back-pointer patterns
- Verify no raw `NkidoPlayer*` references remain in stream/playback

**Verification:**
1. `grep -r "NkidoPlayer\*" addons/nkido/src/nkido_audio_stream*` returns nothing
2. All Phase 1 verification steps still pass

---

## 8. Edge Cases

| Scenario | Expected Behavior |
|----------|-------------------|
| Compile with empty source | `compile()` returns false, error signal emitted |
| Play before compile | No-op (same as current) |
| Multiple rapid recompiles while playing | Cedar's triple-buffer handles hot-swap (unchanged) |
| NkidoPlayer removed from tree during playback | AudioStreamPlayer child removed, audio stops |
| Two NkidoPlayers with same source | Each has its own NkidoAudioStream + VM (independent) |
| Autoplay on `_ready()` | NkidoPlayer calls `compile()` then `play()` on stream (unchanged) |

---

## 9. Testing Strategy

All testing is manual in the Godot editor (same as v1):

1. **Build test**: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)`
2. **Basic playback**: Open example scene → compile → play → hear audio
3. **Hot-swap**: While playing, modify source in inspector → recompile → audio transitions smoothly
4. **Parameters**: After compile, move sliders → audio responds in real-time
5. **Button trigger**: Press button param → hear triggered envelope
6. **Stop/restart**: Stop → play → audio restarts
7. **Error handling**: Enter invalid source → compile fails → status shows error → previous audio continues if was playing
8. **Inspector stability**: Select/deselect NkidoPlayer node → inspector rebuilds correctly
