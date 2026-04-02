# PRD: NkidoAkkadoSource Resource — Shader-Style `.akk` File Workflow

> **Status: NOT STARTED** — Replaces inline source/source_file with a proper Godot resource

**Date:** 2026-04-02
**Prerequisites:**
- v2 phases 4-6 are complete (samples, files, editor)

---

## 1. Overview

### 1.1 Context

Currently, NkidoAudioStream stores Akkado source code in two ways:
- `source` (String) — inline multiline text embedded in the resource
- `source_file` (String) — path to a `.akk` file on disk

This works but doesn't follow Godot's resource model. In Godot, content like shader code lives in a **separate Resource type** (e.g., `Shader`) that the consuming resource (e.g., `ShaderMaterial`) references. This pattern enables:
- Saving embedded resources to files via right-click "Save As..."
- Loading existing files from the resource picker
- Sharing one source across multiple consumers
- First-class editor integration (thumbnails, drag-and-drop, double-click to open)

### 1.2 Goals

- **New resource type `NkidoAkkadoSource`** that holds Akkado source text and is saveable as a `.akk` file
- **ResourceFormatLoader/Saver** so `.akk` files are first-class Godot resources
- **Replace `source` and `source_file`** on NkidoAudioStream with a single `akkado_source` resource property
- **Share one source** across multiple NkidoAudioStream instances
- **Double-click `.akk` files** in the FileSystem dock to open the bottom panel editor

### 1.3 Non-Goals

- Storing BPM, sample paths, or soundfont references in NkidoAkkadoSource (stays on NkidoAudioStream)
- Akkado `import` statements or cross-file module resolution
- Custom editor for standalone `.akk` files outside of a stream context
- Auto-recompile across streams when shared source changes (each stream compiles independently)
- Shared source indicator in editor UI (deferred — sharing works without it)
- Changes to compile trigger behavior (stays manual)

### 1.4 Key Design Decisions

- **Shader pattern**: NkidoAkkadoSource is to NkidoAudioStream as Shader is to ShaderMaterial
- **Plain text on disk**: `.akk` files contain only Akkado source code (no Godot resource headers)
- **C++ implementation**: ResourceFormatLoader/Saver implemented in GDExtension C++
- **Source-only resource**: NkidoAkkadoSource stores only the source text string
- **Built-in `Resource.changed` signal**: Used for change notification, no custom signal

---

## 2. User Experience

### 2.1 Creating an Embedded Source (Inspector)

1. Select an AudioStreamPlayer with a NkidoAudioStream
2. In the inspector, click the `Akkado Source` property (shows `<empty>`)
3. Click the dropdown arrow and select **New NkidoAkkadoSource**
4. An embedded NkidoAkkadoSource is created inline
5. The bottom panel opens with an empty code editor — start writing Akkado code

### 2.2 Saving Embedded Source to a `.akk` File

1. Right-click the `Akkado Source` property in the inspector
2. Select **Save As...** from the context menu
3. Choose a path (e.g., `res://audio/combat_music.akk`)
4. The embedded resource is saved as a `.akk` file on disk
5. The property now references the external file

This is identical to how saving an embedded Shader to a `.gdshader` file works.

### 2.3 Loading an Existing `.akk` File

1. In the `Akkado Source` property dropdown, select **Load**
2. Browse and select an existing `.akk` file
3. The stream now references that file
4. Or: drag a `.akk` file from the FileSystem dock onto the property

### 2.4 Sharing a Source Across Streams

```gdscript
# Two AudioStreamPlayers share the same Akkado source
var source: NkidoAkkadoSource = load("res://audio/ambient.akk")

$Music1.stream.akkado_source = source
$Music1.stream.bpm = 72.0
$Music1.stream.load_sample("pad", "res://samples/warm_pad.wav")

$Music2.stream.akkado_source = source  # Same source, different config
$Music2.stream.bpm = 90.0
$Music2.stream.load_sample("pad", "res://samples/bright_pad.wav")
```

### 2.5 Double-Click to Open in Editor

1. Double-click a `.akk` file in the FileSystem dock
2. The Nkido bottom panel opens with that source loaded for editing
3. If no stream is currently selected, the editor shows the source in standalone mode (edit only, no compile/play)

### 2.6 GDScript API

```gdscript
# Create source programmatically
var source = NkidoAkkadoSource.new()
source.source_code = '''
    freq = param("freq", 440, 20, 20000)
    osc("saw", freq) |> lpf(%, 2000) |> out(%, %)
'''

# Assign to stream
var stream: NkidoAudioStream = $Player.stream
stream.akkado_source = source
stream.compile()
$Player.play()

# Or load from file
stream.akkado_source = load("res://audio/synth.akk")
stream.compile()
```

### 2.7 Bottom Panel with Source Label

```
+------------------------------------------------------------------+
| Nkido  |  ambient.akk  |  [Compile] [Play] [Stop] | BPM:72     |
+------------------------------------------------------------------+
| 1  pad_freq = param("pad_freq", 200, 50, 2000)                  |
| 2  pad = osc("saw", pad_freq) |> lpf(%, pad_freq * 4)          |
| ...                                                              |
```

---

## 3. Architecture

### 3.1 Class Diagram

```
NkidoAkkadoSource (Resource)
  - source_code_: String
  │
  + set_source_code(code: String)
  + get_source_code() -> String
  │
  signals:
  + changed()  [inherited from Resource]

NkidoAkkadoSourceFormatLoader (ResourceFormatLoader)
  + _get_recognized_extensions() -> PackedStringArray  ["akk"]
  + _handles_type(type) -> bool
  + _get_resource_type(path) -> String
  + _load(path, original_path, use_sub_threads, cache_mode) -> Variant

NkidoAkkadoSourceFormatSaver (ResourceFormatSaver)
  + _get_recognized_extensions(resource) -> PackedStringArray  ["akk"]
  + _recognize(resource) -> bool
  + _save(resource, path, flags) -> Error

NkidoAudioStream (AudioStream, Resource)
  - akkado_source_: Ref<NkidoAkkadoSource>     [REPLACES source_ and source_file_]
  - vm_: unique_ptr<cedar::VM>
  - compiled_: bool
  - bpm_: float
  - ...
  │
  + set_akkado_source(source: NkidoAkkadoSource)
  + get_akkado_source() -> NkidoAkkadoSource
  + compile() -> bool                           [reads from akkado_source_]
  + ...
```

### 3.2 Resource Lifecycle

```
User creates "New NkidoAkkadoSource" in inspector
  │
  ├─ Embedded: resource lives inside the .tscn/.tres that contains the NkidoAudioStream
  │   └─ Right-click "Save As..." → ResourceFormatSaver writes plain .akk text file
  │       └─ Resource path updated to res://path/to.akk
  │
  └─ Load existing: ResourceFormatLoader reads .akk text file → creates NkidoAkkadoSource
      └─ Resource cached by Godot's ResourceLoader (shared reference)

Multiple streams reference same NkidoAkkadoSource:
  Stream A ──┐
             ├── NkidoAkkadoSource (res://audio/ambient.akk)
  Stream B ──┘
  │
  Source edited → Resource.changed emitted
  ├── Stream A.compile() [with Stream A's samples, BPM, VM]
  └── Stream B.compile() [with Stream B's samples, BPM, VM]
```

### 3.3 ResourceFormatLoader Flow

```
ResourceLoader.load("res://audio/synth.akk")
  │
  ├─ Godot calls NkidoAkkadoSourceFormatLoader._load(path, ...)
  │   ├─ FileAccess::open(path, READ)
  │   ├─ file->get_as_text() → source string
  │   ├─ NkidoAkkadoSource* res = memnew(NkidoAkkadoSource)
  │   ├─ res->set_source_code(source_string)
  │   ├─ res->set_path(path)
  │   └─ return Ref<NkidoAkkadoSource>(res)
  │
  └─ Godot caches the resource (subsequent loads return same instance)
```

### 3.4 ResourceFormatSaver Flow

```
ResourceSaver.save(akkado_source, "res://audio/synth.akk")
  │
  ├─ Godot calls NkidoAkkadoSourceFormatSaver._save(resource, path, flags)
  │   ├─ Cast resource to NkidoAkkadoSource
  │   ├─ FileAccess::open(path, WRITE)
  │   ├─ file->store_string(resource->get_source_code())
  │   └─ return OK
  │
  └─ Resource path updated to the saved path
```

### 3.5 Compilation Flow (Updated)

```
stream.compile()
  │
  ├─ If akkado_source_ is null → emit compilation_finished(false, [{message: "No source"}])
  │
  ├─ Get source text: akkado_source_->get_source_code()
  ├─ Get filename: akkado_source_->get_path() (for diagnostics, may be empty for embedded)
  │
  ├─ Build akkado::SampleRegistry from vm_->sample_bank()
  ├─ akkado::compile(code, filename, &sample_registry)
  ├─ resolve_sample_ids()
  ├─ apply_state_inits()
  └─ vm_->load_program(bytecode)
```

### 3.6 Change Notification

NkidoAudioStream connects to its source's `Resource.changed` signal:

```
stream.set_akkado_source(source)
  │
  ├─ Disconnect from old source's changed signal (if any)
  ├─ akkado_source_ = source
  └─ Connect to new source's changed signal → _on_source_changed()

_on_source_changed():
  └─ emit_changed()  [propagates to editor, triggers recompile in bottom panel]
```

The stream does **not** auto-recompile when the source changes. The editor or user script decides when to call `compile()`. This matches how ShaderMaterial works — the engine reprocesses the shader when the Shader resource changes, but the Material doesn't call a manual compile step.

---

## 4. Impact Assessment

| Component | Status | Notes |
|-----------|--------|-------|
| `NkidoAkkadoSource` | **New** | New Resource subclass holding source text |
| `NkidoAkkadoSourceFormatLoader` | **New** | Loads `.akk` files as NkidoAkkadoSource resources |
| `NkidoAkkadoSourceFormatSaver` | **New** | Saves NkidoAkkadoSource resources as `.akk` files |
| `NkidoAudioStream` | **Modified** | Replace `source`/`source_file` with `akkado_source` property |
| `NkidoAudioStreamPlayback` | **Stays** | No changes |
| `nkido_bottom_panel.gd` | **Modified** | Read/write via NkidoAkkadoSource, shared source indicator |
| `nkido_inspector.gd` | **Modified** | Source property now shows resource picker instead of CodeEdit + file picker |
| `nkido_plugin.gd` | **Modified** | Handle `.akk` double-click to open bottom panel |
| `register_types.cpp` | **Modified** | Register new classes and format loader/saver |
| `example/` scenes | **Modified** | Update to use NkidoAkkadoSource |

---

## 5. File-Level Changes

### 5.1 New Files

| File | Purpose |
|------|---------|
| `addons/nkido/src/nkido_akkado_source.h` | NkidoAkkadoSource class declaration |
| `addons/nkido/src/nkido_akkado_source.cpp` | NkidoAkkadoSource implementation |
| `addons/nkido/src/nkido_akkado_source_format_loader.h` | ResourceFormatLoader for `.akk` files |
| `addons/nkido/src/nkido_akkado_source_format_loader.cpp` | Loader implementation |
| `addons/nkido/src/nkido_akkado_source_format_saver.h` | ResourceFormatSaver for `.akk` files |
| `addons/nkido/src/nkido_akkado_source_format_saver.cpp` | Saver implementation |

### 5.2 Modified Files

| File | Change |
|------|--------|
| `addons/nkido/src/nkido_audio_stream.h` | Remove `source_`, `source_file_` members. Add `Ref<NkidoAkkadoSource> akkado_source_`. Add `_on_source_changed()`. |
| `addons/nkido/src/nkido_audio_stream.cpp` | Remove `source`/`source_file` property bindings. Add `akkado_source` property with `PROPERTY_HINT_RESOURCE_TYPE, "NkidoAkkadoSource"`. Update `compile()` to read from `akkado_source_`. Connect/disconnect `changed` signal in setter. |
| `addons/nkido/src/register_types.cpp` | Register `NkidoAkkadoSource`, `NkidoAkkadoSourceFormatLoader`, `NkidoAkkadoSourceFormatSaver`. Create and register loader/saver instances in `initialize_nkido_module()`. |
| `addons/nkido/nkido_bottom_panel.gd` | Read source from `current_stream.akkado_source.source_code`. Write changes to `akkado_source.source_code`. Add shared source indicator (count streams referencing same source). Handle null source gracefully. |
| `addons/nkido/nkido_inspector.gd` | Remove inline CodeEdit and file picker for source. The `akkado_source` property uses Godot's standard resource picker (New, Load, Save As). Keep transport, params, status UI. |
| `addons/nkido/nkido_plugin.gd` | Implement `_handles()` for `.akk` files (double-click opens bottom panel). Register `NkidoAkkadoSource` as an editable type. |
| `addons/nkido/src/CMakeLists.txt` or root `CMakeLists.txt` | Add new `.cpp` files to the build. |
| `example/Main.tscn` | Update to use `akkado_source` property instead of `source`/`source_file`. |
| `example/Main.gd` | Update script to use `akkado_source` API. |
| `example/SampleDemo.tscn` | Update to use `akkado_source` property. |

---

## 6. Implementation Phases

### Phase 1: NkidoAkkadoSource Resource

**Goal:** Create the new resource type that holds Akkado source text.

**Files:**
- Create `nkido_akkado_source.h/cpp`
- Modify `register_types.cpp` to register class

**Details:**
- Subclass `Resource`
- Single property: `source_code` (String, `PROPERTY_HINT_MULTILINE_TEXT`)
- `set_source_code()` calls `emit_changed()` when the value changes
- Register class name `NkidoAkkadoSource`

**Verification:**
- In Godot editor: Create → New Resource → search "NkidoAkkadoSource" → appears in list
- Set `source_code` property in inspector → text editor appears

### Phase 2: ResourceFormatLoader and ResourceFormatSaver

**Goal:** Make `.akk` files loadable and saveable as NkidoAkkadoSource resources.

**Files:**
- Create `nkido_akkado_source_format_loader.h/cpp`
- Create `nkido_akkado_source_format_saver.h/cpp`
- Modify `register_types.cpp` to register and instantiate loader/saver

**Details:**

Loader:
- `_get_recognized_extensions()` returns `["akk"]`
- `_handles_type()` returns true for `"NkidoAkkadoSource"`
- `_get_resource_type()` returns `"NkidoAkkadoSource"` for `.akk` paths
- `_load()` reads file as UTF-8 text, creates NkidoAkkadoSource, sets `source_code`

Saver:
- `_get_recognized_extensions()` returns `["akk"]` for NkidoAkkadoSource resources
- `_recognize()` returns true for NkidoAkkadoSource instances
- `_save()` writes `source_code` as plain UTF-8 text

Registration in `register_types.cpp`:
- Create `Ref<NkidoAkkadoSourceFormatLoader>` and `Ref<NkidoAkkadoSourceFormatSaver>` as module-level statics
- Call `ResourceLoader::add_resource_format_loader()` and `ResourceSaver::add_resource_format_saver()` in initialize
- Call the corresponding remove functions in uninitialize

**Verification:**
- Create NkidoAkkadoSource in inspector → right-click → Save As → choose `.akk` → file on disk contains only Akkado text
- `load("res://test.akk")` from GDScript → returns a NkidoAkkadoSource with correct `source_code`
- `.akk` files show in the FileSystem dock with a resource icon
- Drag `.akk` from FileSystem dock onto a resource property → assigns correctly

### Phase 3: Update NkidoAudioStream

**Goal:** Replace `source`/`source_file` with the `akkado_source` resource property.

**Files:**
- Modify `nkido_audio_stream.h`
- Modify `nkido_audio_stream.cpp`

**Details:**

Remove:
- `String source_` member and `set_source()`/`get_source()` accessors
- `String source_file_` member and `set_source_file()`/`get_source_file()` accessors
- Property bindings for `source` and `source_file`

Add:
- `Ref<NkidoAkkadoSource> akkado_source_` member
- `set_akkado_source(Ref<NkidoAkkadoSource>)` / `get_akkado_source()`
- Property binding: `ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "akkado_source", PROPERTY_HINT_RESOURCE_TYPE, "NkidoAkkadoSource"), ...)`
- In setter: disconnect old source's `changed` signal, connect new source's `changed` signal to `_on_source_changed()`
- `_on_source_changed()`: calls `emit_changed()` to propagate to editor

Update `compile()`:
- Read source from `akkado_source_->get_source_code()` instead of `source_` / file read
- Get filename from `akkado_source_->get_path()` for diagnostics (empty string if embedded)
- Return false with error if `akkado_source_` is null

**Verification:**
- Inspector shows "Akkado Source" property with resource picker (New, Load, Quick Load)
- Create New NkidoAkkadoSource → write code → compile → hear audio
- Right-click → Save As `.akk` → file saved as plain text
- Load existing `.akk` → compile → works
- Set same source on two streams → both compile and play independently

### Phase 4: Update Editor (Bottom Panel, Inspector, Plugin)

**Goal:** Update GDScript editor code to work with NkidoAkkadoSource and add new features.

**Files:**
- Modify `nkido_bottom_panel.gd`
- Modify `nkido_inspector.gd`
- Modify `nkido_plugin.gd`

**Details:**

`nkido_bottom_panel.gd`:
- Read source from `current_stream.akkado_source.source_code` (guard for null)
- On text change: write to `current_stream.akkado_source.source_code = code_edit.text`
- Remove file-path-based read/write logic (no more direct FileAccess for source)
- Show source resource name/path in toolbar (e.g., "ambient.akk" or "[embedded]")

`nkido_inspector.gd`:
- Remove the inline CodeEdit for source editing (the bottom panel handles editing)
- Remove the file picker / Reload button for `source_file`
- The `akkado_source` property automatically gets Godot's standard resource picker UI
- Keep transport controls, parameter UI, status label, compile button

`nkido_plugin.gd`:
- Add `_has_main_screen()` returning false (standard for bottom panel plugins)
- Implement `_handles()` to also return true for NkidoAkkadoSource resources
- When a `.akk` file is double-clicked in the FileSystem dock, `_edit()` is called with the NkidoAkkadoSource — open the bottom panel and load the source for editing
- If editing a standalone `.akk` (not via a stream), disable compile/play/stop buttons since there's no stream context

**Verification:**
- Select AudioStreamPlayer → bottom panel shows source from `akkado_source`
- Edit in bottom panel → `akkado_source.source_code` updates → resource marks as modified
- Two streams share source → bottom panel shows "Shared by 2 streams"
- Double-click `.akk` in FileSystem → bottom panel opens with that source
- Inspector shows standard resource picker for `akkado_source` property

### Phase 5: Update Examples

**Goal:** Update demo scenes to use the new API.

**Files:**
- Modify `example/Main.tscn`, `example/Main.gd`
- Modify `example/SampleDemo.tscn`
- Optionally create `example/audio/*.akk` files for demo sources

**Verification:**
- Example scenes open and run without errors
- Demo audio plays correctly

---

## 7. Edge Cases

### Null Source
- **Situation:** NkidoAudioStream with no `akkado_source` assigned (default state)
- **Behavior:** `compile()` returns false, emits `compilation_finished(false, [{message: "No Akkado source assigned"}])`. Bottom panel shows empty editor with a hint to create or load a source.

### Empty Source Code
- **Situation:** NkidoAkkadoSource exists but `source_code` is empty string
- **Behavior:** `compile()` returns false, emits error "Empty source". Same as current behavior with empty inline source.

### Source Reassignment During Playback
- **Situation:** User changes `akkado_source` on a stream while audio is playing
- **Behavior:** Playback continues with the previously compiled program. The new source must be explicitly compiled. No automatic recompile.

### Saving Embedded Source
- **Situation:** User right-clicks embedded NkidoAkkadoSource and selects "Save As..."
- **Behavior:** Standard Godot resource save dialog opens. File is written as plain Akkado text via ResourceFormatSaver. The resource's path updates to the new file path. All streams referencing this resource now reference the external file.

### Multiple Streams, Independent Compilation
- **Situation:** Streams A and B reference the same NkidoAkkadoSource. Stream A has samples loaded, Stream B does not.
- **Behavior:** Each stream compiles independently with its own VM, sample registry, and BPM. Stream A resolves sample references, Stream B may have unresolved samples. No cross-stream interference. Each stream must call `compile()` explicitly.

### `.akk` File Encoding
- **Situation:** `.akk` file contains non-UTF-8 bytes
- **Behavior:** `FileAccess::get_as_text()` handles encoding. If the text is unparseable, Akkado compiler returns syntax errors as usual.

### Resource Caching
- **Situation:** Two streams load `res://audio/ambient.akk` independently
- **Behavior:** Godot's ResourceLoader caches resources by path. Both streams receive the same NkidoAkkadoSource instance. This is the intended sharing behavior.

### Double-Click Without Stream Context
- **Situation:** User double-clicks a `.akk` file but no AudioStreamPlayer is selected
- **Behavior:** Bottom panel opens in standalone edit mode — code editing works, but Compile/Play/Stop buttons are disabled. Toolbar shows the filename but no stream info.

---

## 8. Testing Strategy

### Phase 1 (Resource Type)
1. Create NkidoAkkadoSource via editor "New Resource" dialog — appears in list
2. Set `source_code` property — multiline text editor works
3. Create from GDScript: `NkidoAkkadoSource.new()` — set and get `source_code`

### Phase 2 (Format Loader/Saver)
1. Write a plain text `.akk` file manually → `load("res://test.akk")` returns NkidoAkkadoSource with correct text
2. Create NkidoAkkadoSource, set code, save to `.akk` → file on disk is plain Akkado text (no Godot headers)
3. Reload `.akk` file → same content
4. `.akk` files appear in FileSystem dock
5. Drag `.akk` onto resource property → assigns NkidoAkkadoSource

### Phase 3 (Stream Integration)
1. Assign NkidoAkkadoSource to stream → compile → play → hear audio
2. `stream.akkado_source = null` → compile returns false with "No source" error
3. Change source code → compile again → hot-swap with crossfade
4. Two streams share same source → both compile and play independently
5. Save embedded source as `.akk` → file contains plain text → stream now references file
6. Diagnostics show correct filename for external `.akk` files, empty for embedded

### Phase 4 (Editor)
1. Select AudioStreamPlayer → bottom panel loads source from `akkado_source`
2. Edit in bottom panel → `akkado_source.source_code` updates
3. Double-click `.akk` in FileSystem dock → bottom panel opens
5. Inspector shows standard resource picker (New, Load, Clear)
6. Right-click resource → Save As... → saves `.akk` file

### Phase 5 (Examples)
1. `example/Main.tscn` opens without errors
2. `example/SampleDemo.tscn` opens without errors
3. Running example scenes produces expected audio output

---

## 9. Godot Shader Parallel Reference

For implementers, here is the direct analogy to Godot's shader system:

| Godot Shaders | Nkido Equivalent |
|---------------|------------------|
| `Shader` (Resource) | `NkidoAkkadoSource` (Resource) |
| `ShaderMaterial` (Resource) | `NkidoAudioStream` (Resource) |
| `.gdshader` file extension | `.akk` file extension |
| `Shader.code` property | `NkidoAkkadoSource.source_code` property |
| `ShaderMaterial.shader` property | `NkidoAudioStream.akkado_source` property |
| Plain GLSL-like text on disk | Plain Akkado text on disk |
| `ResourceFormatLoaderShader` | `NkidoAkkadoSourceFormatLoader` |
| `ResourceFormatSaverShader` | `NkidoAkkadoSourceFormatSaver` |
