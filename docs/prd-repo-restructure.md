# PRD: Repository Restructure — Git Submodules, CI/CD, and Distribution

> **Status: NOT STARTED**

**Date:** 2026-03-31
**Prerequisites:**
- v2 features complete (Phases 4–7 from `prd-nkido-v2.md`)
- Enkido repo made public on GitHub

---

## 1. Overview

### 1.1 Context

The Nkido GDExtension currently depends on two external C++ repositories — **godot-cpp** and **enkido** (Cedar + Akkado) — which must exist as sibling directories on disk (`../godot-cpp`, `../enkido`). The CI workflows check out each dependency separately using `actions/checkout`, and enkido requires a `ENKIDO_TOKEN` secret because it is a private repository.

This works but has several problems:
- **Local setup is fragile** — developers must manually clone sibling repos at the correct paths and tags
- **CI duplicates dependency management** — each workflow re-specifies repository URLs, refs, and checkout paths
- **No self-contained build** — cloning the repo alone is not enough to build; you need external knowledge of dependencies
- **No standard distribution** — users must download release zips manually; no Godot AssetLib presence

### 1.2 Goals

- **Self-contained repository**: Add godot-cpp and enkido as git submodules so cloning with `--recurse-submodules` gives you everything needed to build
- **Simplified CI**: Workflows use submodule checkout instead of separate repo checkouts; no more `ENKIDO_TOKEN` once enkido is public
- **macOS universal binaries**: Produce arm64 + x86_64 fat binaries for macOS
- **Build script**: Provide `build.sh` / `build.bat` so developers can build from source with a single command
- **Godot AssetLib**: Package the addon for submission to the Godot Asset Library
- **Updated godot-cpp**: Pin to `godot-4.6-stable` (currently `godot-4.4-stable`)

### 1.3 Non-Goals

- Changing the C++ source code or extension API
- Web/HTML5 or mobile platform builds
- Automated AssetLib submission (packaging only — submission is manual)
- ARM Linux builds
- Changing the addon directory structure (`addons/nkido/`)
- Automated version bumping

---

## 2. Current State vs Proposed

| Aspect | Current | Proposed |
|--------|---------|----------|
| Dependencies | Sibling directories (`../godot-cpp`, `../enkido`) | Git submodules at `thirdparty/godot-cpp`, `thirdparty/enkido` |
| Enkido access | Private repo, `ENKIDO_TOKEN` secret | Public repo, no auth needed |
| godot-cpp version | `godot-4.4-stable` | `godot-4.6-stable` |
| CI checkout | 3 separate `actions/checkout` steps | Single checkout with `submodules: recursive` |
| macOS binary | Single-arch (runner native) | Universal binary (arm64 + x86_64 via `lipo`) |
| Build from source | Manual cmake invocation, know correct paths | `./build.sh` or `build.bat` one-liner |
| Distribution | GitHub Release zip only | GitHub Release zip + Godot AssetLib package |
| Release contents | Release binaries only | Release binaries (debug builds for CI validation only) |
| AssetLib | Not present | Addon + examples packaged for AssetLib |

---

## 3. Repository Structure (Post-Restructure)

```
godot-nkido-addon/
├── thirdparty/
│   ├── godot-cpp/          # git submodule → godotengine/godot-cpp @ godot-4.6-stable
│   └── enkido/             # git submodule → mlaass/enkido @ <pinned-tag>
├── addons/nkido/
│   ├── src/                # C++ extension sources (unchanged)
│   ├── bin/                # Compiled binaries (gitignored)
│   ├── nkido.gdextension
│   ├── plugin.cfg
│   ├── nkido_plugin.gd
│   ├── nkido_bottom_panel.gd
│   └── nkido_inspector.gd
├── example/                # Demo scenes and scripts
├── docs/                   # PRDs and design docs
├── .github/workflows/
│   ├── build.yml           # PR/push CI (updated)
│   └── release.yml         # Tag-triggered release (updated)
├── CMakeLists.txt          # Updated paths
├── build.sh                # New — Linux/macOS build script
├── build.bat               # New — Windows build script
├── .gitmodules             # New — submodule definitions
└── project.godot
```

---

## 4. Technical Design

### 4.1 Git Submodules

**.gitmodules** file:

```ini
[submodule "thirdparty/godot-cpp"]
    path = thirdparty/godot-cpp
    url = https://github.com/godotengine/godot-cpp.git

[submodule "thirdparty/enkido"]
    path = thirdparty/enkido
    url = https://github.com/mlaass/enkido.git
```

Submodules are pinned to specific commits (tags):
- `godot-cpp` → `godot-4.6-stable` tag
- `enkido` → latest stable tag at time of setup

### 4.2 CMakeLists.txt Changes

Update default dependency paths from sibling directories to submodule paths:

```cmake
# Before
set(GODOT_CPP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../godot-cpp"
    CACHE PATH "Path to godot-cpp")
set(ENKIDO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../enkido"
    CACHE PATH "Path to enkido (cedar + akkado)")

# After
set(GODOT_CPP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/godot-cpp"
    CACHE PATH "Path to godot-cpp")
set(ENKIDO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/enkido"
    CACHE PATH "Path to enkido (cedar + akkado)")
```

No other changes to CMakeLists.txt are needed — all source paths and include directories already use `${GODOT_CPP_PATH}` and `${ENKIDO_PATH}` variables.

### 4.3 Build Scripts

**build.sh** (Linux / macOS):

```bash
#!/usr/bin/env bash
set -euo pipefail

# Initialize submodules if needed
git submodule update --init --recursive

BUILD_TYPE="${1:-Release}"
cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo "Build complete: addons/nkido/bin/"
```

**build.bat** (Windows):

```batch
@echo off
git submodule update --init --recursive

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release

cmake -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
cmake --build build --config %BUILD_TYPE% -j%NUMBER_OF_PROCESSORS%

echo Build complete: addons\nkido\bin\
```

### 4.4 CI Workflow Updates

#### build.yml (PR / push validation)

Key changes:
- Replace 3 checkout steps with single checkout + `submodules: recursive`
- Update godot-cpp ref to `godot-4.6-stable`
- Remove `ENKIDO_TOKEN` usage (after enkido is public)
- Add macOS universal binary step

```yaml
name: Build

on:
  push:
    branches: [master]
  pull_request:
    branches: [master]

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: ubuntu-latest
            build_type: Debug
          - os: ubuntu-latest
            build_type: Release
          - os: windows-latest
            build_type: Debug
          - os: windows-latest
            build_type: Release
          - os: macos-latest
            build_type: Debug
          - os: macos-latest
            build_type: Release

    runs-on: ${{ matrix.os }}
    name: ${{ matrix.os }} (${{ matrix.build_type }})

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=${{ matrix.build_type }}

      - name: Build
        run: cmake --build build --config ${{ matrix.build_type }} -j2

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: nkido-${{ matrix.os }}-${{ matrix.build_type }}
          path: addons/nkido/bin/
          retention-days: 14
```

#### release.yml (Tag-triggered release)

Key changes:
- Submodule-based checkout
- macOS builds both arm64 and x86_64, then combines with `lipo`
- Assemble step includes example/ directory for AssetLib
- Produces two zip files: one for GitHub Release, one structured for AssetLib

```yaml
name: Release

on:
  push:
    tags:
      - "v*"

jobs:
  build-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - run: cmake -B build -DCMAKE_BUILD_TYPE=Release
      - run: cmake --build build --config Release -j2
      - uses: actions/upload-artifact@v4
        with:
          name: nkido-linux
          path: addons/nkido/bin/

  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - run: cmake -B build -DCMAKE_BUILD_TYPE=Release
      - run: cmake --build build --config Release -j2
      - uses: actions/upload-artifact@v4
        with:
          name: nkido-windows
          path: addons/nkido/bin/

  build-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Build arm64
        run: |
          cmake -B build-arm64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
          cmake --build build-arm64 --config Release -j$(sysctl -n hw.logicalcpu)

      - name: Build x86_64
        run: |
          cmake -B build-x86_64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
          cmake --build build-x86_64 --config Release -j$(sysctl -n hw.logicalcpu)

      - name: Create universal binary
        run: |
          mkdir -p addons/nkido/bin
          lipo -create \
            build-arm64/addons/nkido/bin/*.dylib \
            build-x86_64/addons/nkido/bin/*.dylib \
            -output addons/nkido/bin/libnkido.macos.template_release.universal.dylib

      - uses: actions/upload-artifact@v4
        with:
          name: nkido-macos
          path: addons/nkido/bin/

  release:
    needs: [build-linux, build-windows, build-macos]
    runs-on: ubuntu-latest
    permissions:
      contents: write

    steps:
      - uses: actions/checkout@v4

      - name: Download all artifacts
        uses: actions/download-artifact@v4
        with:
          path: artifacts

      - name: Assemble addon
        run: |
          mkdir -p dist/addons/nkido/bin
          # Addon files
          cp -r addons/nkido/*.gd dist/addons/nkido/
          cp addons/nkido/plugin.cfg dist/addons/nkido/
          cp addons/nkido/nkido.gdextension dist/addons/nkido/
          # Platform binaries
          cp -r artifacts/nkido-linux/* dist/addons/nkido/bin/ || true
          cp -r artifacts/nkido-windows/* dist/addons/nkido/bin/ || true
          cp -r artifacts/nkido-macos/* dist/addons/nkido/bin/ || true
          # Example scenes
          cp -r example dist/example/
          # Create release zip
          cd dist && zip -r ../nkido-${{ github.ref_name }}.zip addons/ example/

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: nkido-${{ github.ref_name }}.zip
          generate_release_notes: true
```

### 4.5 macOS Universal Binary

The macOS job builds twice — once for `arm64`, once for `x86_64` — then combines using `lipo`. This produces a fat binary that runs natively on both Intel and Apple Silicon Macs.

CMake's `CMAKE_OSX_ARCHITECTURES` variable controls the target architecture. The `lipo -create` command merges the two single-arch dylibs into one universal dylib.

The output filename must match the `.gdextension` manifest entry:
```
macos.release = "res://addons/nkido/bin/libnkido.macos.template_release.universal.dylib"
```

### 4.6 Godot AssetLib Packaging

The Godot Asset Library requires a downloadable zip with a specific structure. The zip root must contain the addon directory that gets installed into the user's project.

**AssetLib zip structure:**

```
nkido-v1.0.0.zip
├── addons/
│   └── nkido/
│       ├── bin/
│       │   ├── libnkido.linux.template_release.x86_64.so
│       │   ├── nkido.windows.template_release.x86_64.dll
│       │   └── libnkido.macos.template_release.universal.dylib
│       ├── nkido.gdextension
│       ├── plugin.cfg
│       ├── nkido_plugin.gd
│       ├── nkido_bottom_panel.gd
│       └── nkido_inspector.gd
└── example/
    ├── Main.tscn
    └── Main.gd
```

This is the same zip the release workflow already produces. AssetLib submission points to the GitHub Release zip URL. The install path is configured during AssetLib submission (set to project root so `addons/nkido/` lands in the right place).

**AssetLib metadata** (configured manually on assetlib.godotengine.org):
- **Category**: Tools
- **Godot version**: 4.6+
- **License**: (project license)
- **Download URL**: Points to latest GitHub Release zip
- **Icon URL**: Points to a hosted icon image

---

## 5. Impact Assessment

| Component | Status | Notes |
|-----------|--------|-------|
| `addons/nkido/src/*.cpp/h` | **Stays** | No changes to C++ sources |
| `addons/nkido/*.gd` | **Stays** | No changes to GDScript files |
| `addons/nkido/nkido.gdextension` | **Stays** | Binary paths already correct |
| `addons/nkido/plugin.cfg` | **Stays** | No changes |
| `CMakeLists.txt` | **Modified** | Default paths change from `../` to `thirdparty/` |
| `.github/workflows/build.yml` | **Modified** | Submodule checkout, remove token, update godot-cpp ref |
| `.github/workflows/release.yml` | **Modified** | Submodule checkout, macOS universal, AssetLib zip |
| `.gitignore` | **Modified** | No new entries needed (bin/ already ignored) |
| `.gitmodules` | **New** | Submodule definitions |
| `build.sh` | **New** | Linux/macOS build script |
| `build.bat` | **New** | Windows build script |
| `thirdparty/godot-cpp/` | **New** | Git submodule |
| `thirdparty/enkido/` | **New** | Git submodule |

---

## 6. File-Level Changes

| File | Change |
|------|--------|
| `CMakeLists.txt` | Update `GODOT_CPP_PATH` default to `thirdparty/godot-cpp`, `ENKIDO_PATH` to `thirdparty/enkido` |
| `.github/workflows/build.yml` | Replace 3 checkout steps with `submodules: recursive`; remove `ENKIDO_TOKEN`; keep debug+release matrix for validation |
| `.github/workflows/release.yml` | Submodule checkout; split macOS into arm64+x86_64 builds with `lipo` step; include `example/` in release zip |
| `.gitmodules` | New file defining `thirdparty/godot-cpp` and `thirdparty/enkido` submodules |
| `build.sh` | New file — bash script: init submodules, configure, build |
| `build.bat` | New file — batch script: init submodules, configure, build |

---

## 7. Implementation Phases

### Phase 1: Transition — Submodules with Token Fallback

**Goal:** Add submodules and update CMake/CI while enkido is still private.

1. Add git submodules:
   ```bash
   git submodule add https://github.com/godotengine/godot-cpp.git thirdparty/godot-cpp
   cd thirdparty/godot-cpp && git checkout godot-4.6-stable && cd ../..
   git submodule add https://github.com/mlaass/enkido.git thirdparty/enkido
   ```

2. Update `CMakeLists.txt` default paths to `thirdparty/`

3. Update `.github/workflows/build.yml`:
   - Use `submodules: recursive` for checkout
   - Keep `ENKIDO_TOKEN` in the checkout step as a fallback for the private submodule:
     ```yaml
     - uses: actions/checkout@v4
       with:
         submodules: recursive
         token: ${{ secrets.ENKIDO_TOKEN }}
     ```
   - Remove the separate godot-cpp and enkido checkout steps
   - Remove `-DGODOT_CPP_PATH` and `-DENKIDO_PATH` overrides from cmake configure (defaults now correct)

4. Update `.github/workflows/release.yml` similarly

5. Create `build.sh` and `build.bat`

**Verification:**
- `git clone --recurse-submodules` populates `thirdparty/` directories
- `./build.sh` succeeds on Linux
- CI build matrix passes (all 6 configs)
- Local build with old sibling paths still works via `-D` overrides

### Phase 2: Enkido Goes Public

**Goal:** Remove auth requirements once enkido repo is made public.

1. Make enkido repo public on GitHub

2. Update `.github/workflows/build.yml`:
   - Remove `token: ${{ secrets.ENKIDO_TOKEN }}` from checkout step
   ```yaml
   - uses: actions/checkout@v4
     with:
       submodules: recursive
   ```

3. Update `.github/workflows/release.yml` similarly

4. Optionally remove `ENKIDO_TOKEN` secret from GitHub repo settings

**Verification:**
- CI builds pass without `ENKIDO_TOKEN`
- Fresh clone with `--recurse-submodules` works without auth (test from a different machine/account)

### Phase 3: macOS Universal Binary

**Goal:** Produce fat arm64+x86_64 binaries for macOS.

1. Update `release.yml` macOS job:
   - Build arm64 (`-DCMAKE_OSX_ARCHITECTURES=arm64`)
   - Build x86_64 (`-DCMAKE_OSX_ARCHITECTURES=x86_64`)
   - Combine with `lipo -create`

2. Update `build.yml` macOS jobs if universal builds are desired for CI validation (optional — single-arch is sufficient for CI)

**Verification:**
- `file` command on the output dylib shows `Mach-O universal binary with 2 architectures: [x86_64:Mach-O 64-bit dynamically linked shared library x86_64] [arm64]`
- Binary loads in Godot on both Intel and Apple Silicon Macs

### Phase 4: AssetLib Packaging and Release

**Goal:** Structure the release zip for Godot AssetLib compatibility.

1. Update `release.yml` assemble step to include `example/` directory in the zip

2. Verify zip structure matches AssetLib requirements:
   - `addons/nkido/` at zip root
   - All platform binaries in `addons/nkido/bin/`
   - `example/` at zip root alongside `addons/`

3. Submit to Godot AssetLib (manual):
   - Create account on assetlib.godotengine.org
   - Submit addon with GitHub Release zip URL
   - Set install path to project root
   - Set Godot version to 4.6+

**Verification:**
- Download release zip, extract into a fresh Godot 4.6 project
- `addons/nkido/` appears in project, plugin activates
- Extension loads on Linux, Windows, and macOS
- Example scene runs

---

## 8. Edge Cases

### 8.1 Submodule Not Initialized

**Situation:** Developer clones without `--recurse-submodules`, `thirdparty/` directories are empty.
**Expected behavior:** `build.sh` / `build.bat` runs `git submodule update --init --recursive` automatically before building. CMake will fail with a clear error if submodules are missing and the build script was not used.

### 8.2 Stale Submodule Versions

**Situation:** Developer has an old submodule checkout that doesn't match the pinned commit.
**Expected behavior:** `build.sh` runs `git submodule update --init --recursive` which updates to the pinned commit. CI always gets the correct version because it does a fresh checkout.

### 8.3 macOS lipo Failure

**Situation:** One of the two architecture builds fails, leaving only one dylib for `lipo`.
**Expected behavior:** `lipo -create` fails and the CI job fails. The release is not created because the `release` job depends on `build-macos` succeeding.

### 8.4 godot-cpp Version Drift

**Situation:** A new Godot 4.x stable release ships and the submodule still pins to `godot-4.6-stable`.
**Expected behavior:** The submodule pin is a commit hash. Update it manually when ready to support a newer Godot version by checking out the new tag in the submodule and committing the updated reference.

### 8.5 Backward Compatibility — Old Sibling Paths

**Situation:** Developer has existing sibling directory setup (`../godot-cpp`, `../enkido`).
**Expected behavior:** Still works — pass `-DGODOT_CPP_PATH=../godot-cpp -DENKIDO_PATH=../enkido` to cmake. The CMake variables are `CACHE PATH` so command-line overrides take precedence over defaults.

### 8.6 AssetLib Zip Structure Mismatch

**Situation:** AssetLib install puts files in the wrong location.
**Expected behavior:** The zip contains `addons/nkido/` at the root. During AssetLib submission, the "install path" is set to the project root (`.`). This ensures `addons/nkido/` lands in the correct location in the user's project.

---

## 9. Testing / Verification Strategy

### Per-Phase Verification

| Phase | Test | Expected Result |
|-------|------|-----------------|
| 1 | `git clone --recurse-submodules <repo>` | `thirdparty/godot-cpp/` and `thirdparty/enkido/` populated |
| 1 | `./build.sh` on Linux | Binary at `addons/nkido/bin/libnkido.linux.template_release.x86_64.so` |
| 1 | CI push to branch | All 6 matrix jobs pass |
| 2 | Clone from a fresh account (no token) | Submodules clone without auth errors |
| 2 | CI build without `ENKIDO_TOKEN` | All jobs pass |
| 3 | `file addons/nkido/bin/*.dylib` | Shows `universal binary with 2 architectures` |
| 3 | Load in Godot on Apple Silicon Mac | Extension loads, audio plays |
| 4 | `unzip nkido-v*.zip -d test-project/` | `test-project/addons/nkido/` has all files + binaries |
| 4 | Open test-project in Godot 4.6 | Plugin activates, NkidoAudioStream available |
| 4 | Run example scene | Audio synthesis works |

### End-to-End Smoke Test

1. Tag a release: `git tag v0.2.0 && git push --tags`
2. Wait for release workflow to complete
3. Download the release zip from GitHub Releases
4. Create a new Godot 4.6 project
5. Extract zip into the project root
6. Open project in Godot editor
7. Enable the Nkido plugin in Project Settings → Plugins
8. Open `example/Main.tscn` and press Play
9. Verify audio output on Linux, Windows, and macOS
