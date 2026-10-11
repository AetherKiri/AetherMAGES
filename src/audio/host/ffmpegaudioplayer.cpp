#include "ffmpegaudioplayer.h"

#include "../../log.h"
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
  av_freep(&AudioBuffer[0]);
  swr_free(&AudioConvertContext);
}

void FFmpegAudioPlayer::InitConvertContext(AVCodecContext* codecCtx) {
  auto& bridge = HostAudioBridge::Get();

  swr_free(&AudioConvertContext);
  av_freep(&AudioBuffer[0]);

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

  swr_init(AudioConvertContext);
  av_samples_alloc_array_and_samples(&AudioBuffer, &AudioLinesize, 2,
                                     StagingFrames, AV_SAMPLE_FMT_FLT, 0);

  std::lock_guard<std::mutex> lock(StagingMutex);
  Converted.clear();
  FramesSubmitted = 0;
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
  Stop();
  av_freep(&AudioBuffer[0]);
  swr_free(&AudioConvertContext);
  std::lock_guard<std::mutex> lock(StagingMutex);
  FramesSubmitted = 0;
}

void FFmpegAudioPlayer::FillAudioBuffers() {
  if (AudioConvertContext == nullptr) return;

  std::lock_guard<std::mutex> lock(StagingMutex);

  auto& bridge = HostAudioBridge::Get();
  // Keep about 40ms of output staged between pushes, mirroring the OpenAL
  // buffer-queue pacing.
  constexpr size_t kMaxPendingBytes =
      2048 * HostAudioBridge::kOutputChannels * sizeof(float);

  // Append one swr_convert() result to the staging buffer.
  const auto appendConverted = [&](int outputSamples) {
    if (outputSamples <= 0) return;
    const size_t bytes = av_samples_get_buffer_size(
        NULL, 2, outputSamples, AV_SAMPLE_FMT_FLT, 1);
    const size_t oldSize = Converted.size();
    Converted.resize(oldSize + bytes / sizeof(float));
    std::memcpy(Converted.data() + oldSize, AudioBuffer[0], bytes);
  };

  while (Converted.size() < kMaxPendingBytes && !Player->AbortRequest) {
    Video::AVDecodedItem<AVMEDIA_TYPE_AUDIO> aFrame;
    if (!Player->AudioStream->FrameQueue.try_dequeue(aFrame)) {
      break;
    }
    if (aFrame.Serial == INT32_MIN) break;

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
    // Drain anything the resampler still holds buffered from earlier calls.
    while ((outputSamples = swr_convert(AudioConvertContext, AudioBuffer,
                                        StagingFrames, nullptr, 0)) > 0) {
      appendConverted(outputSamples);
    }
  }

  PushStaging();
}

// Caller must hold StagingMutex.
void FFmpegAudioPlayer::PushStaging() {
  if (Converted.empty()) return;
  const size_t frames = Converted.size() / HostAudioBridge::kOutputChannels;
  HostAudioBridge::Get().SubmitFrames(
      reinterpret_cast<const float*>(Converted.data()), frames);
  FramesSubmitted += frames;
  Converted.clear();
}

void FFmpegAudioPlayer::Process() {
  if (Player->AudioStream->FrameQueue.peek() != nullptr) {
    FillAudioBuffers();
  }

  auto& bridge = HostAudioBridge::Get();
  const double rate = (double)bridge.SampleRate();
  uint64_t submitted = 0;
  {
    std::lock_guard<std::mutex> lock(StagingMutex);
    submitted = FramesSubmitted;
  }
  double seconds = submitted / rate - (double)bridge.BufferedFrames() / rate;
  if (seconds < 0.0) seconds = 0.0;

  const int streamRate = Player->AudioStream->CodecContext.sampleRate();
  if (streamRate > 0 && Player->AudioStream->Duration > 0) {
    const double duration =
        (double)Player->AudioStream->Duration / (double)streamRate;
    seconds = std::min(seconds, duration);
  }

  auto audioS = std::chrono::duration_cast<Video::Clock::Microseconds>(
      std::chrono::duration<double, std::micro>(seconds));
  AudioClock.Set(audioS, 0);
  ImpLogSlow(LogLevel::Trace, LogChannel::Video, "samplePosition: {:f}\n",
             seconds);
}

void FFmpegAudioPlayer::Reinit() {
  std::lock_guard<std::mutex> lock(StagingMutex);
  Converted.clear();
  FramesSubmitted = 0;
}

}  // namespace Host
}  // namespace Audio
}  // namespace Impacto
