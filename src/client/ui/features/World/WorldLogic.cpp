#include "ui/features/World/WorldLogic.h"
#include "I18N/All.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationNetwork.h"
#include "data/ItemData.h"
#include "data/ResourceData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "render/ModelResources.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionRender.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/World/WorldRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

// Desc: implementation of the CCharInfoBalloonMng class.
// producer: Ahn Sang-Kyu

CCharInfoBalloonMng::CCharInfoBalloonMng(SessionKeeper &keeper)
    : m_charInfoBalloons(keeper), CharactersClient(keeper.CharactersClientStorage())
{
}

CCharInfoBalloonMng::~CCharInfoBalloonMng()
{
    Release();
}

void CCharInfoBalloonMng::Release()
{
    if (!m_isInitialized)
        return;

    m_isInitialized = false;
}

// 함수 이름 : Create()
// 함수 설명 : 캐릭터 정보 풍선 매니저 생성.
//			   (캐릭터 선택씬에서 쓰임. 풍선 5개 생성.)
void CCharInfoBalloonMng::Create()
{
    for (std::size_t i = 0; i < kBalloonCount; ++i)
        m_charInfoBalloons[i].Create(&CharactersClient[i]);

    m_isInitialized = true;
}

// 함수 이름 : Render()
// 함수 설명 : 캐릭터 정보 풍선들 렌더.

// 함수 이름 : UpdateDisplay()
// 함수 설명 : 캐릭터 정보를 업데이트.
void CCharInfoBalloonMng::UpdateDisplay()
{
    if (!m_isInitialized)
        return;

    for (auto &balloon : m_charInfoBalloons)
        balloon.SetInfo();
}

using namespace SEASON3B;
CNewUIMiniMap::CNewUIMiniMap(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), crywolf_(MapProcessForConstruction().Crywolf1st()),
      renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUIMiniMap::~CNewUIMiniMap()
{
    Release();
}
bool CNewUIMiniMap::Create(CNewUIManager *manager, int x, int y)
{
    if (!manager)
        return false;
    m_pNewUIMng = manager;
    manager->AddUIObj(INTERFACE_MINI_MAP, this);
    SetPos(x, y);
    return true;
}

void CNewUIMiniMap::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}
void CNewUIMiniMap::OpenningProcess()
{
    Update();
}
float CNewUIMiniMap::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Minimap;
}
void CNewUIMiniMap::Release()
{
    UnloadImages();
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}
void CNewUIMiniMap::SetBtnPos(int index, float x, float y, float width, float height)
{
    m_Btn_Loc[index][0] = x;
    m_Btn_Loc[index][1] = y;
    m_Btn_Loc[index][2] = width;
    m_Btn_Loc[index][3] = height;
}
bool CNewUIMiniMap::UpdateKeyEvent()
{
    if (!IsVisible() || !m_bSuccess)
        return true;
    if (IsPress(VK_ESCAPE) || IsPress(VK_TAB))
    {
        g_pNewUISystem->Hide(INTERFACE_MINI_MAP);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    if (IsPress(VK_OEM_MINUS))
    {
        panel_.AdjustZoom(-1);
        return false;
    }
    if (IsPress(VK_OEM_PLUS))
    {
        panel_.AdjustZoom(1);
        return false;
    }
    if (IsPress(VK_OEM_4))
    {
        panel_.AdjustOpacity(-1);
        return false;
    }
    if (IsPress(VK_OEM_6))
    {
        panel_.AdjustOpacity(1);
        return false;
    }
    return true;
}

bool CNewUIMiniMap::Update()
{
    visible_ = IsVisible() && m_bSuccess;
    if (!visible_ || !Hero)
        return true;
    heroX_ = Hero->Object.Position[0] / TERRAIN_SCALE;
    heroY_ = Hero->Object.Position[1] / TERRAIN_SCALE;
    heroHeading_ = Hero->Object.Angle[2];
    StageMarkers(gMapManager.ContextMap() == WD_34CRYWOLF_1ST && crywolf_.m_OccupationState > 0);
    return true;
}

bool CNewUIMiniMap::ProcessModernUiInput(const SessionInputEvent &)
{
    return false;
}

bool CNewUIMiniMap::UpdateMouseEvent()
{
    return true;
}
bool SEASON3B::CNewUIMiniMap::InstallImages(const WorldResources &resources)
{
    UnloadImages();
    WorldResources::Failure failure;
    if (!resources.InstallMinimapTexture(sessionKeeper_, failure))
    {
        g_ErrorReport.Write(L"%ls: %ls\r\n", failure.resource.c_str(), failure.detail);
        return false;
    }
    m_bSuccess = resources.HasMinimapImage();
    std::size_t index = 0;
    for (const auto &marker : resources.Minimap().Markers())
    {
        auto &target = m_Mini_Map_Data[index++];
        target.Kind = marker.kind;
        target.Location[0] = marker.location[0];
        target.Location[1] = marker.location[1];
        target.Rotation = marker.rotation;
        std::copy(marker.name.begin(), marker.name.end(), target.Name);
    }
    image_ = sessionKeeper_.TextureNamespace().TryDescribe(IMAGE_MINIMAP_INTERFACE);
    imageOriginPixels_ = resources.Minimap().ImageOriginPixels();
    markerFilter_.reset();
    return true;
}

namespace
{
// mx.transitions.easing.Strong.easeIn, used by S16 MUComponent for both fades.
float FadeProgress(float time)
{
    return time * time * time * time * time;
}
} // namespace
CUIMapName::CUIMapName(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper), panel_(keeper)
{
}
CUIMapName::~CUIMapName() = default;
void CUIMapName::Init()
{
    visible_ = false;
    alpha_ = 0;
    started_ = 0;
    text_ = {};
    panel_.Release();
}
void CUIMapName::ShowMapName()
{
    visible_ = gMapManager.ContextMap() != WD_40AREA_FOR_GM;
    started_ = ::timeGetTime();
    alpha_ = 0;
    text_ = {};
    if (!visible_)
        return;
    text_[0] = gMapManager.GetMapName(gMapManager.ContextMap());
#ifdef ASG_ADD_GENS_SYSTEM
    if (IsStrifeMap(gMapManager.ContextMap()))
        text_[1] = I18N::Game::BattleZone;
#endif
}
void CUIMapName::Update()
{
    if (!visible_)
        return;
    constexpr float MillisecondsPerSecond = 1000.0f;
    const float elapsed = float(DWORD(::timeGetTime() - started_)) / MillisecondsPerSecond;
    const float fade = panel_.FadeSeconds();
    const float holdEnd = fade + panel_.HoldSeconds();
    if (elapsed < fade)
        alpha_ = FadeProgress(elapsed / fade);
    else if (elapsed < holdEnd)
        alpha_ = 1;
    else
    {
        const float progress = std::min(1.0f, (elapsed - holdEnd) / fade);
        alpha_ = 1 - FadeProgress(progress);
        visible_ = progress < 1;
    }
}

// Construction/Destruction

SEASON3B::CNewUINameWindow::CNewUINameWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), gameplay_(GameplayForConstruction()),
      renderer_(RendererForConstruction()), Items(keeper.ItemsStorage()),
      cameraProjection_(keeper.CameraProjectionObject()), monsterInfo_(keeper)

{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;

    m_bShowItemName = false;
    m_bShowMonsterHealthBar = false;
}

SEASON3B::CNewUINameWindow::~CNewUINameWindow()
{
    Release();
}

bool SEASON3B::CNewUINameWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_NAME_WINDOW, this);

    SetPos(x, y);

    Show(true);

    return true;
}

void SEASON3B::CNewUINameWindow::Release()
{
    monsterInfo_.Release();
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUINameWindow::UpdateMouseEvent()
{
    return true;
}

bool SEASON3B::CNewUINameWindow::UpdateKeyEvent()
{
    if (IsPress(VK_MENU) == true)
    {
        m_bShowItemName = !m_bShowItemName;
    }

    if (IsPress(VK_F8) == true)
    {
        m_bShowMonsterHealthBar = !m_bShowMonsterHealthBar;
    }

    return true;
}

bool SEASON3B::CNewUINameWindow::Update()
{
    UpdateNameEntries();
    StageMonsterInfo();
    return true;
}

void SEASON3B::CNewUINameWindow::UpdateNameEntries()
{
    if (renderer_.PlayerNameDisplay() || g_bGMObservation)
    {
        for (int i = 0; i < CharactersClient.Size(); i++)
        {
            if (!CharactersClient.IsValidIndex(i))
                continue;
            CHARACTER *character = &CharactersClient[i];
            OBJECT *object = &character->Object;
            if (object->Live && object->Kind == KIND_PLAYER &&
                IsShopTitleVisible(character) == false)
            {
                CreateChat(character->ID, L"", character);
            }
        }
    }

#ifndef GUILD_WAR_EVENT
    if (gMapManager.InChaosCastle() == true && (SelectedNpc != -1 || SelectedCharacter != -1))
    {
        return;
    }
#endif

    if (SelectedNpc != -1)
    {
        CHARACTER *character = &CharactersClient[SelectedNpc];
        CreateChat(character->ID, L"", character);
        return;
    }
    if (SelectedCharacter == -1)
    {
        return;
    }

    CHARACTER *character = &CharactersClient[SelectedCharacter];
    if (character->Object.Kind == KIND_MONSTER || IsShopTitleVisible(character))
    {
        return;
    }
#ifdef ASG_ADD_GENS_SYSTEM
#ifndef PBG_MOD_STRIFE_GENSMARKRENDER
    if (IsStrifeMap(World) && Hero->m_byGensInfluence != character->m_byGensInfluence)
    {
        return;
    }
#endif
#endif
    CreateChat(character->ID, L"", character);
}

float SEASON3B::CNewUINameWindow::GetLayerDepth()
{
    return 1.0f;
}
