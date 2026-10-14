#pragma once

#include <optional>
#include "../../ui/menu.h"
#include "../../ui/widgets/group.h"
#include "../../ui/widgets/button.h"

namespace Impacto {
namespace UI {
namespace MO8 {

class SystemMenu : public Menu {
 public:
  SystemMenu();

  void Show() override;
  void Hide() override;
  void Update(float dt) override;
  void Render() override;

  void MenuButtonOnClick(Widgets::Button* target);

 private:
  Widgets::Group* MainItems;
  Animation FadeAnimation;
  int LastMenuCount = 0;
  bool ConsumeHudMenuPress = false;
  std::optional<int> LastFocusedButtonId;
};

}  // namespace MO8
}  // namespace UI
}  // namespace Impacto
