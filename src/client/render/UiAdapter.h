#pragma once
#include "render/Assets.h"
#include "render/FrameTape.h"
#include "render/Textures.h"
#include "session/SessionRuntime.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include <RmlUi/Core.h>
#include <cmath>
#include <deque>
#include <filesystem>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Rml
{
struct Mesh;
}
namespace UI::Modern
{
// Decode generated GFx vector geometry once, before retaining it in a render manager.
Rml::Mesh LoadRmlVectorMesh(const std::filesystem::path &design);
} // namespace UI::Modern

#pragma warning(disable : 4786)

#define INVENTORY_CAMERA_Z_ORDER 5.5f
#define INFORMATION_CAMERA_Z_ORDER 10.9f
#define ITEMHOTKEYNUMBER_CAMERA_Z_ORDER 11.f
#define TOOLTIP_CAMERA_Z_ORDER 5.6f

namespace SEASON3B
{
typedef void (*UI_2DEFFECT_CALLBACK)(LPVOID pClass, DWORD dwParamA, DWORD dwParamB);

#pragma pack(push, 1)
typedef struct tagUI_2DEFFECT_INFO
{
    UI_2DEFFECT_CALLBACK pCallbackFunc;
    LPVOID pClass;
    DWORD dwParamA, dwParamB;
} UI_2DEFFECT_INFO;
#pragma pack(pop)

class CNewUIManager;

class INewUI3DRenderObj
{
  public:
    virtual void Render3D() = 0;
    virtual bool IsVisible() const = 0;
    virtual bool IsOwnerRendered() const
    {
        return false;
    }
};

class CNewUI3DCamera : public CNewUIObj, protected SessionUiLegacyBindings
{
    typedef std::list<INewUI3DRenderObj *> type_list_3dobj;
    typedef std::deque<UI_2DEFFECT_INFO> type_deque_2deffect;

    type_list_3dobj m_list3DObjs;
    type_deque_2deffect m_deque2DEffects;
    CameraProjection &cameraProjection_;

    UINT m_uiWidth, m_uiHeight;
    float m_fZOrder;
    int m_iCameraIndex;

  public:
    explicit CNewUI3DCamera(SessionKeeper &keeper);
    virtual ~CNewUI3DCamera();

    bool Create(int iCameraIndex, UINT uiWidth, UINT uiHeight, float fZOrder);
    void Release();
    void UpdateDimensions(UINT uiWidth, UINT uiHeight);

    bool IsEmpty();

    void Add3DRenderObj(INewUI3DRenderObj *pObj);
    void Remove3DRenderObj(INewUI3DRenderObj *pObj);
    void RemoveAll3DRenderObjs();

    void RenderUI2DEffect(UI_2DEFFECT_CALLBACK pCallbackFunc, LPVOID pClass, DWORD dwParamA,
                          DWORD dwParamB);
    void DeleteUI2DEffectObject(UI_2DEFFECT_CALLBACK pCallbackFunc);

    float GetLayerDepth(); //. fZOrder == fLayerDepth
    int GetCameraIndex() const;

    bool Render();
    bool RenderObject(INewUI3DRenderObj &object);
    void RenderUI2DEffects();
    bool Update();           //. DOING NOTHING
    bool UpdateMouseEvent(); //. DOING NOTHING
    bool UpdateKeyEvent();   //. DOING NOTHING

  protected:
    void Begin3D();
    void End3D();
};

class CNewUI3DRenderMng : protected SessionUiLegacyBindings
{
    typedef std::list<CNewUI3DCamera *> type_list_camera;
    type_list_camera m_listCamera;

    CNewUIManager *m_pNewUIMng;

  public:
    explicit CNewUI3DRenderMng(SessionKeeper &keeper);
    virtual ~CNewUI3DRenderMng();

    bool Create(CNewUIManager *pNewUIMng);
    void Release();
    void UpdateAllCameraDimensions(UINT uiWidth, UINT uiHeight);

    void Add3DRenderObj(INewUI3DRenderObj *pObj, float fZOrder = INFORMATION_CAMERA_Z_ORDER);
    void Remove3DRenderObj(INewUI3DRenderObj *pObj);
    void RemoveAll3DRenderObjs();

    void RenderUI2DEffect(float fZOrder, UI_2DEFFECT_CALLBACK pCallbackFunc, LPVOID pClass,
                          DWORD dwParamA, DWORD dwParamB);
    void DeleteUI2DEffectObject(UI_2DEFFECT_CALLBACK pCallbackFunc);
    bool RenderObject(INewUI3DRenderObj &object, float fZOrder);
    void RenderUI2DEffects();

  protected:
    CNewUI3DCamera *FindCamera(float fZOrder);
    int FindAvailableCameraIndex();
};
} // namespace SEASON3B

// Shared render capture types used by the RmlUi runtime.
class SessionRenderUnit;

namespace UI::Modern
{
inline void CopyStraightAlphaPixels(Rml::Span<const Rml::byte> source,
                                    std::vector<std::byte> &destination)
{
    for (std::size_t i = 0; i < source.size(); i += 4)
    {
        const unsigned alpha = source[i + 3];
        for (std::size_t channel = 0; channel < 3; ++channel)
            destination[i + channel] = static_cast<std::byte>(
                alpha ? (source[i + channel] * 255U + alpha / 2U) / alpha : 0U);
        destination[i + 3] = static_cast<std::byte>(alpha);
    }
}

struct CompiledGeometry final
{
    std::uint64_t revision = 0;
    std::shared_ptr<const std::vector<RenderTapeVertex>> vertices;
    std::shared_ptr<const std::vector<std::uint32_t>> indices;
};

struct RmlUiRenderSnapshot final
{
    struct ClipReset final
    {
        std::size_t beforeDraw;
        std::uint32_t value;
    };
    RmlUiPresentation presentation;
    std::deque<LogicalGeometryAssetLease> geometryLeases;
    std::vector<std::shared_ptr<const LogicalRenderAssetRef>> textureLeases;
    std::vector<TrustedGeometryDraw> drawPlan;
    std::vector<ClipReset> clipResets;
    bool valid = true;
};

class TapeRenderInterface final : public Rml::RenderInterface
{
  public:
    explicit TapeRenderInterface(CGlobalBitmap &bitmaps) noexcept : bitmaps_(bitmaps)
    {
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> sourceVertices,
                                                Rml::Span<const int> sourceIndices) override;

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;

    void CaptureGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                         Rml::TextureHandle texture, const std::array<float, 4> *tint);

    Rml::CompiledShaderHandle CompileShader(const Rml::String &name,
                                            const Rml::Dictionary &parameters) override;

    void ReleaseShader(Rml::CompiledShaderHandle handle) override;

    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation, Rml::TextureHandle texture) override;

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override;

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i dimensions) override;

    void ReleaseTexture(Rml::TextureHandle handle) override;

    void EnableScissorRegion(bool enable) override;

    void EnableClipMask(bool enable) override;

    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation) override;

    void ApplyClipState(RenderTapeConstants &constants) const noexcept;

    void SetScissorRegion(Rml::Rectanglei region) override;

    void SetTransform(const Rml::Matrix4f *transform) override;

    void ApplyGeometryTransform(RenderTapeConstants &constants,
                                const RmlUiPresentation &presentation,
                                Rml::Vector2f translation) const;

    void BeginCapture(RmlUiRenderSnapshot &capture, SessionRenderUnit &renderer,
                      CGlobalBitmap &bitmaps, SessionTextureNamespace &textures) noexcept;

    void EndCapture() noexcept;

  private:
    std::optional<LogicalRenderAssetRef> CaptureTexture(Rml::TextureHandle texture);

    struct LoadedTexture final
    {
        LogicalRenderAssetRef asset;
        std::uint32_t bitmapIndex = 0;
        CGlobalBitmap *owner = nullptr;
        ~LoadedTexture()
        {
            if (owner)
                owner->UnloadImage(bitmapIndex);
        }
    };

    bool RetainLoadedTexture(Rml::TextureHandle handle, const LogicalRenderAssetMetadata &metadata);

    CGlobalBitmap &bitmaps_;
    RmlUiRenderSnapshot *capture_ = nullptr;
    SessionRenderUnit *revisionOwner_ = nullptr;
    CGlobalBitmap *captureBitmaps_ = nullptr;
    SessionTextureNamespace *captureTextures_ = nullptr;
    std::unordered_map<Rml::TextureHandle, std::shared_ptr<const LogicalRenderAssetRef>>
        generatedTextures_;
    std::unordered_map<Rml::TextureHandle, std::shared_ptr<LoadedTexture>> loadedTextures_;
    Rml::TextureHandle nextGeneratedTexture_ = static_cast<Rml::TextureHandle>(1)
                                               << (sizeof(Rml::TextureHandle) * 8U - 1U);
    Rml::Rectanglei scissor_{};
    bool scissorEnabled_ = false;
    bool clipEnabled_ = false;
    std::uint32_t clipReference_ = 0;
    RenderStencilOperation clipWriteOperation_ = RenderStencilOperation::Keep;
    Rml::Matrix4f transform_ = Rml::Matrix4f::Identity();
};
} // namespace UI::Modern
