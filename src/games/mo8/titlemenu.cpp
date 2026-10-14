#include "titlemenu.h"

#include "../../profile/ui/titlemenu.h"
#include "../../profile/games/mo8/titlemenu.h"
#include "../../renderer/renderer.h"
#include "../../texture/texture.h"
#include "../../spritesheet.h"
#include "../../audio/audiosystem.h"
#include "../../audio/audiostream.h"
#include "../../audio/audiochannel.h"
#include "../../io/vfs.h"
#include "../../mem.h"
#include "../../profile/scriptvars.h"
#include "../../profile/game.h"
#include "../../background2d.h"
#include "../../vm/interface/input.h"
#include "../../inputsystem.h"

namespace Impacto {
namespace UI {
namespace MO8 {

using namespace Impacto::Profile::TitleMenu;
using namespace Impacto::Profile::MO8::TitleMenu;
using namespace Impacto::Profile::ScriptVars;

using namespace Impacto::UI::Widgets;

void TitleMenu::MenuButtonOnClick(Widgets::Button* target) {
  target->Hovered = false;
  AllowsScriptInput = true;
  ScrWork[SW_TITLECUR] = target->Id;
  SetFlag(SF_TITLEEND, 1);
  Audio::PlayInGroup(Audio::ACG_SE, "sysse", 5, false, 0.0f);
}

void TitleMenu::ContinueButtonOnClick(Widgets::Button* target) {
  target->Hovered = false;
  ContinueSelected = true;
  AllowsScriptInput = false;
  Vm::Interface::PADinputButtonWentDown &= ~Vm::Interface::PADcustom[5];
  Vm::Interface::PADinputMouseWentDown &= ~Vm::Interface::PADcustom[5];
  Audio::PlayInGroup(Audio::ACG_SE, "sysse", 5, false, 0.0f);
}

void TitleMenu::GalleryButtonOnClick(Widgets::Button* target) {
  target->Hovered = false;
  GallerySelected = true;
  AllowsScriptInput = false;
  Audio::PlayInGroup(Audio::ACG_SE, "sysse", 5, false, 0.0f);
}

TitleMenu::TitleMenu() {
  MainItems = new Widgets::Group(this);
  ContinueItems = new Widgets::Group(this);
  GalleryItems = new Widgets::Group(this);
  MainItems->FocusLock = false;
  ContinueItems->FocusLock = false;
  GalleryItems->FocusLock = false;

  auto onClick = [this](auto* btn) { return MenuButtonOnClick(btn); };
  auto continueOnClick = [this](auto* btn) {
    return ContinueButtonOnClick(btn);
  };
  auto galleryOnClick = [this](auto* btn) { return GalleryButtonOnClick(btn); };

  const Sprite& focus = MenuEntriesSprites[6];  // Atlas underline, not text.

  // New Game menu button
  NewGame = new Button(0, MenuEntriesSprites[NewGameSpriteIndex],
                       MenuEntriesHSprites[NewGameSpriteIndex],
                        focus,
                       glm::vec2(MenuEntriesX, MenuEntriesFirstY));
  NewGame->OnClickHandler = onClick;
  NewGame->HighlightOffset = glm::vec2(0.0f);
  MainItems->Add(NewGame, FDIR_DOWN);
  // Continue menu button
  Continue = new Button(
      0, MenuEntriesSprites[ContinueSpriteIndex],
      MenuEntriesHSprites[ContinueSpriteIndex],
      focus,
      glm::vec2(MenuEntriesX, MenuEntriesFirstY + (1 * MenuEntriesYPadding)));
  Continue->OnClickHandler = continueOnClick;
  Continue->HighlightOffset = glm::vec2(0.0f);
  MainItems->Add(Continue, FDIR_DOWN);
  // Options menu button
  Options = new Button(
      20, MenuEntriesSprites[OptionSpriteIndex],
      MenuEntriesHSprites[OptionSpriteIndex],
      focus,
      glm::vec2(MenuEntriesX, MenuEntriesFirstY + (2 * MenuEntriesYPadding)));
  Options->OnClickHandler = onClick;
  Options->HighlightOffset = glm::vec2(0.0f);
  MainItems->Add(Options, FDIR_DOWN);
  //  Gallery menu button
  Gallery = new Button(
      0, MenuEntriesSprites[GallerySpriteIndex],
      MenuEntriesHSprites[GallerySpriteIndex],
      focus,
      glm::vec2(MenuEntriesX, MenuEntriesFirstY + (3 * MenuEntriesYPadding)));
  Gallery->OnClickHandler = galleryOnClick;
  Gallery->HighlightOffset = glm::vec2(0.0f);
  Gallery->Enabled = false;
  MainItems->Add(Gallery, FDIR_DOWN);

  // Continue buttons
  // Load button
  Load = new Button(10, MenuEntriesSprites[LoadSpriteIndex],
                    MenuEntriesHSprites[LoadSpriteIndex],
                    focus,
                    glm::vec2(MenuEntriesX, MenuEntriesFirstY));
  Load->OnClickHandler = onClick;
  Load->HighlightOffset = glm::vec2(0.0f);
  ContinueItems->Add(Load, FDIR_DOWN);
  // Quick load button
  QuickLoad = new Button(
      11, MenuEntriesSprites[QuickLoadSpriteIndex],
      MenuEntriesHSprites[QuickLoadSpriteIndex],
      focus,
      glm::vec2(MenuEntriesX, MenuEntriesFirstY + (1 * MenuEntriesYPadding)));
  QuickLoad->OnClickHandler = onClick;
  QuickLoad->HighlightOffset = glm::vec2(0.0f);
  ContinueItems->Add(QuickLoad, FDIR_DOWN);

  int index = 0;
  // Gallery buttons
  // Album button
  AlbumButton =
      new Button(30, MenuEntriesSprites[AlbumSpriteIndex],
                 MenuEntriesHSprites[AlbumSpriteIndex],
                  focus,
                 glm::vec2(MenuEntriesX, MenuEntriesGalleryFirstY +
                                             (index * MenuEntriesYPadding)));
  AlbumButton->OnClickHandler = onClick;
  AlbumButton->HighlightOffset = glm::vec2(0.0f);
  GalleryItems->Add(AlbumButton, FDIR_DOWN);
  index++;
  // Music button
  MusicButton =
      new Button(31, MenuEntriesSprites[MusicSpriteIndex],
                 MenuEntriesHSprites[MusicSpriteIndex],
                  focus,
                 glm::vec2(MenuEntriesX, MenuEntriesGalleryFirstY +
                                             (index * MenuEntriesYPadding)));
  MusicButton->OnClickHandler = onClick;
  MusicButton->HighlightOffset = glm::vec2(0.0f);
  GalleryItems->Add(MusicButton, FDIR_DOWN);
  index++;
  // Clear List button
  ClearListButton =
      new Button(32, MenuEntriesSprites[ClearListSpriteIndex],
                 MenuEntriesHSprites[ClearListSpriteIndex],
                  focus,
                 glm::vec2(MenuEntriesX, MenuEntriesGalleryFirstY +
                                             (index * MenuEntriesYPadding)));
  ClearListButton->OnClickHandler = onClick;
  ClearListButton->HighlightOffset = glm::vec2(0.0f);
  GalleryItems->Add(ClearListButton, FDIR_DOWN);
  index++;
  // Warning button
  WarningButton =
      new Button(33, MenuEntriesSprites[WarningSpriteIndex],
                 MenuEntriesHSprites[WarningSpriteIndex],
                  focus,
                 glm::vec2(MenuEntriesX, MenuEntriesGalleryFirstY +
                                             (index * MenuEntriesYPadding)));
  WarningButton->OnClickHandler = onClick;
  WarningButton->HighlightOffset = glm::vec2(0.0f);
  GalleryItems->Add(WarningButton, FDIR_DOWN);
  index++;
  if (HasAdditional) {
    // Additional button
    AdditionalButton =
        new Button(34, MenuEntriesSprites[AdditionalSpriteIndex],
                   MenuEntriesHSprites[AdditionalSpriteIndex],
                    focus,
                   glm::vec2(MenuEntriesX, MenuEntriesGalleryFirstY +
                                               (index * MenuEntriesYPadding)));
    AdditionalButton->OnClickHandler = onClick;
    AdditionalButton->HighlightOffset = glm::vec2(0.0f);
    GalleryItems->Add(AdditionalButton, FDIR_DOWN);
    index++;
  }
  // DLC button
  DLCButton =
      new Button(35 - (!HasAdditional), MenuEntriesSprites[DLCSpriteIndex],
                 MenuEntriesHSprites[DLCSpriteIndex],
                  focus,
                 glm::vec2(MenuEntriesX, MenuEntriesGalleryFirstY +
                                             (index * MenuEntriesYPadding)));
  DLCButton->OnClickHandler = onClick;
  DLCButton->HighlightOffset = glm::vec2(0.0f);
  GalleryItems->Add(DLCButton, FDIR_DOWN);
}

void TitleMenu::Show() {
  if (State == Hidden) {
    State = Shown;
    LastFocusedMenu = UI::FocusedMenu;
    if (LastFocusedMenu) {
      LastFocusedMenu->IsFocused = false;
    }
    IsFocused = true;
    UI::FocusedMenu = this;
    if (PressToStartAnimated &&
        PressToStartAnimation.State == AnimationState::Stopped) {
      PressToStartAnimation.StartIn();
    }
    // Keyboard/gamepad confirmation path: focus the first main entry so
    // ControlType::OK activates it (Button::UpdateInput needs focus; mouse
    // hover focusing is handled by Group::UpdateInput). MenuButtonOnClick
    // then raises SF_TITLEEND + SW_TITLECUR, which the startup script's
    // title loop polls (0x1012) to route into the game.
    if (CurrentlyFocusedElement == 0 && !MainItems->Children.empty()) {
      Widget* first = MainItems->Children.front();
      FocusStart[0] = first;
      CurrentlyFocusedElement = first;
      first->HasFocus = true;
    }
  }
}
void TitleMenu::Hide() {
  if (State == Shown) {
    if (ContinueItems->VisibilityState != Hidden) ContinueItems->Hide();
    if (GalleryItems->VisibilityState != Hidden) GalleryItems->Hide();
    ContinueSelected = false;
    GallerySelected = false;
    MainItemsHideAnimation.Finish(AnimationDirection::Out);
    ContinueItemsShowAnimation.Finish(AnimationDirection::Out);
    GalleryItemsShowAnimation.Finish(AnimationDirection::Out);
    if (MainItems->VisibilityState != Shown) MainItems->Show();
    FocusButton(NewGame);
    AllowsScriptInput = true;
    State = Hidden;
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

void TitleMenu::Update(float dt) {
  PrimaryFadeAnimation.Update(dt);
  MainItemsHideAnimation.Update(dt);
  ContinueItemsShowAnimation.Update(dt);
  GalleryItemsShowAnimation.Update(dt);
  if (PressToStartAnimated) {
    PressToStartAnimation.Update(dt);
  }
  if (GetFlag(SF_TITLEMODE)) {
    Show();
  } else {
    Hide();
  }

  if (State != Hidden && IsFocused && GetFlag(SF_TITLEMODE) &&
      ScrWork[SW_TITLEMODE] != 1) {
    Gallery->Tint = glm::vec4(1.0f, 1.0f, 1.0f, GetFlag(803) ? 1.0f : 0.0f);
    Gallery->Enabled = GetFlag(803);
    MainItems->Update(dt);
    ContinueItems->Update(dt);
    GalleryItems->Update(dt);
    // Exactly one group owns input; a click that opens Continue must not
    // activate its newly visible Load button in the same frame.
    if (ScrWork[SW_TITLEMODE] == 5 && !GetFlag(SF_TITLEEND)) {
      if (ContinueSelected) {
        if (ContinueItemsShowAnimation.IsIn()) {
          UpdateInput(dt);
          UpdateGroupInput(ContinueItems, dt);
        }
      } else if (GallerySelected) {
        if (GalleryItemsShowAnimation.IsIn()) {
          UpdateInput(dt);
          UpdateGroupInput(GalleryItems, dt);
        }
      } else if (MainItems->VisibilityState == Shown &&
                 MainItemsHideAnimation.IsOut()) {
        UpdateInput(dt);
        UpdateGroupInput(MainItems, dt);
      }
    }

    switch (ScrWork[SW_TITLEMODE]) {
      case 0:
      case 2: {
        if (PrimaryFadeAnimation.State == AnimationState::Stopped &&
            ScrWork[SW_TITLEDISPCT] == 0)
          PrimaryFadeAnimation.StartOut(true);
      } break;
      case 5: {
        if (ContinueSelected) {
          UpdateSubMenu(&ContinueItemsShowAnimation, ContinueItems,
                        &ContinueSelected);
        } else if (GallerySelected) {
          UpdateSubMenu(&GalleryItemsShowAnimation, GalleryItems,
                        &GallerySelected);
        } else {
          if (MainItems->VisibilityState == Hidden) MainItems->Show();
        }
      } break;
      case 6: {
        if (PrimaryFadeAnimation.State == AnimationState::Stopped &&
            ScrWork[SW_TITLEDISPCT] == 0)
          PrimaryFadeAnimation.StartIn(true);
      } break;
      case 11: {
        if (PrimaryFadeAnimation.IsIn()) {
          PrimaryFadeAnimation.StartOut(true);
        }
      } break;
    }
  }
}

void TitleMenu::Render() {
  if (State != Hidden && GetFlag(SF_TITLEMODE)) {
    Renderer->DrawSprite(BackgroundSprite, glm::vec2(0.0f));
    Renderer->DrawSprite(LogoSprite, glm::vec2(LogoPositionX, LogoPositionY));
    switch (ScrWork[SW_TITLEMODE]) {
      case 0:
      case 1: {  // Press to start
        glm::vec4 col = glm::vec4(1.0f);
        if (PressToStartAnimated) {
          col.a = glm::smoothstep(0.0f, 1.0f, PressToStartAnimation.Progress);
        }
        Renderer->DrawSprite(PressToStartSprite, PressToStartPos, col);
        glm::vec4 black = glm::vec4(0.0f);
        black.a = glm::smoothstep(0.0f, 1.0f, PrimaryFadeAnimation.Progress);
        Renderer->DrawQuad(RectF(0.0f, 0.0f, Profile::Game::DesignWidth,
                                 Profile::Game::DesignHeight),
                           black);
      } break;
      case 2: {  // Press to start fade
        glm::vec4 col = glm::vec4(1.0f);
        col.a = glm::smoothstep(
            0.0f, 1.0f,
            ScrWork[SW_TITLEDISPCT] > 0 ? PrimaryFadeAnimation.Progress : 1.0f);
        Renderer->DrawSprite(PressToStartSprite, PressToStartPos, col);
      } break;
      case 5:
      case 6:
      case 7:
      case 11: {  // Main and main fade out
        MainItems->Tint = glm::vec4(
            1.0f, 1.0f, 1.0f,
            glm::smoothstep(1.0f, 0.0f, MainItemsHideAnimation.Progress));
        MainItems->Render();
        ContinueItems->Tint = glm::vec4(
            1.0f, 1.0f, 1.0f,
            glm::smoothstep(0.0f, 1.0f, ContinueItemsShowAnimation.Progress));
        ContinueItems->Render();
        GalleryItems->Tint = glm::vec4(
            1.0f, 1.0f, 1.0f,
            glm::smoothstep(0.0f, 1.0f, GalleryItemsShowAnimation.Progress));
        GalleryItems->Render();
        glm::vec4 black = glm::vec4(0.0f);
        black.a = glm::smoothstep(0.0f, 1.0f, PrimaryFadeAnimation.Progress);
        Renderer->DrawQuad(RectF(0.0f, 0.0f, Profile::Game::DesignWidth,
                                 Profile::Game::DesignHeight),
                           black);
      } break;
    }
  }
}

void TitleMenu::UpdateSubMenu(Animation* showAnimation,
                              Widgets::Group* subMenuGroup,
                              bool* subMenuSelected) {
  if (MainItemsHideAnimation.IsOut()) {
    MainItemsHideAnimation.StartIn();
  } else if (MainItemsHideAnimation.IsIn() && showAnimation->IsOut()) {
    if (subMenuGroup->VisibilityState != Hidden) {
      subMenuGroup->Hide();
      MainItems->Show();
      MainItemsHideAnimation.StartOut();
      *subMenuSelected = false;
      AllowsScriptInput = true;
      FocusButton(subMenuGroup == ContinueItems ? Continue : Gallery);
    } else {
      MainItems->Hide();
      subMenuGroup->Show();
      showAnimation->StartIn();
      if (!subMenuGroup->Children.empty())
        FocusButton(subMenuGroup->Children.front());
    }
  } else if (showAnimation->IsIn() &&
             Vm::Interface::GetControlState(Vm::Interface::ControlType::Back)) {
    Audio::PlayInGroup(Audio::ACG_SE, "sysse", 3, false, 0.0f);
    showAnimation->StartOut();
  }
  Vm::Interface::PADinputButtonWentDown &= ~Vm::Interface::PADcustom[6];
  Vm::Interface::PADinputMouseWentDown &= ~Vm::Interface::PADcustom[6];
  Vm::Interface::PADinputButtonWentDown &= ~Vm::Interface::PADcustom[10];
  Vm::Interface::PADinputMouseWentDown &= ~Vm::Interface::PADcustom[10];
}

void TitleMenu::FocusButton(Widget* target) {
  if (CurrentlyFocusedElement) CurrentlyFocusedElement->HasFocus = false;
  CurrentlyFocusedElement = target;
  target->HasFocus = true;
}

void TitleMenu::UpdateGroupInput(Group* group, float dt) {
  if (Input::CurrentInputDevice == Input::Device::Mouse ||
      Input::CurrentInputDevice == Input::Device::Touch) {
    for (auto* entry : group->Children) {
      entry->Hovered = entry->Bounds.ContainsPoint(Input::CurMousePos);
      if (entry->Hovered && entry->Enabled) FocusButton(entry);
    }
  }
  group->UpdateInput(dt);
}

}  // namespace MO8
}  // namespace UI
}  // namespace Impacto
