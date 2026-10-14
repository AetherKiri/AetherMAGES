#include "sysmesbox.h"

#include "../../profile/ui/sysmesbox.h"
#include "../../profile/games/chlcc/sysmesbox.h"
#include "../../profile/dialogue.h"
#include "../../profile/scriptvars.h"
#include "../../renderer/renderer.h"
#include "../../inputsystem.h"
#include "../../vm/interface/input.h"
#include "../../mem.h"

#include <algorithm>

namespace Impacto {
namespace UI {
namespace MO8 {

using namespace Profile::SysMesBox;
using namespace Profile::CHLCC::SysMesBox;
using namespace Profile::ScriptVars;
using namespace Vm::Interface;
using namespace Widgets;

void SysMesBox::Init() {
  ChoiceMade = false;
  MessageCount = 0;
  ChoiceCount = 0;
  CurrentlyFocusedElement = nullptr;
  MessageItems.reset();
  ChoiceItems.reset();
  std::fill(std::begin(FocusStart), std::end(FocusStart), nullptr);
  FadeAnimation.DurationIn = FadeInDuration;
  FadeAnimation.DurationOut = FadeOutDuration;
  FadeAnimation.Direction = AnimationDirection::In;
  FadeAnimation.LoopMode = AnimationLoopMode::Stop;
}

std::vector<ProcessedTextGlyph> SysMesBox::Layout(Vm::BufferOffsetContext ctx) {
  Vm::Sc3VmThread dummy;
  dummy.ScriptBufferId = ctx.ScriptBufferId;
  dummy.IpOffset = ctx.IpOffset;
  return TextLayoutPlainLine(
      &dummy, 255, *Profile::Dialogue::DialogueFont, TextFontSize,
      Profile::Dialogue::ColorTable[0], 1.0f, glm::vec2(0.0f),
      TextAlignment::Left);
}

void SysMesBox::AddMessage(Vm::BufferOffsetContext ctx) {
  if (MessageCount >= static_cast<int>(Messages.size())) return;
  Messages[MessageCount] = Layout(ctx);
  MessageWidths[MessageCount] = GetTextWidth(Messages[MessageCount]);
  ++MessageCount;
}

void SysMesBox::AddChoice(Vm::BufferOffsetContext ctx) {
  if (ChoiceCount >= static_cast<int>(Choices.size())) return;
  Choices[ChoiceCount] = Layout(ctx);
  ChoiceWidths[ChoiceCount] = GetTextWidth(Choices[ChoiceCount]);
  ++ChoiceCount;
}

void SysMesBox::FocusChoice(Widget* choice) {
  if (CurrentlyFocusedElement) CurrentlyFocusedElement->HasFocus = false;
  CurrentlyFocusedElement = choice;
  choice->HasFocus = true;
  ScrWork[SW_SYSSEL] = static_cast<Button*>(choice)->Id;
}

void SysMesBox::Show() {
  if (State == Shown || State == Showing) return;
  ChoiceItems = std::make_unique<Group>(this);
  ChoiceItems->FocusLock = false;
  // Game.exe 0x42dce0: message baseline 264*1.5, row step 34*1.5;
  // choices at 432*1.5, with 40*1.5 gaps, ending at x1488.
  float width = 0.0f;
  for (int i = 0; i < MessageCount; ++i)
    width = std::max(width, MessageWidths[i]);
  const float messageX = std::max(BoxX + 96.0f, TextX - width / 2.0f);
  for (int i = 0; i < MessageCount; ++i) {
    if (Messages[i].empty()) continue;
    const auto offset = glm::vec2(messageX, TextMiddleY + i * TextLineHeight) -
                        Messages[i].front().Position;
    for (auto& glyph : Messages[i]) glyph.Move(offset);
    SquishText(Messages[i], Box.ScaledWidth() - 192.0f, messageX);
  }
  float total = 0.0f;
  for (int i = 0; i < ChoiceCount; ++i) total += ChoiceWidths[i];
  total += std::max(ChoiceCount - 1, 0) * ChoicePadding;
  float x = ChoiceXBase - total;
  for (int i = 0; i < ChoiceCount; ++i) {
    if (!Choices[i].empty()) {
      const auto offset = glm::vec2(x, ChoiceY) - Choices[i].front().Position;
      for (auto& glyph : Choices[i]) glyph.Move(offset);
    }
    auto* button = new Button(i, Sprite(), Sprite(), Sprite(), {x, ChoiceY});
    button->Bounds = RectF(x - 24.0f, ChoiceY - 12.0f,
                           std::max(ChoiceWidths[i], TextFontSize) + 48.0f,
                           TextFontSize + 60.0f);
    button->OnClickHandler = [this](Button* target) {
      ScrWork[SW_SYSSEL] = target->Id;
      ChoiceMade = true;
    };
    ChoiceItems->Add(button, FDIR_RIGHT);
    x += ChoiceWidths[i] + ChoicePadding;
  }
  ChoiceItems->Show();
  if (!ChoiceItems->Children.empty())
    FocusChoice(ChoiceItems->Children.front());
  LastFocusedMenu = nullptr;
  Menu::Show();
  FadeAnimation.StartIn();
}

void SysMesBox::Hide() {
  if (State == Hidden || State == Hiding) return;
  // Restore underlying menu focus only after the dialog finishes fading.
  IsFocused = false;
  State = Hiding;
  FadeAnimation.StartOut();
}

void SysMesBox::Update(float dt) {
  FadeAnimation.Update(dt);
  if (State == Showing && FadeAnimation.IsIn()) State = Shown;
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
  if (State != Shown || !IsFocused || ChoiceMade || !ChoiceItems) return;
  UpdateInput(dt);
  if (CurrentlyFocusedElement)
    ScrWork[SW_SYSSEL] = static_cast<Button*>(CurrentlyFocusedElement)->Id;
  const bool back = GetControlState(ControlType::Back);
  if (back && ChoiceCount == 2) {
    // Native 0x42d990: Back focuses No; OK confirms the focused choice.
    FocusChoice(ChoiceItems->Children[1]);
  } else {
    if (Input::CurrentInputDevice == Input::Device::Mouse ||
        Input::CurrentInputDevice == Input::Device::Touch) {
      for (auto* choice : ChoiceItems->Children) {
        choice->Hovered = choice->Bounds.ContainsPoint(Input::CurMousePos);
        if (choice->Hovered &&
            (Input::PrevMousePos != Input::CurMousePos ||
             GetControlState(ControlType::OK)))
          FocusChoice(choice);
      }
    }
    for (auto* choice : ChoiceItems->Children) {
      choice->Update(dt);
      choice->UpdateInput(dt);
    }
  }
  if (ChoiceCount > 0) {
    PADinputButtonWentDown &= ~(PADcustom[5] | PADcustom[6] | PADcustom[10]);
    PADinputMouseWentDown &= ~(PADcustom[5] | PADcustom[6] | PADcustom[10]);
  }
}

void SysMesBox::Render() {
  if (State == Hidden) return;
  const glm::vec4 tint(1.0f, 1.0f, 1.0f, FadeAnimation.Progress);
  Renderer->DrawSprite(Box, glm::vec2(BoxX, BoxY), tint);
  for (int i = 0; i < MessageCount; ++i)
    Profile::Dialogue::DialogueFont->DrawProcessedText(
        Messages[i], tint.a, RendererOutlineMode::Full);
  for (int i = 0; i < ChoiceCount; ++i) {
    if (ChoiceItems && ChoiceItems->Children[i]->HasFocus && !Choices[i].empty()) {
      const auto pos = Choices[i].front().Position;
      Renderer->DrawSprite(
          SelectionMiddlePart,
          RectF(pos.x - 48.0f, ChoiceY + 45.0f, ChoiceWidths[i] + 96.0f,
                SelectionMiddlePart.ScaledHeight()),
          tint);
    }
    Profile::Dialogue::DialogueFont->DrawProcessedText(
        Choices[i], tint.a, RendererOutlineMode::Full);
  }
}

}  // namespace MO8
}  // namespace UI
}  // namespace Impacto
