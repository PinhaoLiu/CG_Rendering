# EDAN35 Assignment 1: GPU Ray Tracing

This target migrates the Lab1-RayTracing scene to CG_Labs and executes the ray
tracing work in an OpenGL 4.6 compute shader.

## Run-time controls

- `Load OBJ...` opens a native file picker. The selected OBJ is triangulated by
  Assimp, centred, uniformly scaled, converted to GPU primitives, and assigned a
  newly built BVH.
- `Lab scene` and `OBJ model` select two independent scenes. Loading a model does
  not replace the built-in lab scene.
- `Maximum depth` controls reflection/refraction recursion depth.
- `Samples per axis` controls the 1x1, 2x2, or 3x3 regular anti-aliasing grid.
- `Trace again` dispatches the compute shader again.
- `W/S/A/D/Q/E` moves the FPS camera, while dragging with the left mouse button
  changes its orientation. Control slows movement and Shift accelerates it.
- `Reset camera` restores the original Lab1-RayTracing viewpoint.
- `R` reloads shaders, `F2` toggles the UI, and `F3` toggles the log.

An OBJ path can also be supplied as the first command-line argument. Without an
argument, the program starts with only the built-in lab scene; a model can then
be selected through the UI.

## GPU data flow

The CPU creates materials and primitives and builds a median-split AABB BVH.
Those four flat arrays are uploaded once as shader storage buffers. One compute
shader invocation owns one output pixel, traverses the BVH iteratively, traces
shadow/reflection/refraction rays, and writes an `rgba32f` image. A full-screen
triangle displays that image with gamma correction. Scene or quality changes
cause a new dispatch; ordinary UI frames reuse the existing result.
While the camera is moving, the renderer uses a one-sample, one-bounce preview;
the selected depth and anti-aliasing quality are restored as soon as movement
stops.
