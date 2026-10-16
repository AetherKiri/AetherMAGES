# Embedded GPU rendering

Configure with `IMPACTO_BUILD_HOST=ON` and `IMPACTO_EMBEDDED=ON` to use
`IMPACTO_HOST_GPU`. This configuration excludes OpenGL and creates no SDL
window or graphics context. SDL supplies only the event queue. The standalone
host configuration can still use offscreen OpenGL.

Register `impacto_host_gpu_callbacks_t` from `src/host/impacto_host_gpu.h`
before opening a game. The engine submits texture operations, GLSL 450 draw
requests, and ordered batches from its engine thread. Draw pointers are valid
only during the callback: an asynchronous host must copy vertices, uniforms,
and shader sources before returning. Texture release follows submission order.
If allocation waits on a renderer owned by the calling thread, supply `pump`
so synchronous engine calls can service that renderer without deadlocking.

The generated shaders share the standalone shader equations. The renderer
carries sprite and font geometry, masks, blending, scissor and stencil state,
YUV/NV12 movie planes, and 3D materials, skeletal animation, morphs, depth and
outlines. Stencil masks use a separate texture. GPU frame publication copies
the completed target to a distinct texture and reports its handle and serial
through `impacto_host_get_gpu_frame`. Ordinary presentation never reads pixels
back to the CPU. Explicit screenshot APIs may use the optional `read_rgba`
callback.

The host and Aether provider share `DetectGameRoot` in `src/host/gameroot.h`.
It recognizes named profile layouts and distinctive archive combinations,
including MO8's nested `MemoriesoffIF` PC directory. Embedded VFS relocation
preserves standalone profile paths and resolves archive filename case. An
unnamed `script.cls`/`system.cpk` pair cannot distinguish Darling from MO6 and
is deliberately unresolved; use the corresponding profile directory name.

Aether packages engine profiles and resources in `res://mages_runtime/data.zip`
and extracts them into its configured cache before provider creation. Bundled
resource reads remain anchored to `EngineDataRoot` after startup restores the
application working directory. Game archives are supplied separately.

Linux isolates SDL3 symbols as `AetherSDL3_*` with `patchelf` for shared SDL
or `objcopy` for static SDL. Android applies the same static rename before
linking. macOS keeps impacto in a separate dynamic image; iOS merges static
archives after renaming SDL3 and the host's SDL2 separately. No graphics
context crosses the host ABI.

The initial GPU ABI is version 1. Callers must provide its complete layout and
all required operations; batch begin/end callbacks must be supplied together.
An incompatible future layout must use a new version.

Local acceptance in Aether uses provider/ABI lifecycle tests and
`tests/unit-tests/mages_runtime/godot_gpu_smoke.gd` with legally supplied game
data. The Godot smoke checks native presentation, nonempty screenshots,
visible image variation and zero bridge failures. Linux MO8 acceptance covered
4,200 frames at 1920x1080 using Godot's Vulkan RenderingDevice on llvmpipe:
117,893 bridge operations completed, no bridge failures, and no dropped audio
frames. This verifies the RenderingDevice path on a software Vulkan device;
Android/iOS device rendering and other games still require runtime acceptance.
