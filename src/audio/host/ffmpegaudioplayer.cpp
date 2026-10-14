#include "ffmpegaudioplayer.h"

#include "../../log.h"
#include "../audiosystem.h"
#include "../../video/ffmpegplayer.h"

#include <algorithm>
#include <chrono>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
};

namespace Impacto {
namespace Audio {
namespace Host {

FFmpegAudioPlayer::~FFmpegAudioPlayer() {
  {
    std::lock_guard<std::mutex> lock(StagingMutex);
    Converted.clear();
    Converted.shrink_to_fit();
  }
  ReleaseConvertContext();
}

void FFmpegAudioPlayer::ReleaseConvertContext() {
  if (AudioBuffer != nullptr) {
    av_freep(&AudioBuffer[0]);
    av_freep(&AudioBuffer);
  }
  swr_free(&AudioConvertContext);
}

void FFmpegAudioPlayer::InitConvertContext(AVCodecContext* codecCtx) {
  auto& bridge = HostAudioBridge::Get();

  ReleaseConvertContext();

  AVChannelLayout stereoFormat;
  av_channel_layout_default(&stereoFormat, 2);
  int res = swr_alloc_set_opts2(
      &AudioConvertContext, &stereoFormat, AV_SAMPLE_FMT_FLT,
      (int)bridge.SampleRate(), &codecCtx->ch_layout, codecCtx->sample_fmt,
      codecCtx->sample_rate, 0, NULL);
  if (res) {
    ImpLog(LogLevel::Error, LogChannel::Video,
           "Could not create audio convert context!\n");
    return;
  }

  res = swr_init(AudioConvertContext);
  if (res >= 0) {
    res = av_samples_alloc_array_and_samples(&AudioBuffer, &AudioLinesize, 2,
                                            StagingFrames, AV_SAMPLE_FMT_FLT, 0);
  }
  if (res < 0) {
    ImpLog(LogLevel::Error, LogChannel::Video,
           "Could not initialize movie audio buffers: {}\n", res);
    ReleaseConvertContext();
    return;
  }

  std::lock_guard<std::mutex> lock(StagingMutex);
  Converted.clear();
  FramesSubmitted = 0;
  AudioClock.Set(Video::Clock::Microseconds{}, 0);
  DecoderEOF = false;
}

void FFmpegAudioPlayer::Stop() {
  // Called from the audio decoder thread on serial changes (seek): drop
  // everything staged but not yet handed to the bridge. Samples already in
  // the FIFO belong to the host now and cannot be selectively removed, same
  // as OpenAL buffers already queued stay until played.
  std::lock_guard<std::mutex> lock(StagingMutex);
  Converted.clear();
}

void FFmpegAudioPlayer::Unload() {
  ImpLog(LogLevel::Info, LogChannel::Video,
         "[movietrace] audio submitted={:d} remaining={:d} decoder_eof={:d}\n",
         FramesSubmitted, Converted.size() / HostAudioBridge::kOutputChannels,
         DecoderEOF);
  Stop();
  ReleaseConvertContext();
  std::lock_guard<std::mutex> lock(StagingMutex);
  FramesSubmitted = 0;
}

void FFmpegAudioPlayer::FillAudioBuffers() {
  if (AudioConvertContext == nullptr) return;

  std::lock_guard<std::mutex> lock(StagingMutex);

  auto& bridge = HostAudioBridge::Get();
  // Keep about 40ms of output staged between pushes, mirroring the OpenAL
  // buffer-queue pacing.
  constexpr size_t kMaxPendingSamples =
      2048 * HostAudioBridge::kOutputChannels;

  // Append one swr_convert() result to the staging buffer.
  const auto appendConverted = [&](int outputSamples) {
    if (outputSamples <= 0) return;
    const size_t bytes = av_samples_get_buffer_size(
        NULL, 2, outputSamples, AV_SAMPLE_FMT_FLT, 1);
    const size_t oldSize = Converted.size();
    Converted.resize(oldSize + bytes / sizeof(float));
    std::memcpy(Converted.data() + oldSize, AudioBuffer[0], bytes);
  };

  while (!DecoderEOF && Converted.size() < kMaxPendingSamples &&
         !Player->AbortRequest) {
    Video::AVDecodedItem<AVMEDIA_TYPE_AUDIO> aFrame;
    if (!Player->AudioStream->FrameQueue.try_dequeue(aFrame)) {
      break;
    }
    if (aFrame.Serial == INT32_MIN) {
      DecoderEOF = true;
      int outputSamples;
      while ((outputSamples = swr_convert(AudioConvertContext, AudioBuffer,
                                          StagingFrames, nullptr, 0)) > 0) {
        appendConverted(outputSamples);
      }
      break;
    }

    const int inSampleRate = aFrame.Frame.sampleRate();
    const int inFrames = aFrame.Frame.samplesCount();

    int64_t delay = swr_get_delay(AudioConvertContext, inSampleRate);
    int64_t samplesPerCh =
        av_rescale_rnd((int64_t)inFrames + delay, (int64_t)bridge.SampleRate(),
                       (int64_t)inSampleRate, AV_ROUND_UP);
    samplesPerCh = std::min<int64_t>(samplesPerCh, StagingFrames);

    int outputSamples = swr_convert(AudioConvertContext, AudioBuffer,
                                    (int)samplesPerCh,
                                    (const uint8_t**)aFrame.Frame.raw()->extended_data,
                                    inFrames);
    appendConverted(outputSamples);
  }

  PushStaging();
}

// Caller must hold StagingMutex.
void FFmpegAudioPlayer::PushStaging() {
  if (Converted.empty()) return;
  auto mix = HostAudioBridge::Get().TickMixBuffer();
  const size_t count = std::min(mix.size(), Converted.size());
  const float gain = Audio::MasterVolume * Audio::GroupVolumes[Audio::ACG_Movie];
  for (size_t i = 0; i < count; ++i) mix[i] += Converted[i] * gain;
  FramesSubmitted += count / HostAudioBridge::kOutputChannels;
  Converted.erase(Converted.begin(), Converted.begin() + count);
}

void FFmpegAudioPlayer::Process() {
  FillAudioBuffers();

  auto& bridge = HostAudioBridge::Get();
  const double rate = (double)bridge.SampleRate();
  uint64_t submitted = 0;
  {
    std::lock_guard<std::mutex> lock(StagingMutex);
    submitted = FramesSubmitted;
  }
  double seconds = submitted / rate - (double)bridge.BufferedFrames() / rate;
  if (seconds < 0.0) seconds = 0.0;

  auto audioS = std::chrono::duration_cast<Video::Clock::Microseconds>(
      std::chrono::duration<double>(seconds));
  AudioClock.Set(audioS, 0);
  ImpLogSlow(LogLevel::Trace, LogChannel::Video, "samplePosition: {:f}\n",
             seconds);
}

void FFmpegAudioPlayer::Reinit() {
  std::lock_guard<std::mutex> lock(StagingMutex);
  Converted.clear();
  FramesSubmitted = 0;
}

bool FFmpegAudioPlayer::IsDrained() {
  std::lock_guard<std::mutex> lock(StagingMutex);
  return DecoderEOF && Converted.empty();
}

}  // namespace Host
}  // namespace Audio
}  // namespace Impacto
