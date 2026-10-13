#include "savesystem.h"

#include "../../io/physicalfilestream.h"
#include "../../mem.h"
#include "../../vm/vm.h"
#include "../../profile/data/savesystem.h"
#include "../../profile/scriptvars.h"
#include "../../renderer/renderer.h"

namespace Impacto {
namespace MO8 {

using namespace Impacto::Vm;
using namespace Impacto::Profile::SaveSystem;
using namespace Impacto::Profile::ScriptVars;

namespace {

constexpr size_t SysDataSize = 0x8974;
constexpr size_t ChecksummedWords = 0x225B;  // u16 words from +0x04

constexpr size_t OffFlagsA = 0x80;   // 0x32 bytes -> Flags[100..150)
constexpr size_t OffFlagsB = 0xB2;   // 0x28 bytes -> Flags[460..500)
constexpr size_t OffScrWorkA = 0xDC;  // 0x640 bytes -> ScrWork[1600..2000]
constexpr size_t OffScrWorkB = 0x71C;  // 0x190 bytes -> ScrWork[2000..2100]
constexpr size_t OffWorkFlags = 0xC16;  // 0x96 bytes packed work-flags

uint16_t ChecksumSum(uint8_t const* data) {
  uint16_t sum = 0;
  for (size_t i = 0; i < ChecksummedWords; i++) {
    sum += (uint16_t)(data[4 + i * 2] | (data[4 + i * 2 + 1] << 8));
  }
  return sum;
}

uint16_t ChecksumXor(uint8_t const* data) {
  uint16_t acc = 0;
  for (size_t i = 0; i < ChecksummedWords; i++) {
    acc ^= (uint16_t)(data[4 + i * 2] | (data[4 + i * 2 + 1] << 8));
  }
  return acc;
}

}  // namespace

// The entry tables live inside the 0x0D74..0x8574 region of the system data
// (not yet parsed). The base class keeps raw entry pointers, so allocate
// placeholder entries up front - InstAutoSave probes QuickSaveEntries via
// GetQuickSaveOpenSlot the moment the game script starts.
SaveSystem::SaveSystem() {
  for (int i = 0; i < MaxSaveEntries; i++) {
    QuickSaveEntries[i] = new SaveFileEntryBase();
    FullSaveEntries[i] = new SaveFileEntryBase();
  }
}

SaveError SaveSystem::MountSaveFile(std::vector<QueuedTexture>& textures) {
  Io::Stream* instream = nullptr;
  IoError err = Io::PhysicalFileStream::Create(SaveFilePath, &instream);
  if (err == IoError_OK) {
    Io::ReadArrayLE<uint8_t>(SystemData.data(), instream,
                             (uint32_t)SystemData.size());
    delete instream;
    SaveFileLoaded = true;
  } else {
    // First boot (or unreadable file): start from zeroed data, the caller
    // recreates it through SaveSystemData once the script initialises.
    SystemData.fill(0);
    SaveFileLoaded = false;
  }
  return SaveError::OK;
}

SaveError SaveSystem::LoadSystemData() {
  // Restore the script-visible regions (original 0x43EDA0).
  memcpy(&FlagWork[100], &SystemData[OffFlagsA], 0x32);
  memcpy(&FlagWork[460], &SystemData[OffFlagsB], 0x28);
  memcpy(&ScrWork[1600], &SystemData[OffScrWorkA], 0x640);
  memcpy(&ScrWork[2000], &SystemData[OffScrWorkB], 0x190);

  // Packed work-flags are kept verbatim (no consumer in the engine yet).
  (void)OffWorkFlags;

  return SaveError::OK;
}

void SaveSystem::SaveSystemData() {
  memset(SystemData.data(), 0, SystemData.size());
  memcpy(&SystemData[OffFlagsA], &FlagWork[100], 0x32);
  memcpy(&SystemData[OffFlagsB], &FlagWork[460], 0x28);
  memcpy(&SystemData[OffScrWorkA], &ScrWork[1600], 0x640);
  memcpy(&SystemData[OffScrWorkB], &ScrWork[2000], 0x190);

  uint16_t sum = ChecksumSum(SystemData.data());
  uint16_t xors = ChecksumXor(SystemData.data());
  SystemData[0] = sum & 0xFF;
  SystemData[1] = sum >> 8;
  SystemData[2] = xors & 0xFF;
  SystemData[3] = xors >> 8;
}

void SaveSystem::InitializeSystemData() {
  SystemData.fill(0);
  SaveFileLoaded = false;
}

SaveError SaveSystem::WriteSaveFile() {
  using CF = Io::PhysicalFileStream::CreateFlagsMode;
  Io::Stream* outstream = nullptr;
  IoError err = Io::PhysicalFileStream::Create(SaveFilePath, &outstream,
                                               CF::CREATE | CF::CREATE_DIRS |
                                                   CF::WRITE);
  if (err != IoError_OK) {
    ImpLog(LogLevel::Error, LogChannel::IO, "Failed to create save file\n");
    return SaveError::Failed;
  }
  outstream->Write(SystemData.data(), 1, (uint32_t)SystemData.size());
  delete outstream;
  SaveFileLoaded = true;
  return SaveError::OK;
}

void SaveSystem::FlushWorkingSaveEntry(SaveType type, int id,
                                       int autoSaveType) {
  // Save entry table layout not yet reverse engineered (MGSSDR); the
  // system-data region is kept intact so future entries survive.
  WriteSaveFile();
}

Sprite& SaveSystem::GetSaveThumbnail(SaveType type, int id) {
  static Sprite stub;
  return stub;
}

}  // namespace MO8
}  // namespace Impacto
