#pragma once

#include "../renderer.h"
#include "../../host/impacto_host_gpu.h"
#include <array>
#include <map>

namespace Impacto::Host {

inline impacto_host_gpu_callbacks_t Gpu{};

class Renderer final : public BaseRenderer {
 public:
  Renderer();
  ~Renderer() override;
  void Init() override;
  void Shutdown() override;
  void BeginFrame() override;
  void BeginFrame2D() override;
  void EndFrame() override;
  void Present();
  uint64_t FrameTexture() const;
  uint32_t Width() const { return TargetWidth; }
  uint32_t Height() const { return TargetHeight; }
  uint32_t MapSpriteSheet(const SpriteSheet& sheet) override;
  bool LoadSurf(int surf, int archive, int file) override;
  void UnloadSurf(int surf) override;
  uint32_t SubmitTexture(TexFmt format, uint8_t* pixels, int width, int height) override;
  int GetSpriteSheetImage(const SpriteSheet& sheet, std::span<uint8_t> output) override;
  void FreeTexture(uint32_t id) override;
  YUVFrame* CreateYUVFrame(float width, float height) override;
  NV12Frame* CreateNV12Frame(float width, float height) override;
  void UploadPlane(uint32_t id, const void* data, int width, int height,
                   int stride, int channels);
  void DrawSprite(const Sprite&, const CornersQuad&, glm::mat4,
                  std::span<const glm::vec4, 4>, glm::vec3, bool, bool, bool) override;
  void DrawMaskedSprite(const Sprite&, const Sprite&, const CornersQuad&,
                        const CornersQuad&, int, int, glm::mat4, glm::mat4,
                        std::span<const glm::vec4, 4>, bool, bool) override;
  void DrawMaskedBinarySprite(const Sprite&, const Sprite&, const CornersQuad&,
                              const CornersQuad&, glm::mat4, std::optional<glm::mat4>,
                              std::span<const glm::vec4, 4>, bool) override;
  void DrawMaskedSpriteOverlay(const Sprite&, const Sprite&, const CornersQuad&,
                               const CornersQuad&, int, int, glm::mat4, glm::mat4,
                               std::span<const glm::vec4, 4>, bool, bool) override;
  void DrawPrimitives(const SpriteSheet&, const SpriteSheet*, ShaderProgramType,
                      std::span<const VertexBufferSprites>, std::span<const uint16_t>,
                      glm::mat4, glm::mat4, bool, TopologyMode, bool) override;
  void DrawCCMessageBox(const Sprite&, const Sprite&, const RectF&, glm::vec4,
                        int, int, float) override;
  void DrawEdgeDetectedSingleSheetFont(const SpriteSheet&, const SpriteSheet*,
      std::span<const VertexBufferSprites>, std::span<const uint16_t>,
      float, float, float, glm::vec2, glm::mat4, glm::mat4) override;
  void DrawCHLCCMenuBackground(const Sprite&, const Sprite&, const RectF&, float) override;
  void DrawBlurredSprite(const Sprite&, const CornersQuad&, glm::mat4,
                         RendererBlurDirection, glm::vec4) override;
  void DrawMosaic(const Sprite&, CornersQuad, float, glm::mat4, glm::vec4) override;
  void DrawVideoTexture(const YUVFrame&, const RectF&, glm::vec4, bool) override;
  void DrawVideoTexture(const NV12Frame&, const RectF&, glm::vec4, bool) override;
  void DrawSubtitleGlyph(const Sprite&, const CornersQuad&, glm::mat4, glm::vec4) override;
  void CaptureScreencap(Sprite&) override;
  void SetFramebuffer(size_t buffer) override;
  int GetFramebufferTexture(size_t buffer) override;
  void EnableScissor() override { ScissorEnabled = true; }
  void SetScissorRect(const RectF& rect) override { ScissorRect = rect; }
  void DisableScissor() override { ScissorEnabled = false; }
  void SetStencilMode(StencilBufferMode mode) override { StencilMode = mode; }
  void ClearStencilBuffer() override;
  void SetBlendMode(RendererBlendMode mode) override { Blend = mode; }
  void Clear(glm::vec4 color) override;

  uint64_t TextureHandle(uint32_t id) const;
  uint32_t CreateTarget(uint32_t width, uint32_t height);
  void BeginCharacters();
  void EndCharacters();
  void DrawModel(const char* vertex, const char* fragment, const void* params,
                 uint32_t bytes, std::span<const float> vertices,
                 std::array<uint32_t, 4> textures, bool depth, bool write, bool outline);

 private:
  struct Parameters {
    glm::mat4 Projection{1.0f};
    glm::mat4 Transformation{1.0f};
    glm::mat4 MaskTransformation{1.0f};
    glm::vec4 Alpha{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 ColorShift{};
    glm::vec4 Flags{};
    glm::vec4 Dimensions{};
    glm::vec4 Font{};
    glm::vec4 Effect{};
    glm::vec4 Stencil{};
    glm::vec4 Reserved{};
  };
  static_assert(sizeof(Parameters) == 20 * 16);
  struct TextureRecord { uint64_t Handle; uint32_t Width; uint32_t Height; };
  std::map<uint32_t, TextureRecord> Textures;
  uint32_t NextTexture = 1;
  std::array<uint32_t, MaxFramebuffers + 1> Framebuffers{};
  std::array<uint32_t, MaxFramebuffers + 1> Stencils{};
  // Published texture is distinct from the current render target. Godot queues
  // presentation after submission; never expose a texture still being drawn.
  uint32_t Published = 0;
  uint32_t Characters = 0;
  bool DrawingCharacters = false;
  bool ClearDepth = true;
  uint32_t TargetWidth = 0, TargetHeight = 0;
  size_t ActiveFramebuffer = 0;
  bool ScissorEnabled = false;
  RectF ScissorRect{};
  RendererBlendMode Blend = RendererBlendMode::Normal;
  StencilBufferMode StencilMode = StencilBufferMode::Off;
  glm::mat4 Projection{1.0f};
  Parameters Params(glm::mat4 transform = glm::mat4(1.0f),
                    glm::mat4 mask = glm::mat4(1.0f)) const;
  uint32_t Unwrap(const SpriteSheet&) const;
  void Draw(const char* vertex, const char* fragment, Parameters params,
            std::array<uint32_t, 3> textures,
            std::span<const VertexBufferSprites> vertices,
            std::span<const uint16_t> indices,
            TopologyMode topology = TopologyMode::Triangles,
            bool repeat = false, bool replace = false);
  void Quad(const char* vertex, const char* fragment, Parameters params,
            std::array<uint32_t, 3> textures, CornersQuad dest, CornersQuad uv,
            std::span<const glm::vec4, 4> tints, CornersQuad mask = RectF(),
            bool repeat = false, bool replace = false);
  void Flush() override {}
};

}  // namespace Impacto::Host
