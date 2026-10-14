#include "systemmenu.h"

#include "../../profile/ui/systemmenu.h"
#include "../../profile/games/mo8/systemmenu.h"
#include "../../renderer/renderer.h"
#include "../../renderer/window.h"
#include "../../mem.h"
#include "../../profile/scriptvars.h"
#include "../../inputsystem.h"
#include "../../ui/widgets/button.h"
#include "../../audio/audiosystem.h"
#include "../../profile/dialogue.h"
#include "../../text/dialoguepage.h"
#include "../../vm/interface/input.h"
#include "../../ui/sysmesbox.h"

namespace Impacto {
namespace UI {
namespace MO8 {

using namespace Impacto::Profile::SystemMenu;
using namespace Impacto::Profile::MO8::SystemMenu;
using namespace Impacto::Profile::ScriptVars;

using namespace Impacto::UI::Widgets;
using namespace Impacto::Vm::Interface;

void SystemMenu::MenuButtonOnClick(Widgets::Button* target) {
  target->Hovered = false;
  ScrWork[SW_SYSMENUCNO] = target->Id;
  ChoiceMade = true;
  // When exiting through a menu button remove focus from the menu to avoid
  // triggering the onClick handler again
  if (target->Id == ExitMenuButtonId)
    IsFocused = false;
  else
    Audio::PlayInGroup(Audio::ACG_SE, "sysse", 5, false, 0.0f);
}

SystemMenu::SystemMenu() {
  MainItems = new Widgets::Group(this);

  auto onClick = [this](auto* btn) { return MenuButtonOnClick(btn); };

  Sprite nullSprite = Sprite();
  nullSprite.Bounds = RectF(0.0f, 0.0f, 0.0f, 0.0f);

  for (int i = 0; i < MenuEntriesNum; i++) {
    Button* menuButton =
        new Button(i, MenuEntriesSprites[i], MenuEntriesHSprites[i], nullSprite,
                   glm::vec2(*MenuEntriesX,
                             *MenuEntriesFirstY + (*MenuEntriesYPadding * i)));
    menuButton->LockedSprite = MenuEntriesLSprites[i];

    menuButton->OnClickHandler = onClick;
    MainItems->Add(menuButton, FDIR_DOWN);
  }

  FadeAnimation.Direction = AnimationDirection::In;
  FadeAnimation.LoopMode = AnimationLoopMode::Stop;
  FadeAnimation.DurationIn = FadeInDuration;
  FadeAnimation.DurationOut = FadeOutDuration;
}

void SystemMenu::Show() {
  if (State == Hidden || State == Hiding) {
    State = Showing;
    FadeAnimation.StartIn();
    MainItems->Show();
    LastFocusedMenu = UI::FocusedMenu;
    if (LastFocusedMenu) {
      LastFocusedMenu->IsFocused = false;
    }
    IsFocused = true;
    UI::FocusedMenu = this;
    ChoiceMade = false;
    const int id = LastFocusedButtonId.value_or(0);
    CurrentlyFocusedElement = MainItems->Children[
        id >= 0 && id < MenuEntriesNum ? id : 0];
    CurrentlyFocusedElement->HasFocus = true;
  }
}

void SystemMenu::Hide() {
  if (State != Hidden) {
    if (CurrentlyFocusedElement) {
      auto* btn = static_cast<Widgets::Button*>(CurrentlyFocusedElement);
      if (btn) {
        LastFocusedButtonId = btn->Id;
      }
    }

    State = Hiding;
    FadeAnimation.StartOut();
    MainItems->Hide();
    if (UI::FocusedMenu == this) {
      while (LastFocusedMenu &&
             (LastFocusedMenu->State == Hidden ||
              LastFocusedMenu->State == Hiding))
        LastFocusedMenu = LastFocusedMenu->LastFocusedMenu;
      UI::FocusedMenu = LastFocusedMenu;
      if (LastFocusedMenu) LastFocusedMenu->IsFocused = true;
    }
    IsFocused = false;
  }
}

void SystemMenu::Update(float dt) {
  const uint32_t advance = PADcustom[5] | PADcustom[23];
  if (ConsumeHudMenuPress) {
    ConsumeHudMenuPress = (PADinputMouseIsDown & advance) != 0;
    PADinputMouseWentDown &= ~advance;
    PADinputMouseIsDown &= ~advance;
  }
  // Menu updates precede VM execution. The plain dialogue box has no input
  // caller, so route its Menu icon here while gameplay is unobstructed.
  if (State == Hidden && !UI::FocusedMenu && Profile::Dialogue::HasMenuButton &&
      (ScrWork[SW_GAMESTATE] & 5) == 1 && !GetFlag(SF_GAMEPAUSE) &&
      !GetFlag(SF_SYSTEMMENUDISABLE) && !GetFlag(SF_SYSMENUDISABLE) &&
      !GetFlag(SF_SYSTEMMENUDISABLE2) && !GetFlag(SF_MOVIEPLAY) &&
      !GetFlag(SF_UIHIDDEN) && ScrWork[SW_SYSMENUCT] == 0 &&
      ScrWork[SW_SYSSUBMENUCT] == 0 &&
      (Input::CurrentInputDevice == Input::Device::Mouse ||
       Input::CurrentInputDevice == Input::Device::Touch)) {
    const RectF bounds(Profile::Dialogue::MenuButtonPosition.x,
                       Profile::Dialogue::MenuButtonPosition.y,
                       Profile::Dialogue::MenuButtonSprite.ScaledWidth(),
                       Profile::Dialogue::MenuButtonSprite.ScaledHeight());
    bool dialogueVisible = false;
    for (const auto& page : DialoguePages) {
      dialogueVisible |= page.GetMode() == DPM_ADV &&
                           GetFlag(SF_MESWINDOW0OPENFL + page.Id) &&
                           !page.FadeAnimation.IsOut() &&
                          !(ScrWork[SW_MESWIN0TYPE +
                                    Profile::Vm::ScrWorkMesStructSize * page.Id] &
                            +MesWinTypeBit::DontDrawWindow) &&
                          page.GetState() != DialoguePage::State::Hidden;
    }
    bool dialogVisible = false;
    for (const auto& item : UI::SysMesBox::MsgBoxes)
      dialogVisible |= item.second->State != Hidden;
    if (dialogueVisible && !dialogVisible &&
        bounds.ContainsPoint(Input::CurMousePos)) {
      RequestCursor(CursorType::Pointer);
      if (GetControlState(ControlType::OK) &&
          bounds.ContainsPoint(Input::InitMousePos)) {
        ConsumeHudMenuPress = (PADinputMouseIsDown & advance) != 0;
        PADinputButtonWentDown &= ~advance;
        PADinputMouseWentDown &= ~advance;
        PADinputButtonIsDown &= ~advance;
        PADinputMouseIsDown &= ~advance;
        PADinputButtonWentDown |= PADcustom[10];
      }
    }
  }

  FadeAnimation.Update(dt);
  if (ScrWork[SW_SYSMENUCT] > 0 && ScrWork[SW_SYSMENUCT] <= 32 &&
      ScrWork[SW_SYSMENUCT] > LastMenuCount &&
      ScrWork[SW_SYSMENUALPHA] > 0 && State == Hidden)
    Show();
  if ((ScrWork[SW_SYSMENUCT] == 0 ||
       ScrWork[SW_SYSMENUCT] < LastMenuCount) &&
      (State == Showing || State == Shown)) {
    Hide();
  }

  if (State == Showing && ScrWork[SW_SYSMENUCT] == 32 && FadeAnimation.IsIn())
    State = Shown;
  else if (State == Hiding && FadeAnimation.IsOut())
    State = Hidden;
  LastMenuCount = ScrWork[SW_SYSMENUCT];

  if (State == Shown && IsFocused && ScrWork[SW_SYSMENUALPHA] > 0 &&
      !ChoiceMade) {
    UpdateInput(dt);
    MainItems->Update(dt);
    if (Input::CurrentInputDevice == Input::Device::Mouse ||
        Input::CurrentInputDevice == Input::Device::Touch) {
      for (auto* entry : MainItems->Children) {
        entry->Hovered = entry->Bounds.ContainsPoint(Input::CurMousePos);
        if (entry->Hovered && entry->Enabled) {
          if (CurrentlyFocusedElement) CurrentlyFocusedElement->HasFocus = false;
          CurrentlyFocusedElement = entry;
          entry->HasFocus = true;
        }
      }
    }
    MainItems->UpdateInput(dt);
    PADinputButtonWentDown &= ~PADcustom[5];
    PADinputMouseWentDown &= ~PADcustom[5];
  }
}

void SystemMenu::Render() {
  if (State != Hidden && ScrWork[SW_SYSMENUALPHA] > 0) {
    glm::vec4 col(1.0f, 1.0f, 1.0f, FadeAnimation.Progress);
    Renderer->DrawSprite(SystemMenuBackgroundSprite,
                         glm::vec2(SystemMenuX, SystemMenuY), col);
    MainItems->Tint = col;
    MainItems->Render();
  }
}

}  // namespace MO8
}  // namespace UI
}  // namespace Impacto
