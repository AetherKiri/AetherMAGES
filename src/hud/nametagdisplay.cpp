#include "nametagdisplay.h"

#include "../profile/dialogue.h"
#include "chlcc/nametagdisplay.h"
#include "cc/nametagdisplay.h"
#include "../log.h"
#include "../profile/vm.h"
#include "../text/dialoguepage.h"

#include <numeric>

namespace Impacto {

using namespace Impacto::Profile::Dialogue;

std::unique_ptr<NametagDisplay> NametagDisplay::Create() {
  switch (NametagCurrentType) {
    default:
      ImpLog(LogLevel::Warning, LogChannel::General,
             "Attempted to create unexpected nametag type {:d}; defaulting to "
             "Void",
             static_cast<int>(NametagCurrentType));
      [[fallthrough]];
    case NametagType::None:
      return std::make_unique<VoidNametagDisplay>();

    case NametagType::Sprite:
      return std::make_unique<SpriteNametagDisplay>();

    case NametagType::TwoPiece:
    case NametagType::ThreePiece:
      return std::make_unique<ThreePieceNametagDisplay>();

    case NametagType::CHLCC:
      return std::make_unique<CHLCC::NametagDisplay>();

    case NametagType::CC:
      return std::make_unique<CC::NametagDisplay>();
  }
}

void VoidNametagDisplay::Render(const NameInfo& nameInfo,
                                const glm::vec4 tint) {
  if (Hidden || !nameInfo.RenderWindow || nameInfo.Name.empty()) return;

  DialogueFont->DrawProcessedText(nameInfo.Name, tint.a,
                                  RendererOutlineMode::Full);
}

void SpriteNametagDisplay::Render(const NameInfo& nameInfo,
                                  const glm::vec4 tint) {
  if (Hidden || !nameInfo.RenderWindow || nameInfo.Name.empty()) return;

  if (Profile::Vm::GameInstructionSet == Vm::InstructionSet::MO8) {
    // Game.exe 0x4334d0: fixed 72px cap, stretched 192px band,
    // and a 346px fading tail. The band has a minimum width of 128*1.5.
    const auto& mode = TextModesInfo[DPM_ADV];
    const float bandWidth = std::max(
        192.0f, std::min(GetTextWidth(nameInfo.Name), mode.MaxNameWidth));
    const glm::vec2 position = mode.NamePos - glm::vec2(72.0f, 32.0f);
    Sprite part = NametagSprite;
    part.Bounds = RectF(0, 330, 72, 104);
    Renderer->DrawSprite(part, RectF(position.x, position.y, 72, 104), tint);
    part.Bounds = RectF(72, 330, 192, 104);
    Renderer->DrawSprite(part,
                         RectF(position.x + 72, position.y, bandWidth, 104), tint);
    part.Bounds = RectF(264, 330, 346, 104);
    Renderer->DrawSprite(
        part, RectF(position.x + 72 + bandWidth, position.y, 346, 104), tint);
  } else {
    Renderer->DrawSprite(NametagSprite, NametagPosition, tint);
    DialogueFont->DrawProcessedText(nameInfo.Name, tint.a,
                                    RendererOutlineMode::Full);
  }
}

void ThreePieceNametagDisplay::Render(const NameInfo& nameInfo,
                                      const glm::vec4 tint) {
  if (Hidden || !nameInfo.RenderWindow || nameInfo.Name.empty()) return;

  Renderer->DrawSprite(NametagLeftSprite, NametagPosition, tint);

  float rightX = NametagPosition.x + NametagLeftSprite.ScaledWidth() +
                 GetTextWidth(nameInfo.Name);

  if (NametagCurrentType == NametagType::ThreePiece) {
    rightX -= NametagMiddleBaseWidth;

    // Draw middle sprites
    const float middleBegin =
        NametagPosition.x + NametagLeftSprite.ScaledWidth();

    Sprite middleSprite = NametagMiddleSprite;
    for (float x = middleBegin; x < rightX;
         x += NametagMiddleSprite.ScaledWidth()) {
      middleSprite.SetScaledWidth(
          std::min(rightX - x, NametagMiddleSprite.ScaledWidth()));
      Renderer->DrawSprite(middleSprite, {x, NametagPosition.y}, tint);
    }
  }

  Renderer->DrawSprite(NametagRightSprite, {rightX, NametagPosition.y}, tint);

  DialogueFont->DrawProcessedText(nameInfo.Name, tint.a,
                                  RendererOutlineMode::Full);
}

}  // namespace Impacto
