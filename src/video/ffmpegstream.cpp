#include "ffmpegstream.h"
#include "ffmpegplayer.h"

namespace Impacto {
namespace Video {

template <AVMediaType MediaType>
void FFmpegStream<MediaType>::FlushPacketQueue() {
  AVPacketItem item;
  bool isNotEmpty = false;
  while (PacketQueue.try_dequeue(item)) {
    isNotEmpty = true;
  };
  if (isNotEmpty) {
    PacketQueueSerial++;
  }
}

template void FFmpegStream<AVMEDIA_TYPE_AUDIO>::FlushPacketQueue();
template void FFmpegStream<AVMEDIA_TYPE_VIDEO>::FlushPacketQueue();
template void FFmpegStream<AVMEDIA_TYPE_SUBTITLE>::FlushPacketQueue();

template <AVMediaType MediaType>
void FFmpegStream<MediaType>::FlushFrameQueue() {
  AVDecodedItem<MediaType> item;
  while (FrameQueue.try_dequeue(item));
}
template void FFmpegStream<AVMEDIA_TYPE_AUDIO>::FlushFrameQueue();
template void FFmpegStream<AVMEDIA_TYPE_VIDEO>::FlushFrameQueue();
template void FFmpegStream<AVMEDIA_TYPE_SUBTITLE>::FlushFrameQueue();

}  // namespace Video
}  // namespace Impacto
