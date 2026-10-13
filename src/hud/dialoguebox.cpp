#include "dialoguebox.h"

#include "mo6tw/dialoguebox.h"
#include "chlcc/dialoguebox.h"
#include "cc/dialoguebox.h"

#include "../text/dialoguepage.h"
#include "../renderer/renderer.h"
#include "../profile/dialogue.h"
#include "../profile/game.h"
#include "../mem.h"
#include "../log.h"
#include "../profile/scriptvars.h"
#include "../profile/vm.h"
#include "../vm/vm.h"
#include "../character2d.h"

namespace Impacto {

using namespace Impacto::Profile::ScriptVars;
using namespace Impacto::Profile::Dialogue;

std::unique_ptr<DialogueBox> DialogueBox::Create(const DialoguePage& page) {
  switch (DialogueBoxCurrentType) {
    default:
      ImpLog(LogLevel::Warning, LogChannel::General,
             "Attempting to create unexpected DialogueBox type {:d}; "
             "defaulting to Void",
             static_cast<int>(DialogueBoxCurrentType));
      [[fallthrough]];
    case DialogueBoxType::None:
      return std::make_unique<VoidDialogueBox>(page);

    case DialogueBoxType::MO6TW:
      return std::make_unique<MO6TW::DialogueBox>(page);

    case DialogueBoxType::CHLCC:
      return std::make_unique<CHLCC::DialogueBox>(page);

    case DialogueBoxType::CC:
      return std::make_unique<CC::DialogueBox>(page);

    case DialogueBoxType::Plain:
      return std::make_unique<PlainDialogueBox>(page);
  }
}

glm::vec2 DialogueBox::GetScrWorkPos() const {
  using namespace Impacto::Profile::ScriptVars;
  const int offset = ParentPage.get().Id * Profile::Vm::ScrWorkMesStructSize;
  return {ScrWork[SW_MESWIN0POSX + offset], ScrWork[SW_MESWIN0POSY + offset]};
}

PlainDialogueBox::PlainDialogueBox(const DialoguePage& page)
    : DialogueBox(page) {
  using namespace UI::Widgets;

  Sprite nullSprite = Sprite();
  nullSprite.Bounds = RectF(0.0f, 0.0f, 0.0f, 0.0f);

  if (HasAutoButton) {
    ControlButtons.emplace_back(std::make_unique<Button>(
        0, AutoButtonSprite, AutoButtonSprite, nullSprite, AutoButtonPosition));
  }

  if (HasSkipButton) {
    ControlButtons.emplace_back(std::make_unique<Button>(
        0, SkipButtonSprite, SkipButtonSprite, nullSprite, SkipButtonPosition));
  }

  if (HasBacklogButton) {
    ControlButtons.emplace_back(
        std::make_unique<Button>(0, BacklogButtonSprite, BacklogButtonSprite,
                                 nullSprite, BacklogButtonPosition));
  }

  if (HasMenuButton) {
    ControlButtons.emplace_back(std::make_unique<Button>(
        0, MenuButtonSprite, MenuButtonSprite, nullSprite, MenuButtonPosition));
  }
}

void PlainDialogueBox::Show() {
  DialogueBox::Show();
  VisibilityState = Shown;
}

void PlainDialogueBox::Hide() {
  DialogueBox::Hide();
  VisibilityState = Hidden;
}

void PlainDialogueBox::UpdateInput(float dt) {
  DialogueBox::UpdateInput(dt);
  UpdateControlButtons(dt);
}

void PlainDialogueBox::Render(const DialoguePageMode mode,
                              const NameInfo& nameInfo, glm::vec4 tint) {
  tint = glm::vec4(glm::vec3(ScrWorkGetColor(SW_MESWINDOW_COLOR)), tint.a);

  if (mode == DPM_ADV) {
    Renderer->DrawSprite(ADVBoxSprite, ADVBoxPos, tint);
    RenderControlButtons(tint);

    if (HasSpeakerPortraits &&
        Profile::Vm::GameInstructionSet == Vm::InstructionSet::MO8) {
      // MO8 chooses one explicit portrait, or the first displayed speaker.
      if (!GetFlag(SF_FACEEX1DISP - 1)) {
        int slot = GetFlag(SF_FACEEX1DISP) ? 0
                   : GetFlag(SF_FACEEX1DISP + 1) ? 1 : -1;
        int source = slot >= 0 ? 8 + slot : -1;
        CharacterPortrait2D* portrait = nullptr;
        if (slot >= 0) {
          const int buffer = ScrWork[SW_FACE1SURF + slot];
          if (buffer >= 0 && buffer < std::ssize(SpeakerPortraits)) {
            portrait = &SpeakerPortraits[buffer];
            portrait->UpdateState(slot);
          }
        } else {
          const int mask = ParentPage.get().PortraitSpeakerMask;
          for (int i = 0; i < 8; ++i) {
            if (!(mask & (1 << i)) || !GetFlag(SF_CHA1DISP + i)) continue;
            const int buffer = ScrWork[SW_CHA1SURF + i];
            if (buffer >= 0 && buffer < std::ssize(CharacterPortraits2D)) {
              source = i;
              portrait = &CharacterPortraits2D[buffer];
              portrait->UpdateFace(
                  ScrWork[SW_CHA1FACE + Profile::Vm::ScrWorkChaStructSize * i]);
              portrait->Position =
                  glm::vec2(ScrWork[SW_FACEPOSX], ScrWork[SW_FACEPOSY]) *
                  glm::vec2(Profile::Game::DesignWidth / 1280.0f,
                            Profile::Game::DesignHeight / 720.0f);
              portrait->Tint = ScrWorkGetColor(
                  SW_CHA1FILTER + Profile::Vm::ScrWorkChaStructSize * i);
              portrait->Scale = glm::vec2(1.0f);
              portrait->Rotation = glm::quat();
            }
            break;
          }
        }
        if (portrait) {
          portrait->Show = true;
          portrait->Tint.a = tint.a;
          portrait->Position +=
              glm::vec2(SpeakerPortraitBaseOffsetX, SpeakerPortraitBaseOffsetY);
          portrait->Render(-1);
        }
        if (getenv("IMPACTO_PORTRAIT_TRACE")) {
          static std::array<std::array<int, 4>, 3> last{};
          const int page = ParentPage.get().Id;
          const std::array<int, 4> state = {
              source, portrait ? static_cast<int>(portrait->Status) : -1,
              portrait ? portrait->Face : -1,
              ParentPage.get().PortraitSpeakerMask};
          if (last[page] != state) {
            last[page] = state;
            ImpLog(LogLevel::Warning, LogChannel::General,
                   "[portraittrace] draw page={} source={} status={} face={} "
                   "mask=0x{:x} alpha={:.3f}\n",
                   page, source, state[1], state[2] >> 16, state[3], tint.a);
          }
        }
      }
    } else if (!nameInfo.Name.empty()) {
      if (HasSpeakerPortraits) {
        // Draw Face
        for (int i = 0; i < std::ssize(SpeakerPortraits); i++) {
          int bufId = ScrWork[SW_FACE1SURF + i];
          SpeakerPortraits[bufId].UpdateState(i);

          SpeakerPortraits[bufId].Tint = tint;
          SpeakerPortraits[bufId].Position +=
              glm::vec2(SpeakerPortraitBaseOffsetX, SpeakerPortraitBaseOffsetY);

          SpeakerPortraits[bufId].Render(-1);
        }
      }
    }

    NametagDisplayInst->Render(nameInfo, tint);

  } else {
    assert(mode == DPM_REV);

    glm::vec4 nvlBoxTint(0.0f, 0.0f, 0.0f, tint.a * NVLBoxMaxOpacity);
    Renderer->DrawQuad(
        RectF(0, 0, Profile::Game::DesignWidth, Profile::Game::DesignHeight),
        nvlBoxTint);
  }
}

void PlainDialogueBox::UpdateControlButtons(float dt) {
  for (auto& button : ControlButtons) {
    button->UpdateInput(dt);
    button->Update(dt);
  }
}

void PlainDialogueBox::RenderControlButtons(glm::vec4 col) {
  for (auto& button : ControlButtons) {
    button->Tint = col;
    button->Render();
  }
}

void VoidDialogueBox::Show() {
  DialogueBox::Show();
  VisibilityState = Shown;
}

void VoidDialogueBox::Hide() {
  DialogueBox::Hide();
  VisibilityState = Hidden;
}

}  // namespace Impacto
