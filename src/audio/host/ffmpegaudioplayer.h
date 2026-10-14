#pragma once

#include "../ffmpegaudioplayer.h"
#include "hostaudiobridge.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace Impacto {
namespace Audio {
namespace Host {

/*
 * FFmpegAudioPlayer for the embedded host bridge: converts movie audio to
 * stereo float at the bridge's fixed output rate and pushes it into the FIFO
 * shared with the channel mixer. The playback clock is derived from the
 * frames written minus the backlog still buffered for the host.
 */
class FFmpegAudioPlayer : public Audio::FFmpegAudioPlayer {
 public:
  FFmpegAudioPlayer(Video::FFmpegPlayer* player)
      : Audio::FFmpegAudioPlayer(player) {}
  ~FFmpegAudioPlayer() override;

  void Init() override {}
  void InitConvertContext(AVCodecContext* codecCtx) override;
  void FillAudioBuffers() override;
  void Process() override;
  void Reinit() override;

  void Stop() override;
  void Unload() override;

 private:
  void ReleaseConvertContext();
  void PushStaging();

  // Interleaved stereo f32 at the bridge rate, staged between swr calls.
  // Guarded by StagingMutex: the decoder thread may call Stop() while the
  // engine thread is mixing (mirrors OpenAL source-stop semantics).
  std::mutex StagingMutex;
  std::vector<float> Converted;
  // Total frames handed to the bridge; the playback clock is derived from
  // this minus whatever the host has not consumed yet.
  uint64_t FramesSubmitted = 0;
  // Staging capacity in frames (converted on demand).
  static int constexpr StagingFrames = 8192;
};

}  // namespace Host
}  // namespace Audio
}  // namespace Impacto
