#pragma once

#include "../../data/savesystem.h"

namespace Impacto {
namespace MO8 {

using namespace Impacto::SaveSystem;

// System data layout of the PC release (reverse engineered from Game.exe
// 0x43EA30 SaveSystemData / 0x43EDA0 LoadSystemData):
//
//   +0x0000  u16 additive checksum, u16 xor checksum (over +0x04..+0x44BA)
//   +0x0080  0x32 bytes  -> Flags[100..150)
//   +0x00B2  0x28 bytes  -> Flags[460..500)
//   +0x00DC  0x640 bytes -> ScrWork[1600..2000]   (byte offset 0x1900)
//   +0x071C  0x190 bytes -> ScrWork[2000..2100]   (byte offset 8000)
//   +0x08B0  ~0x64 bytes of engine option scalars (restored to engine
//            variables; kept verbatim)
//   +0x097C  0x258 bytes (engine global blob, kept verbatim)
//   +0x0BD6  0x30 bytes  (engine global blob, kept verbatim)
//   +0x0C16  0x96 bytes  packed work-flags (8000 bits, expanded on load)
//   +0x0CAC  0xC8 bytes  (engine global blob, kept verbatim)
//   +0x0D74  0x7800 bytes save entry table
//   +0x8574  0x400 bytes tips data
class SaveSystem : public SaveSystemBase {
 public:
  SaveError CheckSaveFile() const override { return SaveError::OK; }
  SaveError MountSaveFile(std::vector<QueuedTexture>& textures) override;

  SaveError LoadSystemData() override;
  void SaveSystemData() override;
  void InitializeSystemData() override;

  void SaveThumbnailData() override {}
  void SaveMemory() override {}
  void LoadEntry(SaveType type, int id) override {}
  void FlushWorkingSaveEntry(SaveType type, int id, int autoSaveType) override;
  SaveError WriteSaveFile() override;
  uint32_t GetSavePlayTime(SaveType type, int id) const override {
    return 0;
  }
  uint8_t GetSaveFlags(SaveType type, int id) const override { return 0; }
  void SetSaveFlags(SaveType type, int id, uint8_t flags) override {}
  tm const& GetSaveDate(SaveType type, int id) const override {
    return LastDateStub;
  }
  uint8_t GetSaveStatus(SaveType type, int id) const override { return 0; }
  int GetSaveTitle(SaveType type, int id) const override { return 0; }
  uint32_t GetTipStatus(size_t tipId) const override { return 0; }
  void SetTipStatus(size_t tipId, bool isLocked, bool isUnread,
                    bool isNew) override {}
  void SetLineRead(size_t scriptId, size_t lineId) override {}
  bool IsLineRead(size_t scriptId, size_t lineId) const override {
    return false;
  }
  void GetReadMessagesCount(int* totalMessageCount,
                            int* readMessageCount) const override {
    *totalMessageCount = 0;
    *readMessageCount = 0;
  }
  void GetViewedEVsCount(int* totalEVCount, int* viewedEVCount) const override {
    *totalEVCount = 0;
    *viewedEVCount = 0;
  }
  void GetEVStatus(int evId, int* totalVariations,
                   int* viewedVariations) const override {
    *totalVariations = 0;
    *viewedVariations = 0;
  }
  void SetEVStatus(int id) override {}
  bool GetEVVariationIsUnlocked(size_t evId,
                                size_t variationIdx) const override {
    return false;
  }
  bool GetBgmFlag(int id) const override { return false; }
  void SetBgmFlag(int id, bool flag) override {}
  void SetCheckpointId(int id) override {}
  Sprite& GetSaveThumbnail(SaveType type, int id) override;

 private:
  std::array<uint8_t, 0x8974> SystemData{};
  bool SaveFileLoaded = false;
  tm LastDateStub{};
};

}  // namespace MO8
}  // namespace Impacto
