#pragma once

#include "audiocommon.h"
#include "../impacto.h"
#include "../io/stream.h"

#include <vector>

namespace Impacto {
namespace Audio {

class AudioStream {
 public:
  virtual ~AudioStream() {
    if (BaseStream) delete BaseStream;
  }

  static AudioStream* Create(Io::Stream* stream);

  // Returns number of samples read
  virtual int Read(void* buffer, int samples) = 0;
  virtual void Seek(int samples) = 0;

  const Io::Stream* GetBaseStream() const { return BaseStream; }

  int BytesPerSample() const { return (BitDepth / 8) * ChannelCount; }

  // Public so embed hosts can register the built-in decoders explicitly:
  // the per-stream self-registration statics are dropped by final linkers
  // when impacto is consumed as a static archive (iOS app link).
  using AudioStreamCreator = auto (*)(Io::Stream* stream) -> AudioStream*;
  static bool AddAudioStreamCreator(AudioStreamCreator c);

  int ChannelCount;
  int SampleRate;
  int BitDepth;

  int LoopStart = 0;
  int LoopEnd = 0;
  // negative indicates no fixed length
  int Duration = -1;
  int ReadPosition = 0;

 protected:
  AudioStream() {};

  Io::Stream* BaseStream = 0;

 private:
  static std::vector<AudioStreamCreator>& GetRegistry();
};

}  // namespace Audio
}  // namespace Impacto