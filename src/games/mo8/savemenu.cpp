#include "savemenu.h"

#include "../../profile/ui/savemenu.h"
#include "../../profile/games/mo8/savemenu.h"
#include "../../profile/dialogue.h"
#include "../../profile/vm.h"
#include "../../renderer/renderer.h"
#include "../../mem.h"
#include "../../profile/scriptvars.h"
#include "../../inputsystem.h"
#include "../../vm/interface/input.h"
#include "../../vm/vm.h"

#include <algorithm>
#include <fmt/format.h>

namespace Impacto {
namespace UI {
namespace MO8 {

using namespace Impacto::Profile::SaveMenu;
using namespace Impacto::Profile::MO8::SaveMenu;
using namespace Impacto::Profile::ScriptVars;
using namespace Impacto::Vm::Interface;
using namespace Impacto::UI::Widgets;

namespace {
// MO8's table 2 contains two u32 MSB IDs per title (script 9, label 16).
// ScriptGetTextTableStrAddress uses the older u16 SCX string-table format.
std::vector<ProcessedTextGlyph> SaveTitleLine(int title, int line,
                                            glm::vec2 pos) {
  const auto table = Vm::TextTable[2];
  if (title < 0 || title >= 206 || table.scriptBufferId >= Vm::MaxLoadedScripts)
    return {};
  const auto script = Vm::ScriptBuffers[table.scriptBufferId];
  const size_t offset = table.labelAdr + (title * 2 + line) * sizeof(uint32_t);
  if (!table.labelAdr || offset + 4 > script.size()) return {};
  const uint32_t id = SDL_Swap32LE(UnalignedRead<uint32_t>(&script[offset]));
  const auto msb = Vm::MsbBuffers[table.scriptBufferId];
  if (msb.size() < 16) return {};
  const auto read = [&msb](size_t at) {
    return SDL_Swap32LE(UnalignedRead<uint32_t>(&msb[at]));
  };
  const uint32_t languages = read(4);
  const size_t strings = read(12);
  if (!languages || Profile::Vm::MsbLanguage < 0 ||
      static_cast<uint32_t>(Profile::Vm::MsbLanguage) >= languages ||
      strings > msb.size())
    return {};
  const size_t stride = (size_t(languages) + 1) * sizeof(uint32_t);
  for (size_t at = 16; at + stride <= strings; at += stride) {
    if (read(at) != id) continue;
    const size_t address = strings + read(at + (1 + Profile::Vm::MsbLanguage) * 4);
    if (address >= msb.size()) return {};
    Vm::Sc3VmThread dummy;
    dummy.ScriptBufferId = table.scriptBufferId + Vm::MsbScriptBufferAliasBase;
    dummy.IpOffset = static_cast<uint32_t>(address);
    return TextLayoutPlainLine(
        &dummy, 255, *Profile::Dialogue::DialogueFont, EntryFontSize,
        Profile::Dialogue::ColorTable[0], 1.0f, pos, TextAlignment::Left);
  }
  return {};
}
}  // namespace

SaveSystem::SaveType SaveMenu::EntryType() const {
  return DisplayedMenuType == SaveMenuPageType::QuickLoad
             ? SaveSystem::SaveType::Quick
             : SaveSystem::SaveType::Full;
}

void SaveMenu::MenuButtonOnClick(Button* target) {
  if (!SaveSystem::Implementation) return;
  const auto type = EntryType();
  const int status = SaveSystem::GetSaveStatus(type, target->Id);
  if (status != 1 && DisplayedMenuType != SaveMenuPageType::Save) return;
  // Keep native script enums: Full=1, Quick=2. VM translates to engine 0/1.
  // Quick IDs follow the query/LoadEntry API's sorted display-position contract.
  ScrWork[SW_SAVEFILENO] = target->Id;
  ScrWork[SW_SAVEFILESTATUS] = status;
  ScrWork[SW_SAVEFILETYPE] = type == SaveSystem::SaveType::Full ? 1 : 2;
  SetFlag(SF_SAVEPROTECTED,
          (SaveSystem::GetSaveFlags(type, target->Id) & SaveSystem::WriteProtect) !=
              0);
  ChoiceMade = true;
  ImpLog(LogLevel::Info, LogChannel::IO,
         "mo8save selected menu={:d} slot={:d} status={:d}\n",
         (int)DisplayedMenuType, target->Id, status);
}

SaveMenu::SaveMenu() : UI::SaveMenu() {
  FadeAnimation.Direction = AnimationDirection::In;
  FadeAnimation.LoopMode = AnimationLoopMode::Stop;
  FadeAnimation.DurationIn = FadeInDuration;
  FadeAnimation.DurationOut = FadeOutDuration;

  MainItems = std::make_unique<Group>(this);
  MainItems->FocusLock = false;
  for (int i = 0; i < EntriesPerPage; ++i) {
    Entries[i] = new Button(
        i, EntrySprite, EntryHighlightedSprite, EntryFocusBorderSprite,
        {EntryStartX + (i / RowsPerPage) * EntryXPadding,
         EntryStartY + (i % RowsPerPage) * EntryYPadding});
    Entries[i]->OnClickHandler = [this](auto* btn) { MenuButtonOnClick(btn); };
    MainItems->Add(Entries[i]);
  }

  PageControls = std::make_unique<Group>(this);
  PageControls->FocusLock = false;
  auto next = new Button(1, NextButtonSprite, NextButtonHighlightedSprite,
                         NextButtonHighlightedSprite, NextButtonPosition);
  auto previous = new Button(-1, BackButtonSprite, BackButtonHighlightedSprite,
                             BackButtonHighlightedSprite, BackButtonPosition);
  next->OnClickHandler = [this](auto*) { ChangePage(1); };
  previous->OnClickHandler = [this](auto*) { ChangePage(-1); };
  PageControls->Add(previous);
  PageControls->Add(next);
}

void SaveMenu::FocusEntry(int id) {
  if (CurrentlyFocusedElement) CurrentlyFocusedElement->HasFocus = false;
  FocusedEntry = id;
  CurrentlyFocusedElement = Entries[id];
  CurrentlyFocusedElement->HasFocus = true;
}

void SaveMenu::ChangePage(int direction) {
  ActivePage = (ActivePage + PageCount + direction) % PageCount;
  RefreshCurrentEntryInfo();
}

void SaveMenu::Cancel() {
  ImpLog(LogLevel::Info, LogChannel::IO, "mo8save menu cancel\n");
  // Leave Back for script 7's ControlOnJump. Flag 1805 is return-to-title,
  // which the script checks independently before processing menu input.
}

void SaveMenu::Show() {
  if (State == Shown || State == Showing) return;
  DisplayedMenuType = ActiveMenuType.value_or(SaveMenuPageType::Load);
  ActivePage = 0;
  ChoiceMade = false;
  LastSubMenuCount = ScrWork[SW_SYSSUBMENUCT];
  RefreshCurrentEntryInfo();
  MainItems->Show();
  PageControls->Show();
  FocusEntry(0);
  LastFocusedMenu = nullptr;
  Menu::Show();
  FadeAnimation.StartIn();
  ImpLog(LogLevel::Info, LogChannel::IO, "mo8save menu show type={:d} count={:d}\n",
         (int)DisplayedMenuType, LastSubMenuCount);
}

void SaveMenu::Hide() {
  if (State == Hidden || State == Hiding) return;
  State = Hiding;
  IsFocused = false;
  FadeAnimation.StartOut();
  ImpLog(LogLevel::Info, LogChannel::IO, "mo8save menu hide count={:d} menu={:d}\n",
         ScrWork[SW_SYSSUBMENUCT], ScrWork[SW_SYSSUBMENUNO]);
}

void SaveMenu::UpdateInput(float dt) {
  if (!IsFocused) return;
  if (State != Shown || ChoiceMade || GetFlag(SF_SUBMENUEXIT)) {
    PADinputButtonWentDown &= ~PADcustom[5];
    PADinputMouseWentDown &= ~PADcustom[5];
    return;
  }
  if (GetControlState(ControlType::Back)) {
    Cancel();
  } else {
    const auto pad = PADinputButtonRepeatAccelDown;
    int row = FocusedEntry % RowsPerPage;
    int column = FocusedEntry / RowsPerPage;
    if (pad & PAD1UP) {
      FocusEntry(column * RowsPerPage + (row + RowsPerPage - 1) % RowsPerPage);
    } else if (pad & PAD1DOWN) {
      FocusEntry(column * RowsPerPage + (row + 1) % RowsPerPage);
    } else if (pad & PAD1LEFT) {
      if (column == 0) ChangePage(-1);
      FocusEntry((1 - column) * RowsPerPage + row);
    } else if (pad & PAD1RIGHT) {
      if (column == 1) ChangePage(1);
      FocusEntry((1 - column) * RowsPerPage + row);
    } else if (PADinputButtonWentDown & PAD1L1) {
      ChangePage(-1);
    } else if (PADinputButtonWentDown & PAD1R1) {
      ChangePage(1);
    }
    const bool cursor = Input::CurrentInputDevice == Input::Device::Mouse ||
                        Input::CurrentInputDevice == Input::Device::Touch;
    // Focus before Button processes a same-tick move+click.
    if (cursor) {
      for (int i = 0; i < EntriesPerPage; ++i) {
        Entries[i]->Hovered = Entries[i]->Bounds.ContainsPoint(Input::CurMousePos);
        if (Entries[i]->Hovered) FocusEntry(i);
      }
      for (auto* button : PageControls->Children) {
        button->Hovered = button->Bounds.ContainsPoint(Input::CurMousePos);
        if (button->Hovered) {
          if (CurrentlyFocusedElement) CurrentlyFocusedElement->HasFocus = false;
          CurrentlyFocusedElement = button;
          button->HasFocus = true;
        }
      }
    }
    MainItems->UpdateInput(dt);
    PageControls->UpdateInput(dt);
  }
  // Script confirmation must come only from InstSaveMenu consuming ChoiceMade.
  // Otherwise an empty Load slot or a NEXT click also appears as a slot OK.
  PADinputButtonWentDown &= ~PADcustom[5];
  PADinputMouseWentDown &= ~PADcustom[5];
}

void SaveMenu::RefreshCurrentEntryInfo() {
  if (!SaveSystem::Implementation) return;
  const auto type = EntryType();
  const auto text = [](std::string const& value, glm::vec2 position,
                       TextAlignment alignment = TextAlignment::Left) {
    return TextLayoutPlainString(
        value, *Profile::Dialogue::DialogueFont, EntryFontSize,
        Profile::Dialogue::ColorTable[0], 1.0f, position, alignment);
  };
  for (int i = 0; i < EntriesPerPage; ++i) {
    auto* entry = Entries[i];
    entry->Id = ActivePage * EntriesPerPage + i;
    const glm::vec2 pos = entry->Bounds.GetPos();
    EntryNumbers[i] = text(fmt::format("{:02}", entry->Id + 1),
                           pos + EntryNumberOffset);
    EntryTitles[i].clear();
    EntryPlayTimes[i].clear();
    EntrySaveDates[i].clear();
    if (SaveSystem::GetSaveStatus(type, entry->Id) != 1) continue;
    const int title = SaveSystem::GetSaveTitle(type, entry->Id);
    EntryTitles[i] = SaveTitleLine(title, 0, pos + EntryTitleOffset);
    auto second = SaveTitleLine(
        title, 1, pos + EntryTitleOffset + glm::vec2(0.0f, 36.0f));
    EntryTitles[i].insert(EntryTitles[i].end(), second.begin(), second.end());
    SquishText(EntryTitles[i], 390.0f, pos.x + EntryTitleOffset.x);
    const uint32_t seconds = SaveSystem::GetSavePlayTime(type, entry->Id);
    EntryPlayTimes[i] = text(
        fmt::format("{:02}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60,
                    seconds % 60),
        pos + EntryPlayTimeOffset);
    const auto& date = SaveSystem::GetSaveDate(type, entry->Id);
    EntrySaveDates[i] = text(
        fmt::format("{:04}/{:02}/{:02} {:02}:{:02}", date.tm_year + 1900,
                    date.tm_mon + 1, date.tm_mday, date.tm_hour, date.tm_min),
        pos + EntrySaveDateOffset);
    SquishText(EntrySaveDates[i], 248.0f, pos.x + EntrySaveDateOffset.x);
  }
  PageNumber = text(fmt::format("{} / {}", ActivePage + 1, PageCount),
                    PageNumberPosition, TextAlignment::Center);
}

void SaveMenu::Update(float dt) {
  const int count = ScrWork[SW_SYSSUBMENUCT];
  const int menu = ScrWork[SW_SYSSUBMENUNO];
  const bool saveMenu = menu == 0 || menu == 3 || menu == 4;
  if (saveMenu && count > LastSubMenuCount && State == Hidden) {
    ActiveMenuType = menu == 0 ? SaveMenuPageType::QuickLoad
                              : menu == 3 ? SaveMenuPageType::Save
                                          : SaveMenuPageType::Load;
    Show();
  }
  // Title fades up to 16, story fades up to 32. A falling script count
  // starts closing in either context, including switches to another submenu.
  if ((!saveMenu || count == 0 || count < LastSubMenuCount) &&
      (State == Showing || State == Shown))
    Hide();
  LastSubMenuCount = count;
  FadeAnimation.Update(dt);
  if (State == Showing && count >= 16 && FadeAnimation.IsIn()) State = Shown;
  if (State == Hiding && FadeAnimation.IsOut()) {
    State = Hidden;
    if (UI::FocusedMenu == this) {
      while (LastFocusedMenu &&
             (LastFocusedMenu->State == Hidden ||
              LastFocusedMenu->State == Hiding))
        LastFocusedMenu = LastFocusedMenu->LastFocusedMenu;
      UI::FocusedMenu = LastFocusedMenu;
      if (LastFocusedMenu) LastFocusedMenu->IsFocused = true;
    }
  }
  if (State == Hidden) return;
  MainItems->Update(dt);
  PageControls->Update(dt);
  UpdateInput(dt);
}

void SaveMenu::Render() {
  if (State == Hidden) return;
  const glm::vec4 tint(1.0f, 1.0f, 1.0f, FadeAnimation.Progress);
  Renderer->DrawSprite(SaveMenuBackgroundSprite, glm::vec2(0.0f), tint);
  const Sprite& heading = DisplayedMenuType == SaveMenuPageType::QuickLoad
                              ? QuickLoadTextSprite
                          : DisplayedMenuType == SaveMenuPageType::Save
                              ? SaveTextSprite
                              : LoadTextSprite;
  Renderer->DrawSprite(heading, MenuTitleTextPos, tint);
  const auto type = EntryType();
  for (int i = 0; i < EntriesPerPage; ++i) {
    const auto* entry = Entries[i];
    const glm::vec2 pos = entry->Bounds.GetPos();
    Renderer->DrawSprite(entry->HasFocus ? EntryHighlightedSprite : EntrySprite,
                         pos, tint);
    const bool occupied = SaveSystem::Implementation &&
                          SaveSystem::GetSaveStatus(type, entry->Id) == 1;
    if (occupied) {
      const auto& thumb = SaveSystem::GetSaveThumbnail(type, entry->Id);
      if (thumb.Sheet.Texture != 0)
        Renderer->DrawSprite(thumb,
                             RectF(pos.x + ThumbnailOffset.x,
                                   pos.y + ThumbnailOffset.y, ThumbnailSize.x,
                                   ThumbnailSize.y),
                             tint);
      if (SaveSystem::GetSaveFlags(type, entry->Id) & SaveSystem::WriteProtect)
        Renderer->DrawSprite(EntryLockSprite, pos + EntryLockOffset, tint);
    }
    if (entry->HasFocus)
      Renderer->DrawSprite(EntryFocusBorderSprite, pos, tint);
    for (const auto* text : {&EntryNumbers[i], &EntryTitles[i],
                            &EntryPlayTimes[i], &EntrySaveDates[i]})
      Profile::Dialogue::DialogueFont->DrawProcessedText(
          *text, tint.a, RendererOutlineMode::Full);
  }
  PageControls->Tint = tint;
  PageControls->Render();
  Profile::Dialogue::DialogueFont->DrawProcessedText(PageNumber, tint.a,
                                                    RendererOutlineMode::Full);
}

}  // namespace MO8
}  // namespace UI
}  // namespace Impacto
