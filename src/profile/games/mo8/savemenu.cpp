#include "savemenu.h"
#include "../../profile_internal.h"
#include "../../../log.h"

#include "../../ui/savemenu.h"
#include "../../../game.h"
#include "../../../ui/ui.h"
#include "../../../games/mo8/savemenu.h"

namespace Impacto {
namespace Profile {
namespace MO8 {
namespace SaveMenu {

void Configure() {
  SaveMenuBackgroundSprite =
      EnsureGetMember<Sprite>("SaveMenuBackgroundSprite");

  EntryStartX = EnsureGetMember<float>("EntryStartX");
  EntryXPadding = EnsureGetMember<float>("EntryXPadding");
  EntryStartY = EnsureGetMember<float>("EntryStartY");
  EntryYPadding = EnsureGetMember<float>("EntryYPadding");
  EntrySprite = EnsureGetMember<Sprite>("EntrySprite");
  EntryHighlightedSprite = EnsureGetMember<Sprite>("EntryHighlightedSprite");
  EntryFocusBorderSprite = EnsureGetMember<Sprite>("EntryFocusBorderSprite");
  EntryLockSprite = EnsureGetMember<Sprite>("EntryLockSprite");
  ThumbnailOffset = EnsureGetMember<glm::vec2>("ThumbnailOffset");
  ThumbnailSize = EnsureGetMember<glm::vec2>("ThumbnailSize");
  EntryNumberOffset = EnsureGetMember<glm::vec2>("EntryNumberOffset");
  EntryTitleOffset = EnsureGetMember<glm::vec2>("EntryTitleOffset");
  EntryPlayTimeOffset = EnsureGetMember<glm::vec2>("EntryPlayTimeOffset");
  EntrySaveDateOffset = EnsureGetMember<glm::vec2>("EntrySaveDateOffset");
  EntryLockOffset = EnsureGetMember<glm::vec2>("EntryLockOffset");
  EntryFontSize = EnsureGetMember<float>("EntryFontSize");
  PageNumberPosition = EnsureGetMember<glm::vec2>("PageNumberPosition");

  QuickLoadTextSprite = EnsureGetMember<Sprite>("QuickLoadTextSprite");
  LoadTextSprite = EnsureGetMember<Sprite>("LoadTextSprite");
  SaveTextSprite = EnsureGetMember<Sprite>("SaveTextSprite");
  MenuTitleTextPos = EnsureGetMember<glm::vec2>("MenuTitleTextPos");

  NextButtonSprite = EnsureGetMember<Sprite>("NextButtonSprite");
  NextButtonHighlightedSprite =
      EnsureGetMember<Sprite>("NextButtonHighlightedSprite");
  NextButtonPosition = EnsureGetMember<glm::vec2>("NextButtonPosition");

  BackButtonSprite = EnsureGetMember<Sprite>("BackButtonSprite");
  BackButtonHighlightedSprite =
      EnsureGetMember<Sprite>("BackButtonHighlightedSprite");
  BackButtonPosition = EnsureGetMember<glm::vec2>("BackButtonPosition");

  auto drawType = EnsureGetMember<Game::DrawComponentType>("DrawType");

  UI::SaveMenuPtr = new UI::MO8::SaveMenu();
  UI::Menus[drawType].push_back(UI::SaveMenuPtr);
}

}  // namespace SaveMenu
}  // namespace MO8
}  // namespace Profile
}  // namespace Impacto
