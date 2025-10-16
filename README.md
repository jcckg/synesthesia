# Synesthesia

Synesthesia is an application which provides a real-time visualisation of audio frequencies represented as colour. Features a rudimentary EQ, real-time frequency & wavelength information, and an audio input selection system.

### Installation

Synesthesia is automatically built by GitHub Actions, you can download the application from the [Releases](https://github.com/jcckg/synesthesia/releases/) page.

### Manual Building & Running

Synesthesia runs on Windows (DirectX 12) and macOS (Metal). To run this project, make sure `cmake` is installed, and just clone this repository with submodules (we build app dependencies locally):

> **Note**:
> For Windows clients, you must install VS Microsoft C++ Build Tools, [here is a guide](https://github.com/bycloudai/InstallVSBuildToolsWindows?tab=readme-ov-file) for installing and setting up your PATH.

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
./synesthesia
```

#### Building an App Bundle on macOS

In order to build a macOS Application Bundle, we use the following flags (`-DBUILD_MACOS_BUNDLE`) to enable our app-building option:

```sh
cmake .. -DBUILD_MACOS_BUNDLE=ON
cmake --build .
```

And your `.app` will be in the root of the build directory.

#### Building an Executable on Windows

To build a standalone/portable Windows executable, we use the following flags (`-DCMAKE_BUILD_TYPE=Release`) to build:

```sh
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

Your executable will then be placed in the Release folder (placed at the root of your build directory).

### Video Demo

https://github.com/user-attachments/assets/f2d9a25c-81e7-4976-b707-c5cdb479754d

### Credits & Footnotes

- GUI built with [Dear ImGui](https://github.com/ocornut/imgui)
- FFT made possible with [KissFFT](https://github.com/mborgerding/kissfft)

> **⚠️ Warning:**<br>
> This application can display rapidly changing colours when multiple frequencies are played. If you have photosensitive epilepsy, I strongly advise against using this application.

> **Note:**
> This application is artistic in nature, and doesn't aim to replicate Synesthesia (Chromesthesia) or either be 100%-scientifically accurate.
