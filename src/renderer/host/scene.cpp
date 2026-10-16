#include "scene.h"
#include "../../profile/scene3d.h"
#include "host_shaders.h"
#include <cstring>
#include <stdexcept>

namespace Impacto::Host {
void Scene3D::Init() {
  Profile::Scene3D::Configure();
  Renderables = new IRenderable3D*[Profile::Scene3D::MaxRenderables]{};
  for (int i = 0; i < Profile::Scene3D::MaxRenderables; ++i)
    Renderables[i] = new Renderable3D(Owner);
  MainCamera.Init();
  IsInit = true;
}
void Scene3D::Shutdown() {
  if (!IsInit) return;
  for (int i = 0; i < Profile::Scene3D::MaxRenderables; ++i) {
    if (Renderables[i]->StaticModel)
      static_cast<Renderable3D*>(Renderables[i])->Dispose();
    delete static_cast<Renderable3D*>(Renderables[i]);
  }
  delete[] Renderables;
  Renderables = nullptr;
  IsInit = false;
}
void Scene3D::Update(float dt) {
  for (int i = 0; i < Profile::Scene3D::MaxRenderables; ++i)
    if (Renderables[i]->Status == LoadStatus::Loaded) Renderables[i]->Update(dt);
}
void Scene3D::Render() {
  MainCamera.AspectRatio = float(Owner->Width()) / Owner->Height();
  MainCamera.Recalculate();
  for (int pass = 0; pass < 2; ++pass) {
    if (pass) Owner->BeginCharacters();
    for (int i = 0; i < Profile::Scene3D::MaxRenderables; ++i) {
      auto* r = Renderables[i];
      if (r->Status == LoadStatus::Loaded && r->StaticModel &&
          r->StaticModel->Type == (pass ? ModelType_Character : ModelType_Background))
        r->Render();
    }
  }
  Owner->EndCharacters();
}
bool Renderable3D::LoadSync(uint32_t id) {
  StaticModel = Model::Load(id);
  ModelTransform = Transform();
  PrevPoseWeight = 0.0f;
  if (!StaticModel) return false;
  InitMeshAnimStatus();
  ReloadDefaultBoneTransforms();
  Animator.Character = this;
  SwitchAnimation(StaticModel->IdleAnimation, 0.0f);
  IsUsed = true;
  return true;
}
void Renderable3D::MakePlane() {
  StaticModel = Model::MakePlane();
  ModelTransform = Transform();
  PrevPoseWeight = 0.0f;
  Animator.Character = this;
  InitMeshAnimStatus();
  ReloadDefaultBoneTransforms();
  IsUsed = true;
}
void Renderable3D::InitMeshAnimStatus() {
  size_t count = 0;
  for (uint32_t i = 0; i < StaticModel->MeshCount; ++i) {
    MeshAnimStatus[i].MorphedVerticesOffset = int(count);
    if (StaticModel->Meshes[i].MorphTargetCount) count += StaticModel->Meshes[i].VertexCount;
  }
  CurrentMorphedVertices = static_cast<MorphVertexBuffer*>(std::calloc(count, sizeof(MorphVertexBuffer)));
  ReloadDefaultMeshAnimStatus();
}
void Renderable3D::ReloadDefaultMeshAnimStatus() {
  for (uint32_t i = 0; i < StaticModel->MeshCount; ++i) {
    MeshAnimStatus[i].Visible = 1.0f;
    std::fill(std::begin(MeshAnimStatus[i].MorphInfluences),
              std::end(MeshAnimStatus[i].MorphInfluences), 0.0f);
  }
}
void Renderable3D::ReloadDefaultBoneTransforms() {
  for (uint32_t i = 0; i < StaticModel->BoneCount; ++i)
    CurrentPose[i].LocalTransform = StaticModel->Bones[i].BaseTransform;
}
void Renderable3D::SwitchAnimation(int16_t id, float transitionTime) {
  if (Animator.CurrentAnimation && transitionTime > 0.0f) {
    PrevPoseWeight = 1.0f;
    for (uint32_t i = 0; i < StaticModel->BoneCount; ++i)
      PrevBoneTransforms[i] = CurrentPose[i].LocalTransform;
    std::copy(std::begin(MeshAnimStatus), std::end(MeshAnimStatus), PrevMeshAnimStatus.begin());
    AnimationTransitionTime = transitionTime;
  } else { PrevPoseWeight = 0.0f; }
  Animator.Start(id);
}
void Renderable3D::PoseBone(int16_t id) {
  const auto& bone = StaticModel->Bones[id];
  auto& pose = CurrentPose[id];
  Transform transform = PrevPoseWeight > 0.0f
      ? PrevBoneTransforms[id].Interpolate(pose.LocalTransform,
          glm::smoothstep(0.0f, 1.0f, 1.0f - PrevPoseWeight)) : pose.LocalTransform;
  pose.World = (bone.Parent < 0 ? glm::mat4(1) : CurrentPose[bone.Parent].World) * transform.Matrix();
  for (int i = 0; i < bone.ChildrenCount; ++i) PoseBone(bone.Children[i]);
  pose.Offset = pose.World * bone.BindInverse;
}
void Renderable3D::Morph(int id) {
  auto& mesh = StaticModel->Meshes[id];
  if (!mesh.MorphTargetCount) return;
  auto* out = CurrentMorphedVertices + MeshAnimStatus[id].MorphedVerticesOffset;
  auto vertex = [&](uint32_t i) -> MorphVertexBuffer {
    if (Profile::Scene3D::Version == LKMVersion::DaSH) {
      const auto& v = static_cast<VertexBufferDaSH*>(StaticModel->VertexBuffers)[mesh.VertexOffset + i];
      return {v.Position, v.Normal};
    }
    const auto& v = static_cast<VertexBuffer*>(StaticModel->VertexBuffers)[mesh.VertexOffset + i];
    return {v.Position, v.Normal};
  };
  for (uint32_t i = 0; i < mesh.VertexCount; ++i) out[i] = vertex(i);
  for (int k = 0; k < mesh.MorphTargetCount; ++k) {
    float influence = MeshAnimStatus[id].MorphInfluences[k];
    if (PrevPoseWeight > 0.0f) influence = glm::mix(PrevMeshAnimStatus[id].MorphInfluences[k],
        influence, glm::smoothstep(0.0f, 1.0f, 1.0f - PrevPoseWeight));
    if (influence == 0.0f) continue;
    auto* target = StaticModel->MorphVertexBuffers + StaticModel->MorphTargets[mesh.MorphTargetIds[k]].VertexOffset;
    for (uint32_t i = 0; i < mesh.VertexCount; ++i) {
      auto base = vertex(i);
      out[i].Position += (target[i].Position - base.Position) * influence;
      out[i].Normal += (target[i].Normal - base.Normal) * influence;
    }
  }
}
void Renderable3D::Update(float dt) {
  if (!IsUsed) return;
  if (Animator.CurrentAnimation) {
    if (!Animator.IsPlaying) SwitchAnimation(StaticModel->IdleAnimation, AnimationTransitionTime);
    else Animator.Update(dt);
  }
  for (int i = 0; i < StaticModel->RootBoneCount; ++i) PoseBone(StaticModel->RootBones[i]);
  if (PrevPoseWeight > 0.0f) PrevPoseWeight = std::max(0.0f, PrevPoseWeight - dt / AnimationTransitionTime);
  for (uint32_t i = 0; i < StaticModel->MeshCount; ++i) Morph(int(i));
}
void Renderable3D::MainThreadOnLoad() {
  if (!StaticModel) return;
  for (uint32_t i = 0; i < StaticModel->TextureCount; ++i)
    Textures[i] = StaticModel->Textures[i].Submit(Owner);
  IsSubmitted = true;
}
void Renderable3D::UnloadSync() {
  Animator.CurrentAnimation = nullptr;
  if (IsSubmitted) for (auto& id : Textures) { Owner->FreeTexture(id); id = 0; }
  delete StaticModel; StaticModel = nullptr;
  std::free(CurrentMorphedVertices); CurrentMorphedVertices = nullptr;
  ModelTransform = Transform();
  PrevPoseWeight = 0.0f;
  IsSubmitted = IsUsed = IsVisible = false;
}
void Renderable3D::Render() {
  if (!IsUsed || !IsVisible || !StaticModel) return;
  if (!IsSubmitted) MainThreadOnLoad();
  for (int pass = 0; pass < 2; ++pass) for (uint32_t i = 0; i < StaticModel->MeshCount; ++i) {
    if (bool(StaticModel->Meshes[i].Flags & MeshFlag_Later) != bool(pass) || !MeshAnimStatus[i].Visible) continue;
    DrawMesh(int(i), false);
    if (StaticModel->Type != ModelType_Background) DrawMesh(int(i), true);
  }
}
void Renderable3D::DrawMesh(int id, bool outline) {
  const auto& mesh = StaticModel->Meshes[id];
  bool bg = StaticModel->Type == ModelType_Background;
  bool dash = Profile::Scene3D::Version == LKMVersion::DaSH;
  bool eye = mesh.Material == MT_Eye || mesh.Material == MT_DaSH_Eye;
  struct Params {
    glm::mat4 ViewProjection{1}, Model{1};
    glm::vec4 Tint{}, Light{}, Eye{}, Flags{};
    std::array<glm::mat4, ModelMaxBonesPerMesh> Bones;
  } params;
  static_assert(sizeof(params) == 140 * 16);
  const auto* scene = Owner->Scene;
  params.ViewProjection = scene->MainCamera.ViewProjection;
  params.Model = ModelTransform.Matrix();
  params.Tint = scene->Tint;
  params.Light = glm::vec4(scene->LightPosition, 0);
  params.Eye = glm::vec4(scene->MainCamera.CameraTransform.Position, 0);
  params.Flags = {float(scene->DarkMode), mesh.Opacity,
      float(mesh.Material == MT_DaSH_Generic && mesh.HasShadowColorMap), 0};
  for (auto& bone : params.Bones) bone = glm::mat4(1);
  if (bg) {
    auto transform = mesh.ModelTransform;
    params.ViewProjection *= transform.Matrix();
  }
  else if (mesh.UsedBones) {
    for (uint32_t i = 0; i < std::min(mesh.UsedBones, uint32_t(ModelMaxBonesPerMesh)); ++i)
      params.Bones[i] = CurrentPose[mesh.BoneMap[i]].Offset;
  } else { params.Bones[0] = CurrentPose[mesh.MeshBone].Offset; }
  std::vector<float> vertices;
  vertices.reserve(size_t(mesh.IndexCount) * 16);
  for (int i = 0; i < mesh.IndexCount; ++i) {
    uint16_t index = StaticModel->Indices[mesh.IndexOffset + i];
    if (index >= mesh.VertexCount) throw std::out_of_range("Model vertex index");
    glm::vec3 position{}, normal{}; glm::vec2 uv{};
    glm::vec4 weights{1,0,0,0}; std::array<uint8_t,4> bones{};
    if (bg) {
      const auto& v = static_cast<BgVertexBuffer*>(StaticModel->VertexBuffers)[mesh.VertexOffset + index];
      position = v.Position; uv = v.UV;
    } else {
      auto unpack = [&](const auto& v) {
        position = v.Position; normal = v.Normal; uv = v.UV; weights = v.BoneWeights;
        std::copy(std::begin(v.BoneIndices), std::end(v.BoneIndices), bones.begin());
      };
      if (dash) unpack(static_cast<VertexBufferDaSH*>(StaticModel->VertexBuffers)[mesh.VertexOffset + index]);
      else unpack(static_cast<VertexBuffer*>(StaticModel->VertexBuffers)[mesh.VertexOffset + index]);
      if (mesh.MorphTargetCount) {
        const auto& v = CurrentMorphedVertices[MeshAnimStatus[id].MorphedVerticesOffset + index];
        position = v.Position; normal = v.Normal;
      }
    }
    vertices.insert(vertices.end(), {position.x,position.y,position.z,normal.x,normal.y,normal.z,
        uv.x,uv.y,float(bones[0]),float(bones[1]),float(bones[2]),float(bones[3]),
        weights.x,weights.y,weights.z,weights.w});
  }
  std::array<int,4> types{TT_ColorMap,-1,-1,-1};
  const char *vert, *frag;
#define SHADER(name) do { vert = dash ? Shaders::Model##name##1_vert : Shaders::Model##name##0_vert; \
                         frag = dash ? Shaders::Model##name##1_frag : Shaders::Model##name##0_frag; } while (false)
  if (bg) { SHADER(Background); }
  else if (outline) { SHADER(Outline); if (dash) types[1] = TT_DaSH_NoiseMap; }
  else if (eye) {
    SHADER(Eye);
    types = dash ? std::array<int,4>{TT_DaSH_Eye_IrisColorMap,TT_DaSH_Eye_WhiteColorMap,-1,TT_DaSH_Eye_HighlightColorMap}
                 : std::array<int,4>{TT_Eye_IrisColorMap,TT_Eye_WhiteColorMap,TT_Eye_IrisSpecularColorMap,TT_Eye_HighlightColorMap};
  } else {
    SHADER(Character);
    types = dash ? std::array<int,4>{TT_DaSH_ColorMap,TT_DaSH_ShadowColorMap,TT_DaSH_GradientMaskMap,TT_DaSH_SpecularColorMap}
                 : std::array<int,4>{TT_ColorMap,TT_GradientMaskMap,TT_SpecularColorMap,-1};
  }
#undef SHADER
  std::array<uint32_t,4> textures{};
  for (int i = 0; i < 4; ++i)
    if (types[i] >= 0 && mesh.Maps[types[i]] >= 0)
      textures[i] = Textures.at(mesh.Maps[types[i]]);
  Owner->DrawModel(vert, frag, &params, sizeof(params), vertices, textures, !bg, !outline && mesh.Opacity >= 0.9f, outline);
}
}  // namespace Impacto::Host
