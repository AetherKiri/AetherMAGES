#pragma once

#include "../../ui/sysmesbox.h"
#include "../../ui/widgets/button.h"

namespace Impacto {
namespace UI {
namespace MO8 {

class SysMesBox : public UI::SysMesBox {
 public:
  void Init() override;
  void Show() override;
  void Hide() override;
  void Update(float dt) override;
  void Render() override;
  void AddMessage(Vm::BufferOffsetContext ctx) override;
  void AddChoice(Vm::BufferOffsetContext ctx) override;

 private:
  std::vector<ProcessedTextGlyph> Layout(Vm::BufferOffsetContext ctx);
  void FocusChoice(Widget* choice);
};

}  // namespace MO8
}  // namespace UI
}  // namespace Impacto
