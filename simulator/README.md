# PC Simulator for ESP32-SIP-Voice UI

This folder contains a native PC simulator for the LVGL user interface of the **[ESP32-SIP-Voice](https://github.com/GeorgeBregman/ESP32-SIP-Voice)** project.
It allows you to develop, test, and preview the GUI themes directly on your Windows, Mac, or Linux computer using SDL2, without needing to flash the ESP32 hardware!

## Requirements

You need a standard C build environment (GCC/Clang, CMake) and the SDL2 development libraries.

### Windows (MSYS2 / MinGW-w64)
1. Open the **MSYS2 UCRT64** (or MINGW64) terminal.
2. Install the toolchain and SDL2:
   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
   ```

### Linux (or Windows WSL2)
1. Open your terminal.
2. Install build-essential, CMake, and SDL2:
   ```bash
   sudo apt update
   sudo apt install build-essential cmake libsdl2-dev
   ```

### macOS
1. Open Terminal.
2. Install CMake and SDL2 using Homebrew:
   ```bash
   brew install cmake sdl2
   ```

## Building the Simulator

1. Create a `build` directory and navigate into it:
   ```bash
   mkdir build
   cd build
   ```

2. Run CMake and compile:
   ```bash
   cmake ..
   cmake --build .
   ```

## Running & Testing Themes

You can dynamically test the different UI themes ("Voice Assistant", "Mobile OS", "Smart Speaker") and screen geometries (Rectangular or Circular) without touching the C code. 

Just pass the `SIM_THEME` and `SIM_ROUND` arguments to CMake during the configuration step:

* `SIM_THEME=0` : Voice Assistant (Default)
* `SIM_THEME=1` : Mobile OS
* `SIM_THEME=2` : Smart Speaker

* `SIM_ROUND=0` : Rectangular Display e.g., ST7789 (Default)
* `SIM_ROUND=1` : Circular Display e.g., GC9A01

### Example 1: Testing "Mobile OS" on a standard rectangular screen
```bash
cmake .. -DSIM_THEME=1 -DSIM_ROUND=0
cmake --build .
./simulator
```

### Example 2: Testing "Smart Speaker" on a circular display
```bash
cmake .. -DSIM_THEME=2 -DSIM_ROUND=1
cmake --build .
./simulator
```

The simulator starts on an **incoming call**. Your mouse emulates the touchscreen:
click the **green** button to answer (→ active call) and the **red** button to
hang up (→ clock face). After ~6 s idle it re-arms another incoming call so the
demo keeps cycling.

> When `SIM_ROUND=1`, the SDL window is square (240×240) so the circular themes
> render as a true circle; otherwise it is 240×320 (rectangular). The window is
> drawn at 2× zoom for visibility.

## License
MIT License

## Contact

For questions or support, visit [George Bregman's Website](https://georgebregman.com/).
