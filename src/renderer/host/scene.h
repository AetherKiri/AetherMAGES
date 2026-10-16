#pragma once
#include "../3d/scene.h"
#include "renderer.h"

namespace Impacto::Host {
class Scene3D final : public IScene3D {
 public:
  explicit Scene3D(Host::Renderer* renderer) : Owner(renderer) {}
  ~Scene3D() override { Shutdown(); }
  void Init() override;
  void Shutdown() override;
  void Update(float dt) override;
  void Render() override;
 private:
  Host::Renderer* Owner;
  bool IsInit = false;
};

class Renderable3D final : public IRenderable3D {
 public:
  explicit Renderable3D(Host::Renderer* renderer) : Owner(renderer) {}
  void MakePlane() override;
  void Update(float dt) override;
  void Render() override;
  void ReloadDefaultBoneTransforms() override;
  void InitMeshAnimStatus() override;
  void ReloadDefaultMeshAnimStatus() override;
  void SwitchAnimation(int16_t id, float transitionTime) override;
  void Dispose() { UnloadSync(); }
 protected:
  bool LoadSync(uint32_t id) override;
  void UnloadSync() override;
  void MainThreadOnLoad() override;
 private:
  Host::Renderer* Owner;
  std::array<uint32_t, ModelMaxTexturesPerModel> Textures{};
  std::array<Transform, ModelMaxBonesPerModel> PrevBoneTransforms{};
  std::array<AnimatedMesh, ModelMaxMeshesPerModel> PrevMeshAnimStatus{};
  float PrevPoseWeight = 0.0f;
  float AnimationTransitionTime = 0.0f;
  void PoseBone(int16_t id);
  void Morph(int id);
  void DrawMesh(int id, bool outline);
};
}  // namespace Impacto::Host
