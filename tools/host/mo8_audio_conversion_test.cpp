#include "audio/host/ffmpegaudioplayer.h"
#include "video/ffmpegplayer.h"
#include "audio/audiosystem.h"
#include <cmath>
#include <cstdio>

int main() {
  using namespace Impacto;
  auto& bridge = Audio::HostAudioBridge::Get();
  bridge.Init(44100);
  Audio::MasterVolume = 1.0f;
  Audio::GroupVolumes[Audio::ACG_Movie] = 1.0f;
  Video::FFmpegPlayer player;
  player.AbortRequest = false;
  player.AudioStream.emplace();
  AVCodecContext* ctx = avcodec_alloc_context3(nullptr);
  if (!ctx) return 1;
  av_channel_layout_default(&ctx->ch_layout, 2);
  ctx->sample_fmt = AV_SAMPLE_FMT_FLT;
  ctx->sample_rate = 44100;
  {
    Audio::Host::FFmpegAudioPlayer unused(&player);
  }
  Audio::Host::FFmpegAudioPlayer output(&player);
  for (int round = 0; round < 3; ++round) {
    output.InitConvertContext(ctx);
    Video::AVDecodedItem<AVMEDIA_TYPE_AUDIO> item;
    item.Frame = av::AudioSamples(AV_SAMPLE_FMT_FLT, 1024, AV_CH_LAYOUT_STEREO, 44100);
    auto* data = reinterpret_cast<float*>(item.Frame.data());
    for (int i = 0; i < 1024; ++i) {
      data[i * 2] = 0.25f;
      data[i * 2 + 1] = -0.125f;
    }
    player.AudioStream->FrameQueue.wait_enqueue(std::move(item));
    float samples[4096];
    const float gain = round == 1 ? 0.5f : 1.0f;
    Audio::GroupVolumes[Audio::ACG_Movie] = gain;
    int64_t count = 0;
    for (int tick = 0; tick < 4; ++tick) {
      bridge.BeginTick(256.0f / 44100.0f);
      output.Process();
      bridge.EndTick();
      int64_t n = bridge.ReadSamples(samples, 4096);
      if (n != 512) return 1;
      count += n;
      for (int64_t i = 0; i < n; ++i) {
        float expected = (i % 2 ? -0.125f : 0.25f) * gain;
        if (!std::isfinite(samples[i]) || std::abs(samples[i] - expected) > 0.00001f) return 1;
      }
    }
    output.Process();
    const auto clock = output.GetClock().Get().count();
    if (clock < 23000 || clock > 30000) return 1;
    output.Unload();
    output.Unload();
    std::printf("audio conversion round=%d samples=%ld all_match=1\n", round, count);
  }
  // The real movie path is 48000 -> 44100. Keep channel distinction, mix
  // alongside an existing channel and verify that EOF drains resampler delay.
  ctx->sample_rate = 48000;
  output.InitConvertContext(ctx);
  Audio::GroupVolumes[Audio::ACG_Movie] = 1.0f;
  for (int block = 0; block < 10; ++block) {
    Video::AVDecodedItem<AVMEDIA_TYPE_AUDIO> item;
    item.Frame = av::AudioSamples(AV_SAMPLE_FMT_FLT, 4800,
                                  AV_CH_LAYOUT_STEREO, 48000);
    auto* data = reinterpret_cast<float*>(item.Frame.data());
    for (int i = 0; i < 4800; ++i) {
      data[i * 2] = 0.25f;
      data[i * 2 + 1] = -0.125f;
    }
    player.AudioStream->FrameQueue.wait_enqueue(std::move(item));
  }
  Video::AVDecodedItem<AVMEDIA_TYPE_AUDIO> end;
  end.Serial = INT32_MIN;
  player.AudioStream->FrameQueue.wait_enqueue(std::move(end));
  int ticks = 0;
  for (; ticks < 70 && !output.IsDrained(); ++ticks) {
    bridge.BeginTick(0.016f);
    for (auto& sample : bridge.TickMixBuffer()) sample = 0.0625f;
    output.Process();
    bridge.EndTick();
    float samples[4096];
    int64_t n = bridge.ReadSamples(samples, 4096);
    if (n != 1412) return 1;
    for (int64_t i = 0; i < n; ++i) {
      int64_t frame = ticks * 706 + i / 2;
      float expected = 0.0625f;
      if (frame < 44100) expected += i % 2 ? -0.125f : 0.25f;
      if (std::abs(samples[i] - expected) > 0.00001f) return 1;
    }
  }
  if (!output.IsDrained() || ticks != 63) return 1;
  output.Unload();
  puts("audio resample 48000->44100 frames=44100 EOF/mix all_match=1");
  avcodec_free_context(&ctx);
  puts("audio conversion passed");
}
