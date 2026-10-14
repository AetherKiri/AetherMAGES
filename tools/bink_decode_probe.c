/* Real-asset software decode regression. Build with the overlay's pkg-config
 * flags for libavformat libavcodec libavutil libswscale. No proprietary assets
 * are included. Hashes cover visible Y/U/V rows, never stride padding. */
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int video_frames, audio_frames, bad_samples;
static uint64_t audio_samples;
static double audio_energy, audio_peak;
static const char *outdir;

static int observe(AVFrame *f, enum AVMediaType type) {
  if (type == AVMEDIA_TYPE_AUDIO) {
    const int planar = av_sample_fmt_is_planar(f->format);
    if (av_get_packed_sample_fmt(f->format) != AV_SAMPLE_FMT_FLT) return -1;
    for (int i = 0; i < f->nb_samples; ++i) {
      for (int ch = 0; ch < f->ch_layout.nb_channels; ++ch) {
        const float *data = (const float *)f->extended_data[planar ? ch : 0];
        const double v = data[planar ? i : i * f->ch_layout.nb_channels + ch];
        if (!isfinite(v)) ++bad_samples;
        else { audio_energy += v * v; audio_peak = fmax(audio_peak, fabs(v)); }
        ++audio_samples;
      }
    }
    ++audio_frames;
    return 0;
  }
  ++video_frames;
  if (f->format != AV_PIX_FMT_YUV420P) return -1;
  if (video_frames != 1 && video_frames % 150) return 0;
  uint64_t hash = 14695981039346656037ULL;
  for (int p = 0; p < 3; ++p) {
    const int w = p ? (f->width + 1) / 2 : f->width;
    const int h = p ? (f->height + 1) / 2 : f->height;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        hash = (hash ^ f->data[p][y * f->linesize[p] + x]) * 1099511628211ULL;
  }
  printf("[decodeprobe] frame n=%d pts=%lld size=%dx%d yuv_hash=%016llx\n",
         video_frames, (long long)f->pts, f->width, f->height, (unsigned long long)hash);
  if (outdir && (video_frames == 150 || video_frames == 900 ||
                 video_frames == 1800 || video_frames == 3000)) {
    struct SwsContext *sws = sws_getContext(f->width, f->height, f->format,
        f->width, f->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
    uint8_t *rgb = malloc((size_t)f->width * f->height * 3);
    if (!sws || !rgb) { free(rgb); sws_freeContext(sws); return -1; }
    uint8_t *dst[] = {rgb, NULL, NULL, NULL};
    int stride[] = {f->width * 3, 0, 0, 0};
    int rows = sws_scale(sws, (const uint8_t *const *)f->data, f->linesize,
                         0, f->height, dst, stride);
    char path[4096];
    snprintf(path, sizeof path, "%s/decode_%06d.ppm", outdir, video_frames);
    FILE *out = fopen(path, "wb");
    int ok = out && rows == f->height;
    if (out) {
      fprintf(out, "P6\n%d %d\n255\n", f->width, f->height);
      ok &= fwrite(rgb, (size_t)f->width * 3, f->height, out) == (size_t)f->height;
      ok &= fclose(out) == 0;
    }
    free(rgb); sws_freeContext(sws);
    if (!ok) return -1;
  }
  return 0;
}

static int drain(AVCodecContext *ctx, AVFrame *frame) {
  int err;
  while ((err = avcodec_receive_frame(ctx, frame)) >= 0) {
    int ok = observe(frame, ctx->codec_type);
    av_frame_unref(frame);
    if (ok < 0) return AVERROR_INVALIDDATA;
  }
  return err == AVERROR(EAGAIN) || err == AVERROR_EOF ? 0 : err;
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) return 2;
  setvbuf(stdout, NULL, _IONBF, 0);
  outdir = argc == 3 ? argv[2] : NULL;
  AVFormatContext *format = NULL;
  AVCodecContext **contexts = NULL;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  int status = 1, err, video_id = -1, audio_id = -1;
  if (!pkt || !frame || avformat_open_input(&format, argv[1], NULL, NULL) < 0) goto cleanup;
  /* Avoid find_stream_info's decoder warming; each packet is decoded once. */
  contexts = calloc(format->nb_streams, sizeof *contexts);
  if (!contexts) goto cleanup;
  for (unsigned i = 0; i < format->nb_streams; ++i) {
    AVCodecParameters *par = format->streams[i]->codecpar;
    if (par->codec_type != AVMEDIA_TYPE_VIDEO && par->codec_type != AVMEDIA_TYPE_AUDIO) continue;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec || !(contexts[i] = avcodec_alloc_context3(codec)) ||
        avcodec_parameters_to_context(contexts[i], par) < 0 ||
        avcodec_open2(contexts[i], codec, NULL) < 0) goto cleanup;
    if (par->codec_type == AVMEDIA_TYPE_VIDEO) video_id = i;
    else audio_id = i;
    printf("[decodeprobe] stream=%u codec=%s size=%dx%d rate=%d channels=%d duration=%lld\n",
           i, codec->name, par->width, par->height, par->sample_rate,
           par->ch_layout.nb_channels, (long long)format->streams[i]->duration);
  }
  while ((err = av_read_frame(format, pkt)) >= 0) {
    AVCodecContext *ctx = contexts[pkt->stream_index];
    if (ctx && (avcodec_send_packet(ctx, pkt) < 0 || drain(ctx, frame) < 0)) goto cleanup;
    av_packet_unref(pkt);
  }
  if (err != AVERROR_EOF) goto cleanup;
  for (unsigned i = 0; i < format->nb_streams; ++i) {
    if (contexts[i] && (avcodec_send_packet(contexts[i], NULL) < 0 || drain(contexts[i], frame) < 0)) goto cleanup;
  }
  if (video_id < 0) goto cleanup;
  const int64_t expected = format->streams[video_id]->duration;
  printf("[decodeprobe] done video_frames=%d expected=%lld audio_frames=%d audio_samples=%llu rms=%.8f peak=%.8f nonfinite=%d\n",
         video_frames, (long long)expected, audio_frames, (unsigned long long)audio_samples,
         audio_samples ? sqrt(audio_energy / audio_samples) : 0.0, audio_peak, bad_samples);
  status = video_frames != expected || bad_samples ||
           (audio_id >= 0 && !audio_samples);
cleanup:
  if (format && contexts)
    for (unsigned i = 0; i < format->nb_streams; ++i) avcodec_free_context(&contexts[i]);
  free(contexts); av_packet_free(&pkt); av_frame_free(&frame);
  avformat_close_input(&format);
  return status;
}
