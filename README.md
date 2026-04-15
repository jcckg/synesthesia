# Synesthesia

Synesthesia is an application designed to provide a real-time visualisation of audio frequencies as colour. Done through an artistic, physics-inspired process mapping our audible frequency range to the visible spectrum of colours.

Synesthesia features two modes of visualisation, one being "ReSyne", allowing you to import audio files (WAV/FLAC/OGG/MP3), visualise the entire song as a gradient (constructed via our mapping process), and export as a .tiff for external editing in applications like Photoshop (allowing for intuitive effects: blacking out w/ shapes -> eq, blur -> reverb). We can also export (our gradient + colour) as a video file on macOS hosts.

<img alt="Synesthesia's ReSyne Window, '4T Recordings' by Four Tet" src="https://github.com/user-attachments/assets/b5b71813-8f61-4ac6-b69f-5b3717b3d37f" />

</div>

### Installation

Synesthesia is automatically built by GitHub Actions, you can download the application from the [Releases](https://github.com/jcckg/synesthesia/releases/) page.

### Manual Building & Running

Synesthesia runs through a universal [BGFX](https://github.com/bkaradzic/bgfx) renderer backend across desktop platforms. To run this project, make sure `cmake` is installed, and clone the repository with submodules so the local third-party dependencies are available:

```sh
# Clone the repository with submodules
git clone --recurse-submodules https://github.com/jcckg/synesthesia
cd synesthesia

# Create (and enter) the build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build .

# Run Synesthesia
./Synesthesia
```

#### Building an App Bundle on macOS

In order to build a macOS Application Bundle, we use the following flags (`-DBUILD_MACOS_BUNDLE`) to enable our app-building option:

```sh
cmake .. -DBUILD_MACOS_BUNDLE=ON
cmake --build .
```

And your `.app` will be in the root of the build directory.

#### Building an Executable on Windows

> **Note**:
> For Windows clients, you must install VS Microsoft C++ Build Tools, [here is a guide](https://github.com/bycloudai/InstallVSBuildToolsWindows?tab=readme-ov-file) for installing and setting up your PATH.

To build a standalone/portable Windows executable, we use the following flags (`-DCMAKE_BUILD_TYPE=Release`) to build:

```sh
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

Your executable will then be placed in the Release folder (placed at the root of your build directory).

### Credits & Footnotes

- GUI built with [Dear ImGui](https://github.com/ocornut/imgui)
- FFT made possible with [KissFFT](https://github.com/mborgerding/kissfft)

> **⚠️ Warning:**<br>
> This application can display rapidly changing colours when multiple frequencies are played. If you have photosensitive epilepsy, I strongly advise against using this application.

> **Note:**
> This application is artistic in nature, and doesn't aim to replicate Synesthesia (Chromesthesia) or either be 100%-scientifically accurate.
