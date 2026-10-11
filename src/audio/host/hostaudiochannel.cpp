#include "hostaudiochannel.h"

#include <algorithm>
#include <cstring>

#include "../audiosystem.h"
#include "../../log.h"

namespace Impacto {
namespace Audio {
namespace Host {

HostAudioChannel::HostAudioChannel(AudioChannelId id,
                                   AudioChannelGroup group)
    : AudioChannel(id, group) {}

void HostAudioChannel::Play(std::unique_ptr<AudioStream> stream, bool loop,
                            float fadeInDuration) {
  EndPlayback();

  Stream = std::move(stream);
  FadeDuration = fadeInDuration;
  FadeProgress = FadeDuration == 0 ? 1.0f : 0.0f;
  State = FadeDuration == 0 ? ACS_Playing : ACS_FadingIn;
  Looping = loop;

  AudioSubtitlesStart(this);
}

void HostAudioChannel::Stop(float fadeOutDuration) {
  if (State == ACS_Stopped) return;

  if (fadeOutDuration == 0 || State == ACS_Paused) {
    EndPlayback();
    return;
  }

  FadeStartFactor = CurrentGain();
  State = ACS_FadingOut;
  FadeDuration = fadeOutDuration;
  FadeProgress = 0;
}

void HostAudioChannel::Pause() {
  if (State != ACS_Playing && State != ACS_FadingIn &&
      State != ACS_FadingOut) {
    return;
  }
  State = ACS_Paused;
}

void HostAudioChannel::Resume() {
  if (State != ACS_Paused) return;
  State = ACS_Playing;
}

void HostAudioChannel::EndPlayback() {
  Stream = nullptr;
  State = ACS_Stopped;
  FadeProgress = 0;
  Staging.clear();
  StagingStart = 0;
  StagedFrames = 0;
  ResamplePhase = 0.0;
  FramesProduced = 0;
}

float HostAudioChannel::PositionInSeconds() const {
  if (Stream == nullptr) return 0.0f;
  auto& bridge = HostAudioBridge::Get();
  const double rate = (double)bridge.SampleRate();
  double seconds =
      (double)FramesProduced / rate - (double)bridge.BufferedFrames() / rate;
  if (seconds < 0.0) seconds = 0.0;
  if (Stream->Duration > 0) {
    const double duration = (double)Stream->Duration / Stream->SampleRate;
    seconds = std::min(seconds, duration);
  }
  return (float)seconds;
}

void HostAudioChannel::UpdateFade(float dt) {
  if (State != ACS_FadingIn && State != ACS_FadingOut) return;

  FadeProgress = std::min(1.0f, FadeProgress + dt / FadeDuration);
  if (FadeProgress >= 1.0f) {
    State = State == ACS_FadingIn ? ACS_Playing : ACS_Stopped;
  }
}

float HostAudioChannel::CurrentGain() const {
  float gain = MasterVolume * GroupVolumes[Group] * Volume;
  switch (State) {
    case ACS_Stopped:
    case ACS_Paused:
      return gain;
    case ACS_Playing:
      return gain;
    case ACS_FadingIn:
      return gain * powf(FadeProgress, 3.0f);
    case ACS_FadingOut:
      return gain * FadeStartFactor * (1.0f - powf(FadeProgress, 3.0f));
  }
  return gain;
}

void HostAudioChannel::MixIntoTickBuffer(float dt) {
  (void)dt;
  auto& bridge = HostAudioBridge::Get();
  std::span<float> mix = bridge.TickMixBuffer();
  if (mix.empty() || Stream == nullptr) return;

  const int outFrames =
      static_cast<int>(mix.size() / HostAudioBridge::kOutputChannels);
  const int outRate = static_cast<int>(bridge.SampleRate());
  const int inRate = Stream->SampleRate;
  const int inChannels = Stream->ChannelCount;
  const int frameBytes = Stream->BytesPerSample();
  const double step = (double)inRate / (double)outRate;

  // Drop consumed staging prefix so the resampler window stays small.
  const int64_t drop =
      static_cast<int64_t>(ResamplePhase) - StagingStart;
  if (drop > 0) {
    const size_t dropBytes = static_cast<size_t>(drop) * frameBytes;
    if (dropBytes >= Staging.size()) {
      Staging.clear();
    } else {
      Staging.erase(Staging.begin(),
                    Staging.begin() + static_cast<std::ptrdiff_t>(dropBytes));
    }
    StagingStart += drop;
  }

  // Decode enough input to cover this tick's output frames.
  const double needEnd =
      (ResamplePhase - (double)StagingStart) + (double)outFrames * step + 2.0;
  while ((double)StagedFrames < needEnd) {
    if (!Looping && Stream->Duration >= 0 &&
        Stream->ReadPosition >= Stream->Duration) {
      break;
    }
    const int end =
        (Looping && Stream->LoopEnd > 0) ? Stream->LoopEnd : Stream->Duration;
    int toRead = StagingChunkSamples;
    if (end > 0 && Stream->ReadPosition < end) {
      toRead = std::min(toRead, end - Stream->ReadPosition);
    }
    if (toRead <= 0) break;

    const size_t oldBytes = Staging.size();
    Staging.resize(oldBytes + static_cast<size_t>(toRead) * frameBytes);
    const int got =
        Stream->Read(Staging.data() + oldBytes, toRead);
    if (got <= 0) {
      Staging.resize(oldBytes);
      break;
    }
    StagedFrames += got;
    if (Looping && Stream->LoopEnd > 0 &&
        Stream->ReadPosition >= Stream->LoopEnd) {
      Stream->Seek(Stream->LoopStart);
    }
  }

  // Read one decoded sample as float; returns 0 for out-of-range frames.
  const auto readSample = [&](int64_t frame, int channel) -> float {
    const int64_t index = frame - StagingStart;
    if (index < 0 || index >= StagedFrames) return 0.0f;
    const uint8_t* base =
        Staging.data() + static_cast<size_t>(index) * frameBytes;
    switch (Stream->BitDepth) {
      case 8:
        return ((int)base[channel] - 128) / 128.0f;
      case 16: {
        const int16_t value =
            (int16_t)((uint16_t)base[channel * 2] |
                      ((uint16_t)base[channel * 2 + 1] << 8));
        return (float)value / 32768.0f;
      }
      case 32: {
        float value;
        std::memcpy(&value, base + channel * 4, sizeof(float));
        return value;
      }
      default:
        return 0.0f;
    }
  };

  const float gain = CurrentGain();
  const int rightChannel = inChannels > 1 ? 1 : 0;
  for (int f = 0; f < outFrames; ++f) {
    const double src =
        (ResamplePhase - (double)StagingStart) + (double)f * step;
    const int64_t i0 = (int64_t)src;
    const double frac = src - (double)i0;
    const float left =
        readSample(i0, 0) * (float)(1.0 - frac) +
        readSample(i0 + 1, 0) * (float)frac;
    const float right =
        readSample(i0, rightChannel) * (float)(1.0 - frac) +
        readSample(i0 + 1, rightChannel) * (float)frac;
    mix[static_cast<size_t>(f) * 2 + 0] += left * gain;
    mix[static_cast<size_t>(f) * 2 + 1] += right * gain;
  }

  ResamplePhase += (double)outFrames * step;
  FramesProduced += outFrames;
}

void HostAudioChannel::Update(float dt) {
  UpdateFade(dt);

  if (State == ACS_Playing || State == ACS_FadingIn ||
      State == ACS_FadingOut) {
    MixIntoTickBuffer(dt);
  }

  // Mirror the OpenAL "source finished" transition: a non-looping stream
  // reports Stopped once everything mixed for it has been consumed.
  if (State == ACS_Playing && Stream != nullptr && !Looping &&
      Stream->Duration >= 0) {
    const double duration = (double)Stream->Duration / Stream->SampleRate;
    if ((double)PositionInSeconds() >= duration) {
      EndPlayback();
    }
  }
}

}  // namespace Host
}  // namespace Audio
}  // namespace Impacto
