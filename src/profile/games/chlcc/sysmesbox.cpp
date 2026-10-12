#include "sysmesbox.h"
#include "../../profile_internal.h"

#include "../../../ui/ui.h"
#include "../../../games/chlcc/sysmesbox.h"
#include "../../../game.h"

namespace Impacto {
namespace Profile {
namespace CHLCC {
namespace SysMesBox {

void Configure() {
  Box = EnsureGetMember<Sprite>("Box");
  BoxDecoration = EnsureGetMember<Sprite>("BoxDecoration");
  SelectionLeftPart = EnsureGetMember<Sprite>("SelectionLeftPart");
  SelectionRightPart = EnsureGetMember<Sprite>("SelectionRightPart");
  SelectionMiddlePart = EnsureGetMember<Sprite>("SelectionMiddlePart");
  // The CHLCC loading-star animation is CHLCC-specific; games that reuse
  // this sysmesbox type without it (MO8) omit the members.
  LoadingStar = TryGetMember<Sprite>("LoadingStar").value_or(Sprite());

  BoxX = EnsureGetMember<float>("BoxX");
  BoxY = EnsureGetMember<float>("BoxY");
  ChoicePadding = EnsureGetMember<float>("ChoicePadding");
  ChoiceY = EnsureGetMember<float>("ChoiceY");
  ChoiceXBase = EnsureGetMember<float>("ChoiceXBase");
  MinMaxMesWidth = EnsureGetMember<float>("MinMaxMesWidth");
  MinHighlightWidth = EnsureGetMember<float>("MinHighlightWidth");
  HighlightBaseWidth = EnsureGetMember<float>("HighlightBaseWidth");
  HighlightRightPartSpriteWidth =
      EnsureGetMember<float>("HighlightRightPartSpriteWidth");
  HighlightYOffset = EnsureGetMember<float>("HighlightYOffset");
  HighlightXOffset = EnsureGetMember<float>("HighlightXOffset");
  HighlightXBase = EnsureGetMember<float>("HighlightXBase");
  HighlightXStep = EnsureGetMember<float>("HighlightXStep");
  LoadingStarsFadeDuration =
      TryGetMember<float>("LoadingStarsFadeDuration").value_or(0.0f);
  LoadingStarsPosition =
      TryGetMember<glm::vec2>("LoadingStarsPosition")
          .value_or(glm::vec2(0.0f));

  auto drawType = EnsureGetMember<Game::DrawComponentType>("DrawType");

  UI::SysMesBox::DrawType = drawType;
  UI::SysMesBox::Factory = []() -> UI::SysMesBox* {
    return new UI::CHLCC::SysMesBox();
  };
}

}  // namespace SysMesBox
}  // namespace CHLCC
}  // namespace Profile
}  // namespace Impacto