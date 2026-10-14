/* MO8 keyboard progression, full RGBA readback and reproducible captures. */
#include "impacto_host.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int save_frame(void* host, uint8_t* pixels, size_t size,
                      int width, int height, const char* path) {
  if (impacto_host_read_frame_rgba(host, pixels, size) != IMPACTO_HOST_OK)
    return 0;
  FILE* fp = fopen(path, "wb");
  if (!fp) return 0;
  fprintf(fp, "P6\n%d %d\n255\n", width, height);
  for (size_t i = 0; i < size; i += 4) fwrite(pixels + i, 1, 3, fp);
  fclose(fp);
  printf("saved %s\n", path);
  return 1;
}

int main(int argc, char** argv) {
  if (argc < 6) {
    fprintf(stderr, "usage: %s data writable root id output [frames] [width] [stop_input_frame]\n", argv[0]);
    return 2;
  }
  const int frames = argc > 6 ? atoi(argv[6]) : 24000;
  const int width = argc > 7 ? atoi(argv[7]) : 1920;
  const int stop_input = argc > 8 ? atoi(argv[8]) : 23600;
  const int height = width * 9 / 16;
  setvbuf(stdout, NULL, _IONBF, 0);
  impacto_host_desc_t desc = {0};
  desc.struct_size = sizeof(desc);
  desc.api_version = IMPACTO_HOST_API_VERSION;
  desc.data_dir_utf8 = argv[1];
  desc.writable_dir_utf8 = argv[2];
  desc.initial_width = width;
  desc.initial_height = height;
  desc.log_level = 5;
  void* host = NULL;
  if (impacto_host_create(&desc, &host) != IMPACTO_HOST_OK) return 1;
  int32_t result = impacto_host_open_game(host, argv[3], argv[4], argv[3]);
  printf("open: %d\n", result);
  if (result != IMPACTO_HOST_OK) { impacto_host_destroy(host); return 1; }
  size_t size = (size_t)width * height * 4;
  uint8_t* pixels = malloc(size);
  if (!pixels) { impacto_host_destroy(host); return 1; }
  /* Optional exact MSB glyph matching. Settle with RETURN released so each
     reference is reviewed at full reveal rather than during rapid fading. */
  const char* review_log = getenv("IMPACTO_REVIEW_LOG");
  const char* review_targets = getenv("IMPACTO_REVIEW_TARGETS");
  FILE* log_reader = review_log ? fopen(review_log, "r") : NULL;
  char targets[4][2048] = {{0}};
  char labels[4][32] = {{0}};
  int captured[4] = {0};
  if (review_targets) {
    FILE* tf = fopen(review_targets, "r");
    if (!tf || !log_reader) return 2;
    for (int i = 0; i < 4; ++i) {
      if (!fgets(targets[i], sizeof targets[i], tf)) return 2;
      char* tab = strchr(targets[i], '\t');
      if (!tab) return 2;
      *tab = 0;
      snprintf(labels[i], sizeof labels[i], "%.31s", targets[i]);
      memmove(targets[i], tab + 1, strlen(tab + 1) + 1);
      targets[i][strcspn(targets[i], "\r\n")] = 0;
    }
    fclose(tf);
  }
  for (int frame = 1; frame <= frames; ++frame) {
    result = impacto_host_tick(host, 16);
    if (result != IMPACTO_HOST_OK) {
      fprintf(stderr, "tick failed at %d: %d\n", frame, result);
      free(pixels); impacto_host_destroy(host); return 1;
    }
    if (log_reader) {
      char line[4096];
      clearerr(log_reader);
      while (fgets(line, sizeof line, log_reader)) {
        if (!strstr(line, "AddString:")) continue;
        for (int i = 0; i < 4; ++i) {
          if (captured[i] || !targets[i][0] || !strstr(line, targets[i])) continue;
          captured[i] = 1;
          impacto_host_send_key(host, 40, 0);
          for (int settle = 0; settle < 240; ++settle) {
            if (impacto_host_tick(host, 16) != IMPACTO_HOST_OK) return 1;
          }
          char path[1024];
          snprintf(path, sizeof path, "%s/stable_%s.ppm", argv[5], labels[i]);
          if (!save_frame(host, pixels, size, width, height, path)) return 1;
          printf("reference %s settled at progression frame %d\n", labels[i], frame);
        }
      }
    }
    if (frame <= stop_input) {
      if (frame % 10 == 0) impacto_host_send_key(host, 40, 1);
      if (frame % 10 == 5) impacto_host_send_key(host, 40, 0);
    }
    if (frame == stop_input + 1) impacto_host_send_key(host, 40, 0);
    if (frame % 60 != 0 && frame != frames) continue;
    uint32_t w, h, stride;
    impacto_host_get_frame_desc(host, &w, &h, &stride);
    if (w != (uint32_t)width || h != (uint32_t)height || stride != w * 4) {
      fprintf(stderr, "unexpected frame layout: %u %u %u\n", w, h, stride);
      free(pixels); impacto_host_destroy(host); return 1;
    }
    result = impacto_host_read_frame_rgba(host, pixels, size);
    if (result != IMPACTO_HOST_OK) {
      free(pixels); impacto_host_destroy(host); return 1;
    }
    long sum = 0, black = 0;
    for (size_t i = 0; i < size; i += 4) {
      int v = pixels[i] + pixels[i + 1] + pixels[i + 2];
      sum += v; black += v < 30;
    }
    printf("frame %5d: mean=%.2f black=%.2f%%\n", frame,
           (double)sum / (width * height * 3), 100.0 * black / (width * height));
    const int portrait_sample = frame % 120 == 0 &&
        ((frame >= 1920 && frame <= 9600) ||
         (frame >= 12000 && frame <= 13680) || frame >= 21600);
    if (frame % 1200 && frame != 4680 && frame != frames && !portrait_sample) continue;
    char path[1024];
    snprintf(path, sizeof(path), "%s/prog_%05d.ppm", argv[5], frame);
    if (!save_frame(host, pixels, size, width, height, path)) return 1;
  }
  if (log_reader) fclose(log_reader);
  if (review_targets) {
    for (int i = 0; i < 4; ++i) if (!captured[i]) {
      fprintf(stderr, "reference %s not captured\n", labels[i]);
      free(pixels); impacto_host_destroy(host); return 1;
    }
  }
  free(pixels);
  impacto_host_destroy(host);
  puts("done");
  return 0;
}
