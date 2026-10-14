#pragma once

#include "../../ui/menu.h"
#include "../../ui/savemenu.h"
#include "../../ui/widgets/group.h"
#include "../../ui/widgets/button.h"

#include <array>
#include <memory>

namespace Impacto {
namespace UI {
namespace MO8 {

class SaveMenu : public UI::SaveMenu {
 public:
  SaveMenu();

  void Show() override;
  void Hide() override;
  void Update(float dt) override;
  void UpdateInput(float dt) override;
  void Render() override;

  void RefreshCurrentEntryInfo() override;

  void MenuButtonOnClick(Widgets::Button* target);

 private:
  static constexpr int EntriesPerPage = 8;
  static constexpr int PageCount = SaveSystem::MaxSaveEntries / EntriesPerPage;
  std::unique_ptr<Widgets::Group> PageControls;
  std::unique_ptr<Widgets::Group> MainItems;
  std::array<Widgets::Button*, EntriesPerPage> Entries{};
  std::array<std::vector<ProcessedTextGlyph>, EntriesPerPage> EntryNumbers;
  std::array<std::vector<ProcessedTextGlyph>, EntriesPerPage> EntryTitles;
  std::array<std::vector<ProcessedTextGlyph>, EntriesPerPage> EntryPlayTimes;
  std::array<std::vector<ProcessedTextGlyph>, EntriesPerPage> EntrySaveDates;
  std::vector<ProcessedTextGlyph> PageNumber;
  int FocusedEntry = 0;
  int LastSubMenuCount = 0;
  SaveMenuPageType DisplayedMenuType = SaveMenuPageType::Load;
  SaveSystem::SaveType EntryType() const;
  void ChangePage(int direction);
  void FocusEntry(int id);
  void Cancel();
  Animation FadeAnimation;
};

}  // namespace MO8
}  // namespace UI
}  // namespace Impacto
