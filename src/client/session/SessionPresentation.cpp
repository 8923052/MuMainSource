#include "session/SessionPresentation.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "app/AppWindow.h"
#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/Guild.h"
#include "domain/ItemsSkills.h"
#include "domain/MapPresentation.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "I18N/All.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionAudio.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

#ifdef _WIN32
#include <eh.h>
#endif

bool LegacySessionVisualView::UpdateVisualAnimation() noexcept
{
    return true;
}

bool LegacySessionVisualView::UpdateRenderScratch() noexcept
{
    return true;
}

SessionPresentationUnit::SessionPresentationUnit(SessionKeeper &keeper,
                                                 SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), observer_(observer)
{
    (void)sessionKeeper_.RegisterPresentation(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::PresentationUnitConstructed);
    }
}

SessionPresentationUnit::~SessionPresentationUnit()
{
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::PresentationUnitDestroyed);
    }
}

bool SessionPresentationUnit::BeginFrame() noexcept
{
    SessionUiUnit *ui = sessionKeeper_.Ui();
    return ui != nullptr && ui->BeginPresentation();
}

GameplayInteractionFact SessionPresentationUnit::PickForLogic() noexcept
{
    // This value-only fact is copied into ApplicationFramePlan before any
    // session lane begins; workers never consult UI/input ownership directly.
    SessionInteractionUnit *interaction = sessionKeeper_.Interaction();
    return interaction != nullptr ? interaction->PickForLogic() : GameplayInteractionFact{};
}

bool SessionPresentationUnit::FinishFrame() noexcept
{
    SessionUiUnit *ui = sessionKeeper_.Ui();
    return ui != nullptr && ui->FinishPresentation();
}

bool SessionPresentationUnit::UpdateVisualAnimation() noexcept
{
    SessionVisualUnit *visual = sessionKeeper_.Visual();
    return visual != nullptr && visual->UpdateAnimation();
}

std::optional<SessionVisualAnimationInput> SessionPresentationUnit::
    CaptureVisualAnimationInputOnOwner() const noexcept
{
    SessionVisualUnit *visual = sessionKeeper_.Visual();
    return visual != nullptr ? visual->CaptureAnimationInputOnOwner() : std::nullopt;
}

bool SessionPresentationUnit::TryBuildVisualAnimationResult(
    const SessionVisualAnimationInput &input, SessionVisualAnimationResult &result) noexcept
{
    return SessionVisualUnit::TryBuildAnimationResult(input, result);
}

bool SessionPresentationUnit::ApplyVisualAnimationResultOnOwner(
    const SessionVisualAnimationResult &result) noexcept
{
    SessionVisualUnit *visual = sessionKeeper_.Visual();
    return visual != nullptr && visual->ApplyAnimationResultOnOwner(result);
}

bool SessionPresentationUnit::UpdateRenderScratch() noexcept
{
    SessionVisualUnit *visual = sessionKeeper_.Visual();
    return visual != nullptr && visual->UpdateRenderScratch();
}

bool SessionPresentationUnit::UpdateSpatialAudio() noexcept
{
    SessionAudioLogicUnit *audio = sessionKeeper_.AudioLogic();
    return audio != nullptr && audio->UpdateSpatialAudio();
}

bool SessionPresentationUnit::BuildSpatialAudioEffect(SessionOrderedEffectBatch &effects,
                                                      std::uint64_t stableSequence) noexcept
{
    SessionAudioLogicUnit *audio = sessionKeeper_.AudioLogic();
    return audio != nullptr && audio->BuildSpatialAudioEffect(effects, stableSequence);
}

bool SessionPresentationUnit::PrepareInteraction() noexcept
{
    if (!sessionKeeper_.Visual()->UpdateCameraOnOwner())
        return false;
    SessionInteractionUnit *interaction = sessionKeeper_.Interaction();
    return interaction != nullptr && interaction->PrepareFrame();
}

SessionVisualUnit::SessionVisualUnit(SessionKeeper &keeper,
                                     SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), g_frameTiming(keeper.FrameTiming()),
      lastWaterChange(keeper.TerrainStorage().lastWaterChange),
      g_timer2StartTickTime(keeper.FrameTimer2StartTickTime()),
      FPS_ANIMATION_FACTOR(keeper.FrameAnimationFactor()), g_ConsoleDebug(keeper.ConsoleDebug()),
      gMapManager(keeper.MapManagerObject()), cameraManager_(keeper.CameraManagerObject()),
      Destroy(keeper.PlatformDestroyRequested()), gameplay_(keeper.GameplayForConstruction()),
      g_petProcess(keeper.PetProcessObject()), boneManager_(keeper.BoneManagerObject()),
      ObjectBlock(keeper.ObjectBlocks()), g_iActionObjectType(keeper.ActionObjectType()),
      g_iActionWorld(keeper.ActionWorld()), g_iActionTime(keeper.ActionTime()),
      Sprites(keeper.SpritesStorage()), g_SummonSystem(keeper.SummonSystemObject()),
      g_CMonkSystem(keeper.MonkSystemObject()), g_Camera(keeper.CameraStateObject()),
      WindowWidth(keeper.PlatformWindowWidth()), WindowHeight(keeper.PlatformWindowHeight()),
      Chat(keeper.ChatStorage().Chat), Random(keeper.RandomForConstruction()),
      WorldTime(keeper.FrameWorldTime()), observer_(observer)
{
    (void)sessionKeeper_.RegisterVisual(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::VisualUnitConstructed);
    }
}

SessionVisualUnit::~SessionVisualUnit()
{
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::VisualUnitDestroyed);
    }
}

bool SessionVisualUnit::UpdateAnimation() noexcept
{
    SessionVisualView *visual = sessionKeeper_.VisualView();
    return visual != nullptr &&
           (visual->UsesSessionOwnedLegacyBehavior() ? UpdateSceneVisualAnimation()
                                                     : visual->UpdateVisualAnimation());
}

bool SessionVisualUnit::UpdateRenderScratch() noexcept
{
    SessionVisualView *visual = sessionKeeper_.VisualView();
    return visual != nullptr &&
           (visual->UsesSessionOwnedLegacyBehavior() ? UpdateSceneRenderScratch()
                                                     : visual->UpdateRenderScratch());
}

std::optional<SessionVisualAnimationInput> SessionVisualUnit::CaptureAnimationInputOnOwner()
    const noexcept
{
    SessionVisualView *visual = sessionKeeper_.VisualView();
    if (visual == nullptr)
    {
        return std::nullopt;
    }
    const bool legacy = visual->UsesSessionOwnedLegacyBehavior();
    const bool active =
        SceneFlag == LOG_IN_SCENE || SceneFlag == CHARACTER_SCENE || SceneFlag == MAIN_SCENE;
    return SessionVisualAnimationInput{
        legacy,
        active,
        legacy && active && Destroy,
        legacy ? g_frameTiming.currentTickCount : 0.0,
        legacy ? lastWaterChange : 0.0,
        legacy ? WaterTextureNumber : 0,
        sessionKeeper_.ApplicationConfig().legacyReferenceFps,
    };
}

bool SessionVisualUnit::TryBuildAnimationResult(const SessionVisualAnimationInput &input,
                                                SessionVisualAnimationResult &result) noexcept
{
    SessionVisualAnimationResult candidate{
        input.workerSafeLegacyPath, true,
        input.previousWaterChange,  input.previousWaterChange,
        input.previousWaterTexture, input.previousWaterTexture,
        input.activeScene,          input.destroyRequested,
        input.currentTickCount,
    };
    if (!input.workerSafeLegacyPath)
    {
        result = candidate;
        return true;
    }
    constexpr int NumberOfWaterTextures = 32;
    if (!std::isfinite(input.currentTickCount) || !std::isfinite(input.previousWaterChange) ||
        input.previousWaterTexture < 0 || input.previousWaterTexture >= NumberOfWaterTextures)
    {
        return false;
    }
    if (input.activeScene)
    {
        const double timePerFrame = 1000.0 / input.referenceFps;
        const double elapsed = input.currentTickCount - input.previousWaterChange;
        if (!std::isfinite(elapsed))
        {
            return false;
        }
        if (elapsed >= timePerFrame)
        {
            const double stepCount = std::floor(elapsed / timePerFrame);
            if (!std::isfinite(stepCount) || stepCount < 1.0 ||
                stepCount >= static_cast<double>((std::numeric_limits<std::uint64_t>::max)()))
            {
                return false;
            }
            const auto steps = static_cast<std::uint64_t>(stepCount);
            candidate.nextWaterTexture =
                static_cast<int>((static_cast<std::uint64_t>(input.previousWaterTexture) +
                                  steps % NumberOfWaterTextures) %
                                 NumberOfWaterTextures);
            candidate.nextWaterChange += steps * timePerFrame;
        }
        candidate.succeeded = !input.destroyRequested;
    }
    result = candidate;
    return true;
}

bool SessionVisualUnit::ApplyAnimationResultOnOwner(
    const SessionVisualAnimationResult &result) noexcept
{
    SessionVisualView *visual = sessionKeeper_.VisualView();
    if (visual == nullptr)
    {
        return false;
    }
    if (!result.workerSafeLegacyPath)
    {
        return !visual->UsesSessionOwnedLegacyBehavior() && visual->UpdateVisualAnimation();
    }
    if (!visual->UsesSessionOwnedLegacyBehavior())
    {
        return false;
    }
    const bool active =
        SceneFlag == LOG_IN_SCENE || SceneFlag == CHARACTER_SCENE || SceneFlag == MAIN_SCENE;
    const bool destroyRequested = active && Destroy;
    SessionVisualAnimationResult accepted = result;
    if (lastWaterChange != result.previousWaterChange ||
        WaterTextureNumber != result.previousWaterTexture || active != result.capturedActiveScene ||
        destroyRequested != result.capturedDestroyRequested)
    {
        const SessionVisualAnimationInput refreshed{
            true,
            active,
            destroyRequested,
            result.capturedCurrentTickCount,
            lastWaterChange,
            WaterTextureNumber,
            sessionKeeper_.ApplicationConfig().legacyReferenceFps,
        };
        if (!TryBuildAnimationResult(refreshed, accepted))
        {
            return false;
        }
    }
    lastWaterChange = accepted.nextWaterChange;
    WaterTextureNumber = accepted.nextWaterTexture;
    return accepted.succeeded;
}

void SessionVisualUnit::UpdateWaterAnimation()
{
    const SessionVisualAnimationInput input{true,
                                            true,
                                            false,
                                            g_frameTiming.currentTickCount,
                                            lastWaterChange,
                                            WaterTextureNumber,
                                            sessionKeeper_.ApplicationConfig().legacyReferenceFps};
    SessionVisualAnimationResult result;
    if (!TryBuildAnimationResult(input, result))
        return;
    lastWaterChange = result.nextWaterChange;
    WaterTextureNumber = result.nextWaterTexture;
}

int SessionLegacyCalls::CreateSprite(int type, const vec_t *position, float scale,
                                     const vec_t *light, const OBJECT *owner, float rotation,
                                     int subType) const
{
    return sessionKeeper_.Visual()->CreateSprite(type, position, scale, light, owner, rotation,
                                                 subType);
}

void SessionVisualUnit::AdvanceSelectedCharacterEffects()
{
    if (SelectedHero == -1)
        return;
    auto &character = CharactersClient[SelectedHero];
    auto *o = &character.Object;
    if (!o->Live)
        return;
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    Vector(0.15f, 0.15f, 0.15f, o->Light);
    CreateParticleFpsChecked(BITMAP_EFFECT, o->Position, o->Angle, o->Light, 4);
    CreateParticleFpsChecked(BITMAP_EFFECT, o->Position, o->Angle, o->Light, 5);
}
CLoadingScene::CLoadingScene(SessionKeeper &keeper)
    : m_asprBack(keeper), legacyUiManager_(keeper.LegacyUiManagerForConstruction()),
      WindowWidth(keeper.PlatformWindowWidth()), WindowHeight(keeper.PlatformWindowHeight())
{
}

CLoadingScene::~CLoadingScene()
{
}

void CLoadingScene::Create()
{
    float fScaleX = static_cast<float>(WindowWidth) / 800.0f;
    float fScaleY = static_cast<float>(WindowHeight) / 600.0f;

    int anHeight[LDS_BACK_MAX] = {512, 512, 88, 88};
    for (int i = 0; i < LDS_BACK_MAX; ++i)
    {
        m_asprBack[i].Create(400, anHeight[i], BITMAP_TITLE + i, 0, NULL, 0, 0, false,
                             SPR_SIZING_DATUMS_LT, fScaleX, fScaleY);
        m_asprBack[i].Show(true);
    }

    m_asprBack[1].SetPosition(400, 0, X);
    m_asprBack[2].SetPosition(0, 512, Y);
    m_asprBack[3].SetPosition(400, 512);
}

void CLoadingScene::Release()
{
    for (int i = 0; i < LDS_BACK_MAX; ++i)
        m_asprBack[i].Release();
}

void CLoadingScene::Render()
{
    for (int i = 0; i < LDS_BACK_MAX; ++i)
    {
        m_asprBack[i].Render();
    }
}

void SessionLegacyCalls::BuildCharacterScenePickOBB(const OBJECT *object, OBB_t &outObb)
{
    sessionKeeper_.Interaction()->BuildCharacterScenePickOBB(object, outObb);
}

//extern bool EnableEdit;

void SessionLegacyCalls::SetAttackSpeed()
{
    sessionKeeper_.Gameplay()->SetAttackSpeed();
}

void SessionLegacyCalls::SetPlayerHighBowAttack(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetPlayerHighBowAttack(character);
}

void SessionLegacyCalls::SetPlayerMagic(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetPlayerMagic(character);
}

void SessionLegacyCalls::SetPlayerTeleport(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetPlayerTeleport(character);
}

void SessionLegacyCalls::MoveMonsterClient(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->MoveMonsterClient(character, object);
}

bool SessionLegacyCalls::CheckMonsterSkill(CHARACTER *character, OBJECT *object)
{
    return sessionKeeper_.Gameplay()->CheckMonsterSkill(character, object);
}

void SessionLegacyCalls::MoveCharacterClient(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->MoveCharacterClient(character);
}

void SessionLegacyCalls::MoveCharactersClient()
{
    sessionKeeper_.Gameplay()->MoveCharactersClient();
}

void SessionLegacyCalls::UpdateCharactersAnimationParallel(std::span<CHARACTER *const> characters)
{
    sessionKeeper_.Gameplay()->UpdateCharactersAnimationParallel(characters);
}

void SessionLegacyCalls::MoveCharacter(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->MoveCharacter(character, object);
}

void SessionLegacyCalls::AttackEffect(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->AttackEffect(character);
}
void SessionLegacyCalls::FallingCharacter(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->FallingCharacter(character, object);
}
void SessionLegacyCalls::PushingCharacter(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->PushingCharacter(character, object);
}
void SessionLegacyCalls::DeadCharacter(CHARACTER *character, OBJECT *object, BMD *model)
{
    sessionKeeper_.Gameplay()->DeadCharacter(character, object, model);
}
void SessionLegacyCalls::HeroAttributeCalc(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->HeroAttributeCalc(character);
}
void SessionLegacyCalls::AnimationCharacter(CHARACTER *character, OBJECT *object, BMD *model)
{
    sessionKeeper_.Gameplay()->AnimationCharacter(character, object, model);
}
void SessionLegacyCalls::CreateWeaponBlur(CHARACTER *character, OBJECT *object, BMD *model)
{
    sessionKeeper_.Gameplay()->CreateWeaponBlur(character, object, model);
}
void SessionLegacyCalls::SetPlayerStop(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetPlayerStop(character);
}
bool SessionLegacyCalls::AttackStage(CHARACTER *character, OBJECT *object)
{
    return sessionKeeper_.Gameplay()->AttackStage(character, object);
}
void SessionLegacyCalls::OnlyNpcChatProcess(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->OnlyNpcChatProcess(character, object);
}
void SessionLegacyCalls::PlayerNpcStopAnimationSetting(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->PlayerNpcStopAnimationSetting(character, object);
}
void SessionLegacyCalls::PlayerStopAnimationSetting(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->PlayerStopAnimationSetting(character, object);
}
void SessionLegacyCalls::PlayWalkSound()
{
    sessionKeeper_.Gameplay()->PlayWalkSound();
}
bool SessionLegacyCalls::CheckFullSet(CHARACTER *character)
{
    return sessionKeeper_.Gameplay()->CheckFullSet(character);
}
float SessionLegacyCalls::CharacterMoveSpeed(CHARACTER *character)
{
    return sessionKeeper_.Gameplay()->CharacterMoveSpeed(character);
}
void SessionLegacyCalls::MoveCharacterPosition(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->MoveCharacterPosition(character);
}

void SessionLegacyCalls::SetChangeClass(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetChangeClass(character);
}

bool SessionLegacyCalls::CharacterAnimation(CHARACTER *character, OBJECT *object)
{
    return sessionKeeper_.Gameplay()->CharacterAnimation(character, object);
}
void SessionLegacyCalls::EtcStopAnimationSetting(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->EtcStopAnimationSetting(character, object);
}
void SessionLegacyCalls::SetPlayerWalk(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetPlayerWalk(character);
}
BOOL SessionLegacyCalls::PlayMonsterSound(OBJECT *object)
{
    return sessionKeeper_.Gameplay()->PlayMonsterSound(object);
}
void SessionLegacyCalls::SetPlayerShock(CHARACTER *character, int hit)
{
    sessionKeeper_.Gameplay()->SetPlayerShock(character, hit);
}
void SessionLegacyCalls::SetPlayerDie(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetPlayerDie(character);
}

int SessionLegacyCalls::HangerBloodCastleQuestItem(int key)
{
    return sessionKeeper_.Gameplay()->HangerBloodCastleQuestItem(key);
}

void SessionLegacyCalls::SetAllAction(int action)
{
    sessionKeeper_.Gameplay()->SetAllAction(action);
}

bool SessionLegacyCalls::IsBackItem(const CHARACTER *character, int type)
{
    return sessionKeeper_.Visual()->IsBackItem(character, type);
}

void SessionLegacyCalls::MonsterMoveSandSmoke(OBJECT *object)
{
    sessionKeeper_.Gameplay()->MonsterMoveSandSmoke(object);
}
BOOL SessionLegacyCalls::PlayMonsterSoundGlobal(OBJECT *object)
{
    return sessionKeeper_.Gameplay()->PlayMonsterSoundGlobal(object);
}

void SessionLegacyCalls::AdvanceCharacterEnvironmentState(CHARACTER &character)
{
    sessionKeeper_.Gameplay()->AdvanceCharacterEnvironmentState(character);
}

bool SessionLegacyCalls::CharacterVisibleToObserver(const CHARACTER &character)
{
    return sessionKeeper_.Gameplay()->CharacterVisibleToObserver(character);
}
bool SessionVisualUnit::UpdateSceneVisualAnimation()
{
    if (!SceneTransitionDetail::IsActivePresentationScene(SceneFlag))
    {
        return true;
    }

    UpdateWaterAnimation();
    return !Destroy;
}

bool SessionVisualUnit::UpdateSceneRenderScratch()
{
    if (!SceneTransitionDetail::IsActivePresentationScene(SceneFlag))
    {
        return true;
    }

    ManageRenderScratch();
    return true;
}
void SessionLegacyCalls::UpdateWaterAnimation()
{
    sessionKeeper_.Visual()->UpdateWaterAnimation();
}

void SessionVisualUnit::ManageRenderScratch()
{
    Bitmaps.Manage(g_timer2StartTickTime, g_ConsoleDebug);
}
void SessionLegacyCalls::ClearActionObject()
{
    sessionKeeper_.Gameplay()->ClearActionObject();
}

OBJECT *SessionLegacyCalls::CollisionDetectObjects(OBJECT *pickObject)
{
    return sessionKeeper_.Interaction()->CollisionDetectObjects(pickObject);
}

vec34_t *SessionLegacyCalls::AllocateDrawPose(int boneCount)
{
    return sessionKeeper_.Renderer()->drawPoses_.Allocate(boneCount);
}

void SessionLegacyCalls::MoveObject(OBJECT *object)
{
    sessionKeeper_.Visual()->MoveObject(object);
}
void SessionLegacyCalls::MoveObjects()
{
    sessionKeeper_.Visual()->MoveObjects();
}

bool SessionLegacyCalls::Calc_RenderObject(ObjectDrawInput &object, bool translate, int select,
                                           int extraMonster)
{
    return sessionKeeper_.Renderer()->Calc_RenderObject(object, translate, select, extraMonster);
}
bool SessionLegacyCalls::Calc_ObjectAnimation(ObjectDrawInput &object, bool translate, int select)
{
    return sessionKeeper_.Renderer()->Calc_ObjectAnimation(object, translate, select);
}

OBJECT *SessionLegacyCalls::CreateObject(int type, vec3_t position, vec3_t angle, float scale)
{
    return sessionKeeper_.Gameplay()->CreateObject(type, position, angle, scale);
}

void SessionLegacyCalls::CreateShadowAngle()
{
    sessionKeeper_.Renderer()->CreateShadowAngle();
}

void SessionLegacyCalls::AdvanceObjectVisual(OBJECT *o)
{
    return sessionKeeper_.Visual()->AdvanceObjectVisual(o);
} // OMF-00605
void SessionLegacyCalls::PrepareWorldObjectPose(OBJECT &object, float fraction)
{
    sessionKeeper_.Visual()->PrepareWorldObjectPose(object, fraction);
}
