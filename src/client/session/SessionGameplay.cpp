#include "session/SessionGameplay.h"
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
#include "domain/Automation.h"
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
#include "domain/Shop.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "I18N/All.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "render/Character.h"
#include "render/FrameTape.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionAudio.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionRuntime.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Dialogs/DialogsRender.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Shell/ShellLogic.h"
#include "ui/features/Social/SocialRender.h"
#include "ui/features/World/WorldLogic.h" // rozy
#include "ui/features/World/WorldRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"

#ifdef _WIN32
#include <eh.h>
#endif

BuffStateSystem &BuffStateSystem::TheBuffStateSystem()
{
    return *this;
}

BuffScriptLoader &BuffStateSystem::TheBuffInfo()
{
    return TheBuffStateSystem().GetBuffInfo();
}

BuffTimeControl &BuffStateSystem::TheBuffTimeControl()
{
    return TheBuffStateSystem().GetBuffTimeControl();
}

BuffStateValueControl &BuffStateSystem::TheBuffStateValueControl()
{
    return TheBuffStateSystem().GetBuffStateValueControl();
}

BuffStateSystem &SessionLegacyCalls::TheBuffStateSystem()
{
    return sessionKeeper_.BuffStateSystemObject().TheBuffStateSystem();
} // OMF-00187
BuffScriptLoader &SessionLegacyCalls::TheBuffInfo()
{
    return sessionKeeper_.BuffStateSystemObject().TheBuffInfo();
} // OMF-00188
BuffTimeControl &SessionLegacyCalls::TheBuffTimeControl()
{
    return sessionKeeper_.BuffStateSystemObject().TheBuffTimeControl();
} // OMF-00189
BuffStateValueControl &SessionLegacyCalls::TheBuffStateValueControl()
{
    return sessionKeeper_.BuffStateSystemObject().TheBuffStateValueControl();
} // OMF-00190

namespace
{
OBB_t BuildObjectPickOBB(const OBJECT &object)
{
    OBB_t bounds{};
    VectorAdd(object.Position, object.BoundingBoxMin, bounds.StartPos);
    bounds.XAxis[0] = object.BoundingBoxMax[0] - object.BoundingBoxMin[0];
    bounds.YAxis[1] = object.BoundingBoxMax[1] - object.BoundingBoxMin[1];
    bounds.ZAxis[2] = object.BoundingBoxMax[2] - object.BoundingBoxMin[2];
    return bounds;
}
} // namespace

int SessionInteractionUnit::SelectItem()
{
    for (int i = 0; i < MAX_ITEMS; i++)
    {
        OBJECT *o = &Items[i].Object;
        if (o->Live && o->Visible)
        {
            o->LightEnable = true;
            Vector(0.2f, 0.2f, 0.2f, o->Light);
        }
    }
    float Luminosity = 1.5f;

    for (int i = 0; i < MAX_ITEMS; i++)
    {
        OBJECT *o = &Items[i].Object;
        if (o->Live)
        {
            if (CollisionDetectLineToOBB(MousePosition, MouseTarget, BuildObjectPickOBB(*o)))
            {
                {
                    o->LightEnable = false;
                    Vector(Luminosity, Luminosity, Luminosity, o->Light);
                    return i;
                }
            }
        }
    }
    return -1;
}

void SessionInteractionUnit::BuildCharacterPickOBB(const CHARACTER &character, OBB_t &bounds)
{
    const auto &object = character.Object;
    if (SceneFlag == CHARACTER_SCENE)
    {
        BuildCharacterScenePickOBB(&object, bounds);
        return;
    }
    bounds = BuildObjectPickOBB(object);
    switch (object.Type)
    {
    case MODEL_SMELTING_NPC:
        bounds.StartPos[2] += 300.f;
        break;
    case MODEL_MAYA_HAND_LEFT:
    case MODEL_MAYA_HAND_RIGHT:
        bounds.StartPos[2] += 200.f;
        break;
    case MODEL_KANTURU2ND_ENTER_NPC:
        bounds.StartPos[0] -= 100.f;
        bounds.StartPos[2] += 100.f;
        bounds.XAxis[0] += 100.f;
        bounds.ZAxis[2] += 100.f;
        break;
    }
}

void SessionInteractionUnit::ResetCharacterPickLighting(BYTE Kind)
{
    for (int i = 0; i < CharactersClient.Size(); i++)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        OBJECT *o = &c->Object;
        if ((Kind & o->Kind) == o->Kind && o->Live && CharactersClient.IsVisible(i) &&
            o->Alpha > 0.f)
        {
            o->LightEnable = true;
            switch (c->Level)
            {
            case 0:
                Vector(0.2f, 0.2f, 0.2f, o->Light);
                break;
            case 1:
                Vector(-0.4f, -0.4f, -0.4f, o->Light);
                break;
            case 2:
                Vector(0.2f, -0.6f, -0.6f, o->Light);
                break;
            case 3:
                Vector(1.5f, 1.5f, 1.5f, o->Light);
                break;
            case 4:
                Vector(0.3f, 0.2f, -0.5f, o->Light);
                break;
            }
            if (c->PK >= PVP_MURDERER2)
            {
                Vector(-0.4f, -0.4f, -0.4f, o->Light);
            }
        }
    }
}

int SessionInteractionUnit::SelectCharacter(BYTE Kind)
{
    bool Main = true;
    if (SceneFlag == CHARACTER_SCENE)
        Main = false;

    ResetCharacterPickLighting(Kind);
    vec3_t Light;
    Vector(0.8f, 0.8f, 0.8f, Light);
    int iSelected = -1;
    float fNearestDist = 1000000000000.0f;

    for (int i = 0; i < CharactersClient.Size(); i++)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        OBJECT *o = &c->Object;

        if (CharacterVisibleToObserver(*c) && o->Alpha > 0.f && c->Dead == 0 &&
            !g_isCharacterBuff(o, eBuff_CrywolfNPCHide))
        {
            if (o->Kind == KIND_PLAYER) //. bug fixed by soyaviper
            {
                for (int j = 0; j < PartyNumber; ++j)
                {
                    PARTY_t *p = &Party[j];

                    if (p->index != -2)
                        continue;
                    if (p->index > -1)
                        continue;

                    int length = std::max<int>(wcslen(p->Name), std::max<int>(1, wcslen(c->ID)));

                    if (!wcsncmp(p->Name, c->ID, length))
                    {
                        p->index = i;
                        break;
                    }
                }
            }

            if (Main && c == Hero)
            {
                continue;
            }

            if (c->m_bIsSelected == false)
            {
                continue;
            }

            if ((Kind & o->Kind) == o->Kind)
            {
                OBB_t pickOBB;
                BuildCharacterPickOBB(*c, pickOBB);

                if (CollisionDetectLineToOBB(MousePosition, MouseTarget, pickOBB))
                {
                    vec3_t vSub;
                    VectorSubtract(o->Position, g_Camera.Position, vSub);

                    float fNewDist = DotProduct(vSub, vSub);

                    if (fNewDist < fNearestDist)
                    {
                        BOOL bCanTalk = TRUE;
                        if (gMapManager.ContextMap() == WD_0LORENCIA ||
                            gMapManager.ContextMap() == WD_2DEVIAS)
                        {
                            int Index = ((int)o->Position[1] / (int)TERRAIN_SCALE) * 256 +
                                        ((int)o->Position[0] / (int)TERRAIN_SCALE);
                            if ((gMapManager.ContextMap() == WD_0LORENCIA &&
                                 TerrainMappingLayer1[Index] == 4) ||
                                (gMapManager.ContextMap() == WD_2DEVIAS &&
                                 TerrainMappingLayer1[Index] == 3))
                            {
                                if (TerrainMappingLayer1[Index] != HeroTile &&
                                    (gMapManager.ContextMap() == WD_2DEVIAS && HeroTile != 11))
                                    bCanTalk = FALSE;
                            }
                        }
                        if (bCanTalk == TRUE)
                        {
                            iSelected = i;
                            fNearestDist = fNewDist;
                        }
                    }
                }
            }
        }
    }

    for (int j = 0; j < PartyNumber; ++j)
    {
        PARTY_t *p = &Party[j];

        if (p->index >= 0)
            continue;

        int length = std::max<int>(wcslen(p->Name), std::max<int>(1, wcslen(Hero->ID)));

        if (!wcsncmp(p->Name, Hero->ID, length))
        {
            p->index = -3;
        }
        else
        {
            p->index = -1;
        }
    }

    return iSelected;
}

int SessionInteractionUnit::SelectOperate()
{
    for (int i = 0; i < MAX_OPERATES; i++)
    {
        OPERATE *n = &Operates[i];
        OBJECT *o = n->Owner;
        if (n->Live && o->Visible && o->HiddenMesh == -1)
        {
            float *Light = &o->Light[0];
            Vector(0.2f, 0.2f, 0.2f, Light);
        }
    }
    if (IsBattleCastleStart() && gMapManager.ContextMap() == WD_30BATTLECASTLE)
        return -1;

    for (int i = 0; i < MAX_OPERATES; i++)
    {
        OPERATE *n = &Operates[i];
        OBJECT *o = n->Owner;
        if (n->Live)
        {
            float *Light = &o->Light[0];
            if (CollisionDetectLineToOBB(MousePosition, MouseTarget, BuildObjectPickOBB(*o)))
            {
                Vector(1.5f, 1.5f, 1.5f, Light);
                return i;
            }
        }
    }
    return -1;
}

void SessionInteractionUnit::ResetAutoAttackSelection()
{
    if (g_pOption->IsAutoAttack() && gMapManager.ContextMap() != WD_6STADIUM &&
        gMapManager.InChaosCastle() == false)
    {
        if (!CharactersClient.IsValidIndex(SelectedCharacter))
        {
            SelectedCharacter = -1;
            Attacking = -1;
        }
        else
        {
            CHARACTER *sc = &CharactersClient[SelectedCharacter];

            if (sc->Dead > 0 || sc->Object.Kind != KIND_MONSTER)
            {
                SelectedCharacter = -1;
                Attacking = -1;
            }

            if (Attacking != -1)
            {
                if (MouseLButton || MouseLButtonPush || MouseRButton || MouseRButtonPush ||
                    Hero->Dead > 0)
                {
                    SelectedCharacter = -1;
                }
            }
            else
            {
                SelectedCharacter = -1;
            }
        }
    }
    else
    {
        SelectedCharacter = -1;
        Attacking = -1;
    }

    SelectedItem = -1;
    SelectedNpc = -1;
    SelectedOperate = -1;
}

void SessionInteractionUnit::SelectObjects()
{
    BYTE CKind_1, CKind_2;

    ResetAutoAttackSelection();

    if (!MouseOnWindow && false == g_pNewUISystem->CheckMouseUse() &&
        CheckMouseIn(0, 0, GetScreenWidth(), 429))
    {
        if (IsKeyDown(VK_MENU))
        {
            if (g_pMyInventory->GetInventoryCtrl()->GetPickedItem() == NULL)
                SelectedItem = SelectItem();

            if (SelectedItem == -1)
            {
                SelectedNpc = SelectCharacter(KIND_NPC);
                if (SelectedNpc == -1)
                {
                    SelectedCharacter = SelectCharacter(KIND_MONSTER | KIND_EDIT);
                    if (SelectedCharacter == -1)
                    {
                        SelectedCharacter = SelectCharacter(KIND_PLAYER);
                        if (SelectedCharacter == -1)
                        {
                            SelectedOperate = SelectOperate();
                        }
                    }
                }
            }
            else
            {
                g_pPartyManager->SearchPartyMember();
            }
        }
        else
        {
            CKind_1 = KIND_MONSTER | KIND_EDIT;
            CKind_2 = KIND_PLAYER;

            if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_ELF ||
                gCharacterManager.GetBaseClass(Hero->Class) == CLASS_WIZARD)
            {
                auto Skill = CharacterAttribute->Skill[Hero->CurrentSkill];

                if (Skill == AT_SKILL_HEALING || Skill == AT_SKILL_HEALING_STR ||
                    Skill == AT_SKILL_DEFENSE || Skill == AT_SKILL_DEFENSE_STR ||
                    Skill == AT_SKILL_DEFENSE_MASTERY || Skill == AT_SKILL_ATTACK ||
                    Skill == AT_SKILL_ATTACK_STR || Skill == AT_SKILL_ATTACK_MASTERY ||
                    Skill == AT_SKILL_TELEPORT_ALLY || Skill == AT_SKILL_SOUL_BARRIER ||
                    Skill == AT_SKILL_SOUL_BARRIER_STR ||
                    Skill == AT_SKILL_SOUL_BARRIER_PROFICIENCY)
                {
                    CKind_1 = KIND_PLAYER;
                    CKind_2 = KIND_MONSTER | KIND_EDIT;
                }
            }

            if (g_pPartyListWindow && g_pPartyListWindow->GetSelectedCharacter() != -1)
            {
                g_pPartyManager->SearchPartyMember();
            }
            else
            {
                if (SelectedCharacter == -1)
                {
                    SelectedCharacter = SelectCharacter(CKind_1);
                }
                if (SelectedCharacter == -1)
                {
                    SelectedCharacter = SelectCharacter(CKind_2);
                    if (SelectedCharacter == -1)
                    {
                        SelectedNpc = SelectCharacter(KIND_NPC);
                        if (SelectedNpc == -1)
                        {
                            if (g_pMyInventory->GetInventoryCtrl()->GetPickedItem() == NULL)
                            {
                                SelectedItem = SelectItem();
                            }
                            if (SelectedItem == -1)
                            {
                                SelectedOperate = SelectOperate();
                            }
                        }
                    }
                }
                else if (Attacking != -1)
                {
                    g_pPartyManager->SearchPartyMember();
                }
            }
        }
    }
    else
    {
        g_pPartyManager->SearchPartyMember();
    }

    if (SelectedCharacter == -1)
    {
        Attacking = -1;
    }

    if (g_pPartyListWindow)
    {
        g_pPartyListWindow->SetListBGColor();
    }
}

int SessionLegacyCalls::SelectItem()
{
    return sessionKeeper_.Interaction()->SelectItem();
}

int SessionLegacyCalls::SelectCharacter(BYTE kind)
{
    return sessionKeeper_.Interaction()->SelectCharacter(kind);
}

int SessionLegacyCalls::SelectOperate()
{
    return sessionKeeper_.Interaction()->SelectOperate();
}

void SessionLegacyCalls::SelectObjects()
{
    sessionKeeper_.Interaction()->SelectObjects();
}

SessionClock::SessionClock(SessionKeeper &keeper, SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), observer_(observer)
{
    (void)sessionKeeper_.RegisterClock(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::ClockConstructed);
    }
}

SessionClock::~SessionClock()
{
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::ClockDestroyed);
    }
}

void SessionClock::Advance() noexcept
{
    ++frameNumber_;
}

std::uint64_t SessionClock::FrameNumber() const noexcept
{
    return frameNumber_;
}

namespace
{
constexpr std::uint64_t RandomMultiplier = 6364136223846793005ULL;
constexpr std::uint64_t RandomIncrement = 1442695040888963407ULL;
} // namespace

SessionRandom::SessionRandom(SessionKeeper &keeper, std::uint64_t seed,
                             SessionLifecycleObserver *observer) noexcept
    : sessionKeeper_(keeper), observer_(observer), state_(seed),
      engine_(static_cast<std::mt19937::result_type>(seed)),
      presentationEngine_(static_cast<std::mt19937::result_type>(seed)), presentationState_(seed),
      drawEngine_(static_cast<std::mt19937::result_type>(seed)), drawState_(seed)
{
    (void)sessionKeeper_.RegisterRandom(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::RandomConstructed);
    }
}

std::int32_t SessionRandom::RangeInt(std::int32_t minInclusive, std::int32_t maxInclusive)
{
    if (minInclusive >= maxInclusive)
    {
        return minInclusive;
    }

    std::uniform_int_distribution<std::int32_t> distribution(minInclusive, maxInclusive);
    return distribution(ActiveEngine());
}

float SessionRandom::RangeFloat(float minInclusive, float maxInclusive)
{
    if (minInclusive >= maxInclusive)
    {
        return minInclusive;
    }

    std::uniform_real_distribution<float> distribution(minInclusive, maxInclusive);
    return distribution(ActiveEngine());
}

float SessionRandom::Unit()
{
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    return distribution(ActiveEngine());
}

double SessionRandom::UnitDouble()
{
    std::uniform_real_distribution<double> distribution(0.0, 1.0);
    return distribution(ActiveEngine());
}

bool SessionRandom::FpsCheck(std::int32_t referenceFrames, double animationFactor)
{
    if (referenceFrames <= 0)
    {
        return false;
    }

    const double elapsedFrames = (std::max)(0.0, animationFactor);
    const double chance = referenceFrames == 1
                              ? (std::min)(1.0, elapsedFrames)
                              : -std::expm1(elapsedFrames * std::log1p(-1.0 / referenceFrames));
    return UnitDouble() <= chance;
}

int SessionRandom::EmissionCount(double animationFactor)
{
    const auto whole = static_cast<int>(animationFactor);
    const double remainder = animationFactor - whole;
    return whole + (UnitDouble() < remainder ? 1 : 0);
}

SessionRandom::~SessionRandom()
{
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::RandomDestroyed);
    }
}

std::uint64_t SessionRandom::Next() noexcept
{
    auto &state = drawing_ ? drawState_ : presentation_ ? presentationState_ : state_;
    state = state * RandomMultiplier + RandomIncrement;
    return state;
}

SessionGameDataUnit::SessionGameDataUnit(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), AbuseFilter(keeper.AbuseFilterStorage()),
      AbuseNameFilter(keeper.AbuseNameFilterStorage()),
      AbuseFilterNumber(keeper.AbuseFilterCount()),
      AbuseNameFilterNumber(keeper.AbuseNameFilterCount()), MonsterScript(keeper.MonsterScripts()),
      ClassAttribute(keeper.ClassAttributes()), EditMonsterNumber(keeper.EditMonsterCount()),
      g_strSelectedML(keeper.AssetLanguage()), gSkillManager(keeper.SkillManagerObject()),
      g_SocketItemMgr(keeper.SocketItemManager()), g_csItemOption(keeper.ItemOptionManager()),
      g_csQuest(keeper.QuestObject()), g_QuestMng(keeper.QuestManagerObject()),
      g_ErrorReport(keeper.ErrorReport()), gMapManager(keeper.MapManagerObject())
{
    (void)sessionKeeper_.RegisterGameData(*this);
    CreatePersonalItemTable();
    items_ = std::make_unique<SessionItemStore>(keeper);
    auto &grids = keeper.InventoryStorage().grids;
    for (std::size_t index = 0; index < grids.size(); ++index)
        grids[index] =
            std::make_unique<InventoryGrid>(keeper, *items_, static_cast<InventoryRole>(index));
}

SessionGameDataUnit::~SessionGameDataUnit()
{
    ClearPickedItem();
    for (auto &grid : sessionKeeper_.InventoryStorage().grids)
        grid.reset();
}

InventoryGrid &SessionGameDataUnit::Inventory(InventoryRole role) noexcept
{
    return *sessionKeeper_.InventoryStorage().grids[static_cast<std::size_t>(role)];
}

void SessionGameDataUnit::ClearInventoryContainers()
{
    CompleteItemMove(false);
    sessionKeeper_.InventoryStorage().luckyMixRequest = 0;
    sessionKeeper_.InventoryStorage().purchaseSourceSlot = -1;
    RemoveAllPerosnalItemPrice(PSHOPWNDTYPE_SALE);
    RemoveAllPerosnalItemPrice(PSHOPWNDTYPE_PURCHASE);
    for (auto &grid : sessionKeeper_.InventoryStorage().grids)
        grid->RemoveAllItems();
}

SessionAdvanceUnit::SessionAdvanceUnit(SessionKeeper &keeper,
                                       SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), observer_(observer)
{
    (void)sessionKeeper_.RegisterAdvance(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::AdvanceUnitConstructed);
    }
}

SessionAdvanceUnit::~SessionAdvanceUnit()
{
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::AdvanceUnitDestroyed);
    }
}

bool SessionAdvanceUnit::AdvanceFrame() noexcept
{
    const auto interaction = CaptureFrameInputOnOwner(true);
    return interaction.has_value() && BeginFrameWorkerSafe() && CompleteFrameOnOwner(*interaction);
}

std::optional<GameplayInteractionFact> SessionAdvanceUnit::CaptureFrameInputOnOwner(
    bool prepareVisibleScene) noexcept
{
    SessionPresentationUnit *presentation = sessionKeeper_.Presentation();
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    SessionGameDataUnit *gameData = sessionKeeper_.GameData();
    if (presentation == nullptr || gameplay == nullptr || gameData == nullptr ||
        !gameData->FlushPendingMonsterModelsOnOwner() || !presentation->BeginFrame())
    {
        return std::nullopt;
    }
    const bool prepared = gameplay->PrepareSceneSimulationOnOwner(prepareVisibleScene);
    if (!prepared || !presentation->PrepareInteraction())
        return std::nullopt;
    const GameplayInteractionFact interaction = presentation->PickForLogic();
    // A legacy local-scene initializer may return normally after its map load failed.
    return prepared && !sessionKeeper_.WorldUnit()->BlocksFrameAdmission()
               ? std::optional<GameplayInteractionFact>{interaction}
               : std::nullopt;
}

std::optional<SessionVisualAnimationInput> SessionAdvanceUnit::
    CaptureVisualAnimationInputOnOwner() noexcept
{
    SessionPresentationUnit *presentation = sessionKeeper_.Presentation();
    return presentation != nullptr ? presentation->CaptureVisualAnimationInputOnOwner()
                                   : std::nullopt;
}

std::optional<SessionPhysicsFrameInput> SessionAdvanceUnit::
    CapturePhysicsFrameInputOnOwner() noexcept
{
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    return gameplay != nullptr ? gameplay->CapturePhysicsFrameInputOnOwner() : std::nullopt;
}

bool SessionAdvanceUnit::AdvanceLogicalFrameWorkerSafe(double elapsedMilliseconds) noexcept
{
    SessionClock *clock = sessionKeeper_.Clock();
    SessionAudioBusView *audio = sessionKeeper_.AudioOutput();
    SessionLifecycleState *lifecycle = sessionKeeper_.Lifecycle();
    if (clock == nullptr || audio == nullptr || lifecycle == nullptr)
    {
        return false;
    }

    sessionKeeper_.FrameElapsedMilliseconds() = std::max(0.0, elapsedMilliseconds);
    sessionKeeper_.FrameAnimationFactor() = ApplicationFrameUnit::CalculateAnimationFactor(
        sessionKeeper_.FrameElapsedMilliseconds(),
        sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    lifecycle->BeginAdvance();
    clock->Advance();
    sessionKeeper_.WorldUnit()->AdvanceTransferTime(elapsedMilliseconds);
    audio->AdvanceLogicalFrame();
    return true;
}

bool SessionAdvanceUnit::BeginFrameWorkerSafe() noexcept
{
    const double frameMilliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    return gameplay != nullptr && AdvanceLogicalFrameWorkerSafe(frameMilliseconds) &&
           gameplay->UpdateVisibleSceneSimulationWorkerSafe(frameMilliseconds);
}

bool SessionAdvanceUnit::BeginFrameWorkerSafe(double frameDeltaMilliseconds, bool renderRequired,
                                              const SessionVisualAnimationInput &visualInput,
                                              SessionVisualAnimationResult &visualResult) noexcept
{
    SessionPresentationUnit *presentation = sessionKeeper_.Presentation();
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    return presentation != nullptr && gameplay != nullptr &&
           AdvanceLogicalFrameWorkerSafe(frameDeltaMilliseconds) &&
           presentation->TryBuildVisualAnimationResult(visualInput, visualResult) &&
           (!renderRequired ||
            gameplay->UpdateVisibleSceneSimulationWorkerSafe(frameDeltaMilliseconds));
}

bool SessionAdvanceUnit::AdvanceMainSceneEntitiesWorkerSafe(
    double frameDeltaMilliseconds, SessionOrderedEffectBatch &orderedEffects,
    std::uint64_t &nextStableSequence) noexcept
{
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    SessionNetworkUnit *network = sessionKeeper_.Network();
    if (gameplay == nullptr || network == nullptr ||
        !network->BeginGameplaySendCapture(orderedEffects, nextStableSequence))
    {
        return false;
    }
    const bool advanced = gameplay->UpdateMainSceneEntitiesWorkerSafe(frameDeltaMilliseconds);
    const bool captured = network->EndGameplaySendCapture();
    return advanced && captured;
}

bool SessionAdvanceUnit::AdvanceMainSceneTickWorkerSafe(SessionOrderedEffectBatch &orderedEffects,
                                                        std::uint64_t &nextStableSequence) noexcept
{
    auto *gameplay = sessionKeeper_.Gameplay();
    auto *network = sessionKeeper_.Network();
    if (!gameplay || !network ||
        !network->BeginGameplaySendCapture(orderedEffects, nextStableSequence))
        return false;
    const bool advanced = gameplay->AdvanceMainSceneTickWorkerSafe();
    const bool captured = network->EndGameplaySendCapture();
    return advanced && captured;
}

bool SessionAdvanceUnit::AdvanceMapObserverTickWorkerSafe(
    SessionOrderedEffectBatch &orderedEffects, std::uint64_t &nextStableSequence) noexcept
{
    auto *gameplay = sessionKeeper_.Gameplay();
    auto *network = sessionKeeper_.Network();
    if (!gameplay || !network ||
        !network->BeginGameplaySendCapture(orderedEffects, nextStableSequence))
        return false;
    const bool advanced = gameplay->AdvanceMapObserverTickWorkerSafe();
    const bool captured = network->EndGameplaySendCapture();
    return advanced && captured;
}

bool SessionAdvanceUnit::CompleteFrameOnOwner() noexcept
{
    const auto interaction = CaptureFrameInputOnOwner(true);
    return interaction.has_value() && CompleteFrameOnOwner(*interaction);
}

bool SessionAdvanceUnit::CompleteFrameOnOwner(const GameplayInteractionFact &interaction) noexcept
{
    SessionLifecycleState *lifecycle = sessionKeeper_.Lifecycle();
    SessionPresentationUnit *presentation = sessionKeeper_.Presentation();
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    if (lifecycle == nullptr || presentation == nullptr || gameplay == nullptr)
    {
        return false;
    }
    const auto physicsInput = gameplay->CapturePhysicsFrameInputOnOwner();
    if (!physicsInput.has_value() || !gameplay->CompleteSceneUpdateOnOwner(interaction) ||
        !presentation->FinishFrame() || !presentation->UpdateVisualAnimation() ||
        !gameplay->UpdateSystems(*physicsInput) || !presentation->UpdateRenderScratch() ||
        !presentation->UpdateSpatialAudio())
    {
        return false;
    }

    lifecycle->MarkAdvanced();
    return true;
}

bool SessionAdvanceUnit::CompleteFrameOnOwner(
    const GameplayInteractionFact &interaction, SessionOrderedEffectBatch &orderedEffects,
    const SessionVisualAnimationResult &visualResult) noexcept
{
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    if (gameplay == nullptr)
    {
        return false;
    }
    const auto physicsInput = gameplay->CapturePhysicsFrameInputOnOwner();
    std::uint64_t nextStableSequence = 1;
    return physicsInput.has_value() &&
           BeginOwnerCompletionBeforeSystems(interaction, orderedEffects, visualResult,
                                             nextStableSequence) &&
           UpdateSystemsWorkerSafe(*physicsInput) &&
           EndOwnerCompletionAfterSystems(orderedEffects, nextStableSequence);
}

bool SessionAdvanceUnit::CompleteFrameOnOwner(const GameplayInteractionFact &interaction,
                                              SessionOrderedEffectBatch &orderedEffects) noexcept
{
    SessionPresentationUnit *presentation = sessionKeeper_.Presentation();
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    SessionNetworkUnit *network = sessionKeeper_.Network();
    if (presentation == nullptr || gameplay == nullptr || network == nullptr)
    {
        return false;
    }

    std::uint64_t nextStableSequence = 1;
    if (!network->BeginGameplaySendCapture(orderedEffects, nextStableSequence))
    {
        return false;
    }
    const auto physicsInput = gameplay->CapturePhysicsFrameInputOnOwner();
    const bool ownerStageSucceeded =
        physicsInput.has_value() && gameplay->CompleteSceneUpdateOnOwner(interaction) &&
        presentation->FinishFrame() && presentation->UpdateVisualAnimation() &&
        gameplay->UpdateSystems(*physicsInput) && presentation->UpdateRenderScratch() &&
        presentation->BuildSpatialAudioEffect(orderedEffects, nextStableSequence++);
    const bool captureSucceeded = network->EndGameplaySendCapture();
    if (!ownerStageSucceeded || !captureSucceeded)
    {
        return false;
    }

    SessionLifecycleState *lifecycle = sessionKeeper_.Lifecycle();
    if (lifecycle == nullptr)
    {
        return false;
    }
    lifecycle->MarkAdvanced();
    return true;
}

bool SessionAdvanceUnit::BeginOwnerCompletionBeforeSystems(
    const GameplayInteractionFact &interaction, SessionOrderedEffectBatch &orderedEffects,
    const SessionVisualAnimationResult &visualResult, std::uint64_t &nextStableSequence) noexcept
{
    SessionPresentationUnit *presentation = sessionKeeper_.Presentation();
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    SessionNetworkUnit *network = sessionKeeper_.Network();
    if (presentation == nullptr || gameplay == nullptr || network == nullptr ||
        !network->BeginGameplaySendCapture(orderedEffects, nextStableSequence))
    {
        return false;
    }
    const bool ownerStageSucceeded = gameplay->CompleteSceneUpdateOnOwner(interaction) &&
                                     presentation->FinishFrame() &&
                                     presentation->ApplyVisualAnimationResultOnOwner(visualResult);
    const bool captureSucceeded = network->EndGameplaySendCapture();
    return ownerStageSucceeded && captureSucceeded;
}

bool SessionAdvanceUnit::UpdateSystemsWorkerSafe(const SessionPhysicsFrameInput &input) noexcept
{
    SessionGameplayUnit *gameplay = sessionKeeper_.Gameplay();
    return gameplay != nullptr && gameplay->UpdateSystems(input);
}

bool SessionAdvanceUnit::EndOwnerCompletionAfterSystems(SessionOrderedEffectBatch &orderedEffects,
                                                        std::uint64_t &nextStableSequence) noexcept
{
    SessionLifecycleState *lifecycle = sessionKeeper_.Lifecycle();
    SessionPresentationUnit *presentation = sessionKeeper_.Presentation();
    SessionNetworkUnit *network = sessionKeeper_.Network();
    if (lifecycle == nullptr || presentation == nullptr || network == nullptr ||
        !network->BeginGameplaySendCapture(orderedEffects, nextStableSequence))
    {
        return false;
    }
    const bool ownerStageSucceeded =
        presentation->UpdateRenderScratch() &&
        presentation->BuildSpatialAudioEffect(orderedEffects, nextStableSequence++);
    const bool captureSucceeded = network->EndGameplaySendCapture();
    if (!ownerStageSucceeded || !captureSucceeded)
    {
        return false;
    }
    lifecycle->MarkAdvanced();
    return true;
}

CUIMng &SessionGameplayUnit::LegacyUiManager()
{
    return sessionKeeper_.Ui()->LegacyUiManager();
}

void SessionGameplayUnit::UpdateResolutionDependentSystems()
{
    const float aspectRatio = static_cast<float>(WindowWidth) / static_cast<float>(WindowHeight);
    cameraProjection_.SetupPerspective(g_Camera, g_Camera.FOV, aspectRatio, g_Camera.ViewNear,
                                       g_Camera.ViewFar * RENDER_DISTANCE_MULTIPLIER);
}

SessionGameplayUnit::SessionGameplayUnit(SessionKeeper &keeper,
                                         SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), Random(keeper.RandomForConstruction()),
      effectMoveBehavior_(keeper), Effects(keeper.EffectsStorage()),
      Particles(keeper.ParticlesStorage()), Joints(keeper.JointsStorage()),
      Points(keeper.PointsStorage()), Pointers(keeper.PointersStorage()),
      g_blurs(keeper.BlurStorage().blurs), g_objectBlurs(keeper.BlurStorage().objectBlurs),
      Sprites(keeper.SpritesStorage()), Leaves(keeper.LeafStorage().Leaves),
      g_SkillEffects(keeper.SkillEffectManagerObject()), g_csMatchInfo(keeper.EventMatch()),
      g_SocketItemMgr(keeper.SocketItemManager()), g_csItemOption(keeper.ItemOptionManager()),
      g_csQuest(keeper.QuestObject()), g_QuestMng(keeper.QuestManagerObject()),
      CharacterMachine(&keeper.CharacterMachineObject()),
      CharacterAttribute(&keeper.CharacterMachineObject().Character),
      Mounts(keeper.MountsStorage()), Boids(keeper.BoidsStorage()), Fishs(keeper.FishsStorage()),
      Operates(keeper.OperatesStorage()), Items(keeper.ItemsStorage()),
      g_byLastSkillSerialNumber(keeper.LastSkillSerialNumber()), ObjectBlock(keeper.ObjectBlocks()),
      MacroText(keeper.MacroTexts()), CollisionPosition(keeper.CollisionPosition()),
      SelectXF(keeper.TerrainSelectX()), SelectYF(keeper.TerrainSelectY()),
      g_wtMatchResult(keeper.WelfareTempleStorage().g_wtMatchResult),
      g_wtMatchTimeLeft(keeper.WelfareTempleStorage().g_wtMatchTimeLeft),
      g_iGoalEffect(keeper.GoalEffect()), gMapManager(keeper.MapManagerObject()),
      cameraMove_(keeper.CameraMoveObject()), g_loginCamera(keeper.LoginCameraStateObject()),
      g_Direction(keeper.DirectionObject()), gSkillManager(keeper.SkillManagerObject()),
      g_SummonSystem(keeper.SummonSystemObject()), g_CMonkSystem(keeper.MonkSystemObject()),
      g_MessageBox(keeper.MessageBoxManagerObject()), g_petProcess(keeper.PetProcessObject()),
      g_pFriendMenu(keeper.FriendMenuForConstruction()),
      g_pSinglePasswdInputBox(keeper.SinglePasswordInputBox()),
      g_dwKeyFocusUIID(keeper.KeyFocusUiId()), cursedTemple_(keeper.CursedTempleObject()),
      thirdChange_(keeper.ThirdChangeObject()), cameraProjection_(keeper), cameraManager_(keeper),
      xmasEvent_(keeper), newYearsDayEvent_(keeper), summerEvent_(keeper),
      g_MapProcess(MapProcess::Make(keeper)), RepresentativeScalar(keeper.RepresentativeScalar()),
      RepresentativeFixedArray(keeper.RepresentativeFixedArray()),
      RepresentativeMatrix(keeper.RepresentativeMatrix()),
      RepresentativePointer(keeper.RepresentativePointer()),
      RepresentativeContainer(keeper.RepresentativeContainer()),
      RepresentativeCallback(keeper.RepresentativeCallback()),
      RepresentativeAtomic(keeper.RepresentativeAtomic()), GateAttribute(keeper.GateAttributes()),
      MonsterScript(keeper.MonsterScripts()), MonsterSkill(keeper.MonsterSkills()),
      g_ErrorReport(keeper.ErrorReport()), boneManager_(keeper.BoneManagerObject()),
      g_ConsoleDebug(keeper.ConsoleDebug()), g_hWnd(keeper.PlatformWindowHandle()),
      g_Camera(keeper.CameraStateObject()), FPS_ANIMATION_FACTOR(keeper.FrameAnimationFactor()),
      WorldTime(keeper.FrameWorldTime()), g_timer2StartTickTime(keeper.FrameTimer2StartTickTime()),
      g_bUseWindowMode(keeper.PlatformWindowMode()), WindowWidth(keeper.PlatformWindowWidth()),
      WindowHeight(keeper.PlatformWindowHeight()), Destroy(keeper.PlatformDestroyRequested()),
      g_iActionObjectType(keeper.ActionObjectType()), g_iActionWorld(keeper.ActionWorld()),
      g_iActionTime(keeper.ActionTime()), g_fActionObjectVelocity(keeper.ActionObjectVelocity()),
      observer_(observer)
{
    (void)sessionKeeper_.RegisterGameplay(*this);
    g_MapProcess->Initialize();
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::GameplayUnitConstructed);
    }
}

SessionGameplayUnit::~SessionGameplayUnit()
{
    DeleteEventMatch();
    gMapManager.DeleteObjects();
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::GameplayUnitDestroyed);
    }
}

void SessionLegacyCalls::SendMove(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->SendMove(character, object);
}

SessionNetworkUnit *SessionGameplayUnit::Network() const noexcept
{
    return sessionKeeper_.Network();
}

bool SessionGameplayUnit::CompleteSceneUpdateOnOwner(
    const GameplayInteractionFact &interaction) noexcept
{
    SelectedCharacter = interaction.selectedCharacter;
    SelectedNpc = interaction.selectedNpc;
    SelectedItem = interaction.selectedItem;
    SelectedOperate = interaction.selectedOperate;
    SelectFlag = interaction.terrainPick.hit;
    if (SelectFlag)
    {
        SelectXF = interaction.terrainPick.tileX;
        SelectYF = interaction.terrainPick.tileY;
        Vector(interaction.terrainPick.worldX, interaction.terrainPick.worldY,
               interaction.terrainPick.worldZ, CollisionPosition);
    }
    return UpdateSceneGameplayOnOwner();
}

std::optional<SessionPhysicsFrameInput> SessionGameplayUnit::CapturePhysicsFrameInputOnOwner()
    const noexcept
{
    return SessionPhysicsFrameInput::TryCreate(FPS_ANIMATION_FACTOR, WorldTime);
}

bool SessionGameplayUnit::UpdateSystems(const SessionPhysicsFrameInput &input) noexcept
{
    return UpdateSceneGameplaySystems(input);
}

bool SessionGameplayUnit::Apply(const GameplayExternalEvent &event) noexcept
{
    switch (event.kind)
    {
    case GameplayExternalEventKind::ResetInteraction:
        SelectedItem = -1;
        SelectedNpc = -1;
        SelectedCharacter = -1;
        SelectedOperate = -1;
        Attacking = -1;
        return true;
    case GameplayExternalEventKind::ClearFollow:
        g_iFollowCharacter = -1;
        return true;
    case GameplayExternalEventKind::StartArrivalCooldown:
        sessionKeeper_.WorldUnit()->StartArrivalCooldown(event.cooldownMilliseconds);
        MouseUpdateTime = 0;
        MouseUpdateTimeMax = static_cast<int>(event.cadenceMaximum);
        return true;
    case GameplayExternalEventKind::CancelWorldTransfer:
        sessionKeeper_.WorldUnit()->CancelTransfer();
        return true;
    case GameplayExternalEventKind::ResetCadence:
        MouseUpdateTime = 0;
        MouseUpdateTimeMax = static_cast<int>(event.cadenceMaximum);
        return true;
    case GameplayExternalEventKind::ClearAttack:
        Attacking = -1;
        return true;
    }
    return false;
}

void SessionLegacyCalls::MoveBat(OBJECT *object)
{
    sessionKeeper_.Gameplay()->MoveBat(object, sessionKeeper_.Gameplay()->FPS_ANIMATION_FACTOR);
}
float SessionLegacyCalls::MoveHumming(vec3_t position, vec3_t angle, vec3_t targetPosition,
                                      float turn)
{
    return sessionKeeper_.Gameplay()->MoveHumming(position, angle, targetPosition, turn);
}
void SessionLegacyCalls::MovePosition(vec3_t position, vec3_t angle, vec3_t speed)
{
    sessionKeeper_.Gameplay()->MovePosition(position, angle, speed);
}
void SessionLegacyCalls::MoveBoid(OBJECT *object, int index, OBJECT *boids, int maximum)
{
    sessionKeeper_.Gameplay()->MoveBoid(object, index, boids, maximum,
                                        sessionKeeper_.Gameplay()->FPS_ANIMATION_FACTOR);
}
bool SessionLegacyCalls::rand_fps_check(int referenceFrames)
{
    return sessionKeeper_.Gameplay()->rand_fps_check(referenceFrames);
}
void SessionLegacyCalls::SetPlayerAttack(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetPlayerAttack(character);
}

namespace
{
std::optional<CharacterId> CharacterIdentity(
    const SessionCharacterPopulationStorage &charactersClient, int index) noexcept
{
    if (!charactersClient.IsValidIndex(index))
    {
        return std::nullopt;
    }

    const SHORT key = charactersClient[index].Key;
    return key >= 0 ? CharacterId::TryCreate(static_cast<std::uint64_t>(key) + 1) : std::nullopt;
}

std::optional<EntityId> NpcIdentity(const SessionCharacterPopulationStorage &charactersClient,
                                    int index) noexcept
{
    if (!charactersClient.IsValidIndex(index))
    {
        return std::nullopt;
    }

    const SHORT key = charactersClient[index].Key;
    return key >= 0 ? EntityId::TryCreate(static_cast<std::uint64_t>(key) + 1) : std::nullopt;
}
} // namespace

SessionInteractionUnit::SessionInteractionUnit(SessionKeeper &keeper,
                                               SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), gMapManager(keeper.MapManagerObject()),
      g_Camera(keeper.CameraStateObject()), Operates(keeper.OperatesStorage()),
      Items(keeper.ItemsStorage()), observer_(observer)
{
    (void)sessionKeeper_.RegisterInteraction(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::InteractionUnitConstructed);
    }
}

SessionInteractionUnit::~SessionInteractionUnit()
{
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::InteractionUnitDestroyed);
    }
}

GameplayInteractionFact SessionInteractionUnit::PickForLogic() noexcept
{
    GameplayInteractionFact fact;
    fact.selectedCharacter = SelectedCharacter;
    fact.selectedNpc = SelectedNpc;
    fact.selectedItem = SelectedItem;
    fact.selectedOperate = SelectedOperate;
    fact.character = CharacterIdentity(CharactersClient, SelectedCharacter);
    fact.entity = NpcIdentity(CharactersClient, SelectedNpc);
    if (SelectedItem >= 0 && SelectedItem < MAX_ITEMS && Items[SelectedItem].Key >= 0)
        fact.item = ItemId::TryCreate(static_cast<std::uint64_t>(Items[SelectedItem].Key) + 1);
    if (fact.character)
        fact.kind = GameplayInteractionKind::Character;
    else if (SelectedOperate >= 0 && SelectedOperate < MAX_OPERATES)
        fact.kind = GameplayInteractionKind::Operate;
    else if (fact.entity)
        fact.kind = GameplayInteractionKind::Npc;
    else if (fact.item)
        fact.kind = GameplayInteractionKind::Item;
    else if (SceneFlag == MAIN_SCENE && MainSceneReady && !MouseOnWindow &&
             !sessionKeeper_.WorldUnit()->BlocksInteraction())
    {
        fact.terrainPick = GameLogic::Interaction::PickTerrain(
            sessionKeeper_.WorldUnit()->ReadView(), MousePosition, MouseTarget);
        fact.kind = fact.terrainPick.hit ? GameplayInteractionKind::GroundMovement
                                         : GameplayInteractionKind::None;
    }
    return fact;
}

bool SessionInteractionUnit::PrepareFrame() noexcept
{
    if (SceneFlag != MAIN_SCENE && SceneFlag != CHARACTER_SCENE)
        return true;
    if (SceneFlag == MAIN_SCENE && !MainSceneReady)
        return true;
    auto &projection = sessionKeeper_.CameraProjectionObject();
    const auto width = static_cast<int>(static_cast<std::int64_t>(GetScreenWidth()) *
                                        sessionKeeper_.PlatformWindowWidth() / REFERENCE_WIDTH);
    projection.PrepareInteractionProjection(g_Camera, width, sessionKeeper_.PlatformWindowHeight());
    projection.ScreenToWorldRay(g_Camera, MouseX, MouseY, MouseTarget);
    if (SceneFlag == MAIN_SCENE && MainSceneReady)
    {
        SelectObjects();
    }
    else if (SceneFlag == CHARACTER_SCENE && !IsCursorOnLegacyUi())
    {
        SelectedCharacter = SelectCharacter(KIND_PLAYER);
    }
    return true;
}

SessionInventoryStorage::SessionInventoryStorage() noexcept = default;
SessionInventoryStorage::~SessionInventoryStorage() = default;
void SessionGameplayUnit::EditObjects()
{
    if (EditFlag == EDIT_MONSTER)
    {
        if (MouseLButtonPush)
        {
            MouseLButtonPush = false;
            bool Success =
                RenderTerrainTile(SelectXF, SelectYF, (int)SelectXF, (int)SelectYF, 1.f, 1, true);
            if (Success)
            {
                CHARACTER *c =
                    CreateMonster((EMonsterType)MonsterScript[SelectMonster].Type,
                                  (BYTE)(CollisionPosition[0] / TERRAIN_SCALE),
                                  (BYTE)(CollisionPosition[1] / TERRAIN_SCALE), MonsterKey++);
                c->Object.Kind = KIND_EDIT;
            }
        }
        if (MouseRButtonPush)
        {
            MouseRButtonPush = false;
            if (SelectedCharacter != -1)
            {
                CharactersClient[SelectedCharacter].Object.Live = false;
            }
        }
    }
    if (EditFlag == EDIT_OBJECT)
    {
        if (MouseRButtonPush)
        {
            MouseRButtonPush = false;
            bool Success =
                RenderTerrainTile(SelectXF, SelectYF, (int)SelectXF, (int)SelectYF, 1.f, 1, true);
            if (Success)
            {
                OBJECT *o = CreateObject(SelectModel, CollisionPosition, PickObjectAngle);
                int Scale = (int)TERRAIN_SCALE;
                o->Position[0] = (float)((int)o->Position[0] / Scale + 0.5f) * Scale;
                o->Position[1] = (float)((int)o->Position[1] / Scale + 0.5f) * Scale;
                if (o->RigidPose)
                    o->RigidPoseDirty = true;
            }
        }
        if (MouseLButtonPush)
        {
            MouseLButtonPush = false;
            if (!PickObject)
            {
                if (MouseX < 100 && MouseY < 100)
                {
                    PickObject = CreateObject(SelectModel, MouseTarget, PickObjectAngle);
                }
                else
                {
                    PickObject = CollisionDetectObjects(PickObject);
                    if (PickObject)
                        PickObjectHeight =
                            PickObject->Position[2] -
                            RequestTerrainHeight(PickObject->Position[0], PickObject->Position[1]);
                }
            }
        }
        if (PickObject)
        {
            if (MouseLButton)
            {
                if (PickObject->RigidPose)
                    PickObject->RigidPoseDirty = true;
                bool Success = RenderTerrainTile(SelectXF, SelectYF, (int)SelectXF, (int)SelectYF,
                                                 1.f, 1, true);
                if (Success)
                {
                    VectorCopy(CollisionPosition, PickObject->Position);
                    if (PickObjectLockHeight)
                    {
                        int Scale = (int)TERRAIN_SCALE / 2;
                        PickObject->Position[0] =
                            (float)((int)PickObject->Position[0] / Scale * Scale);
                        PickObject->Position[1] =
                            (float)((int)PickObject->Position[1] / Scale * Scale);
                    }
                    else
                        PickObject->Position[2] += PickObjectHeight;
                }
                if (IsKeyDown('Q'))
                    PickObject->Angle[0] -= 5.f;
                if (IsKeyDown('E'))
                    PickObject->Angle[0] += 5.f;
                if (IsKeyDown('A'))
                    PickObject->Angle[2] += 30.f;
                if (IsKeyDown('D'))
                    PickObject->Angle[2] -= 30.f;
                if (IsKeyDown('W'))
                    PickObjectHeight += 5.f;
                if (IsKeyDown('S'))
                    PickObjectHeight -= 5.f;
                if (IsKeyDown('R'))
                    PickObject->SetScale(PickObject->Scale + 0.02f);
                if (IsKeyDown('F'))
                    PickObject->SetScale(PickObject->Scale - 0.02f);
                if (MouseX >= REFERENCE_WIDTH - 100 && MouseY < 100)
                {
                    DeleteObject(PickObject, &ObjectBlock[PickObject->Block]);
                    PickObject = NULL;
                }
            }
            else
            {
                VectorCopy(PickObject->Angle, PickObjectAngle);
                CreateObject(PickObject->Type, PickObject->Position, PickObject->Angle,
                             PickObject->Scale);
                if (EnableRandomObject)
                {
                    vec3_t Position, Angle;
                    for (int i = 0; i < 9; i++)
                    {
                        VectorCopy(PickObject->Position, Position);
                        VectorCopy(PickObject->Angle, Angle);
                        Position[0] += (float)(WorldRandom() % 2000 - 1000);
                        Position[1] += (float)(WorldRandom() % 2000 - 1000);
                        Position[2] = RequestTerrainHeight(Position[0], Position[1]);
                        Angle[2] = (float)(WorldRandom() % 360);
                        CreateObject(PickObject->Type, Position, Angle,
                                     PickObject->Scale + (float)(WorldRandom() % 16 - 8) * 0.01f);
                    }
                }
                DeleteObject(PickObject, &ObjectBlock[PickObject->Block]);
                PickObject = NULL;
            }
        }
    }

    if (EditFlag == EDIT_HEIGHT)
    {
        if (MouseLButton)
        {
            sessionKeeper_.WorldUnit()->ChangeTerrainHeight(
                CollisionPosition[0], CollisionPosition[1], -10.f, BrushSize + 1);
        }
        if (MouseRButton)
        {
            sessionKeeper_.WorldUnit()->ChangeTerrainHeight(
                CollisionPosition[0], CollisionPosition[1], 10.f, BrushSize + 1);
        }
    }
    if (EditFlag == EDIT_LIGHT)
    {
        vec3_t Light;
        if (MouseLButton)
        {
            switch (SelectColor)
            {
            case 0:
                Vector(0.1f, 0.1f, 0.1f, Light);
                break;
            case 1:
                Vector(-0.1f, -0.1f, -0.1f, Light);
                break;
            case 2:
                Vector(0.05f, -0.05f, -0.05f, Light);
                break;
            case 3:
                Vector(0.05f, 0.05f, -0.05f, Light);
                break;
            case 4:
                Vector(-0.05f, 0.05f, -0.05f, Light);
                break;
            case 5:
                Vector(-0.05f, 0.05f, 0.05f, Light);
                break;
            case 6:
                Vector(-0.05f, -0.05f, 0.05f, Light);
                break;
            case 7:
                Vector(0.05f, -0.05f, 0.05f, Light);
                break;
            }
            AddTerrainLightClip(CollisionPosition[0], CollisionPosition[1], Light, BrushSize + 1,
                                TerrainLight);
            CreateTerrainLight();
        }
        if (MouseRButton)
        {
            int mx = (int)(CollisionPosition[0] / TERRAIN_SCALE);
            int my = (int)(CollisionPosition[1] / TERRAIN_SCALE);
            for (int y = my - 2; y <= my + 2; y++)
            {
                for (int x = mx - 2; x <= mx + 2; x++)
                {
                    int Index1 = TERRAIN_INDEX_REPEAT(x, y);
                    int Index2 = TERRAIN_INDEX_REPEAT(x - 1, y);
                    int Index3 = TERRAIN_INDEX_REPEAT(x + 1, y);
                    int Index4 = TERRAIN_INDEX_REPEAT(x, y - 1);
                    int Index5 = TERRAIN_INDEX_REPEAT(x, y + 1);
                    for (int i = 0; i < 3; i++)
                    {
                        TerrainLight[Index1][i] =
                            (TerrainLight[Index1][i] + TerrainLight[Index2][i] +
                             TerrainLight[Index3][i] + TerrainLight[Index4][i] +
                             TerrainLight[Index5][i]) *
                            0.2f;
                    }
                }
            }
            CreateTerrainLight();
        }
    }
    if (EditFlag == EDIT_MAPPING)
        EditTerrainMapping();
}

void SessionLegacyCalls::EditObjects()
{
    sessionKeeper_.Gameplay()->EditObjects();
}
bool SessionLegacyCalls::CheckTarget(CHARACTER *character)
{
    return sessionKeeper_.Gameplay()->CheckTarget(character);
}

using namespace GameLogic::Combat;

void SessionLegacyCalls::AttackRagefighter(CHARACTER *character, int skill, float distance)
{
    sessionKeeper_.Gameplay()->AttackRagefighter(character, skill, distance);
}

void SessionLegacyCalls::UseSkillWizard(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->UseSkillWizard(character, object);
}

void SessionLegacyCalls::UseSkillSummon(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->UseSkillSummon(character, object);
}

void SessionLegacyCalls::UseSkillRagefighter(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->UseSkillRagefighter(character, object);
}

bool SessionLegacyCalls::UseSkillRagePosition(CHARACTER *character)
{
    return sessionKeeper_.Gameplay()->UseSkillRagePosition(character);
}

bool SessionLegacyCalls::CastWarriorSkill(CHARACTER *c, OBJECT *o, ITEM *p, ActionSkillType iSkill)
{
    return sessionKeeper_.Gameplay()->CastWarriorSkill(c, o, p, iSkill);
} // OMF-00692
bool SessionLegacyCalls::SkillWarrior(CHARACTER *c, ITEM *p)
{
    return sessionKeeper_.Gameplay()->SkillWarrior(c, p);
} // OMF-00693
void SessionLegacyCalls::UseSkillWarrior(CHARACTER *c, OBJECT *o)
{
    return sessionKeeper_.Gameplay()->UseSkillWarrior(c, o);
} // OMF-00694
void SessionLegacyCalls::UseSkillElf(CHARACTER *c, OBJECT *o)
{
    return sessionKeeper_.Gameplay()->UseSkillElf(c, o);
} // OMF-00696
bool SessionLegacyCalls::SkillElf(CHARACTER *c, ITEM *p)
{
    return sessionKeeper_.Gameplay()->SkillElf(c, p);
} // OMF-00701
bool SessionLegacyCalls::CheckMana(CHARACTER *character, int skill)
{
    return sessionKeeper_.Gameplay()->CheckMana(character, skill);
}

bool SessionLegacyCalls::CanExecuteSkill(CHARACTER *character, ActionSkillType skill,
                                         float distance)
{
    return sessionKeeper_.Gameplay()->CanExecuteSkill(character, skill, distance);
}

int SessionLegacyCalls::ExecuteSkill(CHARACTER *c, ActionSkillType Skill, float Distance)
{
    return sessionKeeper_.Gameplay()->ExecuteSkill(c, Skill, Distance);
} // OMF-00704

void SessionLegacyCalls::EmitMeshEffects(BMD &model, int mesh, int type, int subtype, vec3_t angle,
                                         void *owner)
{
    sessionKeeper_.Gameplay()->EmitMeshEffects(model, mesh, type, subtype, angle, owner);
}

void SessionLegacyCalls::StartMatchCountDown(int type)
{
    sessionKeeper_.Gameplay()->StartMatchCountDown(type);
}

void SessionLegacyCalls::ClearMatchInfo()
{
    sessionKeeper_.Gameplay()->ClearMatchInfo();
}

void SessionLegacyCalls::SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data)
{
    sessionKeeper_.Gameplay()->SetMatchGameCommand(data);
}

void SessionLegacyCalls::UpdateMatchState()
{
    sessionKeeper_.Gameplay()->UpdateMatchState();
}

void SessionLegacyCalls::SetMatchResult(int resultCount, int myResult, MatchResult *results,
                                        int success)
{
    sessionKeeper_.Gameplay()->SetMatchResult(resultCount, myResult, results, success);
}

void SessionGameplayUnit::CreateEventMatch(int iWorld)
{
    DeleteEventMatch();

    if (gMapManager.InBloodCastle() == true)
    {
        g_csMatchInfo = new SEASON3B::CNewBloodCastleSystem(sessionKeeper_);
    }
    else if (gMapManager.InChaosCastle() == true)
    {
        g_csMatchInfo = new SEASON3B::CNewChaosCastleSystem(sessionKeeper_);
    }
    else if (gMapManager.InDevilSquare())
    {
        g_csMatchInfo = new CSDevilSquareMatch(sessionKeeper_);
    }
    else if (gMapManager.IsCursedTemple())
    {
        g_csMatchInfo = new CCursedTempleMatch(sessionKeeper_);
    }
    else if (gMapManager.ContextMap() >= WD_65DOPPLEGANGER1 &&
             gMapManager.ContextMap() <= WD_68DOPPLEGANGER4)
    {
        g_csMatchInfo = new CDoppelGangerMatch(sessionKeeper_);
    }
}

void SessionGameplayUnit::DeleteEventMatch()
{
    if (g_csMatchInfo != NULL)
    {
        delete g_csMatchInfo;
        g_csMatchInfo = NULL;
    }
}

void SessionGameplayUnit::UpdateMatchState()
{
    if (g_csMatchInfo != nullptr)
    {
        g_csMatchInfo->Update();
    }
}

void SessionGameplayUnit::ClearMatchInfo()
{
    if (g_csMatchInfo != nullptr)
    {
        g_csMatchInfo->clearMatchInfo();
    }
}

void SessionGameplayUnit::StartMatchCountDown(int type)
{
    if (g_csMatchInfo != nullptr)
    {
        g_csMatchInfo->StartMatchCountDown(type);
    }
}

void SessionGameplayUnit::SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data)
{
    if (g_csMatchInfo != nullptr)
    {
        g_csMatchInfo->SetMatchGameCommand(data);
    }
}

void SessionGameplayUnit::SetMatchResult(int resultCount, int myResult, MatchResult *results,
                                         int success)
{
    if (g_csMatchInfo != nullptr)
    {
        g_csMatchInfo->SetMatchResult(resultCount, myResult, results, success);
    }
}

void SessionLegacyCalls::CreateEventMatch(int world)
{
    sessionKeeper_.Gameplay()->CreateEventMatch(world);
}
void SessionLegacyCalls::DeleteEventMatch()
{
    sessionKeeper_.Gameplay()->DeleteEventMatch();
}

bool SessionLegacyCalls::CreatePersonalItemTable()
{
    return sessionKeeper_.GameData()->CreatePersonalItemTable();
}
void SessionLegacyCalls::ReleasePersonalItemTable()
{
    sessionKeeper_.GameData()->ReleasePersonalItemTable();
}
void SessionLegacyCalls::AddPersonalItemPrice(int index, int price, int type)
{
    sessionKeeper_.GameData()->AddPersonalItemPrice(index, price, type);
}
void SessionLegacyCalls::RemovePersonalItemPrice(int index, int type)
{
    sessionKeeper_.GameData()->RemovePersonalItemPrice(index, type);
}
void SessionLegacyCalls::RemoveAllPerosnalItemPrice(int type)
{
    sessionKeeper_.GameData()->RemoveAllPerosnalItemPrice(type);
}
bool SessionLegacyCalls::GetPersonalItemPrice(int index, int &price, int type)
{
    return sessionKeeper_.GameData()->GetPersonalItemPrice(index, price, type);
}

/// <summary>
/// Layout:
///   Group:  4 bit
///   Number: 12 bit
///   Level:  8 bit
///   Dura:   8 bit
///   OptFlags: 8 bit
///     HasOpt
///     HasLuck
///     HasSkill
///     HasExc
///     HasAnc
///     HasGuardian
///     HasHarmony
///     HasSockets
///   Optional, depending on Flags:
///     Opt_Lvl 4 bit
///     Opt_Typ 4 bit
///     Exc:    8 bit
///     Anc_Dis 4 bit
///     Anc_Bon 4 bit
///     Harmony 8 bit
///     Soc_Bon 4 bit
///     Soc_Cnt 4 bit
///     Sockets n * 8 bit
///  Total: 5 ~ 15 bytes.
/// </summary>

SessionItemStore *SessionLegacyCalls::ItemStore() const
{
    return &sessionKeeper_.GameDataForConstruction().Items();
}

void SessionLegacyCalls::RegisterBone(CHARACTER *character, const std::wstring &name, int bone)
{
    sessionKeeper_.BoneManagerObject().RegisterBone(character, name, bone);
}
void SessionLegacyCalls::UnregisterBone(CHARACTER *character)
{
    sessionKeeper_.BoneManagerObject().UnregisterBone(character);
}
void SessionLegacyCalls::UnregisterAll()
{
    sessionKeeper_.BoneManagerObject().UnregisterAll();
}
CHARACTER *SessionLegacyCalls::GetOwnCharacter(OBJECT *object, const std::wstring &name)
{
    return sessionKeeper_.BoneManagerObject().GetOwnCharacter(object, name);
}
int SessionLegacyCalls::GetBoneNumber(OBJECT *object, const std::wstring &name)
{
    return sessionKeeper_.BoneManagerObject().GetBoneNumber(object, name);
}
bool SessionLegacyCalls::GetBonePosition(OBJECT *object, const std::wstring &name, vec3_t position)
{
    return sessionKeeper_.BoneManagerObject().GetBonePosition(object, name, position);
}
bool SessionLegacyCalls::GetBonePosition(OBJECT *object, const std::wstring &name, vec3_t relative,
                                         vec3_t position)
{
    return sessionKeeper_.BoneManagerObject().GetBonePosition(object, name, relative, position);
}

bool SessionLegacyCalls::GetBonePosition(const OBJECT *object, int bone, vec3_t position) const
{
    return sessionKeeper_.BoneManagerObject().GetBonePosition(object, bone, position);
}

bool SessionLegacyCalls::GetBonePosition(const OBJECT *object, int bone, const vec3_t relative,
                                         vec3_t position) const
{
    return sessionKeeper_.BoneManagerObject().GetBonePosition(object, bone, relative, position);
}

bool SessionLegacyCalls::GetBonePosition(const ObjectDrawInput &draw, int bone,
                                         vec3_t position) const
{
    const vec3_t zero{};
    return GetBonePosition(draw, bone, zero, position);
}
bool SessionLegacyCalls::GetBonePosition(const ObjectDrawInput &draw, int bone,
                                         const vec3_t relative, vec3_t position) const
{
    vec3_t local;
    VectorTransform(relative, draw.bones[bone], local);
    VectorScale(local, draw.scale, local);
    VectorAdd(local, draw.position, position);
    return true;
}

void SessionGameplayUnit::DeleteCharacter()
{
    if (SelectedHero < 0 || SelectedHero >= MAX_CHARACTERS_PER_ACCOUNT)
    {
        return;
    }

    int characterToDelete = SelectedHero;
    SelectedHero = -1;

    if (g_iChatInputType == 1)
    {
        g_pSinglePasswdInputBox->GetText(InputText[0]);
        g_pSinglePasswdInputBox->SetText(NULL);
        g_pSinglePasswdInputBox->SetState(UISTATE_HIDE);
    }

    CurrentProtocolState = REQUEST_DELETE_CHARACTER;
    SocketClient->ToGameServer()->SendDeleteCharacter(CharactersClient[characterToDelete].ID,
                                                      InputText[0]);

    PlayBuffer(SOUND_MENU01);

    ClearInput();
    InputEnable = false;
}

void SessionLegacyCalls::DeleteCharacter()
{
    sessionKeeper_.Gameplay()->DeleteCharacter();
}

bool SessionGameplayUnit::PrepareLoginProtocolOnOwner()
{
    if (loginProtocolPrepared_)
    {
        return true;
    }

    if (!g_ServerListManager.LoadServerListScript())
    {
        return false;
    }

    LegacyUiManager().CreateLoginScene();

    CurrentProtocolState = REQUEST_JOIN_SERVER;
    DeleteSocket();
    CreateSocket(szServerIpAddress, g_ServerPort);

    GuildInputEnable = false;
    TabInputEnable = false;
    GoldInputEnable = false;
    InputEnable = true;
    ClearInput();

    if (g_iChatInputType == 0)
    {
        wcscpy_s(InputText[0], 256, m_Username);
        InputLength[0] = wcslen(InputText[0]);
        InputTextMax[0] = MAX_USERNAME_SIZE;
        InputIndex = InputLength[0] == 0 ? 0 : 1;
    }
    InputNumber = 2;
    InputTextHide[1] = 1;
    loginProtocolPrepared_ = true;
    return true;
}

/**
 * @brief Updates login scene camera animation along predefined waypoint path.
 */
void SessionGameplayUnit::StartLoginPresentation()
{
    // FIX: Enable tour mode with offset correction
    // Tour mode waypoints work well for movement, but need position offset
    // Offset is applied in CCameraMove::GetCurrentCameraPos()
    cameraMove_.PlayCameraWalk(Hero->Object.Position, 1000);
    cameraMove_.SetTourMode(TRUE, FALSE, 0); // Start from waypoint 0

    g_fMULogoAlpha = 0;

    PlayMp3(MUSIC_LOGIN_THEME);
}

bool SessionGameplayUnit::CreateLogInScene()
{
    auto transition = sessionKeeper_.WorldUnit()->PrepareTransition(WorldTransition::Reason::Login,
                                                                    WD_73NEW_LOGIN_SCENE);
    if (!transition)
        return false;

    SessionRenderUnit *renderer = sessionKeeper_.Renderer();
    if (renderer == nullptr || !renderer->EnsureVisualDataOnOwner(false))
        return false;

    MainSceneReady = true;
    if (!transition->Activate())
        return false;

    const bool loginProtocolWasPrepared = loginProtocolPrepared_;
    if (!PrepareLoginProtocolOnOwner())
    {
        return false;
    }
    if (loginProtocolWasPrepared)
    {
        LegacyUiManager().CreateLoginScene();
    }

    StartLoginPresentation();

    g_ErrorReport.Write(L"> Login Scene init success.\r\n");
    return transition->Complete();
}

bool SessionGameplayUnit::PrepareLogInSceneSimulationOnOwner(bool prepareVisibleScene)
{
    if (!prepareVisibleScene && !sessionKeeper_.Config().autoLoginPort)
    {
        return true;
    }
    if (prepareVisibleScene && !InitLogIn)
    {
        if (!CreateLogInScene())
            return false;
        InitLogIn = true;
    }
    else if (!PrepareLoginProtocolOnOwner())
    {
        return false;
    }
    int &pendingLoginMessageCode = sessionKeeper_.NetworkStorage().PendingLoginMessageCode;
    if (prepareVisibleScene && InitLogIn && pendingLoginMessageCode != 0)
    {
        LegacyUiManager().PopUpMsgWin(pendingLoginMessageCode);
        pendingLoginMessageCode = 0;
    }
    loginSceneSimulationEnabled_ =
        prepareVisibleScene && InitLogIn && !LegacyUiManager().m_CreditWin->IsShow();
    return true;
}

bool SessionGameplayUnit::UpdateLogInSceneSimulationWorkerSafe()
{
    if (!loginSceneSimulationEnabled_)
    {
        return true;
    }

    BeginEffectBirths();
    sessionKeeper_.WorldUnit()->BeginSimulationTick(FPS_ANIMATION_FACTOR);
    MoveCamera();
    if (cameraMove_.IsTourMode())
        g_fMULogoAlpha = (std::min)(10.f, g_fMULogoAlpha + 0.02f * FPS_ANIMATION_FACTOR);
    InitTerrainLight();
    MoveObjects();
    MoveLeaves();

    MoveCharactersClient();

    MoveEffects();
    MoveJoints();
    MoveParticles();
    MoveBoids();
    sessionKeeper_.WorldUnit()->EndSimulationTick();
    sessionKeeper_.Visual()->AdvanceMapObservers();
    FinishEffectBirths();
    return true;
}

void SessionGameplayUnit::NewMoveLogInScene()
{
    // ESC menu toggle is handled by CUIMng::Update()
    if (RECEIVE_LOG_IN_SUCCESS == CurrentProtocolState)
    {
        g_ErrorReport.Write(L"> Request Character list\r\n");

        cameraMove_.SetTourMode(FALSE);

        // Tear down the login scene data before asking the server for the
        // account characters, otherwise a fast reply can be cleared again.
        sessionKeeper_.WorldUnit()->Leave(World::LeaveReason::SceneChange);

        SceneFlag = CHARACTER_SCENE;
        CurrentProtocolState = REQUEST_CHARACTERS_LIST;
        SocketClient->ToGameServer()->SendRequestCharacterList(g_pMultiLanguage->GetLanguage());
    }

    g_ConsoleDebug.UpdateMainScene();
}
bool SessionLegacyCalls::CreateLogInScene()
{
    return sessionKeeper_.Gameplay()->CreateLogInScene();
} // OMF-01782
void SessionLegacyCalls::NewMoveLogInScene()
{
    return sessionKeeper_.Gameplay()->NewMoveLogInScene();
} // OMF-01783

bool SessionGameplayUnit::RequireLeavesEffect()
{
    return TheMapProcess().WeatherEnabled();
}

/**
 * @brief Updates user interface and processes player input.
 *
 * Handles all UI-related updates and input processing including:
 * - Party system updates
 * - New UI system updates
 * - Mouse and keyboard input handling
 * - Window focus management
 * - Interface movement and tournament interface updates
 *
 * @note Input waits for server transfer and world activation.
 */
void SessionGameplayUnit::UpdateUIAndInput()
{
    if (sessionKeeper_.WorldUnit()->BlocksInteraction())
        return;

    if (MouseY >= (int)(REFERENCE_HEIGHT - 48))
        MouseOnWindow = true;

    g_pPartyManager->Update();
    g_pNewUISystem->Update(g_timer2StartTickTime);

    if (MouseLButton == true && false == g_pNewUISystem->CheckMouseUse() && g_dwMouseUseUIID == 0 &&
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CHATINPUTBOX) == false)
    {
        g_pWindowMgr->SetWindowsEnable(FALSE);
        g_pFriendMenu->HideMenu();
        g_dwKeyFocusUIID = 0;
        if (GetFocus() != g_hWnd)
        {
            SaveIMEStatus();
            SetFocus(g_hWnd);
        }
    }

    if (ErrorMessage != MESSAGE_LOG_OUT)
        g_pUIManager->UpdateInput();
}

bool SessionGameplayUnit::PrepareMainSceneUpdate(double frameDeltaMilliseconds) noexcept
{
    if (SceneFlag != MAIN_SCENE || !MainSceneReady || frameDeltaMilliseconds <= 0.0)
        return false;
    FPS_ANIMATION_FACTOR = ApplicationFrameUnit::CalculateAnimationFactor(
        frameDeltaMilliseconds, sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    return true;
}

bool SessionGameplayUnit::AdvanceMainSceneTickWorkerSafe() noexcept
{
    if (observer_ != nullptr)
        observer_->OnSessionWorkerStage(SessionWorkerStage::MainSceneEntities);

    try
    {
        if (ErrorMessage != 0)
            MouseOnWindow = true;
        UpdateGameEntities();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool SessionGameplayUnit::AdvanceMapObserverTickWorkerSafe() noexcept
{
    try
    {
        BeginEffectBirths();
        sessionKeeper_.Visual()->AdvanceMapObservers();
        FinishEffectBirths();
        MoveWaterTerrain();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool SessionGameplayUnit::UpdateMainSceneEntitiesWorkerSafe(double frameDeltaMilliseconds) noexcept
{
    if (!PrepareMainSceneUpdate(frameDeltaMilliseconds))
        return true;
    if (!AdvanceMainSceneTickWorkerSafe())
        return false;
    return AdvanceMapObserverTickWorkerSafe();
}

void SessionGameplayUnit::UpdateGameEntities()
{
    BeginEffectBirths();
    sessionKeeper_.WorldUnit()->BeginSimulationTick(FPS_ANIMATION_FACTOR);
#ifdef ENABLE_EDIT2
    UpdateEditorMovement();
#endif
    InitTerrainLight();
    sessionKeeper_.WorldUnit()->AdvanceEnvironment();

    MoveObjects();

    MoveItems();

    if (RequireLeavesEffect())
    {
        MoveLeaves();
    }

    MoveChat();
    UpdatePersonalShopTitleImp();
    MoveHero();
    UpdateHeroHeight();
    MoveCharactersClient();
    MoveBoids();
    MoveFishs();
    MovePoints();
    g_pCatapultWindow->SetCameraPos();
    MoveEffects();
    MoveJoints();
    MoveParticles();
    MovePointers();

    g_Direction.CheckDirection(gMapManager.ContextMap(), WorldTime);
    FinishEffectBirths();
    sessionKeeper_.WorldUnit()->EndSimulationTick();
}

/**
 * @brief Main update function for the game scene.
 *
 * This is the primary per-frame update loop for the main gameplay scene.
 * It orchestrates initialization, server connection waiting, and frame updates by calling:
 * 1. InitializeMainScene() - One-time setup (first call only)
 * 2. Server join synchronization - Waits for server response before enabling rendering
 * 3. InitializeSceneFrame() - Per-frame state reset
 * 4. UpdateUIAndInput() - UI and input processing
 * 5. UpdateGameEntities() - Game world and entity updates
 *
 * @note Returns early if MainSceneReady is false (waiting for server join).
 */

void SessionGameplayUnit::UpdateMainSceneGameplay()
{
    if (MainSceneReady == false)
    {
        return;
    }
    MoveInterface();
    MoveTournamentInterface();
    UpdateSwitchState();
    UpdateMatchState();
#ifdef ENABLE_EDIT
    EditObjects();
#endif
    g_ConsoleDebug.UpdateMainScene();
}

bool SessionLegacyCalls::RequireLeavesEffect()
{
    return sessionKeeper_.Gameplay()->RequireLeavesEffect();
} // OMF-01785
void SessionLegacyCalls::UpdateUIAndInput()
{
    return sessionKeeper_.Gameplay()->UpdateUIAndInput();
} // OMF-01789
void SessionLegacyCalls::UpdateGameEntities()
{
    return sessionKeeper_.Gameplay()->UpdateGameEntities();
} // OMF-01790

void SessionLegacyCalls::CryWolfMVPInit()
{
    sessionKeeper_.Gameplay()->TheMapProcess().Crywolf1st().CryWolfMVPInit();
}

bool SessionLegacyCalls::IsCyrWolf1st()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Crywolf1st().IsCyrWolf1st();
}

bool SessionLegacyCalls::Get_State_Only_Elf() const
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Crywolf1st().Get_State_Only_Elf();
}

void SessionLegacyCalls::Set_Message_Box(int stringId, int number, int key, int objectNumber)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Crywolf1st().Set_Message_Box(stringId, number, key,
                                                                            objectNumber);
}

void SessionLegacyCalls::Set_Val_Hp(int state)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Crywolf1st().Set_Val_Hp(state);
}

bool SessionLegacyCalls::IsDoppelGanger2()
{
    return sessionKeeper_.MapManagerObject().ContextMap() == WD_66DOPPLEGANGER2;
}

bool SessionLegacyCalls::IsDoppelGanger3()
{
    return sessionKeeper_.MapManagerObject().ContextMap() == WD_67DOPPLEGANGER3;
}

bool SessionLegacyCalls::IsDoppelGanger4()
{
    return sessionKeeper_.MapManagerObject().ContextMap() == WD_68DOPPLEGANGER4;
}

// 몬스터 사운드

bool SessionLegacyCalls::IsDuelArena()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().DuelArena().IsDuelArena();
}

bool SessionLegacyCalls::IsGmArea()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().GmArea().IsGmArea();
}

#ifdef ASG_ADD_MAP_KARUTAN

bool SessionLegacyCalls::IsKarutanMap()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Karutan().IsKarutanMap();
}

#endif // ASG_ADD_MAP_KARUTAN

bool SessionLegacyCalls::IsSantaTown()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().SantaTown().IsSantaTown();
}

bool SessionLegacyCalls::IsUnitedMarketPlace() const
{
    return sessionKeeper_.Gameplay()->TheMapProcess().UnitedMarketPlace().IsUnitedMarketPlace();
}

bool SessionLegacyCalls::IsKanturu1st()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Kanturu1st().IsKanturu1st();
}

bool SessionLegacyCalls::Is_Kanturu2nd()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Kanturu2nd().Is_Kanturu2nd();
}

bool SessionLegacyCalls::Is_Kanturu2nd_3rd()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Kanturu2nd().Is_Kanturu2nd_3rd();
}

bool SessionLegacyCalls::Set_CurrentAction_Kanturu2nd_Monster(CHARACTER *character, OBJECT *object)
{
    return sessionKeeper_.Gameplay()
        ->TheMapProcess()
        .Kanturu2nd()
        .Set_CurrentAction_Kanturu2nd_Monster(character, object);
}
void SessionLegacyCalls::Sound_Kanturu2nd_Object(OBJECT *object)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu2nd().Sound_Kanturu2nd_Object(object);
}

bool SessionLegacyCalls::IsInKanturu3rd()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().IsInKanturu3rd();
}

void SessionLegacyCalls::Kanturu3rdInit()
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().Kanturu3rdInit();
}

bool SessionLegacyCalls::IsSuccessBattle()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().IsSuccessBattle();
}

void SessionLegacyCalls::CheckSuccessBattle(BYTE state, BYTE detailState)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().CheckSuccessBattle(state, detailState);
}

void SessionLegacyCalls::MayaSceneMayaAction(BYTE skill)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().MayaSceneMayaAction(skill);
}

void SessionLegacyCalls::Kanturu3rdState(BYTE state, BYTE detailState)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().Kanturu3rdState(state, detailState);
}

void SessionLegacyCalls::Kanturu3rdResult(BYTE result)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().Kanturu3rdResult(result);
}

void SessionLegacyCalls::Kanturu3rdUserandMonsterCount(int monsterCount, int userCount)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().Kanturu3rdUserandMonsterCount(
        monsterCount, userCount);
}

void SessionLegacyCalls::MayaAction(OBJECT *object, BMD *model)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().MayaAction(object, model);
}
void SessionLegacyCalls::Kanturu3rdSuccess()
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().Kanturu3rdSuccess();
}
void SessionLegacyCalls::Kanturu3rdFailed()
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu3rd().Kanturu3rdFailed();
}

MapProcessPtr MapProcess::Make(SessionKeeper &keeper)
{
    return MapProcessPtr(new MapProcess(keeper));
}

MapProcess &SessionGameplayUnit::TheMapProcess()
{
    assert(g_MapProcess);
    return *g_MapProcess;
}

MapProcess &SessionLegacyCalls::TheMapProcess() const
{
    return sessionKeeper_.Gameplay()->TheMapProcess();
}

MapProcess::MapProcess(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), gMapManager(keeper.MapManagerObject())
{
}

MapProcess::~MapProcess()
{
    Destroy();
}

void MapProcess::RegisterOrdinaryMaps()
{
    auto lorencia = std::make_shared<GMLorencia>(sessionKeeper_);
    lorencia_ = lorencia.get();
    lorencia->AddMapIndex(WD_0LORENCIA);
    Register(lorencia);
    previewMap_ = lorencia.get();

    auto dungeon = std::make_shared<GMDungeon>(sessionKeeper_);
    dungeon->AddMapIndex(WD_1DUNGEON);
    Register(dungeon);

    auto devias = std::make_shared<GMDevias>(sessionKeeper_);
    devias->AddMapIndex(WD_2DEVIAS);
    Register(devias);

    auto noria = std::make_shared<GMNoria>(sessionKeeper_);
    noria->AddMapIndex(WD_3NORIA);
    Register(noria);

    auto atlans = std::make_shared<GMAtlans>(sessionKeeper_);
    atlans_ = atlans.get();
    atlans->AddMapIndex(WD_7ATLANSE);
    Register(atlans);

    auto tarkan = std::make_shared<GMTarkan>(sessionKeeper_);
    tarkan_ = tarkan.get();
    tarkan->AddMapIndex(WD_8TARKAN);
    Register(tarkan);

    auto legacyLogin = std::make_shared<GMLegacyLogin>(sessionKeeper_);
    legacyLogin->AddMapIndex(WD_55LOGINSCENE);
    Register(legacyLogin);

    auto lostTower = std::make_shared<GMLostTower>(sessionKeeper_);
    lostTower->AddMapIndex(WD_4LOSTTOWER);
    Register(lostTower);

    auto unknownWorld = std::make_shared<GMUnknownWorld>(sessionKeeper_);
    unknownWorld->AddMapIndex(WD_5UNKNOWN);
    Register(unknownWorld);

    auto stadium = std::make_shared<GMStadium>(sessionKeeper_);
    stadium->AddMapIndex(WD_6STADIUM);
    Register(stadium);

    auto bloodCastle = std::make_shared<GMBloodCastle>(sessionKeeper_);
    bloodCastle->AddMapIndex(WD_11BLOODCASTLE1);
    bloodCastle->AddMapIndex(static_cast<ENUM_WORLD>(WD_11BLOODCASTLE1 + 1));
    bloodCastle->AddMapIndex(static_cast<ENUM_WORLD>(WD_11BLOODCASTLE1 + 2));
    bloodCastle->AddMapIndex(static_cast<ENUM_WORLD>(WD_11BLOODCASTLE1 + 3));
    bloodCastle->AddMapIndex(static_cast<ENUM_WORLD>(WD_11BLOODCASTLE1 + 4));
    bloodCastle->AddMapIndex(static_cast<ENUM_WORLD>(WD_11BLOODCASTLE1 + 5));
    bloodCastle->AddMapIndex(static_cast<ENUM_WORLD>(WD_11BLOODCASTLE1 + 6));
    bloodCastle->AddMapIndex(WD_52BLOODCASTLE_MASTER_LEVEL);
    Register(bloodCastle);

    auto devilSquare = std::make_shared<GMDevilSquare>(sessionKeeper_);
    devilSquare_ = devilSquare.get();
    devilSquare->AddMapIndex(WD_9DEVILSQUARE);
    devilSquare->AddMapIndex(static_cast<ENUM_WORLD>(32));
    Register(devilSquare);

    auto icarus = std::make_shared<GMIcarus>(sessionKeeper_);
    icarus->AddMapIndex(WD_10HEAVEN);
    Register(icarus);
}

void MapProcess::Initialize()
{
    RegisterOrdinaryMaps();
    auto chaosCastle = std::make_shared<GMChaosCastle>(sessionKeeper_);
    chaosCastle_ = chaosCastle.get();
    for (int raw = WD_18CHAOS_CASTLE; raw <= WD_18CHAOS_CASTLE_END; ++raw)
        chaosCastle->AddMapIndex(static_cast<ENUM_WORLD>(raw));
    chaosCastle->AddMapIndex(WD_53CAOSCASTLE_MASTER_LEVEL);
    Register(chaosCastle);

    CGMAidaPtr aida = CGMAida::Make(sessionKeeper_);
    aida_ = aida.get();
    aida->AddMapIndex(WD_33AIDA);
    aida->AddMapIndex(WD_54CHARACTERSCENE);
    Register(aida);

    CGMGmAreaPtr gmArea = CGMGmArea::Make(sessionKeeper_);
    gmArea_ = gmArea.get();
    gmArea->AddMapIndex(WD_40AREA_FOR_GM);
    Register(gmArea);

    CGMHuntingGroundPtr huntingGround = CGMHuntingGround::Make(sessionKeeper_);
    huntingGround_ = huntingGround.get();
    huntingGround->AddMapIndex(WD_31HUNTING_GROUND);
    Register(huntingGround);

    CGMCrywolf1stPtr crywolf1st = CGMCrywolf1st::Make(sessionKeeper_);
    crywolf1st_ = crywolf1st.get();
    gMapManager.ConnectCrywolf1stForConstruction(*crywolf1st_);
    crywolf1st->AddMapIndex(WD_34CRYWOLF_1ST);
    Register(crywolf1st);

    CGMCryingWolf2ndPtr cryingWolf2nd = CGMCryingWolf2nd::Make(sessionKeeper_);
    cryingWolf2nd_ = cryingWolf2nd.get();
    cryingWolf2nd->AddMapIndex(WD_35CRYWOLF_2ND);
    Register(cryingWolf2nd);

    CGMBattleCastlePtr battleCastle = CGMBattleCastle::Make(sessionKeeper_);
    battleCastle_ = battleCastle.get();
    gMapManager.ConnectBattleCastleForConstruction(*battleCastle_);
    battleCastle->AddMapIndex(WD_30BATTLECASTLE);
    Register(battleCastle);

    CGMHellasPtr hellas = CGMHellas::Make(sessionKeeper_);
    hellas_ = hellas.get();
    hellas->AddMapIndex(WD_24HELLAS);
    hellas->AddMapIndex(static_cast<ENUM_WORLD>(WD_24HELLAS + 1));
    hellas->AddMapIndex(static_cast<ENUM_WORLD>(WD_24HELLAS + 2));
    hellas->AddMapIndex(static_cast<ENUM_WORLD>(WD_24HELLAS + 3));
    hellas->AddMapIndex(static_cast<ENUM_WORLD>(WD_24HELLAS + 4));
    hellas->AddMapIndex(static_cast<ENUM_WORLD>(WD_24HELLAS + 5));
    hellas->AddMapIndex(WD_24HELLAS_7);
    Register(hellas);

    GMKanturu1stPtr kanturu1st = GMKanturu1st::Make(sessionKeeper_);
    kanturu1st_ = kanturu1st.get();
    kanturu1st->AddMapIndex(WD_37KANTURU_1ST);
    Register(kanturu1st);

    GMKanturu2ndPtr kanturu2nd = GMKanturu2nd::Make(sessionKeeper_);
    kanturu2nd_ = kanturu2nd.get();
    kanturu2nd->AddMapIndex(WD_38KANTURU_2ND);
    Register(kanturu2nd);

    GMKanturu3rdPtr kanturu3rd = GMKanturu3rd::Make(sessionKeeper_);
    kanturu3rd_ = kanturu3rd.get();
    kanturu3rd->AddMapIndex(WD_39KANTURU_3RD);
    Register(kanturu3rd);

    SEASON4A::CGM_RaklionPtr raklion = SEASON4A::CGM_Raklion::Make(sessionKeeper_);
    raklion->AddMapIndex(WD_57ICECITY);
    raklion->AddMapIndex(WD_58ICECITY_BOSS);
    raklion_ = raklion.get();
    Register(raklion);

    CGMSantaTownPtr santatown = CGMSantaTown::Make(sessionKeeper_);
    santatown->AddMapIndex(WD_62SANTA_TOWN);
    santaTown_ = santatown.get();
    Register(santatown);

    CGM_PK_FieldPtr pkfield = CGM_PK_Field::Make(sessionKeeper_);
    pkfield->AddMapIndex(WD_63PK_FIELD);
    pkField_ = pkfield.get();
    Register(pkfield);

    CGMDuelArenaPtr duelarena = CGMDuelArena::Make(sessionKeeper_);
    duelarena->AddMapIndex(WD_64DUELARENA);
    duelArena_ = duelarena.get();
    Register(duelarena);

    CGMDoppelGanger1Ptr doppelganger1 = CGMDoppelGanger1::Make(sessionKeeper_);
    doppelganger1->AddMapIndex(WD_65DOPPLEGANGER1);
    doppelGanger1_ = doppelganger1.get();
    Register(doppelganger1);

    CGMDoppelGanger2Ptr doppelganger2 = CGMDoppelGanger2::Make(sessionKeeper_);
    doppelganger2->AddMapIndex(WD_66DOPPLEGANGER2);
    doppelGanger2_ = doppelganger2.get();
    Register(doppelganger2);

    CGMDoppelGanger3Ptr doppelganger3 = CGMDoppelGanger3::Make(sessionKeeper_);
    doppelganger3->AddMapIndex(WD_67DOPPLEGANGER3);
    doppelGanger3_ = doppelganger3.get();
    Register(doppelganger3);

    CGMDoppelGanger4Ptr doppelganger4 = CGMDoppelGanger4::Make(sessionKeeper_);
    doppelganger4->AddMapIndex(WD_68DOPPLEGANGER4);
    doppelGanger4_ = doppelganger4.get();
    Register(doppelganger4);

    GMEmpireGuardian1Ptr empireguardian1 = GMEmpireGuardian1::Make(sessionKeeper_);
    empireguardian1->AddMapIndex(WD_69EMPIREGUARDIAN1);
    empireGuardian1_ = empireguardian1.get();
    Register(empireguardian1);

    GMEmpireGuardian2Ptr empireguardian2 = GMEmpireGuardian2::Make(sessionKeeper_);
    empireguardian2->AddMapIndex(WD_70EMPIREGUARDIAN2);
    empireGuardian2_ = empireguardian2.get();
    Register(empireguardian2);

    GMEmpireGuardian3Ptr empireguardian3 = GMEmpireGuardian3::Make(sessionKeeper_);
    empireguardian3->AddMapIndex(WD_71EMPIREGUARDIAN3);
    empireGuardian3_ = empireguardian3.get();
    Register(empireguardian3);

    GMEmpireGuardian4Ptr empireguardian4 = GMEmpireGuardian4::Make(sessionKeeper_);
    empireguardian4->AddMapIndex(WD_72EMPIREGUARDIAN4);
    empireGuardian4_ = empireguardian4.get();
    Register(empireguardian4);

    SEASON3B::GMNewTownPtr newTown = SEASON3B::GMNewTown::Make(sessionKeeper_);
    newTown_ = newTown.get();
    newTown->AddMapIndex(WD_51HOME_6TH_CHAR);
    newTown->AddMapIndex(WD_73NEW_LOGIN_SCENE);
    newTown->AddMapIndex(WD_74NEW_CHARACTER_SCENE);
    Register(newTown);

    SEASON3C::GMSwampOfQuietPtr swampOfQuiet = SEASON3C::GMSwampOfQuiet::Make(sessionKeeper_);
    swampOfQuiet_ = swampOfQuiet.get();
    swampOfQuiet->AddMapIndex(WD_56MAP_SWAMP_OF_QUIET);
    Register(swampOfQuiet);

    GMUnitedMarketPlacePtr unitedMarketPlace = GMUnitedMarketPlace::Make(sessionKeeper_);
    unitedMarketPlace->AddMapIndex(WD_79UNITEDMARKETPLACE);
    unitedMarketPlace_ = unitedMarketPlace.get();
    Register(unitedMarketPlace);

#ifdef ASG_ADD_MAP_KARUTAN
    CGMKarutan1Ptr karutan1 = CGMKarutan1::Make(sessionKeeper_);
    karutan1->AddMapIndex(WD_80KARUTAN1);
    karutan1->AddMapIndex(WD_81KARUTAN2);
    karutan_ = karutan1.get();
    Register(karutan1);
#endif // ASG_ADD_MAP_KARUTAN
    auto &cursedTemple = sessionKeeper_.CursedTempleObject();
    cursedTemple_ = &cursedTemple;
    for (int raw = WD_45CURSEDTEMPLE_LV1; raw <= WD_45CURSEDTEMPLE_LV6; ++raw)
        cursedTemple.AddMapIndex(static_cast<ENUM_WORLD>(raw));
    Register(cursedTemple);
    auto &thirdChange = sessionKeeper_.ThirdChangeObject();
    thirdChange_ = &thirdChange;
    thirdChange.AddMapIndex(WD_41CHANGEUP3RD_1ST);
    thirdChange.AddMapIndex(WD_42CHANGEUP3RD_2ND);
    Register(thirdChange);
    BindCurrentMap();
}

void MapProcess::Destroy()
{
    gMapManager.ClearMapOwnersForDestruction(battleCastle_, crywolf1st_);
    lorencia_ = nullptr;
    atlans_ = nullptr;
    tarkan_ = nullptr;
    devilSquare_ = nullptr;
    chaosCastle_ = nullptr;
    aida_ = nullptr;
    battleCastle_ = nullptr;
    gmArea_ = nullptr;
    huntingGround_ = nullptr;
    crywolf1st_ = nullptr;
    cryingWolf2nd_ = nullptr;
    hellas_ = nullptr;
    kanturu1st_ = nullptr;
    kanturu2nd_ = nullptr;
    kanturu3rd_ = nullptr;
    pkField_ = nullptr;
    raklion_ = nullptr;
    santaTown_ = nullptr;
    duelArena_ = nullptr;
    doppelGanger1_ = nullptr;
    doppelGanger2_ = nullptr;
    doppelGanger3_ = nullptr;
    doppelGanger4_ = nullptr;
    newTown_ = nullptr;
    swampOfQuiet_ = nullptr;
    empireGuardian1_ = nullptr;
    empireGuardian2_ = nullptr;
    empireGuardian3_ = nullptr;
    cursedTemple_ = nullptr;
    thirdChange_ = nullptr;
    empireGuardian4_ = nullptr;
    unitedMarketPlace_ = nullptr;
#ifdef ASG_ADD_MAP_KARUTAN
    karutan_ = nullptr;
#endif
    audioMap_ = nullptr;
    audioMapId_ = -2;
    inactiveAmbient_.clear();
    inactiveMusic_.clear();
    mapsByWorld_.clear();
    currentMap_ = nullptr;
    previewMap_ = nullptr;
    m_MapList.clear();
}

GMChaosCastle &MapProcess::ChaosCastle() noexcept
{
    assert(chaosCastle_ != nullptr);
    return *chaosCastle_;
}

CGMAida &MapProcess::Aida() noexcept
{
    assert(aida_ != nullptr);
    return *aida_;
}

CGMBattleCastle &MapProcess::BattleCastle() noexcept
{
    assert(battleCastle_ != nullptr);
    return *battleCastle_;
}

CGMGmArea &MapProcess::GmArea() noexcept
{
    assert(gmArea_ != nullptr);
    return *gmArea_;
}

CGMHuntingGround &MapProcess::HuntingGround() noexcept
{
    assert(huntingGround_ != nullptr);
    return *huntingGround_;
}

CGMCrywolf1st &MapProcess::Crywolf1st() noexcept
{
    assert(crywolf1st_ != nullptr);
    return *crywolf1st_;
}

CGMCryingWolf2nd &MapProcess::CryingWolf2nd() noexcept
{
    assert(cryingWolf2nd_ != nullptr);
    return *cryingWolf2nd_;
}

CGMHellas &MapProcess::Hellas() noexcept
{
    assert(hellas_ != nullptr);
    return *hellas_;
}

GMKanturu1st &MapProcess::Kanturu1st() noexcept
{
    assert(kanturu1st_ != nullptr);
    return *kanturu1st_;
}

GMKanturu2nd &MapProcess::Kanturu2nd() noexcept
{
    assert(kanturu2nd_ != nullptr);
    return *kanturu2nd_;
}

GMKanturu3rd &MapProcess::Kanturu3rd() noexcept
{
    assert(kanturu3rd_ != nullptr);
    return *kanturu3rd_;
}

CGM_PK_Field &MapProcess::PKField() noexcept
{
    assert(pkField_ != nullptr);
    return *pkField_;
}

SEASON4A::CGM_Raklion &MapProcess::Raklion() noexcept
{
    assert(raklion_ != nullptr);
    return *raklion_;
}

CGMSantaTown &MapProcess::SantaTown() noexcept
{
    assert(santaTown_ != nullptr);
    return *santaTown_;
}

CGMDuelArena &MapProcess::DuelArena() noexcept
{
    assert(duelArena_ != nullptr);
    return *duelArena_;
}

CGMDoppelGanger1 &MapProcess::DoppelGanger1() noexcept
{
    assert(doppelGanger1_ != nullptr);
    return *doppelGanger1_;
}

CGMDoppelGanger2 &MapProcess::DoppelGanger2() noexcept
{
    assert(doppelGanger2_ != nullptr);
    return *doppelGanger2_;
}

CGMDoppelGanger3 &MapProcess::DoppelGanger3() noexcept
{
    assert(doppelGanger3_ != nullptr);
    return *doppelGanger3_;
}

CGMDoppelGanger4 &MapProcess::DoppelGanger4() noexcept
{
    assert(doppelGanger4_ != nullptr);
    return *doppelGanger4_;
}

GMEmpireGuardian4 &MapProcess::EmpireGuardian4() noexcept
{
    assert(empireGuardian4_ != nullptr);
    return *empireGuardian4_;
}

SEASON3B::GMNewTown &MapProcess::NewTown() noexcept
{
    assert(newTown_ != nullptr);
    return *newTown_;
}

SEASON3C::GMSwampOfQuiet &MapProcess::SwampOfQuiet() noexcept
{
    assert(swampOfQuiet_ != nullptr);
    return *swampOfQuiet_;
}

GMUnitedMarketPlace &MapProcess::UnitedMarketPlace() noexcept
{
    assert(unitedMarketPlace_ != nullptr);
    return *unitedMarketPlace_;
}

#ifdef ASG_ADD_MAP_KARUTAN
CGMKarutan1 &MapProcess::Karutan() noexcept
{
    assert(karutan_ != nullptr);
    return *karutan_;
}
#endif

bool MapProcess::FindMap(ENUM_WORLD type)
{
    return MapFor(type) != nullptr;
}

BaseMap &MapProcess::FindBaseMap(ENUM_WORLD type)
{
    BaseMap *const map = MapFor(type);
    assert(map != nullptr);
    if (map == nullptr)
    {
        std::terminate();
    }
    return *map;
}

BaseMap *MapProcess::MapFor(ENUM_WORLD type) const noexcept
{
    const std::size_t index = static_cast<std::size_t>(type);
    return index < mapsByWorld_.size() ? mapsByWorld_[index] : nullptr;
}

void MapProcess::BindCurrentMap() noexcept
{
    const auto *definition = sessionKeeper_.WorldState().definition;
    currentMap_ = definition ? MapFor(static_cast<ENUM_WORLD>(definition->BehaviorMap())) : nullptr;
}

void MapProcess::Register(Smart_Ptr(BaseMap) pMap)
{
    ENUM_WORLD type = pMap->FindMapIndex();
    if (type == NUM_WD)
    {
        assert(0);
        throw;
    }

    if (FindMap(type))
    {
        return;
    }

    BaseMap *const map = pMap.get();
    m_MapList.push_back(std::move(pMap));
    Register(*map);
}

void MapProcess::Register(BaseMap &owner)
{
    BaseMap *const map = &owner;
    ENUM_WORLD type;
    for (int mapIndex = 0;; ++mapIndex)
    {
        type = map->FindMapIndex(mapIndex);
        if (type == NUM_WD)
        {
            break;
        }
        const std::size_t worldIndex = static_cast<std::size_t>(type);
        if (worldIndex >= mapsByWorld_.size())
        {
            mapsByWorld_.resize(worldIndex + 1U, nullptr);
        }
        if (mapsByWorld_[worldIndex] == nullptr)
        {
            mapsByWorld_[worldIndex] = map;
        }
    }
}

BaseMap &MapProcess::GetMap(int type)
{
    return FindBaseMap(static_cast<ENUM_WORLD>(type));
}

GMLorencia &MapProcess::Lorencia() noexcept
{
    return *lorencia_;
}

GMAtlans &MapProcess::Atlans() noexcept
{
    return *atlans_;
}

GMDevilSquare &MapProcess::DevilSquare() noexcept
{
    return *devilSquare_;
}

GMTarkan &MapProcess::Tarkan() noexcept
{
    return *tarkan_;
}

bool SessionLegacyCalls::IsCyringWolf2nd()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().CryingWolf2nd().IsCyringWolf2nd();
}

bool SessionLegacyCalls::IsInHuntingGround()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().HuntingGround().IsInHuntingGround();
}

bool SessionLegacyCalls::IsInHuntingGroundSection2(const vec3_t position)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().HuntingGround().IsInHuntingGroundSection2(
        position);
}

void SessionLegacyCalls::AttackKnight(CHARACTER *character, ActionSkillType skill, float distance)
{
    sessionKeeper_.Gameplay()->AttackKnight(character, skill, distance);
}

void SessionLegacyCalls::AttackWizard(CHARACTER *character, int skill, float distance)
{
    sessionKeeper_.Gameplay()->AttackWizard(character, skill, distance);
}

void SessionLegacyCalls::AttackCommon(CHARACTER *character, int skill, float distance)
{
    sessionKeeper_.Gameplay()->AttackCommon(character, skill, distance);
}
void SessionLegacyCalls::AttackElf(CHARACTER *c, int Skill, float Distance)
{
    return sessionKeeper_.Gameplay()->AttackElf(c, Skill, Distance);
} // OMF-00687

float RequestTerrainHeight(float xf, float yf);

BOOL SessionGameplayUnit::SendRequestSummonSkill(int iSkill, CHARACTER *pCharacter, OBJECT *pObject)
{
    if (iSkill < AT_SKILL_SUMMON_EXPLOSION || iSkill > AT_SKILL_SUMMON_POLLUTION)
        return FALSE;

    int iTargetKey = -1;
    const int targetIndex = g_MovementSkill.m_iTarget;
    if (CheckMovementSkillTarget(pCharacter) && CharactersClient.IsValidIndex(targetIndex))
    {
        CHARACTER *pTargetCharacter = &CharactersClient[targetIndex];
        iTargetKey = pTargetCharacter->Key;
        TargetX = (int)(pTargetCharacter->Object.Position[0] / TERRAIN_SCALE);
        TargetY = (int)(pTargetCharacter->Object.Position[1] / TERRAIN_SCALE);
        VectorCopy(pTargetCharacter->Object.Position, pCharacter->TargetPosition);
    }

    g_MovementSkill.m_bMagic = FALSE;
    g_MovementSkill.m_iSkill = iSkill;

    float fDistance = gSkillManager.GetSkillDistance(iSkill, pCharacter);
    if (CheckTile(pCharacter, pObject, fDistance))
    {
        if (iTargetKey != -1)
        {
            SendRequestMagicContinue(iSkill, (BYTE)(pCharacter->TargetPosition[0] / TERRAIN_SCALE),
                                     (BYTE)(pCharacter->TargetPosition[1] / TERRAIN_SCALE),
                                     (BYTE)(pObject->Angle[2] / 360.f * 256.f), 0, 0,
                                     (WORD)iTargetKey, 0);
        }
        else
        {
            SendRequestMagicContinue(iSkill, (BYTE)TargetX, (BYTE)TargetY,
                                     (BYTE)(pObject->Angle[2] / 360.f * 256.f), 0, 0, 0xffff, 0);
        }
        g_SummonSystem.CastSummonSkill(iSkill, pCharacter, pObject, TargetX, TargetY);
    }
    return FALSE;
}

// Forward declaration
BOOL Util_CheckOption(std::wstring lpszCommandLine, wchar_t cOption, std::wstring &lpszString);

#if defined(_DEBUG) && defined(_WIN32)
namespace
{
void SignalProfileCharacterSceneReady() noexcept
{
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetEnvironmentVariableW(L"MU_PROFILE_READY_FILE", path.data(),
                                                 static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return;
    }
    const HANDLE file = CreateFileW(path.data(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file);
    }
}
} // namespace
#endif

void SessionGameplayUnit::StartGame()
{
    {
        if (SelectedHero < 0 || SelectedHero >= MAX_CHARACTERS_PER_ACCOUNT ||
            !CharactersClient.IsValidIndex(SelectedHero) ||
            !CharactersClient[SelectedHero].Object.Live)
        {
            SelectedHero = -1;
            return;
        }

        if (CTLCODE_01BLOCKCHAR & CharactersClient[SelectedHero].CtlCode)
            LegacyUiManager().PopUpMsgWin(MESSAGE_BLOCKED_CHARACTER);
        else
        {
            CharacterAttribute->Level = CharactersClient[SelectedHero].Level;
            CharacterAttribute->Class = CharactersClient[SelectedHero].Class;
            CharacterAttribute->Skin = CharactersClient[SelectedHero].Skin;
            ::wcscpy_s(CharacterAttribute->Name, MAX_USERNAME_SIZE + 1,
                       CharactersClient[SelectedHero].ID);
            sessionKeeper_.Network()->MapServer().SetHeroID(CharactersClient[SelectedHero].ID);

            sessionKeeper_.WorldUnit()->Leave(World::LeaveReason::SceneChange);
            sessionKeeper_.WorldUnit()->BeginTransfer();
            InitLoading = false;
            MainSceneReady = false;
            SceneFlag = LOADING_SCENE;
            sessionKeeper_.Ui()->AdvanceLoadingSceneOnOwner(sessionKeeper_.Display()->IsVisible());
        }
    }
}

void SessionGameplayUnit::InitializeCharacterSceneInput()
{
    g_iKeyPadEnable = 0;
    GuildInputEnable = false;
    TabInputEnable = false;
    GoldInputEnable = false;
    InputEnable = true;
    ClearInput();
    InputIndex = 0;
    InputTextWidth = 90;
    InputNumber = 1;

    for (int i = 0; i < MAX_WHISPER; i++)
    {
        g_pChatListBox->AddText(L"", L"", SEASON3B::TYPE_WHISPER_MESSAGE);
    }

    HIMC hIMC = ImmGetContext(g_hWnd);
    DWORD Conversion, Sentence;

    Conversion = IME_CMODE_NATIVE;
    Sentence = IME_SMODE_NONE;

    g_bIMEBlock = FALSE;
    RestoreIMEStatus();
    ImmSetConversionStatus(hIMC, Conversion, Sentence);
    ImmGetConversionStatus(hIMC, &g_dwBKConv, &g_dwBKSent);
    SaveIMEStatus();
    ImmReleaseContext(g_hWnd, hIMC);
    g_bIMEBlock = TRUE;
}

bool SessionGameplayUnit::CreateCharacterScene()
{
    auto transition = sessionKeeper_.WorldUnit()->PrepareTransition(
        WorldTransition::Reason::CharacterSelection, WD_74NEW_CHARACTER_SCENE);
    if (!transition)
        return false;

    SessionRenderUnit *renderer = sessionKeeper_.Renderer();
    if (renderer == nullptr || !renderer->EnsureVisualDataOnOwner(false))
        return false;
    SessionNetworkUnit *network = sessionKeeper_.Network();
    if (network == nullptr || !network->MaterializePendingCharacterListOnOwner())
        return false;

    g_pNewUIMng->ResetActiveUIObj();

    MainSceneReady = true;
    MouseOnWindow = false;
    ErrorMessage = 0;

    if (!transition->Activate())
        return false;
    ConfigureCharacterSceneModels();

    CreateCharacterPointer(&CharacterView, MODEL_FACE + 1, 0, 0);
    CharacterView.Class = CLASS_KNIGHT;
    CharacterView.SkinIndex = gCharacterManager.GetSkinModelIndex(CLASS_KNIGHT);
    CharacterView.Object.Kind = 0;

    SelectedHero = -1;
    LegacyUiManager().CreateCharacterScene();

    ClearInventory();
    CharacterAttribute->SkillNumber = 0;

    for (int i = 0; i < MAX_MAGIC; i++)
        CharacterAttribute->Skill[i] = AT_SKILL_UNDEFINED;

    for (int i = EQUIPMENT_WEAPON_RIGHT; i < EQUIPMENT_HELPER; i++)
        CharacterMachine->Equipment[i].Level = 0;

    g_pNewUISystem->HideAll();

    InitializeCharacterSceneInput();

    g_ErrorReport.Write(L"> Character scene init success.\r\n");
#if defined(_DEBUG) && defined(_WIN32)
    SignalProfileCharacterSceneReady();
#endif
    return transition->Complete();
}

bool SessionGameplayUnit::PrepareCharacterSceneSimulationOnOwner(bool prepareVisibleScene)
{
    if (CurrentProtocolState < RECEIVE_CHARACTERS_LIST)
    {
        return true;
    }

    if ((prepareVisibleScene || !sessionKeeper_.Config().autoSelectCharacter.empty()) &&
        !InitCharacterScene)
    {
        if (!CreateCharacterScene())
            return false;
        InitCharacterScene = true;
        TryAutoSelectCharacter();
    }
    return true;
}

void SessionGameplayUnit::TryAutoSelectCharacter()
{
    auto &configuredName = sessionKeeper_.Config().autoSelectCharacter;
    if (configuredName.empty())
        return;
    const std::wstring name = std::exchange(configuredName, {});
    for (int index = 0; index < MAX_CHARACTERS_PER_ACCOUNT; ++index)
    {
        const CHARACTER &character = CharactersClient[index];
        if (character.Object.Live && name == character.ID)
        {
            SelectedHero = index;
            StartGame();
            return;
        }
    }
}

bool SessionGameplayUnit::UpdateCharacterSceneSimulationWorkerSafe()
{
    if (CurrentProtocolState < RECEIVE_CHARACTERS_LIST || !InitCharacterScene)
    {
        return true;
    }
    BeginEffectBirths();
    sessionKeeper_.WorldUnit()->BeginSimulationTick(FPS_ANIMATION_FACTOR);
    InitTerrainLight();
    sessionKeeper_.Visual()->ApplySelectedCharacterLighting();
    MoveObjects();

    // Preserve the selection camera and frustum prepared before interaction.
    MoveCharactersClient();

    MoveEffects();
    MoveJoints();
    MoveParticles();
    MoveBoids();

    sessionKeeper_.WorldUnit()->EndSimulationTick();
    sessionKeeper_.Visual()->AdvanceMapObservers();
    sessionKeeper_.Visual()->AdvanceSelectedCharacterEffects();
    FinishEffectBirths();
    return true;
}

void SessionGameplayUnit::NewMoveCharacterScene()
{
    if (CurrentProtocolState < RECEIVE_CHARACTERS_LIST)
    {
        return;
    }

#if defined _DEBUG || defined FOR_WORK
    std::wstring lpszTemp = {0};
    if (::Util_CheckOption(::GetCommandLine(), L'c', lpszTemp))
    {
        const int requestedHero = ::_wtoi(lpszTemp.c_str());
        SelectedHero = requestedHero >= 0 && requestedHero < MAX_CHARACTERS_PER_ACCOUNT &&
                               CharactersClient.IsValidIndex(requestedHero) &&
                               CharactersClient[requestedHero].Object.Live
                           ? requestedHero
                           : -1;
        StartGame();
    }
#endif

    CInput &rInput = Input();
    CUIMng &rUIMng = LegacyUiManager();

    if (IsPress(VK_RETURN))
    {
        if (!(rUIMng.m_MsgWin->IsShow() || rUIMng.m_CharMakeWin->IsShow() ||
              rUIMng.m_SysMenuWin->IsShow()) &&
            SelectedHero > -1 && SelectedHero < MAX_CHARACTERS_PER_ACCOUNT)
        {
            PlayBuffer(SOUND_CLICK01);

            if (SelectedCharacter >= 0)
                SelectedHero = SelectedCharacter;

            StartGame();
        }
    }
    // ESC menu toggle is handled by CUIMng::Update()

    if (rUIMng.IsCursorOnUI())
    {
        return;
    }

    if (MouseLButtonDBClick && rUIMng.m_CharSelMainWin->IsShow())
    {
        if (SelectedCharacter < 0 || SelectedCharacter >= MAX_CHARACTERS_PER_ACCOUNT)
        {
            return;
        }

        SelectedHero = SelectedCharacter;
        StartGame();
    }
    else if (MouseLButtonPush)
    {
        if (SelectedCharacter < 0 || SelectedCharacter >= MAX_CHARACTERS_PER_ACCOUNT)
            SelectedHero = -1;
        else
            SelectedHero = SelectedCharacter;
        rUIMng.m_CharSelMainWin->UpdateDisplay();
    }

    g_ConsoleDebug.UpdateMainScene();
}

bool SessionLegacyCalls::CreateCharacterScene()
{
    return sessionKeeper_.Gameplay()->CreateCharacterScene();
}

void SessionLegacyCalls::NewMoveCharacterScene()
{
    sessionKeeper_.Gameplay()->NewMoveCharacterScene();
}
void SessionLegacyCalls::StartGame()
{
    sessionKeeper_.Gameplay()->StartGame();
}

extern int GetMp3PlayPosition();

bool SessionLegacyCalls::IsDoppelGanger1()
{
    return sessionKeeper_.MapManagerObject().ContextMap() == WD_65DOPPLEGANGER1;
}

namespace
{
using INTBYTEPAIR = std::pair<int, BYTE>;

}
void SessionLegacyCalls::SendReqUnMix()
{
    return sessionKeeper_.Gameplay()->SendReqUnMix();
} // OMF-00736
void SessionLegacyCalls::SendReqMix()
{
    return sessionKeeper_.Gameplay()->SendReqMix();
} // OMF-00737
void SessionLegacyCalls::ProcessCSAction()
{
    return sessionKeeper_.Gameplay()->ProcessCSAction();
} // OMF-00738
int SessionLegacyCalls::GetUnMixGemLevel() const
{
    return sessionKeeper_.Gameplay()->GetUnMixGemLevel();
} // OMF-00742
bool SessionLegacyCalls::CheckMyInvValid()
{
    return sessionKeeper_.Gameplay()->CheckMyInvValid();
} // OMF-00747
char SessionLegacyCalls::CheckOneItem(const ITEM *item) const
{
    return sessionKeeper_.Gameplay()->CheckOneItem(item);
} // OMF-00745
void SessionLegacyCalls::CalcGen()
{
    sessionKeeper_.Gameplay()->CalcGen();
} // OMF-00748
char SessionLegacyCalls::CalcCompiledCount(const ITEM *item) const
{
    return sessionKeeper_.Gameplay()->CalcCompiledCount(item);
} // OMF-00749
int SessionLegacyCalls::CalcItemValue(const ITEM *item) const
{
    return sessionKeeper_.Gameplay()->CalcItemValue(item);
} // OMF-00750
int SessionLegacyCalls::CalcEmptyInv() const
{
    return sessionKeeper_.Gameplay()->CalcEmptyInv();
} // OMF-00751
void SessionLegacyCalls::Init()
{
    return sessionKeeper_.Gameplay()->Init();
} // OMF-00752
void SessionLegacyCalls::GetBack()
{
    return sessionKeeper_.Gameplay()->GetBack();
} // OMF-00753
void SessionLegacyCalls::Exit()
{
    return sessionKeeper_.Gameplay()->Exit();
} // OMF-00754
int SessionLegacyCalls::GetJewelRequireCount(int index) const
{
    return sessionKeeper_.Gameplay()->GetJewelRequireCount(index);
} // OMF-00755
int SessionLegacyCalls::Check_Jewel(int jewel, int type, bool model) const
{
    return sessionKeeper_.Gameplay()->Check_Jewel(jewel, type, model);
} // OMF-00756
int SessionLegacyCalls::GetJewelIndex(int jewel, int type) const
{
    return sessionKeeper_.Gameplay()->GetJewelIndex(jewel, type);
} // OMF-00757
void SessionLegacyCalls::SetMode(BOOL mode)
{
    sessionKeeper_.Gameplay()->SetMode(mode);
} // OMF-00758
void SessionLegacyCalls::SetGem(char gem)
{
    sessionKeeper_.Gameplay()->SetGem(gem);
} // OMF-00759
void SessionLegacyCalls::SetComType(char type)
{
    sessionKeeper_.Gameplay()->SetComType(type);
} // OMF-00760
void SessionLegacyCalls::SetState(char state)
{
    sessionKeeper_.Gameplay()->SetState(state);
} // OMF-00761
void SessionLegacyCalls::SetError(char error)
{
    sessionKeeper_.Gameplay()->SetError(error);
} // OMF-00762
char SessionLegacyCalls::GetError() const
{
    return sessionKeeper_.Gameplay()->GetError();
} // OMF-00763
bool SessionLegacyCalls::isComMode() const
{
    return sessionKeeper_.Gameplay()->isComMode();
} // OMF-00764
int SessionLegacyCalls::Check_Jewel_Unit(int jewel, bool model) const
{
    return sessionKeeper_.Gameplay()->Check_Jewel_Unit(jewel, model);
} // OMF-00765
int SessionLegacyCalls::Check_Jewel_Com(int jewel, bool model) const
{
    return sessionKeeper_.Gameplay()->Check_Jewel_Com(jewel, model);
} // OMF-00766
bool SessionLegacyCalls::isCompiledGem(const ITEM *item) const
{
    return sessionKeeper_.Gameplay()->isCompiledGem(item);
} // OMF-00767
bool SessionLegacyCalls::isAble() const
{
    return sessionKeeper_.Gameplay()->isAble();
} // OMF-00768

bool SessionLegacyCalls::IsHeroSwingInProgress() const
{
    return sessionKeeper_.MuHelper()->IsHeroSwingInProgress();
}
void SessionLegacyCalls::ChangeCharacterExt(int key, BYTE *equipment, CHARACTER *character,
                                            OBJECT *helper)
{
    sessionKeeper_.Gameplay()->ChangeCharacterExt(key, equipment, character, helper);
}
void SessionLegacyCalls::ReadEquipmentExtended(int key, BYTE flags, BYTE *equipment,
                                               CHARACTER *character, OBJECT *helper)
{
    sessionKeeper_.Gameplay()->ReadEquipmentExtended(key, flags, equipment, character, helper);
}

void SessionLegacyCalls::CreateCharacterPointer(CHARACTER *character, int type,
                                                unsigned char positionX, unsigned char positionY,
                                                float rotation)
{
    sessionKeeper_.Gameplay()->CreateCharacterPointer(character, type, positionX, positionY,
                                                      rotation);
}
void SessionLegacyCalls::SetCharacterClass(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetCharacterClass(character);
}
CHARACTER *SessionLegacyCalls::CreateCharacter(int key, int type, unsigned char positionX,
                                               unsigned char positionY, float rotation)
{
    return sessionKeeper_.Gameplay()->CreateCharacter(key, type, positionX, positionY, rotation);
}

CHARACTER *SessionLegacyCalls::CreateHero(int key, CLASS_TYPE characterClass, int skin, float x,
                                          float y, float rotation)
{
    return sessionKeeper_.Gameplay()->CreateHero(key, characterClass, skin, x, y, rotation);
}

int SessionLegacyCalls::FindCharacterIndex(int key)
{
    return sessionKeeper_.Gameplay()->FindCharacterIndex(key);
}

int SessionLegacyCalls::FindCharacterIndexByMonsterIndex(int type)
{
    return sessionKeeper_.Gameplay()->FindCharacterIndexByMonsterIndex(type);
}

void SessionLegacyCalls::SetCharacterScale(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetCharacterScale(character);
}

CHARACTER *SessionLegacyCalls::FindCharacterByID(wchar_t *name)
{
    return sessionKeeper_.Gameplay()->FindCharacterByID(name);
}

CHARACTER *SessionLegacyCalls::FindCharacterByKey(int key)
{
    return sessionKeeper_.Gameplay()->FindCharacterByKey(key);
}

void SessionLegacyCalls::Setting_Monster(CHARACTER *character, EMonsterType type, int positionX,
                                         int positionY)
{
    sessionKeeper_.Gameplay()->Setting_Monster(character, type, positionX, positionY);
}
CHARACTER *SessionLegacyCalls::CreateMonster(EMonsterType type, int positionX, int positionY,
                                             int key)
{
    return sessionKeeper_.Gameplay()->CreateMonster(type, positionX, positionY, key);
}
CHARACTER *SessionLegacyCalls::CreateHellGate(char *id, int key, EMonsterType index, int x, int y,
                                              int createFlag)
{
    return sessionKeeper_.Gameplay()->CreateHellGate(id, key, index, x, y, createFlag);
}
void SessionLegacyCalls::ClearCharacters(int key)
{
    sessionKeeper_.Gameplay()->ClearCharacters(key);
}
void SessionLegacyCalls::DeleteCharacter(int key)
{
    sessionKeeper_.Gameplay()->DeleteCharacter(key);
}
void SessionLegacyCalls::DeleteCharacter(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->DeleteCharacter(character, object);
}
void SessionLegacyCalls::ReleaseCharacters()
{
    sessionKeeper_.Gameplay()->ReleaseCharacters();
}

void SessionLegacyCalls::SetCharacterTarget(CHARACTER &character, int index)
{
    sessionKeeper_.Gameplay()->SetCharacterTarget(character, index);
}

void SessionLegacyCalls::DeleteNpcs()
{
    sessionKeeper_.Renderer()->DeleteNpcs();
}

void SessionLegacyCalls::SetMonsterSound(int type, int sound1, int sound2, int sound3, int sound4,
                                         int sound5, int sound6, int sound7, int sound8, int sound9,
                                         int sound10)
{
    sessionKeeper_.GameData()->SetMonsterSound(type, sound1, sound2, sound3, sound4, sound5, sound6,
                                               sound7, sound8, sound9, sound10);
}

void SessionLegacyCalls::DeleteMonsters()
{
    sessionKeeper_.GameData()->DeleteMonsters();
}

bool SessionGameplayUnit::SendAttackPacket(CHARACTER *_pCha, int _nMoveTarget, int _nSkill)
{
    OBJECT *pObj = &_pCha->Object;

    BYTE TargetPosX = (BYTE)(_pCha->TargetPosition[0] / TERRAIN_SCALE);
    BYTE TargetPosY = (BYTE)(_pCha->TargetPosition[1] / TERRAIN_SCALE);

    vec3_t vDis;
    Vector(0.0f, 0.0f, 0.0f, vDis);
    VectorSubtract(_pCha->TargetPosition, _pCha->Object.Position, vDis);
    VectorNormalize(vDis);
    VectorScale(vDis, TERRAIN_SCALE, vDis);
    VectorSubtract(_pCha->TargetPosition, vDis, vDis);
    BYTE CharPosX = (BYTE)(vDis[0] / TERRAIN_SCALE);
    BYTE CharPosY = (BYTE)(vDis[1] / TERRAIN_SCALE);

    if (_nMoveTarget <= -1)
    {
        return false;
    }

    int TargetIndex = TERRAIN_INDEX(TargetPosX, TargetPosY);

    if ((TerrainWall[TargetIndex] & TW_NOMOVE) != TW_NOMOVE &&
        (TerrainWall[TargetIndex] & TW_NOGROUND) != TW_NOGROUND)
    {
#ifdef SEND_POSITION_TO_SERVER
        // if(!InChaosCastle())
        SocketClient->ToGameServer()->SendInstantMoveRequest(CharPosX, CharPosY);
#endif

        VectorCopy(CharactersClient[_nMoveTarget].Object.Position, _pCha->TargetPosition);
        //몬스터의 넉백효과의 의해 이펙트가 혼란스러움 방지
        if (!(pObj->CurrentAction == PLAYER_SKILL_GIANTSWING &&
              g_CMonkSystem.IsSecondAttackState()))
            pObj->SetAngleZ(CreateAngle2D(pObj->Position, _pCha->TargetPosition));

        SendRequestMagic(_nSkill, CharactersClient[_nMoveTarget].Key);
        return true;
    }

    return false;
}

void SessionGameplayUnit::SetDarksideTargetIndex(WORD *targetIndex, ActionSkillType skill)
{
    if (!g_CMonkSystem.SetDarksideTargetIndexState(*Hero, targetIndex))
    {
        SendRequestMagic(skill, HeroKey);
    }
    else
    {
        PlayBuffer(SOUND_RAGESKILL_DARKSIDE_ATTACK);
    }
}

/**
 * @brief Updates the active scene based on current scene flag.
 */
bool SessionGameplayUnit::PrepareSceneSimulationOnOwner(bool prepareVisibleScene) noexcept
try
{
    loginSceneSimulationEnabled_ = false;
    switch (SceneFlag)
    {
    case WEBZEN_SCENE:
        if (prepareVisibleScene)
        {
            if (!sessionKeeper_.Ui()->AdvanceTitleSceneOnOwner())
                return false;
            return SceneFlag != LOG_IN_SCENE || PrepareLogInSceneSimulationOnOwner(true);
        }
        if (!sessionKeeper_.Config().autoLoginPort)
        {
            return true;
        }
        SceneFlag = LOG_IN_SCENE;
        return PrepareLogInSceneSimulationOnOwner(false);
    case LOADING_SCENE:
        return sessionKeeper_.Ui()->AdvanceLoadingSceneOnOwner(prepareVisibleScene);
    case LOG_IN_SCENE:
        return PrepareLogInSceneSimulationOnOwner(prepareVisibleScene);
    case CHARACTER_SCENE:
        return PrepareCharacterSceneSimulationOnOwner(prepareVisibleScene);
    default:
        return true;
    }
}

catch (...)
{
    // Local UI/resource entry cannot terminate healthy peer sessions.
    return sessionKeeper_.WorldUnit()->FinishLoad(false);
}

bool SessionGameplayUnit::UpdateVisibleSceneSimulationWorkerSafe(
    double frameDeltaMilliseconds) noexcept
{
    if (observer_ != nullptr)
        observer_->OnSessionWorkerStage(SessionWorkerStage::VisibleSimulation);

    if (SceneFlag != LOG_IN_SCENE && SceneFlag != CHARACTER_SCENE)
    {
        return true;
    }

    if (frameDeltaMilliseconds <= 0.0)
        return true;
    FPS_ANIMATION_FACTOR = ApplicationFrameUnit::CalculateAnimationFactor(
        frameDeltaMilliseconds, sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    return SceneFlag == LOG_IN_SCENE ? UpdateLogInSceneSimulationWorkerSafe()
                                     : UpdateCharacterSceneSimulationWorkerSafe();
}

bool SessionGameplayUnit::UpdateGameplaySceneOnOwner()
{
    switch (SceneFlag)
    {
    case LOG_IN_SCENE:
        NewMoveLogInScene();
        return true;

    case CHARACTER_SCENE:
        NewMoveCharacterScene();
        return true;

    case MAIN_SCENE:
        UpdateMainSceneGameplay();
        return true;
    }
    return true;
}

bool SessionGameplayUnit::UpdateSceneGameplaySystems(const SessionPhysicsFrameInput &)
{
    if (observer_ != nullptr)
        observer_->OnSessionWorkerStage(SessionWorkerStage::Systems);

    // Physics and wind advance in World::BeginSimulationTick, including catch-up.
    return true;
}

bool SessionGameplayUnit::UpdateApplicationSceneCompatibility()
{
    if (Destroy ||
        (SceneFlag != LOG_IN_SCENE && SceneFlag != CHARACTER_SCENE && SceneFlag != MAIN_SCENE))
    {
        return !Destroy;
    }

    try
    {
        CheckServerConnection();
        ManageMainSceneAudio();
        return true;
    }
    catch (const std::exception &e)
    {
        char errorMsg[256];
        sprintf_s(errorMsg, sizeof(errorMsg), "Exception in scene update: %s", e.what());
        OutputDebugStringA(errorMsg);
    }
    return false;
}

/**
 * @brief Updates scene state, handles input, and manages screenshot capture.
 */

bool SessionGameplayUnit::UpdateSceneGameplayOnOwner()
{
    return UpdateGameplaySceneOnOwner();
}

/**
 * @brief Checks and handles server connection loss.
 */
void SessionGameplayUnit::CheckServerConnection()
{
    if (SocketClient != nullptr && SocketClient->IsConnected())
    {
        return;
    }

    // A reconnect already in progress manages its own connection attempts.
    ReconnectManager &reconnect = sessionKeeper_.Network()->Reconnect();
    if (reconnect.IsActive())
    {
        return;
    }

    // Auto-reconnect only makes sense in-game, where the server restores the
    // character's saved position. Other scenes keep the original behaviour.
    if (SceneFlag == MAIN_SCENE && reconnect.HasSession())
    {
        g_ErrorReport.Write(L"> Connection lost in game - starting auto-reconnect. ");
        g_ErrorReport.WriteCurrentTime();
        g_ConsoleDebug.Write(MCD_NORMAL, L"Connection lost in game - starting auto-reconnect");
        // Keep the character visible during the brief re-login phase instead of showing a black screen.
        reconnect.RequestBegin();
        return;
    }

    if (!serverConnectionClosed_)
    {
        serverConnectionClosed_ = TRUE;
        g_ErrorReport.Write(L"> Connection closed. ");
        g_ErrorReport.WriteCurrentTime();
        g_ConsoleDebug.Write(MCD_NORMAL, L"Connection closed");
        LegacyUiManager().PopUpMsgWin(MESSAGE_SERVER_LOST);
    }
}

// The bound map owns ambient sound and music selection.
void SessionGameplayUnit::ManageMainSceneAudio()
{
    if (SceneFlag != MAIN_SCENE)
        return;

    TheMapProcess().UpdateWorldAudio();
}

void SessionLegacyCalls::ManageMainSceneAudio()
{
    sessionKeeper_.Gameplay()->ManageMainSceneAudio();
}

void SessionLegacyCalls::CheckServerConnection()
{
    return sessionKeeper_.Gameplay()->CheckServerConnection();
} // OMF-01836

using namespace SEASON4A;

bool SessionLegacyCalls::IsIceCity()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Raklion().IsIceCity();
}
//  CSParts.cpp

void SessionLegacyCalls::CreatePartsFactory(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->CreatePartsFactory(character);
}

void SessionLegacyCalls::DeleteParts(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->DeleteParts(character);
}

CharacterDrawInput SessionLegacyCalls::CharacterPresentationInput(const CHARACTER &character) const
{
    CharacterDrawInput input(character);
    if (const auto *visual = sessionKeeper_.Visual()->FindCharacterVisual(character))
        visual->movement.Apply(input.object);
    return input;
}

int SessionGameplayUnit::GetEquipedBowType() const
{
    const ITEM &left = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];
    if (CharacterRulesDetail::IsEquippedBowItem(left.Type))
    {
        return BOWTYPE_BOW;
    }

    const ITEM &right = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT];
    if (CharacterRulesDetail::IsEquippedCrossbowItem(right.Type))
    {
        return BOWTYPE_CROSSBOW;
    }

    return BOWTYPE_NONE;
}

int SessionGameplayUnit::GetEquipedBowType_Skill() const
{
    const ITEM &left = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];
    const ITEM &right = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT];

    if (CharacterRulesDetail::IsEquippedBowItem(left.Type) && right.Type == ITEM_ARROWS)
    {
        return BOWTYPE_BOW;
    }

    if (CharacterRulesDetail::IsEquippedCrossbowItem(right.Type) && left.Type == ITEM_BOLT)
    {
        return BOWTYPE_CROSSBOW;
    }

    return BOWTYPE_NONE;
}

bool SessionGameplayUnit::IsEquipedWing() const
{
    const ITEM &equippedWing = CharacterMachine->Equipment[EQUIPMENT_WING];
    return CharacterRulesDetail::IsWingType(equippedWing.Type);
}

void SessionGameplayUnit::GetMagicSkillDamage(int iType, int *piMinDamage, int *piMaxDamage) const
{
    if (AT_SKILL_SUMMON_EXPLOSION <= iType && iType <= AT_SKILL_SUMMON_REQUIEM)
    {
        *piMinDamage = CharacterMachine->Character.MagicDamageMin;
        *piMaxDamage = CharacterMachine->Character.MagicDamageMax;
        return;
    }

    SKILL_ATTRIBUTE *p = &SkillAttribute[iType];

    int Damage = p->Damage;

    *piMinDamage = CharacterMachine->Character.MagicDamageMin + Damage;
    *piMaxDamage = CharacterMachine->Character.MagicDamageMax + Damage + Damage / 2;

    Damage = 0;
    g_csItemOption.PlusSpecial((WORD *)&Damage, AT_SET_OPTION_IMPROVE_MAGIC_POWER);
    if (Damage != 0)
    {
        float fratio = 1.f + (float)Damage / 100.f;
        *piMinDamage *= fratio;
        *piMaxDamage *= fratio;
    }

    Damage = 0;
    g_csItemOption.PlusMastery(&Damage, p->MasteryType);
    g_csItemOption.PlusSpecial((WORD *)&Damage, AT_SET_OPTION_IMPROVE_SKILL_ATTACK);
    *piMinDamage += Damage;
    *piMaxDamage += Damage;
}

void SessionGameplayUnit::GetCurseSkillDamage(int iType, int *piMinDamage, int *piMaxDamage) const
{
    if (CLASS_SUMMONER != gCharacterManager.GetBaseClass(CharacterMachine->Character.Class))
        return;

    if (AT_SKILL_SUMMON_EXPLOSION <= iType && iType <= AT_SKILL_SUMMON_REQUIEM)
    {
        SKILL_ATTRIBUTE *p = &SkillAttribute[iType];
        *piMinDamage = CharacterMachine->Character.CurseDamageMin + p->Damage;
        *piMaxDamage = CharacterMachine->Character.CurseDamageMax + p->Damage + p->Damage / 2;
    }
    else
    {
        *piMinDamage = CharacterMachine->Character.CurseDamageMin;
        *piMaxDamage = CharacterMachine->Character.CurseDamageMax;
    }
}

void SessionGameplayUnit::GetSkillDamage(int iType, int *piMinDamage, int *piMaxDamage) const
{
    SKILL_ATTRIBUTE *p = &SkillAttribute[iType];

    int Damage = p->Damage;

    *piMinDamage = Damage;
    *piMaxDamage = Damage + Damage / 2;

    Damage = 0;
    g_csItemOption.PlusMastery(&Damage, p->MasteryType);
    g_csItemOption.PlusSpecial((WORD *)&Damage, AT_SET_OPTION_IMPROVE_SKILL_ATTACK);
    *piMinDamage += Damage;
    *piMaxDamage += Damage;
}

bool SessionLegacyCalls::IsBattleCastleStart()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().IsBattleCastleStart();
}
bool SessionLegacyCalls::InBattleCastle2(vec3_t position)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().InBattleCastle2(position);
}
bool SessionLegacyCalls::InBattleCastle3(vec3_t position)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().InBattleCastle3(position);
}
void SessionLegacyCalls::SetBattleCastleStart(bool result)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().SetBattleCastleStart(result);
}
bool SessionLegacyCalls::InArea(float x, float y, vec3_t position, float range)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().InArea(x, y, position, range);
}
void SessionLegacyCalls::CollisionHeroCharacter(vec3_t position, float range, int animationType)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().CollisionHeroCharacter(
        position, range, animationType);
}
void SessionLegacyCalls::CollisionTempCharacter(vec3_t position, float range, int animationType)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().CollisionTempCharacter(
        position, range, animationType);
}
bool SessionLegacyCalls::CalcDistanceChrToChr(OBJECT *object, BYTE type, float range)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().CalcDistanceChrToChr(
        object, type, range);
}
void SessionLegacyCalls::SetCastleGate_Attribute(int x, int y, BYTE operation, bool allClear)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().SetCastleGate_Attribute(
        x, y, operation, allClear);
}
void SessionLegacyCalls::SetBuildTimeLocation(OBJECT *object)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().SetBuildTimeLocation(object);
}

bool SessionLegacyCalls::SettingBattleFormation(CHARACTER *character, eBuffState state)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().SettingBattleFormation(
        character, state);
}
bool SessionLegacyCalls::GetGuildMaster(CHARACTER *character)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().GetGuildMaster(character);
}
void SessionLegacyCalls::SettingBattleKing(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().SettingBattleKing(character);
}
void SessionLegacyCalls::DeleteBattleFormation(CHARACTER *character, eBuffState state)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().DeleteBattleFormation(character,
                                                                                    state);
}
void SessionLegacyCalls::ChangeBattleFormation(wchar_t *guildName, bool effect)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().ChangeBattleFormation(guildName,
                                                                                    effect);
}
void SessionLegacyCalls::DeleteTmpCharacter()
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().DeleteTmpCharacter();
}
void SessionLegacyCalls::StartFog(vec3_t color)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().StartFog(color);
}
void SessionLegacyCalls::EndFog()
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().EndFog();
}

bool SessionLegacyCalls::CreateFireSnuff(PARTICLE *particle)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().CreateFireSnuff(particle);
}
void SessionLegacyCalls::SetAttackDefenseObjectType(OBJECT *object)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().SetAttackDefenseObjectType(object);
}
void SessionLegacyCalls::MoveFlyBigStone(OBJECT *object)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().MoveFlyBigStone(object);
}
bool SessionLegacyCalls::SettingBattleCastleMonsterLinkBone(CHARACTER *character, int type)
{
    return sessionKeeper_.Gameplay()
        ->TheMapProcess()
        .BattleCastle()
        .SettingBattleCastleMonsterLinkBone(character, type);
}
bool SessionLegacyCalls::StopBattleCastleMonster(CHARACTER *character, OBJECT *object)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().StopBattleCastleMonster(
        character, object);
}
void SessionLegacyCalls::InitGateAttribute()
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().InitGateAttribute();
}
void SessionLegacyCalls::EmitMonsterHitEffect(OBJECT *object)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().EmitMonsterHitEffect(object);
}
bool SessionLegacyCalls::CollisionEffectToObject(OBJECT *effect, float range, float rangeZ,
                                                 bool collisionGround, bool realCollision)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().CollisionEffectToObject(
        effect, range, rangeZ, collisionGround, realCollision);
}

void SessionLegacyCalls::CreateGuardStoneHealingVisual(CHARACTER *character, float range)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().CreateGuardStoneHealingVisual(
        character, range);
}

void SessionLegacyCalls::AddWaterWave(int x, int y, int range, int height)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().AddWaterWave(x, y, range, height);
}

void SessionLegacyCalls::SettingHellasColor()
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().SettingHellasColor();
}
BYTE SessionLegacyCalls::GetHellasLevel(CLASS_TYPE characterClass, int level)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().GetHellasLevel(characterClass,
                                                                              level);
}
bool SessionLegacyCalls::EnableKalima(CLASS_TYPE characterClass, int level, int itemLevel)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().EnableKalima(characterClass, level,
                                                                            itemLevel);
}
bool SessionLegacyCalls::GetUseLostMap(bool drawAlert)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().GetUseLostMap(drawAlert);
}

void SessionLegacyCalls::CheckGrass(OBJECT *object)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().CheckGrass(object);
}
int SessionLegacyCalls::CreateBigMon(OBJECT *object)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().CreateBigMon(object);
}
void SessionLegacyCalls::CreateMonsterSkill_ReduceDef(OBJECT *object, int attackTime, BYTE time,
                                                      float height)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().CreateMonsterSkill_ReduceDef(
        object, attackTime, time, height);
}
void SessionLegacyCalls::CreateMonsterSkill_Poison(OBJECT *object, int attackTime, BYTE time)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().CreateMonsterSkill_Poison(object,
                                                                                  attackTime, time);
}
void SessionLegacyCalls::CreateMonsterSkill_Summon(OBJECT *object, int attackTime, BYTE time)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().CreateMonsterSkill_Summon(object,
                                                                                  attackTime, time);
}
void SessionLegacyCalls::SetActionDestroy_Def(OBJECT *object)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().SetActionDestroy_Def(object);
}
bool SessionLegacyCalls::SettingHellasMonsterLinkBone(CHARACTER *character, int type)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().SettingHellasMonsterLinkBone(
        character, type);
}
void SessionLegacyCalls::MonsterMoveWaterSmoke(OBJECT *object)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().MonsterMoveWaterSmoke(object);
}
void SessionLegacyCalls::MonsterDieWaterSmoke(OBJECT *object)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().MonsterDieWaterSmoke(object);
}

void SessionLegacyCalls::PushObject(vec3_t pushPosition, vec3_t position, float power, vec3_t angle)
{
    sessionKeeper_.Gameplay()->PushObject(pushPosition, position, power, angle);
}

void SessionLegacyCalls::SetAction(OBJECT *object, int action, bool blending)
{
    sessionKeeper_.Gameplay()->SetAction(object, action, blending);
}
void SessionLegacyCalls::SetAction_Fenrir_Skill(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->SetAction_Fenrir_Skill(character, object);
}
void SessionLegacyCalls::SetAction_Fenrir_Damage(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->SetAction_Fenrir_Damage(character, object);
}
void SessionLegacyCalls::SetAction_Fenrir_Run(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->SetAction_Fenrir_Run(character, object);
}
void SessionLegacyCalls::SetAction_Fenrir_Walk(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Gameplay()->SetAction_Fenrir_Walk(character, object);
}

void SessionLegacyCalls::InitPath()
{
    sessionKeeper_.Gameplay()->InitPath();
}

bool SessionLegacyCalls::PathFinding2(int sx, int sy, int tx, int ty, PATH_t *pathState,
                                      float distance, int defaultWall)
{
    return sessionKeeper_.Gameplay()->PathFinding2(sx, sy, tx, ty, pathState, distance,
                                                   defaultWall);
}

void SessionLegacyCalls::Damage(vec_t *sourcePosition, CHARACTER *target, float attackRange,
                                int attackPoint, bool hit)
{
    sessionKeeper_.Gameplay()->Damage(sourcePosition, target, attackRange, attackPoint, hit);
}

void SessionLegacyCalls::SendRequestUse(int index, int target, bool addPoints) const
{
    sessionKeeper_.Gameplay()->SendRequestUse(index, target, addPoints);
}

bool SessionLegacyCalls::SendRequestEquipmentItem(STORAGE_TYPE sourceType, int sourceIndex,
                                                  ITEM *item, STORAGE_TYPE destinationType,
                                                  int destinationIndex) const
{
    return sessionKeeper_.Gameplay()->SendRequestEquipmentItem(sourceType, sourceIndex, item,
                                                               destinationType, destinationIndex);
}

void SessionLegacyCalls::ComputeItemInfo(int helpItem)
{
    sessionKeeper_.Renderer()->ComputeItemInfo(helpItem);
}

bool SessionLegacyCalls::GetAttackDamage(int *minimumDamage, int *maximumDamage) const
{
    return sessionKeeper_.Gameplay()->GetAttackDamage(minimumDamage, maximumDamage);
}

int SessionLegacyCalls::GetScreenWidth()
{
    return sessionKeeper_.Ui()->GetScreenWidth();
}

bool SessionLegacyCalls::IsHighValueItem(ITEM *item) const
{
    return sessionKeeper_.GameData()->IsHighValueItem(item);
}

bool SessionLegacyCalls::IsTradeBan(ITEM *item)
{
    return sessionKeeper_.GameData()->IsTradeBan(item);
}

bool SessionLegacyCalls::IsRepairBan(ITEM *item) const
{
    return sessionKeeper_.GameData()->IsRepairBan(item);
}

std::wstring SessionLegacyCalls::GetItemDisplayName(ITEM *item)
{
    return sessionKeeper_.GameData()->GetItemDisplayName(item);
}

void SessionLegacyCalls::InitPartyList()
{
    sessionKeeper_.Ui()->InitPartyList();
}

void SessionLegacyCalls::MoveServerDivisionInventory()
{
    sessionKeeper_.Ui()->MoveServerDivisionInventory();
}

void SessionLegacyCalls::HideKeyPad()
{
    sessionKeeper_.Renderer()->HideKeyPad();
}

int SessionLegacyCalls::CheckMouseOnKeyPad()
{
    return sessionKeeper_.Renderer()->CheckMouseOnKeyPad();
}

void SessionLegacyCalls::ClosePersonalShop()
{
    sessionKeeper_.Ui()->ClosePersonalShop();
}

void SessionLegacyCalls::ClearPersonalShop()
{
    sessionKeeper_.Ui()->ClearPersonalShop();
}

bool SessionLegacyCalls::IsExistUndecidedPrice()
{
    return sessionKeeper_.Ui()->IsExistUndecidedPrice();
}

bool SessionLegacyCalls::IsStrifeMap(int nMapIndex)
{
    return sessionKeeper_.ServerListManagerObject().IsStrifeMap(nMapIndex);
}

bool SessionLegacyCalls::CreateGuildMark(int markIndex, bool blend)
{
    return sessionKeeper_.Renderer()->CreateGuildMark(markIndex, blend);
}

void SessionLegacyCalls::CreateCastleMark(int type, BYTE *buffer, bool blend)
{
    sessionKeeper_.Renderer()->CreateCastleMark(type, buffer, blend);
}

BYTE SessionLegacyCalls::CaculateFreeTicketLevel(int type) const
{
    return sessionKeeper_.Gameplay()->CaculateFreeTicketLevel(type);
}

void SessionLegacyCalls::MovePersonalShop()
{
    sessionKeeper_.Ui()->MovePersonalShop();
}
void SessionLegacyCalls::OpenPersonalShopMsgWnd(int messageType)
{
    sessionKeeper_.Ui()->OpenPersonalShopMsgWnd(messageType);
}
bool SessionLegacyCalls::IsCorrectShopTitle(const wchar_t *shopTitle)
{
    return sessionKeeper_.Ui()->IsCorrectShopTitle(shopTitle);
}

void SessionLegacyCalls::PruneGroundItemLabelCache(DWORD currentTick)
{
    sessionKeeper_.Renderer()->PruneGroundItemLabelCache(currentTick);
}
void SessionLegacyCalls::SetGroundItemLabelBuildBudget(int buildBudget)
{
    sessionKeeper_.Renderer()->SetGroundItemLabelBuildBudget(buildBudget);
}

void SessionLegacyCalls::BuildGroundItemLabelDescriptor(
    OBJECT *object, ITEM *item, ItemRulesDetail::GroundItemLabelDescriptor &descriptor)
{
    sessionKeeper_.Renderer()->BuildGroundItemLabelDescriptor(object, item, descriptor);
}
void SessionLegacyCalls::ApplyGroundItemLabelDescriptor(
    const ItemRulesDetail::GroundItemLabelDescriptor &descriptor)
{
    return sessionKeeper_.Renderer()->ApplyGroundItemLabelDescriptor(descriptor);
} // OMF-00541
bool SessionLegacyCalls::CreateGroundItemLabelTexture(
    const ItemRulesDetail::GroundItemLabelDescriptor &descriptor,
    GroundItemLabelCacheEntry &cacheEntry)
{
    return sessionKeeper_.Renderer()->CreateGroundItemLabelTexture(descriptor, cacheEntry);
} // OMF-00542

void SessionLegacyCalls::SetActionObject(int world, int type, int lifeTime, int velocity)
{
    sessionKeeper_.Gameplay()->SetActionObject(world, type, lifeTime, velocity);
}

void SessionLegacyCalls::ActionObject(OBJECT *object)
{
    sessionKeeper_.Gameplay()->ActionObject(object);
}

void SessionLegacyCalls::ItemObjectAttribute(OBJECT *object)
{
    sessionKeeper_.Gameplay()->ItemObjectAttribute(object);
}
void SessionLegacyCalls::HandleItemFalling(OBJECT *object)
{
    sessionKeeper_.Gameplay()->HandleItemFalling(object);
}
void SessionLegacyCalls::MoveItems()
{
    sessionKeeper_.Gameplay()->MoveItems();
}

void SessionLegacyCalls::CreateItemDrop(ITEM_t *item, ItemCreationParams params, vec_t *position,
                                        bool isFreshDrop)
{
    sessionKeeper_.Gameplay()->CreateItemDrop(item, params, position, isFreshDrop);
}
void SessionLegacyCalls::CreateMoneyDrop(ITEM_t *item, int amount, vec_t *position,
                                         bool isFreshDrop)
{
    sessionKeeper_.Gameplay()->CreateMoneyDrop(item, amount, position, isFreshDrop);
}
void SessionLegacyCalls::CreateShiny(OBJECT *object)
{
    sessionKeeper_.Gameplay()->CreateShiny(object);
}

void SessionLegacyCalls::CreateOperate(OBJECT *owner)
{
    sessionKeeper_.Gameplay()->CreateOperate(owner);
}

void SessionLegacyCalls::DeleteObjectTile(int x, int y)
{
    sessionKeeper_.Gameplay()->DeleteObjectTile(x, y);
}

void SessionLegacyCalls::ClearItems()
{
    sessionKeeper_.Gameplay()->ClearItems();
}

void SessionLegacyCalls::HandleItemOnGround(OBJECT *o)
{
    return sessionKeeper_.Gameplay()->HandleItemOnGround(o);
} // OMF-00633

CSPetSystem *SessionLegacyCalls::ResolvePetSystem(CHARACTER *character)
{
    return sessionKeeper_.Gameplay()->ResolvePetSystem(character);
}

bool SessionLegacyCalls::IsVirtualKeyPressed(int virtualKey)
{
    return sessionKeeper_.Gameplay()->IsVirtualKeyPressed(virtualKey);
}

void SessionLegacyCalls::ClearRightMouseInputState()
{
    sessionKeeper_.Gameplay()->ClearRightMouseInputState();
}

void SessionLegacyCalls::InitPetManager()
{
    sessionKeeper_.Gameplay()->InitPetManager();
}
void SessionLegacyCalls::MovePet(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->MovePet(character);
}

bool SessionLegacyCalls::SelectPetCommand()
{
    return sessionKeeper_.Gameplay()->SelectPetCommand();
}

void SessionLegacyCalls::SetPetCommand(CHARACTER *character, int key, std::uint8_t command)
{
    sessionKeeper_.Gameplay()->SetPetCommand(character, key, command);
}
void SessionLegacyCalls::SetAttack(CHARACTER *character, int key, int attackType)
{
    sessionKeeper_.Gameplay()->SetAttack(character, key, attackType);
}

void SessionLegacyCalls::DeletePet(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->DeletePet(character);
}

void SessionLegacyCalls::InitItemBackup()
{
    sessionKeeper_.Gameplay()->InitItemBackup();
}

void SessionLegacyCalls::SetPetInfo(std::uint8_t inventoryType, std::uint8_t inventoryPosition,
                                    PET_INFO *petInfo)
{
    sessionKeeper_.Gameplay()->SetPetInfo(inventoryType, inventoryPosition, petInfo);
}
PET_INFO *SessionLegacyCalls::GetPetInfo(ITEM *item) const
{
    return sessionKeeper_.Gameplay()->GetPetInfo(item);
}
void SessionLegacyCalls::CalcPetInfo(PET_INFO *petInfo)
{
    sessionKeeper_.Gameplay()->CalcPetInfo(petInfo);
}
void SessionLegacyCalls::SetPetItemConvert(ITEM *item, PET_INFO *petInfo) const
{
    sessionKeeper_.Gameplay()->SetPetItemConvert(item, petInfo);
}
std::uint32_t SessionLegacyCalls::GetPetItemValue(PET_INFO *petInfo) const
{
    return sessionKeeper_.Gameplay()->GetPetItemValue(petInfo);
}

bool SessionLegacyCalls::SendPetCommand(CHARACTER *character, int index)
{
    return sessionKeeper_.Gameplay()->SendPetCommand(character, index);
}

void SessionLegacyCalls::CreatePetDarkSpirit(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->CreatePetDarkSpirit(character);
}

void SessionLegacyCalls::CreatePetDarkSpirit_Now(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->CreatePetDarkSpirit_Now(character);
}
void SessionLegacyCalls::MovePetCommand(CHARACTER *c)
{
    return sessionKeeper_.Ui()->MovePetCommand(c);
} // OMF-00827

void SessionLegacyCalls::ChangeChaosCastleUnit(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->TheMapProcess().ChaosCastle().ChangeChaosCastleUnit(character);
}

const PkFieldDetail::MonsterDefinition *SessionLegacyCalls::FindMonsterDefinition(int monsterType)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().PKField().FindMonsterDefinition(monsterType);
}

BOOL SessionLegacyCalls::IsCorrectSkillType(INT skillSequence, eTypeSkill skillType)
{
    return sessionKeeper_.GameData()->IsCorrectSkillType(skillSequence, skillType);
}

BOOL SessionLegacyCalls::IsCorrectSkillType_FrendlySkill(INT skillSequence)
{
    return sessionKeeper_.GameData()->IsCorrectSkillType_FrendlySkill(skillSequence);
}

BOOL SessionLegacyCalls::IsCorrectSkillType_Buff(INT skillSequence)
{
    return sessionKeeper_.GameData()->IsCorrectSkillType_Buff(skillSequence);
}

BOOL SessionLegacyCalls::IsCorrectSkillType_DeBuff(INT skillSequence)
{
    return sessionKeeper_.GameData()->IsCorrectSkillType_DeBuff(skillSequence);
}

BOOL SessionLegacyCalls::IsCorrectSkillType_CommonAttack(INT skillSequence)
{
    return sessionKeeper_.GameData()->IsCorrectSkillType_CommonAttack(skillSequence);
}

void SessionLegacyCalls::PrintItem(wchar_t *fileName) const
{
    sessionKeeper_.GameData()->PrintItem(fileName);
}

int SessionLegacyCalls::GetExcellentAddValue(ITEM *item) const
{
    return sessionKeeper_.GameData()->GetExcellentAddValue(item);
}
void SessionLegacyCalls::CalcDamageMin(ITEM *item, ITEM_ATTRIBUTE *attribute,
                                       int excellentAddValue) const
{
    sessionKeeper_.GameData()->CalcDamageMin(item, attribute, excellentAddValue);
}
void SessionLegacyCalls::CalcDamageMax(ITEM *item, ITEM_ATTRIBUTE *attribute,
                                       int excellentAddValue) const
{
    sessionKeeper_.GameData()->CalcDamageMax(item, attribute, excellentAddValue);
}
void SessionLegacyCalls::CalcMagicPower(ITEM *item, ITEM_ATTRIBUTE *attribute,
                                        int excellentAddValue) const
{
    sessionKeeper_.GameData()->CalcMagicPower(item, attribute, excellentAddValue);
}
void SessionLegacyCalls::CalcSuccessfulBlocking(ITEM *item, ITEM_ATTRIBUTE *attribute) const
{
    sessionKeeper_.GameData()->CalcSuccessfulBlocking(item, attribute);
}
void SessionLegacyCalls::CalcDefense(ITEM *item, ITEM_ATTRIBUTE *attribute) const
{
    sessionKeeper_.GameData()->CalcDefense(item, attribute);
}
void SessionLegacyCalls::CalcRequirements(ITEM *item, ITEM_ATTRIBUTE *attribute) const
{
    sessionKeeper_.GameData()->CalcRequirements(item, attribute);
}
void SessionLegacyCalls::CalcWingOptions(ITEM *item) const
{
    sessionKeeper_.GameData()->CalcWingOptions(item);
}
void SessionLegacyCalls::CalcExcellentOptions(ITEM *item) const
{
    sessionKeeper_.GameData()->CalcExcellentOptions(item);
}
void SessionLegacyCalls::CalcPartType(ITEM *item) const
{
    sessionKeeper_.GameData()->CalcPartType(item);
}
void SessionLegacyCalls::SetItemAttributes(ITEM *item) const
{
    sessionKeeper_.GameData()->SetItemAttributes(item);
}

int64_t SessionLegacyCalls::ItemValue(ITEM *item, int goldType) const
{
    return sessionKeeper_.GameData()->ItemValue(item, goldType);
}

bool SessionLegacyCalls::IsRequireEquipItem(ITEM *item)
{
    return sessionKeeper_.GameData()->IsRequireEquipItem(item);
}

void SessionLegacyCalls::PlusSpecial(WORD *value, int special, ITEM *item)
{
    sessionKeeper_.GameData()->PlusSpecial(value, special, item);
}

void SessionLegacyCalls::PlusSpecialPercent(WORD *value, int special, ITEM *item, WORD percent)
{
    sessionKeeper_.GameData()->PlusSpecialPercent(value, special, item, percent);
}

void SessionLegacyCalls::PlusSpecialPercent2(WORD *value, int special, ITEM *item)
{
    sessionKeeper_.GameData()->PlusSpecialPercent2(value, special, item);
}

WORD SessionLegacyCalls::ItemDefense(ITEM *item)
{
    return sessionKeeper_.GameData()->ItemDefense(item);
}

WORD SessionLegacyCalls::ItemMagicDefense(ITEM *item)
{
    return sessionKeeper_.GameData()->ItemMagicDefense(item);
}

WORD SessionLegacyCalls::ItemWalkSpeed(ITEM *item)
{
    return sessionKeeper_.GameData()->ItemWalkSpeed(item);
}

const wchar_t *SessionLegacyCalls::getMonsterName(int type) const
{
    return sessionKeeper_.GameData()->getMonsterName(type);
}
void SessionLegacyCalls::MonsterConvert(MONSTER *monster, int level)
{
    sessionKeeper_.GameData()->MonsterConvert(monster, level);
}
// Terrain ���� �Լ�

// Compute FrustrumBound{Min,Max}{X,Y} from the current FrustrumX/Y/Count hull.
// Snaps to a TERRAIN_ITERATION_TILE grid and clamps to valid terrain range.

// Build a CW convex hull from points projected to tile-space XY, storing into
// FrustrumX/Y/Count, then compute iteration bounds.

// Expand CW convex hull outward by `offset` tiles.
// Compensates for TestFrustrum2D only checking tile centers — tiles at the hull
// boundary whose centers are just outside would otherwise be culled even though
// part of the tile is visible. Expanding by ~1 tile ensures full coverage.

extern void RenderCharactersClient();

bool SessionLegacyCalls::SaveTerrainAttribute(wchar_t *fileName, int mapNumber)
{
    return sessionKeeper_.Renderer()->SaveTerrainAttribute(fileName, mapNumber);
}
void SessionLegacyCalls::SetTerrainWaterState(std::list<int> &terrainIndex, int state)
{
    sessionKeeper_.Renderer()->SetTerrainWaterState(terrainIndex, state);
}
void SessionLegacyCalls::InitTerrainMappingLayer()
{
    sessionKeeper_.Renderer()->InitTerrainMappingLayer();
}
void SessionLegacyCalls::AddTerrainAttribute(int x, int y, BYTE attribute)
{
    sessionKeeper_.Gameplay()->AddTerrainAttribute(x, y, attribute);
}
void SessionLegacyCalls::SubTerrainAttribute(int x, int y, BYTE attribute)
{
    sessionKeeper_.Gameplay()->SubTerrainAttribute(x, y, attribute);
}
void SessionLegacyCalls::AddTerrainAttributeRange(int x, int y, int width, int height,
                                                  BYTE attribute, BYTE add)
{
    sessionKeeper_.Gameplay()->AddTerrainAttributeRange(x, y, width, height, attribute, add);
}
bool SessionLegacyCalls::SaveTerrainMapping(wchar_t *fileName, int mapNumber)
{
    return sessionKeeper_.Renderer()->SaveTerrainMapping(fileName, mapNumber);
}
void SessionLegacyCalls::CreateTerrainNormal()
{
    sessionKeeper_.Renderer()->CreateTerrainNormal();
}
void SessionLegacyCalls::CreateTerrainNormal_Part(int x, int y)
{
    sessionKeeper_.Renderer()->CreateTerrainNormal_Part(x, y);
}
void SessionLegacyCalls::CreateTerrainLight_Part(int x, int y)
{
    sessionKeeper_.Renderer()->CreateTerrainLight_Part(x, y);
}
void SessionLegacyCalls::SaveTerrainLight(wchar_t *fileName)
{
    sessionKeeper_.Renderer()->SaveTerrainLight(fileName);
}
float SessionLegacyCalls::RequestTerrainHeight(float x, float y)
{
    return sessionKeeper_.Gameplay()->RequestTerrainHeight(x, y);
}
void SessionLegacyCalls::RequestTerrainNormal(float x, float y, vec3_t normal)
{
    sessionKeeper_.Gameplay()->RequestTerrainNormal(x, y, normal);
}
void SessionLegacyCalls::RequestTerrainLight(float x, float y, vec3_t light)
{
    sessionKeeper_.Gameplay()->RequestTerrainLight(x, y, light);
}

void SessionLegacyCalls::ComputeIterationBoundsFromHull()
{
    sessionKeeper_.Visual()->ComputeIterationBoundsFromHull();
}

void SessionLegacyCalls::CreateSun()
{
    sessionKeeper_.Renderer()->CreateSun();
}
BYTE SessionLegacyCalls::TERRAIN_ATTRIBUTE(float x, float y)
{
    return sessionKeeper_.Gameplay()->TERRAIN_ATTRIBUTE(x, y);
}
void SessionLegacyCalls::CreateTerrainLight()
{
    sessionKeeper_.Renderer()->CreateTerrainLight();
}
void SessionLegacyCalls::SaveTerrainHeight(wchar_t *fileName)
{
    sessionKeeper_.Renderer()->SaveTerrainHeight(fileName);
}

void SessionLegacyCalls::AddTerrainLight(float x, float y, vec3_t light, int range, vec3_t *buffer)
{
    if (buffer == PrimaryTerrainLight)
    {
        sessionKeeper_.TerrainStorage().dynamicLightActive = true;
        sessionKeeper_.TerrainStorage().dynamicLightDirty = true;
        const int centerX = static_cast<int>(x / TERRAIN_SCALE);
        const int centerY = static_cast<int>(y / TERRAIN_SCALE);
        bool *const blocks = sessionKeeper_.TerrainStorage().TerrainDynamicLightBlocks;
        constexpr int BlockSize = 4;
        const int firstY = (centerY - range - 1) & ~(BlockSize - 1);
        const int firstX = (centerX - range - 1) & ~(BlockSize - 1);
        for (int tileY = firstY; tileY <= centerY + range; tileY += BlockSize)
            for (int tileX = firstX; tileX <= centerX + range; tileX += BlockSize)
                blocks[((tileY & TERRAIN_SIZE_MASK) / BlockSize) * 64 +
                       (tileX & TERRAIN_SIZE_MASK) / BlockSize] = true;
    }
    ::AddTerrainLight(x, y, light, range, buffer);
}

void SessionLegacyCalls::InstallTerrainMapping(const WorldFileData &data)
{
    sessionKeeper_.Renderer()->InstallTerrainMapping(data);
}
void SessionLegacyCalls::InstallTerrainAttributes(const WorldFileData &data)
{
    sessionKeeper_.Renderer()->InstallTerrainAttributes(data);
}
void SessionLegacyCalls::InstallTerrainLight(const WorldImageData &data)
{
    sessionKeeper_.Renderer()->InstallTerrainLight(data);
}
void SessionLegacyCalls::CreateTerrain(const WorldImageData &data, bool extended)
{
    sessionKeeper_.Renderer()->CreateTerrain(data, extended);
}

void SessionLegacyCalls::DeleteBoids()
{
    sessionKeeper_.Gameplay()->DeleteBoids();
}
void SessionLegacyCalls::MoveBoids()
{
    sessionKeeper_.Gameplay()->MoveBoids();
}

void SessionLegacyCalls::AdvanceBoidVisual(OBJECT &object)
{
    sessionKeeper_.Visual()->AdvanceBoidVisual(object);
}

bool SessionLegacyCalls::CreateMountSub(int type, vec3_t position, OBJECT *owner, OBJECT *object,
                                        int subType, int linkBone)
{
    return sessionKeeper_.Gameplay()->CreateMountSub(type, position, owner, object, subType,
                                                     linkBone);
}
void SessionLegacyCalls::DeleteMount(OBJECT *owner)
{
    sessionKeeper_.Gameplay()->DeleteMount(owner);
}
void SessionLegacyCalls::CreateMount(int type, vec3_t position, OBJECT *owner, int subType,
                                     int linkBone)
{
    sessionKeeper_.Gameplay()->CreateMount(type, position, owner, subType, linkBone);
}
bool SessionLegacyCalls::MoveMount(OBJECT *object, bool forceRender, CHARACTER *owner,
                                   AnimationPoseSample *pose)
{
    return sessionKeeper_.Gameplay()->MoveMount(object, forceRender, owner, pose);
}
void SessionLegacyCalls::MoveMounts()
{
    sessionKeeper_.Gameplay()->MoveMounts();
}
void SessionLegacyCalls::MoveHeavenBug(OBJECT *object, int index)
{
    sessionKeeper_.Gameplay()->MoveHeavenBug(object, index,
                                             sessionKeeper_.Gameplay()->FPS_ANIMATION_FACTOR,
                                             sessionKeeper_.Gameplay()->WorldTime);
}
void SessionLegacyCalls::MoveBoidGroup(OBJECT *object, int index)
{
    CharacterMotionDetail::AdvanceAmbientIntervals(
        *object, sessionKeeper_.Gameplay()->FPS_ANIMATION_FACTOR,
        [&](float frames, bool refresh, float) {
            sessionKeeper_.Gameplay()->MoveBoidGroup(object, index, frames, refresh,
                                                     object->Velocity);
        });
}
void SessionLegacyCalls::MoveFishs()
{
    sessionKeeper_.Gameplay()->MoveFishs();
}

void SessionLegacyCalls::MoveButterFly(OBJECT *object)
{
    CharacterMotionDetail::AdvanceAmbientIntervals(
        *object, sessionKeeper_.Gameplay()->FPS_ANIMATION_FACTOR,
        [&](float frames, bool refresh, float) {
            sessionKeeper_.Gameplay()->MoveButterFly(object, frames, refresh);
        });
}

void SessionLegacyCalls::MoveBird(OBJECT *object)
{
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    CharacterMotionDetail::AdvanceAmbientIntervals(
        *object, sessionKeeper_.Gameplay()->FPS_ANIMATION_FACTOR,
        [&](float frames, bool refresh, float remaining) {
            sessionKeeper_.Gameplay()->MoveBird(
                object, frames, sessionKeeper_.Gameplay()->WorldTime - remaining * milliseconds,
                refresh);
        });
}

void SessionLegacyCalls::MoveEagle(OBJECT *object)
{
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    auto &gameplay = *sessionKeeper_.Gameplay();
    auto &model = sessionKeeper_.ModelPoolObject()[object->Type];
    model.CurrentAction = object->CurrentAction;
    object->MotionTrace.Begin(gameplay.WorldTime, gameplay.FPS_ANIMATION_FACTOR, object->Position);
    CharacterMotionDetail::AdvanceAmbientIntervals(
        *object, sessionKeeper_.Gameplay()->FPS_ANIMATION_FACTOR,
        [&](float frames, bool refresh, float remaining) {
            gameplay.AdvanceEagleAnimation(
                *object, model, frames,
                model.NumActions > 0 ? model.Actions[model.CurrentAction].PlaySpeed : 0.f, refresh);
            sessionKeeper_.Gameplay()->MoveEagle(
                object, frames, sessionKeeper_.Gameplay()->WorldTime - remaining * milliseconds,
                refresh);
        });
}

void SessionLegacyCalls::MoveTornado(OBJECT *object)
{
    CharacterMotionDetail::AdvanceAmbientIntervals(
        *object, sessionKeeper_.Gameplay()->FPS_ANIMATION_FACTOR,
        [&](float frames, bool refresh, float) {
            sessionKeeper_.Gameplay()->MoveTornado(object, frames, refresh);
        });
}

void SessionLegacyCalls::MoveBigMon(OBJECT *object)
{
    const float total = sessionKeeper_.Gameplay()->FPS_ANIMATION_FACTOR;
    CharacterMotionDetail::AdvanceAmbientIntervals(
        *object, total, [&](float frames, bool refresh, float remaining) {
            sessionKeeper_.Gameplay()->TheMapProcess().Hellas().MoveBigMon(
                object, frames, refresh, object->LifeTime - (total - frames - remaining));
        });
}

int SessionLegacyCalls::SearchArrow() const
{
    return sessionKeeper_.Gameplay()->SearchArrow();
}

int SessionLegacyCalls::SearchArrowCount() const
{
    return sessionKeeper_.Gameplay()->SearchArrowCount();
}

bool SessionLegacyCalls::CheckTile(CHARACTER *character, OBJECT *object, float range)
{
    return sessionKeeper_.Gameplay()->CheckTile(character, object, range);
}

bool SessionLegacyCalls::CheckWall(int sx1, int sy1, int sx2, int sy2)
{
    return sessionKeeper_.Gameplay()->CheckWall(sx1, sy1, sx2, sy2);
}

void SessionGameplayUnit::SendCharacterMove(unsigned short Key, float Angle, unsigned char PathNum,
                                            unsigned char *PathX, unsigned char *PathY,
                                            unsigned char TargetX, unsigned char TargetY)
{
    if (PathNum < 1)
        return;

    if (PathNum >= MAX_PATH_FIND)
    {
        PathNum = MAX_PATH_FIND - 1;
    }

    BYTE PathNew[8]{};
    BYTE Dir = 0;
    for (int i = 1; i < PathNum; i++)
    {
        Dir = 0;
        for (int j = 0; j < 8; j++) // loop to find the direction of this step
        {
            if (DirTable[j * 2] == (PathX[i] - PathX[i - 1]) &&
                DirTable[j * 2 + 1] == (PathY[i] - PathY[i - 1]))
            {
                Dir = j;
                break;
            }
        }

        const auto path_index = ((i + 1) / 2) - 1;
        if (i % 2 == 1)
        {
            PathNew[path_index] |= Dir << 4;
        }
        else
        {
            PathNew[path_index] |= Dir;
        }
    }

    if (PathNum == 1)
    {
        // For example, it's 1 when the character stops walking by starting a skill.
        // Then we just send the direction of the character and no steps.
        Dir = ((BYTE)((Angle + 22.5f) / 360.f * 8.f + 1.f) % 8);
    }

    SocketClient->ToGameServer()->SendWalkRequest(PathX[0], PathY[0], PathNum - 1, Dir, PathNew,
                                                  PathNum / 2);
}

void SessionLegacyCalls::SendCharacterMove(unsigned short key, float angle, unsigned char pathNum,
                                           unsigned char *pathX, unsigned char *pathY,
                                           unsigned char targetX, unsigned char targetY)
{
    sessionKeeper_.Gameplay()->SendCharacterMove(key, angle, pathNum, pathX, pathY, targetX,
                                                 targetY);
}

void SessionGameplayUnit::LetHeroStop(CHARACTER *c, BOOL bSetMovementFalse)
{
    BYTE PathX[1];
    BYTE PathY[1];
    PathX[0] = (Hero->PositionX);
    PathY[0] = (Hero->PositionY);

    SendCharacterMove(Hero->Key, Hero->Object.Angle[2], 1, PathX, PathY, TargetX, TargetY);

    if (c != NULL && bSetMovementFalse == TRUE)
    {
        c->Movement = false;
    }
}

void SessionLegacyCalls::LetHeroStop(CHARACTER *character, BOOL setMovementFalse)
{
    sessionKeeper_.Gameplay()->LetHeroStop(character, setMovementFalse);
}

void SessionGameplayUnit::SetCharacterPos(CHARACTER *c, BYTE posX, BYTE posY, vec3_t position)
{
    BYTE PathX[1];
    BYTE PathY[1];
    PathX[0] = posX;
    PathY[0] = posY;

    c->PositionX = PathX[0];

    c->PositionY = PathY[0];

    c->Object.EnableBoneMatrix = false;
    VectorCopy(position, c->Object.Position);

    SendCharacterMove(c->Key, c->Object.Angle[2], 1, PathX, PathY, PathX[0], PathY[0]);
}

void SessionLegacyCalls::SetCharacterPos(CHARACTER *character, BYTE positionX, BYTE positionY,
                                         vec3_t position)
{
    sessionKeeper_.Gameplay()->SetCharacterPos(character, positionX, positionY, position);
}

BYTE SessionLegacyCalls::MakeSkillSerialNumber(BYTE *serialNumber)
{
    return sessionKeeper_.Gameplay()->MakeSkillSerialNumber(serialNumber);
}

void SessionLegacyCalls::SendRequestMagic(int type, int key)
{
    sessionKeeper_.Gameplay()->SendRequestMagic(type, key);
}

void SessionLegacyCalls::SendRequestMagicContinue(int type, int x, int y, int angle,
                                                  BYTE destination, BYTE targetPosition,
                                                  WORD targetKey, BYTE *skillSerial)
{
    sessionKeeper_.Gameplay()->SendRequestMagicContinue(type, x, y, angle, destination,
                                                        targetPosition, targetKey, skillSerial);
}

BYTE GetDestValue(int xPos, int yPos, int xDst, int yDst)
{
    int DestX = xDst - xPos;
    int DestY = yDst - yPos;
    if (DestX < -8)
        DestX = -8;
    if (DestX > 7)
        DestX = 7;
    if (DestY < -8)
        DestY = -8;
    if (DestY > 7)
        DestY = 7;
    assert(-8 <= DestX && DestX <= 7);
    assert(-8 <= DestY && DestY <= 7);
    BYTE byValue1 = ((BYTE)(DestX + 8)) << 4;
    BYTE byValue2 = ((BYTE)(DestY + 8)) & 0xf;
    return (byValue1 | byValue2);
}

void SessionGameplayUnit::ReloadArrow()
{
    int Index = SearchArrow();

    if (Index != -1)
    {
        ITEM *rp = NULL;
        ITEM *lp = NULL;

        bool Success = false;

        if (gCharacterManager.GetBaseClass(CharacterAttribute->Class) == CLASS_ELF &&
            sessionKeeper_.GameData()->GetPickedItem() == NULL)
        {
            rp = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT];
            lp = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];
            Success = true;
        }

        if (Success)
        {
            if ((gCharacterManager.GetEquipedBowType(lp) == BOWTYPE_BOW) && (rp->Type == -1))
            {
                ITEM *pItem = sessionKeeper_.GameData()->FindInventoryItemBySlot(Index);
                g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(
                    g_pMyInventory->GetInventoryCtrl(), pItem);
                if (pItem)
                {
                    SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, Index, pItem,
                                             STORAGE_TYPE::INVENTORY, EQUIPMENT_WEAPON_RIGHT);
                }
                sessionKeeper_.GameData()->DeleteInventoryItem(Index);
                g_pSystemLogBox->AddText(I18N::Game::ArrowsReloaded, SEASON3B::TYPE_SYSTEM_MESSAGE);
            }
            else if ((gCharacterManager.GetEquipedBowType(rp) == BOWTYPE_CROSSBOW) &&
                     (lp->Type == -1))
            {
                ITEM *pItem = sessionKeeper_.GameData()->FindInventoryItemBySlot(Index);
                g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(
                    g_pMyInventory->GetInventoryCtrl(), pItem);
                if (pItem)
                {
                    SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, Index, pItem,
                                             STORAGE_TYPE::INVENTORY, EQUIPMENT_WEAPON_LEFT);
                }
                sessionKeeper_.GameData()->DeleteInventoryItem(Index);
                g_pSystemLogBox->AddText(I18N::Game::ArrowsReloaded, SEASON3B::TYPE_SYSTEM_MESSAGE);
            }
        }
    }
    else
    {
        if (g_pSystemLogBox->CheckChatRedundancy(I18N::Game::NoMoreArrows) == FALSE)
        {
            g_pSystemLogBox->AddText(I18N::Game::NoMoreArrows, SEASON3B::TYPE_ERROR_MESSAGE);
        }
    }
}

bool SessionGameplayUnit::CheckArrow()
{
    int Right = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;
    int Left = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;

    if (GetEquipedBowType() == BOWTYPE_CROSSBOW)
    {
        if ((Left != ITEM_BOLT) ||
            (CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Durability <= 0))
        {
            ReloadArrow();
            return false;
        }
    }
    else if (GetEquipedBowType() == BOWTYPE_BOW)
    {
        if ((Right != ITEM_ARROWS) ||
            (CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Durability <= 0))
        {
            ReloadArrow();
            return false;
        }
    }
    return true;
}

void SessionLegacyCalls::ReloadArrow()
{
    sessionKeeper_.Gameplay()->ReloadArrow();
}

bool SessionLegacyCalls::CheckArrow()
{
    return sessionKeeper_.Gameplay()->CheckArrow();
}

void SessionLegacyCalls::SendRequestAction(OBJECT &object, BYTE action)
{
    sessionKeeper_.Gameplay()->SendRequestAction(object, action);
}

void SessionLegacyCalls::CloseNPCGMWindow()
{
    sessionKeeper_.Ui()->CloseNPCGMWindow();
}

bool CheckMacroLimit(wchar_t *Text)
{
    wchar_t string[256];
    int length;

    memcpy(string, Text + 3, sizeof(char) * (256 - 2));
    length = wcslen(I18N::Game::Exchange);
    if (wcscmp(string, I18N::Game::Exchange) == 0 || wcscmp(string, I18N::Game::Trade259) == 0 ||
        wcsicmp(string, L"/trade") == 0)
    {
        return true;
    }
    if (wcscmp(string, I18N::Game::Party256) == 0 || wcsicmp(string, L"/party") == 0 ||
        wcsicmp(string, L"/pt") == 0)
    {
        return true;
    }
    if (wcscmp(string, I18N::Game::Guild254) == 0 || wcsicmp(string, L"/guild") == 0)
    {
        return true;
    }
    if (wcscmp(string, I18N::Game::Battle) == 0 || wcsicmp(string, L"/GuildWar") == 0)
    {
        return true;
    }
    if (wcscmp(string, I18N::Game::BattleSoccer) == 0 || wcsicmp(string, L"/BattleSoccer") == 0)
    {
        return true;
    }

    return false;
}

bool SessionGameplayUnit::CheckCommand(wchar_t *Text, bool bMacroText)
{
#ifdef CSK_DEBUG_MAP_ATTRIBUTE
    if (wcscmp(Text, L"$mapatt on") == 0)
    {
        EditFlag = EDIT_WALL;
        return true;
    }
    if (wcscmp(Text, L"$mapatt off") == 0)
    {
        EditFlag = EDIT_NONE;
        return true;
    }
#endif

    if (g_ConsoleDebug.CheckCommand(Text) == true)
    {
        return true;
    }

    if (bMacroText == false && LogOut == false)
    {
        wchar_t Name[256];
        int iTextSize = 0;
        for (int i = 0; i < 256 && Text[i] != ' ' && Text[i] != '\0'; i++)
        {
            Name[i] = Text[i];
            iTextSize = i;
        }
        Name[iTextSize] = 0;

        if (!g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_STORAGE))
        {
            if (wcscmp(Name, I18N::Game::Exchange) == 0 ||
                wcscmp(Name, I18N::Game::Trade259) == 0 || wcsicmp(Text, L"/trade") == 0)
            {
                if (gMapManager.InChaosCastle() == true)
                {
                    g_pSystemLogBox->AddText(I18N::Game::CanTBeInChaosCastle,
                                             SEASON3B::TYPE_SYSTEM_MESSAGE);

                    return false;
                }

                if (IsStrifeMap(gMapManager.ContextMap()))
                {
                    g_pSystemLogBox->AddText(I18N::Game::CannotApplyInBattleZone,
                                             SEASON3B::TYPE_SYSTEM_MESSAGE);
                    return false;
                }

                int level = CharacterAttribute->Level;

                if (level < TRADELIMITLEVEL)
                {
                    g_pSystemLogBox->AddText(I18N::Game::YouCanUseTheTradeCommandAtCharacterLevel6,
                                             SEASON3B::TYPE_SYSTEM_MESSAGE);
                    return true;
                }

                if (!IsCanTrade() || !MainSceneReady)
                {
                    return true;
                }

                if (CharactersClient.IsValidIndex(SelectedCharacter))
                {
                    CHARACTER *c = &CharactersClient[SelectedCharacter];
                    OBJECT *o = &c->Object;
                    if (o->Kind == KIND_PLAYER && c != Hero &&
                        (o->Type == MODEL_PLAYER || c->Change) &&
                        abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                        abs((c->PositionY) - (Hero->PositionY)) <= 1)
                    {
                        if (IsShopInViewport(c))
                        {
                            g_pSystemLogBox->AddText(I18N::Game::YouCannotTradeRightNow,
                                                     SEASON3B::TYPE_ERROR_MESSAGE);
                            return true;
                        }

                        SocketClient->ToGameServer()->SendTradeRequest(c->Key);
                        wchar_t message[100]{};
                        mu_swprintf(message, I18N::Game::YouHaveRequestedSToTrade, c->ID);
                        g_pSystemLogBox->AddText(message, SEASON3B::TYPE_SYSTEM_MESSAGE);
                    }
                }
                else
                    for (int i = 0; i < CharactersClient.Size(); i++)
                    {
                        if (!CharactersClient.IsValidIndex(i))
                            continue;
                        CHARACTER *c = &CharactersClient[i];
                        OBJECT *o = &c->Object;

                        if (o->Live && o->Kind == KIND_PLAYER && c != Hero &&
                            (o->Type == MODEL_PLAYER || c->Change) &&
                            abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                            abs((c->PositionY) - (Hero->PositionY)) <= 1)
                        {
                            if (IsShopInViewport(c))
                            {
                                g_pSystemLogBox->AddText(I18N::Game::YouCannotTradeRightNow,
                                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                                return true;
                            }

                            BYTE Dir1 = (BYTE)((o->Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                            BYTE Dir2 =
                                (BYTE)((Hero->Object.Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                            if (abs(Dir1 - Dir2) == 4)
                            {
                                SocketClient->ToGameServer()->SendTradeRequest(c->Key);
                                wchar_t message[100]{};
                                mu_swprintf(message, I18N::Game::YouHaveRequestedSToTrade, c->ID);
                                g_pSystemLogBox->AddText(message, SEASON3B::TYPE_SYSTEM_MESSAGE);
                                break;
                            }
                        }
                    }
                return true;
            }
        }

        if (wcscmp(Text, I18N::Game::Firecracker688) == 0)
        {
            return false;
        }

        if (wcscmp(Text, I18N::Game::PersonalStore1117) == 0 ||
            wcsicmp(Text, L"/personalshop") == 0)
        {
            if (gMapManager.InChaosCastle() == true)
            {
                g_pSystemLogBox->AddText(I18N::Game::CanTBeInChaosCastle,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return false;
            }

            int level = CharacterAttribute->Level;
            if (level >= 6)
            {
                g_pNewUISystem->Show(SEASON3B::INTERFACE_MYSHOP_INVENTORY);
            }
            else
            {
                wchar_t szError[48] = L"";
                mu_swprintf(szError, I18N::Game::OnlyAboveLevelDCanUse, 6);
                g_pSystemLogBox->AddText(szError, SEASON3B::TYPE_SYSTEM_MESSAGE);
            }
            return true;
        }
        if (wcsstr(Text, I18N::Game::Buy) != nullptr || wcsstr(Text, L"/purchase") != nullptr)
        {
            if (gMapManager.InChaosCastle() == true)
            {
                g_pSystemLogBox->AddText(I18N::Game::CanTBeInChaosCastle,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return false;
            }

            if (IsStrifeMap(gMapManager.ContextMap()))
            {
                g_pSystemLogBox->AddText(I18N::Game::CannotApplyInBattleZone,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return false;
            }

            if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCSHOP) ||
                g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_STORAGE) ||
                g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_TRADE) ||
                g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MIXINVENTORY) ||
                g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYITEMWND))
            {
                g_pSystemLogBox->AddText(I18N::Game::StoreCanTBeOpened,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return false;
            }
            wchar_t szCmd[24];
            wchar_t szId[MAX_USERNAME_SIZE];
            swscanf(Text, L"%ls %ls", szCmd, szId);

            if (CharactersClient.IsValidIndex(SelectedCharacter))
            {
                CHARACTER *c = &CharactersClient[SelectedCharacter];
                OBJECT *o = &c->Object;
                if (o->Kind == KIND_PLAYER && c != Hero && (o->Type == MODEL_PLAYER || c->Change))
                {
                    SocketClient->ToGameServer()->SendPlayerShopItemListRequest(c->Key, c->ID);
                }
            }
            else
            {
                for (int i = 0; i < CharactersClient.Size(); i++)
                {
                    if (!CharactersClient.IsValidIndex(i))
                        continue;
                    CHARACTER *c = &CharactersClient[i];
                    if (wcslen(szId) > 0 && c->Object.Live)
                    {
                        if (wcscmp(c->ID, szId) == 0)
                        {
                            SocketClient->ToGameServer()->SendPlayerShopItemListRequest(c->Key,
                                                                                        c->ID);
                        }
                    }
                    else
                    {
                        OBJECT *o = &c->Object;
                        if (o->Live && o->Kind == KIND_PLAYER && c != Hero &&
                            (o->Type == MODEL_PLAYER || c->Change))
                        {
                            BYTE Dir1 = (BYTE)((o->Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                            BYTE Dir2 =
                                (BYTE)((Hero->Object.Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                            if (abs(Dir1 - Dir2) == 4)
                            {
                                SocketClient->ToGameServer()->SendPlayerShopItemListRequest(c->Key,
                                                                                            c->ID);
                                break;
                            }
                        }
                    }
                }
            }

            return true;
        }

        if (wcscmp(Text, I18N::Game::ViewStoreOn) == 0)
        {
            ShowShopTitles();
            g_pSystemLogBox->AddText(I18N::Game::CanViewPersonalStoreWindow,
                                     SEASON3B::TYPE_SYSTEM_MESSAGE);
        }

        if (wcscmp(Text, I18N::Game::ViewStoreOff) == 0)
        {
            HideShopTitles();
            g_pSystemLogBox->AddText(I18N::Game::CannotViewPersonalStoreWindow,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
        }
        if (wcscmp(Text, I18N::Game::DuelChallenge) == 0 || wcsicmp(Text, L"/duelstart") == 0)
        {
#ifndef GUILD_WAR_EVENT
            if (gMapManager.InChaosCastle() == true)
            {
                g_pSystemLogBox->AddText(I18N::Game::CanTBeInChaosCastle,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return false;
            }
#endif // UILD_WAR_EVENT
            if (!g_DuelMgr.IsDuelEnabled())
            {
                int iLevel = CharacterAttribute->Level;
                if (iLevel < 30)
                {
                    wchar_t szError[48] = L"";
                    mu_swprintf(szError, I18N::Game::OpenOnlyForLevelDOrHigher, 30);
                    g_pSystemLogBox->AddText(szError, SEASON3B::TYPE_ERROR_MESSAGE);
                    return 3;
                }
                else if (CharactersClient.IsValidIndex(SelectedCharacter))
                {
                    CHARACTER *c = &CharactersClient[SelectedCharacter];
                    OBJECT *o = &c->Object;
                    if (o->Kind == KIND_PLAYER && c != Hero &&
                        (o->Type == MODEL_PLAYER || c->Change) &&
                        abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                        abs((c->PositionY) - (Hero->PositionY)) <= 1)
                    {
                        SocketClient->ToGameServer()->SendDuelStartRequest(c->Key, c->ID);
                    }
                }
                else
                    for (int i = 0; i < CharactersClient.Size(); i++)
                    {
                        if (!CharactersClient.IsValidIndex(i))
                            continue;
                        CHARACTER *c = &CharactersClient[i];
                        OBJECT *o = &c->Object;

                        if (o->Live && o->Kind == KIND_PLAYER && c != Hero &&
                            (o->Type == MODEL_PLAYER || c->Change) &&
                            abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                            abs((c->PositionY) - (Hero->PositionY)) <= 1)
                        {
                            BYTE Dir1 = (BYTE)((o->Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                            BYTE Dir2 =
                                (BYTE)((Hero->Object.Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                            if (abs(Dir1 - Dir2) == 4)
                            {
                                SocketClient->ToGameServer()->SendDuelStartRequest(c->Key, c->ID);
                                break;
                            }
                        }
                    }
            }
            else
            {
                g_pSystemLogBox->AddText(I18N::Game::YouCannotChallengePlayerIsAlreadyInADuel,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
            }
        }
        if (wcscmp(Text, I18N::Game::DuelCancel) == 0 || wcsicmp(Text, L"/duelend") == 0)
        {
#ifndef GUILD_WAR_EVENT
            if (gMapManager.InChaosCastle() == true)
            {
                g_pSystemLogBox->AddText(I18N::Game::CanTBeInChaosCastle,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return false;
            }
#endif // GUILD_WAR_EVENT
            if (g_DuelMgr.IsDuelEnabled())
            {
                SocketClient->ToGameServer()->SendDuelStopRequest();
            }
        }
        if (wcscmp(Text, I18N::Game::Guild254) == 0 || wcsicmp(Text, L"/guild") == 0)
        {
            if (gMapManager.InChaosCastle() == true)
            {
                g_pSystemLogBox->AddText(I18N::Game::CanTBeInChaosCastle,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return false;
            }
            if (Hero->GuildStatus != G_NONE)
            {
                g_pSystemLogBox->AddText(I18N::Game::YouAreAlreadyInAGuild,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return true;
            }

            if (CharactersClient.IsValidIndex(SelectedCharacter))
            {
                CHARACTER *c = &CharactersClient[SelectedCharacter];
                OBJECT *o = &c->Object;
                if (o->Kind == KIND_PLAYER && c != Hero && (o->Type == MODEL_PLAYER || c->Change) &&
                    abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                    abs((c->PositionY) - (Hero->PositionY)) <= 1)
                {
                    GuildPlayerKey = c->Key;
                    SocketClient->ToGameServer()->SendGuildJoinRequest(c->Key);
                    wchar_t Text[100];
                    mu_swprintf(Text, I18N::Game::YouHaveRequestedSToJoinYourGuild, c->ID);
                    g_pSystemLogBox->AddText(Text, SEASON3B::TYPE_SYSTEM_MESSAGE);
                }
            }
            else
                for (int i = 0; i < CharactersClient.Size(); i++)
                {
                    if (!CharactersClient.IsValidIndex(i))
                        continue;
                    CHARACTER *c = &CharactersClient[i];
                    OBJECT *o = &c->Object;

                    if (o->Live && o->Kind == KIND_PLAYER && c != Hero &&
                        (o->Type == MODEL_PLAYER || c->Change) &&
                        abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                        abs((c->PositionY) - (Hero->PositionY)) <= 1)
                    {
                        BYTE Dir1 = (BYTE)((o->Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                        BYTE Dir2 = (BYTE)((Hero->Object.Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                        if (abs(Dir1 - Dir2) == 4)
                        {
                            GuildPlayerKey = c->Key;
                            SocketClient->ToGameServer()->SendGuildJoinRequest(c->Key);
                            wchar_t Text[100];
                            mu_swprintf(Text, I18N::Game::YouHaveRequestedSToJoinYourGuild, c->ID);
                            g_pSystemLogBox->AddText(Text, SEASON3B::TYPE_SYSTEM_MESSAGE);
                            break;
                        }
                    }
                }
            return true;
        }
        if (!wcscmp(Text, I18N::Game::Alliance1354) || !wcsicmp(Text, L"/union") ||
            !wcscmp(Text, I18N::Game::Hostilities) || !wcsicmp(Text, L"/rival") ||
            !wcscmp(Text, I18N::Game::SuspendHostilities1357) || !wcsicmp(Text, L"/rivaloff"))
        {
            if (gMapManager.InChaosCastle() == true)
            {
                g_pSystemLogBox->AddText(I18N::Game::CanTBeInChaosCastle,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return false;
            }
            if (Hero->GuildStatus == G_NONE)
            {
                g_pSystemLogBox->AddText(I18N::Game::DoNotBelongToTheGuild,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return true;
            }

            if (CharactersClient.IsValidIndex(SelectedCharacter))
            {
                CHARACTER *c = &CharactersClient[SelectedCharacter];
                OBJECT *o = &c->Object;
                if (o->Kind == KIND_PLAYER && c != Hero && (o->Type == MODEL_PLAYER || c->Change) &&
                    abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                    abs((c->PositionY) - (Hero->PositionY)) <= 1)
                {
                    if (!wcscmp(Text, I18N::Game::Alliance1354) || !wcsicmp(Text, L"/union"))
                    {
                        //SendRequestGuildRelationShip(0x01, 0x01, HIBYTE(CharactersClient[SelectedCharacter].Key), LOBYTE(CharactersClient[SelectedCharacter].Key));
                        SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
                            GuildRelationshipType::Alliance, GuildRequestType::Join,
                            CharactersClient[SelectedCharacter].Key);
                    }
                    else if (!wcscmp(Text, I18N::Game::Hostilities) || !wcsicmp(Text, L"/rival"))
                    {
                        //SendRequestGuildRelationShip(0x02, 0x01, HIBYTE(CharactersClient[SelectedCharacter].Key), LOBYTE(CharactersClient[SelectedCharacter].Key));
                        SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
                            GuildRelationshipType::Hostility, GuildRequestType::Join,
                            CharactersClient[SelectedCharacter].Key);
                    }
                    else
                    {
                        SetAction(&Hero->Object, PLAYER_RESPECT1);
                        SendRequestAction(Hero->Object, AT_RESPECT1);
                        //SendRequestGuildRelationShip(0x02, 0x02, HIBYTE(CharactersClient[SelectedCharacter].Key), LOBYTE(CharactersClient[SelectedCharacter].Key));
                        SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
                            GuildRelationshipType::Hostility, GuildRequestType::Leave,
                            CharactersClient[SelectedCharacter].Key);
                    }
                }
            }
            else
                for (int i = 0; i < CharactersClient.Size(); i++)
                {
                    if (!CharactersClient.IsValidIndex(i))
                        continue;
                    CHARACTER *c = &CharactersClient[i];
                    OBJECT *o = &c->Object;

                    if (o->Live && o->Kind == KIND_PLAYER && c != Hero &&
                        (o->Type == MODEL_PLAYER || c->Change) &&
                        abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                        abs((c->PositionY) - (Hero->PositionY)) <= 1)
                    {
                        BYTE Dir1 = (BYTE)((o->Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                        BYTE Dir2 = (BYTE)((Hero->Object.Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                        if (abs(Dir1 - Dir2) == 4)
                        {
                            if (!wcscmp(Text, I18N::Game::Alliance1354) ||
                                !wcsicmp(Text, L"/union"))
                            {
                                //SendRequestGuildRelationShip(0x01, 0x01, HIBYTE(c->Key), LOBYTE(c->Key));
                                SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
                                    GuildRelationshipType::Alliance, GuildRequestType::Join,
                                    c->Key);
                            }
                            else if (!wcscmp(Text, I18N::Game::Hostilities) ||
                                     !wcsicmp(Text, L"/rival"))
                            {
                                //SendRequestGuildRelationShip(0x02, 0x01, HIBYTE(c->Key), LOBYTE(c->Key));
                                SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
                                    GuildRelationshipType::Hostility, GuildRequestType::Join,
                                    c->Key);
                            }
                            else
                            {
                                SetAction(&Hero->Object, PLAYER_RESPECT1);
                                SendRequestAction(Hero->Object, AT_RESPECT1);
                                //SendRequestGuildRelationShip(0x02, 0x02, HIBYTE(c->Key), LOBYTE(c->Key));
                                SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
                                    GuildRelationshipType::Hostility, GuildRequestType::Leave,
                                    c->Key);
                            }
                            break;
                        }
                    }
                }
            return true;
        }
        if (wcscmp(Text, I18N::Game::Party256) == 0 || wcsicmp(Text, L"/party") == 0 ||
            wcsicmp(Text, L"/pt") == 0)
        {
            if (gMapManager.InChaosCastle() == true)
            {
                g_pSystemLogBox->AddText(I18N::Game::CanTBeInChaosCastle,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return false;
            }
            if (PartyNumber > 0 && wcscmp(Party[0].Name, Hero->ID) != 0)
            {
                g_pSystemLogBox->AddText(I18N::Game::YouAreAlreadyInAParty,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
                return true;
            }

            if (CharactersClient.IsValidIndex(SelectedCharacter))
            {
                CHARACTER *c = &CharactersClient[SelectedCharacter];
                OBJECT *o = &c->Object;
                if (o->Kind == KIND_PLAYER && c != Hero && (o->Type == MODEL_PLAYER || c->Change) &&
                    abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                    abs((c->PositionY) - (Hero->PositionY)) <= 1)
                {
                    PartyKey = c->Key;
                    SocketClient->ToGameServer()->SendPartyInviteRequest(c->Key);
                    wchar_t Text[100];
                    mu_swprintf(Text, I18N::Game::YouHaveRequestedSToJoinYourParty, c->ID);
                    g_pSystemLogBox->AddText(Text, SEASON3B::TYPE_SYSTEM_MESSAGE);
                }
            }
            else
                for (int i = 0; i < CharactersClient.Size(); i++)
                {
                    if (!CharactersClient.IsValidIndex(i))
                        continue;
                    CHARACTER *c = &CharactersClient[i];
                    OBJECT *o = &c->Object;
                    if (o->Live && o->Kind == KIND_PLAYER && c != Hero &&
                        (o->Type == MODEL_PLAYER || c->Change) &&
                        abs((c->PositionX) - (Hero->PositionX)) <= 1 &&
                        abs((c->PositionY) - (Hero->PositionY)) <= 1)
                    {
                        BYTE Dir1 = (BYTE)((o->Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                        BYTE Dir2 = (BYTE)((Hero->Object.Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
                        if (abs(Dir1 - Dir2) == 4)
                        {
                            PartyKey = c->Key;
                            SocketClient->ToGameServer()->SendPartyInviteRequest(c->Key);
                            wchar_t Text[100];
                            mu_swprintf(Text, I18N::Game::YouHaveRequestedSToJoinYourParty, c->ID);
                            g_pSystemLogBox->AddText(Text, SEASON3B::TYPE_SYSTEM_MESSAGE);
                            break;
                        }
                    }
                }
            return true;
        }
        if (wcsicmp(Text, L"/charactername") == 0)
        {
            if (IsGMCharacter() == true)
            {
                g_bGMObservation = !g_bGMObservation;
                return true;
            }
        }

        for (int i = 0; i < 10; i++)
        {
            wchar_t Name[256];
            if (i != 9)
                mu_swprintf(Name, L"/%d", i + 1);
            else
                mu_swprintf(Name, L"/%d", 0);
            if (Text[0] == Name[0] && Text[1] == Name[1])
            {
                if (CheckMacroLimit(Text) == true)
                {
                    return false;
                }

                int iTextSize = 0;
                for (int j = 3; j <= (int)wcslen(Text); j++)
                {
                    MacroText[i][j - 3] = Text[j];
                    iTextSize = j;
                }
                MacroText[i][iTextSize - 3] = 0;
                PlayBuffer(SOUND_CLICK01);
                return true;
            }
        }

        wchar_t lpszFilter[] = L"/filter";
        if ((wcslen(I18N::Game::Filter) > 0 &&
             wcsncmp(Text, I18N::Game::Filter, wcslen(I18N::Game::Filter)) == 0) ||
            (wcsncmp(Text, lpszFilter, wcslen(lpszFilter)) == 0))
        {
            g_pChatListBox->SetFilterText(Text);
        }
    }
#ifdef CSK_FIX_MACRO_MOVEMAP
    else if (bMacroText == true)
    {
        wchar_t Name[256];

        int iTextSize = 0;
        for (int i = 0; i < 256 && Text[i] != ' ' && Text[i] != '\0'; i++)
        {
            Name[i] = Text[i];
            iTextSize = i;
        }
        Name[iTextSize] = NULL;

        if (wcscmp(Name, I18N::Game::Warp) == 0 || wcsicmp(Name, L"/move") == 0)
        {
            if (IsGMCharacter() == true || FindText2(Hero->ID, L"webzen") == true)
            {
                return false;
            }
            return true;
        }
    }
#endif // CSK_FIX_MACRO_MOVEMAP

    if (IsIllegalMovementByUsingMsg(Text))
        return TRUE;

    for (int i = 0; i < 16 * MAX_ITEM_INDEX; ++i)
    {
        ITEM_ATTRIBUTE *p = &ItemAttribute[i];

        if (p->Width != 0)
        {
            wchar_t Name[256];
            mu_swprintf(Name, L"/%ls", p->Name);

            if (wcsicmp(Text, Name) == 0)
            {
                g_csItemOption.ClearOptionHelper();

                g_pNewUISystem->Show(SEASON3B::INTERFACE_ITEM_EXPLANATION);

                ItemHelp = i;
                PlayBuffer(SOUND_CLICK01);
                return true;
            }
        }
    }

    g_csItemOption.CheckRenderOptionHelper(Text);

    return (IsIllegalMovementByUsingMsg(Text)) ? TRUE : FALSE;
    return false;
}

//bool IsWebzenCharacter()
//{
//    const std::wstring character_name = std::wstring(Hero->ID);
//    return character_name.find(L"webzen") >= 0;
//}

bool FindText(const wchar_t *Text, const wchar_t *Token, bool First)
{
    int LengthToken = (int)wcslen(Token);
    int Length = (int)wcslen(Text) - LengthToken;
    if (First)
        Length = 0;
    if (Length < 0)
        return false;

    auto *lpszCheck = (unsigned char *)Text;
    for (int i = 0; i <= Length; i += _mbclen(lpszCheck + i))
    {
        bool Success = true;
        for (int j = 0; j < LengthToken; j++)
        {
            if (Text[i + j] != Token[j])
            {
                Success = false;
                break;
            }
        }
        if (Success)
            return true;
    }
    return false;
}

bool FindTextABS(const wchar_t *Text, const wchar_t *Token, bool First)
{
    int LengthToken = (int)wcslen(Token);
    int Length = (int)wcslen(Text) - LengthToken;
    if (First)
        Length = 0;
    if (Length < 0)
        return false;

    auto *lpszCheck = (unsigned char *)Text;
    for (int i = 0; i <= Length; i += _mbclen(lpszCheck + i))
    {
        bool Success = true;
        for (int j = 0; j < LengthToken; j++)
        {
            if (Text[i + j] != Token[j])
            {
                Success = false;
                break;
            }
        }
        if (Success)
            return true;
    }
    return false;
}

void SessionGameplayUnit::SetActionClass(CHARACTER *c, OBJECT *o, int Action, int ActionType)
{
    if ((o->CurrentAction >= PLAYER_STOP_MALE && o->CurrentAction <= PLAYER_STOP_RIDE_WEAPON) ||
        o->CurrentAction == PLAYER_STOP_TWO_HAND_SWORD_TWO ||
        o->CurrentAction == PLAYER_RAGE_UNI_STOP_ONE_RIGHT ||
        o->CurrentAction == PLAYER_STOP_RAGEFIGHTER)
    {
        if (!gCharacterManager.IsFemale(c->Class) ||
            (Action >= PLAYER_RESPECT1 && Action <= PLAYER_RUSH1))
            SetAction(o, Action);
        else
            SetAction(o, Action + 1);
        SendRequestAction(Hero->Object, ActionType);
    }
}

void SessionLegacyCalls::SetActionClass(CHARACTER *character, OBJECT *object, int action,
                                        int actionType)
{
    sessionKeeper_.Gameplay()->SetActionClass(character, object, action, actionType);
}

bool SessionLegacyCalls::SkillKeyPush(int skill)
{
    return sessionKeeper_.Gameplay()->SkillKeyPush(skill);
}

namespace GameplayInteractionDetail
{
void AdvanceTeleportFade(OBJECT *o, float animationFactor)
{
    if (o->Teleport == TELEPORT_BEGIN)
    {
        o->Alpha -= 0.1f * animationFactor;
        if (o->Alpha < 0.1f)
        {
            o->Teleport = TELEPORT;
        }
    }
}
} // namespace GameplayInteractionDetail

bool SessionGameplayUnit::TryRequestGateTransfer(int gate)
{
    constexpr DWORD MinimumZoneMoveIntervalMilliseconds = 3000;
    if (g_bWhileMovingZone ||
        GetTickCount() - g_dwLatestZoneMoving < MinimumZoneMoveIntervalMilliseconds)
        return false;
    SocketClient->ToGameServer()->SendEnterGateRequest(gate, 0, 0);
    return true;
}

bool SessionGameplayUnit::PauseHeroForWorldTransfer(CHARACTER *character)
{
    auto &world = *sessionKeeper_.WorldUnit();
    if (world.ConsumeTransferStop())
        SetPlayerStop(character);
    return world.BlocksInteraction();
}

void SessionGameplayUnit::CheckGate()
{
    if ((sessionKeeper_.GameData()->HasInventoryItem(ITEM_POTION + 64, true)) ||
        (gMapManager.IsCursedTemple() &&
         sessionKeeper_.GameData()->HasInventoryItem(ITEM_POTION + 64, false)))
    {
        return;
    }

    for (int i = 0; i < MAX_GATES; i++)
    {
        GATE_ATTRIBUTE *gs = &GateAttribute[i];
        if (gs->Flag == 1 && gs->Map == gMapManager.ContextMap())
        {
            if ((Hero->PositionX) >= gs->x1 && (Hero->PositionY) >= gs->y1 &&
                (Hero->PositionX) <= gs->x2 && (Hero->PositionY) <= gs->y2)
            {
                GATE_ATTRIBUTE *gt = &GateAttribute[gs->Target];
                if (sessionKeeper_.WorldUnit()->CanEnterGate() && Hero->JumpTime == 0)
                {
                    bool Success = false;
                    int Level;

                    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK ||
                        gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD ||
                        gCharacterManager.GetBaseClass(Hero->Class) == CLASS_RAGEFIGHTER)
                        Level = gs->Level * 2 / 3;
                    else
                        Level = gs->Level;

                    if (i == 28)
                    {
                        if (CharacterAttribute->Level >= Level)
                            Success = true;
                    }
                    else
                        Success = true;
                    if (Success)
                    {
                        if (((i >= 45 && i <= 49) || (i >= 55 && i <= 56)) &&
                            ((CharacterMachine->Equipment[EQUIPMENT_HELPER].Type >=
                                  ITEM_HORN_OF_UNIRIA &&
                              CharacterMachine->Equipment[EQUIPMENT_HELPER].Type <=
                                  ITEM_HORN_OF_DINORANT)))
                        {
                            g_pSystemLogBox->AddText(
                                I18N::Game::YouCannotGoToAtlansWhileRidingAUnicorn,
                                SEASON3B::TYPE_ERROR_MESSAGE);
                        }
                        else if ((62 <= i && i <= 65) &&
                                 !((CharacterMachine->Equipment[EQUIPMENT_WING].Type >= ITEM_WING &&
                                        CharacterMachine->Equipment[EQUIPMENT_WING].Type <=
                                            ITEM_WINGS_OF_DARKNESS ||
                                    CharacterMachine->Equipment[EQUIPMENT_HELPER].Type ==
                                        ITEM_DARK_HORSE_ITEM ||
                                    CharacterMachine->Equipment[EQUIPMENT_WING].Type ==
                                        ITEM_CAPE_OF_LORD) ||
                                   CharacterMachine->Equipment[EQUIPMENT_HELPER].Type ==
                                       ITEM_HORN_OF_DINORANT ||
                                   CharacterMachine->Equipment[EQUIPMENT_HELPER].Type ==
                                       ITEM_HORN_OF_FENRIR ||
                                   (CharacterMachine->Equipment[EQUIPMENT_WING].Type >=
                                        ITEM_WING_OF_STORM &&
                                    CharacterMachine->Equipment[EQUIPMENT_WING].Type <=
                                        ITEM_WING_OF_DIMENSION) ||
                                   (ITEM_WING + 130 <=
                                        CharacterMachine->Equipment[EQUIPMENT_WING].Type &&
                                    CharacterMachine->Equipment[EQUIPMENT_WING].Type <=
                                        ITEM_WING + 134) ||
                                   (CharacterMachine->Equipment[EQUIPMENT_WING].Type >=
                                        ITEM_CAPE_OF_FIGHTER &&
                                    CharacterMachine->Equipment[EQUIPMENT_WING].Type <=
                                        ITEM_CAPE_OF_OVERRULE) ||
                                   (CharacterMachine->Equipment[EQUIPMENT_WING].Type ==
                                    ITEM_WING + 135)))
                        {
                            g_pSystemLogBox->AddText(
                                I18N::Game::YouCanEnterIcarusOnlyWithWingsDinorantFenrirr,
                                SEASON3B::TYPE_ERROR_MESSAGE);

                            if (CharacterAttribute->Level < Level)
                            {
                                wchar_t Text[100];
                                mu_swprintf(Text, I18N::Game::OnlyCharactersOverLevelDCanEnter,
                                            Level);
                                g_pSystemLogBox->AddText(Text, SEASON3B::TYPE_ERROR_MESSAGE);
                            }
                        }

                        else if ((62 <= i && i <= 65) &&
                                 (CharacterMachine->Equipment[EQUIPMENT_HELPER].Type ==
                                  ITEM_HORN_OF_UNIRIA))
                        {
                            g_pSystemLogBox->AddText(I18N::Game::YouCannotWarpWhileRidingOnAUnicorn,
                                                     SEASON3B::TYPE_ERROR_MESSAGE);
                        }
                        else if (CharacterAttribute->Level < Level)
                        {
                            sessionKeeper_.WorldUnit()->StartGateCooldown(
                                World::RejectedGateCooldownMilliseconds);
                            wchar_t Text[100];
                            mu_swprintf(Text, I18N::Game::OnlyCharactersOverLevelDCanEnter, Level);
                            g_pSystemLogBox->AddText(Text, SEASON3B::TYPE_ERROR_MESSAGE);
                            //							return;
                        }
                        else
                        {
                            const bool bResult = TryRequestGateTransfer(i);

                            if (!bResult)
                            {
                                g_bWhileMovingZone = FALSE;
                            }
                            else
                            {
                                sessionKeeper_.WorldUnit()->BeginTransfer();

                                if (gt->Map == WD_30BATTLECASTLE || gt->Map == WD_31HUNTING_GROUND)
                                {
                                    SaveOptions();
                                    SaveMacro(L"Data\\Macro.txt");
                                }

                                SelectedItem = -1;
                                SelectedNpc = -1;
                                SelectedCharacter = -1;
                                SelectedOperate = -1;
                                Attacking = -1;

                                if ((gs->Map == WD_0LORENCIA && gt->Map == WD_30BATTLECASTLE) ||
                                    (gs->Map == WD_30BATTLECASTLE && gt->Map == WD_0LORENCIA))
                                {
                                    g_dwLatestZoneMoving = GetTickCount();
                                    g_bWhileMovingZone = FALSE;
                                }
                                else
                                {
                                    g_bWhileMovingZone = TRUE;
                                }

                                StandTime = 0;
                            }
                        }
                    }
                }
            }
        }
    }
}

// While the hero slides to a server-set position (the basic weapon skills reposition the
// hero on every cast), MoveHero would freeze all input until the slide ended, which capped
// those skills' auto-attack cadence below the player's attack speed (issue #350). Keep the
// auto-attack re-cast running through the slide; the slide still animates smoothly and only
// manual move/click input stays suppressed. Returns true when the hero is mid-slide, in
// which case MoveHero should stop after this.
bool SessionGameplayUnit::HandleHeroPositionSlide(CHARACTER *c)
{
    if (c->JumpTime <= 0)
    {
        return false;
    }

    if (g_pOption->IsAutoAttack() && Attacking != -1 && SelectedCharacter != -1)
    {
        Attack(Hero);
    }
    return true;
}
void SessionGameplayUnit::MoveHero()
{
    CHARACTER *c = Hero;
    OBJECT *o = &c->Object;
    o->MotionTrace.Begin(WorldTime, FPS_ANIMATION_FACTOR, o->Position);

    // Re-arm world clicks once the button is released; the latch only suppresses the held click.
    // Kept above the early returns below (stun/sleep/dead/loading) so a release during any of
    // those states still clears the latch and the next click is honoured.
    if (!MouseLButton)
        s_bIgnoreHeldClickAfterNpcTalk = false;

    if (o->CurrentAction == PLAYER_CHANGE_UP)
    {
        return;
    }

    if (g_Direction.IsDirection(gMapManager.ContextMap()))
    {
        SetPlayerStop(c);
        return;
    }

    if (SelectedCharacter != -1 && g_isCharacterBuff((&Hero->Object), eBuff_CrywolfHeroContracted))
    {
        return;
    }

    if (c->Dead > 0 || g_isCharacterBuff(o, eDeBuff_Stun) || g_isCharacterBuff(o, eDeBuff_Sleep))
    {
        return;
    }

    if (c->Object.Live == 0)
        return;

    if (HandleHeroPositionSlide(c))
    {
        return;
    }

    if (PauseHeroForWorldTransfer(c))
        return;

    if (g_pWindowMgr->GetAddFriendWindow() > 0)
    {
        if (MouseRButtonPush)
        {
            if (!IsStrifeMap(gMapManager.ContextMap()))
            {
                auto *pWindow = (CUITextInputWindow *)g_pWindowMgr->GetWindow(
                    g_pWindowMgr->GetAddFriendWindow());
                if (pWindow != NULL)
                {
                    pWindow->SetText(CharactersClient[SelectedCharacter].ID);
                }
            }
        }
    }

    int HeroX = GetScreenWidth() / 2;
    int HeroY = 180;

    int Angle;
    bool bLookAtMouse = true;
    bool NoAutoAttacking = false;

    if (g_isCharacterBuff(o, eDeBuff_Stun) || g_isCharacterBuff(o, eDeBuff_Sleep) ||
        o->CurrentAction == PLAYER_SKILL_GIANTSWING)
    {
        Angle = (int)Hero->Object.Angle[2];
        bLookAtMouse = false;
    }
    else
    {
        // The screen-space angle from hero to cursor needs to be rotated into world space
        // by the active camera's yaw. The legacy hardcoded -45 was the default camera's
        // initial yaw, which silently broke for OrbitalCamera and
        // for any rotated DefaultCamera state.
        Angle = (int)(Hero->Object.Angle[2] +
                      CreateAngle((float)HeroX, (float)HeroY, (float)MouseX, (float)MouseY) +
                      g_Camera.Angle[2]) +
                360;
        Angle %= 360;
        if (Angle < 120)
            Angle = 120;
        if (Angle > 240)
            Angle = 240;
        Angle += 180;
        Angle %= 360;
    }

    Hero->Object.HeadTargetAngle[2] = 0.f;

    if ((g_pOption->IsAutoAttack() && Attacking != -1 && gMapManager.ContextMap() != WD_6STADIUM &&
         gMapManager.InChaosCastle() == false) ||
        o->CurrentAction == PLAYER_ATTACK_SKILL_FURY_STRIKE ||
        o->CurrentAction == PLAYER_SKILL_GIANTSWING)
    {
        bLookAtMouse = false;
    }
    if (bLookAtMouse)
    {
        int mousePosY = MouseY;

        if (mousePosY > REFERENCE_HEIGHT)
        {
            mousePosY = REFERENCE_HEIGHT;
        }
        Hero->Object.HeadTargetAngle[0] = (float)Angle;
        Hero->Object.HeadTargetAngle[1] = (HeroY - mousePosY) * 0.05f;

        NoAutoAttacking = true;
    }
    else
    {
        Hero->Object.HeadTargetAngle[0] = 0;
        Hero->Object.HeadTargetAngle[1] = 0;
    }

    if (c->Movement)
    {
        if (g_isCharacterBuff(o, eDeBuff_Harden) ||
            g_isCharacterBuff(o, eDeBuff_CursedTempleRestraint))
        {
            SetPlayerStop(c);
        }
        else
        {
            if (AdvanceCharacterPath(c))
            {
                c->Movement = false;
                SetPlayerStop(c);
                HeroAngle = (int)c->Object.Angle[2];
                StandTime = 0;

                if (c->MovementType == MOVEMENT_OPERATE)
                    Action(c, o, false);
                else if (!CheckArrow() && gCharacterManager.GetBaseClass(Hero->Class) == CLASS_ELF)
                    SetPlayerStop(Hero);
                else
                    Action(c, o, false);
            }
            else
            {
                g_CharacterUnRegisterBuff((&Hero->Object), eBuff_CrywolfHeroContracted);
            }
        }
    }
    else
    {
        StandTime += FPS_ANIMATION_FACTOR;

        if (StandTime >= 40 && !MouseOnWindow && Hero->Dead == 0 &&
            o->CurrentAction != PLAYER_POSE1 && o->CurrentAction != PLAYER_POSE_FEMALE1 &&
            o->CurrentAction != PLAYER_SIT1 && o->CurrentAction != PLAYER_SIT_FEMALE1 &&
            NoAutoAttacking && o->CurrentAction != PLAYER_ATTACK_TELEPORT &&
            o->CurrentAction != PLAYER_ATTACK_RIDE_TELEPORT &&
            o->CurrentAction != PLAYER_FENRIR_ATTACK_DARKLORD_TELEPORT &&
            o->CurrentAction != PLAYER_SKILL_ATT_UP_OURFORCES &&
            o->CurrentAction != PLAYER_SKILL_HP_UP_OURFORCES && Hero->AttackTime == 0)
        {
            StandTime = 0;
            // CreateAngle args are swapped vs the head-aim case above and the result is
            // negated, so the camera-yaw correction enters with the same magnitude but
            // appears with opposite sign in the sum (i.e. -45 became +45 for DefaultCamera).
            HeroAngle =
                -(int)(CreateAngle((float)MouseX, (float)MouseY, (float)HeroX, (float)HeroY) +
                       g_Camera.Angle[2]) +
                360;
            HeroAngle %= 360;
            BYTE Angle1 = ((BYTE)((o->Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8);
            BYTE Angle2 = ((BYTE)(((float)HeroAngle + 22.5f) / 360.f * 8.f + 1.f) % 8);
            if (Angle1 != Angle2)
            {
                if (o->CurrentAction != PLAYER_ATTACK_SKILL_SWORD2)
                {
                    Hero->Object.SetAngleZ((float)HeroAngle);
                }
                SendRequestAction(Hero->Object, AT_STAND1);
            }
        }

        UseSkillRagePosition(c);
    }

    CheckGate();

    if (!MouseOnWindow && false == g_pNewUISystem->CheckMouseUse())
    {
        const bool manualLeftInput = MouseLButtonPush || MouseLButton;
        bool Success = false;
        if (MouseUpdateTime >= MouseUpdateTimeMax && !s_bIgnoreHeldClickAfterNpcTalk)
        {
            if (!EnableFastInput)
            {
                if (MouseLButtonPush)
                {
                    MouseLButtonPush = false;
                    Success = true;
                }
                if (MouseLButton)
                {
                    Success = true;
                }

                if ((g_pOption->IsAutoAttack() && gMapManager.ContextMap() != WD_6STADIUM &&
                     gMapManager.InChaosCastle() == false) &&
                    (Attacking == 1 && SelectedCharacter != -1))
                {
                    Success = true;
                }

                if (Success && !g_isCharacterBuff(o, eDeBuff_Stun) &&
                    !g_isCharacterBuff(o, eDeBuff_Sleep))
                {
                    g_iFollowCharacter = -1;

                    LButtonPressTime = ((WorldTime - LButtonPopTime) / CLOCKS_PER_SEC);

                    if (LButtonPressTime >= GameplayInteractionDetail::AutoMouseLimitTime)
                    {
                        MouseLButtonPush = false;
                        MouseLButton = false;
                        Success = FALSE;
                    }
                }
                else
                {
                    LButtonPopTime = WorldTime;
                    LButtonPressTime = 0.f;
                }
            }
        }
        if (CharactersClient.IsValidIndex(g_iFollowCharacter))
        {
            CHARACTER *followCharacter = &CharactersClient[g_iFollowCharacter];
            if (followCharacter->Object.Live == 0)
            {
                g_iFollowCharacter = -1;
            }
            else
            {
                c->MovementType = MOVEMENT_MOVE;
                ActionTarget = g_iFollowCharacter;
                TargetX = (int)(followCharacter->Object.Position[0] / TERRAIN_SCALE);
                TargetY = (int)(followCharacter->Object.Position[1] / TERRAIN_SCALE);
                if (PathFinding2((c->PositionX), (c->PositionY), TargetX, TargetY, &c->Path))
                    SendMove(c, o);
            }
        }
        else if (Success &&
                 ((o->CurrentAction != PLAYER_SHOCK &&
                   (o->Teleport != TELEPORT_BEGIN && o->Teleport != TELEPORT && o->Alpha >= 0.7f) &&
                   !Engine::Object::IsAttackAction(o->CurrentAction) &&
                   (o->CurrentAction < PLAYER_SKILL_SLEEP ||
                    o->CurrentAction > PLAYER_SKILL_LIGHTNING_SHOCK) &&
                   o->CurrentAction != PLAYER_RECOVER_SKILL &&
                   (o->CurrentAction < PLAYER_SKILL_THRUST ||
                    o->CurrentAction > PLAYER_SKILL_HP_UP_OURFORCES)) ||
                  (o->CurrentAction >= PLAYER_STOP_TWO_HAND_SWORD_TWO &&
                   o->CurrentAction <= PLAYER_RUN_TWO_HAND_SWORD_TWO) ||
                  (o->CurrentAction >= PLAYER_DARKLORD_STAND &&
                   o->CurrentAction <= PLAYER_RUN_RIDE_HORSE) ||
                  (o->CurrentAction >= PLAYER_FENRIR_RUN &&
                   o->CurrentAction <= PLAYER_FENRIR_WALK_ONE_LEFT) ||
                  (o->CurrentAction >= PLAYER_RAGE_FENRIR_WALK &&
                   o->CurrentAction <= PLAYER_RAGE_FENRIR_STAND_ONE_LEFT)))
        {
            int RightType = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;
            int LeftType = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;

            auto *pPickedItem = sessionKeeper_.GameData()->GetPickedItem();

            if (!pPickedItem && RightType == -1 &&
                ((LeftType >= ITEM_SWORD && LeftType < ITEM_MACE + MAX_ITEM_INDEX) ||
                 (LeftType >= ITEM_STAFF && LeftType < ITEM_STAFF + MAX_ITEM_INDEX &&
                  !(LeftType >= ITEM_BOOK_OF_SAHAMUTT && LeftType <= ITEM_STAFF + 29))))
            {
                if (sessionKeeper_.Gameplay()->IsEquipable(
                        EQUIPMENT_WEAPON_LEFT, &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT]))
                {
                    memcpy(&PickItem, &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT],
                           sizeof(ITEM));
                    CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type = -1;
                    CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Level = 0;
                    SetCharacterClass(Hero);
                    SrcInventory = Inventory;
                    SrcInventoryIndex = EQUIPMENT_WEAPON_LEFT;
                    DstInventoryIndex = EQUIPMENT_WEAPON_RIGHT;
                    SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, SrcInventoryIndex, &PickItem,
                                             STORAGE_TYPE::INVENTORY, DstInventoryIndex);
                }
            }
            MouseUpdateTime = 0;
            Success = false;

            if (!c->SafeZone)
            {
                Success = CheckAttack();
            }

            if (Success)
            {
                if (manualLeftInput)
                {
                    sessionKeeper_.MuHelper()->YieldToManualControl();
                }
#ifdef SEND_POSITION_TO_SERVER
                if (c->Movement && c->MovementType == MOVEMENT_MOVE &&
                    gCharacterManager.GetBaseClass(c->Class) == CLASS_ELF)
                {
                    if (gCharacterManager.GetEquipedBowType(CharacterMachine->Equipment) !=
                        BOWTYPE_NONE)
                    {
                        SocketClient->ToGameServer()->SendInstantMoveRequest(c->PositionX,
                                                                             c->PositionY);
                    }
                }
#endif

                if (CharactersClient.IsValidIndex(SelectedCharacter))
                {
                    Attacking = 1;

                    c->MovementType = MOVEMENT_ATTACK;
                    ActionTarget = SelectedCharacter;
                    TargetX =
                        (int)(CharactersClient[ActionTarget].Object.Position[0] / TERRAIN_SCALE);
                    TargetY =
                        (int)(CharactersClient[ActionTarget].Object.Position[1] / TERRAIN_SCALE);

                    if (CheckWall((c->PositionX), (c->PositionY), TargetX, TargetY))
                    {
                        if (!PathFinding2((c->PositionX), (c->PositionY), TargetX, TargetY,
                                          &c->Path))
                        {
                            if (CheckArrow() == false)
                            {
                                return;
                            }
                            Action(c, o, true);
                        }
                        else
                        {
                            if ((GetEquipedBowType() != BOWTYPE_NONE) ||
                                (c->MonsterIndex == MONSTER_THUNDER_LICH))
                            {
                                if (CheckArrow() == false)
                                {
                                    return;
                                }
                                Action(c, o, true);
                            }
                            else
                            {
                                SendMove(c, o);
                            }
                        }
                    }
                }
            }
            else if (SelectedOperate != -1 &&
                     (c->SafeZone || (c->Helper.Type < MODEL_HORN_OF_UNIRIA ||
                                      c->Helper.Type > MODEL_DARK_HORSE_ITEM ||
                                      c->Helper.Type != MODEL_HORN_OF_FENRIR)))
            {
                TargetX = (int)(Operates[SelectedOperate].Owner->Position[0] / TERRAIN_SCALE);
                TargetY = (int)(Operates[SelectedOperate].Owner->Position[1] / TERRAIN_SCALE);
                int wall = TerrainWall[TERRAIN_INDEX(TargetX, TargetY)];

                if (wall == TW_HEIGHT || wall < TW_CHARACTER)
                    if (!c->Movement)
                    {
                        c->MovementType = MOVEMENT_OPERATE;
                        TargetType = Operates[SelectedOperate].Owner->Type;
                        TargetAngle = Operates[SelectedOperate].Owner->Angle[2];
                        if (PathFinding2((c->PositionX), (c->PositionY), TargetX, TargetY,
                                         &c->Path))
                            SendMove(c, o);
                        else
                            Action(c, o, true);
                    }
            }
            else if (SelectedNpc != -1 && !g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCSHOP) &&
                     !g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_STORAGE))
            {
                // usually still held at this point, and the held button keeps re-entering this
                // handler every frame. The moment the cursor isn't on the NPC's pick-box it would
                // fall through to the ground-move branch below and walk the hero away, instantly
                // closing the window the same click just opened. Most visible on adjacent NPCs such
                // shop window does. Latch the held click so it's ignored until released; a fresh
                // press still moves the hero normally. Also restart the debounce like the sibling
                // branches (attack/ground-move) do.
                s_bIgnoreHeldClickAfterNpcTalk = true;
                MouseUpdateTime = 0;

                if (g_isCharacterBuff(o, eBuff_CrywolfNPCHide) == false)
                    c->MovementType = MOVEMENT_TALK;

                ActionTarget = SelectedNpc;
                TargetX = (int)(CharactersClient[ActionTarget].Object.Position[0] / TERRAIN_SCALE);
                TargetY = (int)(CharactersClient[ActionTarget].Object.Position[1] / TERRAIN_SCALE);
                TargetNpc = ActionTarget;
                TargetType = CharactersClient[ActionTarget].Object.Type;
                TargetAngle = CharactersClient[ActionTarget].Object.Angle[2];

                if (TargetType == MODEL_KANTURU2ND_ENTER_NPC)
                {
                    vec3_t vHero, vTarget, vSubstract;
                    VectorCopy(o->Position, vHero);
                    vHero[2] = 0.f;
                    VectorCopy(CharactersClient[ActionTarget].Object.Position, vTarget);
                    vTarget[2] = 0.f;
                    VectorSubtract(vHero, vTarget, vSubstract);
                    float fLength = VectorLength(vSubstract);
                    if (fLength <= 550.f)
                    {
                        Action(c, o, true);
                    }
                    else
                    {
                        if (PathFinding2((c->PositionX), (c->PositionY), TargetX, TargetY,
                                         &c->Path))
                            SendMove(c, o);
                        else
                            Action(c, o, true);
                    }
                }
                else
                {
                    if (PathFinding2((c->PositionX), (c->PositionY), TargetX, TargetY, &c->Path))
                        SendMove(c, o);
                    else
                        Action(c, o, true);
                }
            }
            else if (SelectedItem != -1)
            {
                c->MovementType = MOVEMENT_GET;
                ItemKey = SelectedItem;
                TargetX = (int)(Items[SelectedItem].Object.Position[0] / TERRAIN_SCALE);
                TargetY = (int)(Items[SelectedItem].Object.Position[1] / TERRAIN_SCALE);
                if (PathFinding2((c->PositionX), (c->PositionY), TargetX, TargetY, &c->Path))
                    SendMove(c, o);
                else
                {
                    Action(c, o, true);
                    c->MovementType = MOVEMENT_MOVE;
                }
            }
            else if (!IsKeyDown(VK_SHIFT))
            {
                if (SelectFlag && c->Object.Live)
                {
                    if (manualLeftInput)
                    {
                        sessionKeeper_.MuHelper()->YieldToManualControl();
                    }
                    TargetX = (BYTE)(CollisionPosition[0] / TERRAIN_SCALE);
                    TargetY = (BYTE)(CollisionPosition[1] / TERRAIN_SCALE);
                    int Wall;
                    //if(CharacterMachine->Equipment[EQUIPMENT_WING].Type!=-1)
                    //	Wall = TW_NOMOVE;
                    //else
                    Wall = TW_NOGROUND;
                    WORD CurrAtt = TerrainWall[TargetY * 256 + TargetX];
                    if (CurrAtt >= Wall && (CurrAtt & TW_ACTION) != TW_ACTION &&
                        (CurrAtt & TW_HEIGHT) != TW_HEIGHT)
                        DontMove = true;
                    else
                        DontMove = false;
                    int xPos = (int)(0.01f * o->Position[0]);
                    int yPos = (int)(0.01f * o->Position[1]);

                    if (!c->Movement ||
                        (abs((c->PositionX) - xPos) < 2 && abs((c->PositionY) - yPos) < 2))
                    {
                        if (((c->PositionX) != TargetX || (c->PositionY) != TargetY ||
                             !c->Movement) &&
                            PathFinding2((c->PositionX), (c->PositionY), TargetX, TargetY,
                                         &c->Path))
                        {
                            c->MovementType = MOVEMENT_MOVE;
                            SendMove(c, o);
                            OBJECT *pHeroObj = &Hero->Object;
                            vec3_t vLight, vPos;
                            Vector(1.f, 1.f, 0.f, vLight);
                            VectorCopy(CollisionPosition, vPos);
                            DeleteEffect(MODEL_MOVE_TARGETPOSITION_EFFECT);

                            int iTerrainIndex = TERRAIN_INDEX((int)SelectXF, (int)SelectYF);
                            if ((TerrainWall[iTerrainIndex] & TW_NOMOVE) != TW_NOMOVE)
                            {
                                CreateEffect(MODEL_MOVE_TARGETPOSITION_EFFECT, vPos,
                                             pHeroObj->Angle, vLight, 0, pHeroObj, -1, 0, 0, 0,
                                             0.6f);
                            }
                        }
                        else
                        {
                            MouseUpdateTime = MouseUpdateTimeMax;
                            MouseUpdateTime = 0;
                        }
                    }
                }
            }
        }
        MouseUpdateTime += FPS_ANIMATION_FACTOR;
    }

    Attack(Hero);

    int Index = ((int)Hero->Object.Position[1] / (int)TERRAIN_SCALE) * 256 +
                ((int)Hero->Object.Position[0] / (int)TERRAIN_SCALE);
    if (Index < 0)
        Index = 0;
    else if (Index > 65535)
        Index = 65535;
    HeroTile = TerrainMappingLayer1[Index];
}

int SessionGameplayUnit::FindHotKey(int Skill)
{
    int SkillIndex = 0;

    for (int i = 0; i < MAX_MAGIC; i++)
    {
        if (CharacterAttribute->Skill[i] == Skill)
        {
            SkillIndex = i;
            break;
        }
    }

    return SkillIndex;
}

int SessionLegacyCalls::FindHotKey(int Skill)
{
    return sessionKeeper_.Gameplay()->FindHotKey(Skill);
}

void SessionGameplayUnit::SendMacroChat(wchar_t *Text)
{
    if (!CheckCommand(Text, true))
    {
        if ((Hero->Helper.Type < MODEL_HORN_OF_UNIRIA ||
             Hero->Helper.Type > MODEL_DARK_HORSE_ITEM) ||
            Hero->SafeZone)
        {
            // Send animation for specific texts
            CheckChatText(Text);
        }

        //if (CheckAbuseFilter(Text))
        //{
        //    SendChat(I18N::Game::PwnedByTheFilter);
        //}
        //else
        //{
        //    SendChat(Text);
        //}

        SocketClient->ToGameServer()->SendPublicChatMessage(Hero->ID, Text);

        LastMacroTime = GetTickCount64();
    }
}

void SessionGameplayUnit::MoveInterface()
{
    if (g_Direction.IsDirection(gMapManager.ContextMap()))
    {
        return;
    }

    if (IsBattleCastleStart() == true)
    {
        if (CharacterAttribute->SkillNumber > 0)
        {
            UseBattleMasterSkill();
        }
    }

    if (LastMacroTime < GetTickCount64() - GameplayInteractionDetail::MacroCooldownMs)
    {
        if (gMapManager.InChaosCastle() == false)
        {
            if (IsKeyDown(VK_MENU))
            {
                for (int i = 0; i < 9; i++)
                {
                    if (IsKeyDown('1' + i))
                    {
                        SendMacroChat(MacroText[i]);
                    }
                }
                if (IsKeyDown('0'))
                {
                    SendMacroChat(MacroText[9]);
                }
            }
        }
    }

    if (Hero->Dead == 0)
    {
        g_pMainFrame->UseHotKeyItemRButton();
    }

    if (g_pUIManager->IsInputEnable())
    {
        int x, y, Width, Height;
        if (g_iChatInputType == 0)
        {
            Width = 190;
            Height = 29;
            x = 186;
            y = 415;
            if (MouseX >= x && MouseX < x + Width && MouseY >= y && MouseY < y + Height)
            {
                if (MouseLButtonPush)
                {
                    MouseLButtonPush = false;
                    InputIndex = 0;
                    MouseUpdateTime = 0;
                    MouseUpdateTimeMax = 6;
                    PlayBuffer(SOUND_CLICK01);
                }
            }
            Width = 58;
            Height = 29;
            x = 186 + 190;
            y = 415;
            if (MouseX >= x && MouseX < x + Width && MouseY >= y && MouseY < y + Height)
            {
                if (MouseLButtonPush)
                {
                    MouseLButtonPush = false;
                    InputIndex = 1;
                    MouseUpdateTime = 0;
                    MouseUpdateTimeMax = 6;
                    PlayBuffer(SOUND_CLICK01);
                }
            }
        }
        Width = 20;
        Height = 29;
        x = 186 + 190 + 58;
        y = 415;
        if (MouseX >= x && MouseX < x + Width && MouseY >= y && MouseY < y + Height)
        {
            if (MouseLButtonPush)
            {
                MouseLButtonPush = false;
                if (WhisperEnable)
                    WhisperEnable = false;
                else
                    WhisperEnable = true;
                MouseUpdateTime = 0;
                MouseUpdateTimeMax = 6;
                PlayBuffer(SOUND_CLICK01);
            }
        }
    }
}

bool SessionLegacyCalls::CheckCommand(wchar_t *text, bool macroText)
{
    return sessionKeeper_.Gameplay()->CheckCommand(text, macroText);
}

void SessionLegacyCalls::SendMacroChat(wchar_t *text)
{
    sessionKeeper_.Gameplay()->SendMacroChat(text);
}

bool SessionLegacyCalls::CheckAttack_Fenrir(CHARACTER *character)
{
    return sessionKeeper_.Gameplay()->CheckAttack_Fenrir(character);
}
bool SessionLegacyCalls::CheckAttack()
{
    return sessionKeeper_.Gameplay()->CheckAttack();
}
int SessionLegacyCalls::getTargetCharacterKey(CHARACTER *character, int selected)
{
    return sessionKeeper_.Gameplay()->getTargetCharacterKey(character, selected);
}
bool SessionLegacyCalls::IsCanBCSkill(int type)
{
    return sessionKeeper_.Gameplay()->IsCanBCSkill(type);
}
bool SessionLegacyCalls::CheckSkillUseCondition(OBJECT *object, int type)
{
    return sessionKeeper_.Gameplay()->CheckSkillUseCondition(object, type);
}

void GetTime(DWORD time, std::wstring &timeText, bool isSecond)
{
    wchar_t buff[100];

    if (isSecond)
    {
        DWORD day = time / (1440 * 60);
        DWORD oClock = (time - (day * (1440 * 60))) / 3600;
        DWORD minutes = (time - ((oClock * 3600) + (day * (1440 * 60)))) / 60;
        DWORD second = time % 60;

        if (day != 0)
        {
            mu_swprintf(buff, L"%d %ls %d %ls %d %ls %d %ls", day, I18N::Game::Day, oClock,
                        I18N::Game::Hour, minutes, I18N::Game::Minute, second, I18N::Game::Second);
            timeText = buff;
        }
        else if (day == 0 && oClock != 0)
        {
            mu_swprintf(buff, L"%d %ls %d %ls %d %ls", oClock, I18N::Game::Hour, minutes,
                        I18N::Game::Minute, second, I18N::Game::Second);
            timeText = buff;
        }
        else if (day == 0 && oClock == 0 && minutes != 0)
        {
            mu_swprintf(buff, L"%d %ls %d %ls", minutes, I18N::Game::Minute, second,
                        I18N::Game::Second);
            timeText = buff;
        }
        else if (day == 0 && oClock == 0 && minutes == 0)
        {
            timeText = I18N::Game::LessThan1Minutes;
        }
    }
    else
    {
        DWORD day = time / 1440;
        DWORD oClock = (time - (day * 1440)) / 60;
        DWORD minutes = time % 60;

        if (day != 0)
        {
            mu_swprintf(buff, L"%d %ls %d %ls %d %ls", day, I18N::Game::Day, oClock,
                        I18N::Game::Hour, minutes, I18N::Game::Minute);
            timeText = buff;
        }
        else if (day == 0 && oClock != 0)
        {
            mu_swprintf(buff, L"%d %ls %d %ls", oClock, I18N::Game::Hour, minutes,
                        I18N::Game::Minute);
            timeText = buff;
        }
        else if (day == 0 && oClock == 0 && minutes != 0)
        {
            mu_swprintf(buff, L"%d %ls", minutes, I18N::Game::Minute);
            timeText = buff;
        }
    }
}

namespace GameplayInteractionDetail
{
bool InsideCastleSwitchArea(const CHARACTER &hero)
{
    constexpr int MinimumX = 150, MaximumX = 200;
    constexpr int MinimumY = 180, MaximumY = 230;
    return hero.PositionX >= MinimumX && hero.PositionX <= MaximumX && hero.PositionY >= MinimumY &&
           hero.PositionY <= MaximumY;
}
} // namespace GameplayInteractionDetail

void SessionGameplayUnit::UpdateSwitchState()
{
    if (Switch_Info && !GameplayInteractionDetail::InsideCastleSwitchArea(*Hero))
        Delete_Switch();
}

void SessionGameplayUnit::MoveTournamentInterface()
{
    int Width = 70, Height = 20;
    int WindowX = (REFERENCE_WIDTH - Width) / 2;
    int WindowY = (REFERENCE_HEIGHT - Height) / 2 + 50;
    if (MouseLButtonPush)
    {
        float wRight = WindowX + Width;
        float wBottom = WindowY + Height;

        if (WindowY <= MouseY && MouseY <= WindowY + Height && WindowX <= MouseX &&
            MouseX <= WindowX + Width)
        {
            g_wtMatchResult.Clear();
            g_wtMatchTimeLeft.m_Time = 0;
        }
    }

    if (g_iGoalEffect)
    {
        for (int i = 0; i < CharactersClient.Size(); i++)
        {
            if (!CharactersClient.IsValidIndex(i))
                continue;
            MoveBattleSoccerEffect(&CharactersClient[i]);
        }
        g_iGoalEffect = 0;
    }
}

void SessionGameplayUnit::MoveBattleSoccerEffect(CHARACTER *c)
{
    OBJECT *o = &c->Object;
    if (o->Live)
    {
        if (g_iGoalEffect == 1)
        {
            if (c->GuildTeam == 1)
            {
                vec3_t Position, Angle;
                for (int i = 0; i < 36; ++i)
                {
                    Angle[0] = -10.f;
                    Angle[1] = 0.f;
                    Angle[2] = 45.f;
                    float x_bias = cosf(Q_PI / 180.f * 10.0f * i);
                    float y_bias = sinf(Q_PI / 180.f * 10.0f * i);
                    Position[0] = c->Object.Position[0] + x_bias * TERRAIN_SCALE;
                    Position[1] = c->Object.Position[1] + y_bias * TERRAIN_SCALE;
                    Position[2] = c->Object.Position[2];
                    CreateJoint(BITMAP_FLARE, Position, Position, Angle, 22, &c->Object, 35);
                }
            }
        }
        else if (g_iGoalEffect == 2)
        {
            if (c->GuildTeam == 2)
            {
                vec3_t Position, Angle;
                for (int i = 0; i < 36; ++i)
                {
                    Angle[0] = -10.f;
                    Angle[1] = 0.f;
                    Angle[2] = 45.f;
                    float x_bias = cosf(Q_PI / 180.f * 10.0f * i);
                    float y_bias = sinf(Q_PI / 180.f * 10.0f * i);
                    Position[0] = c->Object.Position[0] + x_bias * TERRAIN_SCALE;
                    Position[1] = c->Object.Position[1] + y_bias * TERRAIN_SCALE;
                    Position[2] = c->Object.Position[2];
                    CreateJoint(BITMAP_FLARE, Position, Position, Angle, 22, &c->Object, 35);
                }
            }
        }
    }
}

void SessionLegacyCalls::BackSelectModel()
{
    sessionKeeper_.Renderer()->BackSelectModel();
}
void SessionLegacyCalls::ForwardSelectModel()
{
    sessionKeeper_.Renderer()->ForwardSelectModel();
}

bool SessionGameplayUnit::IsGMCharacter()
{
    if ((Hero->Object.Kind == KIND_PLAYER && Hero->Object.Type == MODEL_PLAYER &&
         Hero->Object.SubType == MODEL_GM_CHARACTER) ||
        (g_isCharacterBuff((&Hero->Object), eBuff_GMEffect)) ||
        (Hero->CtlCode & CTLCODE_08OPERATOR) || (Hero->CtlCode & CTLCODE_20OPERATOR))
    {
        return true;
    }
    return false;
}

bool SessionLegacyCalls::IsGMCharacter()
{
    return sessionKeeper_.Gameplay()->IsGMCharacter();
}

bool SessionGameplayUnit::IsNonAttackGM()
{
    if (Hero->CtlCode & CTLCODE_04FORTV || Hero->CtlCode & CTLCODE_08OPERATOR)
    {
        return true;
    }

    return false;
}

bool SessionLegacyCalls::IsNonAttackGM()
{
    return sessionKeeper_.Gameplay()->IsNonAttackGM();
}

bool SessionGameplayUnit::IsIllegalMovementByUsingMsg(const wchar_t *szChatText)
{
    bool bCantFly = false;
    bool bCantSwim = false;
    bool bEquipChangeRing = false;

    bool bMoveAtlans = false;
    bool bMoveIcarus = false;

    wchar_t szChatTextUpperChars[256];
    wcscpy(szChatTextUpperChars, szChatText);
    _wcsupr(szChatTextUpperChars);

    short pEquipedRightRingType = (&CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT])->Type;
    short pEquipedLeftRingType = (&CharacterMachine->Equipment[EQUIPMENT_RING_LEFT])->Type;
    short pEquipedHelperType = (&CharacterMachine->Equipment[EQUIPMENT_HELPER])->Type;
    short pEquipedWingType = (&CharacterMachine->Equipment[EQUIPMENT_WING])->Type;

    if ((pEquipedWingType == -1 && pEquipedHelperType != ITEM_HORN_OF_DINORANT &&
         pEquipedHelperType != ITEM_HORN_OF_FENRIR && pEquipedHelperType != ITEM_DARK_HORSE_ITEM) ||
        pEquipedHelperType == ITEM_HORN_OF_UNIRIA)
    {
        bCantFly = true;
    }
    if (pEquipedHelperType == ITEM_HORN_OF_UNIRIA || pEquipedHelperType == ITEM_HORN_OF_DINORANT)
    {
        bCantSwim = true;
    }
    if (g_ChangeRingMgr->CheckMoveMap(pEquipedLeftRingType, pEquipedRightRingType))
    {
        bEquipChangeRing = true;
    }

    if ((wcsstr(szChatTextUpperChars, L"/MOVE") != NULL) ||
        (wcslen(I18N::Game::Warp) > 0 && wcsstr(szChatTextUpperChars, I18N::Game::Warp) != NULL))
    {
        std::list<SEASON3B::CMoveCommandData::MOVEINFODATA *> m_listMoveInfoData;
        m_listMoveInfoData = g_MoveCommandData.GetMoveCommandDatalist();

        auto li = m_listMoveInfoData.begin();

        while (li != m_listMoveInfoData.end())
        {
            wchar_t cMapNameUpperChars[256];
            wcscpy(cMapNameUpperChars, (*li)->_ReqInfo.szSubMapName);
            _wcsupr(cMapNameUpperChars);

            if (wcsstr(szChatText, ((*li)->_ReqInfo.szMainMapName)) != NULL ||
                wcsstr(szChatTextUpperChars, cMapNameUpperChars) != NULL)
                break;

            li++;
        }

        if (li != m_listMoveInfoData.end())
        {
            if (wcsicmp((*li)->_ReqInfo.szMainMapName, I18N::Game::Atlans) == 0)
            {
                bMoveAtlans = true;
            }
            else if (wcsicmp((*li)->_ReqInfo.szMainMapName, I18N::Game::Icarus) == 0)
            {
                bMoveIcarus = true;
            }
        }
    }

    if (bCantSwim && bMoveAtlans)
    {
        g_pSystemLogBox->AddText(I18N::Game::YouCannotGoToAtlansWhileRidingAUnicorn,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
        return true;
    }

    if ((bCantFly || bEquipChangeRing) && bMoveIcarus)
    {
        g_pSystemLogBox->AddText(I18N::Game::YouCanEnterIcarusOnlyWithWingsDinorantFenrirr,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
        return true;
    }

    return false;
}

bool SessionLegacyCalls::IsIllegalMovementByUsingMsg(const wchar_t *text)
{
    return sessionKeeper_.Gameplay()->IsIllegalMovementByUsingMsg(text);
}
void SessionLegacyCalls::Action(CHARACTER *c, OBJECT *o, bool Now)
{
    return sessionKeeper_.Gameplay()->Action(c, o, Now);
} // OMF-00459
void SessionLegacyCalls::Attack(CHARACTER *c)
{
    return sessionKeeper_.Gameplay()->Attack(c);
} // OMF-00468
void SessionLegacyCalls::CheckGate()
{
    return sessionKeeper_.Gameplay()->CheckGate();
} // OMF-00469
bool SessionLegacyCalls::HandleHeroPositionSlide(CHARACTER *c)
{
    return sessionKeeper_.Gameplay()->HandleHeroPositionSlide(c);
} // OMF-00470
void SessionLegacyCalls::MoveHero()
{
    return sessionKeeper_.Gameplay()->MoveHero();
} // OMF-00471

#ifdef ENABLE_EDIT2
void SessionGameplayUnit::UpdateEditorMovement()
{
    if (!Hero || !Hero->Object.Live || g_pUIManager->IsInputEnable())
        return;
    constexpr float EditorSpeed = TERRAIN_SCALE * 1.25f;
    const float velocity = EditorSpeed * FPS_ANIMATION_FACTOR;
    vec3_t input = {}, motion, angles = {0.f, 0.f, -g_Camera.Angle[2]};
    if (IsKeyDown(VK_LEFT))
    {
        Vector(-velocity, -velocity, 0.f, input);
    }
    else if (IsKeyDown(VK_RIGHT))
    {
        Vector(velocity, velocity, 0.f, input);
    }
    else if (IsKeyDown(VK_UP))
    {
        Vector(-velocity, velocity, 0.f, input);
    }
    else if (IsKeyDown(VK_DOWN))
    {
        Vector(velocity, -velocity, 0.f, input);
    }
    else
        return;
    float rotation[3][4];
    AngleMatrix(angles, rotation);
    VectorRotate(input, rotation, motion);
    VectorAdd(Hero->Object.Position, motion, Hero->Object.Position);
    BYTE pathX[] = {static_cast<BYTE>(Hero->Object.Position[0] / TERRAIN_SCALE)};
    BYTE pathY[] = {static_cast<BYTE>(Hero->Object.Position[1] / TERRAIN_SCALE)};
    SendCharacterMove(Hero->Key, Hero->Object.Angle[2], 1, pathX, pathY, pathX[0], pathY[0]);
    Hero->Path.PathNum = 0;
}
#endif
