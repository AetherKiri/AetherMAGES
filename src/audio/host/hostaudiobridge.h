#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <span>
#include <vector>

namespace Impacto {
namespace Audio {

/*
 * Host audio bridge: the engine-side replacement for a device audio backend.
 * Every enabled AudioChannel decodes its stream into stereo float frames at
 * the fixed output rate, they are summed per tick (mirroring OpenAL's device
 * mixing) and pushed into a single-producer/single-consumer FIFO. The host
 * process drains that FIFO and plays it through its own audio stack (the
 * Aether bridge uses the same OpenAL streaming output as the embedded
 * siglus_rs runtime).
 */
class HostAudioBridge {
 public:
  static HostAudioBridge& Get();

  static constexpr uint32_t kOutputChannels = 2;
  // Enough room for ~1 second of stereo float frames.
  static constexpr size_t kFifoCapacityFrames = 48000;
  // Engine thread.
  void Init(uint32_t outputSampleRate);
  void Reset();
  uint32_t SampleRate() const { return OutputSampleRate; }
  // Per engine tick: allocate the mix buffer for dt seconds of output.
  void BeginTick(float dt);
  std::span<float> TickMixBuffer() { return TickMix; }
  // Push the accumulated mix into the FIFO.
  void EndTick();
  // Direct push for streams paced outside the per-tick mixer (movie audio).
  void SubmitFrames(const float* frames, size_t frameCount);

  // Host thread.
  int64_t ReadSamples(float* outSamples, int64_t capacitySamples);
  size_t BufferedFrames() const;
  void GetStats(uint64_t& framesWritten, uint64_t& framesDropped) const;

 private:
  HostAudioBridge() = default;

  /* Single-producer/single-consumer FIFO. Payloads are tiny (a few KiB per
   * tick) and both endpoints run at ~60 Hz, so a mutex-guarded fixed ring
   * buffer is simpler and faster to reason about than a lock-free queue:
   * pushes either fit entirely or are dropped, which is the back-pressure
   * behaviour the bridge wants. */
  mutable std::mutex FifoMutex;
  std::vector<float> FifoBuffer;
  size_t FifoHead = 0;  // next read slot
  size_t FifoTail = 0;  // next write slot
  size_t FifoCount = 0; // samples currently stored

  size_t PushSamples(const float* samples, size_t sampleCount);
  size_t PopSamples(float* outSamples, size_t capacitySamples);

  uint32_t OutputSampleRate = 44100;
  std::vector<float> TickMix;

  std::atomic<uint64_t> FramesWritten{0};
  std::atomic<uint64_t> FramesDropped{0};
};

}  // namespace Audio
}  // namespace Impacto
