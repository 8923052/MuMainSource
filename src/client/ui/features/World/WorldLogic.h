#pragma once
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/World/WorldRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <optional>
#include <vector>

// Desc: interface for the CCharInfoBalloonMng class.
//		 캐릭터 정보 풍선 관리 클래스.(캐릭터 선택씬에서 쓰임)
// producer: Ahn Sang-Kyu

struct SessionCharacterPopulationStorage;

class CCharInfoBalloonMng
{
  protected:
    static constexpr std::size_t kBalloonCount = 5;
    SessionBoundArray<CCharInfoBalloon, kBalloonCount> m_charInfoBalloons;
    SessionCharacterPopulationStorage &CharactersClient;
    bool m_isInitialized{false};

  public:
    explicit CCharInfoBalloonMng(SessionKeeper &keeper);
    virtual ~CCharInfoBalloonMng();

    void Release();
    void Create();
    void Render();
    void UpdateDisplay();
};

class CUIMapName : protected SessionUiLegacyBindings
{
  public:
    explicit CUIMapName(SessionKeeper &keeper);
    virtual ~CUIMapName();
    void Init();
    void ShowMapName();
    void Update();
    void Render();
    bool PrepareModernUiOnWorker(int width, int height);

  private:
    friend class PresentationSeparationTestPeer;
    UI::Modern::PC::World::RmlMapNamePanel panel_;
    std::array<std::wstring, 3> text_;
    DWORD started_ = 0;
    float alpha_ = 0;
    bool visible_ = false;
};

class SessionKeeper;
class SessionGameplayUnit;
class SessionRenderUnit;
class CMapManager;

namespace SEASON3B
{
// item name
class CNewUINameWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUINameWindow(SessionKeeper &keeper);
    virtual ~CNewUINameWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);

    float GetLayerDepth(); // 1.0f

  private:
    SessionGameplayUnit &gameplay_;
    SessionRenderUnit &renderer_;
    ITEM_t (&Items)[MAX_ITEMS];
    CameraProjection &cameraProjection_;
    void UpdateNameEntries();
    void RenderName();

    CNewUIManager *m_pNewUIMng; // UI manager
    POINT m_Pos;                // window position

    bool m_bShowItemName;
    bool m_bShowMonsterHealthBar;

    void StageMonsterInfo();
    void StageOverheadMonsters();
    UI::Modern::PC::World::RmlMonsterInfoLayer monsterInfo_;
    std::vector<UI::Modern::PC::World::RmlMonsterInfoLayer::Content> monsters_;
};
} // namespace SEASON3B

class WorldResources;
class WorldMinimapTestPeer;
class CGMCrywolf1st;
struct SessionInputEvent;
namespace SEASON3B
{
class CNewUIMiniMap : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_MINIMAP_INTERFACE = BITMAP_MINI_MAP_BEGIN
    };
    explicit CNewUIMiniMap(SessionKeeper &keeper);
    ~CNewUIMiniMap();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    void SetBtnPos(int index, float x, float y, float width, float height);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    float GetLayerDepth();
    void OpenningProcess();
    void ClosingProcess();
    bool InstallImages(const WorldResources &resources);
    void UnloadImages();
    bool m_bSuccess = false;

  private:
    friend class ::WorldMinimapTestPeer;
    void StageMarkers(bool occupied);
    CNewUIManager *m_pNewUIMng = nullptr;
    MINI_MAP m_Mini_Map_Data[MAX_MINI_MAP_DATA]{};
    float m_Btn_Loc[MAX_MINI_MAP_DATA][4]{};
    CGMCrywolf1st &crywolf_;
    SessionRenderUnit &renderer_;
    UI::Modern::RmlMiniMapPanel panel_;
    std::optional<LogicalRenderAssetMetadata> image_;
    std::array<int, 2> imageOriginPixels_{};
    std::vector<WorldMinimapData::Marker> markers_;
    std::optional<bool> markerFilter_;
    std::uint64_t markerRevision_ = 0;
    float heroX_ = 0, heroY_ = 0, heroHeading_ = 0;
    bool visible_ = false;
};
} // namespace SEASON3B

namespace CharacterLabelDetail
{

#pragma pack(push)
#pragma pack()
struct GuildStatusText
{
    std::uint8_t status;
    int textIndex;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::array<GuildStatusText, 5> kGuildStatusTexts{{
    {0, 1330},
    {32, 1302},
    {64, 1301},
    {128, 1300},
    {255, 488},
}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <std::size_t N> void CopyWideString(wchar_t (&destination)[N], const wchar_t *source)
{
    if (source == nullptr)
    {
        destination[0] = L'\0';
        return;
    }

    std::wcsncpy(destination, source, N - 1);
    destination[N - 1] = L'\0';
}
#pragma pack(pop)

} // namespace CharacterLabelDetail

namespace HeroPositionDetail
{
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
enum class DesignKey
{
    FrameWidth,
    FrameHeight,
    OptionRect,
    ActionRect,
    TooltipOffset
};
#pragma pack(pop)

} // namespace HeroPositionDetail

namespace CharacterLabelDetail
{

DWORD ResolveNameColor(std::uint8_t controlCode);
int ResolveGuildTextIndex(std::uint8_t guildStatus);
} // namespace CharacterLabelDetail
