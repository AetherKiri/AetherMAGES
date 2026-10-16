#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMPACTO_HOST_GPU_API_VERSION 1u
/* Version 1 is introduced with the embedded Godot renderer. Each structure
 * must include every field below; trailing fields may be added by a newer
 * caller, and incompatible layouts require a new API version. */

/* All pointers in a draw request are borrowed for the callback duration. The
 * host copies commands before returning; textures remain owned by impacto
 * until release_texture, in submission order. No graphics context is shared. */
typedef struct impacto_host_gpu_draw_t {
  uint32_t struct_size;
  uint64_t target;
  uint64_t textures[4];
  const char* vertex_source;
  const char* fragment_source;
  const float* vertices;
  uint32_t vertex_float_count;
  uint32_t vertex_count;
  const void* uniforms;
  uint32_t uniform_bytes;
  int32_t clip[4]; /* left, top, right, bottom, in target pixels */
  uint32_t blend; /* 0: normal, 1: additive, 2: premultiplied, 3: replace, 4: 3D alpha */
  uint32_t repeat;
  uint32_t depth_test;
  uint32_t depth_write;
  uint32_t clear_depth;
  uint32_t cull; /* 0: disabled, 1: back, 2: front */
} impacto_host_gpu_draw_t;

typedef struct impacto_host_gpu_callbacks_t {
  uint32_t struct_size;
  uint32_t api_version;
  uint64_t (*create_rgba)(uint32_t width, uint32_t height,
                          const void* pixels, uint32_t stride);
  void (*release_texture)(uint64_t texture);
  int32_t (*update_rgba)(uint64_t texture, const void* pixels, uint32_t stride);
  int32_t (*clear_rgba)(uint64_t texture, uint32_t rgba);
  int32_t (*copy_rgba)(uint64_t target, uint64_t source,
                       uint32_t width, uint32_t height);
  int32_t (*read_rgba)(uint64_t texture, void* pixels, size_t size,
                       uint32_t stride);
  int32_t (*draw)(const impacto_host_gpu_draw_t* request);
  uint64_t (*begin_batch)(void);
  int32_t (*end_batch)(uint64_t token);
  /* Called on the caller thread while waiting for engine work, allowing a
   * single-threaded host renderer to service synchronous GPU allocations. */
  void (*pump)(void);
} impacto_host_gpu_callbacks_t;

/* Register before opening a game. The table is copied into the host instance. */
int32_t impacto_host_set_gpu_callbacks(
    void* host, const impacto_host_gpu_callbacks_t* callbacks);
int32_t impacto_host_get_gpu_frame(void* host, uint64_t* texture,
                                   uint32_t* width, uint32_t* height,
                                   uint64_t* serial);

#ifdef __cplusplus
}
#endif
