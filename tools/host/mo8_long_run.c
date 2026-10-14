/* Continuous MO8 soak: normal keyboard choices, full-size captures and
 * progress evidence. All pauses count toward the requested tick budget. */
#define _POSIX_C_SOURCE 200809L
#include "impacto_host.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static int capture(void* host, uint8_t* rgba, uint8_t* rgb, int width,
                   int height, const char* dir, const char* kind, int frame) {
  size_t count = (size_t)width * height;
  if (impacto_host_read_frame_rgba(host, rgba, count * 4) != IMPACTO_HOST_OK)
    return 0;
  for (size_t i = 0; i < count; ++i)
    memcpy(rgb + i * 3, rgba + i * 4, 3);
  char path[1024];
  snprintf(path, sizeof path, "%s/%s_%06d.ppm", dir, kind, frame);
  FILE* out = fopen(path, "wb");
  if (!out) return 0;
  fprintf(out, "P6\n%d %d\n255\n", width, height);
  int ok = fwrite(rgb, 3, count, out) == count;
  if (fclose(out)) ok = 0;
  printf("[longrun] capture frame=%d kind=%s path=%s ok=%d\n",
         frame, kind, path, ok);
  return ok;
}

int main(int argc, char** argv) {
  if (argc != 8) {
    fprintf(stderr, "usage: %s data writable game output frames width log\n", argv[0]);
    return 2;
  }
  int frames = atoi(argv[5]), width = atoi(argv[6]), height = width * 9 / 16;
  if (frames < 1 || width < 640) return 2;
  setvbuf(stdout, NULL, _IONBF, 0);
  impacto_host_desc_t desc = {0};
  desc.struct_size = sizeof desc;
  desc.api_version = IMPACTO_HOST_API_VERSION;
  desc.data_dir_utf8 = argv[1];
  desc.writable_dir_utf8 = argv[2];
  desc.initial_width = width;
  desc.initial_height = height;
  desc.log_level = 5;
  void* host = NULL;
  if (impacto_host_create(&desc, &host) != IMPACTO_HOST_OK) return 1;
  int rc = impacto_host_open_game(host, argv[3], "mo8", argv[3]);
  printf("[longrun] open=%d frames=%d size=%dx%d policy=first-focused-choice\n",
         rc, frames, width, height);
  if (rc != IMPACTO_HOST_OK) { impacto_host_destroy(host); return 1; }
  size_t count = (size_t)width * height;
  uint8_t* rgba = malloc(count * 4);
  uint8_t* rgb = malloc(count * 3);
  /* The supervisor opens this same path before exec and redirects stdout to
   * it.  a+ also makes direct invocations fail cleanly instead of returning
   * with the host engine thread still joinable (which calls terminate()). */
  FILE* log = fopen(argv[7], "a+");
  if (!rgba || !rgb || !log) {
    free(rgb);
    free(rgba);
    if (log) fclose(log);
    impacto_host_destroy(host);
    return 1;
  }
  fseek(log, 0, SEEK_END);
  int lines = 0, choices = 0, selected = 0, last_activity = 0;
  int next_sample = 2400, paused = 0, pause_start = 0, shown = 0;
  int choice_capture_frame = 0;
  int force_skip = getenv("IMPACTO_SOAK_FORCE_SKIP") != NULL;
  int skip_down = 0;
  float audio[32768];
  int last_frame = 0, status = 0;
  int movie_active = 0, movie_capture = 0, movie_starts = 0, movie_ends = 0;
  unsigned long long audio_samples = 0, audio_nonzero = 0, audio_nonfinite = 0;
  double audio_energy = 0.0, audio_peak = 0.0;
  for (int frame = 1; frame <= frames; ++frame) {
    struct timespec tick_start;
    clock_gettime(CLOCK_MONOTONIC, &tick_start);
    last_frame = frame;
    rc = impacto_host_tick(host, 16);
    if (rc != IMPACTO_HOST_OK) {
      printf("[longrun] tick-error frame=%d rc=%d error=%s\n", frame, rc,
             impacto_host_get_last_error(host));
      status = 1; break;
    }
    int64_t got;
    while ((got = impacto_host_read_audio_f32(host, audio, 32768)) > 0) {
      if (movie_active) {
        for (int64_t i = 0; i < got; ++i) {
          ++audio_samples;
          if (!isfinite(audio[i])) ++audio_nonfinite;
          else {
            audio_nonzero += fabs(audio[i]) > 0.00001;
            audio_energy += (double)audio[i] * audio[i];
            if (fabs(audio[i]) > audio_peak) audio_peak = fabs(audio[i]);
          }
        }
      }
    }
    char text[8192];
    clearerr(log);
    while (fgets(text, sizeof text, log)) {
      /* Driver summaries share stdout with engine logs; never parse them as
       * engine events or feed their embedded source text back into stdout. */
      if (strstr(text, "[longrun]")) continue;
      if (strstr(text, "[movietrace] start ")) {
        movie_active = strstr(text, "loop=1") == NULL;
        ++movie_starts; last_activity = frame;
        if (movie_active) {
          paused = 0;
          impacto_host_send_key(host, IMPACTO_HOST_SCANCODE_RETURN, 0);
        }
        printf("[longrun] movie-start frame=%d\n", frame);
      }
      if (strstr(text, "[movietrace] frame ")) {
        movie_capture = 1; last_activity = frame;
      }
      if (strstr(text, "[movietrace] end ")) {
        ++movie_ends;
        printf("[longrun] movie-natural-end frame=%d\n", frame);
      }
      if (strstr(text, "[movietrace] stop ")) {
        movie_active = 0; last_activity = frame;
        printf("[longrun] movie-stop frame=%d\n", frame);
      }
      if (strstr(text, "[texttrace] page 0 AddString:")) {
        ++lines; last_activity = frame;
        if (!movie_active && !paused && frame >= next_sample) {
          paused = 1; pause_start = frame; shown = 0;
          impacto_host_send_key(host, IMPACTO_HOST_SCANCODE_RETURN, 0);
          printf("[longrun] settle-start frame=%d line=%d\n", frame, lines);
        }
      }
      if (strstr(text, "[texttrace] page 0 render:")) {
        if (strstr(text, "state=3 ")) shown = 1;
        else shown = 0;
      }
      if (strstr(text, "[choicetrace] show")) {
        ++choices; last_activity = frame;
        choice_capture_frame = frame + 60;
        impacto_host_send_key(host, IMPACTO_HOST_SCANCODE_RETURN, 0);
        printf("[longrun] choice-open frame=%d ordinal=%d policy=first\n", frame, choices);
      }
      if (strstr(text, "[choicetrace] selected=")) {
        ++selected; last_activity = frame;
        printf("[longrun] choice-confirm frame=%d\n", frame);
      }
    }
    if (movie_capture) {
      if (!capture(host, rgba, rgb, width, height, argv[4], "movie", frame)) {
        status = 1; break;
      }
      movie_capture = 0;
    }
    if (paused && shown && frame - pause_start >= 30) {
      if (!capture(host, rgba, rgb, width, height, argv[4], "stable", frame)) {
        status = 1; break;
      }
      paused = 0; next_sample = frame + 2400;
    } else if (paused && frame - pause_start >= 2400) {
      printf("[longrun] settle-timeout frame=%d elapsed=%d\n", frame, frame - pause_start);
      if (!capture(host, rgba, rgb, width, height, argv[4], "settle_timeout", frame)) {
        status = 1; break;
      }
      paused = 0; next_sample = frame + 2400;
    }
    if (choice_capture_frame && frame >= choice_capture_frame) {
      if (!capture(host, rgba, rgb, width, height, argv[4], "choice", frame)) {
        status = 1; break;
      }
      choice_capture_frame = 0;
      paused = 0;
      next_sample = frame + 2400;
    }
    if (!movie_active && !paused && !choice_capture_frame) {
      if (frame % 10 == 0) impacto_host_send_key(host, IMPACTO_HOST_SCANCODE_RETURN, 1);
      if (frame % 10 == 5) impacto_host_send_key(host, IMPACTO_HOST_SCANCODE_RETURN, 0);
    }
    int want_skip = force_skip && lines > 0 && !paused && !choice_capture_frame;
    if (want_skip != skip_down) {
      impacto_host_send_key(host, IMPACTO_HOST_SCANCODE_LCTRL, want_skip);
      skip_down = want_skip;
    }
    if (frame % 60 == 0 || frame == frames) {
      uint32_t w = 0, h = 0, stride = 0;
      if (impacto_host_get_frame_desc(host, &w, &h, &stride) != IMPACTO_HOST_OK ||
          w != (uint32_t)width || h != (uint32_t)height || stride != w * 4 ||
          impacto_host_read_frame_rgba(host, rgba, count * 4) != IMPACTO_HOST_OK) {
        printf("[longrun] readback-error frame=%d\n", frame);
        status = 1; break;
      }
      uint64_t sum = 0, black = 0, hash = 14695981039346656037ULL;
      for (size_t i = 0; i < count; ++i) {
        int value = rgba[i * 4] + rgba[i * 4 + 1] + rgba[i * 4 + 2];
        sum += value; black += value < 30;
        if (i % 16 == 0) hash = (hash ^ value) * 1099511628211ULL;
      }
      printf("[longrun] frame=%d lines=%d choices=%d selected=%d idle=%d mean=%.2f black=%.2f hash=%016llx\n",
             frame, lines, choices, selected, frame - last_activity,
             (double)sum / (count * 3), 100.0 * black / count,
             (unsigned long long)hash);
      if (frame % 2400 == 0 || frame == frames ||
          (!movie_active && frame - last_activity >= 3600 && frame % 3600 == 0)) {
        const char* kind = !movie_active && frame - last_activity >= 3600 ? "idle" : "prog";
        if (!capture(host, rgba, rgb, width, height, argv[4], kind, frame)) {
          status = 1; break;
        }
        if (!movie_active && frame - last_activity >= 3600)
          printf("[longrun] possible-blocker frame=%d idle=%d lines=%d\n", frame, frame - last_activity, lines);
      }
      if (impacto_host_should_quit(host) != 0) {
        printf("[longrun] quit-before-budget frame=%d\n", frame);
        status = 3; break;
      }
    }
    if (movie_active) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      int64_t elapsed = (now.tv_sec - tick_start.tv_sec) * 1000000000LL +
                        now.tv_nsec - tick_start.tv_nsec;
      if (elapsed < 16000000) {
        struct timespec delay = {0, 16000000 - elapsed};
        nanosleep(&delay, NULL);
      }
    }
  }
  impacto_host_send_key(host, IMPACTO_HOST_SCANCODE_RETURN, 0);
  impacto_host_send_key(host, IMPACTO_HOST_SCANCODE_LCTRL, 0);
  printf("[longrun] finished frame=%d requested=%d lines=%d choices=%d selected=%d status=%d\n",
         last_frame, frames, lines, choices, selected, status);
  printf("[longrun] movies starts=%d natural_ends=%d active=%d audio_samples=%llu nonzero=%llu nonfinite=%llu rms=%.8f peak=%.8f\n",
         movie_starts, movie_ends, movie_active, audio_samples, audio_nonzero,
         audio_nonfinite, audio_samples ? sqrt(audio_energy / audio_samples) : 0.0, audio_peak);
  fclose(log); free(rgb); free(rgba); impacto_host_destroy(host);
  if (status == 0) puts("[longrun] done");
  return status;
}
