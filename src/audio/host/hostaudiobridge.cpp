#include "hostaudiobridge.h"

#include <algorithm>
#include <cstring>

#include "../../log.h"

namespace Impacto {
namespace Audio {

HostAudioBridge& HostAudioBridge::Get() {
  static HostAudioBridge instance;
  return instance;
}

size_t HostAudioBridge::PushSamples(const float* samples,
                                    size_t sampleCount) {
  if (samples == nullptr || sampleCount == 0) return 0;
  const size_t capacity = FifoBuffer.size();
  if (sampleCount > capacity - FifoCount) {
    // Not enough room: drop the whole mix. The host is not draining (paused,
    // hidden, startup); accumulating unbounded latency would be worse.
    return 0;
  }
  size_t copied = 0;
  if (FifoTail >= FifoHead) {
    const size_t first = std::min(sampleCount, capacity - FifoTail);
    std::memcpy(FifoBuffer.data() + FifoTail, samples, first * sizeof(float));
    copied = first;
    if (copied < sampleCount) {
      std::memcpy(FifoBuffer.data(), samples + copied,
                  (sampleCount - copied) * sizeof(float));
      copied = sampleCount;
    }
  } else {
    std::memcpy(FifoBuffer.data() + FifoTail, samples,
                sampleCount * sizeof(float));
    copied = sampleCount;
  }
  FifoTail = (FifoTail + copied) % capacity;
  FifoCount += copied;
  return copied;
}

size_t HostAudioBridge::PopSamples(float* outSamples, size_t capacitySamples) {
  if (outSamples == nullptr || capacitySamples == 0 || FifoCount == 0)
    return 0;
  const size_t capacity = FifoBuffer.size();
  const size_t toPop = std::min(FifoCount, capacitySamples);
  if (FifoHead < FifoTail) {
    std::memcpy(outSamples, FifoBuffer.data() + FifoHead,
                toPop * sizeof(float));
  } else {
    const size_t first = std::min(toPop, capacity - FifoHead);
    std::memcpy(outSamples, FifoBuffer.data() + FifoHead,
                first * sizeof(float));
    if (first < toPop) {
      std::memcpy(outSamples + first, FifoBuffer.data(),
                  (toPop - first) * sizeof(float));
    }
  }
  FifoHead = (FifoHead + toPop) % capacity;
  FifoCount -= toPop;
  return toPop;
}

void HostAudioBridge::Init(uint32_t outputSampleRate) {
  OutputSampleRate = outputSampleRate;
  FramesWritten.store(0, std::memory_order_relaxed);
  FramesDropped.store(0, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(FifoMutex);
    FifoBuffer.assign(kFifoCapacityFrames * kOutputChannels, 0.0f);
    FifoHead = FifoTail = FifoCount = 0;
  }
  TickMix.clear();
  ImpLog(LogLevel::Info, LogChannel::Audio,
         "Host audio bridge initialised at {:d} Hz stereo float\n",
         OutputSampleRate);
}

void HostAudioBridge::Reset() {
  {
    std::lock_guard<std::mutex> lock(FifoMutex);
    FifoHead = FifoTail = FifoCount = 0;
  }
  TickMix.clear();
  FramesWritten.store(0, std::memory_order_relaxed);
  FramesDropped.store(0, std::memory_order_relaxed);
}

void HostAudioBridge::BeginTick(float dt) {
  size_t frames = static_cast<size_t>(dt * OutputSampleRate + 0.5f);
  frames = std::min<size_t>(frames, 2048);
  TickMix.assign(frames * kOutputChannels, 0.0f);
}

void HostAudioBridge::EndTick() {
  if (TickMix.empty()) return;
  const size_t sampleCount = TickMix.size();
  {
    std::lock_guard<std::mutex> lock(FifoMutex);
    if (PushSamples(TickMix.data(), sampleCount) != sampleCount) {
      FramesDropped.fetch_add(sampleCount / kOutputChannels,
                              std::memory_order_relaxed);
      return;
    }
  }
  FramesWritten.fetch_add(sampleCount / kOutputChannels,
                          std::memory_order_relaxed);
  TickMix.clear();
}

void HostAudioBridge::SubmitFrames(const float* frames, size_t frameCount) {
  if (frames == nullptr || frameCount == 0) return;
  const size_t sampleCount = frameCount * kOutputChannels;
  {
    std::lock_guard<std::mutex> lock(FifoMutex);
    if (PushSamples(frames, sampleCount) != sampleCount) {
      FramesDropped.fetch_add(frameCount, std::memory_order_relaxed);
      return;
    }
  }
  FramesWritten.fetch_add(frameCount, std::memory_order_relaxed);
}

int64_t HostAudioBridge::ReadSamples(float* outSamples,
                                     int64_t capacitySamples) {
  if (outSamples == nullptr || capacitySamples <= 0) return 0;
  std::lock_guard<std::mutex> lock(FifoMutex);
  return static_cast<int64_t>(
      PopSamples(outSamples, static_cast<size_t>(capacitySamples)));
}

size_t HostAudioBridge::BufferedFrames() const {
  std::lock_guard<std::mutex> lock(FifoMutex);
  return FifoCount / kOutputChannels;
}

void HostAudioBridge::GetStats(uint64_t& framesWritten,
                               uint64_t& framesDropped) const {
  framesWritten = FramesWritten.load(std::memory_order_relaxed);
  framesDropped = FramesDropped.load(std::memory_order_relaxed);
}

}  // namespace Audio
}  // namespace Impacto
