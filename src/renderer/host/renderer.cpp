#include "renderer.h"
#include "../../profile/game.h"
#include "../../texture/texture.h"
#include "scene.h"
#include "../../host/hostbridge.h"
#include "host_shaders.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Impacto::Host {
namespace {
void Require(int32_t ok, const char* operation) {
  if (!ok) throw std::runtime_error(std::string("Host GPU: ") + operation);
}
std::array<glm::vec4, 4> Tints(glm::vec4 tint) { return {tint, tint, tint, tint}; }
glm::vec4 Alpha(int alpha, int range) {
  range = std::max(range, 1);
  alpha = std::clamp(alpha, 0, range + 256);
  float scale = 256.0f / range;
  return {scale, (1.0f - alpha / 255.0f) * scale, 0.0f, 0.0f};
}
}

Renderer::Renderer() {
  if (!Gpu.create_rgba || !Gpu.draw || !Gpu.copy_rgba || !Gpu.clear_rgba ||
      !Gpu.update_rgba || !Gpu.release_texture)
    throw std::runtime_error("Host GPU callbacks are not registered");
  RectF viewport = Window->GetViewport();
  TargetWidth = static_cast<uint32_t>(viewport.Width);
  TargetHeight = static_cast<uint32_t>(viewport.Height);
  try {
    Framebuffers[0] = CreateTarget(TargetWidth, TargetHeight);
    Stencils[0] = CreateTarget(TargetWidth, TargetHeight);
    Published = CreateTarget(TargetWidth, TargetHeight);
    Characters = CreateTarget(TargetWidth, TargetHeight);
    Projection = glm::ortho(0.0f, float(TargetWidth), float(TargetHeight), 0.0f);
    uint8_t white[4] = {255, 255, 255, 255};
    SpriteSheet sheet(1.0f, 1.0f);
    sheet.Texture = SubmitTexture(TexFmt_RGBA, white, 1, 1);
    RectSprite = Sprite(sheet, 0, 0, 1, 1);
  } catch (...) {
    // A throwing constructor has no destructor; release targets allocated
    // before a later GPU allocation failed.
    Shutdown();
    throw;
  }
}
Renderer::~Renderer() { Shutdown(); }
void Renderer::Init() {
  Projection = glm::ortho(0.0f, Profile::Game::DesignWidth,
                          Profile::Game::DesignHeight, 0.0f,
                          -Profile::Game::DesignWidth, Profile::Game::DesignWidth);
  if (+Profile::Game::GameFeatures & +GameFeature::Scene3D) {
    Scene = new Scene3D(this);
    Scene->Init();
  }
  IsInit = true;
}
void Renderer::Shutdown() {
  if (Scene) { Scene->Shutdown(); delete Scene; Scene = nullptr; }
  for (const auto& [id, texture] : Textures) Gpu.release_texture(texture.Handle);
  Textures.clear();
  SheetPathToId.clear(); LookupTextureIdToTexture.clear(); SurfToId.clear();
  IsInit = false;
}
void Renderer::BeginFrame() {
  ActiveFramebuffer = 0;
  ClearDepth = true;
  StencilMode = StencilBufferMode::Off;
  ScissorEnabled = false;
  Blend = RendererBlendMode::Normal;
  Clear(glm::vec4(0,0,0,1));
}
void Renderer::BeginFrame2D() {}
void Renderer::EndFrame() {}
void Renderer::Present() {
  Require(Gpu.copy_rgba(TextureHandle(Published), TextureHandle(Framebuffers[0]),
                       TargetWidth, TargetHeight), "publish frame");
  HostBridge::Get().PublishGpuFrame(TextureHandle(Published), TargetWidth, TargetHeight);
}
uint64_t Renderer::FrameTexture() const { return TextureHandle(Published); }
uint64_t Renderer::TextureHandle(uint32_t id) const {
  auto it = Textures.find(id);
  return it == Textures.end() ? 0 : it->second.Handle;
}
uint32_t Renderer::CreateTarget(uint32_t width, uint32_t height) {
  return SubmitTexture(TexFmt_RGBA, nullptr, int(width), int(height));
}
uint32_t Renderer::SubmitTexture(TexFmt format, uint8_t* pixels, int width, int height) {
  if (width <= 0 || height <= 0) throw std::invalid_argument("Invalid texture size");
  int channels = format == TexFmt_RGBA ? 4 : format == TexFmt_RGB ? 3 : format == TexFmt_U8 ? 1 : 0;
  if (!channels) throw std::invalid_argument("Unsupported host texture format");
  std::vector<uint8_t> rgba;
  if (pixels && channels != 4) {
    rgba.resize(size_t(width) * height * 4);
    for (size_t i = 0; i < size_t(width) * height; ++i) {
      rgba[4*i] = pixels[channels*i];
      rgba[4*i+1] = channels == 3 ? pixels[channels*i+1] : 0;
      rgba[4*i+2] = channels == 3 ? pixels[channels*i+2] : 0;
      rgba[4*i+3] = 255;
    }
    pixels = rgba.data();
  }
  uint64_t handle = Gpu.create_rgba(width, height, pixels, width * 4);
  if (!handle) throw std::runtime_error("Host GPU texture allocation failed");
  uint32_t id = NextTexture++;
  Textures.emplace(id, TextureRecord{handle, uint32_t(width), uint32_t(height)});
  return id;
}
void Renderer::FreeTexture(uint32_t id) {
  auto it = Textures.find(id);
  if (it == Textures.end()) return;
  Gpu.release_texture(it->second.Handle); Textures.erase(it);
}
uint32_t Renderer::Unwrap(const SpriteSheet& sheet) const {
  if (!sheet.ScriptHandled) return sheet.Texture;
  auto it = LookupTextureIdToTexture.find(sheet.Texture);
  return it == LookupTextureIdToTexture.end() ? 0 : it->second;
}
uint32_t Renderer::MapSpriteSheet(const SpriteSheet& sheet) {
  auto id = uint32_t(SheetPathToId.size());
  return SheetPathToId.emplace(sheet.Path, id).first->second;
}
bool Renderer::LoadSurf(int surf, int archive, int file) {
  Io::AssetPathKey key{.MountId = uint32_t(archive), .Id = uint32_t(file)};
  auto it = SheetPathToId.find(key);
  if (it == SheetPathToId.end()) return false;
  Io::Stream* stream = nullptr;
  if (Io::AssetPathKey::KeyToAssetPath(key).Open(&stream) != IoError_OK) return false;
  Texture tex{};
  try { tex.Load(stream); } catch (...) { delete stream; throw; }
  delete stream;
  auto old = LookupTextureIdToTexture.find(it->second);
  if (old != LookupTextureIdToTexture.end()) FreeTexture(old->second);
  LookupTextureIdToTexture[it->second] = tex.Submit(this);
  SurfToId[surf] = key;
  return true;
}
void Renderer::UnloadSurf(int surf) {
  auto it = SurfToId.find(surf);
  if (it == SurfToId.end()) return;
  auto sheet = SheetPathToId.find(it->second);
  if (sheet != SheetPathToId.end()) {
    auto texture = LookupTextureIdToTexture.find(sheet->second);
    if (texture != LookupTextureIdToTexture.end()) {
      FreeTexture(texture->second); LookupTextureIdToTexture.erase(texture);
    }
  }
  SurfToId.erase(it);
}
int Renderer::GetSpriteSheetImage(const SpriteSheet& sheet, std::span<uint8_t> output) {
  auto it = Textures.find(Unwrap(sheet));
  if (it == Textures.end() || !Gpu.read_rgba) return 0;
  size_t bytes = size_t(it->second.Width) * it->second.Height * 4;
  if (output.size() < bytes) return 0;
  Require(Gpu.read_rgba(it->second.Handle, output.data(), output.size(),
                       it->second.Width * 4), "read screenshot");
  return int(bytes);
}
Renderer::Parameters Renderer::Params(glm::mat4 transform, glm::mat4 mask) const {
  Parameters p;
  p.Projection = Projection; p.Transformation = transform; p.MaskTransformation = mask;
  return p;
}
void Renderer::Draw(const char* vertex, const char* fragment, Parameters p,
                    std::array<uint32_t, 3> textures,
                    std::span<const VertexBufferSprites> vertices,
                    std::span<const uint16_t> indices, TopologyMode topology,
                    bool repeat, bool replace) {
  if (indices.empty()) return;
  std::vector<float> data;
  data.reserve(indices.size() * 10 * (topology == TopologyMode::Triangles ? 1 : 3));
  auto append = [&](uint16_t i) {
    if (i >= vertices.size()) throw std::out_of_range("Host GPU vertex index");
    const auto& v = vertices[i];
    data.insert(data.end(), {v.Position.x,v.Position.y,v.UV.x,v.UV.y,
        v.Tint.r,v.Tint.g,v.Tint.b,v.Tint.a,v.MaskUV.x,v.MaskUV.y});
  };
  if (topology == TopologyMode::TriangleStrips) {
    for (size_t i = 2; i < indices.size(); ++i) {
      append(indices[i-2 + (i & 1)]); append(indices[i-1 - (i & 1)]); append(indices[i]);
    }
  } else { for (auto i : indices) append(i); }
  p.Stencil.x = StencilMode == StencilBufferMode::Test ? 1.0f : 0.0f;
  p.Stencil.y = StencilMode == StencilBufferMode::Write ? 1.0f : 0.0f;
  impacto_host_gpu_draw_t request{};
  request.struct_size = sizeof(request);
  request.target = TextureHandle(StencilMode == StencilBufferMode::Write
      ? Stencils[ActiveFramebuffer] : Framebuffers[ActiveFramebuffer]);
  for (size_t i = 0; i < 3; ++i) {
    request.textures[i] = TextureHandle(textures[i]);
    if (!request.textures[i]) request.textures[i] = TextureHandle(RectSprite.Sheet.Texture);
  }
  request.textures[3] = TextureHandle(StencilMode == StencilBufferMode::Test
      ? Stencils[ActiveFramebuffer] : RectSprite.Sheet.Texture);
  request.vertex_source = vertex; request.fragment_source = fragment;
  request.vertices = data.data(); request.vertex_float_count = uint32_t(data.size());
  request.vertex_count = uint32_t(data.size() / 10);
  request.uniforms = &p; request.uniform_bytes = sizeof(p);
  request.clip[2] = int(TargetWidth); request.clip[3] = int(TargetHeight);
  if (ScissorEnabled) {
    float dw = Profile::Game::HasInit ? Profile::Game::DesignWidth : float(TargetWidth);
    float dh = Profile::Game::HasInit ? Profile::Game::DesignHeight : float(TargetHeight);
    request.clip[0] = std::clamp(int(ScissorRect.X * TargetWidth / dw), 0, int(TargetWidth));
    request.clip[1] = std::clamp(int(ScissorRect.Y * TargetHeight / dh), 0, int(TargetHeight));
    request.clip[2] = std::clamp(int((ScissorRect.X+ScissorRect.Width)*TargetWidth/dw), 0, int(TargetWidth));
    request.clip[3] = std::clamp(int((ScissorRect.Y+ScissorRect.Height)*TargetHeight/dh), 0, int(TargetHeight));
  }
  if (request.clip[2] <= request.clip[0] || request.clip[3] <= request.clip[1]) return;
  request.blend = replace || StencilMode == StencilBufferMode::Write ? 3 : uint32_t(Blend);
  request.repeat = repeat;
  Require(Gpu.draw(&request), "draw submission failed");
}
void Renderer::Quad(const char* vertex, const char* fragment, Parameters p,
                    std::array<uint32_t, 3> textures, CornersQuad dest,
                    CornersQuad uv, std::span<const glm::vec4, 4> tints,
                    CornersQuad mask, bool repeat, bool replace) {
  std::array<VertexBufferSprites, 4> v{{
      {dest.BottomLeft, uv.BottomLeft, tints[0], mask.BottomLeft},
      {dest.TopLeft, uv.TopLeft, tints[1], mask.TopLeft},
      {dest.TopRight, uv.TopRight, tints[2], mask.TopRight},
      {dest.BottomRight, uv.BottomRight, tints[3], mask.BottomRight}}};
  const std::array<uint16_t, 6> indices{0,1,2,0,2,3};
  Draw(vertex, fragment, p, textures, v, indices, TopologyMode::Triangles, repeat, replace);
}
void Renderer::DrawSprite(const Sprite& s, const CornersQuad& d, glm::mat4 t,
    std::span<const glm::vec4, 4> tint, glm::vec3 shift, bool inverted, bool replace, bool repeat) {
  auto p = Params(t); p.ColorShift = glm::vec4(shift, 0);
  Quad(inverted ? Shaders::SpriteInverted_vert : Shaders::Sprite_vert,
       inverted ? Shaders::SpriteInverted_frag : Shaders::Sprite_frag,
       p, {Unwrap(s.Sheet)}, d, s.NormalizedBounds(), tint, RectF(), repeat, replace);
}
void Renderer::DrawMaskedSprite(const Sprite& s, const Sprite& m, const CornersQuad& d,
    const CornersQuad&, int alpha, int range, glm::mat4 t, glm::mat4 mt,
    std::span<const glm::vec4, 4> tint, bool inverted, bool same) {
  auto p = Params(t, mt); p.Alpha = Alpha(alpha, range == 0 ? 16 : range);
  p.Flags = {float(inverted),float(same),0,0};
  Quad(Shaders::MaskedSprite_vert, Shaders::MaskedSprite_frag, p,
       {Unwrap(s.Sheet),Unwrap(m.Sheet)}, d, s.NormalizedBounds(), tint, RectF(0,0,1,1));
}
void Renderer::DrawMaskedBinarySprite(const Sprite& s, const Sprite& m, const CornersQuad& d,
    const CornersQuad& md, glm::mat4 t, std::optional<glm::mat4> mt,
    std::span<const glm::vec4, 4> tint, bool inverted) {
  auto p = Params(t, mt.value_or(glm::mat4(1)));
  p.Flags = {float(inverted),0,float(!mt.has_value()),0};
  Quad(Shaders::MaskedSpriteBinary_vert, Shaders::MaskedSpriteBinary_frag, p,
       {Unwrap(s.Sheet),Unwrap(m.Sheet)}, d, s.NormalizedBounds(), tint,
       CornersQuad(md).Scale({1/m.Bounds.Width,1/m.Bounds.Height},{0,0}));
}
void Renderer::DrawMaskedSpriteOverlay(const Sprite& s, const Sprite& m, const CornersQuad& d,
    const CornersQuad& md, int alpha, int range, glm::mat4 t, glm::mat4 mt,
    std::span<const glm::vec4, 4> tint, bool inverted, bool maskAlpha) {
  auto p = Params(t, mt); p.Alpha = Alpha(alpha, range); p.Flags.x = float(inverted);
  Quad(maskAlpha ? Shaders::MaskedSprite_vert : Shaders::MaskedSpriteNoAlpha_vert,
       maskAlpha ? Shaders::MaskedSprite_frag : Shaders::MaskedSpriteNoAlpha_frag, p,
       {Unwrap(s.Sheet),Unwrap(m.Sheet)}, d, s.NormalizedBounds(), tint,
       CornersQuad(md).Scale({1/m.Sheet.DesignWidth,1/m.Sheet.DesignHeight},{0,0}));
}
void Renderer::DrawPrimitives(const SpriteSheet& s, const SpriteSheet* m, ShaderProgramType type,
    std::span<const VertexBufferSprites> vertices, std::span<const uint16_t> indices,
    glm::mat4 t, glm::mat4 mt, bool inverted, TopologyMode topology, bool repeat) {
  auto p = Params(t, mt); p.Flags.x = float(inverted);
  const char *vert = nullptr, *frag = nullptr;
#define SELECT_SHADER(name) case ShaderProgramType::name: vert = Shaders::name##_vert; frag = Shaders::name##_frag; break
  switch (type) {
    SELECT_SHADER(AdditiveMaskedSprite); SELECT_SHADER(CCMessageBoxSprite);
    SELECT_SHADER(CHLCCMenuBackground); SELECT_SHADER(ColorBurnMaskedSprite);
    SELECT_SHADER(ColorDodgeMaskedSprite); SELECT_SHADER(ColorMaskedSprite);
    SELECT_SHADER(HardLightMaskedSprite); SELECT_SHADER(LinearBurnMaskedSprite);
    SELECT_SHADER(MaskedSprite); SELECT_SHADER(MaskedSpriteBinary);
    SELECT_SHADER(MaskedSpriteNoAlpha); SELECT_SHADER(OverlayMaskedSprite);
    SELECT_SHADER(ScreenMaskedSprite); SELECT_SHADER(SoftLightMaskedSprite);
    SELECT_SHADER(Sprite); SELECT_SHADER(SpriteInverted); SELECT_SHADER(YUVFrame);
    SELECT_SHADER(GaussianBlur); SELECT_SHADER(Mosaic);
  }
#undef SELECT_SHADER
  if (!vert) throw std::invalid_argument("Unsupported MAGES shader");
  Draw(vert, frag, p, {Unwrap(s),m ? Unwrap(*m) : 0}, vertices, indices, topology, repeat);
}
void Renderer::DrawCCMessageBox(const Sprite& s, const Sprite& m, const RectF& d,
    glm::vec4 tint, int alpha, int range, float effect) {
  auto p = Params(); p.Alpha = Alpha(alpha, range); p.Alpha.z = effect;
  Quad(Shaders::CCMessageBoxSprite_vert, Shaders::CCMessageBoxSprite_frag, p,
       {Unwrap(s.Sheet),Unwrap(m.Sheet)}, d, s.NormalizedBounds(), Tints(tint), m.NormalizedBounds());
}
void Renderer::DrawEdgeDetectedSingleSheetFont(const SpriteSheet& s, const SpriteSheet* m,
    std::span<const VertexBufferSprites> vertices, std::span<const uint16_t> indices,
    float difference, float intensity, float alpha, glm::vec2 scale, glm::mat4 t, glm::mat4 mt) {
  auto p = Params(t, mt); glm::vec2 offset = scale/s.GetDimensions();
  p.Dimensions = {0,0,offset.x,offset.y};
  p.Font = {difference,intensity,alpha,float(m != nullptr)};
  Draw(Shaders::EdgeDetectedSingleSheetFont_vert, Shaders::EdgeDetectedSingleSheetFont_frag,
       p, {Unwrap(s),m ? Unwrap(*m) : 0}, vertices, indices);
}
void Renderer::DrawCHLCCMenuBackground(const Sprite& s, const Sprite& m, const RectF& d, float alpha) {
  auto p = Params(); p.Alpha.x = std::clamp(alpha,0.0f,1.0f);
  Quad(Shaders::CHLCCMenuBackground_vert, Shaders::CHLCCMenuBackground_frag, p,
       {Unwrap(s.Sheet),Unwrap(m.Sheet)}, d, s.NormalizedBounds(), Tints(glm::vec4(1)), m.NormalizedBounds());
}
void Renderer::DrawBlurredSprite(const Sprite& s, const CornersQuad& d, glm::mat4 t,
    RendererBlurDirection direction, glm::vec4 tint) {
  auto p = Params(t); p.Dimensions = {s.Bounds.Width,s.Bounds.Height,0,0};
  p.Effect.x = direction == RendererBlurDirection::Horizontal ? 1.0f : 0.0f;
  Quad(Shaders::GaussianBlur_vert, Shaders::GaussianBlur_frag, p,
       {Unwrap(s.Sheet)}, d, s.NormalizedBounds(), Tints(tint));
}
void Renderer::DrawMosaic(const Sprite& s, CornersQuad d, float size, glm::mat4 t, glm::vec4 tint) {
  auto p = Params(t); p.Dimensions = {s.Bounds.Width,s.Bounds.Height,0,0}; p.Effect.y = size/s.BaseScale.x;
  Quad(Shaders::Mosaic_vert, Shaders::Mosaic_frag, p, {Unwrap(s.Sheet)}, d, s.NormalizedBounds(), Tints(tint));
}
void Renderer::DrawSubtitleGlyph(const Sprite& s, const CornersQuad& d, glm::mat4 t, glm::vec4 tint) {
  Quad(Shaders::SubtitleGlyph_vert, Shaders::SubtitleGlyph_frag, Params(t),
       {Unwrap(s.Sheet)}, d, s.NormalizedBounds(), Tints(tint));
}
void Renderer::DrawVideoTexture(const YUVFrame& f, const RectF& d, glm::vec4 tint, bool alpha) {
  auto p = Params(); p.Flags.w = float(alpha);
  Quad(Shaders::YUVFrame_vert, Shaders::YUVFrame_frag, p, {f.LumaId,f.CbId,f.CrId},
       d, RectF(0,0,1,1), Tints(tint));
}
void Renderer::DrawVideoTexture(const NV12Frame& f, const RectF& d, glm::vec4 tint, bool alpha) {
  auto p = Params(); p.Flags.w = float(alpha);
  Quad(Shaders::NV12Frame_vert, Shaders::NV12Frame_frag, p, {f.LumaId,f.CbCrId},
       d, RectF(0,0,1,1), Tints(tint));
}
void Renderer::CaptureScreencap(Sprite& sprite) {
  auto it = Textures.find(sprite.Sheet.Texture);
  if (it == Textures.end() || it->second.Width != TargetWidth || it->second.Height != TargetHeight) {
    FreeTexture(sprite.Sheet.Texture);
    sprite.Sheet.Texture = CreateTarget(TargetWidth, TargetHeight);
  }
  Require(Gpu.copy_rgba(TextureHandle(sprite.Sheet.Texture), TextureHandle(Framebuffers[ActiveFramebuffer]),
                       TargetWidth, TargetHeight), "capture screen");
  sprite.Sheet.IsScreenCap = true;
  sprite.Sheet.DesignWidth = float(TargetWidth); sprite.Sheet.DesignHeight = float(TargetHeight);
  sprite.Bounds = RectF(0,0,float(TargetWidth),float(TargetHeight));
  sprite.BaseScale = {Profile::Game::DesignWidth/TargetWidth,Profile::Game::DesignHeight/TargetHeight};
}
void Renderer::SetFramebuffer(size_t buffer) {
  if (buffer > MaxFramebuffers) throw std::out_of_range("Host framebuffer");
  if (!Framebuffers[buffer]) {
    Framebuffers[buffer] = CreateTarget(TargetWidth, TargetHeight);
    Stencils[buffer] = CreateTarget(TargetWidth, TargetHeight);
  }
  ActiveFramebuffer = buffer;
}
int Renderer::GetFramebufferTexture(size_t buffer) {
  if (buffer > MaxFramebuffers) throw std::out_of_range("Host framebuffer");
  if (!buffer) return int(Published);
  auto previous = ActiveFramebuffer; SetFramebuffer(buffer); ActiveFramebuffer = previous;
  return int(Framebuffers[buffer]);
}
void Renderer::ClearStencilBuffer() {
  Require(Gpu.clear_rgba(TextureHandle(Stencils[ActiveFramebuffer]), 0), "clear stencil");
}
void Renderer::Clear(glm::vec4 color) {
  color = glm::clamp(color, 0.0f, 1.0f);
  uint32_t rgba = uint32_t(color.r*255) << 24 | uint32_t(color.g*255) << 16 |
                  uint32_t(color.b*255) << 8 | uint32_t(color.a*255);
  Require(Gpu.clear_rgba(TextureHandle(Framebuffers[ActiveFramebuffer]), rgba), "clear target");
}
void Renderer::UploadPlane(uint32_t id, const void* source, int width, int height, int stride, int channels) {
  const auto* data = static_cast<const uint8_t*>(source);
  std::vector<uint8_t> rgba(size_t(width)*height*4, 0);
  for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
    auto i=(size_t(y)*width+x)*4;
    rgba[i]=data[size_t(y)*stride+x*channels];
    if (channels==2) rgba[i+1]=data[size_t(y)*stride+x*channels+1];
    rgba[i+3]=255;
  }
  Require(Gpu.update_rgba(TextureHandle(id), rgba.data(), width*4), "upload video plane");
}

namespace {
class HostYUVFrame final : public YUVFrame {
 public:
  Renderer* Owner;
  explicit HostYUVFrame(Renderer* owner) : Owner(owner) {}
  void Init(float width, float height) override {
    Width=width; Height=height;
    LumaId=Owner->CreateTarget(uint32_t(width),uint32_t(height));
    CbId=Owner->CreateTarget(uint32_t(width)/2,uint32_t(height)/2);
    CrId=Owner->CreateTarget(uint32_t(width)/2,uint32_t(height)/2);
  }
  void Submit(const void* y,const void* u,const void* v) override {
    Owner->UploadPlane(LumaId,y,int(Width),int(Height),int(Width),1);
    Owner->UploadPlane(CbId,u,int(Width)/2,int(Height)/2,int(Width)/2,1);
    Owner->UploadPlane(CrId,v,int(Width)/2,int(Height)/2,int(Width)/2,1);
  }
  void Release() override { Owner->FreeTexture(LumaId);Owner->FreeTexture(CbId);Owner->FreeTexture(CrId); }
};
class HostNV12Frame final : public NV12Frame {
 public:
  Renderer* Owner;
  explicit HostNV12Frame(Renderer* owner) : Owner(owner) {}
  void Init(float width,float height) override {
    Width=width;Height=height;
    LumaId=Owner->CreateTarget(uint32_t(width),uint32_t(height));
    CbCrId=Owner->CreateTarget(uint32_t(width)/2,uint32_t(height)/2);
  }
  void Submit(const void* y,int ys,const void* uv,int uvs) override {
    Owner->UploadPlane(LumaId,y,int(Width),int(Height),ys,1);
    Owner->UploadPlane(CbCrId,uv,int(Width)/2,int(Height)/2,uvs,2);
  }
  void Release() override { Owner->FreeTexture(LumaId);Owner->FreeTexture(CbCrId); }
};
}
YUVFrame* Renderer::CreateYUVFrame(float width,float height) {
  auto* f = new HostYUVFrame(this); f->Init(width,height); return f;
}
NV12Frame* Renderer::CreateNV12Frame(float width,float height) {
  auto* f = new HostNV12Frame(this); f->Init(width,height); return f;
}

void Renderer::BeginCharacters() {
  DrawingCharacters = true;
  ClearDepth = true;
  Require(Gpu.clear_rgba(TextureHandle(Characters), 0), "clear 3D characters");
}
void Renderer::EndCharacters() {
  DrawingCharacters = false;
  SpriteSheet sheet{float(TargetWidth), float(TargetHeight)};
  sheet.Texture = Characters;
  Sprite sprite(sheet, 0, 0, float(TargetWidth), float(TargetHeight));
  auto previous = Blend;
  Blend = RendererBlendMode::Premultiplied;
  BaseRenderer::DrawSprite(sprite, RectF(0,0,Profile::Game::DesignWidth,Profile::Game::DesignHeight));
  Blend = previous;
}
void Renderer::DrawModel(const char* vertex, const char* fragment, const void* params,
    uint32_t bytes, std::span<const float> vertices, std::array<uint32_t,4> textures,
    bool depth, bool write, bool outline) {
  if (vertices.empty()) return;
  impacto_host_gpu_draw_t request{};
  request.struct_size = sizeof(request);
  request.target = TextureHandle(DrawingCharacters ? Characters : Framebuffers[0]);
  for (size_t i = 0; i < textures.size(); ++i)
    request.textures[i] = TextureHandle(textures[i] ? textures[i] : RectSprite.Sheet.Texture);
  request.vertex_source = vertex; request.fragment_source = fragment;
  request.vertices = vertices.data(); request.vertex_float_count = uint32_t(vertices.size());
  request.vertex_count = uint32_t(vertices.size()/16);
  request.uniforms = params; request.uniform_bytes = bytes;
  request.clip[2] = int32_t(TargetWidth); request.clip[3] = int32_t(TargetHeight);
  request.blend = DrawingCharacters ? 4 : 0;
  request.depth_test = depth; request.depth_write = depth && write;
  request.clear_depth = depth && ClearDepth;
  request.cull = outline ? 2 : 1;
  Require(Gpu.draw(&request), "draw 3D model");
  if (depth) ClearDepth = false;
}
}  // namespace Impacto::Host
