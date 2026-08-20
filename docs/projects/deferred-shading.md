# Deferred Shading

## Project Goal

This project implements a deferred-shading pipeline in OpenGL. Scene geometry is first written to a G-buffer and then evaluated in screen space. The renderer also includes dynamic spotlights, shadow mapping with 3×3 PCF, SSAO, and debug views for intermediate render targets.

## Code

- [Application and render passes](../../src/EDAN35/assignment2.cpp)
- [G-buffer shaders](../../shaders/EDAN35/fill_gbuffer.frag)
- [Lighting and shadow shader](../../shaders/EDAN35/accumulate_lights.frag)
- [SSAO shader](../../shaders/EDAN35/ssao.frag)
- [SSAO blur shader](../../shaders/EDAN35/ssao_blur.frag)
- [Deferred resolve shader](../../shaders/EDAN35/resolve_deferred.frag)

The project is implemented within the CG_Labs/Bonobo EDAN35 Assignment 2 framework.

## Build

This project is built as part of CG_Labs. Follow the repository setup and Visual Studio instructions in:

- [CG_Labs README](../../README.rst)
- [CG_Labs build guide](../../BUILD.rst)

## Usage

1. Open the CG_Labs repository folder in Visual Studio and wait for **CMake generation finished.**
2. In the target dropdown next to the green Run button, select `src\EDAN35\EDAN35_Assignment2.exe`. Do not select an `(Install)` target.
3. Press `F5` to build and run the project.

The ImGui interface can be used to change the number of lights, pause their animation, configure SSAO, display intermediate render targets, and inspect GPU timings for individual render passes.

| Input | Action |
| --- | --- |
| `R` | Reload shaders |
| `F2` | Show or hide the interface |
| `F3` | Show or hide the log |
