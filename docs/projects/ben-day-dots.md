# Ben-Day Halftone Rendering

## Project Goal

This project implements real-time Ben-Day dot rendering as an OpenGL full-screen post-process. A textured Sponza scene is rendered into colour, depth, and world-space normal targets before the halftone pass is applied.

The effect supports monochrome ink, RGB dots on black, and CMY dots on white. It also includes independent channel angles, dot sizes and registration offsets, tone controls, posterization, combined depth-and-normal Sobel outlines, and Perlin-noise paper and ink variation.

## Code

- [Project source](../../src/BenDayDots)
- [Application and ImGui controls](../../src/BenDayDots/app/BenDayApplication.cpp)
- [Scene renderer and render targets](../../src/BenDayDots/rendering)
- [Ben-Day post-process shader](../../shaders/BenDayDots/benday.frag)
- [Scene shaders](../../shaders/BenDayDots/scene.vert)
- [Project design notes](../../bendaydots.md)

The project uses CG_Labs/Bonobo for window creation, input, camera control, ImGui, Assimp scene loading, shader management, and OpenGL setup.

## Build

This project is built as part of CG_Labs. Follow the repository setup and Visual Studio instructions in:

- [CG_Labs README](../../README.rst)
- [CG_Labs build guide](../../BUILD.rst)

## Usage

1. Open the CG_Labs repository folder in Visual Studio and wait for **CMake generation finished.**
2. In the target dropdown next to the green Run button, select `src\BenDayDots\BenDay_Dots.exe`. Do not select an `(Install)` target.
3. Press `F5` to build and run the project.

Use the ImGui interface to select a colour mode, configure the halftone grid and channels, adjust tone mapping, enable posterization or Sobel outlines, and control the Perlin-noise paper and ink texture. The interface also displays the current FPS and frame time.

| Input | Action |
| --- | --- |
| `W/A/S/D` and `Q/E` | Move the camera |
| Drag with the left mouse button | Rotate the camera |
| `Left Shift` / `Left Ctrl` | Move faster / slower |
| `F2` | Show or hide the interface |
| `F3` | Show or hide the log |
| `F5` | Reload shaders |
| `F11` | Toggle fullscreen |
