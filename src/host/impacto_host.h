#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Embedded host bridge for impacto (MAGES. engine reimplementation).
 *
 * The library runs the engine on one internal thread ("engine thread") and
 * exposes a synchronous, thread-safe C surface for a host application such as
 * the Aether shell:
 *
 *   - create        spawns the engine thread (cheap, nothing is initialised)
 *   - open_game     boots the renderer, Lua profile system and game. Runs on the
 *                   engine thread and blocks until done.
 *   - tick          advances the simulation by delta_ms and renders one frame
 *                   into the renderer's frame surface.
 *   - read_frame_rgba / get_frame_desc
 *                   copy the latest rendered frame (top-down RGBA8888).
 *   - read_audio_f32 / get_audio_format
 *                   drain the engine's mixed stereo float PCM FIFO.
 *   - send_*        inject input as SDL events (safe from any thread).
 *
 * Embedded builds use impacto_host_gpu.h callbacks to render with the host's
 * GPU and publish native textures. No SDL window or graphics context is made;
 * CPU readback is available only for explicit screenshot requests. Standalone
 * host builds may use the offscreen OpenGL backend.
 *
 * The engine state is process-global, so at most one host instance may exist
 * at a time; impacto_host_create fails while another instance is alive.
 * API-level calls other than the input senders must be issued from a single
 * calling thread (they are forwarded to the engine thread and block).
 */

#define IMPACTO_HOST_API_VERSION 0x01000000u

/* SDL scancode values accepted by impacto_host_send_key. The numbers mirror
 * the SDL_Scancode enum, which SDL keeps ABI-stable; they are repeated here
 * so hosts do not need SDL headers. */
enum impacto_host_scancode_t {
  /* Letters A..Z follow USB HID usage codes 0x04..0x1D. */
  IMPACTO_HOST_SCANCODE_A = 4,
  IMPACTO_HOST_SCANCODE_B = 5,
  IMPACTO_HOST_SCANCODE_C = 6,
  IMPACTO_HOST_SCANCODE_D = 7,
  IMPACTO_HOST_SCANCODE_E = 8,
  IMPACTO_HOST_SCANCODE_F = 9,
  IMPACTO_HOST_SCANCODE_G = 10,
  IMPACTO_HOST_SCANCODE_H = 11,
  IMPACTO_HOST_SCANCODE_I = 12,
  IMPACTO_HOST_SCANCODE_J = 13,
  IMPACTO_HOST_SCANCODE_K = 14,
  IMPACTO_HOST_SCANCODE_L = 15,
  IMPACTO_HOST_SCANCODE_M = 16,
  IMPACTO_HOST_SCANCODE_N = 17,
  IMPACTO_HOST_SCANCODE_O = 18,
  IMPACTO_HOST_SCANCODE_P = 19,
  IMPACTO_HOST_SCANCODE_Q = 20,
  IMPACTO_HOST_SCANCODE_R = 21,
  IMPACTO_HOST_SCANCODE_S = 22,
  IMPACTO_HOST_SCANCODE_T = 23,
  IMPACTO_HOST_SCANCODE_U = 24,
  IMPACTO_HOST_SCANCODE_V = 25,
  IMPACTO_HOST_SCANCODE_W = 26,
  IMPACTO_HOST_SCANCODE_X = 27,
  IMPACTO_HOST_SCANCODE_Y = 28,
  IMPACTO_HOST_SCANCODE_Z = 29,
  /* Digits 1..9,0. */
  IMPACTO_HOST_SCANCODE_1 = 30,
  IMPACTO_HOST_SCANCODE_2 = 31,
  IMPACTO_HOST_SCANCODE_3 = 32,
  IMPACTO_HOST_SCANCODE_4 = 33,
  IMPACTO_HOST_SCANCODE_5 = 34,
  IMPACTO_HOST_SCANCODE_6 = 35,
  IMPACTO_HOST_SCANCODE_7 = 36,
  IMPACTO_HOST_SCANCODE_8 = 37,
  IMPACTO_HOST_SCANCODE_9 = 38,
  IMPACTO_HOST_SCANCODE_0 = 39,
  IMPACTO_HOST_SCANCODE_RETURN = 40,
  IMPACTO_HOST_SCANCODE_ESCAPE = 41,
  IMPACTO_HOST_SCANCODE_BACKSPACE = 42,
  IMPACTO_HOST_SCANCODE_TAB = 43,
  IMPACTO_HOST_SCANCODE_SPACE = 44,
  IMPACTO_HOST_SCANCODE_F1 = 58,
  IMPACTO_HOST_SCANCODE_F12 = 69,
  IMPACTO_HOST_SCANCODE_PAGEUP = 75,
  IMPACTO_HOST_SCANCODE_PAGEDOWN = 78,
  IMPACTO_HOST_SCANCODE_END = 77,
  IMPACTO_HOST_SCANCODE_HOME = 74,
  IMPACTO_HOST_SCANCODE_DELETE = 76,
  IMPACTO_HOST_SCANCODE_INSERT = 73,
  IMPACTO_HOST_SCANCODE_LEFT = 80,
  IMPACTO_HOST_SCANCODE_UP = 82,
  IMPACTO_HOST_SCANCODE_RIGHT = 79,
  IMPACTO_HOST_SCANCODE_DOWN = 81,
  IMPACTO_HOST_SCANCODE_LCTRL = 224,
  IMPACTO_HOST_SCANCODE_LSHIFT = 225,
  IMPACTO_HOST_SCANCODE_LALT = 226,
  IMPACTO_HOST_SCANCODE_RCTRL = 228,
  IMPACTO_HOST_SCANCODE_RSHIFT = 229,
  IMPACTO_HOST_SCANCODE_RALT = 230,
};

enum impacto_host_result_t {
  IMPACTO_HOST_OK = 0,
  IMPACTO_HOST_INVALID_ARGUMENT = -1,
  IMPACTO_HOST_INVALID_STATE = -2,
  IMPACTO_HOST_NOT_SUPPORTED = -3,
  IMPACTO_HOST_IO_ERROR = -4,
  IMPACTO_HOST_INTERNAL_ERROR = -5,
};

/* data_dir:      directory containing gamedefinitions.lua, profiles/,
 *                resources/ and shaders/ from the impacto distribution.
 * writable_dir:  directory for generated files, user config and saves.
 * log_level:     Impacto log level (0=off .. 6=trace). */
typedef struct impacto_host_desc_t {
  uint32_t struct_size;
  uint32_t api_version;
  const char* data_dir_utf8;
  const char* writable_dir_utf8;
  uint32_t initial_width;
  uint32_t initial_height;
  uint32_t log_level;
  uint64_t reserved_u64[4];
  void* reserved_ptr[4];
} impacto_host_desc_t;

int32_t impacto_host_create(const impacto_host_desc_t* desc, void** out_host);
void impacto_host_destroy(void* host);

/* Boots the engine for a game. game_id_utf8 selects the game:
 *   - a game id registered in data_dir's gamedefinitions.lua (e.g. "mo8",
 *     "chlcc", "cclcc", "mo6tw", ...);
 *   - "auto": resolve the game from the game root using the standard MAGES.
 *     install layout (<gamedata>/<game-id>/script.*); fails if no registered
 *     game matches;
 *   - "launcher": boot without a game (data-free loop that renders empty
 *     frames; the headless smoke path).
 * gamedata_dir_utf8 may be null/empty to use the game root itself. */
int32_t impacto_host_open_game(void* host, const char* game_root_utf8,
                               const char* game_id_utf8,
                               const char* gamedata_dir_utf8);
int32_t impacto_host_tick(void* host, uint32_t delta_ms);
int32_t impacto_host_set_paused(void* host, int32_t paused);
int32_t impacto_host_should_quit(void* host);

int32_t impacto_host_get_frame_desc(void* host, uint32_t* out_width,
                                    uint32_t* out_height,
                                    uint32_t* out_stride_bytes);
int32_t impacto_host_read_frame_rgba(void* host, void* out_pixels,
                                     size_t out_pixels_size);

int32_t impacto_host_get_audio_format(void* host, uint32_t* out_sample_rate,
                                      uint32_t* out_channel_count);
/* Reads interleaved stereo float samples; returns the number of samples
 * (not frames) written to out_samples, or a negative impacto_host_result_t. */
int64_t impacto_host_read_audio_f32(void* host, float* out_samples,
                                    int64_t capacity_samples);
int32_t impacto_host_get_audio_stats(void* host, uint64_t* out_frames_written,
                                     uint64_t* out_frames_dropped);

int32_t impacto_host_send_key(void* host, uint32_t sdl_scancode, int32_t down);
/* down: 1 pressed, 0 released, -1 move-only (button ignored) */
int32_t impacto_host_send_pointer(void* host, double x, double y,
                                  int32_t button, int32_t down);
int32_t impacto_host_send_wheel(void* host, double dx, double dy);
int32_t impacto_host_send_text(void* host, const char* utf8_text);

/* Presentations scale on the host side, so the engine keeps its design
 * resolution; this call is accepted for API symmetry and remembered for the
 * next open. */
int32_t impacto_host_surface_size(void* host, uint32_t width, uint32_t height);

const char* impacto_host_get_last_error(void* host);
const char* impacto_host_get_renderer_info(void* host);

#ifdef __cplusplus
} /* extern "C" */
#endif
