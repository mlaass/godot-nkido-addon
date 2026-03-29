# **Standard Operating Procedure: Godot C++ Extension CI/CD & Distribution**

## **Executive Summary**

When developing C++ extensions for Godot 4+, **the standard distribution method is providing precompiled binaries.** End-users should never be expected to compile the C++ source code on their local machines.

To support cross-platform development without requiring physical Windows, Linux, and macOS hardware, our CI/CD pipeline utilizes GitHub Actions with a Build Matrix to automatically compile the extension for all major desktop platforms simultaneously, bundle them into a Godot-compliant addon folder, and ship them as a GitHub Release.

## **1. GDExtension vs. Custom C++ Modules (Godot 4.6+)**

Before setting up the pipeline, it is important to distinguish between the two ways Godot supports C++ integration, as this pipeline specifically targets **GDExtensions**:

* **GDExtension (Dynamic Libraries):** This is the standard for plugins and addons. Your C++ code is compiled into dynamic libraries (.dll, .so, .dylib). End-users simply drop the addon folder into their project, and Godot loads it at runtime. It **does not** require recompiling the engine.
* **Custom C++ Modules (Built-in):** As noted in the Godot 4.6 documentation, custom modules reside in the modules/ directory of the engine source code. They are compiled directly *into* the Godot executable. While they offer deeper engine integration (like modifying core servers before scene initialization via `preregister_module_types()`), they require the end-user to use a custom build of the Godot engine.

**Best Practice:** Use GDExtension for custom game logic, tools, and typical Asset Library addons. Only use Custom C++ Modules when GDExtension does not suffice and deep engine integration/overrides are strictly required.

## **2. Distribution Strategy: Why Precompiled Binaries?**

Our GDExtension release packages must be "plug and play." Distributing source code for local compilation introduces several critical friction points:

* **Toolchain Dependencies:** It assumes the end-user has a full C++ build environment installed (MSVC/GCC/Clang, Python, SCons). Most game designers and GDScript developers do not.
* **Build Times:** C++ compilation is time-consuming. Precompiling ensures immediate usability.
* **Support Overhead:** Troubleshooting local compilation errors on diverse user environments creates an unsustainable support burden.

## **3. Extension Architecture (The Addon Structure)**

Godot relies on a standard addons/ folder structure and a .gdextension configuration file to resolve which binary to load at runtime.

Your repository should maintain this structure so the CI/CD pipeline can easily package it:

```
my-repository/
├── addons/
│   └── my_extension/
│       ├── my_extension.gdextension  <-- The config file
│       ├── my_node_icon.svg
│       └── bin/                      <-- CI will place compiled binaries here
├── src/                              <-- Your C++ source code
├── godot-cpp/                        <-- Submodule for bindings
└── SConstruct                        <-- Build script
```

**Example .gdextension configuration:**

```ini
[configuration]
entry_symbol = "my_extension_init"
compatibility_minimum = "4.1"

[libraries]
macos.debug = "res://addons/my_extension/bin/my_extension.macos.template_debug.framework"
macos.release = "res://addons/my_extension/bin/my_extension.macos.template_release.framework"
windows.debug.x86_64 = "res://addons/my_extension/bin/my_extension.windows.template_debug.x86_64.dll"
windows.release.x86_64 = "res://addons/my_extension/bin/my_extension.windows.template_release.x86_64.dll"
linux.debug.x86_64 = "res://addons/my_extension/bin/my_extension.linux.template_debug.x86_64.so"
linux.release.x86_64 = "res://addons/my_extension/bin/my_extension.linux.template_release.x86_64.so"
```

## **4. Automated Build & Release Pipeline (GitHub Actions)**

To completely automate the build and distribution of the addon, we use a two-job GitHub Actions workflow:

1. **build**: A matrix job that compiles the C++ code on Windows, Mac, and Linux concurrently.
2. **release**: A packaging job that triggers when a version tag (e.g., v1.0.0) is pushed. It downloads the compiled binaries, places them into the addons/ folder, zips it, and publishes it to GitHub Releases.

Create the following YAML file in the repository at `.github/workflows/build_and_release.yml`:

```yaml
name: Build and Release GDExtension Addon

on:
  push:
    branches: [ main ]
    tags: [ 'v*' ] # Triggers the release job when a tag like v1.0.0 is pushed
  pull_request:
    branches: [ main ]
  workflow_dispatch:

jobs:
  build:
    name: Build for ${{ matrix.platform }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: ubuntu-latest
            platform: linux
          - os: windows-latest
            platform: windows
          - os: macos-latest
            platform: macos

    runs-on: ${{ matrix.os }}

    steps:
      - name: Checkout source code
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.x'

      - name: Install SCons
        run: python -m pip install scons

      - name: Compile Extension (Release)
        run: scons platform=${{ matrix.platform }} target=template_release

      # Temporarily store the compiled binaries from this specific OS
      - name: Upload Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: bin-${{ matrix.platform }}
          path: addons/my_extension/bin/
          retention-days: 1

  release:
    name: Package and Ship Addon
    needs: build # Wait for all OS builds to finish
    runs-on: ubuntu-latest
    if: startsWith(github.ref, 'refs/tags/') # Only run if a tag was pushed

    steps:
      - name: Checkout source code
        uses: actions/checkout@v4

      - name: Download all compiled binaries
        uses: actions/download-artifact@v4
        with:
          path: downloaded_bins

      - name: Assemble Addon Directory
        run: |
          # Copy all downloaded OS binaries into the final addon bin folder
          cp -r downloaded_bins/bin-linux/* addons/my_extension/bin/ || true
          cp -r downloaded_bins/bin-windows/* addons/my_extension/bin/ || true
          cp -r downloaded_bins/bin-macos/* addons/my_extension/bin/ || true

          # Zip the addons folder for distribution
          zip -r my_extension_addon.zip addons/

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: my_extension_addon.zip
          generate_release_notes: true
```

## **5. Developer Workflow**

With this CI/CD process established, the workflow for the development team is streamlined:

1. **Develop:** Write C++ code locally.
2. **Commit & Push:** Push changes to main. GitHub Actions will automatically ensure the code compiles on all platforms via the build matrix, acting as a continuous integration check.
3. **Release:** When the team is ready to publish an update to the Asset Library or users, they create a Git tag (e.g., `git tag v1.2.0` -> `git push origin v1.2.0`).
4. **Ship:** The pipeline will automatically compile the latest binaries, combine them with the `.gdextension` file into an `addons/` zip file, and attach it to a new GitHub Release. Users can then download `my_extension_addon.zip` and extract it directly into their Godot projects.