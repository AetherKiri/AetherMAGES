#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Impacto {
namespace Host {
inline std::string& WritableDirectory() {
  static std::string path;
  return path;
}

/*
 * Process-wide state shared between the engine thread and the host-facing
 * side of the embedding library. One instance per process; the engine itself
 * is built around globals, which is why the C API only allows a single
 * active host instance.
 */
class HostBridge {
 public:
  static HostBridge& Get();

  void Reset();

  // Engine thread only.
  void PublishFrame(std::vector<uint8_t>&& pixels, uint32_t width,
                    uint32_t height);
  void PublishGpuFrame(uint64_t texture, uint32_t width, uint32_t height);
  bool GetGpuFrame(uint64_t& texture, uint32_t& width, uint32_t& height,
                    uint64_t& serial) const;
  void SetRendererInfo(std::string const& info);
  void SetEngineThreadError(std::string const& error);

  // Any thread.
  bool CopyFrame(void* outPixels, size_t outPixelsSize) const;
  void GetFrameDesc(uint32_t& width, uint32_t& height,
                    uint32_t& strideBytes) const;
  uint64_t FrameSerial() const;
  std::string GetRendererInfo() const;
  std::string GetLastError() const;

 private:
  HostBridge() = default;

  mutable std::mutex Mutex;
  std::vector<uint8_t> FramePixels;
  uint32_t FrameWidth = 0;
  uint32_t FrameHeight = 0;
  uint64_t Serial = 0;
  uint64_t FrameTexture = 0;
  std::string RendererInfo;
  std::string LastError;
};

}  // namespace Host
}  // namespace Impacto
