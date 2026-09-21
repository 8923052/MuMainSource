#include "domain/WorldSimulation.h"
#include "domain/ItemsSkills.h"
#include "support/CoreMath.h"
#include "session/SessionRender.h"
#include "domain/MapSimulation.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "domain/WorldPhysics.h"
#include "app/ApplicationLoopFrame.h"
#include "session/SessionPresentation.h"
#include "data/ResourceData.h"
#include "support/Camera.h"
#include "domain/Events.h"
#include "app/ApplicationAudio.h"
#include "render/Sprites.h"
#include "ui/session/UiSessionLogic.h"
#include "domain/Shop.h"
#include "render/ModelResources.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/CharacterPresentation.h"
#include "render/World.h"
#include "data/GameData.h"
#include "render/Textures.h"
#include "session/SessionNetwork.h"
#include "I18N/All.h"
#include "app/ApplicationDiagnostics.h"
#include "session/SessionUi.h"
#include "app/ApplicationNetwork.h"
#include "render/ModelGeometry.h"
#include "data/Localization.h"
#include "domain/MovementAI.h"
#include "data/ItemData.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"

void ObjectMotionTrace::Reset() noexcept
{
    frameTime_ = -1.0;
    frames_ = elapsed_ = animationElapsed_ = 0.f;
    positionStart_ = yawStart_ = animationStart_ = 0.f;
    points_.clear();
    yaws_.clear();
    animations_.clear();
}

void ObjectMotionTrace::Begin(double frameTime, float frames, const float *position,
                              float startFraction)
{
    if (frames <= 0.f || frameTime == frameTime_)
        return;
    frameTime_ = frameTime;
    positionStart_ = yawStart_ = animationStart_ = startFraction;
    frames_ = frames;
    elapsed_ = animationElapsed_ = 0.f;
    std::copy_n(position, origin_.size(), origin_.begin());
    points_.clear();
    yaws_.clear();
    animations_.clear();
}

void ObjectMotionTrace::BeginDrivenMotion(double frameTime, float frames, const float *position,
                                          bool replaceYaw, float startFraction)
{
    if (frames <= 0.f)
        return;
    Begin(frameTime, frames, position, startFraction);
    positionStart_ = startFraction;
    if (replaceYaw)
        yawStart_ = startFraction;
    frames_ = frames;
    elapsed_ = 0.f;
    points_.clear();
    if (replaceYaw)
        yaws_.clear();
    std::copy_n(position, origin_.size(), origin_.begin());
}

void ObjectMotionTrace::Advance(float frames, const float *position)
{
    if (frames <= 0.f || frames_ <= 0.f)
        return;
    elapsed_ += frames;
    if (elapsed_ < frames_)
        points_.push_back({elapsed_ / frames_, {position[0], position[1], position[2]}});
}

void ObjectMotionTrace::Sample(double frameTime, float fraction, const float *finalPosition,
                               float *position) const
{
    if (frameTime != frameTime_ || fraction >= 1.f)
    {
        std::copy_n(finalPosition, origin_.size(), position);
        return;
    }
    fraction = (fraction - positionStart_) / (1.f - positionStart_);
    if (fraction <= 0.f)
    {
        std::copy(origin_.begin(), origin_.end(), position);
        return;
    }
    const auto next =
        std::lower_bound(points_.begin(), points_.end(), fraction,
                         [](const Point &point, float value) { return point.fraction < value; });
    const float before = next == points_.begin() ? 0.f : (next - 1)->fraction;
    const float after = next == points_.end() ? 1.f : next->fraction;
    const float *start = next == points_.begin() ? origin_.data() : (next - 1)->position.data();
    const float *end = next == points_.end() ? finalPosition : next->position.data();
    const float amount = (fraction - before) / (after - before);
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = start[axis] + (end[axis] - start[axis]) * amount;
}

void ObjectMotionTrace::TurnYaw(float frames, float from, float target, float blend)
{
    if (frames <= 0.f || frames_ <= 0.f)
        return;
    yaws_.push_back({elapsed_ / frames_, (elapsed_ + frames) / frames_, frames, from,
                     std::remainder(target - from, 360.f), blend});
}

float ObjectMotionTrace::SampleYaw(double frameTime, float fraction, float currentYaw) const
{
    if (frameTime != frameTime_)
        return currentYaw;
    fraction = (fraction - yawStart_) / (1.f - yawStart_);
    const auto interval =
        std::upper_bound(yaws_.begin(), yaws_.end(), fraction,
                         [](float value, const YawInterval &yaw) { return value < yaw.end; });
    if (interval == yaws_.end())
        return currentYaw;
    const float elapsed =
        std::clamp((fraction - interval->begin) / (interval->end - interval->begin), 0.f, 1.f) *
        interval->frames;
    const float blend = interval->blend == 0.f   ? elapsed / interval->frames
                        : interval->blend == 1.f ? 1.f
                                                 : 1.f - std::pow(1.f - interval->blend, elapsed);
    return interval->from + interval->difference * blend;
}

void ObjectMotionTrace::AdvanceAnimation(float frames, AnimationPhase phase, float travel)
{
    if (frames <= 0.f || frames_ <= 0.f)
        return;
    const float begin = animationElapsed_ / frames_;
    animationElapsed_ += frames;
    animations_.push_back({begin, animationElapsed_ / frames_, travel, phase});
}

ObjectMotionTrace::AnimationPhase ObjectMotionTrace::SampleAnimation(double frameTime,
                                                                     float fraction,
                                                                     AnimationPhase current) const
{
    if (frameTime != frameTime_)
        return current;
    fraction = (fraction - animationStart_) / (1.f - animationStart_);
    const auto interval = std::upper_bound(
        animations_.begin(), animations_.end(), fraction,
        [](float value, const AnimationInterval &animation) { return value < animation.end; });
    if (interval == animations_.end())
        return current;
    AnimationPhase phase = interval->phase;
    const float amount =
        std::clamp((fraction - interval->begin) / (interval->end - interval->begin), 0.f, 1.f);
    phase.frame += interval->travel * amount;
    if (std::floor(phase.frame) != std::floor(interval->phase.frame))
    {
        phase.priorAction = phase.action;
        phase.adjacentKeys = true;
    }
    return phase;
}

std::optional<float> ObjectMotionTrace::FirstAnimationCrossing(double frameTime, int action,
                                                               float frame) const
{
    if (frameTime != frameTime_)
        return std::nullopt;
    for (const auto &interval : animations_)
    {
        if (interval.phase.action != action || interval.travel <= 0.f ||
            frame < interval.phase.frame || frame > interval.phase.frame + interval.travel)
            continue;
        const float local = interval.begin + (interval.end - interval.begin) *
                                                 ((frame - interval.phase.frame) / interval.travel);
        return animationStart_ + (1.f - animationStart_) * local;
    }
    return std::nullopt;
}

void World::AdvanceEnvironment()
{
    TheMapProcess().AdvanceEnvironment();
}

void CInterpolateContainer::GetCurrentValue(vec3_t &v3Out, float fCurrentRate,
                                            VEC_INTERPOLATES &vecInterpolates)
{
    auto iterBegin = vecInterpolates.begin();
    auto iterEnd = vecInterpolates.end();
    VEC_INTERPOLATES::iterator iter_;

    INTERPOLATE_FACTOR *pCurFactor = NULL;

    bool bFindInterpolateFactor = false;

    for (iter_ = iterBegin; iter_ < iterEnd; ++iter_)
    {
        INTERPOLATE_FACTOR &interpolateFactor = (*iter_);

        if (interpolateFactor.fRateStart <= fCurrentRate &&
            interpolateFactor.fRateEnd > fCurrentRate)
        {
            bFindInterpolateFactor = true;
            pCurFactor = &interpolateFactor;
            break;
        }
    }

    if (bFindInterpolateFactor == true)
    {
        VectorInterpolation_F(v3Out, pCurFactor->v3Start, pCurFactor->v3End,
                              pCurFactor->fRateEnd - pCurFactor->fRateStart,
                              fCurrentRate - pCurFactor->fRateStart);
    }
}

void CInterpolateContainer::GetCurrentValueF(float &fOut, float fCurrentRate,
                                             VEC_INTERPOLATES_F &vecInterpolates)
{
    auto iterBegin = vecInterpolates.begin();
    auto iterEnd = vecInterpolates.end();
    VEC_INTERPOLATES_F::iterator iter_;

    INTERPOLATE_FACTOR_F *pCurFactor = NULL;

    bool bFindInterpolateFactor = false;

    for (iter_ = iterBegin; iter_ < iterEnd; ++iter_)
    {
        INTERPOLATE_FACTOR_F &interpolateFactor = (*iter_);

        if (interpolateFactor.fRateStart <= fCurrentRate &&
            interpolateFactor.fRateEnd > fCurrentRate)
        {
            bFindInterpolateFactor = true;
            pCurFactor = &interpolateFactor;
            break;
        }
    }

    if (bFindInterpolateFactor == true)
    {
        LInterpolationF(fOut, pCurFactor->fStart, pCurFactor->fEnd,
                        (float)(fCurrentRate - pCurFactor->fRateStart) /
                            (float)(pCurFactor->fRateEnd - pCurFactor->fRateStart));
    }
}

void CInterpolateContainer::ClearContainer()
{
    m_vecInterpolatesPos.clear();
    m_vecInterpolatesAngle.clear();
    m_vecInterpolatesScale.clear();
    m_vecInterpolatesAlpha.clear();
}

OBJECT::OBJECT()
{
    Initialize();
}

OBJECT::~OBJECT()
{
    Destroy();
}

void OBJECT::Initialize()
{
    m_bpcroom = false;
    Live = false;
    PresentationRandom = false;
    bBillBoard = false;
    m_bCollisionCheck = false;
    m_bRenderShadow = false;
    EnableShadow = false;
    LightEnable = false;
    m_bActionStart = false;
    Visible = false;
    AlphaEnable = false;
    EnableBoneMatrix = false;
    RigidPose.reset();
    RigidPoseDirty = false;
    ContrastEnable = false;
    ChromeEnable = false;
    m_bRenderAfterCharacter = false;

    AI = 0;
    MotionTrace.Reset();
    EffectEmissionAge = 0.0;
    EffectMotionFrames = 0.f;
    Vector(0.f, 0.f, 0.f, EffectMotionVelocity);
    Vector(0.f, 0.f, 0.f, EffectMotionAngleRate);
    EffectResting = false;
    EffectAnimationAdvance.reset();
    AmbientNoiseFrames = 0.f;
    AmbientVerticalNoise = 0.f;
    AmbientTurnRate = 0.f;
    AmbientSpeedNoise = 1.f;
    AmbientSteering = true;
    AmbientFlockHeading = 0.f;
    CurrentAction = 0;
    PriorAction = 0;

    ExtState = 0;
    Teleport = 0;
    Kind = 0;
    Skill = 0;
    m_byNumCloth = 0;
    m_byHurtByDeathstab = 0;
    WeaponLevel = 0;
    DamageTime = 0;
    m_byBuildTime = 0;
    m_bySkillCount = 0;
    m_bySkillSerialNum = 0;
    Block = 0;

    ScreenX = 0;
    ScreenY = 0;
    PKKey = 0;
    Weapon = 0;

    Type = 0;
    SubType = 0;
    m_iAnimation = 0;
    HiddenMesh = 0;
    LifeTime = 0;
    BlendMesh = 0;
    AttackPoint[0] = 0;
    AttackPoint[1] = 0;
    RenderType = 0;
    InitialSceneTime = 0;
    LinkBone = 0;

    m_dwTime = 0;

    Scale = 0.0f;
    BlendMeshLight = 0.0f;
    BlendMeshTexCoordU = 0.0f;
    BlendMeshTexCoordV = 0.0f;
    MaterialJitterU = MaterialJitterV = 0.f;
    Timer = 0.0f;
    m_fEdgeScale = 0.0f;
    Velocity = 0.0f;
    CollisionRange = 0.0f;
    ShadowScale = 0.0f;
    Gravity = 0.0f;
    Distance = 0.0f;
    AnimationFrame = 0.0f;
    PriorAnimationFrame = 0.0f;
    AnimationCycleEnded = false;
    AlphaTarget = 0.0f;
    Alpha = 0.0f;
    HorseSkillTicks = 0.0f;
    PKKey = 0.0f;

    IdentityMatrix(Matrix);

    IdentityVector3D(Light);
    IdentityVector3D(Direction);
    IdentityVector3D(m_vPosSword);
    IdentityVector3D(StartPosition);
    IdentityVector3D(BoundingBoxMin);
    IdentityVector3D(BoundingBoxMax);
    IdentityVector3D(m_vDownAngle);
    IdentityVector3D(m_vDeadPosition);
    IdentityVector3D(Position);
    IdentityVector3D(Angle);
    IdentityVector3D(HeadAngle);
    IdentityVector3D(HeadTargetAngle);
    IdentityVector3D(EyeLeft);
    IdentityVector3D(EyeRight);
    IdentityVector3D(EyeLeft2);
    IdentityVector3D(EyeRight2);
    IdentityVector3D(EyeLeft3);
    IdentityVector3D(EyeRight3);
    IdentityVector3D(m_v3PrePos1);
    IdentityVector3D(m_v3PrePos2);

    IdentityVector3D(OBB.StartPos);
    IdentityVector3D(OBB.XAxis);
    IdentityVector3D(OBB.YAxis);
    IdentityVector3D(OBB.ZAxis);

    m_pCloth = NULL;
    BoneTransform = NULL;

    Owner = NULL;
    Prior = NULL;
    Next = NULL;

    m_sTargetIndex = -1;
    m_Interpolates.ClearContainer();
    m_BuffMap.ClearBuff();
}

void OBJECT::Destroy()
{
    //m_BuffMap.ClearBuff();
}

World::World(SessionKeeper &keeper, SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), physics_(std::make_unique<CPhysicsManager>(keeper)),
      observer_(observer), terrain_{keeper.Id()}, entities_{keeper.Id()}, characters_{keeper.Id()},
      items_{keeper.Id()}, map_{keeper.Id()}, pathing_{keeper.Id()}, gameplayEffects_{keeper.Id()},
      readView_(*this), commandView_(*this)
{
    (void)sessionKeeper_.RegisterWorld(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::WorldConstructed);
    }
}

World::~World()
{
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::WorldDestroyed);
    }
}

WorldReadView &World::ReadView() noexcept
{
    return readView_;
}

WorldCommandView &World::Commands() noexcept
{
    return commandView_;
}

bool World::SelectMap(int rawMap, std::optional<std::uint32_t> route) noexcept
{
    const MapDefinition *definition = MapDefinition::Find(rawMap);
    if (definition == nullptr)
        return false;
    if (definition->scene != MapDefinition::Scene::Gameplay)
        route.reset();
    sessionKeeper_.WorldState() = {definition, route};
    TheMapProcess().BindCurrentMap();
    return true;
}

const WorldBinding &World::Binding() const noexcept
{
    return sessionKeeper_.WorldState();
}

SessionId World::OwnerSession() const noexcept
{
    return sessionKeeper_.Id();
}

std::uint64_t World::Revision() const noexcept
{
    return revision_;
}

WorldCapabilityStatus World::TerrainStatus() const noexcept
{
    return WorldCapabilityStatus::SessionOwned(terrain_.owner);
}

WorldCapabilityStatus World::EntityRegistryStatus() const noexcept
{
    return WorldCapabilityStatus::FrozenLegacy(entities_.owner);
}

WorldCapabilityStatus World::CharacterRegistryStatus() const noexcept
{
    return WorldCapabilityStatus::FrozenLegacy(characters_.owner);
}

WorldCapabilityStatus World::ItemRegistryStatus() const noexcept
{
    return WorldCapabilityStatus::FrozenLegacy(items_.owner);
}

WorldCapabilityStatus World::MapStatus() const noexcept
{
    return WorldCapabilityStatus::SessionOwned(map_.owner);
}

WorldCapabilityStatus World::PathStatus() const noexcept
{
    return WorldCapabilityStatus::SessionOwned(pathing_.owner);
}

WorldCapabilityStatus World::GameplayEffectStatus() const noexcept
{
    return WorldCapabilityStatus::FrozenLegacy(gameplayEffects_.owner);
}

WorldCapabilityStatus World::ModelClockStatus() const noexcept
{
    return WorldCapabilityStatus::SessionOwned(sessionKeeper_.Id());
}

WorldCapabilityStatus World::ModelRandomStatus() const noexcept
{
    return WorldCapabilityStatus::SessionOwned(sessionKeeper_.Id());
}

std::optional<std::uint64_t> World::ModelFrameNumber() const noexcept
{
    const SessionClock *clock = sessionKeeper_.Clock();
    if (clock == nullptr)
    {
        return std::nullopt;
    }
    return clock->FrameNumber();
}

void World::AdvanceModel() noexcept
{
    ++revision_;
}

std::optional<std::uint64_t> World::NextModelRandom() noexcept
{
    SessionRandom *random = sessionKeeper_.Random();
    if (random == nullptr)
    {
        return std::nullopt;
    }
    return random->Next();
}

bool World::FinishLoad(bool succeeded) noexcept
{
    preparedResources_.reset();
    preparedBinding_ = {};
    loadState_ = succeeded ? LoadState::Active : LoadState::Failed;
    awaitingTransfer_ = false;
    if (!succeeded)
    {
        sessionKeeper_.NetworkStorage().MainSceneReady = false;
        sessionKeeper_.InterfaceStorage().LockInputStatus = true;
        sessionKeeper_.CharacterPopulationStorage().ClearWorldInstance();
        sessionKeeper_.Lifecycle()->BeginAdvance();
    }
    return succeeded;
}

void World::ResetLoad() noexcept
{
    ++transitionSequence_;
    preparedResources_.reset();
    preparedBinding_ = {};
    loadState_ = LoadState::Empty;
    CancelTransfer();
    sessionKeeper_.CharacterPopulationStorage().ClearWorldInstance();
    sessionKeeper_.WorldState() = {};
    TheMapProcess().BindCurrentMap();
    sessionKeeper_.InterfaceStorage().LockInputStatus = false;
}

void World::BeginTransfer() noexcept
{
    awaitingTransfer_ = true;
    transferStopRequested_ = true;
    gateCooldownMilliseconds_ = 0;
}

void World::CancelTransfer() noexcept
{
    awaitingTransfer_ = false;
    transferStopRequested_ = false;
    gateCooldownMilliseconds_ = 0;
}

void World::StartArrivalCooldown(std::uint32_t milliseconds) noexcept
{
    awaitingTransfer_ = false;
    transferStopRequested_ = true;
    StartGateCooldown(milliseconds);
}

void World::StartGateCooldown(std::uint32_t milliseconds) noexcept
{
    gateCooldownMilliseconds_ = milliseconds;
}

void World::AdvanceTransferTime(double elapsedMilliseconds) noexcept
{
    gateCooldownMilliseconds_ = (std::max)(0.0, gateCooldownMilliseconds_ - elapsedMilliseconds);
}

bool World::ConsumeTransferStop() noexcept
{
    return std::exchange(transferStopRequested_, false);
}

void World::BeginSimulationTick(float animationFactor) noexcept
{
    simulationAnimationFactor_ = animationFactor;
    simulationTimeMilliseconds_ +=
        animationFactor * (1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    physics_->PrepareWind(animationFactor);
    sessionKeeper_.SpritesStorage().BeginTick();
}

void World::EndSimulationTick() noexcept
{
    physics_->Move(0.025f, simulationAnimationFactor_, simulationTimeMilliseconds_);
    sessionKeeper_.SpritesStorage().EndTick();
}

int SessionLegacyCalls::WorldRandom()
{
    return sessionKeeper_.RandomForConstruction().RangeInt(0, RAND_MAX);
}

double SessionLegacyCalls::WorldSimulationTime() const noexcept
{
    return sessionKeeper_.WorldUnit()->SimulationTime();
}

void SessionLegacyCalls::GetNearRandomPos(vec3_t position, int range, vec3_t result)
{
    VectorCopy(position, result);
    for (int axis = 0; axis < 3; ++axis)
        result[axis] += static_cast<float>(WorldRandom() % (range * 2 + 1) - range);
}

WorldCommandView::WorldCommandView(World &world) noexcept : world_(world)
{
}

void WorldCommandView::AdvanceModel() noexcept
{
    world_.AdvanceModel();
}

std::optional<std::uint64_t> WorldCommandView::NextModelRandom() noexcept
{
    return world_.NextModelRandom();
}

WorldTransition::WorldTransition(WorldTransition &&other) noexcept
    : world_(std::exchange(other.world_, nullptr)), reload_(other.reload_),
      sequence_(other.sequence_)
{
}

WorldTransition::~WorldTransition()
{
    if (world_ != nullptr && world_->transitionSequence_ == sequence_)
        world_->FinishLoad(false);
}

bool WorldTransition::Activate() noexcept
{
    return world_ != nullptr && world_->transitionSequence_ == sequence_ &&
           world_->ActivatePrepared();
}

bool WorldTransition::Complete() noexcept
{
    World *world = std::exchange(world_, nullptr);
    return world != nullptr && world->transitionSequence_ == sequence_ &&
           world->CompleteTransition();
}

bool CMapManager::ActivateWorld(const MapDefinition &definition, WorldResources &resources)
{
    const int rawMap = definition.id.RawValue();

    g_Direction.Init();
    g_Direction.HeroFallingDownInit();
    g_Direction.DeleteMonster();
    Kanturu3rdInit();
    g_Direction.m_CKanturu.m_iKanturuState = 0;
    g_Direction.m_CKanturu.m_iMayaState = 0;
    g_Direction.m_CKanturu.m_iNightmareState = 0;

    if (!InstallWorldAssets(resources))
        return false;
    TheMapProcess().InstallBehavior();

    InstallWorldLayout(resources);
    CreateTerrain(resources.Height(), definition.extendedHeight);
    InstallTerrainLight(resources.Light());
    sessionKeeper_.WorldUnit()->CommitTerrainBase(resources.TerrainAsset());
    BattleCastleForUse().Init();
    InstallWorldPlacements(resources.Placements());

    CreateWaterTerrain(this->ContextMap());

    if (rawMap != WD_73NEW_LOGIN_SCENE && rawMap != WD_74NEW_CHARACTER_SCENE)
    {
        if (!g_pNewUIMiniMap->InstallImages(resources))
            return false;
    }
    return true;
}

namespace WorldLoadingDetail
{
void ReleaseWorldBehaviorModels(SessionKeeper &keeper, const MapDefinition *definition)
{
    if (definition == nullptr)
        return;
    for (const auto &dependency : WorldModelDependency::For(definition->BehaviorMap()))
    {
        // Primary/NPC/monster ranges have already been retired by their existing owners.
        if (dependency.slot < MAX_WORLD_OBJECTS || dependency.slot >= MODEL_MONSTER01 ||
            dependency.lifetime == WorldModelDependency::Lifetime::SessionBasic)
            continue;
        if (auto *model = keeper.ModelPoolObject().Find(dependency.slot))
            model->Release();
    }
}
} // namespace WorldLoadingDetail

std::optional<WorldTransition> World::PrepareTransition(WorldTransition::Reason reason, int rawMap,
                                                        std::optional<std::uint32_t> route) noexcept
{
    using Reason = WorldTransition::Reason;
    if (loadState_ == LoadState::Preparing || loadState_ == LoadState::Prepared ||
        loadState_ == LoadState::Entering)
        return std::nullopt;
    const bool wasActive = loadState_ == LoadState::Active;
    ++transitionSequence_;
    loadState_ = LoadState::Preparing;
    try
    {
        const auto *definition = MapDefinition::Find(rawMap);
        const bool networkEntry =
            reason == Reason::Join || reason == Reason::Revival || reason == Reason::Teleport;
        if (definition == nullptr ||
            (networkEntry && (!route || definition->scene != MapDefinition::Scene::Gameplay)) ||
            (reason == Reason::Login && definition->scene != MapDefinition::Scene::Login) ||
            (reason == Reason::CharacterSelection &&
             definition->scene != MapDefinition::Scene::Character))
        {
            FinishLoad(false);
            return std::nullopt;
        }
        if (definition->scene != MapDefinition::Scene::Gameplay)
            route.reset();
        preparedBinding_ = {definition, route};
        const bool reload = !wasActive ||
                            (reason != Reason::Revival && reason != Reason::Teleport) ||
                            Binding().definition != definition || Binding().route != route;
        if (reload && !PrepareResources())
            return std::nullopt;
        loadState_ = LoadState::Prepared;
        return WorldTransition(*this, reload, transitionSequence_);
    }
    catch (const std::exception &error)
    {
        ReportTransitionFailure(rawMap, error.what());
    }
    catch (...)
    {
        ReportTransitionFailure(rawMap, "unknown preparation error");
    }
    return std::nullopt;
}

bool World::ActivatePrepared() noexcept
{
    if (loadState_ != LoadState::Prepared)
        return FinishLoad(false);
    const auto destination = preparedBinding_;
    auto resources = std::move(preparedResources_);
    preparedResources_.reset();
    loadState_ = LoadState::Entering;
    try
    {
        if (!resources)
            return true;
        DetachForTransition();
        SelectMap(destination.definition->id.RawValue(), destination.route);
        if (!sessionKeeper_.MapManagerObject().ActivateWorld(*destination.definition, *resources))
            return FinishLoad(false);
        auto &characters = sessionKeeper_.CharacterPopulationStorage();
        if (const auto instance = Binding().CharacterInstance())
            characters.SetWorldInstance(*instance);
        else
            characters.ClearWorldInstance();
        return true;
    }
    catch (const std::exception &error)
    {
        ReportTransitionFailure(destination.definition->id.RawValue(), error.what());
    }
    catch (...)
    {
        ReportTransitionFailure(destination.definition->id.RawValue(), "unknown activation error");
    }
    return false;
}

bool World::CompleteTransition() noexcept
{
    return FinishLoad(loadState_ == LoadState::Entering);
}

bool World::Enter(WorldTransition::Reason reason, int rawMap,
                  std::optional<std::uint32_t> route) noexcept
{
    auto transition = PrepareTransition(reason, rawMap, route);
    return transition && transition->Activate() && transition->Complete();
}

void World::ReportTransitionFailure(int rawMap, const char *message) noexcept
{
    FinishLoad(false);
    try
    {
        sessionKeeper_.ErrorReport().Write(L"World%d transition failed: %hs\r\n", rawMap, message);
    }
    catch (...)
    {
    } // Reporting cannot turn one failed world into application failure.
}

void World::DetachForTransition()
{
    StopMusic();
    AllStopSound();
    RemoveAllShopTitleExceptHero();
    if (SceneFlag != CHARACTER_SCENE)
    {
        ClearItems();
        ClearCharacters(HeroKey);
    }
    ReleaseSceneResources();
}

void World::Leave(LeaveReason reason) noexcept
{
    if (reason != LeaveReason::SceneChange)
    {
        sessionKeeper_.Gameplay()->ClearInventoryState();
        sessionKeeper_.SkillManagerObject().SetHeroPriorSkill(0);
    }
    physics_->RemoveAll();
    MainSceneReady = false;
    StopMusic();
    AllStopSound();
    RemoveAllShopTitle();
    ClearCharacters();
    ClearItems();
    ReleaseSceneResources();
    if (reason == LeaveReason::Logout && Hero != nullptr)
    {
        const SHORT controlledKey = Hero->Key;
        delete[] Hero->Object.BoneTransform;
        Hero->Object.BoneTransform = nullptr;
        Hero->ResetIdentity();
        Hero->Key = controlledKey;
    }
    ResetLoad();
}

namespace WorldObjectDetail
{
int AdvanceGateMotion(OBJECT &gate, float life, float frames)
{
    constexpr float Acceleration = 1.5f, OpenAngle = 90.f, ReboundVelocity = 2.f;
    int dustBursts = 0;
    float remaining = (std::min)(frames, (std::max)(0.f, life));
    float elapsed = 0.f;
    while (remaining > 0.f)
    {
        if (gate.EffectMotionFrames <= 0.f)
        {
            float angle = gate.Angle[0] + gate.Gravity;
            gate.Gravity += Acceleration;
            gate.EffectResting = angle >= OpenAngle;
            if (gate.EffectResting)
            {
                angle -= std::round(life - elapsed);
                gate.Gravity = ReboundVelocity;
            }
            gate.EffectMotionVelocity[0] = angle;
            gate.EffectMotionAngleRate[0] = angle - gate.Angle[0];
            gate.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(remaining, gate.EffectMotionFrames);
        gate.Angle[0] += gate.EffectMotionAngleRate[0] * step;
        gate.EffectMotionFrames -= step;
        remaining -= step;
        elapsed += step;
        if (gate.EffectMotionFrames <= 0.f)
        {
            gate.Angle[0] = gate.EffectMotionVelocity[0];
            if (gate.EffectResting && gate.Angle[0] == 80.f)
                ++dustBursts;
        }
    }
    return dustBursts;
}

} // namespace WorldObjectDetail

//int   World = -1;

void SessionGameplayUnit::ClearActionObject()
{
    g_iActionObjectType = -1;
    g_iActionWorld = -1;
    g_iActionTime = -1;
    g_fActionObjectVelocity = -1;
}

void SessionGameplayUnit::SetActionObject(int iWorld, int iType, int iLifeTime, int iVel)
{
    g_iActionWorld = iWorld;
    g_iActionObjectType = iType;
    g_iActionTime = iLifeTime;
    g_fActionObjectVelocity = (float)iVel;
}

void SessionGameplayUnit::ActionObject(OBJECT *o)
{
    if (g_iActionWorld < 0)
        return;
    if (g_iActionObjectType < 0)
        return;
    if (g_iActionTime < 0)
        return;

    if (gMapManager.ContextMap() == g_iActionWorld)
    {
        if (TheMapProcess().ActionObject(o))
            return;
        {
            vec3_t Position, Light;

            Vector(1.f, 1.f, 1.f, Light);
            if (o->Type == g_iActionObjectType || o->Type == 9 || o->Type == 10)
            {
                if (o->Type == 9)
                {
                    if (FPS_ANIMATION_FACTOR > 0.f && g_iActionTime <= FPS_ANIMATION_FACTOR)
                    {
                        o->SetHiddenMesh(-1);
                        o->PKKey = 4;
                    }
                }
                else if (o->Type == 10)
                {
                    if (FPS_ANIMATION_FACTOR > 0.f && g_iActionTime <= FPS_ANIMATION_FACTOR)
                    {
                        o->SetHiddenMesh(-1);
                        o->PKKey = 4;
                    }
                }
                else if (o->Type == g_iActionObjectType)
                {
                    if (o->RigidPose)
                        o->RigidPoseDirty = true;
                    if (Core::Time::Reaches(g_iActionTime, FPS_ANIMATION_FACTOR, 20.f))
                    {
                        o->Angle[0] = 35.f;
                        o->Gravity = g_fActionObjectVelocity;
                        o->EffectMotionFrames = 0.f;
                        o->SetHiddenMesh(-1);

                        PlayBuffer(SOUND_DOWN_GATE);
                    }

                    if (g_iActionTime >= 0)
                    {
                        for (int bursts = WorldObjectDetail::AdvanceGateMotion(
                                 *o, g_iActionTime, FPS_ANIMATION_FACTOR);
                             bursts > 0; --bursts)
                        {
                            for (int particle = 0; particle < 10; ++particle)
                            {
                                VectorCopy(o->Position, Position);
                                Position[0] += WorldRandom() % 300 - 150.f;
                                Position[1] -= WorldRandom() % 20 + 600.f;
                                CreateParticle(BITMAP_SMOKE + 1, Position, o->Angle, o->Light);
                            }
                        }
                        if (FPS_ANIMATION_FACTOR > 0.f && g_iActionTime <= FPS_ANIMATION_FACTOR)
                        {
                            o->SetHiddenMesh(-2);
                            o->Angle[0] = 90.f;
                            // Clear the shared action after every placement has consumed this interval.

                            AddTerrainAttributeRange(13, 70, 3, 6, TW_NOGROUND, false);
                        }
                    }
                }
            }
        }
    }
}

namespace WorldObjectDetail
{
bool MatchesPreparedDrawPose(const ObjectDrawInput &draw, const BMD &model, bool translate,
                             float animationFrame)
{
    if (!draw.preparedPose)
        return false;
    AnimationPoseSample sample(draw, model.BoneHead, model.BodyHeight, !translate,
                               model.PoseAssetIdentity());
    sample.frame = animationFrame;
    if (sample.translated)
    {
        VectorCopy(model.BodyOrigin, sample.origin.data());
        sample.scale = model.BodyScale;
    }
    return *draw.preparedPose == sample;
}
} // namespace WorldObjectDetail

void SessionGameplayUnit::CreateOperate(OBJECT *Owner)
{
    for (int i = 0; i < MAX_OPERATES; i++)
    {
        OPERATE *o = &Operates[i];
        if (!o->Live)
        {
            o->Live = true;
            o->Owner = Owner;
            return;
        }
    }
}

OBJECT *SessionGameplayUnit::CreateObject(int Type, vec3_t Position, vec3_t Angle, float Scale)
{
    int i = (int)(Position[0] / (16 * TERRAIN_SCALE));
    int j = (int)(Position[1] / (16 * TERRAIN_SCALE));
    if (i < 0 || j < 0 || i >= 16 || j >= 16)
        return NULL;

    BYTE Block = i * 16 + j;
    OBJECT_BLOCK *ob = &ObjectBlock[Block];
    ob->DrawGroupsDirty = true;
    auto *o = new OBJECT;

    o->Initialize();

    if (ob->Head == NULL)
    {
        o->Prior = NULL;
        o->Next = NULL;
        ob->Head = o;
        ob->Tail = o;
    }
    else
    {
        ob->Tail->Next = o;
        o->Prior = ob->Tail;
        o->Next = NULL;
        ob->Tail = o;
    }
    o->Live = true;
    o->Visible = false;
    o->AlphaEnable = false;
    o->SetLightEnable(true);
    o->ContrastEnable = false;
    o->EnableBoneMatrix = false;

    o->m_bCollisionCheck = false;

    o->Type = Type;
    o->Scale = Scale;
    o->Alpha = 1.f;
    o->AlphaTarget = 1.f;
    o->Velocity = 0.16f;
    o->ShadowScale = 50.f;
    o->SetHiddenMesh(-1);
    o->CurrentAction = 0;
    o->PriorAction = 0;
    o->AnimationFrame = 0.f;
    o->PriorAnimationFrame = 0.f;
    o->Block = Block;
    o->SetBlendMesh(-1);
    o->BlendMeshLight = 1.f;
    o->BlendMeshTexCoordU = 0.f;
    o->BlendMeshTexCoordV = 0.f;
    g_CharacterClearBuff(o);
    o->CollisionRange = -30.f;
    o->Timer = 0.f;
    o->m_bpcroom = FALSE;
    o->m_bRenderAfterCharacter = gMapManager.IsCursedTemple() &&
                                 (o->Type == 64 || o->Type == 65 || o->Type == 66 || o->Type == 80);

    VectorCopy(Position, o->Position);
    VectorCopy(Position, o->StartPosition);
    VectorCopy(Angle, o->Angle);
    Vector(0.f, 0.f, 0.f, o->Light);
    Vector(0.f, 0.f, 0.f, o->HeadAngle);
    Vector(0.f, 0.f, 0.f, o->Direction);
    Vector(-40.f, -40.f, 0.f, o->BoundingBoxMin);
    Vector(40.f, 40.f, 80.f, o->BoundingBoxMax);
    if (SceneFlag == LOG_IN_SCENE || SceneFlag == CHARACTER_SCENE)
    {
        switch (Type)
        {
        case MODEL_SHIP:
            o->Scale = 0.8f;
            Vector(0.2f, 0.2f, 0.2f, o->Light);
            o->SetLightEnable(true);
            break;
        case MODEL_WAVEBYSHIP:
            o->Scale = 0.8f;
            o->SetBlendMesh(0);
            o->BlendMeshLight = 1.f;
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        case MODEL_MUGAME:
            o->Scale = 2.2f;
            o->SetBlendMesh(1);
            o->SetLightEnable(false);
            break;
        case MODEL_LOGO:
            o->SetBlendMesh(1);
            o->BlendMeshLight = 1.f;
            o->Scale = 5.f;
            o->Scale = 0.044f;
            Vector(1.f, 1.f, 1.f, o->Light);
            o->SetLightEnable(false);
            break;
        case MODEL_LOGOSUN:
            o->Scale = 3.f;
            o->SetBlendMesh(0);
            Vector(0.5f, 0.5f, 0.5f, o->Light);
            break;
        case MODEL_LOGO + 4:
            o->SetBlendMesh(10);
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        case MODEL_DRAGON:
            o->SetHiddenMesh(-2);
            Vector(1.f, 1.f, 1.f, o->Light);
            CreateEffect(MODEL_DRAGON, o->Position, o->Angle, o->Light, 0, o, -1, 0, 0, 0, 1.6f);
            break;
        }
    }
    TheMapProcess().CreateObject(o);
    sessionKeeper_.Visual()->PrepareRigidObjectPose(*o);
    return o;
}

void DeleteObject(OBJECT *o, OBJECT_BLOCK *ob)
{
    ob->DrawGroupsDirty = true;
    if (o != NULL)
    {
        OBJECT *Next = o->Next;
        OBJECT *Prior = o->Prior;
        if (Next != NULL)
        {
            if (Prior != NULL)
            {
                Prior->Next = o->Next;
                Next->Prior = o->Prior;
            }
            else
            {
                Next->Prior = NULL;
                ob->Head = Next;
            }
        }
        else
        {
            if (Prior != NULL)
            {
                Prior->Next = NULL;
                ob->Tail = Prior;
            }
            else
            {
                ob->Head = NULL;
                ob->Tail = NULL;
            }
        }
        SAFE_DELETE(o);
    }
}

void SessionGameplayUnit::DeleteObjectTile(int x, int y)
{
    for (int i = 0; i < 16; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            OBJECT_BLOCK *ob = &ObjectBlock[i * 16 + j];
            OBJECT *o = ob->Head;
            while (1)
            {
                if (o != nullptr && reinterpret_cast<std::uintptr_t>(o) !=
                                        WorldObjectDetail::FreedObjectPointerPattern)
                {
                    if (o->Live && (int)(o->Position[0] / TERRAIN_SCALE) == x &&
                        (int)(o->Position[1] / TERRAIN_SCALE) == y)
                        DeleteObject(o, ob);
                    if (o->Next == NULL)
                        break;
                    o = o->Next;
                }
                else
                    break;
            }
        }
    }
}

void SessionGameplayUnit::CreateMoneyDrop(ITEM_t *ip, int amount, vec3_t position, bool isFreshDrop)
{
    sessionKeeper_.Gameplay()->RetireGroundItem(*ip);
    ++ip->Generation;
    int Type = ITEM_ZEN;
    ITEM *n = &ip->Item;
    n->Type = Type;
    n->Level = amount;
    n->Durability = 0;
    n->ExcellentFlags = 0;
    n->AncientDiscriminator = 0;
    if (isFreshDrop)
    {
        PlayBuffer(SOUND_DROP_GOLD01);
    }

    OBJECT *o = &ip->Object;
    o->Live = true;
    o->Type = MODEL_ITEM + Type;
    o->SubType = 1;

    ItemObjectAttribute(o);
    Vector(-30.f, -30.f, -30.f, o->BoundingBoxMin);
    Vector(30.f, 30.f, 30.f, o->BoundingBoxMax);
    VectorCopy(position, o->Position);
    if (isFreshDrop)
    {
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 180.f;
        o->Gravity = 20.f;
    }
    else
    {
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
    }

    ItemAngle(o);
}

void SessionGameplayUnit::CreateShiny(OBJECT *o)
{
    vec3_t p, Position;
    if (o->SubType++ % 48 == 0)
    {
        float Matrix[3][4];
        AngleMatrix(o->Angle, Matrix);
        Vector((float)(WorldRandom() % 32 + 16), 0.f, (float)(WorldRandom() % 32 + 16), p);
        VectorRotate(p, Matrix, Position);
        VectorAdd(o->Position, Position, Position);
        vec3_t Light;
        Vector(1.f, 1.f, 1.f, Light);

        CreateParticle(BITMAP_SHINY, Position, o->Angle, Light);
        CreateParticle(BITMAP_SHINY, Position, o->Angle, Light, 1);
    }
}

void ItemHeight(int Type, BMD *b)
{
    if (Type >= MODEL_HELM && Type < MODEL_HELM + MAX_ITEM_INDEX)
        b->BodyHeight = -160.f;
    else if (Type >= MODEL_ARMOR && Type < MODEL_ARMOR + MAX_ITEM_INDEX)
        b->BodyHeight = -100.f;
    else if (Type >= MODEL_GLOVES && Type < MODEL_GLOVES + MAX_ITEM_INDEX)
        b->BodyHeight = -70.f;
    else if (Type >= MODEL_PANTS && Type < MODEL_PANTS + MAX_ITEM_INDEX)
        b->BodyHeight = -50.f;
    else if (Type >= MODEL_BOOTS && Type < MODEL_BOOTS + MAX_ITEM_INDEX)
        b->BodyHeight = 0.f;
    else
        b->BodyHeight = 0.f;
}

namespace WorldObjectDetail
{
void ApplyPartPalette(int Color, float Alpha, float Bright, vec3_t Light)
{
    Bright *= Alpha;
    switch (Color)
    {
    case 0:
        Vector(Bright * 1.0f, Bright * 0.5f, Bright * 0.0f, Light);
        break;
    case 1:
        Vector(Bright * 1.0f, Bright * 0.2f, Bright * 0.0f, Light);
        break;
    case 2:
        Vector(Bright * 0.0f, Bright * 0.5f, Bright * 1.0f, Light);
        break;
    case 3:
        Vector(Bright * 0.0f, Bright * 0.5f, Bright * 1.0f, Light);
        break;
    case 4:
        Vector(Bright * 0.0f, Bright * 0.8f, Bright * 0.4f, Light);
        break;
    case 5:
        Vector(Bright * 1.0f, Bright * 1.0f, Bright * 1.0f, Light);
        break;
    case 6:
        Vector(Bright * 0.6f, Bright * 0.8f, Bright * 0.4f, Light);
        break;
    case 7:
        Vector(Bright * 0.9f, Bright * 0.8f, Bright * 1.0f, Light);
        break;
    case 8:
        Vector(Bright * 0.8f, Bright * 0.8f, Bright * 1.0f, Light);
        break;
    case 9:
        Vector(Bright * 0.5f, Bright * 0.5f, Bright * 0.8f, Light);
        break;
    case 10:
        Vector(Bright * 0.75f, Bright * 0.65f, Bright * 0.5f, Light);
        break;
    case 11:
        Vector(Bright * 0.35f, Bright * 0.35f, Bright * 0.6f, Light);
        break;
    case 12:
        Vector(Bright * 0.47f, Bright * 0.67f, Bright * 0.6f, Light);
        break;
    case 13:
        Vector(Bright * 0.0f, Bright * 0.3f, Bright * 0.6f, Light);
        break;
    case 14:
        Vector(Bright * 0.65f, Bright * 0.65f, Bright * 0.55f, Light);
        break;
    case 15:
        Vector(Bright * 0.2f, Bright * 0.3f, Bright * 0.6f, Light);
        break;
    case 16:
        Vector(Bright * 0.8f, Bright * 0.46f, Bright * 0.25f, Light);
        break;
    case 17:
        Vector(Bright * 0.65f, Bright * 0.45f, Bright * 0.3f, Light);
        break;
    case 18:
        Vector(Bright * 0.5f, Bright * 0.4f, Bright * 0.3f, Light);
        break;
    case 19:
        Vector(Bright * 0.37f, Bright * 0.37f, Bright * 1.0f, Light);
        break;
    case 20:
        Vector(Bright * 0.3f, Bright * 0.7f, Bright * 0.3f, Light);
        break;
    case 21:
        Vector(Bright * 0.5f, Bright * 0.4f, Bright * 1.0f, Light);
        break;
    case 22:
        Vector(Bright * 0.45f, Bright * 0.45f, Bright * 0.23f, Light);
        break;
    case 23:
        Vector(Bright * 0.3f, Bright * 0.3f, Bright * 0.45f, Light);
        break;
    case 24:
        Vector(Bright * 0.6f, Bright * 0.5f, Bright * 0.2f, Light);
        break;
    case 25:
        Vector(Bright * 0.6f, Bright * 0.6f, Bright * 0.6f, Light);
        break;
    case 26:
        Vector(Bright * 0.3f, Bright * 0.7f, Bright * 0.3f, Light);
        break;
    case 27:
        Vector(Bright * 0.5f, Bright * 0.6f, Bright * 0.7f, Light);
        break;
    case 28:
        Vector(Bright * 0.45f, Bright * 0.45f, Bright * 0.23f, Light);
        break;
    case 29:
        Vector(Bright * 0.2f, Bright * 0.7f, Bright * 0.3f, Light);
        break;
    case 30:
        Vector(Bright * 0.7f, Bright * 0.3f, Bright * 0.3f, Light);
        break;
    case 31:
        Vector(Bright * 0.7f, Bright * 0.5f, Bright * 0.3f, Light);
        break;
    case 32:
        Vector(Bright * 0.5f, Bright * 0.2f, Bright * 0.7f, Light);
        break;
    case 33:
        Vector(Bright * 0.8f, Bright * 0.4f, Bright * 0.6f, Light);
        break;
    case 34:
        Vector(Bright * 0.6f, Bright * 0.4f, Bright * 0.8f, Light);
        break;
    case 35:
        Vector(Bright * 0.7f, Bright * 0.4f, Bright * 0.4f, Light);
        break;
    case 36:
        Vector(Bright * 0.5f, Bright * 0.5f, Bright * 0.7f, Light);
        break;
    case 37:
        Vector(Bright * 0.7f, Bright * 0.5f, Bright * 0.7f, Light);
        break;
    case 38:
        Vector(Bright * 0.2f, Bright * 0.4f, Bright * 0.7f, Light);
        break;
    case 39:
        Vector(Bright * 0.3f, Bright * 0.6f, Bright * 0.4f, Light);
        break;
    case 40:
        Vector(Bright * 0.7f, Bright * 0.2f, Bright * 0.2f, Light);
        break;
    case 41:
        Vector(Bright * 0.7f, Bright * 0.2f, Bright * 0.7f, Light);
        break;
    case 42:
        Vector(Bright * 0.8f, Bright * 0.4f, Bright * 0.0f, Light);
        break;
    case 43:
        Vector(Bright * 0.8f, Bright * 0.6f, Bright * 0.2f, Light);
        break;
    case 44:
        Vector(Bright * 0.8f, Bright * 0.7f, Bright * 0.4f, Light);
        break;
    case 45:
        Vector(Bright * 0.5f, Bright * 0.8f, Bright * 0.9f, Light);
        break;
    }
}
} // namespace WorldObjectDetail

namespace WorldObjectDetail
{
void ApplyPartModulation(int Color, float Alpha, float Bright, vec3_t Light)
{
    Bright *= Alpha;
    switch (Color)
    {
    case 0:
        Vector(Bright * 1.0f * Light[0], Bright * 1.0f * Light[1], Bright * 1.0f * Light[2], Light);
        break;
    case 1:
        Vector(Bright * 1.0f * Light[0], Bright * 0.5f * Light[1], Bright * 0.0f * Light[2], Light);
        break;
    case 2:
        Vector(Bright * 0.0f * Light[0], Bright * 0.5f * Light[1], Bright * 1.0f * Light[2], Light);
        break;
    case 3:
        Vector(1.f, 1.f, 1.f, Light); //
    }
}
} // namespace WorldObjectDetail

void PartObjectColor3(int Type, float Alpha, float Bright, vec3_t Light, bool ExtraMon)
{
    int Color = 0;
    int ItemType = Type - MODEL_ITEM;

    if (ItemType / MAX_ITEM_INDEX >= 7 && ItemType / MAX_ITEM_INDEX <= 11)
    {
        switch (ItemType % MAX_ITEM_INDEX)
        {
        case 0:
            Color = 0;
            break;
        case 1:
            Color = 0;
            break;
        case 2:
            Color = 0;
            break;
        case 3:
            Color = 1;
            break;
        case 4:
            Color = 0;
            break;
        case 5:
            Color = 0;
            break;
        case 6:
            Color = 0;
            break;
        case 7:
            Color = 0;
            break;
            //		case 7 :Color=44;break;
        case 8:
            Color = 0;
            break;
        case 9:
            Color = 1;
            break;
        case 10:
            Color = 0;
            break;
        case 11:
            Color = 0;
            break;
        case 12:
            Color = 0;
            break;
        case 13:
            Color = 0;
            break;
        case 14:
            Color = 0;
            break;
        case 15:
            Color = 0;
            break;
        case 16:
            Color = 0;
            break;
        case 17:
            Color = 1;
            break;
        case 18:
            Color = 0;
            break;
        case 19:
            Color = 0;
            break;
        }
    }
    switch (Color)
    {
    case 0:
        Vector(0.1f, 0.6f, 1.0f, Light);
        break;
    case 1:
        Vector(1.f, 0.7f, 0.2f, Light);
        break;
    }
}

namespace WorldObjectDetail
{
bool HasCharacterPartCloth(int type)
{
    return type == MODEL_GRAND_SOUL_PANTS || type == MODEL_DIVINE_PANTS ||
           type == MODEL_DARK_SOUL_PANTS;
}
} // namespace WorldObjectDetail

void CopyShadowAngle(OBJECT *o, BMD *b)
{
}

void InitShadowAngle()
{
}

void ShadowAngle(OBJECT *Owner)
{
}

void RenderObjectShadow()
{
}

bool IsPartyMemberBuff(int partyindex, eBuffState buffstate)
{
    return false;
}

bool isPartyMemberBuff(int partyindex)
{
    return false;
}

_OBJECT_BLOCK::_OBJECT_BLOCK()
{
    Index = 0;
    Head = NULL;
    Tail = NULL;
    Visible = false;
}
