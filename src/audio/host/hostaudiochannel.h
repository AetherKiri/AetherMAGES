#pragma once

#include "../audiochannel.h"
#include "hostaudiobridge.h"

#include <vector>

namespace Impacto {
namespace Audio {
namespace Host {

/*
 * AudioChannel implementation for the embedded host bridge: instead of
 * queueing OpenAL buffers, it decodes the AudioStream in real time, converts
 * it to stereo float at the bridge's fixed output rate and mixes it into the
 * shared FIFO the host process drains (see HostAudioBridge).
 */
class HostAudioChannel : public Audio::AudioChannel {
 public:
  HostAudioChannel(AudioChannelId id, AudioChannelGroup group);
  ~HostAudioChannel() override = default;

  void Play(std::unique_ptr<AudioStream> stream, bool loop,
            float fadeInDuration) override;
  void Stop(float fadeOutDuration) override;
  void Pause() override;
  void Resume() override;

  void Reinit() override {}
  void Update(float dt) override;

  float PositionInSeconds() const override;

 private:
  void EndPlayback();
  void UpdateFade(float dt);
  float CurrentGain() const;
  // Decodes, resamples and mixes dt worth of output frames.
  void MixIntoTickBuffer(float dt);

  // Staging: raw decoded PCM from the stream (interleaved, native format).
  std::vector<uint8_t> Staging;
  // Input frame index (in stream samples) of Staging[0].
  int64_t StagingStart = 0;
  // Input frames currently staged.
  int64_t StagedFrames = 0;
  // Absolute (fractional) input frame position for the resampler.
  double ResamplePhase = 0.0;
  // Output frames mixed since playback started (at the bridge rate).
  uint64_t FramesProduced = 0;

  float FadeDuration = 0;
  float FadeProgress = 0;
  float FadeStartFactor = 0;

  static int constexpr StagingChunkSamples = 2048;
};

}  // namespace Host
}  // namespace Audio
}  // namespace Impacto
