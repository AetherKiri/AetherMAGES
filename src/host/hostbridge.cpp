#include "hostbridge.h"

#include <cstring>

namespace Impacto {
namespace Host {

HostBridge& HostBridge::Get() {
  static HostBridge instance;
  return instance;
}

void HostBridge::Reset() {
  std::lock_guard<std::mutex> lock(Mutex);
  FramePixels.clear();
  FrameWidth = 0;
  FrameHeight = 0;
  Serial = 0;
  RendererInfo.clear();
  LastError.clear();
}

void HostBridge::PublishFrame(std::vector<uint8_t>&& pixels, uint32_t width,
                              uint32_t height) {
  std::lock_guard<std::mutex> lock(Mutex);
  FramePixels = std::move(pixels);
  FrameWidth = width;
  FrameHeight = height;
  Serial += 1;
}

void HostBridge::SetRendererInfo(std::string const& info) {
  std::lock_guard<std::mutex> lock(Mutex);
  RendererInfo = info;
}

void HostBridge::SetEngineThreadError(std::string const& error) {
  std::lock_guard<std::mutex> lock(Mutex);
  LastError = error;
}

bool HostBridge::CopyFrame(void* outPixels, size_t outPixelsSize) const {
  std::lock_guard<std::mutex> lock(Mutex);
  if (outPixels == nullptr || FramePixels.empty()) return false;
  if (outPixelsSize < FramePixels.size()) return false;
  std::memcpy(outPixels, FramePixels.data(), FramePixels.size());
  return true;
}

void HostBridge::GetFrameDesc(uint32_t& width, uint32_t& height,
                              uint32_t& strideBytes) const {
  std::lock_guard<std::mutex> lock(Mutex);
  width = FrameWidth;
  height = FrameHeight;
  strideBytes = FrameWidth * 4;
}

uint64_t HostBridge::FrameSerial() const {
  std::lock_guard<std::mutex> lock(Mutex);
  return Serial;
}

std::string HostBridge::GetRendererInfo() const {
  std::lock_guard<std::mutex> lock(Mutex);
  return RendererInfo;
}

std::string HostBridge::GetLastError() const {
  std::lock_guard<std::mutex> lock(Mutex);
  return LastError;
}

}  // namespace Host
}  // namespace Impacto
