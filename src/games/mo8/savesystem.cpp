#include "savesystem.h"

#include "../../io/physicalfilestream.h"
#include "../../mem.h"
#include "../../vm/vm.h"
#include "../../profile/data/savesystem.h"
#include "../../profile/scriptvars.h"
#include "../../renderer/renderer.h"
#include "../../profile/vm.h"

#include <filesystem>
#include <fstream>
#include <algorithm>

namespace Impacto {
namespace MO8 {

using namespace Impacto::Vm;
using namespace Impacto::Profile::SaveSystem;
using namespace Impacto::Profile::ScriptVars;

namespace {

constexpr size_t ChecksummedWords = 0x225B * 2;  // two u16 words per iteration

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

uint32_t U32(uint8_t const* p) {
  return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
void Put32(uint8_t* p, uint32_t value) {
  for (int i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}
uint32_t Crc(std::span<const uint8_t> data) {
  uint32_t crc = ~0u;
  for (uint8_t b : data) {
    crc ^= b;
    for (int i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

constexpr size_t SlotSize = 0x42A8 + 240 * 135 * 2;
constexpr size_t SlotPayload = MaxSaveEntries * 2 * SlotSize + MaxSaveEntries;

}  // namespace

SaveSystem::SaveSystem() {
  for (int i = 0; i < MaxSaveEntries; i++) {
    QuickSaveEntries[i] = &QuickEntries[i];
    FullSaveEntries[i] = &FullEntries[i];
  }
  LockedQuickSaveCount = 0;
  QuickSavedOnCurrentLine = false;
  const RectF viewport = Window->GetViewport();
  WorkingSaveThumbnail.Sheet = SpriteSheet(viewport.Width, viewport.Height);
  WorkingSaveThumbnail.Bounds.SetSize(viewport.GetSize());
  Texture tex;
  tex.LoadSolidColor((int)viewport.Width, (int)viewport.Height, 0x000000FF);
  WorkingSaveThumbnail.Sheet.Texture = tex.Submit();
}

SaveError SaveSystem::MountSaveFile(std::vector<QueuedTexture>& textures) {
  Io::Stream* instream = nullptr;
  IoError err = Io::PhysicalFileStream::Create(SaveFilePath, &instream);
  if (err == IoError_OK) {
    if (instream->Read(SystemData.data(), SystemData.size()) !=
        (int64_t)SystemData.size()) {
      delete instream;
      SlotFileError = SaveError::Corrupted;
      return SlotFileError;
    }
    delete instream;
    SaveFileLoaded = true;
  } else {
    if (err != IoError_NotFound) {
      SlotFileError = SaveError::Failed;
      return SlotFileError;
    }
    // First boot: start from zeroed data, the caller
    // recreates it through SaveSystemData once the script initialises.
    SystemData.fill(0);
    SaveFileLoaded = false;
  }
  SlotFileError = ReadSlots(textures);
  return SlotFileError;
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
  auto writeError = outstream->Write(SystemData.data(), 1,
                                     (uint32_t)SystemData.size());
  delete outstream;
  if (writeError != static_cast<int64_t>(SystemData.size()))
    return SaveError::Failed;
  auto slotError = WriteSlots();
  if (slotError != SaveError::OK) return slotError;
  SaveFileLoaded = true;
  return SaveError::OK;
}

void SaveSystem::FlushWorkingSaveEntry(SaveType type, int id,
                                        int autoSaveType) {
  if (!WorkingValid || id < 0 || id >= MaxSaveEntries) return;
  Entry* entry = GetSaveEntry<Entry>(type, id);
  if (entry->Flags & WriteProtect) return;
  auto sprite = entry->SaveThumbnail;
  auto flags = entry->Flags;
  *entry = WorkingEntry;
  entry->SaveThumbnail = sprite;
  entry->Flags = flags;
  entry->Status = 1;
  entry->SaveDate = CurrentDateTime();
  entry->SaveType = type == SaveType::Quick ? autoSaveType : 0;
  EncodeEntry(*entry);
  CaptureThumbnail(*entry);
  if (type == SaveType::Quick) UpdateQuickSaveRecentSortedId(id);
  ImpLog(LogLevel::Info, LogChannel::IO,
         "mo8save flush type={:d} slot={:d} title={:d} checkpoint={:d}\n",
         (int)type, id, entry->SwTitle, entry->MainThreadIp);
}

Sprite& SaveSystem::GetSaveThumbnail(SaveType type, int id) {
  return GetSaveEntry<Entry>(type, id)->SaveThumbnail;
}

SaveError SaveSystem::CheckSaveFile() const { return SlotFileError; }

void SaveSystem::EncodeEntry(Entry& e) {
  auto* p = e.State.data();
  p[0] = e.Status;
  p[8] = (e.SaveDate.tm_year + 1900) & 255;
  p[9] = (e.SaveDate.tm_year + 1900) >> 8;
  p[10] = e.SaveDate.tm_mday;
  p[11] = e.SaveDate.tm_mon + 1;
  p[12] = e.SaveDate.tm_sec;
  p[13] = e.SaveDate.tm_min;
  p[14] = e.SaveDate.tm_hour;
  Put32(p + 0x10, e.PlayTime);
  Put32(p + 0x14, e.SwTitle);
  p[0x1C] = e.Flags;
  Put32(p + 0x24, e.SaveType);
  const uint32_t members[] = {
      e.MainThreadExecPriority, e.MainThreadGroupId,
      e.MainThreadWaitCounter, e.MainThreadScriptParam, e.MainThreadIp,
      e.MainThreadLoopCounter, e.MainThreadLoopAdr, e.MainThreadCallStackDepth};
  for (int i = 0; i < 8; ++i) Put32(p + 0x3958 + i * 4, members[i]);
  for (int i = 0; i < 8; ++i) {
    Put32(p + 0x3978 + i * 4, e.MainThreadReturnIds[i]);
    Put32(p + 0x3998 + i * 4, e.MainThreadReturnBufIds[i]);
  }
  Put32(p + 0x39BC, e.MainThreadScriptBufferId);
  for (int i = 0; i < 16; ++i)
    Put32(p + 0x39C0 + i * 4, e.MainThreadVariables[i]);
  Put32(p + 0x3A00, e.MainThreadDialoguePageId);
  uint16_t sum = 0x4716, xors = 0x1482;
  for (size_t i = 6; i < 0x42A8; i += 2) {
    uint16_t word = p[i] | uint16_t(p[i + 1]) << 8;
    sum += word;
    xors ^= word;
  }
  p[2] = sum & 255;
  p[3] = sum >> 8;
  p[4] = xors & 255;
  p[5] = xors >> 8;
}

void SaveSystem::DecodeEntry(Entry& e) {
  auto* p = e.State.data();
  e.Status = p[0];
  e.PlayTime = U32(p + 0x10);
  e.SwTitle = U32(p + 0x14);
  e.Flags = p[0x1C];
  e.SaveType = U32(p + 0x24);
  e.SaveDate = {};
  e.SaveDate.tm_year = (p[8] | p[9] << 8) - 1900;
  e.SaveDate.tm_mon = p[11] - 1;
  e.SaveDate.tm_mday = p[10];
  e.SaveDate.tm_sec = p[12];
  e.SaveDate.tm_min = p[13];
  e.SaveDate.tm_hour = p[14];
  e.MainThreadExecPriority = U32(p + 0x3958);
  e.MainThreadGroupId = U32(p + 0x395C);
  e.MainThreadWaitCounter = U32(p + 0x3960);
  e.MainThreadScriptParam = U32(p + 0x3964);
  e.MainThreadIp = U32(p + 0x3968);
  e.MainThreadLoopCounter = U32(p + 0x396C);
  e.MainThreadLoopAdr = U32(p + 0x3970);
  e.MainThreadCallStackDepth = U32(p + 0x3974);
  for (int i = 0; i < 8; ++i) {
    e.MainThreadReturnIds[i] = U32(p + 0x3978 + i * 4);
    e.MainThreadReturnBufIds[i] = U32(p + 0x3998 + i * 4);
  }
  e.MainThreadScriptBufferId = U32(p + 0x39BC);
  for (int i = 0; i < 16; ++i)
    e.MainThreadVariables[i] = U32(p + 0x39C0 + i * 4);
  e.MainThreadDialoguePageId = U32(p + 0x3A00);
}

void SaveSystem::SaveMemory() {
  const uint32_t id = ScrWork[SW_MAINTHDP] & 0x7FFFFFFF;
  if (id >= MaxThreads) return;
  auto& thd = ThreadPool[id];
  if (thd.GroupId < 5 || thd.GroupId > 7 || thd.CallStackDepth > 8) return;
  auto& e = WorkingEntry;
  e.Status = 1;
  e.PlayTime = ScrWork[SW_PLAYTIME];
  e.SwTitle = ScrWork[SW_TITLE];
  memcpy(e.State.data() + 0x80, &FlagWork[50], 50);
  memcpy(e.State.data() + 0xB2, &FlagWork[300], 100);
  for (int i = 0; i < 600; ++i)
    Put32(e.State.data() + 0x118 + i * 4, ScrWork[1000 + i]);
  for (int i = 0; i < 3000; ++i)
    Put32(e.State.data() + 0xA78 + i * 4, ScrWork[4300 + i]);
  e.MainThreadExecPriority = thd.ExecPriority;
  e.MainThreadGroupId = thd.GroupId;
  e.MainThreadWaitCounter = thd.WaitCounter;
  e.MainThreadScriptParam = thd.ScriptParam;
  e.MainThreadIp = CheckpointId;
  e.MainThreadLoopCounter = thd.LoopCounter;
  e.MainThreadLoopAdr = thd.LoopLabelNum;
  e.MainThreadCallStackDepth = thd.CallStackDepth;
  e.MainThreadScriptBufferId = thd.ScriptBufferId;
  for (int i = 0; i < 8; ++i) {
    e.MainThreadReturnIds[i] = thd.ReturnIds[i];
    e.MainThreadReturnBufIds[i] = thd.ReturnScriptBufferIds[i];
  }
  memcpy(e.MainThreadVariables.data(), thd.Variables, 16 * sizeof(int));
  e.MainThreadDialoguePageId = thd.DialoguePageId;
  EncodeEntry(e);
  WorkingValid = true;
}

void SaveSystem::LoadEntry(SaveType type, int id) {
  if (id < 0 || id >= MaxSaveEntries || GetSaveStatus(type, id) != 1) {
    WorkingValid = false;
    ScrWork[SW_SAVEERRORCODE] = (int)SaveError::Corrupted;
    return;
  }
  WorkingEntry = *GetSaveEntry<Entry>(type, id);
  WorkingValid = true;
  ImpLog(LogLevel::Info, LogChannel::IO,
         "mo8save load type={:d} slot={:d} title={:d} checkpoint={:d}\n",
         (int)type, id, WorkingEntry.SwTitle, WorkingEntry.MainThreadIp);
}

void SaveSystem::LoadMemoryNew(LoadProcess load) {
  if (!WorkingValid) return;
  auto& e = WorkingEntry;
  if (load == LoadProcess::Vars) {
    memcpy(&FlagWork[50], e.State.data() + 0x80, 50);
    memcpy(&FlagWork[300], e.State.data() + 0xB2, 100);
    for (int i = 0; i < 600; ++i)
      ScrWork[1000 + i] = U32(e.State.data() + 0x118 + i * 4);
    for (int i = 0; i < 3000; ++i)
      ScrWork[4300 + i] = U32(e.State.data() + 0xA78 + i * 4);
    ScrWork[2112] = e.SaveType;
    // Original resource reload staging (0x43DFF0). The game script reloads
    // these backgrounds, actors, scripts and audio before restoring thread.
    const int source[] = {4311, 4312, 4313, 4310, 4331, 4342, 4343,
                          4344, 4345};
    const int dest[] = {3601, 3602, 3603, 3605, 3634, 3606, 3607,
                        3608, 3609};
    for (int i = 0; i < 9; ++i) ScrWork[dest[i]] = ScrWork[source[i]];
    for (int i = 0; i < 8; ++i) {
      ScrWork[3610 + i] = ScrWork[4507 + i * 40];
      ScrWork[3618 + i] = ScrWork[5109 + i * 40];
    }
    ScrWork[3635] = ScrWork[5991] * 0x10000 + ScrWork[5990];
    ScrWork[3636] = ScrWork[5996] * 0x10000 + ScrWork[5995];
  } else if (load == LoadProcess::Thread) {
    uint32_t id = ScrWork[SW_MAINTHDP] & 0x7FFFFFFF;
    if (id >= MaxThreads || e.MainThreadScriptBufferId >= MaxLoadedScripts ||
        e.MainThreadCallStackDepth > 8) return;
    auto script = ScriptBuffers[e.MainThreadScriptBufferId];
    if (script.size() < 12) return;
    const uint32_t table = U32(script.data() + 8);
    const uint64_t location = uint64_t(table) + uint64_t(e.MainThreadIp) * 4;
    if (location + 4 > script.size()) {
      ScrWork[SW_SAVEERRORCODE] = (int)SaveError::Corrupted;
      return;
    }
    const uint32_t address = U32(script.data() + location);
    if (address >= script.size()) {
      ScrWork[SW_SAVEERRORCODE] = (int)SaveError::Corrupted;
      return;
    }
    auto& thd = ThreadPool[id];
    thd.ExecPriority = e.MainThreadExecPriority;
    thd.GroupId = e.MainThreadGroupId;
    thd.WaitCounter = e.MainThreadWaitCounter;
    thd.ScriptParam = e.MainThreadScriptParam;
    thd.ScriptBufferId = e.MainThreadScriptBufferId;
    thd.IpOffset = address;
    thd.LoopCounter = e.MainThreadLoopCounter;
    thd.LoopLabelNum = e.MainThreadLoopAdr;
    thd.CallStackDepth = e.MainThreadCallStackDepth;
    for (int i = 0; i < 8; ++i) {
      thd.ReturnIds[i] = e.MainThreadReturnIds[i];
      thd.ReturnScriptBufferIds[i] = e.MainThreadReturnBufIds[i];
    }
    memcpy(thd.Variables, e.MainThreadVariables.data(), 16 * sizeof(int));
    thd.DialoguePageId = e.MainThreadDialoguePageId;
    CheckpointId = e.MainThreadIp;
    ImpLog(LogLevel::Info, LogChannel::IO,
           "mo8save restored thread={:d} buffer={:d} checkpoint={:d} ip={:x}\n",
           id, thd.ScriptBufferId, CheckpointId, thd.IpOffset);
  }
}

void SaveSystem::CaptureThumbnail(Entry& e) {
  auto pixels = Renderer->GetSpriteSheetImage(WorkingSaveThumbnail.Sheet);
  int width = (int)WorkingSaveThumbnail.Bounds.Width;
  int height = (int)WorkingSaveThumbnail.Bounds.Height;
  if (pixels.size() < (size_t)width * height * 4 || width == 0 || height == 0)
    return;
  Texture tex;
  tex.Init(TexFmt_RGBA, 240, 135);
  for (int y = 0; y < 135; ++y) {
    for (int x = 0; x < 240; ++x) {
      size_t src = ((y * height / 135) * width + x * width / 240) * 4;
      size_t dst = (y * 240 + x);
      uint16_t rgb = ((pixels[src] & 0xF8) << 8) |
                     ((pixels[src + 1] & 0xFC) << 3) | (pixels[src + 2] >> 3);
      e.Thumbnail[dst * 2] = rgb & 255;
      e.Thumbnail[dst * 2 + 1] = rgb >> 8;
      std::copy_n(pixels.data() + src, 4, tex.Buffer.data() + dst * 4);
    }
  }
  if (e.SaveThumbnail.Sheet.Texture)
    Renderer->FreeTexture(e.SaveThumbnail.Sheet.Texture);
  e.SaveThumbnail.Sheet = SpriteSheet(240, 135);
  e.SaveThumbnail.Bounds = RectF(0, 0, 240, 135);
  e.SaveThumbnail.Sheet.Texture = tex.Submit();
}

void SaveSystem::SaveThumbnailData() {
  // Captured once when the slot is flushed, before asynchronous disk writes.
}

void SaveSystem::SetSaveFlags(SaveType type, int id, uint8_t flags) {
  if (id < 0 || id >= MaxSaveEntries) return;
  auto* e = GetSaveEntry<Entry>(type, id);
  e->Flags = flags;
  EncodeEntry(*e);
}

SaveError SaveSystem::ReadSlots(std::vector<QueuedTexture>& textures) {
  // The original compressed container has not been reconstructed. Keep an
  // explicitly versioned host sidecar rather than claim RAD save compatibility.
  const std::string path = SaveFilePath + ".host-slots";
  if (!std::filesystem::exists(path)) return SaveError::OK;
  std::ifstream file(path, std::ios::binary);
  std::array<uint8_t, 24> header{};
  std::vector<uint8_t> data(SlotPayload);
  if (!file.read((char*)header.data(), header.size()) ||
      memcmp(header.data(), "MO8HOST1", 8) || U32(header.data() + 8) != 1 ||
      U32(header.data() + 12) != SlotPayload ||
      !file.read((char*)data.data(), data.size()) ||
      file.peek() != std::char_traits<char>::eof() ||
      U32(header.data() + 16) != Crc(data))
    return SaveError::Corrupted;
  std::array<bool, MaxSaveEntries> seen{};
  for (int i = 0; i < MaxSaveEntries; ++i) {
    uint8_t id = data[i];
    if (id >= MaxSaveEntries || seen[id]) return SaveError::Corrupted;
    seen[id] = true;
  }
  size_t pos = MaxSaveEntries;
  // Validate every entry before exposing any slot or queuing its texture.
  for (int slot = 0; slot < MaxSaveEntries * 2; ++slot) {
    auto* p = data.data() + pos + slot * SlotSize;
    if (p[0] == 0) continue;
    if (p[0] != 1 || U32(p + 0x3974) > 8 ||
        U32(p + 0x39BC) >= MaxLoadedScripts || U32(p + 0x3A00) >= 3 ||
        U32(p + 0x395C) < 5 || U32(p + 0x395C) > 7)
      return SaveError::Corrupted;
  }
  for (auto* entries : {&FullEntries, &QuickEntries}) {
    for (auto& e : *entries) {
      memcpy(e.State.data(), data.data() + pos, e.State.size());
      pos += e.State.size();
      memcpy(e.Thumbnail.data(), data.data() + pos, e.Thumbnail.size());
      pos += e.Thumbnail.size();
      DecodeEntry(e);
      if (e.Status == 0) continue;
      e.SaveThumbnail.Sheet = SpriteSheet(240, 135);
      e.SaveThumbnail.Bounds = RectF(0, 0, 240, 135);
      QueuedTexture q{.Id = std::ref(e.SaveThumbnail.Sheet.Texture)};
      q.Tex.Init(TexFmt_RGBA, 240, 135);
      for (size_t i = 0; i < e.Thumbnail.size() / 2; ++i) {
        uint16_t rgb = e.Thumbnail[i * 2] | e.Thumbnail[i * 2 + 1] << 8;
        q.Tex.Buffer[i * 4] = ((rgb >> 11) * 255 + 15) / 31;
        q.Tex.Buffer[i * 4 + 1] = (((rgb >> 5) & 63) * 255 + 31) / 63;
        q.Tex.Buffer[i * 4 + 2] = ((rgb & 31) * 255 + 15) / 31;
        q.Tex.Buffer[i * 4 + 3] = 255;
      }
      textures.push_back(std::move(q));
    }
  }
  std::copy_n(data.begin(), MaxSaveEntries, QuickSaveRecentSortedId.begin());
  ImpLog(LogLevel::Info, LogChannel::IO, "mo8save mounted host slots\n");
  return SaveError::OK;
}

SaveError SaveSystem::WriteSlots() {
  std::vector<uint8_t> data(SlotPayload);
  std::copy(QuickSaveRecentSortedId.begin(), QuickSaveRecentSortedId.end(),
            data.begin());
  size_t pos = MaxSaveEntries;
  for (auto* entries : {&FullEntries, &QuickEntries}) {
    for (auto& e : *entries) {
      memcpy(data.data() + pos, e.State.data(), e.State.size());
      pos += e.State.size();
      memcpy(data.data() + pos, e.Thumbnail.data(), e.Thumbnail.size());
      pos += e.Thumbnail.size();
    }
  }
  std::array<uint8_t, 24> header{};
  memcpy(header.data(), "MO8HOST1", 8);
  Put32(header.data() + 8, 1);
  Put32(header.data() + 12, SlotPayload);
  Put32(header.data() + 16, Crc(data));
  std::string path = SaveFilePath + ".host-slots";
  std::ofstream file(path + ".tmp", std::ios::binary | std::ios::trunc);
  file.write((const char*)header.data(), header.size());
  file.write((const char*)data.data(), data.size());
  file.flush();
  if (!file) return SaveError::Failed;
  file.close();
  if (file.fail()) return SaveError::Failed;
  std::error_code ec;
  std::filesystem::rename(path + ".tmp", path, ec);
  if (ec) return SaveError::Failed;
  ImpLog(LogLevel::Info, LogChannel::IO,
         "mo8save persisted bytes={:d} crc={:08x}\n", data.size(), Crc(data));
  return SaveError::OK;
}

}  // namespace MO8
}  // namespace Impacto
