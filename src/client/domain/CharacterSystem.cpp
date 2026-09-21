#include "domain/CharacterSystem.h"
#include "render/Character.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "domain/CharacterPresentation.h"
#include "support/CoreMath.h"
#include "render/ModelGeometry.h"
#include "app/ApplicationLoopFrame.h"
#include "domain/WorldSimulation.h"
#include "domain/MovementAI.h"
#include "domain/ItemsSkills.h"
#include "domain/EffectsUpdate.h"
#include "render/World.h"
#include "app/ApplicationAudio.h"
#include "session/SessionNetwork.h"
#include "session/SessionKeeper.h"
#include "session/SessionGameplay.h"
#include "ui/features/Hud/HudLogic.h"
#include "session/SessionWorkspace.h"
#include "session/SessionRender.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "session/SessionPresentation.h"
#include "render/ModelResources.h"
#include "domain/MapSimulation.h"
#include "domain/Events.h"
#include "domain/ChatSocial.h"
#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "domain/Quests.h"
#include "session/SessionAudio.h"
#include "ui/session/UiSessionLogic.h"
#include "domain/Guild.h"
#include "render/Textures.h"
#include "data/Localization.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"
#include "I18N/All.h"
#include "domain/WorldPhysics.h"
#include "support/Camera.h"
#include "app/AppWindow.h"
#include "app/ApplicationNetwork.h"
#include "domain/Shop.h"
#include "session/SessionUi.h"

SessionCharacterPopulationStorage::~SessionCharacterPopulationStorage()
{
    boneManager_.UnregisterAll();
    if (sharedCharacters_ == nullptr || !sessionId_.has_value())
    {
        return;
    }

    for (const std::optional<SharedCharacterKey> &key : sharedKeys_)
    {
        if (key.has_value())
        {
            (void)sharedCharacters_->Unobserve(*key, *sessionId_);
        }
    }
}

bool SessionCharacterPopulationStorage::Initialize(SharedCharacterPool &sharedCharacters,
                                                   SessionId sessionId,
                                                   CHARACTER_MACHINE &characterMachine,
                                                   CHARACTER_ATTRIBUTE &characterAttribute) noexcept
{
    if (sharedCharacters_ != nullptr)
    {
        return false;
    }

    try
    {
        controlledCharacter_ = std::make_unique<CHARACTER>();
    }
    catch (...)
    {
        return false;
    }

    sharedCharacters_ = &sharedCharacters;
    sessionId_ = sessionId;
    Hero = controlledCharacter_.get();
    CharacterMachine = &characterMachine;
    CharacterAttribute = &characterAttribute;
    return true;
}

CHARACTER &SessionCharacterPopulationStorage::operator[](std::ptrdiff_t index) noexcept
{
    return index >= 0 && static_cast<std::size_t>(index) < slots_.size() &&
                   slots_[static_cast<std::size_t>(index)] != nullptr
               ? *slots_[static_cast<std::size_t>(index)]
               : invalidCharacter_;
}

const CHARACTER &SessionCharacterPopulationStorage::operator[](std::ptrdiff_t index) const noexcept
{
    return index >= 0 && static_cast<std::size_t>(index) < slots_.size() &&
                   slots_[static_cast<std::size_t>(index)] != nullptr
               ? *slots_[static_cast<std::size_t>(index)]
               : invalidCharacter_;
}

int SessionCharacterPopulationStorage::Size() const noexcept
{
    return slots_.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())
               ? (std::numeric_limits<int>::max)()
               : static_cast<int>(slots_.size());
}

bool SessionCharacterPopulationStorage::IsValidIndex(int index) const noexcept
{
    return index >= 0 && static_cast<std::size_t>(index) < slots_.size() &&
           slots_[static_cast<std::size_t>(index)] != nullptr;
}

std::span<CHARACTER *const> SessionCharacterPopulationStorage::Pointers() const noexcept
{
    return slots_;
}

CHARACTER *SessionCharacterPopulationStorage::AcquireLocalByKey(int key) noexcept
{
    const int existing = FindIndexByKey(key);
    if (existing >= 0)
    {
        return slots_[static_cast<std::size_t>(existing)];
    }

    const int index = AllocateSlot();
    return index >= 0 ? AcquireLocalAt(index, key) : nullptr;
}

CHARACTER *SessionCharacterPopulationStorage::AcquireLocalAt(int index, int key) noexcept
{
    if (index < 0 || !EnsureSlot(index))
    {
        return nullptr;
    }

    const std::size_t slot = static_cast<std::size_t>(index);
    if (slots_[slot] == nullptr)
    {
        try
        {
            localCharacters_[slot] = std::make_unique<CHARACTER>();
        }
        catch (...)
        {
            return nullptr;
        }
        std::erase(freeSlots_, index);
        slots_[slot] = localCharacters_[slot].get();
        slotKinds_[slot] = SlotKind::Local;
    }
    MapKey(index, key);
    slots_[slot]->Key = static_cast<SHORT>(key);
    boneManager_.Admit(&slots_[slot]->Object, slots_[slot]);
    return slots_[slot];
}

int SessionCharacterPopulationStorage::BindControlled(int key) noexcept
{
    if (controlledCharacter_ == nullptr)
    {
        return -1;
    }

    for (std::size_t index = 0; index < slots_.size(); ++index)
    {
        if (slotKinds_[index] == SlotKind::Controlled)
        {
            MapKey(static_cast<int>(index), key);
            controlledCharacter_->Key = static_cast<SHORT>(key);
            Hero = controlledCharacter_.get();
            boneManager_.Admit(&Hero->Object, Hero);
            return static_cast<int>(index);
        }
    }

    const int index = AllocateSlot();
    if (index < 0)
    {
        return -1;
    }
    const std::size_t slot = static_cast<std::size_t>(index);
    slots_[slot] = controlledCharacter_.get();
    slotKinds_[slot] = SlotKind::Controlled;
    MapKey(index, key);
    controlledCharacter_->Key = static_cast<SHORT>(key);
    Hero = controlledCharacter_.get();
    boneManager_.Admit(&Hero->Object, Hero);
    return index;
}

SessionCharacterPopulationStorage::Acquisition SessionCharacterPopulationStorage::ObserveRemote(
    int key) noexcept
{
    const int existing = FindIndexByKey(key);
    if (existing >= 0)
    {
        return {
            slots_[static_cast<std::size_t>(existing)],
            IsSource(existing),
        };
    }
    if (!worldInstance_.has_value() || sharedCharacters_ == nullptr || !sessionId_.has_value())
    {
        return {AcquireLocalByKey(key), true};
    }

    const SharedCharacterKey sharedKey = SharedKey(key);
    const SharedCharacterPool::Observation observation =
        sharedCharacters_->Observe(sharedKey, *sessionId_);
    if (observation.character == nullptr)
    {
        return {};
    }

    const int index = AllocateSlot();
    if (index < 0)
    {
        (void)sharedCharacters_->Unobserve(sharedKey, *sessionId_);
        return {};
    }

    const std::size_t slot = static_cast<std::size_t>(index);
    slots_[slot] = observation.character;
    slotKinds_[slot] = SlotKind::Shared;
    sharedKeys_[slot] = sharedKey;
    {
        auto access = sharedCharacters_->AcquireAccess(sharedKey);
        worldVisuals_[slot].BindAppearance(*observation.character);
        worldVisuals_[slot].CaptureSample(*observation.character);
    }
    MapKey(index, key);
    boneManager_.Admit(&observation.character->Object, observation.character);
    return {observation.character, observation.source};
}

std::unique_ptr<CHARACTER> SessionCharacterPopulationStorage::RemoveByKey(int key) noexcept
{
    const int index = FindIndexByKey(key);
    if (index < 0)
    {
        return nullptr;
    }

    const std::size_t slot = static_cast<std::size_t>(index);
    if (slotKinds_[slot] == SlotKind::Controlled)
    {
        return nullptr;
    }
    if (Hero == slots_[slot])
    {
        Hero = controlledCharacter_.get();
    }

    std::unique_ptr<CHARACTER> released;
    if (slotKinds_[slot] == SlotKind::Local)
    {
        released = std::move(localCharacters_[slot]);
    }
    else if (slotKinds_[slot] == SlotKind::Shared && sharedCharacters_ != nullptr &&
             sessionId_.has_value() && sharedKeys_[slot].has_value())
    {
        released = sharedCharacters_->Unobserve(*sharedKeys_[slot], *sessionId_);
    }
    ClearSlot(index);
    return released;
}

int SessionCharacterPopulationStorage::FindIndexByKey(int key) const noexcept
{
    const auto found = indicesByKey_.find(key);
    return found == indicesByKey_.end() ? -1 : found->second;
}

int SessionCharacterPopulationStorage::FindIndexByMonsterType(int type) const noexcept
{
    for (std::size_t index = 0; index < slots_.size(); ++index)
    {
        const CHARACTER *character = slots_[index];
        if (character != nullptr && character->Object.Live && character->MonsterIndex == type)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool SessionCharacterPopulationStorage::IsShared(int index) const noexcept
{
    return IsValidIndex(index) && slotKinds_[static_cast<std::size_t>(index)] == SlotKind::Shared;
}

bool SessionCharacterPopulationStorage::IsControlled(int index) const noexcept
{
    return IsValidIndex(index) &&
           slotKinds_[static_cast<std::size_t>(index)] == SlotKind::Controlled;
}

bool SessionCharacterPopulationStorage::IsSource(int index) const noexcept
{
    if (!IsValidIndex(index))
    {
        return false;
    }
    const std::size_t slot = static_cast<std::size_t>(index);
    if (slotKinds_[slot] != SlotKind::Shared)
    {
        return true;
    }
    return sharedCharacters_ != nullptr && sessionId_.has_value() &&
           sharedKeys_[slot].has_value() &&
           sharedCharacters_->IsSource(*sharedKeys_[slot], *sessionId_);
}

bool SessionCharacterPopulationStorage::IsSourceByKey(int key) const noexcept
{
    return IsSource(FindIndexByKey(key));
}

void SessionCharacterPopulationStorage::SetVisible(int index, bool visible) noexcept
{
    if (IsValidIndex(index))
    {
        visibility_[static_cast<std::size_t>(index)] = visible ? 1 : 0;
    }
}

bool SessionCharacterPopulationStorage::IsVisible(int index) const noexcept
{
    return IsValidIndex(index) && visibility_[static_cast<std::size_t>(index)] != 0;
}

SharedCharacterPool::AccessLease SessionCharacterPopulationStorage::AcquireSharedAccess(int index)
{
    if (!IsShared(index) || sharedCharacters_ == nullptr)
        return {};

    const auto &key = sharedKeys_[static_cast<std::size_t>(index)];
    return key.has_value() ? sharedCharacters_->AcquireAccess(*key)
                           : SharedCharacterPool::AccessLease{};
}

std::size_t SessionCharacterPopulationStorage::ObserverCount(int index) const noexcept
{
    if (!IsShared(index) || sharedCharacters_ == nullptr)
    {
        return IsValidIndex(index) ? 1 : 0;
    }
    const auto &key = sharedKeys_[static_cast<std::size_t>(index)];
    return key.has_value() ? sharedCharacters_->ObserverCount(*key) : 0;
}

void SessionCharacterPopulationStorage::SetWorldInstance(std::uint64_t worldInstance) noexcept
{
    worldInstance_ = worldInstance;
}

void SessionCharacterPopulationStorage::ClearWorldInstance() noexcept
{
    worldInstance_.reset();
}

bool SessionCharacterPopulationStorage::HasWorldInstance() const noexcept
{
    return worldInstance_.has_value();
}

int SessionCharacterPopulationStorage::AllocateSlot() noexcept
{
    while (!freeSlots_.empty())
    {
        const int index = freeSlots_.back();
        freeSlots_.pop_back();
        if (index >= 0 && static_cast<std::size_t>(index) < slots_.size() &&
            slots_[static_cast<std::size_t>(index)] == nullptr)
        {
            return index;
        }
    }

    const int index = Size();
    if (!EnsureSlot(index) || freeSlots_.empty())
    {
        return -1;
    }
    const int appended = freeSlots_.back();
    freeSlots_.pop_back();
    return appended;
}

bool SessionCharacterPopulationStorage::EnsureSlot(int index) noexcept
{
    if (index < 0)
    {
        return false;
    }
    const std::size_t required = static_cast<std::size_t>(index) + 1;
    if (required <= slots_.size())
    {
        return true;
    }

    try
    {
        const std::size_t previous = slots_.size();
        slots_.reserve(required);
        slotKinds_.reserve(required);
        localCharacters_.reserve(required);
        sharedKeys_.reserve(required);
        visibility_.reserve(required);
        worldVisuals_.reserve(required);
        freeSlots_.reserve(required);
        slots_.resize(required, nullptr);
        slotKinds_.resize(required, SlotKind::Empty);
        localCharacters_.resize(required);
        sharedKeys_.resize(required);
        visibility_.resize(required, 0);
        worldVisuals_.resize(required);
        for (std::size_t freeSlot = required; freeSlot > previous; --freeSlot)
        {
            freeSlots_.push_back(static_cast<int>(freeSlot - 1));
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void SessionCharacterPopulationStorage::MapKey(int index, int key) noexcept
{
    if (!IsValidIndex(index))
    {
        return;
    }
    const CHARACTER *character = slots_[static_cast<std::size_t>(index)];
    const auto previous =
        character == nullptr ? indicesByKey_.end() : indicesByKey_.find(character->Key);
    if (previous != indicesByKey_.end() && previous->second == index)
    {
        indicesByKey_.erase(previous);
    }
    indicesByKey_[key] = index;
}

void SessionCharacterPopulationStorage::ClearSlot(int index) noexcept
{
    if (!IsValidIndex(index))
    {
        return;
    }
    const std::size_t slot = static_cast<std::size_t>(index);
    // Viewport leave retires this admission even when another observer keeps the target alive.
    for (std::size_t attacker = 0; attacker < slots_.size(); ++attacker)
    {
        if (attacker == slot || !slots_[attacker])
            continue;
        auto &visualTarget = worldVisuals_[attacker].target;
        if (visualTarget.character == slots_[slot])
            visualTarget.character = nullptr;
        auto access = AcquireSharedAccess(static_cast<int>(attacker));
        auto &character = *slots_[attacker];
        auto &binding = character.TargetBinding;
        if (binding.sender == sessionId_ && binding.sourceIndex == index)
        {
            character.TargetCharacter = -1;
            binding.sourceIndex = -1;
        }
    }
    boneManager_.Forget(&slots_[slot]->Object);
    indicesByKey_.erase(slots_[slot]->Key);
    slots_[slot] = nullptr;
    slotKinds_[slot] = SlotKind::Empty;
    localCharacters_[slot].reset();
    sharedKeys_[slot].reset();
    visibility_[slot] = 0;
    worldVisuals_[slot].Reset();
    freeSlots_.push_back(index);

    const std::size_t previousSize = slots_.size();
    while (!slots_.empty() && slots_.back() == nullptr)
    {
        slots_.pop_back();
        slotKinds_.pop_back();
        localCharacters_.pop_back();
        sharedKeys_.pop_back();
        visibility_.pop_back();
        worldVisuals_.pop_back();
    }
    if (slots_.size() != previousSize)
    {
        std::erase_if(freeSlots_, [this](int freeSlot) {
            return freeSlot < 0 || static_cast<std::size_t>(freeSlot) >= slots_.size();
        });
    }
}

SharedCharacterKey SessionCharacterPopulationStorage::SharedKey(int key) const noexcept
{
    return {
        worldInstance_.value_or(0),
        static_cast<std::uint16_t>(key),
    };
}

SharedCharacterPool::~SharedCharacterPool() = default;

std::optional<std::uint32_t> SharedCharacterPool::ResolveWorldRoute(
    std::wstring_view host, std::int32_t port, std::uint32_t serverCode) noexcept
{
    try
    {
        std::wstring normalizedHost(host);
        std::transform(normalizedHost.begin(), normalizedHost.end(), normalizedHost.begin(),
                       [](wchar_t value) { return std::towlower(value); });
        auto key = std::make_tuple(std::move(normalizedHost), port, serverCode);
        const std::lock_guard entriesLock(entriesMutex_);
        const auto found = worldRoutes_.find(key);
        if (found != worldRoutes_.end())
            return found->second;
        if (worldRoutes_.size() > (std::numeric_limits<std::uint32_t>::max)())
            return std::nullopt;
        const auto id = static_cast<std::uint32_t>(worldRoutes_.size());
        worldRoutes_.emplace(std::move(key), id);
        return id;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

SharedCharacterPool::AccessLease::AccessLease(std::shared_ptr<std::mutex> mutex)
    : mutex_(std::move(mutex)), lock_(*mutex_)
{
}

SharedCharacterPool::Observation SharedCharacterPool::Observe(SharedCharacterKey key,
                                                              SessionId observer) noexcept
{
    try
    {
        const std::lock_guard entriesLock(entriesMutex_);
        auto [entry, inserted] = entries_.try_emplace(key);
        if (inserted)
        {
            entry->second.character = std::make_unique<CHARACTER>();
        }

        auto &observers = entry->second.observers;
        if (std::find(observers.begin(), observers.end(), observer) == observers.end())
        {
            observers.push_back(observer);
        }
        return {
            entry->second.character.get(),
            !observers.empty() && observers.front() == observer,
        };
    }
    catch (...)
    {
        const std::lock_guard entriesLock(entriesMutex_);
        const auto entry = entries_.find(key);
        if (entry != entries_.end() && entry->second.observers.empty())
        {
            entries_.erase(entry);
        }
        return {};
    }
}

std::unique_ptr<CHARACTER> SharedCharacterPool::Unobserve(SharedCharacterKey key,
                                                          SessionId observer) noexcept
{
    std::shared_ptr<std::mutex> accessMutex;
    {
        const std::lock_guard entriesLock(entriesMutex_);
        const auto entry = entries_.find(key);
        if (entry == entries_.end())
            return nullptr;
        accessMutex = entry->second.accessMutex;
    }
    const std::lock_guard accessLock(*accessMutex);
    const std::lock_guard entriesLock(entriesMutex_);
    const auto entry = entries_.find(key);
    if (entry == entries_.end() || entry->second.accessMutex != accessMutex)
        return nullptr;

    auto &observers = entry->second.observers;
    const auto found = std::find(observers.begin(), observers.end(), observer);
    if (found == observers.end())
    {
        return nullptr;
    }

    observers.erase(found);
    if (!observers.empty())
    {
        return nullptr;
    }

    std::unique_ptr<CHARACTER> released = std::move(entry->second.character);
    entries_.erase(entry);
    return released;
}

CHARACTER *SharedCharacterPool::Find(SharedCharacterKey key) noexcept
{
    const std::lock_guard entriesLock(entriesMutex_);
    const auto entry = entries_.find(key);
    return entry == entries_.end() ? nullptr : entry->second.character.get();
}

bool SharedCharacterPool::IsSource(SharedCharacterKey key, SessionId observer) const noexcept
{
    const std::lock_guard entriesLock(entriesMutex_);
    const auto entry = entries_.find(key);
    return entry != entries_.end() && !entry->second.observers.empty() &&
           entry->second.observers.front() == observer;
}

std::size_t SharedCharacterPool::ObserverCount(SharedCharacterKey key) const noexcept
{
    const std::lock_guard entriesLock(entriesMutex_);
    const auto entry = entries_.find(key);
    return entry == entries_.end() ? 0 : entry->second.observers.size();
}

std::size_t SharedCharacterPool::Size() const noexcept
{
    const std::lock_guard entriesLock(entriesMutex_);
    return entries_.size();
}

SharedCharacterPool::AccessLease SharedCharacterPool::AcquireAccess(SharedCharacterKey key)
{
    std::shared_ptr<std::mutex> accessMutex;
    {
        const std::lock_guard entriesLock(entriesMutex_);
        const auto entry = entries_.find(key);
        if (entry != entries_.end())
        {
            accessMutex = entry->second.accessMutex;
        }
    }
    return accessMutex == nullptr ? AccessLease{} : AccessLease(std::move(accessMutex));
}

std::size_t SharedCharacterPool::KeyHash::operator()(SharedCharacterKey key) const noexcept
{
    const std::size_t world = std::hash<std::uint64_t>{}(key.worldInstance);
    const std::size_t entity = std::hash<std::uint16_t>{}(key.serverId);
    return world ^ (entity + 0x9e3779b9U + (world << 6U) + (world >> 2U));
}

void AdvanceHoveringPet(OBJECT &pet, float frames, double worldTime, float speedMultiplier,
                        float speedOffset, float rescueHeight)
{
    constexpr float Range = 50.f;
    const float totalFrames = frames;
    while (frames > 0.f)
    {
        if (pet.EffectMotionFrames <= 0.f)
        {
            vec3_t ownerPosition, angle;
            const float fraction = (totalFrames - frames) / totalFrames;
            pet.Owner->MotionTrace.Sample(worldTime, fraction, pet.Owner->Position, ownerPosition);
            const float dx = ownerPosition[0] - pet.Position[0],
                        dy = ownerPosition[1] - pet.Position[1];
            const float distanceSquared = dx * dx + dy * dy;
            VectorCopy(pet.Angle, angle);
            pet.AmbientVerticalNoise = 230.f * pet.Owner->Scale;
            if (distanceSquared >= Range * Range * Range * Range)
            {
                VectorCopy(ownerPosition, pet.Position);
                VectorCopy(pet.Owner->Angle, angle);
                angle[2] = pet.Owner->MotionTrace.SampleYaw(worldTime, fraction, angle[2]);
                VectorCopy(angle, pet.Angle);
                pet.AmbientVerticalNoise = rescueHeight;
            }
            else if (distanceSquared >= Range * Range)
                angle[2] = TurnAngle2(angle[2], CreateAngle2D(pet.Position, ownerPosition), 10.f);
            AngleMatrix(angle, pet.Matrix);
            VectorRotate(pet.Direction, pet.Matrix, pet.EffectMotionVelocity);
            pet.EffectMotionAngleRate[2] = std::remainder(angle[2] - pet.Angle[2], 360.f);
            const float speed = distanceSquared <= Range * Range
                                    ? 0.f
                                    : std::log(distanceSquared) * speedMultiplier + speedOffset;
            Vector(0.f, -speed, 0.f, pet.Direction);
            pet.EffectMotionFrames = 1.f;
        }
        const float step = Core::Time::ReferenceStep(frames, pet.EffectMotionFrames);
        pet.MotionTrace.TurnYaw(step, pet.Angle[2],
                                pet.Angle[2] + pet.EffectMotionAngleRate[2] * step, 0.f);
        VectorAddScaled(pet.Position, pet.EffectMotionVelocity, pet.Position, step);
        pet.Angle[2] += pet.EffectMotionAngleRate[2] * step;
        pet.EffectMotionFrames -= step;
        frames -= step;
        vec3_t ownerPosition;
        pet.Owner->MotionTrace.Sample(worldTime, (totalFrames - frames) / totalFrames,
                                      pet.Owner->Position, ownerPosition);
        pet.Position[2] = ownerPosition[2] + pet.AmbientVerticalNoise +
                          pet.EffectMotionVelocity[2] * (1.f - pet.EffectMotionFrames);
        pet.MotionTrace.Advance(step, pet.Position);
    }
    AngleMatrix(pet.Angle, pet.Matrix);
}

bool RootingItem::FindZen(const OBJECT &owner, ITEM_t (&items)[MAX_ITEMS], float radius)
{
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        auto &item = items[i];
        if (!item.Object.Live || item.Item.Type != ITEM_ZEN)
            continue;
        const float dx = owner.Position[0] - item.Object.Position[0];
        const float dy = owner.Position[1] - item.Object.Position[1];
        if (dx * dx + dy * dy >= radius * radius)
            continue;
        itemIndex = i;
        generation = item.Generation;
        VectorCopy(item.Object.Position, position);
        return true;
    }
    return false;
}

// Construction/Destruction

PetActionCollecterPtr PetActionCollecter::Make(SessionKeeper &keeper)
{
    PetActionCollecterPtr temp(new PetActionCollecter(keeper));
    return temp;
}

PetActionCollecter::PetActionCollecter(SessionKeeper &keeper)
    : PetAction(keeper), Items(keeper.ItemsStorage())
{
    m_isRooting = false;

    m_dwSendDelayTime = 0;
    m_dwRootingTime = 0;
    m_dwRoundCountDelay = 0;
    m_state = eAction_Stand;

    m_fRadWidthStand = 0.0f;
    m_fRadWidthGet = 0.0f;
}

PetActionCollecter::~PetActionCollecter()
{
}

bool PetActionCollecter::Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                               bool bForceRender)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    return false;
}

bool PetActionCollecter::Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                              bool bForceRender, float FPS_ANIMATION_FACTOR)
{
    if (obj == nullptr || Owner == nullptr)
        return FALSE;
    AdvanceCollectorMotion(
        *obj, FPS_ANIMATION_FACTOR, tick, sessionKeeper_.FrameWorldTime(),
        1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps,
        [&](double sampleTick, const vec3_t ownerPosition, const vec3_t ownerAngle) {
            PrepareMove(obj, Owner, targetKey, sampleTick, bForceRender, 1.f, ownerPosition,
                        ownerAngle);
        });
    return TRUE;
}

bool PetActionCollecter::PrepareMove(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                     bool bForceRender, float FPS_ANIMATION_FACTOR,
                                     const vec3_t ownerPosition, const vec3_t ownerAngle)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    FindZen(obj);

    if (eAction_Stand == m_state && m_isRooting)
    {
        m_state = eAction_Move;
    }

    float FlyRange = 10.0f;
    vec3_t targetPos, Range, Direction;
    m_fRadWidthStand = ((2 * Q_PI) / 4000.0f) * fmodf(tick, 4000);
    m_fRadWidthGet = ((2 * Q_PI) / 2000.0f) * fmodf(tick, 2000);

    obj->Position[2] = ownerPosition[2] + 20.0f;
    VectorSubtract(obj->Position, ownerPosition, Range);

    float Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
    if (Distance > SEARCH_LENGTH * 3)
    {
        obj->Position[0] = ownerPosition[0] + (sinf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        obj->Position[1] = ownerPosition[1] + (cosf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);

        VectorCopy(ownerAngle, obj->Angle);

        m_state = eAction_Stand;
        m_isRooting = false;
    }

    switch (m_state)
    {
    case eAction_Stand: {
        targetPos[0] = ownerPosition[0] + (sinf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[1] = ownerPosition[1] + (cosf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[2] = ownerPosition[2];

        VectorSubtract(targetPos, obj->Position, Range);
        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);

        if (80.0f >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos); //test
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 8.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (FlyRange >= Distance) ? 0 : (float)log(Distance) * 2.3f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;
    }
    break;

    case eAction_Move: {
        if (!m_isRooting)
        {
            m_isRooting = false;
            m_state = eAction_Return;
            break;
        }

        targetPos[0] = m_RootItem.position[0] + (sinf(m_fRadWidthGet) * CIRCLE_STAND_RADIAN);
        targetPos[1] = m_RootItem.position[1] + (cosf(m_fRadWidthGet) * CIRCLE_STAND_RADIAN);
        targetPos[2] = m_RootItem.position[2]; // + 70 + (sinf(fRadHeight) * 70.0f);

        VectorSubtract(targetPos, obj->Position, Range);

        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
        if (Distance >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos); //test
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 20.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (20.0f >= Distance) ? 0 : logf(Distance) * 2.5f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;

        if (0 == Speed || CompTimeControl(100000, m_dwRootingTime, tick))
        {
            m_dwSendDelayTime = tick;
            m_dwRootingTime = tick;
            m_state = eAction_Get;
        }
    }
    break;

    case eAction_Get: {
        if (!m_isRooting || SEARCH_LENGTH < Distance ||
            CompTimeControl(3000, m_dwRootingTime, tick))
        {
            m_isRooting = false;
            m_dwRootingTime = tick;
            m_state = eAction_Return;
            break;
        }

        VectorCopy(m_RootItem.position, targetPos);

        float Angle = CreateAngle2D(obj->Position, targetPos);
        obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 10.0f * FPS_ANIMATION_FACTOR);

        if (CompTimeControl(1000, m_dwSendDelayTime, tick) && &Hero->Object == obj->Owner &&
            SendGetItem == -1 && m_RootItem.CanPickup(Items))
        {
            SendGetItem = m_RootItem.itemIndex;
            SocketClient->ToGameServer()->SendPickupItemRequest(m_RootItem.itemIndex);
        }
    }
    break;

    case eAction_Return: {
        targetPos[0] = ownerPosition[0] + (sinf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[1] = ownerPosition[1] + (cosf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[2] = ownerPosition[2]; // + 70 + (sinf(fRadHeight) * 70.0f);

        VectorSubtract(targetPos, obj->Position, Range);

        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
        if (Distance >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos);
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 20.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (FlyRange >= Distance) ? 0 : logf(Distance) * 2.5f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;

        if (0 == Speed || CompTimeControl(3000, m_dwRootingTime, tick))
        {
            m_state = eAction_Stand;
        }
    }
    break;
    }

    return TRUE;
}

bool PetActionCollecter::Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                bool bForceRender, double WorldTime)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    BMD *b = &Models[obj->Type];
    vec3_t Position, vRelativePos, Light;

    VectorCopy(obj->Position, b->BodyOrigin);
    Vector(0.f, 0.f, 0.f, vRelativePos);

    const auto *preparedBones = obj->BoneTransform;

    float fRad1 = ((Q_PI / 3000.0f) * fmodf(tick, 3000));
    float fSize = sinf(fRad1) * 0.2f;
    float fSize2 = 1.0f;

    Vector(1.0f, 0.8f, 0.2f, Light);
    VectorCopy(obj->Position, Position);
    Position[2] += 30.0f;
    CreateParticleFpsChecked(BITMAP_SHINY, Position, obj->Angle, Light, 7);

    switch (m_state)
    {
    case eAction_Move:
        fSize = 0.8f;
        fSize2 = 3.0f;
        break;

    case eAction_Get:
        fSize = 0.8f;
        fSize2 = 3.0f;
        break;

    case eAction_Return:
        CreateEffect(MODEL_NEWYEARSDAY_EVENT_MONEY, Position, obj->Angle, Light);
        break;
    }

    b->TransformPosition(preparedBones[10], vRelativePos, Position, false);
    Vector(1.0f, 0.8f, 0.2f, Light);
    CreateSprite(BITMAP_FLARE_RED, Position, (0.5f + fSize), Light, obj);
    Vector(1.0f, 0.1f, 0.2f, Light);
    CreateSprite(BITMAP_LIGHT, Position, (2.0f + fSize), Light, obj);

    int temp[] = {19, 32, 33, 34, 35};
    for (int i = 0; i < 5; i++)
    {
        b->TransformPosition(preparedBones[temp[i]], vRelativePos, Position, false);
        Vector(0.8f, 0.6f, 0.2f, Light);
        CreateSprite(BITMAP_LIGHT, Position, (0.6f * fSize2), Light, obj);
        Vector(0.8f, 0.8f, 0.2f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, (0.4f * fSize2), Light, obj);
    }
    return TRUE;
}

bool PetActionCollecter::Sound(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                               bool bForceRender)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    switch (m_state)
    {
    case eAction_Return:
        PlayBuffer(SOUND_DROP_GOLD01);
        break;
    }

    return TRUE;
}

void PetActionCollecter::FindZen(OBJECT *obj)
{
    if (!obj || m_isRooting)
        return;
    m_isRooting = m_RootItem.FindZen(*obj->Owner, Items, SEARCH_LENGTH);
}
#ifdef PJH_ADD_PANDA_PET

PetActionCollecterAddPtr PetActionCollecterAdd::Make(SessionKeeper &keeper)
{
    PetActionCollecterAddPtr temp(new PetActionCollecterAdd(keeper));
    return temp;
}

PetActionCollecterAdd::PetActionCollecterAdd(SessionKeeper &keeper)
    : PetAction(keeper), Items(keeper.ItemsStorage())
{
    m_isRooting = false;
    m_dwSendDelayTime = 0;
    m_dwRootingTime = 0;
    m_dwRoundCountDelay = 0;
    m_state = eAction_Stand;
    m_fRadWidthStand = 0.0f;
    m_fRadWidthGet = 0.0f;
}

PetActionCollecterAdd::~PetActionCollecterAdd()
{
}

bool PetActionCollecterAdd::Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                  bool bForceRender)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    return false;
}

bool PetActionCollecterAdd::Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                 bool bForceRender, float FPS_ANIMATION_FACTOR)
{
    if (obj == nullptr || Owner == nullptr)
        return FALSE;
    AdvanceCollectorMotion(
        *obj, FPS_ANIMATION_FACTOR, tick, sessionKeeper_.FrameWorldTime(),
        1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps,
        [&](double sampleTick, const vec3_t ownerPosition, const vec3_t ownerAngle) {
            PrepareMove(obj, Owner, targetKey, sampleTick, bForceRender, 1.f, ownerPosition,
                        ownerAngle);
        });
    return TRUE;
}

bool PetActionCollecterAdd::PrepareMove(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                        bool bForceRender, float FPS_ANIMATION_FACTOR,
                                        const vec3_t ownerPosition, const vec3_t ownerAngle)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    FindZen(obj);

    if (eAction_Stand == m_state && m_isRooting)
    {
        m_state = eAction_Move;
    }

    float FlyRange = 10.0f;
    vec3_t targetPos, Range, Direction;
    m_fRadWidthStand = ((2 * Q_PI) / 4000.0f) * fmod(tick, 4000);
    m_fRadWidthGet = ((2 * Q_PI) / 2000.0f) * fmod(tick, 2000);

    obj->Position[2] = ownerPosition[2] + 20.0f;
    VectorSubtract(obj->Position, ownerPosition, Range);

    float Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
    if (Distance > SEARCH_LENGTH * 3)
    {
        obj->Position[0] = ownerPosition[0] + (sin(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        obj->Position[1] = ownerPosition[1] + (cos(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);

        VectorCopy(ownerAngle, obj->Angle);

        m_state = eAction_Stand;
        m_isRooting = false;
    }

    switch (m_state)
    {
    case eAction_Stand: {
        targetPos[0] = ownerPosition[0] + (sin(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[1] = ownerPosition[1] + (cos(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[2] = ownerPosition[2];

        VectorSubtract(targetPos, obj->Position, Range);
        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);

        if (80.0f >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos);
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 8.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (FlyRange >= Distance) ? 0 : logf(Distance) * 2.3f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;
    }
    break;

    case eAction_Move: {
        if (!m_isRooting)
        {
            m_isRooting = false;
            m_state = eAction_Return;
            break;
        }

        targetPos[0] = m_RootItem.position[0] + (sinf(m_fRadWidthGet) * CIRCLE_STAND_RADIAN);
        targetPos[1] = m_RootItem.position[1] + (cosf(m_fRadWidthGet) * CIRCLE_STAND_RADIAN);
        targetPos[2] = m_RootItem.position[2]; // + 70 + (sinf(fRadHeight) * 70.0f);

        VectorSubtract(targetPos, obj->Position, Range);

        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
        if (Distance >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos); //test
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 20.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (20.0f >= Distance) ? 0 : logf(Distance) * 2.5f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;

        if (0 == Speed || CompTimeControl(100000, m_dwRootingTime, tick))
        {
            m_dwSendDelayTime = tick;
            m_dwRootingTime = tick;
            m_state = eAction_Get;
        }
    }
    break;

    case eAction_Get:

    {
        if (!m_isRooting || SEARCH_LENGTH < Distance ||
            CompTimeControl(3000, m_dwRootingTime, tick))
        {
            m_isRooting = false;
            m_dwRootingTime = tick;
            m_state = eAction_Return;
            break;
        }

        VectorCopy(m_RootItem.position, targetPos);

        float Angle = CreateAngle2D(obj->Position, targetPos);
        obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 10.0f * FPS_ANIMATION_FACTOR);

        if (CompTimeControl(1000, m_dwSendDelayTime, tick) && &Hero->Object == obj->Owner &&
            SendGetItem == -1 && m_RootItem.CanPickup(Items))
        {
            SendGetItem = m_RootItem.itemIndex;
            SocketClient->ToGameServer()->SendPickupItemRequest(m_RootItem.itemIndex);
        }
    }
    break;

    case eAction_Return: {
        targetPos[0] = ownerPosition[0] + (sinf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[1] = ownerPosition[1] + (cosf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[2] = ownerPosition[2]; // + 70 + (sinf(fRadHeight) * 70.0f);

        VectorSubtract(targetPos, obj->Position, Range);

        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
        if (Distance >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos); //test
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 20.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (FlyRange >= Distance) ? 0 : logf(Distance) * 2.5f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;

        if (0 == Speed || CompTimeControl(3000, m_dwRootingTime, tick))
        {
            m_state = eAction_Stand;
        }
    }
    break;
    }

    return TRUE;
}

bool PetActionCollecterAdd::Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                   bool bForceRender, double WorldTime)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

#ifdef PJH_ADD_PANDA_PET

    BMD *b = &Models[obj->Type];
    vec3_t Position, vRelativePos, Light;

    VectorCopy(obj->Position, b->BodyOrigin);
    Vector(0.f, 0.f, 0.f, vRelativePos);

    const auto *preparedBones = obj->BoneTransform;

    float fRad1 = ((Q_PI / 3000.0f) * fmodf(tick, 3000));
    float fSize = sinf(fRad1) * 0.2f;

    Vector(1.f, 1.f, 1.f, Light);
    VectorCopy(obj->Position, Position);

    Vector(0.f, 0.f, 0.f, vRelativePos);
    b->TransformPosition(preparedBones[7], vRelativePos, Position, false);

    //CreateParticle(BITMAP_LIGHT+3, Position, obj->Angle, Light, 7 );
    const float frames = sessionKeeper_.FrameAnimationFactor();
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames / 3.f))
    {
        const float fraction = birth.FrameFraction();
        AnimationPoseSample pose(obj, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
        pose.SampleBonePosition(*b, *obj, 7, vRelativePos, WorldTime, fraction, Position);
        Vector(0.6f, 1.0f, 0.4f, Light);
        CreateParticle(BITMAP_LIGHT + 3, Position, obj->Angle, Light, 1, 1.f);
    }

    Vector(0.f, 0.f, 0.f, vRelativePos);
    b->TransformPosition(preparedBones[4], vRelativePos, Position, false);
    Vector(0.9f, 0.9f, 0.0f, Light);
    CreateSprite(BITMAP_LIGHT, Position, (1.5f + fSize), Light, obj);
    Vector(0.6f, 1.0f, 0.2f, Light);
    CreateSprite(BITMAP_LIGHT, Position, (2.5f + fSize), Light, obj);
#endif //PJH_ADD_PANDA_PET
    return TRUE;
}

bool PetActionCollecterAdd::Sound(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                  bool bForceRender)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    switch (m_state)
    {
    case eAction_Return:
        PlayBuffer(SOUND_DROP_GOLD01);
        break;
    }

    return TRUE;
}

void PetActionCollecterAdd::FindZen(OBJECT *obj)
{
    if (!obj || m_isRooting)
        return;
    m_isRooting = m_RootItem.FindZen(*obj->Owner, Items, SEARCH_LENGTH);
}

#endif //PJH_ADD_PANDA_PET

PetActionCollecterSkeleton::PetActionCollecterSkeleton(SessionKeeper &keeper)
    : PetActionCollecterAdd(keeper)
{
    m_isRooting = false;
    m_dwSendDelayTime = 0;
    m_dwRootingTime = 0;
    m_dwRoundCountDelay = 0;
    m_state = eAction_Stand;
    m_fRadWidthStand = 0.0f;
    m_fRadWidthGet = 0.0f;
    m_bIsMoving = FALSE;
}

PetActionCollecterSkeleton::~PetActionCollecterSkeleton()
{
}

PetActionCollecterSkeletonPtr PetActionCollecterSkeleton::Make(SessionKeeper &keeper)
{
    PetActionCollecterSkeletonPtr temp(new PetActionCollecterSkeleton(keeper));
    return temp;
}

bool PetActionCollecterSkeleton::Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                      bool bForceRender, float FPS_ANIMATION_FACTOR)
{
    if (obj == nullptr || Owner == nullptr)
        return FALSE;
    AdvanceCollectorMotion(
        *obj, FPS_ANIMATION_FACTOR, tick, sessionKeeper_.FrameWorldTime(),
        1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps,
        [&](double sampleTick, const vec3_t ownerPosition, const vec3_t ownerAngle) {
            PrepareMove(obj, Owner, targetKey, sampleTick, bForceRender, 1.f, ownerPosition,
                        ownerAngle);
        });
    return TRUE;
}

bool PetActionCollecterSkeleton::PrepareMove(OBJECT *obj, CHARACTER *Owner, int targetKey,
                                             double tick, bool bForceRender,
                                             float FPS_ANIMATION_FACTOR, const vec3_t ownerPosition,
                                             const vec3_t ownerAngle)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    FindZen(obj);

    if (eAction_Stand == m_state && m_isRooting)
    {
        m_state = eAction_Move;
    }

    float FlyRange = 12.0f;
    vec3_t targetPos, Range, Direction;
    bool _isMove = false;
    float fRadHeight = ((2 * Q_PI) / 15000.0f) * fmodf(tick, 15000);
    m_fRadWidthStand = ((2 * Q_PI) / 4000.0f) * fmodf(tick, 4000);
    m_fRadWidthGet = ((2 * Q_PI) / 2000.0f) * fmodf(tick, 2000);

    obj->Position[2] = ownerPosition[2] + (50.0f * obj->Owner->Scale);

    VectorSubtract(obj->Position, ownerPosition, Range);

    float Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
    if (Distance > SEARCH_LENGTH * 3)
    {
        obj->Position[0] = ownerPosition[0] + (sinf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        obj->Position[1] = ownerPosition[1] + (cosf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);

        VectorCopy(ownerAngle, obj->Angle);

        m_state = eAction_Stand;
        m_isRooting = false;
    }

    switch (m_state)
    {
    case eAction_Stand: {
        targetPos[0] = ownerPosition[0];
        targetPos[1] = ownerPosition[1];
        targetPos[2] = ownerPosition[2];

        VectorSubtract(targetPos, obj->Position, Range);
        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);

        if (80.0f >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos); //test
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 8.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        //	float Speed = ( FlyRange >= Distance ) ?  0 : (float)log(Distance) * 2.3f;
        float Speed = (FlyRange * FlyRange >= Distance) ? 0 : logf(Distance) * 2.3f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;
    }
    break;

    case eAction_Move: {
        if (!m_isRooting)
        {
            m_isRooting = false;
            m_state = eAction_Return;
            break;
        }

        targetPos[0] = m_RootItem.position[0] + (sinf(m_fRadWidthGet) * CIRCLE_STAND_RADIAN);
        targetPos[1] = m_RootItem.position[1] + (cosf(m_fRadWidthGet) * CIRCLE_STAND_RADIAN);
        targetPos[2] = m_RootItem.position[2];

        VectorSubtract(targetPos, obj->Position, Range);

        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
        if (Distance >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos);
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 20.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (20.0f >= Distance) ? 0 : logf(Distance) * 2.5f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;

        if (0 == Speed || CompTimeControl(100000, m_dwRootingTime, tick))
        {
            m_dwSendDelayTime = tick;
            m_dwRootingTime = tick;
            m_state = eAction_Get;
        }
    }
    break;

    case eAction_Get: {
        if (!m_isRooting || SEARCH_LENGTH < Distance ||
            CompTimeControl(3000, m_dwRootingTime, tick))
        {
            m_isRooting = false;
            m_dwRootingTime = tick;
            m_state = eAction_Return;
            break;
        }

        VectorCopy(m_RootItem.position, targetPos);

        float Angle = CreateAngle2D(obj->Position, targetPos);
        obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 10.0f * FPS_ANIMATION_FACTOR);

        if (CompTimeControl(1000, m_dwSendDelayTime, tick) && &Hero->Object == obj->Owner &&
            SendGetItem == -1 && m_RootItem.CanPickup(Items))
        {
            SendGetItem = m_RootItem.itemIndex;
            SocketClient->ToGameServer()->SendPickupItemRequest(m_RootItem.itemIndex);
        }
    }
    break;

    case eAction_Return: {
        targetPos[0] = ownerPosition[0] + (sinf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[1] = ownerPosition[1] + (cosf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[2] = ownerPosition[2]; // + 70 + (sinf(fRadHeight) * 70.0f);

        VectorSubtract(targetPos, obj->Position, Range);

        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
        if (Distance >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos);
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 20.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (FlyRange >= Distance) ? 0 : logf(Distance) * 2.5f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;

        if (0 == Speed || CompTimeControl(3000, m_dwRootingTime, tick))
        {
            m_state = eAction_Stand;
        }
    }
    break;
    }

    return TRUE;
}

bool PetActionCollecterSkeleton::Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                        bool bForceRender, double WorldTime)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    BMD *b = &Models[obj->Type];
    vec3_t vPosition, vLight;

    b->BodyScale = obj->Scale;
    b->Animation(BoneTransform, obj->AnimationFrame, obj->PriorAnimationFrame, obj->PriorAction,
                 obj->Angle, obj->HeadAngle, false, false);

    float fLumi = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f + 0.5f;
    Vector(0.0f * fLumi, 1.0f * fLumi, 0.5f * fLumi, vLight);

    b->TransformByBoneMatrix(vPosition, BoneTransform[12], obj->Position);
    CreateSprite(BITMAP_LIGHTNING + 1, vPosition, 0.05f, vLight, obj);
    b->TransformByBoneMatrix(vPosition, BoneTransform[11], obj->Position);
    CreateSprite(BITMAP_LIGHTNING + 1, vPosition, 0.05f, vLight, obj);

    b->TransformByBoneMatrix(vPosition, BoneTransform[44], obj->Position);
    CreateSprite(BITMAP_LIGHT, vPosition, 0.3f, vLight, obj);
    b->TransformByBoneMatrix(vPosition, BoneTransform[46], obj->Position);
    CreateSprite(BITMAP_LIGHT, vPosition, 0.6f, vLight, obj);
    b->TransformByBoneMatrix(vPosition, BoneTransform[45], obj->Position);
    CreateSprite(BITMAP_LIGHT, vPosition, 0.4f, vLight, obj);
    b->TransformByBoneMatrix(vPosition, BoneTransform[62], obj->Position);
    CreateSprite(BITMAP_LIGHT, vPosition, 0.3f, vLight, obj);

    m_bIsMoving = !((Owner->Object.CurrentAction >= PLAYER_STOP_MALE &&
                     Owner->Object.CurrentAction <= PLAYER_STOP_RIDE_WEAPON) ||
                    Owner->Object.CurrentAction == PLAYER_STOP_RIDE_HORSE ||
                    Owner->Object.CurrentAction == PLAYER_STOP_TWO_HAND_SWORD_TWO ||
                    Owner->Object.CurrentAction == PLAYER_DARKLORD_STAND ||
                    (Owner->Object.CurrentAction >= PLAYER_FENRIR_DAMAGE &&
                     Owner->Object.CurrentAction <= PLAYER_FENRIR_DAMAGE_ONE_LEFT) ||
                    (Owner->Object.CurrentAction >= PLAYER_FENRIR_STAND &&
                     Owner->Object.CurrentAction <= PLAYER_FENRIR_STAND_ONE_LEFT) ||
                    (Owner->Object.CurrentAction >= PLAYER_DEFENSE1 &&
                     Owner->Object.CurrentAction <= PLAYER_CHANGE_UP) ||
                    (Owner->Object.CurrentAction >= PLAYER_RAGE_FENRIR_STAND &&
                     Owner->Object.CurrentAction <= PLAYER_RAGE_FENRIR_STAND_ONE_LEFT) ||
                    (Owner->Object.CurrentAction >= PLAYER_RAGE_FENRIR_DAMAGE &&
                     Owner->Object.CurrentAction <= PLAYER_RAGE_FENRIR_DAMAGE_ONE_LEFT) ||
                    Owner->Object.CurrentAction == PLAYER_RAGE_UNI_STOP_ONE_RIGHT ||
                    Owner->Object.CurrentAction == PLAYER_STOP_RAGEFIGHTER);

    if (m_bIsMoving == TRUE)
    {
        Vector(0.0f, 1.0f, 0.5f, vLight);
        b->TransformByBoneMatrix(vPosition, BoneTransform[13], obj->Position);

        vec3_t vAngle;
        VectorCopy(obj->Angle, vAngle);
        vAngle[0] += 35.0f;

        const float frames = sessionKeeper_.FrameAnimationFactor();
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames))
        {
            const float fraction = birth.FrameFraction();
            vec3_t birthPosition, offset{};
            AnimationPoseSample pose(obj, b->BoneHead, b->BodyHeight, false,
                                     b->PoseAssetIdentity());
            pose.SampleBonePosition(*b, *obj, 13, offset, WorldTime, fraction, birthPosition);
            for (int i = 0; i < 2; ++i)
            {
                if (i == 1 && sessionKeeper_.Random()->FpsCheck(2, 1.f))
                    continue;

                switch (WorldRandom() % 3)
                {
                case 0:
                    CreateParticle(BITMAP_FIRE_HIK1_MONO, birthPosition, vAngle, vLight, 4,
                                   obj->Scale, obj);
                    break;
                case 1:
                    CreateParticle(BITMAP_FIRE_HIK2_MONO, birthPosition, vAngle, vLight, 8,
                                   obj->Scale, obj);
                    break;
                case 2:
                    CreateParticle(BITMAP_FIRE_HIK3_MONO, birthPosition, vAngle, vLight, 5,
                                   obj->Scale, obj);
                    break;
                }
            }
        }

        Vector(1.0f, 1.0f, 1.0f, vLight);
        CreateSprite(BITMAP_HOLE, vPosition, (sinf(WorldTime * 0.005f) + 1.0f) * 0.1f + 0.1f,
                     vLight, obj);
    }

    return TRUE;
}

PetActionDemonPtr PetActionDemon::Make(SessionKeeper &keeper)
{
    PetActionDemonPtr petAction(new PetActionDemon(keeper));
    return petAction;
}

PetActionDemon::PetActionDemon(SessionKeeper &keeper) : PetAction(keeper)
{
}

PetActionDemon::~PetActionDemon()
{
}

bool PetActionDemon::Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                           bool bForceRender)
{
    float fRad = (Q_PI / 10000.0f) * fmodf(tick, 10000);
    float temp = sinf(fRad);

    Vector(temp, temp, temp, obj->Light);

    return TRUE;
}

bool PetActionDemon::Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                          bool bForceRender, float FPS_ANIMATION_FACTOR)
{
    AdvanceHoveringPet(*obj, FPS_ANIMATION_FACTOR, sessionKeeper_.FrameWorldTime(), 1.f, 5.f, 0.f);

    return TRUE;
}

bool PetActionDemon::Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                            bool bForceRender, double WorldTime)
{
    BMD *b = &Models[obj->Type];
    vec3_t Position, vRelativePos;
    vec3_t Light, Light2;
    float fRad = ((Q_PI / 2500.0f) * fmodf(tick, 25000));
    float temp = sinf(fRad) + 0.4f;

    Vector(0.f, 0.f, 0.f, vRelativePos);
    Vector(temp * 0.7f, temp * 0.5f, temp * 0.6f, Light);
    Vector(0.7f, 0.3f, 0.3f, Light2);

    int itemp[] = {8, 34, 51, 61, 56, 66, 52, 58, 44};

    for (int i = 0; i < 9; i++)
    {
        b->TransformPosition(obj->BoneTransform[itemp[i]], vRelativePos, Position, false);

        switch (i)
        {
        case 0:
            CreateSprite(BITMAP_LIGHTMARKS_FOREIGN, Position, 1.0f, Light, obj);
            CreateSprite(BITMAP_FLARE, Position, 0.5f, Light, obj);
            break;

        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
            CreateParticleFpsChecked(BITMAP_CLUD64, Position, obj->Angle, Light, 11, 0.5f);
            break;

        case 6:
        case 7:
            CreateSprite(BITMAP_LIGHTMARKS_FOREIGN, Position, 0.5f, Light, obj);
            break;

        case 8:
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3, Position, obj->Angle, Light, 1, 0.4f);
            CreateSprite(BITMAP_FLARE, Position, 1.5f, Light, obj);
            CreateSprite(BITMAP_FLARE, Position, 0.5f, Light2, obj);
            break;
        }
    }
    return TRUE;
}

PetActionRoundPtr PetActionRound::Make(SessionKeeper &keeper)
{
    PetActionRoundPtr petActionRound(new PetActionRound(keeper));
    return petActionRound;
}

PetActionRound::PetActionRound(SessionKeeper &keeper) : PetAction(keeper)
{
}

PetActionRound::~PetActionRound()
{
}

bool PetActionRound::Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                           bool bForceRender)
{
    return false;
}

bool PetActionRound::Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                          bool bForceRender, float FPS_ANIMATION_FACTOR)
{
    constexpr float SteeringInterval = 1.f / 16.f;
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const double worldTime = sessionKeeper_.FrameWorldTime();
    const float totalFrames = FPS_ANIMATION_FACTOR;
    float remaining = totalFrames;
    while (remaining > 0.f)
    {
        if (obj->EffectMotionFrames <= 0.f)
        {
            const double nextTick =
                tick - remaining * milliseconds + SteeringInterval * milliseconds;
            const float phase =
                static_cast<float>((2.0 * Q_PI / 5000.0) * std::fmod(nextTick, 5000.0));
            vec3_t orbit{std::sin(phase) * 150.f, std::cos(phase) * 150.f, 0.f}, center{};
            const float targetYaw = CreateAngle2D(orbit, center) + 270.f;
            const float nextYaw = TurnAngle2(obj->Angle[2], targetYaw, 20.f * SteeringInterval);
            obj->EffectMotionAngleRate[2] =
                std::remainder(nextYaw - obj->Angle[2], 360.f) / SteeringInterval;
            obj->EffectMotionFrames = SteeringInterval;
        }
        const float step = Core::Time::ReferenceStep(remaining, obj->EffectMotionFrames);
        obj->MotionTrace.TurnYaw(step, obj->Angle[2],
                                 obj->Angle[2] + obj->EffectMotionAngleRate[2] * step, 0.f);
        obj->Angle[2] += obj->EffectMotionAngleRate[2] * step;
        obj->EffectMotionFrames -= step;
        remaining -= step;
        const double sampleTick = tick - remaining * milliseconds;
        const float width =
            static_cast<float>((2.0 * Q_PI / 5000.0) * std::fmod(sampleTick, 5000.0));
        const float height =
            static_cast<float>((2.0 * Q_PI / 1000.0) * std::fmod(sampleTick, 1000.0));
        vec3_t ownerPosition;
        obj->Owner->MotionTrace.Sample(worldTime, (totalFrames - remaining) / totalFrames,
                                       obj->Owner->Position, ownerPosition);
        obj->Position[0] = ownerPosition[0] + std::sin(width) * 150.f;
        obj->Position[1] = ownerPosition[1] + std::cos(width) * 150.f;
        obj->Position[2] = ownerPosition[2] + 100.f + std::sin(height) * 30.f;
        obj->MotionTrace.Advance(step, obj->Position);
    }
    VectorCopy(obj->Owner->HeadAngle, obj->HeadAngle);
    return TRUE;
}

bool PetActionRound::Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                            bool bForceRender, double WorldTime)
{
    return false;
}

PetActionStandPtr PetActionStand::Make(SessionKeeper &keeper)
{
    PetActionStandPtr temp(new PetActionStand(keeper));
    return temp;
}

PetActionStand::PetActionStand(SessionKeeper &keeper) : PetAction(keeper)
{
}

PetActionStand::~PetActionStand()
{
}

bool PetActionStand::Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                           bool bForceRender)
{
    return false;
}

bool PetActionStand::Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                          bool bForceRender, float FPS_ANIMATION_FACTOR)
{
    AdvanceHoveringPet(*obj, FPS_ANIMATION_FACTOR, sessionKeeper_.FrameWorldTime(), 1.8f, 0.f,
                       200.f);

    return TRUE;
}

bool PetActionStand::Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                            bool bForceRender, double WorldTime)
{
    BMD *b = &Models[obj->Type];
    vec3_t Position, vRelativePos;
    vec3_t Light;

    float fRad1 = ((Q_PI / 3000.0f) * fmodf(tick, 3000));
    float fRad2 = ((Q_PI / 3000.0f) * fmodf((tick + 1500), 3000));
    float tempLight;

    VectorCopy(obj->Position, b->BodyOrigin);
    Vector(0.f, 0.f, 0.f, vRelativePos);

    const auto *preparedBones = obj->BoneTransform;

    Vector(0.7f, 0.2f, 0.6f, Light);
    b->TransformPosition(preparedBones[3], vRelativePos, Position, false);
    CreateSprite(BITMAP_LIGHTMARKS_FOREIGN, Position, 1.5f, Light, obj);
    CreateSprite(BITMAP_LIGHTMARKS_FOREIGN, Position, 0.8f, Light, obj);

    Vector(0.3f, 0.3f, 0.6f, Light);
    b->TransformPosition(preparedBones[5], vRelativePos, Position, false);
    Position[2] -= 25.0f;
    CreateEffect(MODEL_FEATHER_FOREIGN, Position, obj->Angle, Light, 4, NULL, -1, 0, 0, 0, 0.3f);

    int temp[] = {45, 42, 48, 54, 51, 57, 25, 26, 27, 38, 32};

    for (int i = 0; i < 11; ++i)
    {
        b->TransformPosition(preparedBones[temp[i]], vRelativePos, Position, false);

        switch (i)
        {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
            Vector(0.5f, 0.5f, 0.8f, Light);
            CreateSprite(BITMAP_LIGHTMARKS_FOREIGN, Position, 0.3f, Light, obj);
            break;

        case 6:
        case 7:
        case 8:
            Vector(0.6f, 0.2f, 0.8f, Light);
            CreateSprite(BITMAP_LIGHT, Position, 0.2f, Light, obj);
            break;

        case 9:
        case 10:
            tempLight = (0 == i % 2) ? sinf(fRad1) * 2.0f : sinf(fRad2) * 2.0f;
            Vector(tempLight, tempLight, tempLight, Light);
            CreateSprite(BITMAP_FLARE, Position, 0.5f, Light, obj);
            break;
        }
    }
    return TRUE;
}

PetActionUnicornPtr PetActionUnicorn::Make(SessionKeeper &keeper)
{
    PetActionUnicornPtr temp(new PetActionUnicorn(keeper));
    return temp;
}

PetActionUnicorn::PetActionUnicorn(SessionKeeper &keeper)
    : PetAction(keeper), Items(keeper.ItemsStorage())
{
    m_isRooting = false;

    m_dwSendDelayTime = 0;
    m_dwRootingTime = 0;
    m_dwRoundCountDelay = 0;
    m_state = eAction_Stand;

    m_fRadWidthStand = 0.0f;
    m_fRadWidthGet = 0.0f;

    m_speed = 0;
}

PetActionUnicorn::~PetActionUnicorn()
{
}

bool PetActionUnicorn::Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                             bool bForceRender)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    return false;
}

bool PetActionUnicorn::Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                            bool bForceRender, float FPS_ANIMATION_FACTOR)
{
    if (obj == nullptr || Owner == nullptr)
        return FALSE;
    AdvanceCollectorMotion(
        *obj, FPS_ANIMATION_FACTOR, tick, sessionKeeper_.FrameWorldTime(),
        1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps,
        [&](double sampleTick, const vec3_t ownerPosition, const vec3_t ownerAngle) {
            PrepareMove(obj, Owner, targetKey, sampleTick, bForceRender, 1.f, ownerPosition,
                        ownerAngle);
        });
    return TRUE;
}

bool PetActionUnicorn::PrepareMove(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                                   bool bForceRender, float FPS_ANIMATION_FACTOR,
                                   const vec3_t ownerPosition, const vec3_t ownerAngle)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    FindZen(obj);

    if (eAction_Stand == m_state && m_isRooting)
    {
        m_state = eAction_Move;
    }

    if (m_speed == 0)
    {
        m_speed = obj->Velocity;
    }

    float FlyRange = 10.0f;
    vec3_t targetPos, Range, Direction;
    bool _isMove = false;

    float fRadHeight = ((2 * Q_PI) / 15000.0f) * fmodf(tick, 15000);
    m_fRadWidthStand = ((2 * Q_PI) / 4000.0f) * fmodf(tick, 4000);
    m_fRadWidthGet = ((2 * Q_PI) / 2000.0f) * fmodf(tick, 2000);

    obj->Position[2] = ownerPosition[2] + (200.0f * obj->Owner->Scale);

    VectorSubtract(obj->Position, ownerPosition, Range);

    float Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
    if (Distance > SEARCH_LENGTH * 3)
    {
        obj->Position[0] = ownerPosition[0] + (sinf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        obj->Position[1] = ownerPosition[1] + (cosf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);

        VectorCopy(ownerAngle, obj->Angle);

        m_state = eAction_Stand;
        m_isRooting = false;
    }

    switch (m_state)
    {
    case eAction_Stand: {

        targetPos[0] = ownerPosition[0];
        targetPos[1] = ownerPosition[1];
        targetPos[2] = ownerPosition[2];

        VectorSubtract(targetPos, obj->Position, Range);
        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);

        if (80.0f >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos); //test
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 8.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        //	float Speed = ( FlyRange >= Distance ) ?  0 : (float)log(Distance) * 2.3f;
        float Speed = (FlyRange * FlyRange >= Distance) ? 0 : (float)log(Distance) * 2.3f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;

        if (Speed == 0)
        {
            obj->Velocity = m_speed * 0.35f;
        }
        else
        {
            obj->Velocity = m_speed * 1.2f;
        }
    }
    break;

    case eAction_Move: {
        if (!m_isRooting)
        {
            m_isRooting = false;
            m_state = eAction_Return;
            break;
        }

        targetPos[0] = m_RootItem.position[0] + (sinf(m_fRadWidthGet) * CIRCLE_STAND_RADIAN);
        targetPos[1] = m_RootItem.position[1] + (cosf(m_fRadWidthGet) * CIRCLE_STAND_RADIAN);
        targetPos[2] = m_RootItem.position[2]; // + 70 + (sinf(fRadHeight) * 70.0f);

        VectorSubtract(targetPos, obj->Position, Range);

        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
        if (Distance >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos); //test
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 20.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (20.0f >= Distance) ? 0 : (float)log(Distance) * 2.5f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;

        if (0 == Speed || CompTimeControl(100000, m_dwRootingTime, tick))
        {
            m_dwSendDelayTime = tick;
            m_dwRootingTime = tick;
            m_state = eAction_Get;
        }

        obj->Velocity = m_speed;
    }
    break;

    case eAction_Get: {
        if (!m_isRooting || SEARCH_LENGTH < Distance ||
            CompTimeControl(3000, m_dwRootingTime, tick))
        {
            m_isRooting = false;
            m_dwRootingTime = tick;
            m_state = eAction_Return;
            break;
        }

        VectorCopy(m_RootItem.position, targetPos);

        float Angle = CreateAngle2D(obj->Position, targetPos);
        obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 10.0f * FPS_ANIMATION_FACTOR);

        if (CompTimeControl(1000, m_dwSendDelayTime, tick) && &Hero->Object == obj->Owner &&
            SendGetItem == -1 && m_RootItem.CanPickup(Items))
        {
            SendGetItem = m_RootItem.itemIndex;
            SocketClient->ToGameServer()->SendPickupItemRequest(m_RootItem.itemIndex);
        }
        obj->Velocity = m_speed;
    }
    break;

    case eAction_Return: {
        targetPos[0] = ownerPosition[0] + (sinf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[1] = ownerPosition[1] + (cosf(m_fRadWidthStand) * CIRCLE_STAND_RADIAN);
        targetPos[2] = ownerPosition[2]; // + 70 + (sinf(fRadHeight) * 70.0f);

        VectorSubtract(targetPos, obj->Position, Range);

        Distance = sqrtf(Range[0] * Range[0] + Range[1] * Range[1]);
        if (Distance >= FlyRange)
        {
            float Angle = CreateAngle2D(obj->Position, targetPos);
            obj->Angle[2] = TurnAngle2(obj->Angle[2], Angle, 20.0f * FPS_ANIMATION_FACTOR);
        }

        AngleMatrix(obj->Angle, obj->Matrix);
        VectorRotate(obj->Direction, obj->Matrix, Direction);
        VectorAddScaled(obj->Position, Direction, obj->Position, FPS_ANIMATION_FACTOR);

        float Speed = (FlyRange >= Distance) ? 0 : (float)log(Distance) * 2.5f;

        obj->Direction[0] = 0.0f;
        obj->Direction[1] = -Speed;
        obj->Direction[2] = 0.0f;

        if (0 == Speed || CompTimeControl(3000, m_dwRootingTime, tick))
        {
            m_state = eAction_Stand;
        }
        obj->Velocity = m_speed;
    }
    break;
    }

    return TRUE;
}

bool PetActionUnicorn::Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                              bool bForceRender, double WorldTime)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    const float frames = sessionKeeper_.FrameAnimationFactor();
    BMD *b = &Models[obj->Type];
    vec3_t Position, vRelativePos, Light;

    VectorCopy(obj->Position, b->BodyOrigin);
    Vector(0.f, 0.f, 0.f, vRelativePos);

    const auto *preparedBones = obj->BoneTransform;

    Vector(0.f, 0.f, 0.f, vRelativePos);
    b->TransformPosition(preparedBones[11], vRelativePos, Position, false);
    Vector(1.0f, 0.7f, 0.0f, Light);
    CreateSprite(BITMAP_MAGIC, Position, 0.15f, Light, obj);

    Vector(1.0f, 0.7f, 0.3f, Light);
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames / 3.f))
    {
        const float fraction = birth.FrameFraction();
        AnimationPoseSample pose(obj, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
        pose.SampleBonePosition(*b, *obj, 11, vRelativePos, WorldTime, fraction, Position);
        CreateEffect(BITMAP_PIN_LIGHT, Position, obj->Angle, Light, 4, obj, -1, 0, 0, 0, 0.45f);
    }

    b->TransformPosition(preparedBones[4], vRelativePos, Position, false);
    Vector(0.5f, 0.5f, 1.0f, Light);

    CreateSprite(BITMAP_SMOKE, Position, 1.2f, Light, obj);
    CreateSprite(BITMAP_LIGHT, Position, 4.0f, Light, obj);

    for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames / 2.f))
    {
        const float fraction = birth.FrameFraction();
        AnimationPoseSample pose(obj, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
        pose.SampleBonePosition(*b, *obj, 4, vRelativePos, WorldTime, fraction, Position);
        CreateParticle(BITMAP_SMOKE, Position, obj->Angle, Light, 67, 1.0f);
    }

    Vector(0.7f, 0.7f, 1.0f, Light);
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames))
    {
        const float fraction = birth.FrameFraction();
        AnimationPoseSample pose(obj, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
        pose.SampleBonePosition(*b, *obj, 4, vRelativePos, WorldTime, fraction, Position);
        CreateParticle(BITMAP_SMOKELINE1, Position, obj->Angle, Light, 4, 0.6f, obj);
        CreateParticle(BITMAP_SMOKELINE2, Position, obj->Angle, Light, 4, 0.6f, obj);
        CreateParticle(BITMAP_SMOKELINE3, Position, obj->Angle, Light, 4, 0.6f, obj);
    }

    return TRUE;
}

bool PetActionUnicorn::Sound(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                             bool bForceRender)
{
    if (NULL == obj || NULL == Owner)
        return FALSE;

    switch (m_state)
    {
    case eAction_Return:
        PlayBuffer(SOUND_DROP_GOLD01);
        break;
    }

    return TRUE;
}

void PetActionUnicorn::FindZen(OBJECT *obj)
{
    if (!obj || m_isRooting)
        return;
    m_isRooting = m_RootItem.FindZen(*obj->Owner, Items, SEARCH_LENGTH);
}

CHARACTER::CHARACTER()
{
    Initialize();
}

CHARACTER::~CHARACTER()
{
    Destroy();
}

void CHARACTER::Initialize()
{
    ResetPresentationIdentity();
    Object.Initialize();

    Blood = false;
    Ride = false;
    SkillSuccess = false;
    NotRotateOnMagicHit = false;
    SafeZone = false;
    Change = false;
    HideShadow = false;
    m_bIsSelected = false;
    Decoy = false;

    Class = CLASS_WIZARD;
    SkinIndex = SKIN_CLASS_WIZARD;
    Skin = 0;
    CtlCode = 0;
    ExtendState = 0;
    EtcPart = 0;
    GuildStatus = 0;
    GuildType = 0;
    GuildRelationShip = 0;
    GuildSkill = 0;
    GuildMasterKillCount = 0;
    BackupCurrentSkill = 0;
    GuildTeam = 0;
    m_byGensInfluence = 0;
    PK = 0;
    AttackFlag = 0;
    AttackTime = 0;
    LastAttackEffectTime = -1;
    TargetAngle = 0;
    Dead = 0;
    Run = 0;
    PathAnimationWorldTime = -1.0;
    PathAnimationFrames = 0.f;
    Skill = 0;
    SwordCount = 0;
    byExtensionSkill = 0;
    m_byDieType = 0;
    StormTime = 0;
    JumpTime = 0;
    TargetX = 0;
    TargetY = 0;
    SkillX = 0;
    SkillY = 0;
    Appear = 0;
    CurrentSkill = 0;
    CastRenderTime = 0;
    m_byFriend = 0;
    MonsterSkill = 0;

    for (int i = 0; i < 32; ++i)
        ID[i] = 0;

    Movement = 0;
    MovementType = 0;
    CollisionTime = 0;
    GuildMarkIndex = 0;
    Key = 0;
    TargetCharacter = 0;

    Level = 0;
    MonsterIndex = MONSTER_BULL_FIGHTER;
    Damage = 0;
    Hit = 0;
    MoveSpeed = 0;
    AttackSpeed = 0;
    MagicSpeed = 0;

    Action = 0;
    ExtendStateTime = 0;
    LongRangeAttack = 0;
    SelectItem = 0;
    Item = 0;
    FreezeType = 0;
    PriorPositionX = 0;
    PriorPositionY = 0;
    PositionX = 0;
    PositionY = 0;
    m_iDeleteTime = 0;
    m_iFenrirSkillTarget = -1;
    LastCritDamageEffect = 0;

    ProtectGuildMarkWorldTime = 0.0f;
    AttackRange = 0.0f;
    Freeze = 0.0f;
    Duplication = 0.0f;
    Rot = 0.0f;
    HealthStatus = 0.0f;
    ShieldStatus = 0.0f;

    IdentityVector3D(TargetPosition);
    IdentityVector3D(Light);

    for (PART_t &part : BodyPart)
        part = PART_t{};
    for (PART_t &part : Weapon)
        part = PART_t{};
    Wing = PART_t{};
    Helper = PART_t{};
    Flag = PART_t{};
    Path.Reset();

    InitPetInfo(PET_TYPE_DARK_HORSE);
    InitPetInfo(PET_TYPE_DARK_SPIRIT);

    for (int i = 0; i < 32; ++i)
        OwnerID[i] = 0;

    m_pPostMoveProcess = NULL;

    m_iTempKey = 0;
    m_CursedTempleCurSkill = AT_SKILL_CURSED_TEMPLE_PRODECTION;
    m_CursedTempleCurSkillPacket = false;
    GensRanking = 0;
    GensContributionPoints = 0;
}

void CHARACTER::Destroy()
{
    if (SocketSource)
        SocketSource->object = nullptr;
    SocketSource.reset();
    delete m_pPostMoveProcess;
    m_pPostMoveProcess = NULL;
}

void CHARACTER::ResetIdentity()
{
    Destroy();
    Initialize();
}

void CHARACTER::InitPetInfo(int iPetType)
{
    m_PetInfo[iPetType] = PET_INFO{};
    m_PetInfo[iPetType].m_dwPetType = PET_TYPE_NONE;
}

PET_INFO *CHARACTER::GetEquipedPetInfo(int iPetType)
{
    return &(m_PetInfo[iPetType]);
}

void CHARACTER::PostMoveProcess_Active(unsigned int uiLimitCount)
{
    if (m_pPostMoveProcess != NULL)
    {
        delete m_pPostMoveProcess;
        m_pPostMoveProcess = NULL;
    }

    m_pPostMoveProcess = new ST_POSTMOVE_PROCESS();
    m_pPostMoveProcess->bProcessingPostMoveEvent = false;
    m_pPostMoveProcess->uiProcessingCount_PostMoveEvent = uiLimitCount;
}

unsigned int CHARACTER::PostMoveProcess_GetCurProcessCount()
{
    return m_pPostMoveProcess->uiProcessingCount_PostMoveEvent;
}

bool CHARACTER::PostMoveProcess_IsProcessing() const
{
    if (m_pPostMoveProcess == NULL)
    {
        return false;
    }

    return (0 <= m_pPostMoveProcess->uiProcessingCount_PostMoveEvent);
}

bool CHARACTER::PostMoveProcess_Process()
{
    if (m_pPostMoveProcess == NULL)
    {
        return false;
    }

    unsigned int uiCurretProcessingCount_ = m_pPostMoveProcess->uiProcessingCount_PostMoveEvent--;

    if (0 >= uiCurretProcessingCount_)
    {
        delete m_pPostMoveProcess;
        m_pPostMoveProcess = NULL;
        return false;
    }

    return true;
}

PetObjectPtr PetObject::Make(SessionKeeper &keeper)
{
    PetObjectPtr ptr(new PetObject(keeper));
    ptr->Init();
    return ptr;
}

PetObject::PetObject(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), FPS_ANIMATION_FACTOR(keeper.FrameAnimationFactor()),
      WorldTime(keeper.FrameWorldTime()), m_moveType(eAction_Stand), m_oldMoveType(eAction_End),
      m_targetKey(-1), m_pOwner(NULL), m_itemType(-1)
{
}

PetObject::~PetObject()
{
    Release();
}

void PetObject::Init()
{
    m_obj = new OBJECT();
}

void PetObject::SetScale(float scale)
{
    if (NULL == m_obj)
        return;

    m_obj->Scale = scale;
}

void PetObject::SetBlendMesh(int blendMesh)
{
    if (NULL == m_obj)
        return;

    m_obj->BlendMesh = blendMesh;
}

bool PetObject::Create(int itemType, int modelType, vec3_t Position, CHARACTER *Owner, int SubType,
                       int LinkBone)
{
    assert(Owner);
    if (m_obj->Live)
        return FALSE;
    // PetProcess admits the model before creating this runtime.

    elapsedMilliseconds_ = 0.0;
    poseSample_ = {};
    m_pose = std::make_unique<vec34_t[]>(Models[modelType].NumBones);
    m_obj->BoneTransform = m_pose.get();

    m_pOwner = Owner;
    m_itemType = itemType;

    m_obj->Type = modelType;
    m_obj->Live = TRUE;
    m_obj->Visible = FALSE;
    m_obj->LightEnable = TRUE;
    m_obj->ContrastEnable = FALSE;
    m_obj->AlphaEnable = FALSE;
    m_obj->EnableBoneMatrix = TRUE;
    m_obj->Owner = &m_pOwner->Object;
    m_obj->SubType = SubType;
    m_obj->HiddenMesh = -1;
    m_obj->BlendMesh = -1;
    m_obj->BlendMeshLight = 1.f;
    m_obj->Scale = 0.7f;
    m_obj->LifeTime = 30;
    m_obj->Alpha = 0.f;
    m_obj->AlphaTarget = 1.f;

    VectorCopy(Position, m_obj->Position);
    VectorCopy(m_obj->Owner->Angle, m_obj->Angle);
    Vector(3.f, 3.f, 3.f, m_obj->Light);

    m_obj->PriorAnimationFrame = 0.f;
    m_obj->AnimationFrame = 0.f;
    m_obj->Velocity = 0.5f;

    switch (m_obj->Type)
    {
    case MODEL_PET_SKELETON:
        m_obj->Position[1] += (60.0f * Owner->Object.Scale);
        break;
    }
    return TRUE;
}

void PetObject::Release()
{
    m_actionMap.clear();

    m_speedMap.clear();

    if (m_obj)
    {
        OBJECT *targets[]{m_obj};
        if (auto *gameplay = sessionKeeper_.Gameplay(); gameplay && !effectsRetired_)
            gameplay->RetireCharacterEffectTargets(targets);
        m_obj->Live = false;
        delete m_obj;
        m_obj = nullptr;
        m_pose.reset();
    }
}

void PetObject::Update(bool bForceRender)
{
    if (!m_obj || !m_obj->Live || !m_obj->Owner)
        return;

    if (SceneFlag == MAIN_SCENE)
    {
        if (!m_obj->Owner->Live || m_obj->Owner->Kind != KIND_PLAYER)
        {
            m_obj->Live = FALSE;
            return;
        }
    }

    m_obj->MotionTrace.Begin(WorldTime, FPS_ANIMATION_FACTOR, m_obj->Position);
    Alpha(m_obj, FPS_ANIMATION_FACTOR);

    elapsedMilliseconds_ +=
        FPS_ANIMATION_FACTOR * 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const auto tick = elapsedMilliseconds_;
    UpdateModel(tick, bForceRender);
    const float initialAnimationSpeed = m_obj->Velocity;
    m_obj->EffectAnimationAdvance.reset();
    UpdateMove(tick, FPS_ANIMATION_FACTOR, bForceRender);
    BMD *model = &Models[m_obj->Type];
    model->CurrentAction = m_obj->CurrentAction;
    const float animationTravel =
        m_obj->EffectAnimationAdvance.value_or(initialAnimationSpeed * FPS_ANIMATION_FACTOR);
    if (!m_obj->EffectAnimationAdvance.has_value())
    {
        const ObjectMotionTrace::AnimationPhase phase{m_obj->AnimationFrame,
                                                      m_obj->PriorAnimationFrame,
                                                      m_obj->CurrentAction, m_obj->PriorAction};
        m_obj->MotionTrace.AdvanceAnimation(FPS_ANIMATION_FACTOR, phase, animationTravel);
    }
    model->PlayAnimation(&m_obj->AnimationFrame, &m_obj->PriorAnimationFrame, &m_obj->PriorAction,
                         animationTravel, m_obj->Position, m_obj->Angle, 1.f);
    UpdateSound(tick, bForceRender);
    PreparePresentation(bForceRender);
    if (m_obj->Visible)
    {
        SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
        CreateEffect(tick, WorldTime, bForceRender);
    }
}

bool PetObject::IsSameOwner(OBJECT *Owner)
{
    assert(Owner);

    return (Owner == m_obj->Owner) ? TRUE : FALSE;
}

bool PetObject::IsSameObject(OBJECT *Owner, int itemType)
{
    assert(Owner);

    return (Owner == m_obj->Owner && itemType == m_itemType) ? TRUE : FALSE;
}

void PetObject::SetActions(ActionType type, Smart_Ptr(PetAction) action, float speed)
{
    if (!action)
        return;

    m_actionMap.insert(std::make_pair(type, action));
    m_speedMap.insert(std::make_pair(type, speed));
}

void PetObject::SetCommand(int targetKey, ActionType cmdType)
{
    m_targetKey = targetKey;
    m_moveType = cmdType;
}

bool PetObject::UpdateMove(double tick, float animationFactor, bool bForceRender)
{
    if (m_oldMoveType != m_moveType)
    {
        m_oldMoveType = m_moveType;
        m_obj->EffectMotionFrames = 0.f;

        auto iter2 = m_speedMap.find(m_moveType);
        if (iter2 == m_speedMap.end())
            return FALSE;

        m_obj->Velocity = (*iter2).second;
    }

    auto iter = m_actionMap.find(m_moveType);
    if (iter == m_actionMap.end())
    {
        m_moveType = eAction_Stand;
        return FALSE;
    }

    const auto &petAction = iter->second;

    {
        petAction->Move(m_obj, m_pOwner, m_targetKey, tick, bForceRender, animationFactor);
    }

    return TRUE;
}

bool PetObject::UpdateModel(double tick, bool bForceRender)
{
    auto iter = m_actionMap.find(m_moveType);
    if (iter == m_actionMap.end())
        return FALSE;

    const auto &petAction = iter->second;

    {
        petAction->Model(m_obj, m_pOwner, m_targetKey, tick, bForceRender);
    }

    return TRUE;
}

bool PetObject::UpdateSound(double tick, bool bForceRender)
{
    auto iter = m_actionMap.find(m_moveType);
    if (iter == m_actionMap.end())
        return FALSE;

    const auto &petAction = iter->second;

    {
        petAction->Sound(m_obj, m_pOwner, m_targetKey, tick, bForceRender);
    }

    return TRUE;
}

bool PetObject::CreateEffect(double tick, double worldTime, bool bForceRender)
{
    auto iter = m_actionMap.find(m_moveType);
    if (iter == m_actionMap.end())
        return FALSE;

    const auto &petAction = iter->second;

    {
        petAction->Effect(m_obj, m_pOwner, m_targetKey, tick, bForceRender, worldTime);
    }

    return TRUE;
}

PetInfoPtr PetInfo::Make()
{
    PetInfoPtr petInfo(new PetInfo);
    return petInfo;
}

PetInfo::PetInfo() : m_scale(0.0f), m_actions(NULL), m_speeds(NULL), m_count(0)
{
}

PetInfo::~PetInfo()
{
    Destroy();
}

void PetInfo::Destroy()
{

    delete[] m_actions;
    m_actions = NULL;
    delete[] m_speeds;
    m_speeds = NULL;
}

void PetInfo::SetActions(int count, int *actions, float *speeds)
{
    constexpr int MAX_PET_ACTIONS = 100;

    if (NULL == actions || NULL == speeds || count <= 0 || count > MAX_PET_ACTIONS)
    {
        return;
    }

    Destroy();

    m_count = count;
    m_actions = new int[count]();
    memcpy(m_actions, actions, sizeof(int) * m_count);

    m_speeds = new float[count];
    memcpy(m_speeds, speeds, sizeof(float) * m_count);
}

PetProcessPtr PetProcess::Make(SessionKeeper &keeper)
{
    PetProcessPtr petprocess(new PetProcess(keeper));
    petprocess->Init();
    return petprocess;
}

PetProcess::PetProcess(SessionKeeper &keeper)
    : sessionKeeper_(keeper), g_ErrorReport(keeper.ErrorReport()),
      g_hWnd(keeper.PlatformWindowHandle())
{
}

PetProcess::~PetProcess()
{
    Destroy();
}

void PetProcess::Init()
{
    LoadData();
}

void PetProcess::Destroy()
{
    m_petsInfo.clear();
}

Smart_Ptr(PetAction) PetProcess::CreateAction(int key)
{
    switch (key)
    {
    case PC4_ELF:
        return PetActionStand::Make(sessionKeeper_);
    case PC4_TEST:
        return PetActionRound::Make(sessionKeeper_);
    case PC4_SATAN:
        return PetActionDemon::Make(sessionKeeper_);
    case XMAS_RUDOLPH:
        return PetActionCollecter::Make(sessionKeeper_);
#ifdef PJH_ADD_PANDA_PET
    case PANDA:
        return PetActionCollecterAdd::Make(sessionKeeper_);
#endif
    case UNICORN:
        return PetActionUnicorn::Make(sessionKeeper_);
    case SKELETON:
        return PetActionCollecterSkeleton::Make(sessionKeeper_);
    default:
        return {};
    }
}

bool PetProcess::LoadData()
{
    wchar_t FileName[100];
    mu_swprintf(FileName, L"Data\\Local\\pet.bmd");

    int _ver;
    int _array;

    std::unique_ptr<FILE, decltype(&fclose)> file(_wfopen(FileName, L"rb"), &fclose);
    FILE *fp = file.get();
    if (fp == NULL)
    {
        wchar_t Text[256];
        mu_swprintf(Text, L"%ls - File not exist.", FileName);
        g_ErrorReport.Write(Text);
        MessageBox(g_hWnd, Text, NULL, MB_OK);
        SendMessage(g_hWnd, WM_DESTROY, 0, 0);

        return FALSE;
    }

    fread(&_ver, sizeof(int), 1, fp);
    fread(&_array, sizeof(int), 1, fp);

    int _type;
    int _blendMesh;
    float _scale;
    int _count;
    int *_action = new int[_array];
    float *_speed = new float[_array];

    int _listSize = 0;
    fread(&_listSize, sizeof(DWORD), 1, fp);

    int Size = sizeof(int) + sizeof(int) + sizeof(float) + sizeof(int) +
               ((sizeof(int) + sizeof(float)) * _array);
    BYTE *Buffer = new BYTE[Size * _listSize];

    fread(Buffer, Size * _listSize, 1, fp);

    DWORD dwCheckSum;
    fread(&dwCheckSum, sizeof(DWORD), 1, fp);

    if (dwCheckSum != GenerateCheckSum2(Buffer, Size * _listSize, 0x7F1D))
    {
        wchar_t Text[256];
        mu_swprintf(Text, L"%ls - File corrupted.", FileName);
        g_ErrorReport.Write(Text);
        MessageBox(g_hWnd, Text, NULL, MB_OK);
        SendMessage(g_hWnd, WM_DESTROY, 0, 0);

        return FALSE;
    }
    else
    {
        BYTE *pSeek = Buffer;
        for (int i = 0; i < _listSize; i++)
        {
            _type = 0;
            _scale = 0.0f;
            _blendMesh = -1;
            _count = 0;
            ZeroMemory(_action, sizeof(_action));
            ZeroMemory(_speed, sizeof(_speed));

            BuxConvert(pSeek, Size);

            memcpy(&_type, pSeek, sizeof(_type));
            pSeek += sizeof(_type);

            memcpy(&_blendMesh, pSeek, sizeof(_blendMesh));
            pSeek += sizeof(_blendMesh);

            memcpy(&_scale, pSeek, sizeof(_scale));
            pSeek += sizeof(_scale);

            memcpy(&_count, pSeek, sizeof(_count));
            pSeek += sizeof(_count);

            memcpy(_action, pSeek, sizeof(int) * _array);
            pSeek += sizeof(int) * _array;

            // sizeof(float), not sizeof(_speed): _speed is a pointer, and on a
            // 64-bit build its size (8) overruns the 4-bytes-per-entry record
            // layout `Size` was computed with, overflowing the allocation.
            memcpy(_speed, pSeek, sizeof(float) * _array);
            pSeek += sizeof(float) * _array;

            constexpr int MAX_PET_ACTIONS = 100;
            if (_type < 0 || _type > 10000 || _count <= 0 || _count > _array ||
                _count > MAX_PET_ACTIONS)
            {
                continue;
            }

            PetInfoPtr petInfo = PetInfo::Make();
            petInfo->SetBlendMesh(_blendMesh);
            petInfo->SetScale(_scale);
            petInfo->SetActions(_count, _action, _speed);

            m_petsInfo.insert(make_pair(ITEM_HELPER + _type, petInfo));
        }
    }
    delete[] _action;
    delete[] _speed;
    delete[] Buffer;

    return TRUE;
}

bool PetProcess::IsPet(int itemType)
{
    auto iter = m_petsInfo.find(itemType);
    if (iter == m_petsInfo.end())
        return FALSE;

    Weak_Ptr(PetInfo) petInfo = (*iter).second;
    if (petInfo.expired())
        return FALSE;

    return TRUE;
}

bool PetProcess::CreatePet(int itemType, int modelType, vec3_t position, CHARACTER *owner,
                           int subType, int linkBone)
{
    if (!owner || !IsPet(itemType))
        return false;
    auto &state = owner->HelperPetState;
    state.itemType = itemType;
    state.modelType = modelType;
    state.subType = subType;
    state.linkBone = linkBone;
    VectorCopy(position, state.position);
    ++state.generation;
    state.commandRevision = 0;
    state.targetKey = -1;
    state.action = PetObject::eAction_Stand;
    return true;
}

PetObjectPtr PetProcess::CreatePetObject(CHARACTER &owner)
{
    const auto &state = owner.HelperPetState;
    const auto found = m_petsInfo.find(state.itemType);
    if (found == m_petsInfo.end())
        return {};
    const auto &model = sessionKeeper_.ModelPoolObject()[state.modelType];
    if (model.NumBones == 0 || model.NumActions == 0)
        return {};
    const auto &info = found->second;
    auto pet = PetObject::Make(sessionKeeper_);
    vec3_t position;
    VectorCopy(state.position, position);
    if (!pet->Create(state.itemType, state.modelType, position, &owner, state.subType,
                     state.linkBone))
        return {};
    pet->SetScale(info->GetScale());
    pet->SetBlendMesh(info->GetBlendMesh());
    ActionMap actions;
    for (int index = 0; index < info->GetActionsCount(); ++index)
    {
        auto &action = actions[info->GetActions()[index]];
        if (!action)
            action = CreateAction(info->GetActions()[index]);
        pet->SetActions(static_cast<PetObject::ActionType>(index), action,
                        info->GetSpeeds()[index]);
    }
    pet->PreparePresentation();
    return pet;
}

void PetProcess::DeletePet(CHARACTER *owner, int itemType, bool allDelete)
{
    if (!owner)
        return;
    auto &state = owner->HelperPetState;
    if (state.itemType == -1 || (!allDelete && itemType != -1 && state.itemType != itemType))
        return;
    state.itemType = state.modelType = -1;
    ++state.generation;
}

void PetProcess::SetCommandPet(CHARACTER *owner, int targetKey, PetObject::ActionType command)
{
    if (!owner || owner->HelperPetState.itemType == -1)
        return;
    auto &state = owner->HelperPetState;
    state.targetKey = targetKey;
    state.action = command;
    ++state.commandRevision;
}

void SessionGameplayUnit::AdvanceMapCharacterTransitions(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;
    // The caller holds the source lease and this tick's animation inputs.
    TheMapProcess().AdvanceMonsterState(character, model);
    if (character.WorldVisualStructureDeath)
        object.Live = false;
    model.BodyHeight = 0.f;
    model.BodyScale = object.Scale;
    model.CurrentAction = character.WorldVisualAction;
    VectorCopy(object.Position, model.BodyOrigin);
}

void CSummonSystem::MoveEquipEffect(CHARACTER *pCharacter, int iItemType, int iItemLevel,
                                    int iItemOption1, double worldTime, bool removeAbsent)
{
    if (iItemType >= MODEL_BOOK_OF_SAHAMUTT && iItemType <= MODEL_STAFF + 29)
    {
        CreateEquipEffect_WristRing(pCharacter, iItemType, iItemLevel, iItemOption1, worldTime);
        CreateEquipEffect_Summon(pCharacter, iItemType, iItemLevel, iItemOption1, worldTime);
    }
    else if (removeAbsent)
    {
        RemoveEquipEffect_WristRing(pCharacter);
        RemoveEquipEffect_Summon(pCharacter);
    }
}

void CSummonSystem::RemoveEquipEffects(CHARACTER *pCharacter)
{
    RemoveEquipEffect_WristRing(pCharacter);
    RemoveEquipEffect_Summon(pCharacter);
}

float RequestTerrainHeight(float xf, float yf);

void CSummonSystem::CreateSummonObject(int iSkill, CHARACTER *pCharacter, OBJECT *pObject,
                                       float fTargetPos_X, float fTargetPos_Y)
{
    PART_t *pWeapon = &pCharacter->Weapon[1];
    int iSummonLevel = pWeapon->Level;
    if (iSummonLevel >= 11)
        iSummonLevel = 2;
    else if (iSummonLevel >= 7)
        iSummonLevel = 1;
    else
        iSummonLevel = 0;

    switch (iSkill)
    {
    case AT_SKILL_SUMMON_EXPLOSION: {
        vec3_t vPos;
        VectorCopy(pObject->Position, vPos);
        if (sessionKeeper_.Random()->FpsCheck(2, 1.f))
            vPos[0] += WorldRandom() % 300 + 150;
        else
            vPos[0] -= WorldRandom() % 250 + 150;
        if (sessionKeeper_.Random()->FpsCheck(2, 1.f))
            vPos[1] += WorldRandom() % 300 + 150;
        else
            vPos[1] -= WorldRandom() % 250 + 150;

        vec3_t vTargetPos;
        Vector(fTargetPos_X, fTargetPos_Y, RequestTerrainHeight(fTargetPos_X, fTargetPos_Y),
               vTargetPos);

        CreateEffect(MODEL_SUMMONER_SUMMON_SAHAMUTT, vPos, pObject->Angle, vTargetPos,
                     iSummonLevel);

        PlayBuffer(SOUND_SUMMON_SAHAMUTT);
    }
    break;
    case AT_SKILL_SUMMON_REQUIEM: {
        float Matrix[3][4];
        vec3_t vMoveDir, vPosition;
        AngleMatrix(pObject->Angle, Matrix);
        Vector(0, -100.0f, 0, vMoveDir);
        VectorRotate(vMoveDir, Matrix, vPosition);
        VectorAdd(pObject->Position, vPosition, vPosition);

        vec3_t vTargetPos;
        Vector(fTargetPos_X, fTargetPos_Y, RequestTerrainHeight(fTargetPos_X, fTargetPos_Y),
               vTargetPos);

        CreateEffect(MODEL_SUMMONER_SUMMON_NEIL, vPosition, pObject->Angle, vTargetPos,
                     iSummonLevel);

        PlayBuffer(SOUND_SUMMON_NEIL);
    }
    break;
    case AT_SKILL_SUMMON_POLLUTION: {
        vec3_t vTargetPos;
        Vector(fTargetPos_X, fTargetPos_Y, RequestTerrainHeight(fTargetPos_X, fTargetPos_Y),
               vTargetPos);
        vec3_t vLight, vAngle;

        Vector(1.0f, 1.0f, 1.0f, vLight);
        Vector(1.0f, 1.0f, 1.0f, vAngle);
        CreateEffect(MODEL_SUMMONER_SUMMON_LAGUL, vTargetPos, vAngle, vLight, 0, NULL,
                     iSummonLevel);

        int anRargle[3] = {1, 2, 4};
        for (int i = 0; i < anRargle[iSummonLevel]; ++i)
        {
            Vector(0.f, 0.f, i * 90.f, vAngle);
            CreateJoint(BITMAP_JOINT_SPIRIT, vTargetPos, vTargetPos, vAngle, 24, NULL, 100.f);
            CreateJoint(BITMAP_JOINT_SPIRIT, vTargetPos, vTargetPos, vAngle, 24, NULL, 20.f);
        }
        PlayBuffer(SOUND_SUMMOM_RARGLE);
    }
    break;
    }
}

void CSummonSystem::CreateCastingEffect(vec3_t vPosition, vec3_t vAngle, int iSubType)
{
    vec3_t vLight;
    Vector(1.0f, 1.0f, 1.0f, vLight);
    CreateEffect(BITMAP_MAGIC, vPosition, vAngle, vLight, 10);
    switch (iSubType)
    {
    case AT_SKILL_SUMMON_EXPLOSION:
        Vector(1.0f, 0.6f, 0.4f, vLight);
        break;
    case AT_SKILL_SUMMON_REQUIEM:
        Vector(0.7f, 0.7f, 1.0f, vLight);
        break;
    case AT_SKILL_SUMMON_POLLUTION:
        Vector(0.6f, 0.6f, 0.9f, vLight);
        break;
    default:
        Vector(0.7f, 0.7f, 1.0f, vLight);
        break;
    }
    CreateEffect(BITMAP_MAGIC, vPosition, vAngle, vLight, 9);
    switch (iSubType)
    {
    case AT_SKILL_SUMMON_EXPLOSION:
        Vector(1.0f, 0.5f, 0.0f, vLight);
        break;
    case AT_SKILL_SUMMON_REQUIEM:
        Vector(0.0f, 0.7f, 1.0f, vLight);
        break;
    case AT_SKILL_SUMMON_POLLUTION:
        Vector(0.6f, 0.3f, 0.9f, vLight);
        break;
    default:
        Vector(0.0f, 0.7f, 1.0f, vLight);
        break;
    }
    CreateEffect(MODEL_SUMMONER_CASTING_EFFECT1, vPosition, vAngle, vLight);
    CreateEffect(MODEL_SUMMONER_CASTING_EFFECT11, vPosition, vAngle, vLight);
    CreateEffect(MODEL_SUMMONER_CASTING_EFFECT111, vPosition, vAngle, vLight);
    switch (iSubType)
    {
    case AT_SKILL_SUMMON_EXPLOSION:
        Vector(1.0f, 0.5f, 0.0f, vLight);
        break;
    case AT_SKILL_SUMMON_REQUIEM:
        Vector(0.0f, 0.0f, 1.0f, vLight);
        break;
    case AT_SKILL_SUMMON_POLLUTION:
        Vector(0.8f, 0.1f, 0.6f, vLight);
        break;
    default:
        Vector(0.0f, 0.0f, 1.0f, vLight);
        break;
    }
    CreateEffect(MODEL_SUMMONER_CASTING_EFFECT2, vPosition, vAngle, vLight);
    CreateEffect(MODEL_SUMMONER_CASTING_EFFECT22, vPosition, vAngle, vLight);
    CreateEffect(MODEL_SUMMONER_CASTING_EFFECT222, vPosition, vAngle, vLight);
    switch (iSubType)
    {
    case AT_SKILL_SUMMON_EXPLOSION:
        Vector(1.0f, 0.5f, 0.8f, vLight);
        break;
    case AT_SKILL_SUMMON_REQUIEM:
        Vector(0.8f, 0.5f, 1.0f, vLight);
        break;
    case AT_SKILL_SUMMON_POLLUTION:
        Vector(0.9f, 0.1f, 1.0f, vLight);
        break;
    default:
        Vector(0.8f, 0.5f, 1.0f, vLight);
        break;
    }
    CreateEffect(MODEL_SUMMONER_CASTING_EFFECT4, vPosition, vAngle, vLight);
}

void CSummonSystem::CreateEquipEffect_WristRing(CHARACTER *pCharacter, int iItemType,
                                                int iItemLevel, int iItemOption1, double worldTime)
{
    OBJECT *pObject = &pCharacter->Object;
    BMD *pModel = &Models[pObject->Type];

    vec3_t vPos, vRelative, vLight;
    Vector(0.0f, 0.0f, 0.0f, vRelative);
    pModel->TransformPosition(pObject->BoneTransform[37], vRelative, vPos, true);

    switch (iItemLevel)
    {
    case 0:
    case 1:
    case 2:
        Vector(1.0f, 1.0f, 0.0f, vLight);
        break;
    case 3:
    case 4:
        Vector(0.5f, 1.0f, 0.5f, vLight);
        break;
    case 5:
    case 6:
        Vector(0.5f, 0.1f, 1.0f, vLight);
        break;
    case 7:
    case 8:
        Vector(1.0f, 0.5f, 0.0f, vLight);
        break;
    case 9:
    case 10:
        Vector(1.0f, 0.2f, 0.2f, vLight);
        break;
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        Vector(0.3f, 0.5f, 1.0f, vLight);
        break;
    }

    if (!SearchJoint(MODEL_SPEARSKILL, pObject, 14))
    {
        for (int i = 0; i < 4; ++i)
        {
            CreateJoint(MODEL_SPEARSKILL, vPos, vPos, pObject->Angle, 14, pObject, 18.0f, -1, 0, 0,
                        pCharacter->TargetCharacter, vLight);
        }

        if (iItemLevel >= 12)
        {
            CreateEffect(MODEL_SUMMONER_WRISTRING_EFFECT, vPos, pObject->Angle, vLight, 0, pObject);
        }
    }

    if (iItemLevel >= 5)
    {
        CreateSprite(BITMAP_FLARE, vPos, 0.7f, vLight, pObject);
    }
    if (iItemLevel >= 7)
    {
        CreateSprite(BITMAP_SHINY + 1, vPos, 0.8f, vLight, pObject);
    }
    if (iItemLevel >= 9)
    {
        CreateSprite(BITMAP_SHINY + 1, vPos, 1.0f, vLight, pObject,
                     static_cast<float>(static_cast<int>(worldTime / 20) % 360));
    }
    if (iItemLevel >= 13)
    {
        if (WorldRandom() % 4)
        {
            CreateParticleFpsChecked(BITMAP_SPARK + 1, vPos, pObject->Angle, vLight, 23, 0.5f,
                                     pObject);
        }
    }
}

void CSummonSystem::RemoveEquipEffect_WristRing(CHARACTER *pCharacter)
{
    OBJECT *pObject = &pCharacter->Object;
    DeleteJoint(MODEL_SPEARSKILL, pObject, 14);
    DeleteEffect(MODEL_SUMMONER_WRISTRING_EFFECT, pObject);
}

void CSummonSystem::CreateEquipEffect_Summon(CHARACTER *pCharacter, int iItemType, int iItemLevel,
                                             int iItemOption1, double worldTime)
{
    OBJECT *pObject = &pCharacter->Object;

    if (gMapManager.ContextMap() == WD_74NEW_CHARACTER_SCENE)
        return;

    if (pObject->CurrentAction == PLAYER_SKILL_SUMMON ||
        pObject->CurrentAction == PLAYER_SKILL_SUMMON_UNI ||
        pObject->CurrentAction == PLAYER_SKILL_SUMMON_DINO ||
        pObject->CurrentAction == PLAYER_SKILL_SUMMON_FENRIR)
    {
        return;
    }

    vec3_t vLight;
    Vector(1.0f, 1.0f, 1.0f, vLight);

    BYTE byRandom;
    auto iter = m_EquipEffectRandom.find(pObject);
    if (iter == m_EquipEffectRandom.end())
    {
        byRandom = WorldRandom() % 256;
        m_EquipEffectRandom.emplace(pObject, byRandom);
    }
    else
    {
        byRandom = iter->second;
    }

    switch (iItemType)
    {
    case MODEL_BOOK_OF_SAHAMUTT:
        if (!SearchEffect(MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT, pObject, 0) &&
            !pCharacter->SafeZone && sinf(worldTime * 0.0004f + byRandom * 0.024f) > 0.3f)
        {
            CreateEffect(MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT, pObject->Position, pObject->Angle,
                         vLight, 0, pObject, -1, byRandom);
        }
        break;
    case MODEL_BOOK_OF_NEIL:
        if (!SearchEffect(MODEL_SUMMONER_EQUIP_HEAD_NEIL, pObject, 0) && !pCharacter->SafeZone &&
            sinf(worldTime * 0.0004f + byRandom * 0.024f) > 0.3f)
        {
            CreateEffect(MODEL_SUMMONER_EQUIP_HEAD_NEIL, pObject->Position, pObject->Angle, vLight,
                         0, pObject, -1, byRandom);
        }
        break;
    case MODEL_BOOK_OF_LAGLE:
        if (!SearchEffect(MODEL_SUMMONER_EQUIP_HEAD_LAGUL, pObject, 0) && !pCharacter->SafeZone &&
            sinf(worldTime * 0.0004f + byRandom * 0.024f) > 0.3f)
        {
            CreateEffect(MODEL_SUMMONER_EQUIP_HEAD_LAGUL, pObject->Position, pObject->Angle, vLight,
                         0, pObject, -1, byRandom);
        }
        break;
    default:
        break;
    }
}

void CSummonSystem::RemoveEquipEffect_Summon(CHARACTER *pCharacter)
{
    OBJECT *pObject = &pCharacter->Object;
    DeleteEffect(MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT, pObject, 0);
    DeleteEffect(MODEL_SUMMONER_EQUIP_HEAD_NEIL, pObject, 0);
    DeleteEffect(MODEL_SUMMONER_EQUIP_HEAD_LAGUL, pObject, 0);
}

void CSummonSystem::CreateDamageOfTimeEffect(int iSkill, OBJECT *pObject)
{
    switch (iSkill)
    {
    case AT_SKILL_SUMMON_EXPLOSION:
        if (!SearchEffect(MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT, pObject, 1))
        {
            CreateEffect(MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT, pObject->Position, pObject->Angle,
                         pObject->Light, 1, pObject, -1);
        }
        break;
    case AT_SKILL_SUMMON_REQUIEM:
        if (!SearchEffect(MODEL_SUMMONER_EQUIP_HEAD_NEIL, pObject, 1))
        {
            CreateEffect(MODEL_SUMMONER_EQUIP_HEAD_NEIL, pObject->Position, pObject->Angle,
                         pObject->Light, 1, pObject, -1);
        }
        break;
    }
}

void CSummonSystem::RemoveDamageOfTimeEffect(int iSkill, OBJECT *pObject)
{
    switch (iSkill)
    {
    case AT_SKILL_SUMMON_EXPLOSION:
        DeleteEffect(MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT, pObject, 1);
        break;
    case AT_SKILL_SUMMON_REQUIEM:
        DeleteEffect(MODEL_SUMMONER_EQUIP_HEAD_NEIL, pObject, 1);
        break;
    }
}

void CSummonSystem::RemoveAllDamageOfTimeEffect(OBJECT *pObject)
{
    RemoveDamageOfTimeEffect(AT_SKILL_SUMMON_EXPLOSION, pObject);
    RemoveDamageOfTimeEffect(AT_SKILL_SUMMON_REQUIEM, pObject);
}
namespace CharacterPresentationDetail
{
// Character selection screen: generous axis-aligned pick box dimensions.

constexpr float CHARACTER_DELETE_TIME_INACTIVE = -128.0f;

void PublishEffectSocket(OBJECT &object, int bone, vec3_t offset, vec3_t output)
{
    VectorTransform(offset, object.BoneTransform[bone], output);
    VectorScale(output, object.Scale, output);
    VectorAdd(output, object.Position, output);
}

void PublishCharacterEffectSockets(CHARACTER &character)
{
    auto &object = character.Object;
    if (object.Type == MODEL_PLAYER)
    {
        constexpr int armorFlareBone = 20;
        vec3_t leftArmorFlareOffset{13.f, 10.f, 3.f};
        vec3_t rightArmorFlareOffset{13.f, 10.f, -3.f};
        PublishEffectSocket(object, armorFlareBone, leftArmorFlareOffset, object.EyeLeft);
        PublishEffectSocket(object, armorFlareBone, rightArmorFlareOffset, object.EyeRight);
        return;
    }
    const auto eyes = [&](int right, int left, int right2 = -1, int left2 = -1, int right3 = -1,
                          int left3 = -1) {
        vec3_t zero{};
        PublishEffectSocket(object, right, zero, object.EyeRight);
        PublishEffectSocket(object, left, zero, object.EyeLeft);
        if (right2 >= 0)
            PublishEffectSocket(object, right2, zero, object.EyeRight2);
        if (left2 >= 0)
            PublishEffectSocket(object, left2, zero, object.EyeLeft2);
        if (right3 >= 0)
            PublishEffectSocket(object, right3, zero, object.EyeRight3);
        if (left3 >= 0)
            PublishEffectSocket(object, left3, zero, object.EyeLeft3);
    };
    switch (object.Type)
    {
    case MODEL_FRED:
        eyes(14, 15, 71, 72, 73, 74);
        return;
    case MODEL_RED_SKELETON_KNIGHT_1:
    case MODEL_MUTANT:
    case MODEL_BEAM_KNIGHT:
    case MODEL_GOLDEN_WHEEL:
        eyes(8, 9);
        return;
    case MODEL_BLOODY_WOLF:
        eyes(11, 12);
        return;
    case MODEL_TANTALLOS:
        eyes(24, 25);
        return;
    case MODEL_TITAN:
        eyes(28, 27);
        return;
    case MODEL_BALGASS:
        eyes(9, 10);
        return;
    case MODEL_ORCUS:
        eyes(9, 9);
        return;
    }
    int rightEyeBone;
    switch (object.Type)
    {
    case MODEL_BULL_FIGHTER:
        if (character.Level != 1)
            return;
        [[fallthrough]];
    case MODEL_DEATH_COW:
        rightEyeBone = 23;
        break;
    case MODEL_CRUST:
        rightEyeBone = 27;
        break;
    case MODEL_LIZARD:
        rightEyeBone = 43;
        break;
    default:
        return;
    }
    // The legacy effect target uses EyeLeft for the right-eye socket.
    vec3_t rightEyeOffset{-5.f, 0.f, 0.f};
    PublishEffectSocket(object, rightEyeBone, rightEyeOffset, object.EyeLeft);
}

void CaptureCharacterPoseInputs(CHARACTER &character)
{
    const auto &object = character.Object;
    character.WorldVisualPriorLifeTime = object.LifeTime;
    character.WorldVisualAction = object.CurrentAction;
    character.WorldVisualAnimationFrame = object.AnimationFrame;
    character.WorldVisualPriorAnimationFrame = object.PriorAnimationFrame;
    character.WorldVisualPriorAction = object.PriorAction;
    character.WorldVisualAttackTime = character.AttackTime;
    character.WorldVisualPriorAI = object.AI;
}

void PublishCharacterPose(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;
    const CharacterDrawInput presentation(character);
    AnimationPoseSample sample(presentation.object, model.BoneHead, 0.f, false,
                               model.PoseAssetIdentity());
    sample.frame = character.WorldVisualAnimationFrame;
    sample.priorFrame = character.WorldVisualPriorAnimationFrame;
    sample.action = static_cast<unsigned short>(character.WorldVisualAction);
    sample.priorAction = static_cast<unsigned short>(character.WorldVisualPriorAction);
    if (character.WorldVisualPoseSample != sample)
    {
        sample.Evaluate(model, object.BoneTransform);
        character.WorldVisualPoseSample = sample;
    }
    object.EnableBoneMatrix = true;
    PublishCharacterEffectSockets(character);
    ++character.WorldVisualPoseRevision;
}

void SetCharacterHeadPart(CHARACTER &character)
{
    const auto helm = character.BodyPart[BODYPART_HELM].Type;
    const bool exposesHead = helm == MODEL_HELM || helm == MODEL_PAD_HELM ||
                             helm == MODEL_HELM + 63 || helm == MODEL_HELM + 68 ||
                             helm == MODEL_HELM + 65 || helm == MODEL_HELM + 70 ||
                             (helm >= MODEL_VINE_HELM && helm <= MODEL_SPIRIT_HELM);
    character.BodyPart[BODYPART_HEAD].Type =
        exposesHead ? static_cast<int>(MODEL_BODY_HELM) + character.SkinIndex : -1;
}

void ResetCharacterSelectionBounds(OBJECT &object) noexcept
{
    VectorAdd(object.BoundingBoxMin, object.Position, object.OBB.StartPos);
    Vector(object.BoundingBoxMax[0] - object.BoundingBoxMin[0], 0.0F, 0.0F, object.OBB.XAxis);
    Vector(0.0F, object.BoundingBoxMax[1] - object.BoundingBoxMin[1], 0.0F, object.OBB.YAxis);
    Vector(0.0F, 0.0F, object.BoundingBoxMax[2] - object.BoundingBoxMin[2], object.OBB.ZAxis);
}

bool AdvanceCharacterDeleteTimer(CHARACTER &character, float animationFactor) noexcept
{
    if (character.m_iDeleteTime > 0)
    {
        character.m_iDeleteTime -= animationFactor;
    }
    return static_cast<int>(character.m_iDeleteTime) !=
               static_cast<int>(CHARACTER_DELETE_TIME_INACTIVE) &&
           character.m_iDeleteTime <= 0;
}

bool IsExpiredXmasCharacter(const OBJECT &object) noexcept
{
    return object.Type == MODEL_PLAYER &&
           (object.SubType == MODEL_XMAS_EVENT_CHA_SSANTA ||
            object.SubType == MODEL_XMAS_EVENT_CHA_SNOWMAN ||
            object.SubType == MODEL_XMAS_EVENT_CHA_DEER) &&
           GetMillisecondsTimestamp() - object.m_dwTime >= XMAS_EVENT_TIME.count();
}
} // namespace CharacterPresentationDetail

int GetFenrirType(CHARACTER *c)
{
    if (c->Helper.ExcellentFlags == 0x01)
        return FENRIR_TYPE_BLACK;
    else if (c->Helper.ExcellentFlags == 0x02)
        return FENRIR_TYPE_BLUE;
    else if (c->Helper.ExcellentFlags == 0x04)
        return FENRIR_TYPE_GOLD;

    return FENRIR_TYPE_RED;
}

void FallingMonster(CHARACTER *, OBJECT *object, float animationFactor)
{
    constexpr float Acceleration = 2.5f, TurnPerTick = 4.f;
    MoveFallingObject(object, Acceleration, TurnPerTick, animationFactor);
}

BOOL SessionGameplayUnit::PlayMonsterSound(OBJECT *object)
{
    return PlayMonsterSoundGlobal(object) || TheMapProcess().PlayMonsterSound(object);
}

void SessionGameplayUnit::SetPlayerStop(CHARACTER *c)
{
    c->Run = 0;

    OBJECT *o = &c->Object;
    if (c->Object.Type == MODEL_PLAYER)
    {
        if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
        {
            if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER) //레이지파이터이면
            {
                if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1)
                    SetAction(&c->Object, PLAYER_RAGE_FENRIR_STAND_TWO_SWORD);
                else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1)
                    SetAction(&c->Object, PLAYER_RAGE_FENRIR_STAND_ONE_RIGHT);
                else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1)
                    SetAction(&c->Object, PLAYER_RAGE_FENRIR_STAND_ONE_LEFT);
                else
                    SetAction(&c->Object, PLAYER_RAGE_FENRIR_STAND);
            }
            else
            {
                if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1) // 양손무기
                    SetAction(&c->Object, PLAYER_FENRIR_STAND_TWO_SWORD);
                else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1) // 오른손 무기
                    SetAction(&c->Object, PLAYER_FENRIR_STAND_ONE_RIGHT);
                else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1) // 왼손 무기
                    SetAction(&c->Object, PLAYER_FENRIR_STAND_ONE_LEFT);
                else // 맨손
                    SetAction(&c->Object, PLAYER_FENRIR_STAND);
            }
        }
        else if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
        {
            if (c->Weapon[0].Type == -1 && c->Weapon[1].Type == -1)
                SetAction(&c->Object, PLAYER_STOP_RIDE_HORSE);
            else
                SetAction(&c->Object, PLAYER_STOP_RIDE_HORSE);
        }
        else if (c->SafeZone && c->m_PetInfo->m_dwPetType == PET_TYPE_DARK_SPIRIT &&
                 !gMapManager.InChaosCastle())
        {
            SetAction(&c->Object, PLAYER_DARKLORD_STAND);
        }
        else if ((c->Helper.Type == MODEL_HORN_OF_UNIRIA ||
                  c->Helper.Type == MODEL_HORN_OF_DINORANT) &&
                 !c->SafeZone)
        {
            if (c->Weapon[0].Type == -1 && c->Weapon[1].Type == -1)
                SetAction(&c->Object, PLAYER_STOP_RIDE);
            else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                SetAction(&c->Object, PLAYER_RAGE_UNI_STOP_ONE_RIGHT);
            else
                SetAction(&c->Object, PLAYER_STOP_RIDE_WEAPON);
        }
        else
        {
            bool Fly = false;

            if (!(c->Object.SubType == MODEL_CURSEDTEMPLE_ALLIED_PLAYER ||
                  c->Object.SubType == MODEL_CURSEDTEMPLE_ILLUSION_PLAYER) &&
                !c->SafeZone && c->Wing.Type != -1)
                Fly = true;

            int Index = TERRAIN_INDEX_REPEAT((int)(c->Object.Position[0] / TERRAIN_SCALE),
                                             (int)(c->Object.Position[1] / TERRAIN_SCALE));

            if (SceneFlag == MAIN_SCENE && (TheMapProcess().CharacterPolicy().swimming) &&
                (TerrainWall[Index] & TW_SAFEZONE) != TW_SAFEZONE)
                Fly = true;

            if (c->MonsterIndex == MONSTER_ELF_SOLDIER)
                Fly = true;

            if (Fly)
            {
                if (g_isCharacterBuff((&c->Object), eBuff_CrywolfHeroContracted))
                {
                    if (c->Object.CurrentAction != PLAYER_HEALING_FEMALE1)
                    {
                        SetAction(&c->Object, PLAYER_HEALING_FEMALE1);
                        SendRequestAction(c->Object, AT_HEALING1);
                    }
                }
                else
                {
                    if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
                    {
                        SetAction(&c->Object, PLAYER_STOP_FLY_CROSSBOW);
                    }
                    else
                    {
                        SetAction(&c->Object, PLAYER_STOP_FLY);
                    }
                }
            }
            else
            {
                if (g_isCharacterBuff((&c->Object), eBuff_CrywolfHeroContracted))
                {
                    if (c->Object.CurrentAction != PLAYER_HEALING_FEMALE1)
                    {
                        SetAction(&c->Object, PLAYER_HEALING_FEMALE1);
                        SendRequestAction(c->Object, AT_HEALING1);
                    }
                }
                else
                {
                    //  No weapons or. Blur when you are in a safe zone that is not a hunting ground.
                    if ((c->Weapon[0].Type == -1 && c->Weapon[1].Type == -1) ||
                        (c->SafeZone && (gMapManager.InBloodCastle() == false)))
                    {
                        if (gCharacterManager.GetBaseClass(c->Class) == CLASS_ELF)
                            SetAction(&c->Object, PLAYER_STOP_FEMALE);
                        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_SUMMONER &&
                                 !gMapManager.InChaosCastle())
                            SetAction(&c->Object, PLAYER_STOP_SUMMONER);
                        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                            SetAction(&c->Object, PLAYER_STOP_RAGEFIGHTER);
                        else
                            SetAction(&c->Object, PLAYER_STOP_MALE);
                    }
                    else
                    {
                        //  칼 장착.
                        if (c->Weapon[0].Type >= MODEL_SWORD &&
                            c->Weapon[0].Type < MODEL_MACE + MAX_ITEM_INDEX)
                        {
                            if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                            {
                                SetAction(&c->Object, PLAYER_STOP_SWORD);
                            }
                            else if (c->Weapon[0].Type == MODEL_DARK_REIGN_BLADE ||
                                     c->Weapon[0].Type == MODEL_RUNE_BLADE ||
                                     c->Weapon[0].Type == MODEL_EXPLOSION_BLADE ||
                                     c->Weapon[0].Type == MODEL_SWORD_DANCER)
                            {
                                SetAction(&c->Object, PLAYER_STOP_TWO_HAND_SWORD_TWO);
                            }
                            else
                            {
                                SetAction(&c->Object, PLAYER_STOP_TWO_HAND_SWORD);
                            }
                        }
                        //  창 장착.
                        else if (c->Weapon[0].Type == MODEL__SPEAR ||
                                 c->Weapon[0].Type == MODEL_DRAGON_LANCE)
                        {
                            SetAction(&c->Object, PLAYER_STOP_SPEAR);
                        }
                        //  창 장착.
                        else if (c->Weapon[0].Type >= MODEL_SPEAR &&
                                 c->Weapon[0].Type < MODEL_SPEAR + MAX_ITEM_INDEX)
                        {
                            if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                                SetAction(&c->Object, PLAYER_STOP_SWORD);
                            else
                                SetAction(&c->Object, PLAYER_STOP_SCYTHE);
                        }
                        // 소환술사 스틱.
                        else if (c->Weapon[0].Type >= MODEL_MISTERY_STICK &&
                                 c->Weapon[0].Type <= MODEL_ETERNAL_WING_STICK)
                        {
                            SetAction(&c->Object, PLAYER_STOP_WAND);
                        }
                        else if (c->Weapon[0].Type >= MODEL_STAFF &&
                                 c->Weapon[0].Type < MODEL_STAFF + MAX_ITEM_INDEX)
                        {
                            if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                                SetAction(&c->Object, PLAYER_STOP_SWORD);
                            else
                                SetAction(&c->Object, PLAYER_STOP_SCYTHE);
                        }
                        else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_BOW)
                        {
                            SetAction(&c->Object, PLAYER_STOP_BOW);
                        }
                        else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
                        {
                            SetAction(&c->Object, PLAYER_STOP_CROSSBOW);
                        }
                        else
                        {
                            if (gCharacterManager.GetBaseClass(c->Class) == CLASS_ELF)
                                SetAction(&c->Object, PLAYER_STOP_FEMALE);
                            else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_SUMMONER)
                                SetAction(&c->Object, PLAYER_STOP_SUMMONER);
                            else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                                SetAction(&c->Object, PLAYER_STOP_RAGEFIGHTER);
                            else
                                SetAction(&c->Object, PLAYER_STOP_MALE);
                        }
                    }
                }
            }
        }
    }
    else
    {
        int Index = TERRAIN_INDEX_REPEAT((c->PositionX), (c->PositionY));
        if (o->Type == MODEL_BALI && (TerrainWall[Index] & TW_SAFEZONE) == TW_SAFEZONE) //발리
            SetAction(&c->Object, MONSTER01_APEAR);
        else
            SetAction(&c->Object, MONSTER01_STOP1);
    }

    if (rand_fps_check(16))
    {
        if (o->Type != MODEL_PLAYER ||
            (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3))
        {
            if (Models[o->Type].Sounds[0] != -1)
            {
                int offset = 0;
                if (o->SubType == 9)
                {
                    offset = 5;
                }

                PlayBuffer(static_cast<ESound>(SOUND_MONSTER + offset +
                                               Models[o->Type].Sounds[WorldRandom() % 2]),
                           o);
            }
        }
        else if (c->Helper.Type == MODEL_HORN_OF_FENRIR)
        {
            if (rand_fps_check(3))
            {
                PlayBuffer(static_cast<ESound>(SOUND_FENRIR_IDLE_1 + WorldRandom() % 2), o);
            }
        }
    }
}

bool SessionGameplayUnit::CanAdvanceCharacterRun(const CHARACTER &c)
{
    if (c.SafeZone ||
        (c.MonsterIndex >= MONSTER_DOPPELGANGER_ELF && c.MonsterIndex <= MONSTER_DOPPELGANGER_SUM))
        return false;
    const int baseClass = gCharacterManager.GetBaseClass(c.Class);
    const bool swimming = TheMapProcess().CharacterPolicy().swimming;
    const auto &body = c.BodyPart[swimming ? BODYPART_GLOVES : BODYPART_BOOTS];
    const auto &equipped =
        CharacterMachine->Equipment[swimming ? EQUIPMENT_GLOVES : EQUIPMENT_BOOTS];
    return baseClass == CLASS_DARK || baseClass == CLASS_DARK_LORD ||
           baseClass == CLASS_RAGEFIGHTER || (body.Type != -1 && body.Level >= 5) ||
           equipped.Level >= 5 || c.Helper.Type == MODEL_HORN_OF_FENRIR ||
           c.Object.SubType == MODEL_CURSEDTEMPLE_ALLIED_PLAYER ||
           c.Object.SubType == MODEL_CURSEDTEMPLE_ILLUSION_PLAYER;
}

void SessionGameplayUnit::SetPlayerWalk(CHARACTER *c, float runFrames)
{
    if (runFrames < 0.f)
        runFrames = c->Run;
    if (c->SafeZone || (c->MonsterIndex >= MONSTER_DOPPELGANGER_ELF &&
                        c->MonsterIndex <= MONSTER_DOPPELGANGER_SUM))
        c->Run = runFrames = 0.f;
    OBJECT *o = &c->Object;
    if (c->Object.Type == MODEL_PLAYER)
    {
        for (int i = PLAYER_WALK_MALE; i <= PLAYER_WALK_CROSSBOW; i++)
        {
            if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.32f;
            else
                Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.33f;

            if (g_isCharacterBuff(o, eDeBuff_Freeze))
            {
                Models[MODEL_PLAYER].Actions[i].PlaySpeed *= 0.5f;
            }
            else if (g_isCharacterBuff(o, eDeBuff_BlowOfDestruction))
            {
                Models[MODEL_PLAYER].Actions[i].PlaySpeed *= 0.33f;
            }
        }

        for (int i = PLAYER_RUN; i <= PLAYER_RUN_RIDE_WEAPON; i++)
        {
            if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.28f;
            else
                Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.34f;

            Models[MODEL_PLAYER].Actions[PLAYER_RAGE_UNI_RUN].PlaySpeed = 0.34f;
            Models[MODEL_PLAYER].Actions[PLAYER_RAGE_UNI_RUN_ONE_RIGHT].PlaySpeed = 0.34f;

            if (g_isCharacterBuff(o, eDeBuff_Freeze))
            {
                Models[MODEL_PLAYER].Actions[i].PlaySpeed *= 0.5f;
            }
            else if (g_isCharacterBuff(o, eDeBuff_BlowOfDestruction))
            {
                Models[MODEL_PLAYER].Actions[i].PlaySpeed *= 0.33f;
            }
        }

        Models[MODEL_PLAYER].Actions[PLAYER_CHANGE_UP].PlaySpeed = 0.049f;
        Models[MODEL_PLAYER].Actions[PLAYER_RUN_RIDE_HORSE].PlaySpeed = 0.33f;
        Models[MODEL_PLAYER].Actions[PLAYER_DARKLORD_WALK].PlaySpeed = 0.33f;
        Models[MODEL_PLAYER].Actions[PLAYER_WALK_WAND].PlaySpeed = 0.44f;
        Models[MODEL_PLAYER].Actions[PLAYER_RUN_WAND].PlaySpeed = 0.76f;

        if (g_isCharacterBuff(o, eDeBuff_Freeze))
        {
            Models[MODEL_PLAYER].Actions[PLAYER_RUN_RIDE_HORSE].PlaySpeed *= 0.5f;
            Models[MODEL_PLAYER].Actions[PLAYER_DARKLORD_WALK].PlaySpeed *= 0.5f;
        }
        else if (g_isCharacterBuff(o, eDeBuff_BlowOfDestruction))
        {
            Models[MODEL_PLAYER].Actions[PLAYER_RUN_RIDE_HORSE].PlaySpeed *= 0.33f;
            Models[MODEL_PLAYER].Actions[PLAYER_DARKLORD_WALK].PlaySpeed *= 0.33f;
        }

        if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
        {
            if (runFrames < FENRIR_RUN_DELAY)
            {
                SetAction_Fenrir_Walk(c, &c->Object);
            }
            else
            {
                SetAction_Fenrir_Run(c, &c->Object);
            }
        }
        else if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
        {
            SetAction(&c->Object, PLAYER_RUN_RIDE_HORSE);
        }
        else if (c->SafeZone && c->m_PetInfo->m_dwPetType == PET_TYPE_DARK_SPIRIT)
        {
            SetAction(&c->Object, PLAYER_DARKLORD_WALK);
        }
        else if (c->Helper.Type == MODEL_HORN_OF_UNIRIA && !c->SafeZone)
        {
            if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
            {
                if (c->Weapon[0].Type == -1 && c->Weapon[1].Type == -1)
                    SetAction(&c->Object, PLAYER_RAGE_UNI_RUN);
                else
                    SetAction(&c->Object, PLAYER_RAGE_UNI_RUN_ONE_RIGHT);
            }
            else
            {
                if (c->Weapon[0].Type == -1 && c->Weapon[1].Type == -1)
                    SetAction(&c->Object, PLAYER_RUN_RIDE);
                else
                    SetAction(&c->Object, PLAYER_RUN_RIDE_WEAPON);
            }
        }
        else if (c->Helper.Type == MODEL_HORN_OF_DINORANT && !c->SafeZone) //  페가시아를 타고있음.
        {
            if (TheMapProcess().MountsUseFlyingActions())
            {
                // 애니메이션 튀는거때문에 아예 막아버림
                //                if(c->Weapon[0].Type==-1 && c->Weapon[1].Type==-1)
                //				    SetAction(&c->Object,PLAYER_FLY_RIDE);
                //			    else
                //				    SetAction(&c->Object,PLAYER_FLY_RIDE_WEAPON);
            }
            else
            {
                if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                {
                    if (c->Weapon[0].Type == -1 && c->Weapon[1].Type == -1)
                        SetAction(&c->Object, PLAYER_RAGE_UNI_RUN);
                    else
                        SetAction(&c->Object, PLAYER_RAGE_UNI_RUN_ONE_RIGHT);
                }
                else
                {
                    if (c->Weapon[0].Type == -1 && c->Weapon[1].Type == -1)
                        SetAction(&c->Object, PLAYER_RUN_RIDE);
                    else
                        SetAction(&c->Object, PLAYER_RUN_RIDE_WEAPON);
                }
            }
        }
        else
        {
            int Index = TERRAIN_INDEX_REPEAT((int)(c->Object.Position[0] / TERRAIN_SCALE),
                                             (int)(c->Object.Position[1] / TERRAIN_SCALE));

            if (!(c->Object.SubType == MODEL_CURSEDTEMPLE_ALLIED_PLAYER ||
                  c->Object.SubType == MODEL_CURSEDTEMPLE_ILLUSION_PLAYER) &&
                !c->SafeZone && c->Wing.Type != -1)
            {
                if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
                    SetAction(&c->Object, PLAYER_FLY_CROSSBOW);
                else
                    SetAction(&c->Object, PLAYER_FLY);
            }
            else if (!c->SafeZone && (TheMapProcess().CharacterPolicy().swimming))
            {
                if (runFrames >= 40)
                    SetAction(&c->Object, PLAYER_RUN_SWIM);
                else
                    SetAction(&c->Object, PLAYER_WALK_SWIM);
            }
            else
            {
                if ((c->Weapon[0].Type == -1 && c->Weapon[1].Type == -1) ||
                    (c->SafeZone && (gMapManager.InBloodCastle() == false)))
                {
                    if (runFrames >= 40)
                        SetAction(&c->Object, PLAYER_RUN);
                    else
                    {
                        if (!gCharacterManager.IsFemale(c->Class))
                            SetAction(&c->Object, PLAYER_WALK_MALE);
                        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_SUMMONER &&
                                 gMapManager.InChaosCastle())
                            SetAction(&c->Object, PLAYER_WALK_MALE);
                        else
                            SetAction(&c->Object, PLAYER_WALK_FEMALE);
                    }
                }
                else
                {
                    if (runFrames < 40)
                    {
                        if (c->Weapon[0].Type >= MODEL_SWORD &&
                            c->Weapon[0].Type < MODEL_MACE + MAX_ITEM_INDEX)
                        {
                            if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                            {
                                SetAction(&c->Object, PLAYER_WALK_SWORD);
                            }
                            else if (c->Weapon[0].Type == MODEL_DARK_REIGN_BLADE ||
                                     c->Weapon[0].Type == MODEL_RUNE_BLADE ||
                                     c->Weapon[0].Type == MODEL_EXPLOSION_BLADE ||
                                     c->Weapon[0].Type == MODEL_SWORD_DANCER)
                            {
                                SetAction(&c->Object, PLAYER_WALK_TWO_HAND_SWORD_TWO);
                            }
                            else
                            {
                                SetAction(&c->Object, PLAYER_WALK_TWO_HAND_SWORD);
                            }
                        }
                        else if (c->Weapon[0].Type >= MODEL_MISTERY_STICK &&
                                 c->Weapon[0].Type <= MODEL_ETERNAL_WING_STICK)
                        {
                            SetAction(&c->Object, PLAYER_WALK_WAND);
                        }
                        else if (c->Weapon[0].Type >= MODEL_STAFF &&
                                 c->Weapon[0].Type < MODEL_STAFF + MAX_ITEM_INDEX)
                        {
                            if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                                SetAction(&c->Object, PLAYER_WALK_SWORD);
                            else
                                SetAction(&c->Object, PLAYER_WALK_SCYTHE);
                        }
                        else if (c->Weapon[0].Type == MODEL__SPEAR ||
                                 c->Weapon[0].Type == MODEL_DRAGON_LANCE + MAX_ITEM_INDEX)
                            SetAction(&c->Object, PLAYER_WALK_SPEAR);
                        else if (c->Weapon[0].Type >= MODEL_SPEAR &&
                                 c->Weapon[0].Type < MODEL_SPEAR + MAX_ITEM_INDEX)
                            SetAction(&c->Object, PLAYER_WALK_SCYTHE);

                        else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_BOW)
                        {
                            SetAction(&c->Object, PLAYER_WALK_BOW);
                        }
                        // 석궁
                        else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
                        {
                            SetAction(&c->Object, PLAYER_WALK_CROSSBOW);
                        }
                        else
                        {
                            if (!gCharacterManager.IsFemale(c->Class))
                                SetAction(&c->Object, PLAYER_WALK_MALE);
                            else
                                SetAction(&c->Object, PLAYER_WALK_FEMALE);
                        }
                    }
                    else
                    {
                        if (c->Weapon[0].Type >= MODEL_SWORD &&
                            c->Weapon[0].Type < MODEL_MACE + MAX_ITEM_INDEX)
                        {
                            if (c->Weapon[1].Type >= MODEL_SWORD &&
                                c->Weapon[1].Type < MODEL_MACE + MAX_ITEM_INDEX)
                            {
                                if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                                {
                                    SetAction(&c->Object, PLAYER_RUN);
                                }
                                else
                                    SetAction(&c->Object, PLAYER_RUN_TWO_SWORD);
                            }
                            else if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                            {
                                SetAction(&c->Object, PLAYER_RUN_SWORD);
                            }
                            else if (c->Weapon[0].Type == MODEL_DARK_REIGN_BLADE ||
                                     c->Weapon[0].Type == MODEL_RUNE_BLADE ||
                                     c->Weapon[0].Type == MODEL_EXPLOSION_BLADE ||
                                     c->Weapon[0].Type == MODEL_SWORD_DANCER)
                            {
                                SetAction(&c->Object, PLAYER_RUN_TWO_HAND_SWORD_TWO);
                            }
                            else
                            {
                                SetAction(&c->Object, PLAYER_RUN_TWO_HAND_SWORD);
                            }
                        }
                        else if (c->Weapon[0].Type >= MODEL_MISTERY_STICK &&
                                 c->Weapon[0].Type <= MODEL_ETERNAL_WING_STICK)
                        {
                            SetAction(&c->Object, PLAYER_RUN_WAND);
                        }
                        else if (c->Weapon[0].Type >= MODEL_STAFF &&
                                 c->Weapon[0].Type < MODEL_STAFF + MAX_ITEM_INDEX)
                        {
                            if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                                SetAction(&c->Object, PLAYER_RUN_SWORD);
                            else
                                SetAction(&c->Object, PLAYER_RUN_SPEAR);
                        }
                        else if (c->Weapon[0].Type >= MODEL_SPEAR &&
                                 c->Weapon[0].Type < MODEL_SPEAR + MAX_ITEM_INDEX)
                        {
                            SetAction(&c->Object, PLAYER_RUN_SPEAR);
                        }
                        else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_BOW)
                        {
                            SetAction(&c->Object, PLAYER_RUN_BOW);
                        }
                        else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
                        {
                            SetAction(&c->Object, PLAYER_RUN_CROSSBOW);
                        }
                        else
                        {
                            SetAction(&c->Object, PLAYER_RUN);
                        }
                    }
                }
            }
        }
    }
    else
    {
        SetAction(&c->Object, MONSTER01_WALK);
    }
    PlayMonsterSound(o);
    if (o->Type == MODEL_BALROG)
        PlayBuffer(SOUND_BONE2, o);
    else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD &&
             c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
    {
        PlayBuffer(static_cast<ESound>(SOUND_RUN_DARK_HORSE_1 + WorldRandom() % 3), o);
    }
    else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone &&
             (c->Object.CurrentAction >= PLAYER_FENRIR_RUN &&
              c->Object.CurrentAction <= PLAYER_FENRIR_RUN_ONE_LEFT_ELF))
    {
        PlayBuffer(static_cast<ESound>(SOUND_FENRIR_RUN_1 + WorldRandom() % 3), o);
    }
    else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone &&
             (c->Object.CurrentAction >= PLAYER_FENRIR_WALK &&
              c->Object.CurrentAction <= PLAYER_FENRIR_WALK_ONE_LEFT))
    {
        PlayBuffer(static_cast<ESound>(SOUND_FENRIR_RUN_1 + WorldRandom() % 2), o);
    }
    else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone &&
             (c->Object.CurrentAction >= PLAYER_RAGE_FENRIR_RUN &&
              c->Object.CurrentAction <= PLAYER_RAGE_FENRIR_RUN_ONE_LEFT))
    {
        PlayBuffer(static_cast<ESound>(SOUND_FENRIR_RUN_1 + WorldRandom() % 3), o);
    }
    else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone &&
             (c->Object.CurrentAction >= PLAYER_RAGE_FENRIR_WALK &&
              c->Object.CurrentAction <= PLAYER_RAGE_FENRIR_WALK_TWO_SWORD))
    {
        PlayBuffer(static_cast<ESound>(SOUND_FENRIR_RUN_1 + WorldRandom() % 2), o);
    }
    else if ((c == Hero && rand_fps_check(64)) || (c != Hero && rand_fps_check(16)))
    {
        if (o->Type != MODEL_PLAYER ||
            (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3))
        {
            if (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3)
                PlayBuffer(SOUND_BONE1, o);
            if (Models[o->Type].Sounds[0] != -1)
            {
                int offset = 0;
                if (o->SubType == 9)
                {
                    offset = 5;
                }
                PlayBuffer(static_cast<ESound>(SOUND_MONSTER + offset +
                                               Models[o->Type].Sounds[WorldRandom() % 2]),
                           o);
            }
        }
    }
}

#define RGZ_FIX_ATTACK_SPEED

void SessionGameplayUnit::SetAttackSpeed()
{
#ifndef RGZ_FIX_ATTACK_SPEED
    float AttackSpeed1 = CharacterAttribute->AttackSpeed * 0.004f;
    float MagicSpeed1 = CharacterAttribute->MagicSpeed * 0.004f;
    float MagicSpeed2 = CharacterAttribute->MagicSpeed * 0.002f;
#else
    float AttackSpeed1 = CharacterAttribute->AttackSpeed;
    float MagicSpeed1 = CharacterAttribute->MagicSpeed;
    float MagicSpeed2 = CharacterAttribute->MagicSpeed;

    if (CharacterAttribute->AttackSpeed >= 509 && CharacterAttribute->AttackSpeed <= 549)
    {
        AttackSpeed1 = AttackSpeed1 * 0.0026000f;
    }
    else if (CharacterAttribute->AttackSpeed >= 550 && CharacterAttribute->AttackSpeed <= 750)
    {
        AttackSpeed1 = AttackSpeed1 * 0.0017000f;
    }
    else
    {
        AttackSpeed1 = AttackSpeed1 * 0.0040000f;
    }

    if (CharacterAttribute->MagicSpeed >= 509 && CharacterAttribute->MagicSpeed <= 549)
    {
        MagicSpeed1 = MagicSpeed1 * 0.0026000f;
    }
    else if (CharacterAttribute->MagicSpeed >= 550 && CharacterAttribute->MagicSpeed <= 750)
    {
        MagicSpeed1 = MagicSpeed1 * 0.0017000f;
    }
    else
    {
        MagicSpeed1 = MagicSpeed1 * 0.0040000f;
    }

    if (CharacterAttribute->MagicSpeed >= 455 && CharacterAttribute->MagicSpeed <= 479)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0024700f;
    }
    else if (CharacterAttribute->MagicSpeed >= 605 && CharacterAttribute->MagicSpeed <= 636)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0019000f;
    }
    else if (CharacterAttribute->MagicSpeed >= 637 && CharacterAttribute->MagicSpeed <= 668)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0018000f;
    }
    else if (CharacterAttribute->MagicSpeed >= 669 && CharacterAttribute->MagicSpeed <= 688)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0017000f;
    }
    else if (CharacterAttribute->MagicSpeed >= 855 && CharacterAttribute->MagicSpeed <= 1040)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0016300f;
    }
    else if (CharacterAttribute->MagicSpeed >= 1041 && CharacterAttribute->MagicSpeed <= 1104)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0015500f;
    }
    else if (CharacterAttribute->MagicSpeed >= 1301 && CharacterAttribute->MagicSpeed <= 1500)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0017500f;
    }
    else if (CharacterAttribute->MagicSpeed >= 1501 && CharacterAttribute->MagicSpeed <= 1524)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0015000f;
    }
    else if (CharacterAttribute->MagicSpeed >= 1525 && CharacterAttribute->MagicSpeed <= 1800)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0014500f;
    }
    else if (CharacterAttribute->MagicSpeed >= 1801 && CharacterAttribute->MagicSpeed <= 1999)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0013000f;
    }
    else if (CharacterAttribute->MagicSpeed >= 2000 && CharacterAttribute->MagicSpeed <= 2167)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0012500f;
    }
    else if (CharacterAttribute->MagicSpeed >= 2168 && CharacterAttribute->MagicSpeed <= 2354)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0011500f;
    }
    else if (CharacterAttribute->MagicSpeed >= 2855 && CharacterAttribute->MagicSpeed <= 3011)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0009000f;
    }
    else if (CharacterAttribute->MagicSpeed >= 3011)
    {
        MagicSpeed2 = MagicSpeed2 * 0.0008100f;
    }
    else
    {
        MagicSpeed2 = MagicSpeed2 * 0.0020000f;
    }
#endif

    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_FIST].PlaySpeed = 0.6f + AttackSpeed1;

    for (int i = PLAYER_ATTACK_SWORD_RIGHT1; i <= PLAYER_ATTACK_RIDE_CROSSBOW; i++)
    {
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.25f + AttackSpeed1;
    }

    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SKILL_SWORD1].PlaySpeed = 0.30f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SKILL_SWORD2].PlaySpeed = 0.30f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SKILL_SWORD3].PlaySpeed = 0.27f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SKILL_SWORD4].PlaySpeed = 0.30f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SKILL_SWORD5].PlaySpeed = 0.24f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SKILL_WHEEL].PlaySpeed = 0.24f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_DEATHSTAB].PlaySpeed = 0.25f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SKILL_SPEAR].PlaySpeed = 0.30f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_RIDER].PlaySpeed = 0.3f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_RIDER_FLY].PlaySpeed = 0.3f + AttackSpeed1;

    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_TWO_HAND_SWORD_TWO].PlaySpeed = 0.25f + AttackSpeed1;

    for (int i = PLAYER_ATTACK_BOW; i <= PLAYER_ATTACK_FLY_CROSSBOW; i++)
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.30f + AttackSpeed1;
    for (int i = PLAYER_ATTACK_RIDE_BOW; i <= PLAYER_ATTACK_RIDE_CROSSBOW; i++)
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.30f + AttackSpeed1;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_ELF1].PlaySpeed = 0.25f + MagicSpeed1;

    for (int i = PLAYER_SKILL_HAND1; i <= PLAYER_SKILL_WEAPON2; i++)
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.29f + MagicSpeed2;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_TELEPORT].PlaySpeed = 0.30f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_FLASH].PlaySpeed = 0.40f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_INFERNO].PlaySpeed = 0.60f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_HELL].PlaySpeed = 0.50f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_RIDE_SKILL].PlaySpeed = 0.30f + MagicSpeed2;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_HELL_BEGIN].PlaySpeed = 0.50f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_STRIKE].PlaySpeed = 0.25f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_STRIKE].PlaySpeed = 0.2f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_HORSE_SWORD].PlaySpeed = 0.25f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_ATTACK_FLASH].PlaySpeed = 0.40f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_ATTACK_MAGIC].PlaySpeed = 0.3f + MagicSpeed2;

    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK].PlaySpeed = 0.25f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE].PlaySpeed =
        0.2f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_DARKLORD_SWORD].PlaySpeed =
        0.25f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_DARKLORD_FLASH].PlaySpeed =
        0.40f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_TWO_SWORD].PlaySpeed = 0.25f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_MAGIC].PlaySpeed = 0.37f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_CROSSBOW].PlaySpeed = 0.30f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_SPEAR].PlaySpeed = 0.25f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_ONE_SWORD].PlaySpeed = 0.25f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_BOW].PlaySpeed = 0.30f + AttackSpeed1;

    for (int i = PLAYER_ATTACK_BOW_UP; i <= PLAYER_ATTACK_RIDE_CROSSBOW_UP; ++i)
    {
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.30f + AttackSpeed1;
    }

    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_ONE_FLASH].PlaySpeed = 0.4f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RUSH].PlaySpeed = 0.3f + AttackSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_DEATH_CANNON].PlaySpeed = 0.2f + AttackSpeed1;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_SLEEP].PlaySpeed = 0.3f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_SLEEP_UNI].PlaySpeed = 0.3f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_SLEEP_DINO].PlaySpeed = 0.3f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_SLEEP_FENRIR].PlaySpeed = 0.3f + MagicSpeed2;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_LIGHTNING_ORB].PlaySpeed = 0.4f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_LIGHTNING_ORB_UNI].PlaySpeed = 0.25f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_LIGHTNING_ORB_DINO].PlaySpeed = 0.25f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_LIGHTNING_ORB_FENRIR].PlaySpeed = 0.25f + MagicSpeed2;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_CHAIN_LIGHTNING].PlaySpeed = 0.25f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_CHAIN_LIGHTNING_UNI].PlaySpeed = 0.15f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_CHAIN_LIGHTNING_DINO].PlaySpeed = 0.15f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_CHAIN_LIGHTNING_FENRIR].PlaySpeed =
        0.15f + MagicSpeed2;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_DRAIN_LIFE].PlaySpeed = 0.25f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_DRAIN_LIFE_UNI].PlaySpeed = 0.25f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_DRAIN_LIFE_DINO].PlaySpeed = 0.25f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_DRAIN_LIFE_FENRIR].PlaySpeed = 0.25f + MagicSpeed2;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_GIGANTICSTORM].PlaySpeed = 0.55f + MagicSpeed1;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_FLAMESTRIKE].PlaySpeed = 0.69f + MagicSpeed2;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_LIGHTNING_SHOCK].PlaySpeed = 0.35f + MagicSpeed2;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_SUMMON].PlaySpeed = 0.25f;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_SUMMON_UNI].PlaySpeed = 0.25f;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_SUMMON_DINO].PlaySpeed = 0.25f;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_SUMMON_FENRIR].PlaySpeed = 0.25f;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_BLOW_OF_DESTRUCTION].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_RECOVER_SKILL].PlaySpeed = 0.33f;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_SWELL_OF_MP].PlaySpeed = 0.2f;

    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SKILL_FURY_STRIKE].PlaySpeed = 0.38f;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_VITALITY].PlaySpeed = 0.34f;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_HELL_START].PlaySpeed = 0.30f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_TELEPORT].PlaySpeed = 0.28f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_TELEPORT].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_DARKHORSE].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_DARKLORD_TELEPORT].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_REMOVAL].PlaySpeed = 0.28f;

    float RageAttackSpeed = CharacterAttribute->AttackSpeed * 0.002f;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_THRUST].PlaySpeed = 0.4f + RageAttackSpeed;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_STAMP].PlaySpeed = 0.4f + RageAttackSpeed;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_GIANTSWING].PlaySpeed = 0.4f + RageAttackSpeed;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_DARKSIDE_READY].PlaySpeed = 0.3f + RageAttackSpeed;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_DARKSIDE_ATTACK].PlaySpeed = 0.3f + RageAttackSpeed;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_DRAGONKICK].PlaySpeed = 0.4f + RageAttackSpeed;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_DRAGONLORE].PlaySpeed = 0.3f + RageAttackSpeed;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_ATT_UP_OURFORCES].PlaySpeed = 0.35f;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_HP_UP_OURFORCES].PlaySpeed = 0.35f;
    Models[MODEL_PLAYER].Actions[PLAYER_RAGE_FENRIR_ATTACK_RIGHT].PlaySpeed =
        0.25f + RageAttackSpeed;
}

void SessionGameplayUnit::SetPlayerHighBowAttack(CHARACTER *c)
{
    OBJECT *o = &c->Object;
    if (o->Type == MODEL_PLAYER)
    {
        SetAttackSpeed();
        if ((c->Helper.Type == MODEL_HORN_OF_UNIRIA || c->Helper.Type == MODEL_HORN_OF_DINORANT) &&
            !c->SafeZone)
        {
            if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_BOW)
            {
                SetAction(&c->Object, PLAYER_ATTACK_RIDE_BOW_UP);
            }
            // 석궁
            else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
            {
                SetAction(&c->Object, PLAYER_ATTACK_RIDE_CROSSBOW_UP);
            }
        }
        else
        {
            if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_BOW)
            {
                if (c->Wing.Type != -1)
                    SetAction(&c->Object, PLAYER_ATTACK_FLY_BOW_UP);
                else
                    SetAction(&c->Object, PLAYER_ATTACK_BOW_UP);
            }
            else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
            {
                if (c->Wing.Type != -1)
                    SetAction(&c->Object, PLAYER_ATTACK_FLY_CROSSBOW_UP);
                else
                    SetAction(&c->Object, PLAYER_ATTACK_CROSSBOW_UP);
            }
        }
    }
    c->SwordCount++;
}

void SessionGameplayUnit::SetPlayerAttack(CHARACTER *c)
{
    OBJECT *o = &c->Object;
    if (o->Type == MODEL_PLAYER)
    {
        SetAttackSpeed();

        if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
        {
            if (c->Weapon[0].Type >= MODEL_SPEAR && c->Weapon[0].Type < MODEL_DOUBLE_POLEAXE)
                SetAction(&c->Object, PLAYER_FENRIR_ATTACK_SPEAR);
            else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_BOW)
            {
                SetAction(&c->Object, PLAYER_FENRIR_ATTACK_BOW);
            }
            else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
            {
                SetAction(&c->Object, PLAYER_FENRIR_ATTACK_CROSSBOW); //석궁공격
            }
            else
            {
                if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1)
                    SetAction(&c->Object, PLAYER_FENRIR_ATTACK_TWO_SWORD);
                else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1)
                    SetAction(&c->Object, PLAYER_FENRIR_ATTACK_ONE_SWORD);
                else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1 &&
                         (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER))
                    SetAction(&c->Object, PLAYER_RAGE_FENRIR_ATTACK_RIGHT);
                else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1)
                    SetAction(&c->Object, PLAYER_FENRIR_ATTACK_ONE_SWORD);
                else
                    SetAction(&c->Object, PLAYER_FENRIR_ATTACK);
            }

            if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD)
            {
                SetAction(&c->Object, PLAYER_FENRIR_ATTACK_DARKLORD_SWORD);
            }
        }
        else if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
        {
            SetAction(&c->Object, PLAYER_ATTACK_RIDE_HORSE_SWORD);
        }
        else if ((c->Helper.Type == MODEL_HORN_OF_UNIRIA ||
                  c->Helper.Type == MODEL_HORN_OF_DINORANT) &&
                 !c->SafeZone)
        {
            if (c->Weapon[0].Type >= MODEL_SPEAR && c->Weapon[0].Type < MODEL_DOUBLE_POLEAXE)
                SetAction(&c->Object, PLAYER_ATTACK_RIDE_SPEAR);
            else if (c->Weapon[0].Type >= MODEL_DOUBLE_POLEAXE &&
                     c->Weapon[0].Type < MODEL_SPEAR + MAX_ITEM_INDEX)
                SetAction(&c->Object, PLAYER_ATTACK_RIDE_SCYTHE);
            else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_BOW)
            {
                SetAction(&c->Object, PLAYER_ATTACK_RIDE_BOW);
            }
            else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
            {
                SetAction(&c->Object, PLAYER_ATTACK_RIDE_CROSSBOW);
            }
            else
            {
                if (c->Weapon[0].Type == -1)
                {
                    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                        SetAction(&c->Object, PLAYER_RAGE_UNI_ATTACK);
                    else
                        SetAction(&c->Object, PLAYER_ATTACK_RIDE_SWORD);
                }
                else
                {
                    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                    {
                        if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                            SetAction(&c->Object, PLAYER_RAGE_UNI_ATTACK);
                        else
                            SetAction(&c->Object, PLAYER_RAGE_UNI_ATTACK_ONE_RIGHT);
                    }
                    else
                    {
                        if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                            SetAction(&c->Object, PLAYER_ATTACK_RIDE_SWORD);
                        else
                            SetAction(&c->Object, PLAYER_ATTACK_RIDE_TWO_HAND_SWORD);
                    }
                }
            }
        }
        else
        {
            if (c->Weapon[0].Type == -1 && c->Weapon[1].Type == -1)
            {
                SetAction(&c->Object, PLAYER_ATTACK_FIST);
            }
            else
            {
                if (c->Weapon[0].Type >= MODEL_SWORD &&
                    c->Weapon[0].Type < MODEL_MACE + MAX_ITEM_INDEX)
                {
                    if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                    {
                        if (c->Weapon[1].Type >= MODEL_SWORD &&
                            c->Weapon[1].Type < MODEL_MACE + MAX_ITEM_INDEX)
                        {
                            switch (c->SwordCount % 4)
                            {
                            case 0:
                                SetAction(&c->Object, PLAYER_ATTACK_SWORD_RIGHT1);
                                break;
                            case 1:
                                SetAction(&c->Object, PLAYER_ATTACK_SWORD_LEFT1);
                                break;
                            case 2:
                                SetAction(&c->Object, PLAYER_ATTACK_SWORD_RIGHT1 + 1);
                                break;
                            case 3:
                                SetAction(&c->Object, PLAYER_ATTACK_SWORD_LEFT1 + 1);
                                break;
                            }
                        }
                        else
                            SetAction(&c->Object, PLAYER_ATTACK_SWORD_RIGHT1 + c->SwordCount % 2);
                    }
                    else if (c->Weapon[0].Type == MODEL_DARK_REIGN_BLADE ||
                             c->Weapon[0].Type == MODEL_RUNE_BLADE ||
                             c->Weapon[0].Type == MODEL_EXPLOSION_BLADE ||
                             c->Weapon[0].Type == MODEL_SWORD_DANCER)
                    {
                        SetAction(&c->Object, PLAYER_ATTACK_TWO_HAND_SWORD_TWO);
                    }
                    else
                    {
                        SetAction(&c->Object, PLAYER_ATTACK_TWO_HAND_SWORD1 + c->SwordCount % 3);
                    }
                }
                else if (c->Weapon[1].Type >= MODEL_SWORD &&
                         c->Weapon[1].Type < MODEL_MACE + MAX_ITEM_INDEX)
                {
                    SetAction(&c->Object, PLAYER_ATTACK_SWORD_LEFT1 + WorldRandom() % 2);
                }
                else if (c->Weapon[0].Type >= MODEL_STAFF &&
                         c->Weapon[0].Type < MODEL_STAFF + MAX_ITEM_INDEX)
                {
                    if (!ItemAttribute[c->Weapon[0].Type - MODEL_ITEM].TwoHand)
                        SetAction(&c->Object, PLAYER_ATTACK_SWORD_RIGHT1 + WorldRandom() % 2);
                    else
                        SetAction(&c->Object, PLAYER_SKILL_WEAPON1 + WorldRandom() % 2);
                }
                else if (c->Weapon[0].Type == MODEL__SPEAR ||
                         c->Weapon[0].Type == MODEL_DRAGON_LANCE)
                    SetAction(&c->Object, PLAYER_ATTACK_SPEAR1);
                else if (c->Weapon[0].Type >= MODEL_SPEAR &&
                         c->Weapon[0].Type < MODEL_SPEAR + MAX_ITEM_INDEX)
                    SetAction(&c->Object, PLAYER_ATTACK_SCYTHE1 + c->SwordCount % 3);
                else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_BOW)
                {
                    if (c->Wing.Type != -1)
                        SetAction(&c->Object, PLAYER_ATTACK_FLY_BOW);
                    else
                        SetAction(&c->Object, PLAYER_ATTACK_BOW);
                }
                else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
                {
                    if (c->Wing.Type != -1)
                        SetAction(&c->Object, PLAYER_ATTACK_FLY_CROSSBOW);
                    else
                        SetAction(&c->Object, PLAYER_ATTACK_CROSSBOW);
                }
                else
                    SetAction(&c->Object, PLAYER_ATTACK_FIST);
            }
        }
    }

    else if (o->Type == 39)
    {
        CreateEffect(MODEL_SAW, o->Position, o->Angle, o->Light);
        PlayBuffer(SOUND_TRAP01, o);
    }
    else if (o->Type == 40)
    {
        SetAction(&c->Object, 1);
        PlayBuffer(SOUND_TRAP01, o);
    }
    else if (o->Type == 51)
    {
        CreateEffect(BITMAP_FIRE + 1, o->Position, o->Angle, o->Light);
        PlayBuffer(SOUND_FLAME);
    }
    else
    {
        if (o->Type == MODEL_BALI)
        {
            int Action = WorldRandom() % 8;
            if (Action > 2)
                SetAction(&c->Object, MONSTER01_ATTACK1 + WorldRandom() % 2);
            else if (Action > 0)
                SetAction(&c->Object, MONSTER01_ATTACK3);
            else
                SetAction(&c->Object, MONSTER01_ATTACK4);
        }
        else
        {
            bool Success = true;

            if (TheMapProcess().SetCurrentActionMonster(c, o))
                Success = false;

            if (Success)
            {
                if (c->SwordCount % 3 == 0)
                    SetAction(&c->Object, MONSTER01_ATTACK1);
                else
                    SetAction(&c->Object, MONSTER01_ATTACK2);
                c->SwordCount++;
            }
        }
    }
    if (TheMapProcess().CharacterPolicy().attackSounds && c->Object.AnimationFrame == 0.f)
    {
        PlayMonsterSound(o);

        if (o->Type != MODEL_PLAYER ||
            (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3))
        {
            if (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3)
                PlayBuffer(static_cast<ESound>(SOUND_BRANDISH_SWORD01 + WorldRandom() % 2), o);
            if (Models[o->Type].Sounds[2] != -1)
            {
                int offset = 0;
                if (o->SubType == 9)
                {
                    offset = 5;
                }
                PlayBuffer(static_cast<ESound>(SOUND_MONSTER + offset +
                                               Models[o->Type].Sounds[2 + WorldRandom() % 2]),
                           o);
            }
        }
        else
        {
            if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_BOW)
            {
                PlayBuffer(SOUND_BOW01, o);
            }
            else if (gCharacterManager.GetEquipedBowType(c) == BOWTYPE_CROSSBOW)
            {
                PlayBuffer(SOUND_CROSSBOW01, o);
            }
            else if (c->Weapon[0].Type >= MODEL_BLUEWING_CROSSBOW &&
                     c->Weapon[0].Type < MODEL_ARROWS)
                PlayBuffer(SOUND_MAGIC, o);
            else if (c->Weapon[0].Type == MODEL_LIGHT_SABER || c->Weapon[0].Type == MODEL_SPEAR)
                PlayBuffer(SOUND_BRANDISH_SWORD03, o);
            else if (c->Weapon[0].Type != -1 || c->Weapon[1].Type != -1)
                PlayBuffer(static_cast<ESound>(SOUND_BRANDISH_SWORD01 + WorldRandom() % 2), o);
        }
    }
    c->SwordCount++;
}

void SessionGameplayUnit::SetPlayerMagic(CHARACTER *c)
{
    OBJECT *o = &c->Object;
    if (o->Type == MODEL_PLAYER)
    {
        SetAttackSpeed();
        if ((c->Helper.Type == MODEL_HORN_OF_UNIRIA || c->Helper.Type == MODEL_HORN_OF_DINORANT) &&
            !c->SafeZone)
        {
            SetAction(o, PLAYER_RIDE_SKILL);
        }
        else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
        {
            SetAction(o, PLAYER_FENRIR_ATTACK_MAGIC);
        }
        else
        {
            if (gCharacterManager.IsFemale(c->Class))
                SetAction(o, PLAYER_SKILL_ELF1);
            else
                SetAction(o, PLAYER_SKILL_HAND1 + WorldRandom() % 2);
        }
    }
    else
    {
        if (c->SwordCount % 3 == 0)
            SetAction(&c->Object, MONSTER01_ATTACK1);
        else
            SetAction(&c->Object, MONSTER01_ATTACK2);
        c->SwordCount++;
    }
}

void SessionGameplayUnit::SetPlayerTeleport(CHARACTER *c)
{
    OBJECT *o = &c->Object;
    if (o->Type == MODEL_PLAYER)
    {
        SetAction(o, PLAYER_SKILL_TELEPORT);
    }
    else
    {
        SetAction(o, MONSTER01_SHOCK);
    }
}

void SessionGameplayUnit::SetPlayerShock(CHARACTER *c, int Hit)
{
    if (c->Dead > 0)
        return;
    if (c->Helper.Type == MODEL_HORN_OF_UNIRIA || c->Helper.Type == MODEL_HORN_OF_DINORANT)
        return;
    if (c->Helper.Type == MODEL_DARK_HORSE_ITEM)
        return;

    OBJECT *o = &c->Object;

    if (o->CurrentAction == PLAYER_ATTACK_SKILL_FURY_STRIKE ||
        o->CurrentAction == PLAYER_SKILL_VITALITY)
        return;
    if (o->CurrentAction == PLAYER_SKILL_HELL_BEGIN ||
        (o->CurrentAction == PLAYER_SKILL_ATT_UP_OURFORCES ||
         o->CurrentAction == PLAYER_SKILL_HP_UP_OURFORCES ||
         o->CurrentAction == PLAYER_SKILL_GIANTSWING ||
         o->CurrentAction == PLAYER_SKILL_DRAGONLORE))
        return;

    if (Hit > 0)
    {
        if (c->Object.Type == MODEL_PLAYER)
        {
            if (c->Helper.Type == MODEL_HORN_OF_FENRIR)
            {
                SetAction_Fenrir_Damage(c, &c->Object);
                if (Random.FpsCheck(3, 1.f))
                    PlayBuffer(static_cast<ESound>(SOUND_FENRIR_DAMAGE_1 + WorldRandom() % 2), o);
            }
            else
            {
                SetAction(&c->Object, PLAYER_SHOCK);
            }

            c->Movement = false;
        }
        else
        {
            if (o->CurrentAction < MONSTER01_ATTACK1 || o->CurrentAction > MONSTER01_ATTACK2)
                SetAction(&c->Object, MONSTER01_SHOCK);
            //c->Movement = false;
        }
        if (c->Object.AnimationFrame == 0.f)
        {
            PlayMonsterSound(o);

            if (o->Type != MODEL_PLAYER ||
                (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3))
            {
                if (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3)
                    PlayBuffer(SOUND_BONE1, o);
                else if (o->Type != MODEL_ASSASSIN && Models[o->Type].Sounds[2] != -1)
                {
                    int offset = 0;
                    if (o->SubType == 9)
                    {
                        offset = 5;
                    }
                    PlayBuffer(static_cast<ESound>(SOUND_MONSTER + offset +
                                                   Models[o->Type].Sounds[2 + WorldRandom() % 2]),
                               o);
                }
            }
            else
            {
                if (!gCharacterManager.IsFemale(c->Class))
                {
                    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD &&
                        WorldRandom() % 5)
                        PlayBuffer(SOUND_DARKLORD_PAIN, o);
                    else
                        PlayBuffer(static_cast<ESound>(SOUND_HUMAN_SCREAM01 + WorldRandom() % 3),
                                   o);
                }
                else
                    PlayBuffer(static_cast<ESound>(SOUND_FEMALE_SCREAM01 + WorldRandom() % 2), o);
            }
        }

        if (o->Type == MODEL_CASTLE_GATE)
        {
            vec3_t Position;
            for (int i = 0; i < 5; i++)
            {
                if (Random.FpsCheck(2, 1.f))
                {
                    Position[0] = o->Position[0] + (WorldRandom() % 128 - 64);
                    Position[1] = o->Position[1];
                    Position[2] = o->Position[2] + 200 + (WorldRandom() % 50);

                    CreateParticle(BITMAP_SMOKE + 1, Position, o->Angle, o->Light);
                }
            }
            PlayBuffer(SOUND_HIT_GATE);
        }
    }
}

void SessionGameplayUnit::SetPlayerDie(CHARACTER *c)
{
    OBJECT *o = &c->Object;

    if (c == Hero)
    {
        CharacterAttribute->Life = 0;
    }

    if (c->Object.Type == MODEL_PLAYER)
    {
        if (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3)
        {
            o->Live = false;
            CreateEffect(MODEL_BONE1, o->Position, o->Angle, o->Light);
            for (int j = 0; j < 10; j++)
                CreateEffect(MODEL_BONE2, o->Position, o->Angle, o->Light);
            PlayBuffer(SOUND_BONE2, o);
        }
        else
            SetAction(&c->Object, PLAYER_DIE1);
    }
    else
    {
        switch (o->Type)
        {
        case MODEL_DEATH_COW: {
            o->Live = false;
            CreateEffect(MODEL_BONE1, o->Position, o->Angle, o->Light);
            for (int j = 0; j < 10; j++)
                CreateEffect(MODEL_BONE2, o->Position, o->Angle, o->Light);
            PlayBuffer(SOUND_BONE2, o);
        }
        break;
        case MODEL_STONE_GOLEM: {
            o->Live = false;
            for (int j = 0; j < 8; j++)
            {
                CreateEffect(MODEL_BIG_STONE1, o->Position, o->Angle, o->Light);
                CreateEffect(MODEL_BIG_STONE2, o->Position, o->Angle, o->Light);
            }
            PlayBuffer(SOUND_BONE2, o);
        }
        break;
        case MODEL_BLADE_HUNTER:
        case MODEL_TWIN_TAIL:
        case MODEL_MAYA_HAND_LEFT:
        case MODEL_MAYA_HAND_RIGHT:
            if (!TheMapProcess().SetMonsterDeathAction(c, o))
                SetAction(&c->Object, MONSTER01_DIE);
            break;
        case MODEL_DOPPELGANGER: {
            if (c->Object.CurrentAction != MONSTER01_APEAR)
            {
                SetAction(&c->Object, MONSTER01_DIE);
            }
        }
        break;
        default:
            SetAction(&c->Object, MONSTER01_DIE);
            break;
        }
    }

    if (c->Object.AnimationFrame == 0.f)
    {
        PlayMonsterSound(o);
        if (TheMapProcess().PlayMonsterDeathSound(o))
        {
        }
        else
        {
            if (o->Type != MODEL_PLAYER ||
                (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3))
            {
                if (Models[o->Type].Sounds[4] != -1)
                {
                    int offset = 0;
                    if (o->SubType == 9)
                    {
                        offset = 5;
                    }
                    PlayBuffer(
                        static_cast<ESound>(SOUND_MONSTER + offset + Models[o->Type].Sounds[4]), o);
                }
            }
            else
            {
                if (!gCharacterManager.IsFemale(c->Class))
                {
                    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD)
                        PlayBuffer(SOUND_DARKLORD_DEAD, o);
                    else
                        PlayBuffer(SOUND_HUMAN_SCREAM04, o);
                }
                else
                {
                    PlayBuffer(SOUND_FEMALE_SCREAM02, o);
                }

                if (c->Helper.Type == MODEL_HORN_OF_FENRIR)
                    PlayBuffer(SOUND_FENRIR_DEATH, o); // 펜릴 죽는
            }
        }
        c->Object.AnimationFrame = 0.001f;
    }
}

void CalcAddPosition(OBJECT *o, float x, float y, float z, vec3_t Position)
{
    float Matrix[3][4];
    AngleMatrix(o->Angle, Matrix);
    vec3_t p;
    Vector(x, y, z, p);
    VectorRotate(p, Matrix, Position);
    VectorAdd(Position, o->Position, Position);
}

void SessionGameplayUnit::DarkPhoenixAttackPosition(const CHARACTER &character, bool wing,
                                                    vec3_t position, float fraction)
{
    const auto &object = character.Object;
    auto &model = Models[object.Type + (wing ? 1 : 0)];
    model.BodyScale = object.Scale;
    model.BodyHeight = 0.f;
    if (fraction != 1.f)
    {
        ObjectDrawInput draw(&object);
        draw.type += wing ? 1 : 0;
        AnimationPoseSample pose(draw, model.BoneHead, 0.f, false, model.PoseAssetIdentity());
        vec3_t origin{};
        pose.SampleBonePosition(model, object, wing ? 23 : 24, origin, WorldTime, fraction,
                                position);
        return;
    }
    model.CurrentAction = object.CurrentAction;
    VectorCopy(object.Position, model.BodyOrigin);
    const vec34_t *pose = object.BoneTransform;
    if (wing)
    {
        model.Animation(BoneTransform, object.AnimationFrame, object.PriorAnimationFrame,
                        object.PriorAction, object.Angle, object.HeadAngle, false, false);
        pose = BoneTransform;
    }
    constexpr int bodyAttackBone = 24;
    constexpr int wingAttackBone = 23;
    const vec3_t origin{};
    model.TransformPosition(pose[wing ? wingAttackBone : bodyAttackBone], origin, position, true);
}

bool SessionGameplayUnit::CheckMonsterSkill(CHARACTER *pCharacter, OBJECT *pObject)
{
    if (pCharacter->MonsterIndex == MONSTER_MAYA)
    {
        MayaSceneMayaAction(pCharacter->MonsterSkill);
        return true;
    }

    int iCheckAttackAni = -1;

    for (int i = 0; i < MAX_MONSTERSKILL_NUM; i++)
    {
        if (MonsterSkill[pCharacter->MonsterIndex].Skill_Num[i] == pCharacter->MonsterSkill)
        {
            iCheckAttackAni = i;
            break;
        }
        else
        {
            iCheckAttackAni = -1;
        }
    }

    switch (iCheckAttackAni)
    {
    case 0:
        SetAction(pObject, MONSTER01_ATTACK1);
        break;
    case 1:
        SetAction(pObject, MONSTER01_ATTACK2);
        break;
    case 2:
        SetAction(pObject, MONSTER01_ATTACK3);
        break;
    case 3:
        SetAction(pObject, MONSTER01_ATTACK4);
        break;
    case 4:
        SetAction(pObject, MONSTER01_ATTACK5);
        break;
    case 5:
        SetAction(pObject, MONSTER01_ATTACK5);
        break;
    case 6:
        SetAction(pObject, MONSTER01_ATTACK5);
        break;
    case 7:
        SetAction(pObject, MONSTER01_ATTACK5);
        break;
    case 8:
        SetAction(pObject, MONSTER01_ATTACK5);
        break;
    case 9:
        SetAction(pObject, MONSTER01_ATTACK5);
        break;
    }

    pCharacter->MonsterSkill = -1;

    if (iCheckAttackAni < 0)
    {
        SetAction(pObject, MONSTER01_ATTACK1);
    }

    if (iCheckAttackAni > 4)
        return false;

    return true;
}

float SessionGameplayUnit::CharacterAnimationSpeed(CHARACTER *c, OBJECT *o, BMD *b, float frame)
{
    float PlaySpeed = 0.f;
    if (b->NumActions > 0)
    {
        PlaySpeed = b->Actions[b->CurrentAction].PlaySpeed;
        if (PlaySpeed < 0.f)
            PlaySpeed = 0.f;
        if (c->Change && o->CurrentAction >= MONSTER01_ATTACK1 &&
            o->CurrentAction <= MONSTER01_ATTACK2)
            PlaySpeed *= 1.5f;
        if (o->CurrentAction == PLAYER_SKILL_VITALITY && frame > 6.f)
        {
            PlaySpeed *= 1.0f / (2.f);
        }
        else if ((o->CurrentAction == PLAYER_ATTACK_TELEPORT ||
                  o->CurrentAction == PLAYER_ATTACK_RIDE_TELEPORT ||
                  o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_TELEPORT) &&
                 frame > 5.5f)
        {
            PlaySpeed *= 1.0f / (10.f);
        }
        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD &&
                 (o->CurrentAction == PLAYER_SKILL_FLASH ||
                  o->CurrentAction == PLAYER_ATTACK_RIDE_ATTACK_FLASH ||
                  o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_FLASH) &&
                 (frame > 1.f && frame < 3.f))
        {
            if (g_pPartyManager->IsPartyMemberChar(c) == false)
            {
                PlaySpeed *= 1.0f / (2.f);
            }
            else
            {
                PlaySpeed *= 1.0f / (8.f);
            }
        }
        if (o->CurrentAction == PLAYER_SKILL_HELL_BEGIN)
        {
            PlaySpeed *= 1.0f / (2.f);
        }
        if (o->Type != MODEL_PLAYER)
        {
            switch (o->Type)
            {
            case MODEL_ILLUSION_OF_KUNDUN:
                if (o->CurrentAction == MONSTER01_DIE && frame > 6)
                    PlaySpeed *= 4.0f;
                break;
            case MODEL_FACE:
            case MODEL_FACE + 1:
            case MODEL_FACE + 2:
            case MODEL_FACE + 3:
            case MODEL_FACE + 4:
            case MODEL_FACE + 5:
            case MODEL_FACE + 6:
                PlaySpeed *= 2.0f;
                break;
            }
        }
        if (o->Type == MODEL_EROHIM)
        {
            if (o->CurrentAction == MONSTER01_DIE)
                PlaySpeed *= 1.0f / (2.f);
        }
    }

    return PlaySpeed;
}

float SessionGameplayUnit::CharacterAnimationBoundary(CHARACTER *c, OBJECT *o, BMD *b)
{
    const auto &action = b->Actions[b->CurrentAction];
    float boundary =
        float(action.NumAnimationKeys - (!action.Loop && action.LockPositions ? 1 : 0));
    const auto consider = [&](float value) {
        if (value > o->AnimationFrame)
            boundary = (std::min)(boundary, value);
    };
    if (o->Type == MODEL_DOPPELGANGER && o->CurrentAction == MONSTER01_APEAR &&
        !o->m_bActionStart && gMapManager.ContextMap() >= WD_65DOPPLEGANGER1 &&
        gMapManager.ContextMap() <= WD_68DOPPLEGANGER4)
        consider(CGMDoppelGanger1::AppearanceEndFrame);
    if (o->Type == MODEL_KANTURU2ND_ENTER_NPC && o->CurrentAction == KANTURU2ND_NPC_ANI_ROT)
        consider(50.f);
    if (o->CurrentAction == PLAYER_SKILL_VITALITY ||
        (o->Type == MODEL_ILLUSION_OF_KUNDUN && o->CurrentAction == MONSTER01_DIE))
        consider(6.f);
    if (o->CurrentAction == PLAYER_ATTACK_TELEPORT ||
        o->CurrentAction == PLAYER_ATTACK_RIDE_TELEPORT ||
        o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_TELEPORT)
        consider(5.5f);
    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD &&
        (o->CurrentAction == PLAYER_SKILL_FLASH ||
         o->CurrentAction == PLAYER_ATTACK_RIDE_ATTACK_FLASH ||
         o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_FLASH))
    {
        consider(1.f);
        consider(3.f);
    }
    return boundary;
}

bool SessionGameplayUnit::CharacterAnimation(CHARACTER *c, OBJECT *o)
{
    return CharacterAnimation(c, o, FPS_ANIMATION_FACTOR);
}

bool SessionGameplayUnit::CharacterAnimation(CHARACTER *c, OBJECT *o, float frames)
{
    bool Play = true;
    BMD *b = &Models[o->Type];
    b->CurrentAction = o->CurrentAction;
    const auto phase = [&] {
        return ObjectMotionTrace::AnimationPhase{o->AnimationFrame, o->PriorAnimationFrame,
                                                 o->CurrentAction, o->PriorAction};
    };

    if (g_isCharacterBuff(o, eDeBuff_Stun) || g_isCharacterBuff(o, eDeBuff_Sleep))
    {
        o->MotionTrace.AdvanceAnimation(frames, phase(), 0.f);
        return false;
    }

    if (frames > 0.f)
        FinishScriptedAnimationPhase(*o, *b);
    float remaining = frames;
    if (b->NumActions <= 0 || b->Actions[b->CurrentAction].NumAnimationKeys <= 1)
    {
        o->MotionTrace.AdvanceAnimation(frames, phase(), 0.f);
        return true;
    }
    while (remaining > 0.f)
    {
        // A scripted transition may enter a one-key idle during this interval.
        if (b->Actions[b->CurrentAction].NumAnimationKeys <= 1)
        {
            o->MotionTrace.AdvanceAnimation(remaining, phase(), 0.f);
            remaining = 0.f;
            break;
        }
        const float sample =
            std::nextafter(o->AnimationFrame, std::numeric_limits<float>::infinity());
        const float speed = CharacterAnimationSpeed(c, o, b, sample);
        if (speed <= 0.f)
            break;
        const float distance = CharacterAnimationBoundary(c, o, b) - o->AnimationFrame;
        const float step = distance > 0.f ? (std::min)(remaining, distance / speed) : remaining;
        const auto startPhase = phase();
        const bool continued =
            b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction, speed,
                             o->Position, o->Angle, step);
        o->MotionTrace.AdvanceAnimation(step, startPhase, speed * step);
        Play = continued && Play;
        remaining -= step;
        FinishScriptedAnimationPhase(*o, *b);
        if (!continued && b->Actions[b->CurrentAction].Loop)
            break;
    }

    if (o->CurrentAction == PLAYER_CHANGE_UP)
    {
        if (Play == false)
            SetPlayerStop(c);
    }

    if (o->CurrentAction == PLAYER_RECOVER_SKILL)
    {
        if (Play == false)
            SetPlayerStop(c);
    }
    if (remaining > 0.f)
        o->MotionTrace.AdvanceAnimation(remaining, phase(), 0.f);
    return Play;
}

void SessionGameplayUnit::FinishScriptedAnimationPhase(OBJECT &object, BMD &model)
{
    if (object.Type == MODEL_DOPPELGANGER && object.CurrentAction == MONSTER01_APEAR &&
        !object.m_bActionStart && object.AnimationFrame >= CGMDoppelGanger1::AppearanceEndFrame &&
        gMapManager.ContextMap() >= WD_65DOPPLEGANGER1 &&
        gMapManager.ContextMap() <= WD_68DOPPLEGANGER4)
    {
        object.m_bActionStart = true;
        SetAction(&object, MONSTER01_ATTACK1);
    }
    else if (object.Type == MODEL_KANTURU2ND_ENTER_NPC &&
             object.CurrentAction == KANTURU2ND_NPC_ANI_ROT && object.AnimationFrame >= 50.f)
        SetAction(&object, KANTURU2ND_NPC_ANI_STOP);
    model.CurrentAction = object.CurrentAction;
}

int GetHandOfWeapon(OBJECT *o)
{
    int Hand = 0;
    if (o->Type == MODEL_PLAYER)
    {
        if (o->CurrentAction == PLAYER_ATTACK_SWORD_LEFT1 ||
            o->CurrentAction == PLAYER_ATTACK_SWORD_LEFT2)
            Hand = 1;
    }

    return (Hand);
}

namespace CharacterPresentationDetail
{
// The frame at which an attack animation lands its hit; before this the swing
// is still winding up.
constexpr float ATTACK_IMPACT_FRAME = 5.f;

bool IsMonsterAttackAction(const OBJECT *o)
{
    return o->Type >= MODEL_MONSTER01 && o->Type < MODEL_MONSTER_END &&
           o->CurrentAction >= MONSTER01_ATTACK1 && o->CurrentAction <= MONSTER01_ATTACK2;
}

// True once a player or monster swing has reached its impact frame, i.e. the
// moment the hit, damage numbers and sound should fire.
bool IsAttackImpactFrame(const OBJECT *o)
{
    if (o->AnimationFrame < ATTACK_IMPACT_FRAME)
        return false;

    const bool bPlayerAttack =
        o->Type == MODEL_PLAYER && Engine::Object::IsAttackAction(o->CurrentAction);
    return bPlayerAttack || IsMonsterAttackAction(o);
}
} // namespace CharacterPresentationDetail

bool SessionGameplayUnit::AttackStage(CHARACTER *c, OBJECT *o)
{
    // 무기 위치 얻기
    int Hand = GetHandOfWeapon(o);

    int iSkill = (c->Skill);

    g_iLimitAttackTime = 15;
    switch (iSkill)
    {
    case AT_SKILL_DEATHSTAB:
    case AT_SKILL_DEATHSTAB_STR: {
        BMD *b = &Models[o->Type];

        if (b->Bones[c->Weapon[Hand].LinkBone].Dummy || c->Weapon[Hand].LinkBone >= b->NumBones)
        {
            break;
        }

        if (c->CheckAttackTime(8))
        {
            if (SceneFlag != LOG_IN_SCENE)
                PlayBuffer(SOUND_SKILL_SWORD2);

            c->SetLastAttackEffectTime();
        }

        if (2 <= c->AttackTime && c->AttackTime <= 8)
            for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
            {
                for (int j = 0; j < 3; ++j)
                {
                    vec3_t CurPos;
                    o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, CurPos);
                    CurPos[2] += 120.0f;
                    vec3_t TempPos;
                    GetNearRandomPos(CurPos, 300, TempPos);
                    float fDistance = 1400.0f;
                    TempPos[0] += -fDistance * sinf(o->Angle[2] * Q_PI / 180.0f);
                    TempPos[1] += fDistance * cosf(o->Angle[2] * Q_PI / 180.0f);
                    CreateJoint(MODEL_SPEARSKILL, TempPos, TempPos, o->Angle, 2, o, 40.0f);
                }
            }

        if (c->AttackTime <= 8)
        { // 기 모일 곳 위치
            vec3_t Position2 = {0.0f, 0.0f, 0.0f};
            b->TransformPosition(o->BoneTransform[c->Weapon[Hand].LinkBone], Position2,
                                 o->m_vPosSword, true);

            float fDistance = 300.0f;
            o->m_vPosSword[0] += fDistance * sinf(o->Angle[2] * Q_PI / 180.0f);
            o->m_vPosSword[1] += -fDistance * cosf(o->Angle[2] * Q_PI / 180.0f);
        }

        if (6 <= c->AttackTime && c->AttackTime <= 12)
        { // 꼬깔 만들기
            for (auto birth : Emissions(FPS_ANIMATION_FACTOR / 2.f))
            {
                vec3_t Position;

                //memcpy( Position, o->Position, sizeof ( vec3_t));
                vec3_t Position2 = {0.0f, 0.0f, 0.0f};

                AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                         b->PoseAssetIdentity());
                pose.SampleBonePosition(*b, *o, c->Weapon[Hand].LinkBone, Position2, WorldTime,
                                        birth.FrameFraction(), Position);

                float fDistance = 100.0f + (float)(c->AttackTime - 8) * 10.0f;
                Position[0] += fDistance * sinf(o->Angle[2] * Q_PI / 180.0f);
                Position[1] += -fDistance * cosf(o->Angle[2] * Q_PI / 180.0f);
                //Position[2] += 110.0f;
                vec3_t Light = {1.0f, 1.0f, 1.0f};
                CreateEffect(MODEL_SPEAR, Position, o->Angle, Light, 1, o);
                CreateEffect(MODEL_SPEAR, Position, o->Angle, Light, 1, o);
            }

            if (c->TargetCharacter != -1)
            {
                CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                if (c->TargetCharacter != -1)
                {
                    OBJECT *to = &tc->Object;
                    if (10 <= c->AttackTime && to->Live)
                    {
                        //PlayBuffer( SOUND_THUNDER01);
                        to->m_byHurtByDeathstab = 35;
                    }
                }
            }
        }
        if (c->AttackTime >= 12)
        {
            c->AttackTime = g_iLimitAttackTime;
        }
    }
    break;
    case AT_SKILL_IMPALE: // 창찌르기
    {
        BMD *b = &Models[o->Type];

        vec3_t p;
        if (c->CheckAttackTime(10))
        {
            PlayBuffer(SOUND_RIDINGSPEAR);
            c->SetLastAttackEffectTime();
        }
        else if (c->CheckAttackTime(4))
        { // 준비동작
            vec3_t Light = {1.0f, 1.0f, .5f};
            vec3_t Position2 = {0.0f, 0.0f, 0.0f};
            b->TransformPosition(o->BoneTransform[c->Weapon[Hand].LinkBone], Position2, p, true);
            CreateEffect(MODEL__SPEAR, p, o->Angle, Light, c->Weapon[Hand].Type, o);
            //CreateEffect(BITMAP_MAGIC+1,o->Position,o->Angle,Light,4,o);
            c->SetLastAttackEffectTime();
        }
        else if (c->CheckAttackTime(8))
        { // 꼬깔 만들기
            vec3_t Position;
            memcpy(Position, o->Position, sizeof(vec3_t));
            Position[0] += 50.0f * sinf(o->Angle[2] * Q_PI / 180.0f);
            Position[1] += -50.0f * cosf(o->Angle[2] * Q_PI / 180.0f);
            Position[2] += 110.0f;
            vec3_t Light = {1.0f, 1.0f, 1.0f};
            CreateEffect(MODEL_SPEAR, Position, o->Angle, Light, 0, o);
            CreateEffect(MODEL_SPEAR, Position, o->Angle, Light, 0, o);
            c->SetLastAttackEffectTime();
        }
        if (13 <= c->AttackTime && c->AttackTime <= 14)
        { // 현란한 창술
            for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
            {
                for (int i = 0; i < 3; ++i)
                {
                    vec3_t Position;
                    o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, Position);
                    Position[0] += 145.0f * sinf(o->Angle[2] * Q_PI / 180.0f);
                    Position[1] += -145.0f * cosf(o->Angle[2] * Q_PI / 180.0f);
                    Position[2] += 110.0f;
                    //vec3_t Light = { .6f, .6f, .2f};
                    vec3_t Light = {.3f, .3f, .3f};
                    if (c->CheckAttackTime(11))
                    {
                    }
                    else
                    {
                        Position[0] += (WorldRandom() % 60 - 30);
                        Position[1] += (WorldRandom() % 60 - 30);
                        Position[2] += (WorldRandom() % 60 - 30);
                    }
                    CreateEffect(MODEL_SPEARSKILL, Position, o->Angle, Light, c->Weapon[Hand].Type,
                                 o);
                }
            }
        }
    }
    break;

    case AT_SKILL_PENETRATION:
    case AT_SKILL_PENETRATION_STR:
        if (o->Type == MODEL_PLAYER && Engine::Object::IsAttackAction(o->CurrentAction))
        {
            if (o->AnimationFrame >= 5.f)
            {
                o->PriorAnimationFrame = 4.f;
                o->AnimationFrame = 5.f;
            }
        }

        if (c->CheckAttackTime(3)) //  氣 모으기.
        {
            CreateEffect(BITMAP_GATHERING, o->Position, o->Angle, o->Light, 0, o);
            PlayBuffer(SOUND_PIERCING, o);
            c->SetLastAttackEffectTime();
        }
        g_iLimitAttackTime = 5;
        break;

    case AT_SKILL_FIRE_SLASH:
    case AT_SKILL_FIRE_SLASH_STR:
        if (o->Type == MODEL_PLAYER)
        {
            SetAction(o, PLAYER_ATTACK_SKILL_WHEEL);

            if (c->CheckAttackTime(1) || c->CheckAttackTime(2))
            {
                vec3_t Angle;
                Vector(1.f, 0.f, 0.f, Angle);
                CreateEffect(BITMAP_GATHERING, o->Position, o->Angle, o->Light, 1, o);
                c->SetLastAttackEffectTime();
            }

            if (o->AnimationFrame >= 3.f)
            {
                o->PKKey = getTargetCharacterKey(c, SelectedCharacter);

                PlayBuffer(SOUND_SKILL_SWORD3);

                if (iSkill == AT_SKILL_FIRE_SLASH_STR)
                {
                    CreateEffect(BITMAP_SWORD_FORCE, o->Position, o->Angle, o->Light, 1, o,
                                 o->PKKey, FindHotKey(iSkill));
                }
                else
                {
                    CreateEffect(BITMAP_SWORD_FORCE, o->Position, o->Angle, o->Light, 0, o,
                                 o->PKKey, FindHotKey(iSkill));
                }

                float AttackSpeed1 = CharacterAttribute->AttackSpeed * 0.004f; //
                Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SKILL_WHEEL].PlaySpeed =
                    0.54f + AttackSpeed1;
                c->AttackTime = 15;
            }
        }
        g_iLimitAttackTime = 15;
        break;

    case AT_SKILL_POWER_SLASH:
    case AT_SKILL_POWER_SLASH_STR:
        if (o->Type == MODEL_PLAYER && o->CurrentAction == PLAYER_ATTACK_TWO_HAND_SWORD_TWO)
        {
            vec3_t Angle;

            VectorCopy(o->Angle, Angle);

            Angle[2] -= 40.f;
            CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light, 2, o);
            Angle[2] += 20.f;
            CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light, 2, o);
            Angle[2] += 20.f;
            CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light, 2, o);
            Angle[2] += 20.f;
            CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light, 2, o);
            Angle[2] += 20.f;
            CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light, 2, o);

            PlayBuffer(SOUND_SKILL_SWORD3);

            c->AttackTime = 15;
        }
        g_iLimitAttackTime = 15;
        break;

    case AT_SKILL_NOVA:
        if (o->AnimationFrame >= 14.f && o->Type == MODEL_PLAYER &&
            o->CurrentAction == PLAYER_SKILL_HELL_START)
        {
            c->AttackTime = 15;
        }
        break;
    case AT_SKILL_SWELL_LIFE:
    case AT_SKILL_SWELL_LIFE_STR:
    case AT_SKILL_SWELL_LIFE_PROFICIENCY:
        if ((int)c->AttackTime > 9 && o->Type == MODEL_PLAYER &&
            o->CurrentAction == PLAYER_SKILL_VITALITY)
        {
            c->AttackTime = 15;
        }
        break;
    case AT_SKILL_IMPROVE_AG:
        if (CharacterPresentationDetail::IsAttackImpactFrame(o))
        {
            c->AttackTime = 15;
        }
        break;
    case AT_SKILL_FORCE:
    case AT_SKILL_FORCE_WAVE:
    case AT_SKILL_FORCE_WAVE_STR:
        if (o->AnimationFrame >= 3.f && o->Type == MODEL_PLAYER &&
            (o->CurrentAction == PLAYER_ATTACK_STRIKE ||
             o->CurrentAction == PLAYER_ATTACK_RIDE_STRIKE ||
             o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE))
        {
            c->AttackTime = 15;
        }
        break;
    case AT_SKILL_FIREBURST:
    case AT_SKILL_FIREBURST_STR:
    case AT_SKILL_FIREBURST_MASTERY:
        if (o->AnimationFrame >= 3.f && o->Type == MODEL_PLAYER &&
            (o->CurrentAction == PLAYER_ATTACK_STRIKE ||
             o->CurrentAction == PLAYER_ATTACK_RIDE_STRIKE ||
             o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE))
        {
            c->AttackTime = 15;
        }
        break;
    case AT_SKILL_THUNDER_STRIKE:
        if (o->AnimationFrame >= 5.5f && o->Type == MODEL_PLAYER &&
            (o->CurrentAction == PLAYER_SKILL_FLASH ||
             o->CurrentAction == PLAYER_ATTACK_RIDE_ATTACK_FLASH ||
             o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_FLASH))
        {
            c->AttackTime = 15;
        }
        else
        {
            c->AttackTime = 10;
        }
        break;
    case AT_SKILL_EARTHSHAKE:
    case AT_SKILL_EARTHSHAKE_STR:
    case AT_SKILL_EARTHSHAKE_MASTERY:
        if (o->AnimationFrame >= 5.f && o->Type == MODEL_PLAYER &&
            o->CurrentAction == PLAYER_ATTACK_DARKHORSE)
        {
            c->AttackTime = 15;
        }
        break;
    case AT_SKILL_PARTY_TELEPORT:
        if ((int)c->AttackTime > 5 && o->Type == MODEL_PLAYER &&
            (o->CurrentAction == PLAYER_ATTACK_TELEPORT ||
             o->CurrentAction == PLAYER_ATTACK_RIDE_TELEPORT ||
             o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_TELEPORT))
        {
            c->AttackTime = 15;
        }
        break;
    case AT_SKILL_RIDER:
        if (o->AnimationFrame >= 5.f && o->Type == MODEL_PLAYER &&
            (o->CurrentAction == PLAYER_SKILL_RIDER || o->CurrentAction == PLAYER_SKILL_RIDER_FLY))
        {
            c->AttackTime = 15;
        }
        break;

    case AT_SKILL_STUN:
    case AT_SKILL_MANA:
        break;
    case AT_SKILL_INVISIBLE:
        c->AttackTime = 15;
        break;
    case AT_SKILL_REMOVAL_STUN:
    case AT_SKILL_REMOVAL_INVISIBLE:
        c->AttackTime = 15;
        break;
    case AT_SKILL_REMOVAL_BUFF:
        if (o->AnimationFrame >= 3.5f)
        {
            c->AttackTime = 15;
        }
        break;

    case AT_SKILL_RUSH:
    case AT_SKILL_OCCUPY:
        if (o->AnimationFrame > 5.f)
        {
            c->AttackTime = 15;
        }
        for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t Position;
            vec3_t Angle;

            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, Position);
            Position[0] += WorldRandom() % 30 - 15.f;
            Position[1] += WorldRandom() % 30 - 15.f;
            Position[2] += 20.f;
            for (int i = 0; i < 4; i++)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2], Angle);
                CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
                if (iSkill == AT_SKILL_OCCUPY)
                    CreateParticle(BITMAP_FIRE, Position, Angle, o->Light, 18, 1.5f);
                else
                    CreateParticle(BITMAP_FIRE, Position, Angle, o->Light, 2, 1.5f);
            }
        }
        break;
    case AT_SKILL_SPIRAL_SLASH:
        if (o->AnimationFrame > 5.f)
        {
            CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 23, NULL, 40.f, 0);
            CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 23, NULL, 40.f, 1);
            CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 23, NULL, 40.f, 4);
            c->AttackTime = 15;

            PlayBuffer(SOUND_BCS_ONE_FLASH);
        }
        else if (o->AnimationFrame > 2.3f && o->AnimationFrame < 2.6f && rand_fps_check(1))
        {
            CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 23, NULL, 40.f, 2);
            CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 23, NULL, 40.f, 3);

            PlayBuffer(SOUND_BCS_ONE_FLASH);
        }
        g_iLimitAttackTime = 15;
        break;

    case AT_SKILL_SPACE_SPLIT:
        if (o->AnimationFrame >= 3.f && o->Type == MODEL_PLAYER &&
            (o->CurrentAction == PLAYER_ATTACK_STRIKE ||
             o->CurrentAction == PLAYER_ATTACK_RIDE_STRIKE ||
             o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE))
        {
            c->AttackTime = 15;
        }
        break;

    case AT_SKILL_DEATH_CANNON:
        if (o->AnimationFrame >= 3.f && o->Type == MODEL_PLAYER &&
            o->CurrentAction == PLAYER_ATTACK_DEATH_CANNON)
        {
            c->AttackTime = 15;
        }
        break;
    case AT_SKILL_FLAME_STRIKE: {
        c->AttackTime = 15;
    }
    break;
    case AT_SKILL_GIGANTIC_STORM:
        if (o->AnimationFrame > 7.f)
        {
            c->AttackTime = 15;
        }
        break;

    case AT_SKILL_LIGHTNING_SHOCK_STR:
    case AT_SKILL_LIGHTNING_SHOCK: {
        c->AttackTime = 15;
    }
    break;
    case AT_SKILL_STRIKE_OF_DESTRUCTION:
    case AT_SKILL_STRIKE_OF_DESTRUCTION_STR: {
        c->AttackTime = 15;
    }
    break;
    case AT_SKILL_ATT_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES:
    case AT_SKILL_DEF_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES_MASTERY: {
        c->AttackTime = 15;
    }
    break;
    case AT_SKILL_DRAGON_KICK: {
        c->AttackTime = 1;
    }
    break;
    case AT_SKILL_CHAIN_DRIVE:
    case AT_SKILL_CHAIN_DRIVE_STR:
    case AT_SKILL_BEAST_UPPERCUT:
    case AT_SKILL_BEAST_UPPERCUT_STR:
    case AT_SKILL_BEAST_UPPERCUT_MASTERY: {
        o->m_sTargetIndex = c->TargetCharacter;
    }
    break;
    default:
        if (o->AnimationFrame >= 1.f && o->Type == MODEL_PLAYER &&
            o->CurrentAction == PLAYER_ATTACK_SKILL_FURY_STRIKE)
        {
            c->AttackTime = 15;
        }
        else if (CharacterPresentationDetail::IsAttackImpactFrame(o))
        {
            int RightType = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;
            int LeftType = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;

            if (c->AttackTime >= 1 && LeftType == ITEM_SYLPH_WIND_BOW && o->Type == MODEL_PLAYER &&
                rand_fps_check(1))
            {
                for (int i = 0; i < 20; i++)
                {
                    CreateParticle(BITMAP_SPARK + 1, o->Position, o->Angle, o->Light, 12, 2.0f);
                }
            }

            c->AttackTime = 15;
        }

        break;
    }
    return true;
}

void DeleteCloth(CHARACTER *character, OBJECT *object, PART_t *)
{
    // Equipment facts invalidate observer-owned cloth on the next reconciliation.
    if (character)
        character->MarkAppearanceChanged();
    if (!object || !object->m_pCloth)
        return;
    CPhysicsCloth::DestroyArray(static_cast<CPhysicsCloth *>(object->m_pCloth),
                                object->m_byNumCloth);
    object->m_pCloth = nullptr;
    object->m_byNumCloth = 0;
}

void SessionGameplayUnit::FallingCharacter(CHARACTER *c, OBJECT *o)
{
    float Matrix[3][4];
    vec3_t Position, p;

    Vector(0.f, 0.f, 0.f, Position);
    o->Direction[1] += o->Direction[0] * FPS_ANIMATION_FACTOR;
    Vector(0.f, o->Direction[1] - o->Direction[0], 0.f, p);
    AngleMatrix(o->m_vDownAngle, Matrix);
    VectorRotate(p, Matrix, Position);

    Core::Time::Advance(o->Gravity, o->Velocity, -o->Direction[2], FPS_ANIMATION_FACTOR);
    o->Angle[0] -= 5.f * FPS_ANIMATION_FACTOR;

    o->Position[0] = o->m_vDeadPosition[0] + Position[0];
    o->Position[1] = o->m_vDeadPosition[1] + Position[1];
    o->Position[2] = o->m_vDeadPosition[2] + o->Gravity;
}

namespace CharacterPresentationDetail
{
void AdvanceDeathKnockback(OBJECT &object, float frames)
{
    const float speed = object.Direction[1], deceleration = object.Velocity;
    float moving = 0.f;
    bool stopped = speed <= 0.f;
    if (speed > 0.f)
    {
        const float derivative = (std::max)(0.f, speed - deceleration * 0.5f + 1.f / 6.f);
        const float stop =
            -deceleration + std::sqrt(deceleration * deceleration + 2.f * derivative);
        moving = (std::min)(frames, stop);
        stopped = frames >= stop;
    }
    vec3_t local{object.Direction[0] * frames,
                 speed * moving - deceleration * moving * (moving + 1.f) * 0.5f -
                     moving * (moving - 1.f) * (moving + 1.f) / 6.f,
                 object.Direction[2] * frames};
    float matrix[3][4];
    vec3_t movement;
    AngleMatrix(object.HeadAngle, matrix);
    VectorRotate(local, matrix, movement);
    VectorAdd(object.Position, movement, object.Position);
    object.Direction[1] =
        stopped ? 0.f
                : (std::max)(0.f, speed - deceleration * frames - frames * (frames - 1.f) * 0.5f);
    object.Velocity += frames;
}

void AdvanceBallMotion(OBJECT *o, float Height, float animationFactor)
{
    Core::Time::AdvanceBouncing(o->Position[2], o->Gravity, Height, 6.f, 0.4f, animationFactor);
    VectorAddScaled(o->Angle, o->Direction, o->Angle,
                    Core::Time::DampedDistance(0.8f, animationFactor));
    VectorScale(o->Direction, std::pow(0.8f, animationFactor), o->Direction);
}
} // namespace CharacterPresentationDetail

void SessionGameplayUnit::AdvanceDefaultCharacterPush(CHARACTER *c, OBJECT *o, float Speed)
{
    const float frames = std::clamp(static_cast<float>(CharacterPushEndFrame - c->JumpTime), 0.f,
                                    FPS_ANIMATION_FACTOR);
    o->Position[0] += (((float)c->TargetX + 0.5f) * TERRAIN_SCALE - o->Position[0]) *
                      Core::Time::Blend(Speed, frames);
    o->Position[1] += (((float)c->TargetY + 0.5f) * TERRAIN_SCALE - o->Position[1]) *
                      Core::Time::Blend(Speed, frames);
    if (o->Type != MODEL_BALL)
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
    c->JumpTime += frames;
    if (CharacterPushEndFrame - c->JumpTime <= 0.000001)
    {
        c->PositionX = c->TargetX;
        c->PositionY = c->TargetY;
        o->Position[0] = ((float)c->TargetX + 0.5f) * TERRAIN_SCALE;
        o->Position[1] = ((float)c->TargetY + 0.5f) * TERRAIN_SCALE;
        if (o->Type != MODEL_BALL)
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);

        if (o->Type == MODEL_CRUST)
            SetPlayerStop(c);

        c->JumpTime = 0;
    }
}

void SessionGameplayUnit::PushingCharacter(CHARACTER *c, OBJECT *o)
{
    if (c->StormTime > 0)
    {
        const float frames = (std::min)(c->StormTime, FPS_ANIMATION_FACTOR);
        o->Angle[2] += 10.f * frames * (c->StormTime - (frames - 1.f) * 0.5f);
        c->StormTime -= frames;
    }
    if (c->JumpTime > 0)
    {
        float Speed = 0.2f;
        if (o->Type == MODEL_CRUST)
        {
            Speed = 0.07f;
        }
        if (!TheMapProcess().PushCharacter(c, o, Speed))
            AdvanceDefaultCharacterPush(c, o, Speed);
    }
    if (o->Type == MODEL_BALL)
        CharacterPresentationDetail::AdvanceBallMotion(
            o, RequestTerrainHeight(o->Position[0], o->Position[1]) + 30.f, FPS_ANIMATION_FACTOR);
}

void DeadCharacterBuff(OBJECT *o)
{
    g_CharacterUnRegisterBuff(o, eDeBuff_Stun);
    g_CharacterUnRegisterBuff(o, eBuff_Cloaking);
    g_CharacterUnRegisterBuff(o, eDeBuff_Harden);
    g_CharacterUnRegisterBuff(o, eDeBuff_Sleep);
}

void SessionGameplayUnit::DeadCharacter(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (c->Dead <= 0)
        return;

    DeadCharacterBuff(o);

    c->Rot += TheMapProcess().MonsterDeathRotationRate(*o) * FPS_ANIMATION_FACTOR;
    float RotTime = 1.f;
    if (c->Rot >= RotTime)
    {
        if (o->Type != MODEL_DREADFEAR)
        {
            o->Alpha = 1.f - (c->Rot - RotTime);
        }

        if (o->Alpha >= 0.01f)
            o->Position[2] -= 0.4f * FPS_ANIMATION_FACTOR;
        else if (c != Hero)
        {
            o->Live = false;
            c->m_byDieType = 0;
            o->m_bActionStart = false;
            o->m_bySkillCount = 0;

            boneManager_.UnregisterBone(c);
        }
        DeleteCloth(c, o);
    }

    if (!TheMapProcess().AdvanceCharacterDeath(c, o))
    {
        switch (c->m_byDieType)
        {
        case AT_SKILL_NOVA:
        case AT_SKILL_COMBO:
            constexpr float KnockbackEnd = 15.f;
            const float knockbackFrames =
                std::clamp(KnockbackEnd - c->Dead, 0.f, FPS_ANIMATION_FACTOR);
            CharacterPresentationDetail::AdvanceDeathKnockback(*o, knockbackFrames);

            if (c->Dead <= 30 && c->m_byDieType == AT_SKILL_NOVA)
                for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
                {
                    vec3_t Light, p, Position;
                    ObjectDrawInput draw(o);
                    o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position,
                                          draw.position);
                    AnimationPoseSample pose(draw, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    std::array<vec34_t, MAX_BONES> bones;
                    draw.bones =
                        pose.EvaluateAtTime(*b, *o, WorldTime, birth.FrameFraction(), bones.data());
                    Vector(0.3f, 0.3f, 1.f, Light);
                    Vector(0.f, 0.f, 0.f, p);
                    for (int i = 0; i < 10; i++)
                    {
                        b->TransformByObjectBone(Position, draw, WorldRandom() % 32, p);
                        CreateParticle(BITMAP_LIGHT, Position, o->Angle, Light, 5,
                                       0.5f + (WorldRandom() % 100) / 50.f);
                    }
                }
            break;
        }
    }
    if (SceneFlag == MAIN_SCENE && TheMapProcess().CharacterPolicy().ocean)
        for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
        {
            for (int i = 0; i < 4; i++)
            {
                vec3_t Position;
                Vector((float)(WorldRandom() % 128 - 64), (float)(WorldRandom() % 128 - 64),
                       (float)(WorldRandom() % 256), Position);
                vec3_t origin;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, origin);
                VectorAdd(Position, origin, Position);
                CreateParticle(BITMAP_BUBBLE, Position, o->Angle, o->Light);
            }
        }
}

void SessionGameplayUnit::HeroAttributeCalc(CHARACTER *c)
{
    if (c != Hero)
        return;

    if (CharacterAttribute->AbilityTime[0] > 0)
    {
        CharacterAttribute->AbilityTime[0] -= FPS_ANIMATION_FACTOR * REFERENCE_FPS /
                                              sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    }
    if (CharacterAttribute->AbilityTime[0] <= 0)
    {
        CharacterAttribute->Ability &= (~ABILITY_FAST_ATTACK_SPEED);
    }
    if (CharacterAttribute->AbilityTime[1] > 0)
    {
        CharacterAttribute->AbilityTime[1] -= FPS_ANIMATION_FACTOR * REFERENCE_FPS /
                                              sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    }
    if (CharacterAttribute->AbilityTime[1] <= 0)
    {
        CharacterAttribute->Ability &= (~ABILITY_PLUS_DAMAGE);
        CharacterMachine->CalculateDamage();
        CharacterMachine->CalculateMagicDamage();
        CharacterMachine->CalculateCurseDamage();
    }
    if (CharacterAttribute->AbilityTime[2] > 0)
    {
        CharacterAttribute->AbilityTime[2] -= FPS_ANIMATION_FACTOR * REFERENCE_FPS /
                                              sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    }
    if (CharacterAttribute->AbilityTime[2] <= 0)
    {
        CharacterAttribute->Ability &= (~ABILITY_FAST_ATTACK_SPEED2);
    }
}

void SessionGameplayUnit::OnlyNpcChatProcess(CHARACTER *c, OBJECT *o)
{
    if (o->Kind == KIND_NPC && rand_fps_check(2))
    {
        switch (o->Type)
        {
        case MODEL_MERCHANT_GIRL:
            if (gMapManager.InBattleCastle() == false)
            {
                CreateChat(c->ID, I18N::Game::FeelTheUnusualForcesAroundTheFortressOfCrywolf, c);
            }
            break;
        case MODEL_ELF_WIZARD:
            CreateChat(c->ID, I18N::Game::ThePowerOfTheWolfStatue, c);
            break;
        case MODEL_MASTER:
            CreateChat(c->ID, I18N::Game::CrywolfIsAskingForYourHelpOnlyYouCanSaveThisContinent, c);
            break;
        case MODEL_PLAYER:
            if (c->MonsterIndex == MONSTER_ELF_SOLDIER)
                CreateChat(c->ID, I18N::Game::ILlBeYourStrengthForTheJourneyToBecomeAWarrior, c);
            break;
        }
    }
}

void SessionGameplayUnit::PlayerNpcStopAnimationSetting(CHARACTER *c, OBJECT *o)
{
    int action = WorldRandom() % 100;

    if (o->CurrentAction != PLAYER_STOP_MALE)
    {
        SetAction(&c->Object, PLAYER_STOP_MALE);
    }
    else
    {
        if (action < 80)
        {
            SetAction(&c->Object, PLAYER_STOP_MALE);
        }
        else if (action < 85)
        {
            SetAction(&c->Object, PLAYER_CLAP1);
        }
        else if (action < 90)
        {
            SetAction(&c->Object, PLAYER_CHEER1);
        }
        else if (action < 95)
        {
            SetAction(&c->Object, PLAYER_SEE1);
        }
        else if (action < 100)
        {
            SetAction(&c->Object, PLAYER_UNKNOWN1);
        }

        const int TextIndex = TheMapProcess().PlayerNpcText(o->CurrentAction != o->PriorAction);

        wchar_t szText[512];
        mu_swprintf(szText, I18N::Game::Lookup(TextIndex));
        CreateChat(c->ID, szText, c);
    }
}

void SessionGameplayUnit::PlayerStopAnimationSetting(CHARACTER *c, OBJECT *o)
{
    if (o->CurrentAction == PLAYER_DIE1 || o->CurrentAction == PLAYER_DIE2)
    {
        if (!c->Blood)
        {
            c->Blood = true;
            CreateBlood(o);
        }
        return;
    }

    if (o->CurrentAction < PLAYER_WALK_MALE ||
        (o->CurrentAction >= PLAYER_PROVOCATION && o->CurrentAction <= PLAYER_CHEERS) ||
        (o->CurrentAction >= PLAYER_IDLE1_DARKHORSE &&
         o->CurrentAction <= PLAYER_IDLE2_DARKHORSE) ||
        ((o->CurrentAction >= PLAYER_SKILL_THRUST &&
          o->CurrentAction <= PLAYER_RAGE_FENRIR_ATTACK_RIGHT) &&
         !(o->CurrentAction >= PLAYER_RAGE_FENRIR_RUN &&
           o->CurrentAction <= PLAYER_RAGE_FENRIR_RUN_ONE_LEFT) &&
         !(o->CurrentAction >= PLAYER_RAGE_UNI_RUN &&
           o->CurrentAction <= PLAYER_RAGE_UNI_STOP_ONE_RIGHT)) ||
        o->CurrentAction == PLAYER_STOP_RAGEFIGHTER ||
        (o->CurrentAction >= PLAYER_ATTACK_FIST && o->CurrentAction <= PLAYER_SHOCK &&
         o->CurrentAction != PLAYER_WALK_TWO_HAND_SWORD_TWO &&
         o->CurrentAction != PLAYER_RUN_TWO_HAND_SWORD_TWO && o->CurrentAction != PLAYER_FLY_RIDE &&
         o->CurrentAction != PLAYER_FLY_RIDE_WEAPON &&
         o->CurrentAction != PLAYER_SKILL_HELL_BEGIN && o->CurrentAction != PLAYER_DARKLORD_WALK &&
         o->CurrentAction != PLAYER_RUN_RIDE_HORSE &&
         (o->CurrentAction < PLAYER_FENRIR_RUN ||
          o->CurrentAction > PLAYER_FENRIR_RUN_ONE_LEFT_ELF) &&
         o->CurrentAction != PLAYER_RECOVER_SKILL))

        SetPlayerStop(c);

    if (o->CurrentAction == PLAYER_SKILL_HELL_BEGIN)
    {
        o->AnimationFrame = 0;
    }
}

void SessionGameplayUnit::EtcStopAnimationSetting(CHARACTER *c, OBJECT *o)
{
    if (o->Type == MODEL_WARCRAFT)
    {
        o->CurrentAction = 1;
    }
    else if (TheMapProcess().StopMonster(c, o))
        return;
    else if (o->Type >= MODEL_MONSTER01 && o->Type < MODEL_MONSTER_END)
    {
        if (o->CurrentAction == MONSTER01_DIE)
        {
            if (!c->Blood)
            {
                c->Blood = true;
                CreateBlood(o);
            }
            return;
        }
        else if (o->CurrentAction == MONSTER01_STOP2 || o->CurrentAction == MONSTER01_SHOCK ||
                 o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2 ||
                 o->CurrentAction == MONSTER01_ATTACK3 || o->CurrentAction == MONSTER01_ATTACK4 ||
                 o->CurrentAction == MONSTER01_ATTACK5)
        {
            SetAction(o, MONSTER01_STOP1);
        }

        if (o->CurrentAction == MONSTER01_APEAR &&
            (o->Type == MODEL_MAYA_HAND_LEFT || o->Type == MODEL_MAYA_HAND_RIGHT ||
             o->Type == MODEL_SELUPAN))
        {
            if (o->Type == MODEL_SELUPAN)
            {
                o->CurrentAction = MONSTER01_STOP1;
            }
            SetAction(o, MONSTER01_STOP1);
        }
    }
    else
    {
        switch (o->Type)
        {
        case MODEL_WEDDING_NPC:
            if ((WorldRandom() % 16 < 4) && o->SubType == 0)
            {
                SetAction(o, 1);
                o->SubType = 1;
            }
            else
            {
                SetAction(o, 0);
                o->SubType = 0;
            }
            break;
        case MODEL_SMITH:
        case MODEL_SCIENTIST:
            if (WorldRandom() % 16 < 12)
                SetAction(o, 0);
            else
                SetAction(o, WorldRandom() % 2 + 1);
            break;
        case MODEL_FACE:
        case MODEL_FACE + 1:
        case MODEL_FACE + 2:
        case MODEL_FACE + 3:
        case MODEL_FACE + 4:
        case MODEL_FACE + 5:
        case MODEL_FACE + 6:
            break;
        case MODEL_ELBELAND_SILVIA:
        case MODEL_ELBELAND_RHEA:
            if (WorldRandom() % 5 < 4 || o->CurrentAction == 1)
                SetAction(o, 0);
            else
                SetAction(o, 1);
            break;
        case MODEL_NPC_DEVIN:
            if (WorldRandom() % 5 < 4)
                SetAction(o, 0);
            else
                SetAction(o, 1);
            break;

        case MODEL_SEED_MASTER:
            if (WorldRandom() % 3 < 2 || o->CurrentAction != 0)
                SetAction(o, 0);
            else
            {
                SetAction(o, WorldRandom() % 3 + 1);
            }
            break;
        case MODEL_SEED_INVESTIGATOR:
            if (WorldRandom() % 3 < 2 || o->CurrentAction == 1)
                SetAction(o, 0);
            else
                SetAction(o, 1);
            break;
        case MODEL_LITTLESANTA:
        case MODEL_LITTLESANTA + 1:
        case MODEL_LITTLESANTA + 2:
        case MODEL_LITTLESANTA + 3:
            if (WorldRandom() % 5 < 2)
                SetAction(o, 0);
            else
                SetAction(o, WorldRandom() % 3 + 2);
            break;

        case MODEL_LITTLESANTA + 4:
        case MODEL_LITTLESANTA + 5:
        case MODEL_LITTLESANTA + 6:
        case MODEL_LITTLESANTA + 7:
            if (WorldRandom() % 5 < 2)
                SetAction(o, 1);
            else
                SetAction(o, WorldRandom() % 3 + 2);
            break;
        case MODEL_XMAS2008_SANTA_NPC:
            if (WorldRandom() % 3 < 2 || (o->CurrentAction == 1 || o->CurrentAction == 2))
            {
                SetAction(o, 0);
            }
            else
            {
                SetAction(o, WorldRandom() % 2 + 1);
            }
            break;
        case MODEL_XMAS2008_SNOWMAN_NPC:
            SetAction(o, 0);
            break;
        case MODEL_GAMBLE_NPC_MOSS:
            if (WorldRandom() % 5 < 4 || o->CurrentAction == 1)
            {
                SetAction(o, 0);
            }
            else
            {
                SetAction(o, 1);
            }
            break;
        case MODAL_GENS_NPC_DUPRIAN:
        case MODAL_GENS_NPC_BARNERT:
            if (WorldRandom() % 5 < 4)
                SetAction(o, 0);
            else
                SetAction(o, WorldRandom() % 2 + 1);
            break;
        case MODEL_UNITEDMARKETPLACE_RAUL:
        case MODEL_UNITEDMARKETPLACE_JULIA:
            if (WorldRandom() % 5 < 4)
                SetAction(o, 0);
            else
                SetAction(o, WorldRandom() % 2 + 1);
            break;
        case MODEL_UNITEDMARKETPLACE_CHRISTIN:
        case MODEL_KARUTAN_NPC_REINA:
            if (WorldRandom() % 5 < 3)
                SetAction(o, 0);
            else
                SetAction(o, WorldRandom() % 2 + 1);
            break;
        case MODEL_KARUTAN_NPC_VOLVO:
            if (WorldRandom() % 5 < 4)
                SetAction(o, 0);
            else
                SetAction(o, 1);
            break;
        case MODEL_LUCKYITEM_NPC:
            if (WorldRandom() % 5 < 4 || o->CurrentAction == 1)
            {
                SetAction(o, 0);
            }
            else
            {
                SetAction(o, 1);
            }
            break;
        default:
            SetAction(o, WorldRandom() % 2);
            break;
        }
    }
}

void SessionGameplayUnit::AnimationCharacter(CHARACTER *c, OBJECT *o, BMD *b)
{
    bool bEventNpc = false;
    if (o->Kind == KIND_NPC && TheMapProcess().CharacterPolicy().festiveNpcs &&
        o->Type == MODEL_PLAYER && (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3))
    {
        Vector(0.f, 0.f, TheMapProcess().CharacterPolicy().festiveFacing, o->Angle);

        bEventNpc = true;
    }

    OnlyNpcChatProcess(c, o);

    const float pathFrames = c->PathAnimationWorldTime == WorldTime ? c->PathAnimationFrames : 0.f;
    bool Play = CharacterAnimation(c, o, (std::max)(0.f, FPS_ANIMATION_FACTOR - pathFrames));

    if (!Play)
    {
        c->LongRangeAttack = -1;
        if (o->Type == MODEL_PLAYER)
        {
            if (bEventNpc)
            {
                PlayerNpcStopAnimationSetting(c, o);
            }
            else
            {
                PlayerStopAnimationSetting(c, o);
            }
        }
        else
        {
            EtcStopAnimationSetting(c, o);
        }
        if (o->CurrentAction == MONSTER01_STOP1 || o->CurrentAction == MONSTER01_STOP2)
            PlayMonsterSound(o);

        if (o->Type == MODEL_WARCRAFT)
        {
            o->AnimationFrame = 8.f;
        }
    }

    switch (o->Type)
    {
    case MODEL_DEVIAS_TRADER:
        if (b->CurrentAnimationFrame == b->Actions[o->CurrentAction].NumAnimationKeys - 1)
        {
            if (rand_fps_check(32))
                SetAction(o, 1);
            else
                SetAction(o, 0);
        }
        break;
    case MODEL_RABBIT:
        if (o->CurrentAction <= 1 &&
            b->CurrentAnimationFrame == b->Actions[o->CurrentAction].NumAnimationKeys - 1)
        {
            if (rand_fps_check(10))
                SetAction(o, 1);
            else
                SetAction(o, 0);
        }
        break;
    }
}

void SessionGameplayUnit::MoveCharacter(CHARACTER *c, OBJECT *o)
{
    if (o->Type == MODEL_WARCRAFT)
    {
        wchar_t Text[100];
        wchar_t ID[100];
        mu_swprintf(ID, L"%ls .", c->ID);
        mu_swprintf(Text, I18N::Game::DToKalima, c->Level);
        wcscat(ID, Text);
        AddObjectDescription(ID, o->Position);
    }

    BMD *b = &Models[o->Type];
    VectorCopy(o->Position, b->BodyOrigin);
    b->BodyScale = o->Scale;
    b->CurrentAction = o->CurrentAction;

    TheMapProcess().AdvanceCharacterStopTime();
    HeroAttributeCalc(c);
    PushingCharacter(c, o);
    DeadCharacter(c, o, b);
    Alpha(o, FPS_ANIMATION_FACTOR);

    if (c->Freeze > 0.f)
    {
        c->Freeze -= (0.03f) * FPS_ANIMATION_FACTOR;
    }

    AnimationCharacter(c, o, b);

    if (c->Dead > 0)
    {
        c->Dead += FPS_ANIMATION_FACTOR;
        if (c->Dead >= 15)
        {
            SetPlayerDie(c);
        }
        if (gMapManager.InBloodCastle() && o->m_bActionStart)
        {
            SetPlayerDie(c);
        }
    }

    vec3_t p, Position;
    vec3_t Light;
    float Luminosity = (float)(WorldRandom() % 6 + 2) * 0.1f;

    Vector(0.f, 0.f, 0.f, p);
    Vector(1.f, 1.f, 1.f, Light);

    if (gMapManager.InBattleCastle() == false && o->m_byHurtByDeathstab > 0)
    {
        const float frames = (std::min)(FPS_ANIMATION_FACTOR, o->m_byHurtByDeathstab);
        for (auto birth : Emissions(frames / 2.f, frames))
        {
            ObjectDrawInput draw(o);
            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, draw.position);
            AnimationPoseSample pose(draw, b->BoneHead, b->BodyHeight, false,
                                     b->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            draw.bones =
                pose.EvaluateAtTime(*b, *o, WorldTime, birth.FrameFraction(), bones.data());
            vec3_t pos1, pos2;

            Vector(0.f, 0.f, 0.f, p);
            for (int i = 0; i < b->NumBones; ++i)
            {
                if (!b->Bones[i].Dummy)
                {
                    int iParent = b->Bones[i].Parent;
                    if (iParent > -1 && iParent < b->NumBones)
                    {
                        b->TransformByObjectBone(pos1, draw, i, p);
                        b->TransformByObjectBone(pos2, draw, iParent, p);

                        GetNearRandomPos(pos1, 20, pos1);
                        GetNearRandomPos(pos2, 20, pos2);
                        CreateJoint(BITMAP_JOINT_THUNDER, pos1, pos2, o->Angle, 7, NULL, 20.f);
                    }
                }
            }
        }
        o->m_byHurtByDeathstab -= FPS_ANIMATION_FACTOR;
    }

    if ((o->CurrentAction == PLAYER_ATTACK_TELEPORT ||
         o->CurrentAction == PLAYER_ATTACK_RIDE_TELEPORT ||
         o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_TELEPORT) &&
        o->AnimationFrame > 5.5f)
        for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
        {
            Vector(0.f, 0.f, 0.f, p);
            Vector(0.3f, 0.5f, 1.f, Light);
            AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
            pose.SampleBonePosition(*b, *o, 42, p, WorldTime, birth.FrameFraction(), Position);
            CreateParticle(BITMAP_LIGHT, Position, o->Angle, Light);
        }

    if ((int)c->AttackTime > 0)
    {
        AdvanceAttackEffects(c, o);
    }

    if ((int)c->AttackTime >= g_iLimitAttackTime)
    {
        c->AttackTime = 0;
        c->LastAttackEffectTime = -1;
        o->PKKey = getTargetCharacterKey(c, SelectedCharacter);

        switch ((c->Skill))
        {
        case AT_SKILL_SUMMON:
        case AT_SKILL_SUMMON + 1:
        case AT_SKILL_SUMMON + 2:
        case AT_SKILL_SUMMON + 3:
        case AT_SKILL_SUMMON + 4:
        case AT_SKILL_SUMMON + 5:
        case AT_SKILL_SUMMON + 6:
#ifdef ADD_ELF_SUMMON
        case AT_SKILL_SUMMON + 7:
#endif // ADD_ELF_SUMMON
            CreateEffect(BITMAP_MAGIC + 1, o->Position, o->Angle, o->Light, 3, o);
            break;
        case AT_SKILL_RAGEFUL_BLOW:
        case AT_SKILL_RAGEFUL_BLOW_STR:
        case AT_SKILL_RAGEFUL_BLOW_MASTERY: {
            o->Weapon = c->Weapon[0].Type - MODEL_SWORD;
            o->WeaponLevel = (BYTE)c->Weapon[0].Level;
            CreateEffect(MODEL_SKILL_FURY_STRIKE, o->Position, o->Angle, o->Light, 0, o, o->PKKey,
                         FindHotKey((c->Skill)));
            PlayBuffer(SOUND_FURY_STRIKE1);
            break;
        }
        case AT_SKILL_STRIKE_OF_DESTRUCTION:
        case AT_SKILL_STRIKE_OF_DESTRUCTION_STR:
            o->Weapon = c->Weapon[0].Type - MODEL_SWORD;
            o->WeaponLevel = (BYTE)c->Weapon[0].Level;
            Vector(0.f, 0.f, 0.f, o->Light);
            o->Light[0] = (float)(c->SkillX + 0.5f) * TERRAIN_SCALE;
            o->Light[1] = (float)(c->SkillY + 0.5f) * TERRAIN_SCALE;
            o->Light[2] = o->Position[2];
            CreateEffect(MODEL_BLOW_OF_DESTRUCTION, o->Position, o->Angle, o->Light, 0, o);
            PlayBuffer(SOUND_SKILL_BLOWOFDESTRUCTION);
            break;
        case AT_SKILL_FIRE_SLASH:
        case AT_SKILL_FIRE_SLASH_STR:
            o->Weapon = c->Weapon[0].Type - MODEL_SWORD;
            o->WeaponLevel = (BYTE)c->Weapon[0].Level;
            break;
        case AT_SKILL_POWER_SLASH:
        case AT_SKILL_POWER_SLASH_STR:
            o->Weapon = c->Weapon[0].Type - MODEL_SWORD;
            o->WeaponLevel = (BYTE)c->Weapon[0].Level;
            break;
        case AT_SKILL_SWELL_LIFE:
        case AT_SKILL_SWELL_LIFE_STR:
        case AT_SKILL_SWELL_LIFE_PROFICIENCY: {
            vec3_t Angle = {0.0f, 0.0f, 0.0f};
            int iCount = 36;
            for (int i = 0; i < iCount; ++i)
            {
                Angle[0] = -10.f;
                Angle[1] = 0.f;
                Angle[2] = i * 10.f;
                vec3_t Position;
                VectorCopy(o->Position, Position);
                Position[2] += 100.f;
                CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, Angle, 2, o, 60.f, 0, 0);

                if ((i % 20) == 0)
                {
                    CreateEffect(BITMAP_MAGIC + 1, o->Position, Angle, o->Light, 4, o);
                }
            }
        }
            PlayBuffer(SOUND_SWELLLIFE);
            break;
        case AT_SKILL_STUN:
            //            CreateEffect ( MODEL_STUN_STONE, o->Position, o->Angle, o->Light, 1 );
            CreateJoint(BITMAP_FLASH, o->Position, o->Position, o->Angle, 7, NULL);

            PlayBuffer(SOUND_BMS_STUN);
            break;

        case AT_SKILL_REMOVAL_STUN: {
            if (c->TargetCharacter != -1)
            {
                CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                OBJECT *to = &tc->Object;
                if (to != o)
                {
                    VectorCopy(to->Position, Position);
                    Position[2] += 1200.f;
                    CreateJoint(BITMAP_FLASH, Position, Position, to->Angle, 0, to, 120.f);

                    PlayBuffer(SOUND_BMS_STUN_REMOVAL);
                }
            }
        }
        break;

        case AT_SKILL_MANA: {
            vec3_t Angle = {0.0f, 0.0f, 0.0f};
            int iCount = 36;

            for (int i = 0; i < iCount; ++i)
            {
                Angle[0] = -10.f;
                Angle[1] = 0.f;
                Angle[2] = i * 10.f;
                vec3_t Position;
                VectorCopy(o->Position, Position);
                Position[2] += 100.f;
                CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, Angle, 21, o, 60.f, 0, 0);
                if ((i % 20) == 0)
                    CreateEffect(BITMAP_MAGIC + 1, o->Position, Angle, Light, 10, o);
            }
        }
            PlayBuffer(SOUND_BMS_MANA);
            break;

        case AT_SKILL_INVISIBLE: {
            if (c->TargetCharacter != -1)
            {
                CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                OBJECT *to = &tc->Object;

                DeleteJoint(MODEL_SPEARSKILL, to, 4);
                DeleteJoint(MODEL_SPEARSKILL, to, 9);
                //				if(to != o)
                //				{
                CreateEffect(BITMAP_MAGIC + 1, to->Position, to->Angle, to->Light, 6, to);
                PlayBuffer(SOUND_BMS_INVISIBLE);
                //				}
            }
        }
        break;
        case AT_SKILL_REMOVAL_INVISIBLE: {
            if (c->TargetCharacter != -1)
            {
                CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                OBJECT *to = &tc->Object;

                if (to != o)
                {
                    VectorCopy(to->Position, Position);
                    Position[2] += 1200.f;
                    CreateJoint(BITMAP_FLASH, Position, Position, to->Angle, 1, to, 120.f);

                    PlayBuffer(SOUND_BMS_STUN_REMOVAL);
                }
            }
        }
        break;
        case AT_SKILL_REMOVAL_BUFF: {
            vec3_t Angle;
            vec3_t Position;
            VectorCopy(o->Position, Position);
            Position[2] += 100.f;

            std::list<eBuffState> bufflist;

            //debuff
            bufflist.push_back(eDeBuff_Poison);
            bufflist.push_back(eDeBuff_Freeze);
            bufflist.push_back(eDeBuff_Harden);
            bufflist.push_back(eDeBuff_Defense);
            bufflist.push_back(eDeBuff_Stun);
            bufflist.push_back(eDeBuff_Sleep);
            bufflist.push_back(eDeBuff_BlowOfDestruction);

            //buff
            bufflist.push_back(eBuff_Life);
            bufflist.push_back(eBuff_Attack);
            bufflist.push_back(eBuff_Defense);
            bufflist.push_back(eBuff_AddAG);
            bufflist.push_back(eBuff_Cloaking);
            bufflist.push_back(eBuff_AddSkill);
            bufflist.push_back(eBuff_WizDefense);
            bufflist.push_back(eBuff_AddCriticalDamage);
            bufflist.push_back(eBuff_CrywolfAltarOccufied);

            g_CharacterUnRegisterBuffList(o, bufflist);

            Vector(0.f, 0.f, 45.f, Angle);
            CreateJoint(MODEL_SPEARSKILL, Position, Position, Angle, 5, o, 170.0f);
            Position[2] -= 10.f;
            Vector(0.f, 0.f, 135.f, Angle);
            CreateJoint(MODEL_SPEARSKILL, Position, Position, Angle, 6, o, 170.0f);
            Position[2] -= 10.f;
            Vector(0.f, 0.f, 225.f, Angle);
            CreateJoint(MODEL_SPEARSKILL, Position, Position, Angle, 7, o, 170.0f);

            Vector(0.f, 0.f, 90.f, Angle);
            CreateJoint(MODEL_SPEARSKILL, Position, Position, Angle, 5, o, 170.0f);
            Position[2] -= 10.f;
            Vector(0.f, 0.f, 180.f, Angle);
            CreateJoint(MODEL_SPEARSKILL, Position, Position, Angle, 6, o, 170.0f);
            Position[2] -= 10.f;
            Vector(0.f, 0.f, 270.f, Angle);
            CreateJoint(MODEL_SPEARSKILL, Position, Position, Angle, 7, o, 170.0f);
        }

            PlayBuffer(SOUND_BMS_MAGIC_REMOVAL);
            break;
        case AT_SKILL_IMPROVE_AG: {
            vec3_t Angle = {-45.f, 0.f, 45.f};
            vec3_t Light = {1.f, 1.f, 1.f};
            vec3_t Position;

            Position[0] = o->Position[0] + sinf(45 * 0.1f) * 80.f;
            Position[1] = o->Position[1] + cosf(45 * 0.1f) * 80.f;
            Position[2] = o->Position[2] + 300;
            CreateJoint(BITMAP_JOINT_HEALING, Position, Position, Angle, 10, o, 15.f);
            Angle[2] = 405.f;
            CreateJoint(BITMAP_JOINT_HEALING, Position, Position, Angle, 10, o, 15.f);

            Angle[2] = 225.f;
            Position[0] = o->Position[0] + cosf(225 * 0.1f) * 80.f;
            Position[1] = o->Position[1] + sinf(225 * 0.1f) * 80.f;
            Position[2] = o->Position[2] + 300;
            CreateJoint(BITMAP_JOINT_HEALING, Position, Position, Angle, 10, o, 15.f);
            Angle[2] = 450.f;
            CreateJoint(BITMAP_JOINT_HEALING, Position, Position, Angle, 10, o, 15.f);
        }
            PlayBuffer(SOUND_SWELLLIFE);
            break;
        case AT_SKILL_ADD_CRITICAL:
        case AT_SKILL_ADD_CRITICAL_STR1:
        case AT_SKILL_ADD_CRITICAL_STR2:
        case AT_SKILL_ADD_CRITICAL_STR3:
            Vector(0.f, 0.f, 0.f, p);
            Vector(1.f, 0.6f, 0.3f, Light);
            if (c->Weapon[0].Type != MODEL_ARROWS)
            {
                b->TransformPosition(o->BoneTransform[c->Weapon[0].LinkBone], p, Position, true);
                CreateEffect(MODEL_DARKLORD_SKILL, Position, o->Angle, Light, 0);
            }
            if (c->Weapon[1].Type != MODEL_BOLT &&
                (c->Weapon[1].Type < MODEL_SHIELD ||
                 c->Weapon[1].Type >= MODEL_SHIELD + MAX_ITEM_INDEX))
            {
                b->TransformPosition(o->BoneTransform[c->Weapon[1].LinkBone], p, Position, true);
                CreateEffect(MODEL_DARKLORD_SKILL, Position, o->Angle, Light, 1);
            }
            PlayBuffer(SOUND_CRITICAL, o);
            break;
        case AT_SKILL_PARTY_TELEPORT:
            CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, o->Light, 2, o);
            CreateEffect(MODEL_CIRCLE_LIGHT, o->Position, o->Angle, o->Light, 3);

            PlayBuffer(SOUND_PART_TELEPORT, o);
            break;
        case AT_SKILL_THUNDER_STRIKE:
            CalcAddPosition(o, 0.f, -90.f, -50.f, Position);
            if (o->CurrentAction == PLAYER_ATTACK_RIDE_ATTACK_FLASH)
            {
                Position[2] += 80.f;
            }
            else if (o->CurrentAction == PLAYER_FENRIR_ATTACK_DARKLORD_FLASH)
            {
                Position[2] += 40.f;
            }

            CreateEffect(BITMAP_FLARE_FORCE, Position, o->Angle, o->Light, 0, o);

            PlayBuffer(SOUND_ELEC_STRIKE, o);
            break;
        case AT_SKILL_RIDER:
            CreateEffect(BITMAP_SHOTGUN, o->Position, o->Angle, o->Light, 0, o, o->PKKey,
                         CurrentSkill);

            PlayBuffer(SOUND_SKILL_SWORD3);
            break;
        case AT_SKILL_TWISTING_SLASH:
        case AT_SKILL_TWISTING_SLASH_STR:
        case AT_SKILL_TWISTING_SLASH_STR_MG:
        case AT_SKILL_TWISTING_SLASH_MASTERY:
            o->Weapon = c->Weapon[0].Type - MODEL_SWORD;
            o->WeaponLevel = (BYTE)c->Weapon[0].Level;
            CreateEffect(MODEL_SKILL_WHEEL1, o->Position, o->Angle, o->Light, 0, o, o->PKKey,
                         FindHotKey((c->Skill)));

            if (SceneFlag != LOG_IN_SCENE)
                PlayBuffer(SOUND_SKILL_SWORD4);

            c->PostMoveProcess_Active(g_iLimitAttackTime);
            break;
        case AT_SKILL_HELL_FIRE:
        case AT_SKILL_HELL_FIRE_STR:
            CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, o->Light, 0, o, o->PKKey,
                         FindHotKey((c->Skill)));
            CreateEffect(MODEL_CIRCLE_LIGHT, o->Position, o->Angle, o->Light);

            if (gMapManager.InHellas() == true)
            {
                AddWaterWave((c->PositionX), (c->PositionY), 2, -1500);
            }

            PlayBuffer(SOUND_HELLFIRE);
            break;
        case AT_SKILL_NOVA:
            CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, o->Light, 1, o);
            StopBuffer(SOUND_NUKE1, true);
            PlayBuffer(SOUND_NUKE2);
            break;
        case AT_SKILL_DECAY:
        case AT_SKILL_DECAY_STR:
            Position[0] = (float)(c->SkillX + 0.5f) * TERRAIN_SCALE;
            Position[1] = (float)(c->SkillY + 0.5f) * TERRAIN_SCALE;
            Position[2] = RequestTerrainHeight(Position[0], Position[1]);

            Vector(0.8f, 0.5f, 0.1f, Light);
            CreateEffect(MODEL_FIRE, Position, o->Angle, Light, 6, NULL, 0);
            CreateEffect(MODEL_FIRE, Position, o->Angle, Light, 6, NULL, 0);
            PlayBuffer(SOUND_DEATH_POISON1);

            if (c == Hero)
            {
                ++CharacterMachine->PacketSerial;
            }
            break;
        case AT_SKILL_ICE_STORM: {
            vec3_t TargetPosition, Pos;
            TargetPosition[0] = (float)(c->SkillX + 0.5f) * TERRAIN_SCALE;
            TargetPosition[1] = (float)(c->SkillY + 0.5f) * TERRAIN_SCALE;
            TargetPosition[2] = RequestTerrainHeight(TargetPosition[0], TargetPosition[1]);

            for (int i = 0; i < 10; ++i)
            {
                Pos[0] = TargetPosition[0];
                Pos[1] = TargetPosition[1];
                Pos[2] = TargetPosition[2] + (WorldRandom() % 50) * i;
                CreateEffect(MODEL_BLIZZARD, Pos, o->Angle, Light, 0, NULL, i + 1);
            }
            if (c == Hero)
            {
                ++CharacterMachine->PacketSerial;
            }

            PlayBuffer(SOUND_SUDDEN_ICE1);
        }
        break;

        case AT_SKILL_FLAME:
        case AT_SKILL_FLAME_STR:
        case AT_SKILL_FLAME_STR_MG:
            Position[0] = (float)(c->SkillX + 0.5f) * TERRAIN_SCALE;
            Position[1] = (float)(c->SkillY + 0.5f) * TERRAIN_SCALE;
            Position[2] = RequestTerrainHeight(Position[0], Position[1]);
            CreateEffect(BITMAP_FLAME, Position, o->Angle, o->Light, 0, o, o->PKKey,
                         FindHotKey(AT_SKILL_FLAME));

            if (c == Hero)
            {
                ++CharacterMachine->PacketSerial;
            }
            if (SceneFlag != LOG_IN_SCENE)
                PlayBuffer(SOUND_FLAME);
            break;
        case AT_SKILL_STORM:
            CreateEffect(MODEL_STORM, o->Position, o->Angle, Light, 0, o, o->PKKey,
                         FindHotKey(AT_SKILL_STORM));
            PlayBuffer(SOUND_STORM);
            break;

        case AT_SKILL_FIRE_SCREAM:
        case AT_SKILL_FIRE_SCREAM_STR: {
            int SkillIndex = FindHotKey((c->Skill));
            OBJECT *pObj = o;
            vec3_t ap, P, dp;
            float BkO = pObj->Angle[2];

            VectorCopy(pObj->Position, ap);
            CreateEffect(MODEL_DARK_SCREAM, pObj->Position, pObj->Angle, pObj->Light, 0, pObj,
                         pObj->PKKey, SkillIndex);
            CreateEffect(MODEL_DARK_SCREAM_FIRE, pObj->Position, pObj->Angle, pObj->Light, 0, pObj,
                         pObj->PKKey, SkillIndex);

            Vector(80.f, 0.f, 0.f, P);

            pObj->Angle[2] += 10.f;

            AngleMatrix(pObj->Angle, pObj->Matrix);
            VectorRotate(P, pObj->Matrix, dp);
            VectorAdd(dp, pObj->Position, pObj->Position);
            CreateEffect(MODEL_DARK_SCREAM, pObj->Position, pObj->Angle, pObj->Light, 0, pObj,
                         pObj->PKKey, SkillIndex);
            CreateEffect(MODEL_DARK_SCREAM_FIRE, pObj->Position, pObj->Angle, pObj->Light, 0, pObj,
                         pObj->PKKey, SkillIndex);

            VectorCopy(ap, pObj->Position);
            VectorCopy(pObj->Position, ap);
            Vector(-80.f, 0.f, 0.f, P);
            pObj->Angle[2] -= 20.f;

            AngleMatrix(pObj->Angle, pObj->Matrix);
            VectorRotate(P, pObj->Matrix, dp);
            VectorAdd(dp, pObj->Position, pObj->Position);
            CreateEffect(MODEL_DARK_SCREAM, pObj->Position, pObj->Angle, pObj->Light, 0, pObj,
                         pObj->PKKey, SkillIndex);
            CreateEffect(MODEL_DARK_SCREAM_FIRE, pObj->Position, pObj->Angle, pObj->Light, 0, pObj,
                         pObj->PKKey, SkillIndex);
            VectorCopy(ap, pObj->Position);
            pObj->Angle[2] = BkO;

            if ((c->Helper.Type >= MODEL_HORN_OF_UNIRIA &&
                 c->Helper.Type <= MODEL_DARK_HORSE_ITEM) &&
                !c->SafeZone)
            {
                SetAction(o, PLAYER_ATTACK_RIDE_STRIKE);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
            {
                SetAction(o, PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE);
            }
            else
            {
                SetAction(o, PLAYER_ATTACK_STRIKE);
            }

            PlayBuffer(SOUND_FIRE_SCREAM);
        }
        break;
        case AT_SKILL_FLASH:
            CalcAddPosition(o, -20.f, -90.f, 100.f, Position);
            CreateEffect(BITMAP_BOSS_LASER, Position, o->Angle, Light, 0, o, o->PKKey,
                         FindHotKey(AT_SKILL_FLASH));
            PlayBuffer(SOUND_FLASH);
            break;

        case AT_SKILL_BLAST:
        case AT_SKILL_BLAST_STR:
        case AT_SKILL_BLAST_STR_MG:
            Position[0] = (float)(c->SkillX + 0.5f) * TERRAIN_SCALE;
            Position[1] = (float)(c->SkillY + 0.5f) * TERRAIN_SCALE;
            Position[2] = RequestTerrainHeight(Position[0], Position[1]);
            {
                int SkillIndex = FindHotKey((c->Skill));
                CreateEffect(MODEL_SKILL_BLAST, Position, o->Angle, o->Light, 0, o, o->PKKey,
                             SkillIndex);
                CreateEffect(MODEL_SKILL_BLAST, Position, o->Angle, o->Light, 0, o, o->PKKey,
                             SkillIndex);
            }

            if (c == Hero)
            {
                ++CharacterMachine->PacketSerial;
            }
            break;
        case AT_SKILL_INFERNO:
        case AT_SKILL_INFERNO_STR:
        case AT_SKILL_INFERNO_STR_MG:
            CreateInferno(o->Position);
            CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light, 0, o, o->PKKey,
                         FindHotKey(c->Skill));

            if (c == Hero)
            {
                ++CharacterMachine->PacketSerial;
            }
            break;
        case AT_SKILL_EVIL_SPIRIT:
        case AT_SKILL_EVIL_SPIRIT_STR:
        case AT_SKILL_EVIL_SPIRIT_STR_MG:
            vec3_t Position;
            VectorCopy(o->Position, Position);
            Position[2] += 100.f;

            for (int i = 0; i < 4; i++)
            {
                vec3_t Angle;
                Vector(0.f, 0.f, i * 90.f, Angle);

                int SkillIndex = FindHotKey((c->Skill));
                CreateJoint(BITMAP_JOINT_SPIRIT, Position, o->Position, Angle, 0, o, 80.f, o->PKKey,
                            SkillIndex, o->m_bySkillSerialNum);
                CreateJoint(BITMAP_JOINT_SPIRIT, Position, o->Position, Angle, 0, o, 20.f);
            }
            if (c == Hero)
            {
                ++CharacterMachine->PacketSerial;
            }
            PlayBuffer(SOUND_EVIL);
            break;
        case AT_SKILL_PLASMA_STORM_FENRIR: {
            PlayBuffer(SOUND_FENRIR_SKILL);
            CHARACTER *p_temp_c;
            OBJECT *p_o[MAX_FENRIR_SKILL_MONSTER_NUM];
            int iMonsterNum = 0;

            for (int i = 0; i < CharactersClient.Size(); ++i)
            {
                if (!CharactersClient.IsValidIndex(i))
                    continue;
                p_temp_c = &CharactersClient[i];

                if (p_temp_c->Object.Live == TRUE && p_temp_c->Object.Kind == KIND_MONSTER &&
                    p_temp_c->Object.CurrentAction != MONSTER01_DIE)
                {
                    float dx = c->Object.Position[0] - p_temp_c->Object.Position[0];
                    float dy = c->Object.Position[1] - p_temp_c->Object.Position[1];
                    float fDistance = sqrtf(dx * dx + dy * dy) / TERRAIN_SCALE;
                    if (fDistance <= gSkillManager.GetSkillDistance(AT_SKILL_PLASMA_STORM_FENRIR))
                    {
                        p_o[iMonsterNum] = &p_temp_c->Object;
                        iMonsterNum++;
                    }
                }
                else if (p_temp_c->Object.Live == TRUE && p_temp_c->Object.Kind == KIND_PLAYER &&
                         p_temp_c->Object.CurrentAction != PLAYER_DIE1)
                {
                    if (CheckAttack_Fenrir(p_temp_c) == true && IsKeyDown(VK_LCONTROL))
                    {
                        float dx = c->Object.Position[0] - p_temp_c->Object.Position[0];
                        float dy = c->Object.Position[1] - p_temp_c->Object.Position[1];
                        float fDistance = sqrtf(dx * dx + dy * dy) / TERRAIN_SCALE;
                        if (fDistance <=
                            gSkillManager.GetSkillDistance(AT_SKILL_PLASMA_STORM_FENRIR))
                        {
                            p_o[iMonsterNum] = &p_temp_c->Object;
                            iMonsterNum++;
                        }
                    }
                }

                if (iMonsterNum >= 10)
                    break;
            }

            vec3_t vAngle;
            int iAngle = WorldRandom() % 360;

            if (CharactersClient.IsValidIndex(c->m_iFenrirSkillTarget))
            {
                CHARACTER *p_tc = &CharactersClient[c->m_iFenrirSkillTarget];
                OBJECT *p_to = &p_tc->Object;
                auto fenrirType = GetFenrirType(c);

                for (int j = 0; j < 2; j++)
                {
                    CalcAddPosition(o, 0.f, -140.f, 130.f, Position);
                    Vector((float)(WorldRandom() % 360), 0.0f, (float)(WorldRandom() % 360),
                           vAngle);

                    CreateJoint(MODEL_FENRIR_SKILL_THUNDER, Position, p_to->Position, vAngle,
                                0 + fenrirType, p_to, 100.f);
                    CreateJoint(MODEL_FENRIR_SKILL_THUNDER, Position, p_to->Position, vAngle,
                                3 + fenrirType, p_to, 80.f);
                }

                for (int i = 0; i < iMonsterNum; i++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        CalcAddPosition(o, 0.f, -140.f, 130.f, Position);
                        Vector((float)(WorldRandom() % 360), 0.0f, (float)(WorldRandom() % 360),
                               vAngle);

                        CreateJoint(MODEL_FENRIR_SKILL_THUNDER, Position, p_o[i]->Position, vAngle,
                                    0 + fenrirType, p_o[i], 100.f);
                        CreateJoint(MODEL_FENRIR_SKILL_THUNDER, Position, p_o[i]->Position, vAngle,
                                    4 + fenrirType, p_o[i], 80.f);
                    }
                }

                for (int k = 0; k < 6; k++)
                {
                    CalcAddPosition(o, 0.f, 10.f + (WorldRandom() % 40 - 20), 130.f, Position);
                    Vector((float)(WorldRandom() % 360), 0.0f, (float)(WorldRandom() % 360),
                           vAngle);
                    CreateJoint(BITMAP_FLARE_FORCE, Position, Position, vAngle, 11 + fenrirType,
                                NULL, 60.f);
                }
            }
            break;
        }
        case AT_SKILL_RUSH:
            CreateEffect(MODEL_SWORD_FORCE, o->Position, o->Angle, Light, 0, o);
            PlayBuffer(SOUND_BCS_RUSH);
            break;
        case AT_SKILL_OCCUPY: {
            CreateEffect(MODEL_SWORD_FORCE, o->Position, o->Angle, Light, 2, o);
            PlayBuffer(SOUND_BCS_RUSH);
        }
        break;
        case AT_SKILL_SPIRAL_SLASH:
            break;

        case AT_SKILL_BRAND_OF_SKILL:
            Vector(0.f, 0.f, 0.f, p);
            Vector(1.f, 1.f, 1.f, Light);
            if (c->Weapon[0].Type != MODEL_ARROWS)
            {
                b->TransformPosition(o->BoneTransform[c->Weapon[0].LinkBone], p, Position, true);
                CreateEffect(MODEL_DARKLORD_SKILL, Position, o->Angle, Light, 0);
            }
            if (c->Weapon[1].Type != MODEL_BOLT &&
                (c->Weapon[1].Type < MODEL_SHIELD ||
                 c->Weapon[1].Type >= MODEL_SHIELD + MAX_ITEM_INDEX))
            {
                b->TransformPosition(o->BoneTransform[c->Weapon[1].LinkBone], p, Position, true);
                CreateEffect(MODEL_DARKLORD_SKILL, Position, o->Angle, Light, 1);
            }
            CreateEffect(MODEL_MANA_RUNE, o->Position, o->Angle, o->Light);

            PlayBuffer(SOUND_BCS_BRAND_OF_SKILL);
            break;
        case AT_SKILL_ALICE_BERSERKER:
        case AT_SKILL_ALICE_BERSERKER_STR:
            Vector(1.0f, 0.1f, 0.2f, Light);
            CreateEffect(BITMAP_MAGIC + 1, o->Position, o->Angle, Light, 11, o);
            CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT, o->Position, o->Angle, Light, 0, o);
            CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT2, o->Position, o->Angle, Light, 0, o);
            PlayBuffer(SOUND_SKILL_BERSERKER);
            break;
        case AT_SKILL_ALICE_WEAKNESS:
            Vector(2.0f, 0.1f, 0.1f, Light);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 1, NULL, -1, 0, 0, 0,
                         7.0f);
            Vector(2.0f, 0.4f, 0.3f, Light);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 0, NULL, -1, 0, 0, 0,
                         2.0f);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 2, NULL, -1, 0, 0, 0,
                         1.0f);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 2, NULL, -1, 0, 0, 0,
                         0.2f);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 2, NULL, -1, 0, 0, 0,
                         0.1f);
            CreateEffect(MODEL_SUMMONER_CASTING_EFFECT2, o->Position, o->Angle, Light, 1, NULL, -1,
                         0, 0, 0, 0.6f);
            CreateEffect(MODEL_SUMMONER_CASTING_EFFECT22, o->Position, o->Angle, Light, 1, NULL, -1,
                         0, 0, 0, 0.6f);
            CreateEffect(MODEL_SUMMONER_CASTING_EFFECT222, o->Position, o->Angle, Light, 1, NULL,
                         -1, 0, 0, 0, 0.6f);
            Vector(1.4f, 0.2f, 0.2f, Light);
            CreateEffect(BITMAP_SHINY + 6, o->Position, o->Angle, Light, 0, NULL, -1, 0, 0, 0,
                         0.5f);
            CreateEffect(BITMAP_PIN_LIGHT, o->Position, o->Angle, Light, 0, NULL, -1, 0, 0, 0, 1.f);
            PlayBuffer(SOUND_SKILL_WEAKNESS);
            break;
        case AT_SKILL_ALICE_ENERVATION:
            Vector(0.25f, 1.0f, 0.7f, Light);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 1, NULL, -1, 0, 0, 0,
                         7.0f);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 0, NULL, -1, 0, 0, 0,
                         2.0f);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 2, NULL, -1, 0, 0, 0,
                         1.0f);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 2, NULL, -1, 0, 0, 0,
                         0.2f);
            CreateEffect(BITMAP_MAGIC_ZIN, o->Position, o->Angle, Light, 2, NULL, -1, 0, 0, 0,
                         0.1f);
            CreateEffect(MODEL_SUMMONER_CASTING_EFFECT2, o->Position, o->Angle, Light, 1, NULL, -1,
                         0, 0, 0, 0.6f);
            CreateEffect(MODEL_SUMMONER_CASTING_EFFECT22, o->Position, o->Angle, Light, 1, NULL, -1,
                         0, 0, 0, 0.6f);
            CreateEffect(MODEL_SUMMONER_CASTING_EFFECT222, o->Position, o->Angle, Light, 1, NULL,
                         -1, 0, 0, 0, 0.6f);
            CreateEffect(BITMAP_SHINY + 6, o->Position, o->Angle, Light, 0, NULL, -1, 0, 0, 0,
                         0.5f);
            CreateEffect(BITMAP_PIN_LIGHT, o->Position, o->Angle, Light, 0, NULL, -1, 0, 0, 0, 1.f);
            PlayBuffer(SOUND_SKILL_ENERVATION);
            break;
        case AT_SKILL_FLAME_STRIKE: {
            //DeleteEffect(MODEL_EFFECT_FLAME_STRIKE, o, 0);
            CreateEffect(MODEL_EFFECT_FLAME_STRIKE, o->Position, o->Angle, o->Light, 0, o);
            PlayBuffer(SOUND_SKILL_FLAME_STRIKE);
        }
        break;
        case AT_SKILL_GIGANTIC_STORM: {
            vec34_t Matrix;
            vec3_t vAngle, vDirection, vPosition;
            float fAngle;
            for (int i = 0; i < 5; ++i)
            {
                Vector(0.f, 200.f, 0.f, vDirection);
                fAngle = o->Angle[2] + i * 72.f;
                Vector(0.f, 0.f, fAngle, vAngle);
                AngleMatrix(vAngle, Matrix);
                VectorRotate(vDirection, Matrix, vPosition);
                VectorAdd(vPosition, o->Position, vPosition);

                CreateEffect(BITMAP_JOINT_THUNDER, vPosition, o->Angle, o->Light);
            }
            PlayBuffer(SOUND_SKILL_GIGANTIC_STORM);
        }
        break;
        case AT_SKILL_LIGHTNING_SHOCK:
        case AT_SKILL_LIGHTNING_SHOCK_STR: {
            // 				CHARACTER *tc = &CharactersClient[c->TargetCharacter];
            // 				OBJECT *to = &tc->Object;
            // 				CreateEffect(MODEL_LIGHTNING_SHOCK, to->Position, to->Angle, to->Light, 2, to);
            vec3_t vLight;
            Vector(1.0f, 1.0f, 1.0f, vLight);

            CreateEffect(MODEL_LIGHTNING_SHOCK, o->Position, o->Angle, vLight, 0, o);
            PlayBuffer(SOUND_SKILL_LIGHTNING_SHOCK);
        }
        break;
        case AT_SKILL_KILLING_BLOW:
        case AT_SKILL_KILLING_BLOW_STR:
        case AT_SKILL_KILLING_BLOW_MASTERY: {
            o->Angle[2] = CreateAngle2D(o->Position, c->TargetPosition);
            o->m_sTargetIndex = c->TargetCharacter;
        }
        break;
        case AT_SKILL_BEAST_UPPERCUT:
        case AT_SKILL_BEAST_UPPERCUT_STR:
        case AT_SKILL_BEAST_UPPERCUT_MASTERY: {
            o->m_sTargetIndex = c->TargetCharacter;
        }
        break;
        }

        if (c->TargetCharacter == -1)
        {
            BYTE Skill = 0;
            if (c->Skill == AT_SKILL_TRIPLE_SHOT || c->Skill == AT_SKILL_TRIPLE_SHOT_STR ||
                c->Skill == AT_SKILL_TRIPLE_SHOT_MASTERY)
                Skill = 1;
            if ((o->Type == MODEL_PLAYER && (o->CurrentAction == PLAYER_ATTACK_BOW ||
                                             o->CurrentAction == PLAYER_ATTACK_CROSSBOW ||
                                             o->CurrentAction == PLAYER_ATTACK_FLY_BOW ||
                                             o->CurrentAction == PLAYER_ATTACK_FLY_CROSSBOW ||
                                             o->CurrentAction == PLAYER_FENRIR_ATTACK_BOW ||
                                             o->CurrentAction == PLAYER_FENRIR_ATTACK_CROSSBOW ||
                                             o->CurrentAction == PLAYER_ATTACK_RIDE_BOW ||
                                             o->CurrentAction == PLAYER_ATTACK_RIDE_CROSSBOW)) ||
                o->Type != MODEL_PLAYER && o->Kind == KIND_PLAYER)
            {
                if (AT_SKILL_MULTI_SHOT != (c->Skill))
                    CreateArrows(c, o, NULL, FindHotKey((c->Skill)), Skill, (c->Skill));
            }

            if (o->Type == MODEL_HUNTER || o->Type == MODEL_VALKYRIE || o->Type == MODEL_SOLDIER ||
                o->Type == MODEL_ORC_ARCHER)
            {
                CreateArrows(c, o, NULL, 0, 0);
            }
        }
        else
        {
            CHARACTER *tc = &CharactersClient[c->TargetCharacter];
            OBJECT *to = &tc->Object;
            if (o->Type == MODEL_PLAYER &&
                (o->CurrentAction == PLAYER_ATTACK_BOW ||
                 o->CurrentAction == PLAYER_ATTACK_CROSSBOW ||
                 o->CurrentAction == PLAYER_ATTACK_FLY_BOW ||
                 o->CurrentAction == PLAYER_ATTACK_FLY_CROSSBOW ||
                 o->CurrentAction == PLAYER_ATTACK_RIDE_BOW ||
                 o->CurrentAction == PLAYER_ATTACK_RIDE_CROSSBOW ||
                 o->CurrentAction == PLAYER_FENRIR_ATTACK_BOW ||
                 o->CurrentAction ==
                     PLAYER_FENRIR_ATTACK_CROSSBOW //^ 펜릴 스킬 관련(요정 화살 나가게 하는 것)
                 ))
            {
                if (AT_SKILL_MULTI_SHOT != (c->Skill))
                    CreateArrows(c, o, to, FindHotKey((c->Skill)), 0, (c->Skill));
            }

            if (o->Type == MODEL_HUNTER || o->Type == MODEL_VALKYRIE || o->Type == MODEL_SOLDIER)
            {
                CreateArrows(c, o, to, 0, 0);
            }

            if (tc->Hit >= 1)
            {
                if (to->Type != MODEL_GHOST)
                {
                    for (int i = 0; i < 10; i++)
                    {
                        Vector(to->Position[0] + (float)(WorldRandom() % 64 - 32),
                               to->Position[1] + (float)(WorldRandom() % 64 - 32),
                               to->Position[2] + (float)(WorldRandom() % 64 + 90), Position);
                        CreateParticle(BITMAP_BLOOD + 1, Position, o->Angle, Light);
                    }
                }

                if (to->Type == MODEL_STATUE_OF_SAINT)
                {
                    for (int i = 0; i < 5; i++)
                    {
                        if (rand_fps_check(2))
                        {
                            Position[0] = to->Position[0];
                            Position[1] = to->Position[1];
                            Position[2] = to->Position[2] + 50 + WorldRandom() % 30;

                            CreateEffect(MODEL_STONE_COFFIN + 1, Position, o->Angle, o->Light);
                        }
                    }
                    PlayBuffer(SOUND_HIT_CRISTAL);
                }
                EmitMonsterHitEffect(to);
            }
            if (o->CurrentAction >= PLAYER_ATTACK_SKILL_SWORD1 &&
                o->CurrentAction <= PLAYER_ATTACK_SKILL_SWORD5)
            {
                CreateSpark(0, tc, to->Position, o->Angle);
            }
            vec3_t Angle;
            VectorCopy(o->Angle, Angle);
            Angle[2] = CreateAngle2D(o->Position, to->Position);
            switch ((c->Skill))
            {
            case AT_SKILL_TRIPLE_SHOT:
            case AT_SKILL_TRIPLE_SHOT_STR:
            case AT_SKILL_TRIPLE_SHOT_MASTERY:
                CreateArrows(c, o, NULL, FindHotKey((c->Skill)), 1);
                break;
            case AT_SKILL_PENETRATION:
            case AT_SKILL_PENETRATION_STR:
                CreateArrows(c, o, NULL, FindHotKey((c->Skill)), 0, (c->Skill));
                break;
            case AT_SKILL_ICE_ARROW:
            case AT_SKILL_ICE_ARROW_STR:
                CreateArrows(c, o, NULL, FindHotKey((c->Skill)), 0, (c->Skill));
                break;
            case AT_SKILL_DEEPIMPACT:
                CreateArrows(c, o, to, FindHotKey((c->Skill)), 0, (c->Skill));
                PlayBuffer(SOUND_BCS_DEEP_IMPACT);
                break;
            case AT_SKILL_HEALING:
            case AT_SKILL_HEALING_STR:
                CreateEffect(BITMAP_MAGIC + 1, to->Position, to->Angle, to->Light, 1, to);
                break;
            case AT_SKILL_DEFENSE:
            case AT_SKILL_DEFENSE_STR:
            case AT_SKILL_DEFENSE_MASTERY:
                if (c->SkillSuccess)
                {
                    if (g_isCharacterBuff(o, eBuff_Cloaking))
                    {
                        break;
                    }

                    CreateEffect(BITMAP_MAGIC + 1, to->Position, to->Angle, to->Light, 2, to);

                    if (!g_isCharacterBuff(to, eBuff_Defense))
                    {
                        g_CharacterRegisterBuff(to, eBuff_Defense);

                        for (int j = 0; j < 5; ++j)
                        {
                            CreateJoint(MODEL_SPEARSKILL, to->Position, to->Position, to->Angle, 4,
                                        to, 20.0f, -1, 0, 0, c->TargetCharacter);
                        }
                    }
                    else if (!SearchJoint(MODEL_SPEARSKILL, to, 4) &&
                             !SearchJoint(MODEL_SPEARSKILL, to, 9))
                    {
                        for (int j = 0; j < 5; ++j)
                        {
                            CreateJoint(MODEL_SPEARSKILL, to->Position, to->Position, to->Angle, 4,
                                        to, 20.0f, -1, 0, 0, c->TargetCharacter);
                        }
                    }
                }
                break;
            case AT_SKILL_ATTACK:
            case AT_SKILL_ATTACK_STR:
            case AT_SKILL_ATTACK_MASTERY:
                if (g_isCharacterBuff(o, eBuff_Cloaking))
                    break;
                CreateEffect(BITMAP_MAGIC + 1, to->Position, to->Angle, to->Light, 3, to);
                if (c->SkillSuccess)
                {
                    g_CharacterRegisterBuff(to, eBuff_Attack);
                }
                break;
            case AT_SKILL_ICE:
            case AT_SKILL_ICE_STR:
            case AT_SKILL_ICE_STR_MG:
                CreateEffect(MODEL_ICE, to->Position, o->Angle, Light);

                for (int i = 0; i < 5; i++)
                    CreateEffect(MODEL_ICE_SMALL, to->Position, o->Angle, o->Light);

                if (c->SkillSuccess)
                {
                    if (!g_isCharacterBuff(to, eDeBuff_Freeze))
                    {
                        g_CharacterRegisterBuff(to, eDeBuff_Freeze);
                    }
                }
                PlayBuffer(SOUND_ICE);
                break;
            case AT_SKILL_SOUL_BARRIER:
            case AT_SKILL_SOUL_BARRIER_STR:
            case AT_SKILL_SOUL_BARRIER_PROFICIENCY:
                if (o->Type == MODEL_DARK_PHEONIX_SHIELD)
                {
                    g_CharacterRegisterBuff(o, eBuff_WizDefense);
                }
                else
                {
                    if (g_isCharacterBuff(to, eBuff_Cloaking))
                        break;
                    g_CharacterRegisterBuff(to, eBuff_WizDefense);

                    PlayBuffer(SOUND_SOULBARRIER);
                    DeleteJoint(MODEL_SPEARSKILL, to, 0);
                    for (int j = 0; j < 5; ++j)
                    {
                        CreateJoint(MODEL_SPEARSKILL, to->Position, to->Position, to->Angle, 0, to,
                                    20.0f);
                    }
                }
                break;
            case AT_SKILL_POISON:
            case AT_SKILL_POISON_STR:
                if (o->Type == MODEL_PLAYER)
                    CreateEffect(MODEL_POISON, to->Position, o->Angle, o->Light);
                Vector(0.4f, 0.6f, 1.f, Light);

                for (int i = 0; i < 10; i++)
                    CreateParticle(BITMAP_SMOKE, to->Position, o->Angle, Light, 1);

                if (c->SkillSuccess)
                {
                    g_CharacterRegisterBuff(to, eDeBuff_Poison);
                }
                PlayBuffer(SOUND_HEART);
                break;
            case AT_SKILL_METEO:
                CreateEffect(MODEL_FIRE, to->Position, to->Angle, o->Light);
                PlayBuffer(SOUND_METEORITE01);
                break;
            case AT_SKILL_JAVELIN:
                CreateEffect(MODEL_SKILL_JAVELIN, o->Position, o->Angle, o->Light, 0, to);
                CreateEffect(MODEL_SKILL_JAVELIN, o->Position, o->Angle, o->Light, 1, to);
                CreateEffect(MODEL_SKILL_JAVELIN, o->Position, o->Angle, o->Light, 2, to);

                PlayBuffer(SOUND_BCS_JAVELIN);
                break;
            case AT_SKILL_DEATH_CANNON:
                Vector(0.f, 0.f, o->Angle[2], Angle);
                VectorCopy(o->Position, Position);

                Position[2] += 130.f;
                CreateJoint(BITMAP_JOINT_FORCE, Position, Position, Angle, 4, NULL, 40.f);

                PlayBuffer(SOUND_BCS_DEATH_CANON);
                break;
            case AT_SKILL_SPACE_SPLIT:
                CreateEffect(MODEL_PIER_PART, o->Position, o->Angle, o->Light, 2, to);
                PlayBuffer(SOUND_BCS_SPACE_SPLIT);
                break;
            case AT_SKILL_FIREBALL:
                CreateEffect(MODEL_FIRE, o->Position, Angle, o->Light, 1, to);
                PlayBuffer(SOUND_METEORITE01);
                break;
            case AT_SKILL_FLAME:
            case AT_SKILL_FLAME_STR:
            case AT_SKILL_FLAME_STR_MG:
                Position[0] = to->Position[0];
                Position[1] = to->Position[1];
                Position[2] = RequestTerrainHeight(Position[0], Position[1]);
                CreateEffect(BITMAP_FLAME, Position, o->Angle, o->Light, 5, o, o->PKKey,
                             FindHotKey(AT_SKILL_FLAME));
                PlayBuffer(SOUND_FLAME);
                break;
            case AT_SKILL_POWERWAVE:
                if (o->Type == MODEL_ICE_QUEEN)
                {
                    Angle[2] += 10.f;
                    CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light);
                    Angle[2] -= 20.f;
                    CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light);
                    Angle[2] += 10.f;
                }
                CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light);
                PlayBuffer(SOUND_MAGIC);
                break;
            case AT_SKILL_FORCE:
            case AT_SKILL_FORCE_WAVE:
            case AT_SKILL_FORCE_WAVE_STR:
                CreateEffect(MODEL_WAVES, o->Position, o->Angle, o->Light, 1);
                CreateEffect(MODEL_WAVES, o->Position, o->Angle, o->Light, 1);
                CreateEffect(MODEL_PIERCING2, o->Position, o->Angle, o->Light);
                PlayBuffer(SOUND_ATTACK_SPEAR);
                break;
            case AT_SKILL_FIREBURST:
            case AT_SKILL_FIREBURST_STR:
            case AT_SKILL_FIREBURST_MASTERY: {
                vec3_t Angle = {0.f, 0.f, o->Angle[2]};
                vec3_t Pos = {0.f, 0.f, (to->BoundingBoxMax[2] / 1.f)};

                Vector(40.f, 0.f, 10.f, p);
                b->TransformPosition(o->BoneTransform[0], p, Position, true);
                Angle[2] = o->Angle[2] + 90;
                CreateEffect(MODEL_PIER_PART, Position, Angle, Pos, 0, to);
                Pos[2] -= to->BoundingBoxMax[2] / 2;
                Angle[2] = o->Angle[2];
                CreateEffect(MODEL_PIER_PART, Position, Angle, Pos, 0, to);
                Angle[2] = o->Angle[2] - 90;
                CreateEffect(MODEL_PIER_PART, Position, Angle, Pos, 0, to);

                Vector(1.f, 0.6f, 0.3f, Light);
                CreateEffect(MODEL_DARKLORD_SKILL, Position, o->Angle, Light, 0);
                CreateEffect(MODEL_DARKLORD_SKILL, Position, o->Angle, Light, 1);
            }
            break;

            case AT_SKILL_ENERGYBALL:
                switch (c->MonsterIndex)
                {
                case MONSTER_DEVIL:
                case MONSTER_VEPAR:
                case MONSTER_BEAM_KNIGHT:
                case MONSTER_CURSED_KING:
                case MONSTER_ALQUAMOS:
                case MONSTER_QUEEN_RAINER:
                case MONSTER_DRAKAN:
                case MONSTER_GREAT_DRAKAN:
                case MONSTER_DARK_PHOENIX:
                case MONSTER_MAGIC_SKELETON_1:
                case MONSTER_MAGIC_SKELETON_2:
                case MONSTER_MAGIC_SKELETON_3:
                case MONSTER_MAGIC_SKELETON_4:
                case MONSTER_MAGIC_SKELETON_5:
                case MONSTER_MAGIC_SKELETON_6:
                case MONSTER_MAGIC_SKELETON_7:
                case MONSTER_GIANT_OGRE_1:
                case MONSTER_GIANT_OGRE_2:
                case MONSTER_GIANT_OGRE_3:
                case MONSTER_GIANT_OGRE_4:
                case MONSTER_GIANT_OGRE_5:
                case MONSTER_GIANT_OGRE_6:
                case MONSTER_GIANT_OGRE_7:
                case MONSTER_CHAOS_CASTLE_2:
                case MONSTER_CHAOS_CASTLE_4:
                case MONSTER_CHAOS_CASTLE_6:
                case MONSTER_CHAOS_CASTLE_8:
                case MONSTER_CHAOS_CASTLE_10:
                case MONSTER_CHAOS_CASTLE_12:
                case MONSTER_CHAOS_CASTLE_14:
                case MONSTER_GIGAS_GOLEM:
                case MONSTER_POISON_GOLEM:
                    break;
                default:
                    if (o->Type == MODEL_YETI)
                    {
                        CreateEffect(MODEL_SNOW1, o->Position, Angle, o->Light, 0, to);
                    }
                    else if (o->Type == MODEL_GRIZZLY)
                    {
                        CreateEffect(MODEL_WOOSISTONE, o->Position, Angle, o->Light, 0, to);
                    }
                    else if (o->Type == MODEL_SAPITRES)
                    {
                        vec3_t vLight;
                        Vector(1.0f, 1.0f, 1.0f, vLight);
                        CreateEffect(MODEL_EFFECT_SAPITRES_ATTACK, o->Position, o->Angle, vLight, 0,
                                     to);
                    }
                    else
                    {
                        CreateEffect(BITMAP_ENERGY, o->Position, Angle, o->Light, 0, to);
                        PlayBuffer(SOUND_MAGIC);
                    }
                    break;
                }
                break;

            case AT_SKILL_ALICE_LIGHTNINGORB: {
                vec3_t vLight;
                Vector(1.0f, 1.0f, 1.0f, vLight);

                CreateEffect(MODEL_LIGHTNING_ORB, o->Position, o->Angle, vLight, 0, to);

                PlayBuffer(SOUND_SUMMON_SKILL_LIGHTORB);
            }
            break;

            case AT_SKILL_ALICE_BLIND:
            case AT_SKILL_ALICE_SLEEP:
            case AT_SKILL_ALICE_SLEEP_STR:
            case AT_SKILL_ALICE_THORNS: {
                int iSkillIndex = (c->Skill);
                vec3_t vLight;

                if (iSkillIndex == AT_SKILL_ALICE_SLEEP || iSkillIndex == AT_SKILL_ALICE_SLEEP_STR)
                {
                    Vector(0.7f, 0.3f, 0.8f, vLight);
                }
                else if (iSkillIndex == AT_SKILL_ALICE_BLIND)
                {
                    Vector(1.0f, 1.0f, 1.0f, vLight);
                }
                else if (iSkillIndex == AT_SKILL_ALICE_THORNS)
                {
                    Vector(0.8f, 0.5f, 0.2f, vLight);
                }
                if (iSkillIndex == AT_SKILL_ALICE_SLEEP || iSkillIndex == AT_SKILL_ALICE_THORNS ||
                    iSkillIndex == AT_SKILL_ALICE_SLEEP_STR)
                {
                    CreateEffect(BITMAP_MAGIC + 1, o->Position, o->Angle, vLight, 11, o);
                }
                else if (iSkillIndex == AT_SKILL_ALICE_BLIND)
                {
                    CreateEffect(BITMAP_MAGIC + 1, o->Position, o->Angle, vLight, 12, o);
                }

                if (iSkillIndex == AT_SKILL_ALICE_SLEEP || iSkillIndex == AT_SKILL_ALICE_SLEEP_STR)
                {
                    Vector(0.8f, 0.3f, 0.9f, vLight);
                    CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT, to->Position, to->Angle, vLight, 0,
                                 to);
                    CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT2, to->Position, to->Angle, vLight, 0,
                                 to);
                }
                else if (iSkillIndex == AT_SKILL_ALICE_BLIND)
                {
                    Vector(1.0f, 1.0f, 1.0f, vLight);
                    CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT, to->Position, to->Angle, vLight, 1,
                                 to);
                    CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT2, to->Position, to->Angle, vLight, 1,
                                 to);
                }
                else if (iSkillIndex == AT_SKILL_ALICE_THORNS)
                {
                    Vector(0.8f, 0.5f, 0.2f, vLight);
                    CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT, to->Position, to->Angle, vLight, 2,
                                 to);
                    CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT2, to->Position, to->Angle, vLight, 2,
                                 to);
                }
            }
            break;
            case AT_SKILL_ALICE_CHAINLIGHTNING:
            case AT_SKILL_ALICE_CHAINLIGHTNING_STR: {
                PlayBuffer(SOUND_SKILL_CHAIN_LIGHTNING);
            }
            break;
            case AT_SKILL_ALICE_DRAINLIFE:
            case AT_SKILL_ALICE_DRAINLIFE_STR: {
                CHARACTER *pTargetChar = &CharactersClient[c->TargetCharacter];
                OBJECT *pSourceObj = o;
                pSourceObj->Owner = &(pTargetChar->Object);

                CreateEffect(MODEL_ALICE_DRAIN_LIFE, pSourceObj->Position, pSourceObj->Angle,
                             pSourceObj->Light, 0, pSourceObj);
                PlayBuffer(SOUND_SKILL_DRAIN_LIFE);
            }
            break;
            }

            VectorCopy(to->Position, Position);
            Position[2] += 120.f;

            int Hand = 0;
            if (o->CurrentAction == PLAYER_ATTACK_SWORD_LEFT1 ||
                o->CurrentAction == PLAYER_ATTACK_SWORD_LEFT2)
                Hand = 1;

            if (tc == Hero)
            {
                Vector(1.f, 0.f, 0.f, Light);
            }
            else
            {
                Vector(1.f, 0.6f, 0.f, Light);
            }

            switch (c->AttackFlag)
            {
            case ATTACK_DIE:
                CreateJoint(BITMAP_JOINT_ENERGY, to->Position, to->Position, o->Angle, 0, o, 20.f);
                CreateJoint(BITMAP_JOINT_ENERGY, to->Position, to->Position, o->Angle, 1, o, 20.f);
                break;
            }

            switch ((c->Skill))
            {
            case AT_SKILL_HEALING:
            case AT_SKILL_HEALING_STR:
            case AT_SKILL_ATTACK:
            case AT_SKILL_ATTACK_STR:
            case AT_SKILL_ATTACK_MASTERY:
            case AT_SKILL_DEFENSE:
            case AT_SKILL_DEFENSE_STR:
            case AT_SKILL_DEFENSE_MASTERY:
            case AT_SKILL_SUMMON:
            case AT_SKILL_SUMMON + 1:
            case AT_SKILL_SUMMON + 2:
            case AT_SKILL_SUMMON + 3:
            case AT_SKILL_SUMMON + 4:
            case AT_SKILL_SUMMON + 5:
            case AT_SKILL_SUMMON + 6:
            case AT_SKILL_SUMMON + 7:
            case AT_SKILL_SOUL_BARRIER:
            case AT_SKILL_SOUL_BARRIER_STR:
            case AT_SKILL_SOUL_BARRIER_PROFICIENCY:
            case AT_SKILL_DEATHSTAB:
            case AT_SKILL_DEATHSTAB_STR:
            case AT_SKILL_IMPALE:
            case AT_SKILL_SWELL_LIFE:
            case AT_SKILL_SWELL_LIFE_STR:
            case AT_SKILL_SWELL_LIFE_PROFICIENCY:
            case AT_SKILL_NOVA:
            case AT_SKILL_IMPROVE_AG:
            case AT_SKILL_ADD_CRITICAL:
            case AT_SKILL_ADD_CRITICAL_STR1:
            case AT_SKILL_ADD_CRITICAL_STR2:
            case AT_SKILL_ADD_CRITICAL_STR3:
            case AT_SKILL_PARTY_TELEPORT:
            case AT_SKILL_STUN:
            case AT_SKILL_REMOVAL_STUN:
            case AT_SKILL_MANA:
            case AT_SKILL_INVISIBLE:
            case AT_SKILL_REMOVAL_BUFF:
            case AT_SKILL_BRAND_OF_SKILL:
                break;
            default:
                if (MONSTER_MOLT <= c->MonsterIndex && c->MonsterIndex <= MONSTER_GREAT_DRAKAN)
                {
                }
                else
                {
                    ITEM *r = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT];
                    ITEM *l = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];

                    if ((r->Type >= ITEM_BOW && r->Type < ITEM_BOW + MAX_ITEM_INDEX) &&
                        (l->Type >= ITEM_BOW && l->Type < ITEM_BOW + MAX_ITEM_INDEX))
                    {
                        PlayBuffer(
                            static_cast<ESound>(SOUND_ATTACK_MELEE_HIT1 + 5 + WorldRandom() % 4),
                            o);
                    }
                    else
                    {
                        PlayBuffer(static_cast<ESound>(SOUND_ATTACK_MELEE_HIT1 + WorldRandom() % 4),
                                   o);
                    }
                }
                break;
            }
        }

        c->Skill = 0;
        c->Damage = 0;
        c->AttackFlag = ATTACK_FAIL;
    }

    SetBuildTimeLocation(o);

    CreateWeaponBlur(c, o, b);

    switch (o->Type)
    {
    case MODEL_BALL:
        CreateFire(0, o, 0.f, 0.f, 0.f);
        break;
    }

    if (c->Dead > 0)
    {
        if (g_isCharacterBuff(o, eBuff_BlessPotion))
            g_CharacterUnRegisterBuff(o, eBuff_BlessPotion);
        if (g_isCharacterBuff(o, eBuff_SoulPotion))
            g_CharacterUnRegisterBuff(o, eBuff_SoulPotion);
    }
}

void SessionGameplayUnit::PlayWalkSound()
{
    const auto action = Hero->Object.CurrentAction;
    if (action == PLAYER_FLY || action == PLAYER_FLY_CROSSBOW)
        return;
    PlayBuffer(TheMapProcess().WalkingSound(HeroTile, Hero->SafeZone));
}

CharacterEquipmentSet SessionGameplayUnit::EvaluateCharacterEquipmentSet(const CHARACTER &character)
{
    const bool controlled = &character == Hero;
    const int baseClass = gCharacterManager.GetBaseClass(character.Class);
    const int armor = controlled ? EQUIPMENT_ARMOR : BODYPART_ARMOR;
    const int gloves = controlled ? EQUIPMENT_GLOVES : BODYPART_GLOVES;
    const int boots = controlled ? EQUIPMENT_BOOTS : BODYPART_BOOTS;
    const int first = baseClass == CLASS_DARK ? armor : controlled ? EQUIPMENT_HELM : BODYPART_HELM;
    const auto typeAt = [&](int index) {
        if (controlled)
            return static_cast<int>(CharacterMachine->Equipment[index].Type);
        const int type = character.BodyPart[index].Type;
        return type == -1 ? -1 : type - MODEL_ITEM;
    };
    const auto levelAt = [&](int index) {
        return controlled ? static_cast<int>(CharacterMachine->Equipment[index].Level)
                          : character.BodyPart[index].Level & 0xf;
    };
    CharacterEquipmentSet result;
    const int setType = typeAt(boots) % MAX_ITEM_INDEX;
    int level = levelAt(boots);
    constexpr int fullSetMinimumLevel = 9;
    for (int index = boots; index >= first; --index)
    {
        if (baseClass == CLASS_RAGEFIGHTER && index == gloves)
            continue;
        if (typeAt(index) == -1 || levelAt(index) < fullSetMinimumLevel ||
            typeAt(index) % MAX_ITEM_INDEX != setType)
            return result;
        level = (std::min)(level, levelAt(index));
    }
    result.complete = true;
    result.level = level;
    if (baseClass != CLASS_DARK)
        return result;
    switch (typeAt(armor))
    {
    case ITEM_STORM_CROW_ARMOR:
    case ITEM_THUNDER_HAWK_ARMOR:
    case ITEM_HURRICANE_ARMOR:
    case ITEM_VOLCANO_ARMOR:
    case ITEM_VALIANT_ARMOR:
    case ITEM_DESTORY_ARMOR:
    case ITEM_PHANTOM_ARMOR:
        break;
    default:
        result.addDefense = false;
        break;
    }
    return result;
}

bool SessionGameplayUnit::CheckFullSet(CHARACTER *character)
{
    const auto result = EvaluateCharacterEquipmentSet(*character);
    if (character == Hero)
    {
        EquipmentLevelSet = result.level;
        g_bAddDefense = result.addDefense;
    }
    return result.complete;
}

void MoveEye(OBJECT *o, BMD *b, int Right, int Left, int Right2, int Left2, int Right3, int Left3)
{
    vec3_t p;
    Vector(0.f, 0.f, 0.f, p);
    b->TransformPosition(o->BoneTransform[Right], p, o->EyeRight, true);
    Vector(0.f, 0.f, 0.f, p);
    b->TransformPosition(o->BoneTransform[Left], p, o->EyeLeft, true);
    if (Right2 != -1)
        b->TransformPosition(o->BoneTransform[Right2], p, o->EyeRight2, true);
    if (Left2 != -1)
        b->TransformPosition(o->BoneTransform[Left2], p, o->EyeLeft2, true);
    if (Right3 != -1)
        b->TransformPosition(o->BoneTransform[Right3], p, o->EyeRight3, true);
    if (Left3 != -1)
        b->TransformPosition(o->BoneTransform[Left3], p, o->EyeLeft3, true);
}

void SessionGameplayUnit::MonsterMoveSandSmoke(OBJECT *o)
{
    if (o->CurrentAction != MONSTER01_WALK)
        return;
    for (auto birthTime : Emissions(FPS_ANIMATION_FACTOR))
    {
        vec3_t position;
        const float fraction = birthTime.FrameFraction();
        o->MotionTrace.Sample(WorldTime, fraction, o->Position, position);
        position[0] += WorldRandom() % 200 - 100;
        position[1] += WorldRandom() % 200 - 100;
        CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, o->Light);
    }
}

void SessionGameplayUnit::SetCharacterTarget(CHARACTER &character, int index)
{
    character.TargetCharacter = static_cast<short>(index);
    auto &binding = character.TargetBinding;
    binding.sourceIndex = index;
    ++binding.revision;
    binding.sender = sessionKeeper_.Id();
    binding.character = CharactersClient.IsValidIndex(index) ? &CharactersClient[index] : nullptr;
    binding.source = binding.character ? binding.character->SocketSource : nullptr;
    binding.key = binding.character ? binding.character->Key : -1;
}

void SessionGameplayUnit::BindCharacterTarget(CHARACTER &character)
{
    auto &binding = character.TargetBinding;
    if (binding.sourceIndex != character.TargetCharacter || !binding.sender)
        SetCharacterTarget(character, character.TargetCharacter);
    else if (binding.sender != sessionKeeper_.Id() || (binding.source && !binding.source->object))
    {
        const bool alive = binding.source && binding.source->object;
        const int index = alive ? CharactersClient.FindIndexByKey(binding.key) : -1;
        if (index >= 0)
            SetCharacterTarget(character, index);
        else
        {
            character.TargetCharacter = -1;
            binding.sourceIndex = -1;
            binding.sender = sessionKeeper_.Id();
            binding.character = nullptr;
        }
    }
}

void SessionGameplayUnit::AdvanceCharacterEnvironmentState(CHARACTER &character)
{
    const int index = TERRAIN_INDEX_REPEAT(character.PositionX, character.PositionY);
    character.SafeZone = (TerrainWall[index] & TW_SAFEZONE) != 0;
    character.WorldVisualAppearing = character.Appear > 0.f;
    character.Appear = (std::max)(0.f, character.Appear - FPS_ANIMATION_FACTOR);
}

float SessionGameplayUnit::CharacterMoveSpeed(CHARACTER *c, float runFrames)
{
    if (runFrames < 0.f)
        runFrames = c->Run;
    OBJECT *o = &c->Object;
    auto Speed = (float)c->MoveSpeed;
    if (o->Type == MODEL_PLAYER && o->Kind == KIND_PLAYER)
    {
        bool isholyitem = false;

        isholyitem = sessionKeeper_.CursedTempleObject().CheckInventoryHolyItem(c);

        if (isholyitem)
        {
            c->Run = 40;
            Speed = 8;
            return Speed;
        }

        if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone && !isholyitem)
        {
            if (runFrames < FENRIR_RUN_DELAY / 2)
                Speed = 15;
            else if (runFrames < FENRIR_RUN_DELAY)
                Speed = 16;
            else
            {
                if (c->Helper.ExcellentFlags > 0)
                    Speed = 19;
                else
                    Speed = 17;
            }
        }
        else if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone && !isholyitem)
        {
            c->Run = 40;
            Speed = 17;
        }
        else if (!(c->Object.SubType == MODEL_CURSEDTEMPLE_ALLIED_PLAYER ||
                   c->Object.SubType == MODEL_CURSEDTEMPLE_ILLUSION_PLAYER) &&
                 (c->Wing.Type != -1 || (c->Helper.Type >= MODEL_HORN_OF_UNIRIA &&
                                         c->Helper.Type <= MODEL_HORN_OF_DINORANT)) &&
                 !c->SafeZone && !isholyitem)
        {
            if (c->Wing.Type == MODEL_WINGS_OF_DRAGON || c->Wing.Type == MODEL_WING_OF_STORM)
            {
                c->Run = 40;
                Speed = 16;
            }
            else
            {
                c->Run = 40;
                Speed = 15;
            }
        }
        else if (!isholyitem)
        {
            if (runFrames < 40)
                Speed = 12;
            else
                Speed = 15;
        }
    }
#ifndef GUILD_WAR_EVENT
    if (gMapManager.InChaosCastle() == true)
    {
        c->Run = 40;
        Speed = 15;
    }
#endif // GUILD_WAR_EVENT

    if (g_isCharacterBuff((&c->Object), eDeBuff_Freeze))
    {
        Speed *= 0.5f;
    }
    else if (g_isCharacterBuff((&c->Object), eDeBuff_BlowOfDestruction))
    {
        Speed *= 0.33f;
    }

    if (g_isCharacterBuff((&c->Object), eBuff_CursedTempleQuickness))
    {
        c->Run = 40;
        Speed = 20;
    }

    return Speed;
}

void SessionGameplayUnit::SetCharacterTerrainHeight(CHARACTER &character)
{
    constexpr float FlyingMountHeight = 90.f, GroundMountHeight = 30.f;
    auto &object = character.Object;
    float height = RequestTerrainHeight(object.Position[0], object.Position[1]);
    if (gMapManager.ContextMap() != -1 && character.Helper.Type == MODEL_HORN_OF_DINORANT &&
        !character.SafeZone)
        height +=
            TheMapProcess().CharacterPolicy().flyingMounts ? FlyingMountHeight : GroundMountHeight;
    object.SetPositionZ(height);
}

void SessionGameplayUnit::UpdateHeroHeight()
{
    if (!Hero || !Hero->Object.Live)
        return;
    constexpr float RaisedTerrainHeight = 1201.f, DirectionHeroHeight = 300.f;
    const int tile = TERRAIN_INDEX(Hero->PositionX, Hero->PositionY);
    if ((TerrainWall[tile] & TW_HEIGHT) != 0)
        g_fSpecialHeight = RaisedTerrainHeight;
    if (g_Direction.IsDirection(gMapManager.ContextMap()) && !g_Direction.m_bDownHero)
    {
        Hero->Object.SetPositionZ(DirectionHeroHeight);
        return;
    }
    if (Hero->Object.m_bActionStart &&
        (gMapManager.InChaosCastle() || gMapManager.ContextMap() == WD_39KANTURU_3RD))
        return;
    SetCharacterTerrainHeight(*Hero);
}

void SessionGameplayUnit::FinishCharacterMovement(CHARACTER *c, float frames)
{
    auto &object = c->Object;
    object.EnableBoneMatrix = false;
    constexpr float BobPhaseRate = 0.15f;
    object.Timer += BobPhaseRate * frames;
    SetCharacterTerrainHeight(*c);
    if (object.Type == MODEL_BUDGE_DRAGON)
    {
        // Retain the authored one-reference-tick pose lag, independent of render FPS.
        const float phase = object.Timer - BobPhaseRate;
        object.Position[2] += -absf(sinf(phase)) * 70.f + 70.f;
    }
}

void SessionGameplayUnit::MoveCharacterPosition(CHARACTER *c)
{
    OBJECT *o = &c->Object;
    float matrix[3][4];
    AngleMatrix(o->Angle, matrix);
    vec3_t velocity, direction;
    Vector(0.f, -CharacterMoveSpeed(c), 0.f, velocity);
    VectorRotate(velocity, matrix, direction);
    VectorAddScaled(o->Position, direction, o->Position, FPS_ANIMATION_FACTOR);
    FinishCharacterMovement(c, FPS_ANIMATION_FACTOR);
}

bool SessionGameplayUnit::AdvanceCharacterPath(CHARACTER *c, bool preserveAction)
{
    float remaining = FPS_ANIMATION_FACTOR;
    if (remaining <= 0.f)
        return false;
    if (c->SafeZone || (c->MonsterIndex >= MONSTER_DOPPELGANGER_ELF &&
                        c->MonsterIndex <= MONSTER_DOPPELGANGER_SUM))
        c->Run = 0.f;
    if (!preserveAction && c->PathAnimationWorldTime != WorldTime)
    {
        c->PathAnimationWorldTime = WorldTime;
        c->PathAnimationFrames = 0.f;
    }
    const bool advanceRun = !preserveAction && CanAdvanceCharacterRun(*c);
    constexpr float RunDelay = 40.f;
    while (remaining > 0.f)
    {
        // Original movement used Run after its one-tick increment. The +1
        // keeps that authored speed schedule while Run itself remains fractional.
        float runFrames = advanceRun ? (std::min)(RunDelay, c->Run + 1.f) : c->Run;
        if (!preserveAction)
            SetPlayerWalk(c, runFrames);
        const float speed = CharacterMoveSpeed(c, runFrames);
        if (speed <= 0.f)
            return false;
        float frames = remaining;
        if (advanceRun)
        {
            const bool fenrir = c->Helper.Type == MODEL_HORN_OF_FENRIR;
            const float boundary =
                fenrir ? (c->Run < FENRIR_RUN_DELAY / 2.f - 1.f ? FENRIR_RUN_DELAY / 2.f - 1.f
                                                                : FENRIR_RUN_DELAY - 1.f)
                       : RunDelay - 1.f;
            if (c->Run < boundary)
                frames = (std::min)(frames, boundary - c->Run);
        }
        float travel = speed * frames;
        const bool arrived = MovePath(c, frames, travel, true, [&](float step) {
            FinishCharacterMovement(c, step);
            c->Object.MotionTrace.Advance(step, c->Object.Position);
        });
        const float consumed = frames - travel / speed;
        if (advanceRun)
            c->Run = (std::min)(RunDelay, c->Run + consumed);
        if (!preserveAction)
        {
            if (!CharacterAnimation(c, &c->Object, consumed))
                c->LongRangeAttack = -1;
            c->PathAnimationFrames += consumed;
        }
        if (arrived)
            return true;
        remaining -= consumed;
    }
    return false;
}

void SessionGameplayUnit::MoveMonsterClient(CHARACTER *c, OBJECT *o)
{
    if (c == Hero)
        return;

    if (c->Dead == 0)
    {
        if (c->MonsterIndex == MONSTER_ILLUSION_ITEM_STORAGE ||
            c->MonsterIndex == MONSTER_ALLIANCE_ITEM_STORAGE)
        {
            c->Movement = false;
        }

        if (!c->Movement)
        {
            if (c->Appear == 0 && o->Type != MODEL_GHOST &&
                ((c->PositionX) != c->TargetX || (c->PositionY) != c->TargetY))
            {
                const int iDefaultWall = TheMapProcess().CharacterPolicy().monsterWall;

                if (PathFinding2((c->PositionX), (c->PositionY), c->TargetX, c->TargetY, &c->Path,
                                 0.0f, iDefaultWall))
                {
                    c->Movement = true;
                }
            }
        }
        if (c->Movement)
        {
            const bool preserveAction =
                o->Type == MODEL_DARK_SKULL_SOLDIER_5 &&
                (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2 ||
                 o->CurrentAction == MONSTER01_ATTACK3 || o->CurrentAction == MONSTER01_ATTACK4 ||
                 o->CurrentAction == MONSTER01_ATTACK5);
            if (preserveAction)
                SetAction(o, o->CurrentAction);

            if (AdvanceCharacterPath(c, preserveAction))
            {
                c->Movement = false;
                SetPlayerStop(c);
            }
        }
    }
    else
    {
        if (o->Type == MODEL_BUDGE_DRAGON)
        {
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
        }
    }
}

void SessionGameplayUnit::MoveCharacterClient(CHARACTER *cc)
{
    OBJECT *co = &cc->Object;
    co->MotionTrace.Begin(WorldTime, FPS_ANIMATION_FACTOR, co->Position);
    if (co->Live)
    {
        co->Visible = TestFrustrum2D(co->Position[0] * 0.01f, co->Position[1] * 0.01f, -20.f);

        BindCharacterTarget(*cc);
        MoveMonsterClient(cc, co);
        MoveCharacter(cc, co);
        AdvanceCharacterEnvironmentState(*cc);

        TheMapProcess().MoveCharacterState(cc, co);
    }
}

void SessionGameplayUnit::PrepareCharacterOccupancy()
{
    for (int i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++)
    {
        if ((TerrainWall[i] & TW_CHARACTER) == TW_CHARACTER)
            TerrainWall[i] -= TW_CHARACTER;
    }

    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        auto sharedCharacterAccess = CharactersClient.AcquireSharedAccess(i);
        CHARACTER *tc = &CharactersClient[i];
        OBJECT *to = &tc->Object;
        if (to->Live && tc->Dead == 0 && to->Kind != KIND_TRAP)
        {
            int Index = TERRAIN_INDEX_REPEAT((tc->PositionX), (tc->PositionY));
            TerrainWall[Index] |= TW_CHARACTER;
        }

        const bool visible =
            TestFrustrum2D(to->Position[0] * 0.01f, to->Position[1] * 0.01f, -20.f);
        CharactersClient.SetVisible(i, visible);
        if (CharactersClient.IsSource(i))
            to->Visible = visible;
    }
}

void SessionGameplayUnit::MoveCharactersClient()
{
    PrepareCharacterOccupancy();
    TheMapProcess().BeginCharacterTick();
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        int characterToDelete = -1;
        {
            auto sharedCharacterAccess = CharactersClient.AcquireSharedAccess(i);
            if (CharactersClient.IsSource(i))
            {
                CHARACTER *const character = &CharactersClient[i];
                MoveCharacterClient(character);
                if (CharacterPresentationDetail::AdvanceCharacterDeleteTimer(*character,
                                                                             FPS_ANIMATION_FACTOR))
                {
                    characterToDelete = character->Key;
                    if (!CharactersClient.IsShared(i))
                    {
                        character->m_iDeleteTime =
                            CharacterPresentationDetail::CHARACTER_DELETE_TIME_INACTIVE;
                    }
                }
            }
            if (characterToDelete < 0)
                TheMapProcess().ObserveCharacterTick(CharactersClient[i]);
        }
        if (characterToDelete >= 0)
            DeleteCharacter(characterToDelete);
    }
    TheMapProcess().FinishCharacterTick();
    UpdateCharactersAnimationParallel(CharactersClient.Pointers());
    MoveBlurs();
}

namespace CharacterPresentationDetail
{
bool CharacterUsesGroundShadow(const CHARACTER &character, bool skyTerrain)
{
    if (character.Object.Type == MODEL_PLAYER || character.Object.Kind == KIND_TRAP || skyTerrain)
        return false;
    switch (character.MonsterIndex)
    {
    case MONSTER_ICE_QUEEN:
    case MONSTER_ICE_MONSTER:
    case MONSTER_RED_DRAGON:
    case MONSTER_ELF_LALA:
    case MONSTER_ZAIKAN:
    case MONSTER_DEATH_BEAM_KNIGHT:
    case MONSTER_GATE_TO_KALIMA_1:
        return false;
    default:
        return true;
    }
}
} // namespace CharacterPresentationDetail

void SessionGameplayUnit::AdvanceHalloweenFormState(CHARACTER &character)
{
    auto &object = character.Object;
    character.WorldVisualHalloweenSparks = 0;
    if (character.WorldVisualHalloweenBurst)
    {
        character.WorldVisualHalloweenBurst = false;
        character.WorldVisualHalloweenProgress = 0.f;
        object.m_iAnimation = 0;
        if (object.CurrentAction == PLAYER_JACK_1 || object.CurrentAction == PLAYER_JACK_2)
        {
            SetAction(&object, PLAYER_SHOCK);
            if (&character == Hero)
                SendRequestAction(object, PLAYER_SHOCK);
        }
        return;
    }
    const bool firstAction = object.CurrentAction == PLAYER_JACK_1;
    const bool secondAction = object.CurrentAction == PLAYER_JACK_2;
    if (!firstAction && !secondAction)
        return;
    const float previousFrame = character.WorldVisualAction == object.CurrentAction &&
                                        character.WorldVisualAnimationFrame <= object.AnimationFrame
                                    ? character.WorldVisualAnimationFrame
                                    : 0.f;
    const auto visitsFrame = [&](float frame) {
        return object.AnimationFrame >= frame &&
               (object.AnimationFrame < frame + 1.f || previousFrame < frame + 1.f);
    };
    const float firstSparkFrame = firstAction ? 2.f : 10.f;
    const float lastSparkFrame = firstAction ? 9.f : 19.f;
    character.WorldVisualHalloweenSparks = static_cast<unsigned int>(visitsFrame(firstSparkFrame)) +
                                           static_cast<unsigned int>(visitsFrame(lastSparkFrame));
    character.WorldVisualHalloweenProgress +=
        character.WorldVisualHalloweenSparks * FPS_ANIMATION_FACTOR;
    object.m_iAnimation = static_cast<int>(character.WorldVisualHalloweenProgress);
    constexpr float burstProgress = 40.f;
    character.WorldVisualHalloweenBurst =
        character.WorldVisualHalloweenProgress >= burstProgress && visitsFrame(lastSparkFrame);
}

namespace CharacterPresentationDetail
{
void CollectHelperPetRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                                std::vector<std::shared_ptr<PetObject>> &retired)
{
    if (!visual.helperPet)
        return;
    targets.push_back(visual.helperPet->GetObject());
    retired.push_back(std::move(visual.helperPet));
}

void CollectMountRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                            std::vector<std::unique_ptr<CharacterMountVisual>> &retired)
{
    if (!visual.mount)
        return;
    targets.push_back(&visual.mount->object);
    retired.push_back(std::move(visual.mount));
}

void CollectPetRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                          std::vector<std::unique_ptr<CSPetSystem>> &retired)
{
    if (!visual.darkSpirit)
        return;
    targets.push_back(visual.darkSpirit->GetObject());
    retired.push_back(std::move(visual.darkSpirit));
}

void CollectPartsRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                            std::vector<std::unique_ptr<CSIPartsMDL>> &retired)
{
    for (auto *part : {&visual.parts, &visual.temporaryParts})
    {
        if (!*part)
            continue;
        targets.push_back((*part)->GetObject());
        retired.push_back(std::move(*part));
    }
    visual.partsType = 0;
    visual.temporaryPartsType = -1;
}

void CollectLinkedItemRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                                 std::vector<std::unique_ptr<CharacterLinkedItemVisual>> &retired)
{
    for (auto &[slot, item] : visual.linkedItems)
    {
        if (!item)
            continue;
        targets.push_back(&item->target);
        targets.push_back(&item->item);
        retired.push_back(std::move(item));
    }
    if (visual.sprites)
        visual.sprites->Clear();
    visual.linkedItems.clear();
    visual.linkedItemSlots = 0;
}
} // namespace CharacterPresentationDetail

namespace CharacterPresentationDetail
{
void RetireClothAndAfterImages(WorldCharacterVisualState &visual)
{
    visual.bodyCloth.reset();
    visual.capeCloth.reset();
    visual.partCloth.reset();
    visual.darkside.reset();
    visual.darksidePoses.clear();
    if (visual.attachmentSprites)
        visual.attachmentSprites->Clear();
}
} // namespace CharacterPresentationDetail

void LittleSantaLight(int type, vec3_t light)
{
    static constexpr vec3_t colors[]{{0.5f, 0.5f, 0.f},  {0.3f, 0.8f, 0.4f}, {0.8f, 0.1f, 0.1f},
                                     {0.3f, 0.3f, 0.8f}, {0.9f, 0.9f, 0.9f}, {0.9f, 0.9f, 0.9f},
                                     {0.8f, 0.4f, 0.f},  {0.9f, 0.5f, 0.7f}};
    VectorCopy(colors[type - MODEL_LITTLESANTA], light);
}

void SessionGameplayUnit::DetachCharacterBindings(CHARACTER *character)
{
    OBJECT *const object = &character->Object;
    g_SummonSystem.ForgetCharacterPhase(&character->Object);
    for (int i = 0; i < MAX_MOUNTS; ++i)
    {
        OBJECT &mount = Mounts[i];
        if (mount.Live && mount.Owner == object)
            mount.Live = false;
    }
    sessionKeeper_.Visual()->DetachCharacterChats(character);
    boneManager_.UnregisterBone(character);
}

void SessionGameplayUnit::DetachCharacterVisuals(CHARACTER *character)
{
    DetachCharacterBindings(character);
    // Retire all exact-session consumers before releasing this observation.
    std::vector<OBJECT *> targets{&character->Object};
    std::vector<std::unique_ptr<CharacterLinkedItemVisual>> retiredItems;
    std::vector<std::unique_ptr<CSIPartsMDL>> retiredParts;
    std::vector<std::unique_ptr<CSPetSystem>> retiredPets;
    std::vector<std::unique_ptr<CharacterMountVisual>> retiredMounts;
    std::vector<std::shared_ptr<PetObject>> retiredHelperPets;
    const int index = CharactersClient.FindIndexByKey(character->Key);
    if (index >= 0 && &CharactersClient[index] == character)
    {
        CharacterPresentationDetail::CollectLinkedItemRetirement(
            CharactersClient.WorldVisuals(index), targets, retiredItems);
        CharacterPresentationDetail::CollectPartsRetirement(CharactersClient.WorldVisuals(index),
                                                            targets, retiredParts);
        CharacterPresentationDetail::RetireClothAndAfterImages(
            CharactersClient.WorldVisuals(index));
        CharacterPresentationDetail::CollectPetRetirement(CharactersClient.WorldVisuals(index),
                                                          targets, retiredPets);
        CharacterPresentationDetail::CollectHelperPetRetirement(
            CharactersClient.WorldVisuals(index), targets, retiredHelperPets);
        CharacterPresentationDetail::CollectMountRetirement(CharactersClient.WorldVisuals(index),
                                                            targets, retiredMounts);
    }
    RetireCharacterEffectTargets(targets);
    for (auto &pet : retiredPets)
        pet->EffectsRetired();
    for (auto &pet : retiredHelperPets)
        pet->EffectsRetired();
}

void SessionGameplayUnit::ClearCharacters(int Key)
{
    std::vector<OBJECT *> targets;
    std::vector<std::unique_ptr<CHARACTER>> retiredCharacters;
    std::vector<std::unique_ptr<CharacterLinkedItemVisual>> retiredItems;
    std::vector<std::unique_ptr<CSIPartsMDL>> retiredParts;
    std::vector<std::unique_ptr<CSPetSystem>> retiredPets;
    std::vector<std::unique_ptr<CharacterMountVisual>> retiredMounts;
    std::vector<std::shared_ptr<PetObject>> retiredHelperPets;
    for (int i = CharactersClient.Size() - 1; i >= 0; --i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        OBJECT *o = &c->Object;
        if (c->Key == Key)
            continue;
        const int characterKey = c->Key;
        const bool controlled = CharactersClient.IsControlled(i);
        std::unique_ptr<CHARACTER> retired;

        targets.push_back(o);
        CharacterPresentationDetail::CollectLinkedItemRetirement(CharactersClient.WorldVisuals(i),
                                                                 targets, retiredItems);
        CharacterPresentationDetail::CollectPartsRetirement(CharactersClient.WorldVisuals(i),
                                                            targets, retiredParts);
        CharacterPresentationDetail::RetireClothAndAfterImages(CharactersClient.WorldVisuals(i));
        CharacterPresentationDetail::CollectPetRetirement(CharactersClient.WorldVisuals(i), targets,
                                                          retiredPets);
        CharacterPresentationDetail::CollectHelperPetRetirement(CharactersClient.WorldVisuals(i),
                                                                targets, retiredHelperPets);
        CharacterPresentationDetail::CollectMountRetirement(CharactersClient.WorldVisuals(i),
                                                            targets, retiredMounts);
        DetachCharacterBindings(c);

        if (!controlled)
        {
            retired = CharactersClient.RemoveByKey(characterKey);
            if (retired == nullptr)
                continue;
            c = retired.get();
            o = &c->Object;
        }

        o->Live = false;
        DeletePet(c);
        DeleteCloth(c, o);
        DeleteParts(c);
        if (retired)
            retiredCharacters.push_back(std::move(retired));
    }
    // Last-observer characters stay alive until every target has been retired.
    RetireCharacterEffectTargets(targets);
    for (auto &pet : retiredPets)
        pet->EffectsRetired();
    for (auto &pet : retiredHelperPets)
        pet->EffectsRetired();
}

void SessionGameplayUnit::DeleteCharacter(int Key)
{
    const int index = CharactersClient.FindIndexByKey(Key);
    if (!CharactersClient.IsValidIndex(index))
        return;

    CHARACTER *c = &CharactersClient[index];
    OBJECT *o = &c->Object;
    DetachCharacterVisuals(c);

    const bool shared = CharactersClient.IsShared(index);
    std::unique_ptr<CHARACTER> retired;
    if (!CharactersClient.IsControlled(index))
    {
        retired = CharactersClient.RemoveByKey(Key);
        if (shared && retired == nullptr)
            return;
        if (retired != nullptr)
        {
            c = retired.get();
            o = &c->Object;
        }
    }

    o->Live = false;
    DeletePet(c);
    DeleteCloth(c, o);
    DeleteParts(c);
}

void SessionGameplayUnit::DeleteCharacter(CHARACTER *c, OBJECT *o)
{
    const int index = CharactersClient.FindIndexByKey(c->Key);
    if (CharactersClient.IsValidIndex(index) && &CharactersClient[index] == c &&
        !CharactersClient.IsControlled(index))
    {
        DeleteCharacter(c->Key);
        return;
    }

    DetachCharacterVisuals(c);
    o->Live = false;

    DeletePet(c);
    DeleteCloth(c, o);
    DeleteParts(c);
}

int SessionGameplayUnit::FindCharacterIndex(int Key)
{
    const int index = CharactersClient.FindIndexByKey(Key);
    return CharactersClient.IsValidIndex(index) && CharactersClient[index].Object.Live ? index : -1;
}

int SessionGameplayUnit::FindCharacterIndexByMonsterIndex(int Type)
{
    return CharactersClient.FindIndexByMonsterType(Type);
}

int SessionGameplayUnit::HangerBloodCastleQuestItem(int Key)
{
    int index = -1;
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        if (c->Object.Live && c->Key == Key)
        {
            index = i;
        }
        c->EtcPart = 0;
    }
    return index;
}

void SessionGameplayUnit::SetAllAction(int Action)
{
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        if (c->Object.Live && c->Object.Type == MODEL_PLAYER)
        {
            c->Object.EnableBoneMatrix = false;
            Vector(0.f, 0.f, 180.f, c->Object.Angle);
            SetAction(&c->Object, Action);
        }
    }
}

void SessionGameplayUnit::ReleaseCharacters()
{
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        if (!CharactersClient.IsValidIndex(i) ||
            (CharactersClient.IsShared(i) && CharactersClient.ObserverCount(i) > 1))
        {
            continue;
        }
        CHARACTER *c = &CharactersClient[i];
        OBJECT *o = &c->Object;
        sessionKeeper_.Visual()->DetachCharacterChats(c);
        delete[] o->BoneTransform;
        o->BoneTransform = nullptr;
        DeletePet(c);
        DeleteCloth(c, o);
        DeleteParts(c);
    }
    OBJECT *o = &CharacterView.Object;
    if (o->BoneTransform != NULL)
    {
        delete[] o->BoneTransform;
        o->BoneTransform = NULL;
    }
    DeletePet(&CharacterView);
    DeleteCloth(&CharacterView, o);
    DeleteParts(&CharacterView);

    boneManager_.UnregisterAll();
}

void SessionGameplayUnit::CreateCharacterPointer(CHARACTER *c, int Type, unsigned char PositionX,
                                                 unsigned char PositionY, float Rotation)
{
    c->ResetPresentationIdentity();
    boneManager_.Admit(&c->Object, c);
    OBJECT *o = &c->Object;
    c->PositionX = PositionX;
    c->PositionY = PositionY;
    c->TargetX = PositionX;
    c->TargetY = PositionY;
    if (c != Hero)
    {
        c->byExtensionSkill = 0;
    }

    DeletePet(c);

    int Index = TERRAIN_INDEX_REPEAT((c->PositionX), (c->PositionY));
    if ((TerrainWall[Index] & TW_SAFEZONE) == TW_SAFEZONE)
        c->SafeZone = true;
    else
        c->SafeZone = false;

    c->Path.PathNum = 0;
    c->Path.CurrentPath = 0;
    c->Movement = false;
    o->Live = true;
    o->Visible = false;
    o->AlphaEnable = true;
    o->LightEnable = true;
    o->ContrastEnable = false;
    o->EnableBoneMatrix = true;
    o->EnableShadow = false;
    c->Dead = 0;
    c->Blood = false;
    c->GuildTeam = 0;
    c->Run = 0;
    c->GuildMarkIndex = -1;
    c->PK = PVP_NEUTRAL;
    o->Type = Type;
    o->Scale = 0.9f;
    o->Timer = 0.f;
    o->Alpha = 1.f;
    o->AlphaTarget = 1.f;
    o->Velocity = 0.f;
    o->ShadowScale = 0.f;
    o->m_byHurtByDeathstab = 0;
    o->AI = 0;
    o->m_byBuildTime = 10;
    c->m_iDeleteTime = -128;
    o->m_bRenderShadow = true;
    o->m_fEdgeScale = 1.2f;
    c->m_bIsSelected = true;
    c->ExtendState = 0;
    c->ExtendStateTime = 0;
    c->m_byGensInfluence = 0;
    c->GuildStatus = -1;
    c->GuildType = 0;
    c->ProtectGuildMarkWorldTime = 0.0f;
    c->GuildRelationShip = 0;
    c->GuildSkill = AT_SKILL_STUN;
    c->BackupCurrentSkill = 255;
    c->GuildMasterKillCount = 0;
    c->m_byDieType = 0;
    o->m_bActionStart = false;
    o->m_bySkillCount = 0;
    c->NotRotateOnMagicHit = false;
    c->CtlCode = 0;
    c->m_CursedTempleCurSkill = AT_SKILL_CURSED_TEMPLE_PRODECTION;
    c->m_CursedTempleCurSkillPacket = false;
    c->HealthStatus = -1;
    c->ShieldStatus = -1;

    if (Type < MODEL_FACE || Type > MODEL_FACE + 6)
    {
        c->Class = CLASS_WIZARD;
    }

    if (Type == MODEL_PLAYER)
    {
        o->PriorAction = 1;
        o->CurrentAction = 1;
    }
    else
    {
        o->PriorAction = 0;
        o->CurrentAction = 0;
    }
    o->AnimationFrame = 0.002f;
    o->PriorAnimationFrame = 0;
    c->JumpTime = 0;
    o->HiddenMesh = -1;
    c->MoveSpeed = 10;

    g_CharacterClearBuff(o);

    o->Teleport = TELEPORT_NONE;
    o->Kind = KIND_PLAYER;
    c->Change = false;
    o->SubType = 0;
    c->MonsterIndex = MONSTER_UNDEFINED;
    o->BlendMeshTexCoordU = 0.f;
    o->BlendMeshTexCoordV = 0.f;
    c->Skill = 0;
    c->AttackTime = 0;
    c->LastAttackEffectTime = -1;
    SetCharacterTarget(*c, -1);
    c->AttackFlag = ATTACK_FAIL;
    c->Weapon[0].Type = -1;
    c->Weapon[0].Level = 0;
    c->Weapon[1].Type = -1;
    c->Weapon[1].Level = 0;
    c->Wing.Type = -1;
    c->Helper.Type = -1;

    o->Position[0] = (float)((c->PositionX) * TERRAIN_SCALE) + 0.5f * TERRAIN_SCALE;
    o->Position[1] = (float)((c->PositionY) * TERRAIN_SCALE) + 0.5f * TERRAIN_SCALE;

    o->InitialSceneTime = WorldTime;

    if (gMapManager.ContextMap() == -1 || c->Helper.Type != MODEL_HORN_OF_DINORANT || c->SafeZone)
    {
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
    }
    else
    {
        if (TheMapProcess().CharacterPolicy().flyingMounts)
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 90.f;
        else
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 30.f;
    }

    Vector(0.f, 0.f, Rotation, o->Angle);
    Vector(0.f, 0.f, 0.f, c->Light);
    Vector(0.f, 0.f, 0.f, o->Light);
    Vector(-60.f, -60.f, 0.f, o->BoundingBoxMin);
    switch (Type)
    {
    case MODEL_PLAYER:
        Vector(40.f, 40.f, 120.f, o->BoundingBoxMax);
        break;
    case MODEL_CHAOS_CASTLE_KNIGHT:
    case MODEL_CHAOS_CASTLE_ELF:
    case MODEL_CHAOS_CASTLE_WIZARD:
        Vector(40.f, 40.f, 120.f, o->BoundingBoxMax);
        break;
    case MODEL_BUDGE_DRAGON:
    case MODEL_LARVA:
    case MODEL_SPIDER:
    case MODEL_CHAIN_SCORPION:
    case MODEL_GOBLIN:
    case MODEL_WORM:
        Vector(50.f, 50.f, 80.f, o->BoundingBoxMax);
        break;
    case MODEL_GORGON:
    case MODEL_DRAGON_:
    case MODEL_TITAN:
    case MODEL_TANTALLOS:
    case MODEL_BEAM_KNIGHT:
        Vector(70.f, 70.f, 250.f, o->BoundingBoxMax);
        break;
    case MODEL_HYDRA:
        Vector(100.f, 100.f, 150.f, o->BoundingBoxMax);
        break;
    case MODEL_CASTLE_GATE:
        Vector(-120.f, -120.f, 0.f, o->BoundingBoxMin);
        Vector(100.f, 100.f, 300.f, o->BoundingBoxMax);
        break;
    case MODEL_STATUE_OF_SAINT:
        Vector(-90.f, -50.f, 0.f, o->BoundingBoxMin);
        Vector(90.f, 50.f, 200.f, o->BoundingBoxMax);
        break;
    case MODEL_SELUPAN:
        Vector(-150.f, -150.f, 0.f, o->BoundingBoxMin);
        Vector(150.f, 150.f, 400.f, o->BoundingBoxMax);
        break;
    case MODEL_SPIDER_EGGS_1:
    case MODEL_SPIDER_EGGS_2:
    case MODEL_SPIDER_EGGS_3:
        Vector(-100.f, -100.f, 0.f, o->BoundingBoxMin);
        Vector(100.f, 100.f, 200.f, o->BoundingBoxMax);
        break;
    case MODEL_LITTLESANTA:
    case MODEL_LITTLESANTA + 1:
    case MODEL_LITTLESANTA + 2:
    case MODEL_LITTLESANTA + 3:
    case MODEL_LITTLESANTA + 4:
    case MODEL_LITTLESANTA + 5:
    case MODEL_LITTLESANTA + 6:
    case MODEL_LITTLESANTA + 7: {
        Vector(-160.f, -60.f, -20.f, o->BoundingBoxMin);
        Vector(10.f, 80.f, 50.f, o->BoundingBoxMax);
    }
    break;
    case MODEL_ZOMBIE_FIGHTER: {
        Vector(-100.f, -70.f, 0.f, o->BoundingBoxMin);
        Vector(100.f, 70.f, 150.f, o->BoundingBoxMax);
    }
    break;
    case MODEL_GLADIATOR: {
        Vector(-100.f, -100.f, 50.f, o->BoundingBoxMin);
        Vector(100.f, 100.f, 150.f, o->BoundingBoxMax);
    }
    break;
    case MODEL_SLAUGHTERER: {
        Vector(-100.f, -100.f, 0.f, o->BoundingBoxMin);
        Vector(100.f, 100.f, 180.f, o->BoundingBoxMax);
    }
    break;
    case MODEL_BLOOD_ASSASSIN: {
        Vector(-80.f, -80.f, 0.f, o->BoundingBoxMin);
        Vector(80.f, 80.f, 130.f, o->BoundingBoxMax);
    }
    break;
    case MODEL_CRUEL_BLOOD_ASSASSIN: {
        Vector(-80.f, -80.f, 0.f, o->BoundingBoxMin);
        Vector(80.f, 80.f, 130.f, o->BoundingBoxMax);
    }
    break;
    case MODEL_LAVA_GIANT: {
        Vector(-100.f, -80.f, 50.f, o->BoundingBoxMin);
        Vector(100.f, 70.f, 280.f, o->BoundingBoxMax);
    }
    break;
    case MODEL_BURNING_LAVA_GIANT: {
        Vector(-100.f, -80.f, 50.f, o->BoundingBoxMin);
        Vector(100.f, 70.f, 280.f, o->BoundingBoxMax);
    }
    break;
    default:
        Vector(50.f, 50.f, 150.f, o->BoundingBoxMax);
        break;
    }
    CharacterPresentationDetail::ResetCharacterSelectionBounds(*o);

    if (o->BoneTransform != NULL)
    {
        delete[] o->BoneTransform;
        o->BoneTransform = NULL;
    }
    o->BoneTransform = new vec34_t[Models[Type].NumBones];

    for (int i = 0; i < 2; i++)
    {
        c->Weapon[i].Type = -1;
        c->Weapon[i].Level = 0;
        c->Weapon[i].ExcellentFlags = 0;
    }

    for (int i = 0; i < MAX_BODYPART; i++)
    {
        c->BodyPart[i].Type = -1;
        c->BodyPart[i].Level = 0;
        c->BodyPart[i].ExcellentFlags = 0;
        c->BodyPart[i].AncientDiscriminator = 0;
    }
    c->Wing.Type = -1;
    c->Helper.Type = -1;
    c->Flag.Type = -1;

    c->LongRangeAttack = -1;
    c->CollisionTime = 0;
    o->CollisionRange = 200.f;
    c->Rot = 0.f;
    c->Level = 0;
    c->Item = -1;

    for (int i = 0; i < 32; ++i)
        c->OwnerID[i] = 0;

    o->BlendMesh = -1;
    o->BlendMeshLight = 1.f;
    switch (Type)
    {
    case MODEL_CHAOS_CASTLE_KNIGHT:
    case MODEL_CHAOS_CASTLE_ELF:
    case MODEL_CHAOS_CASTLE_WIZARD:
        c->Weapon[0].LinkBone = 33;
        c->Weapon[1].LinkBone = 42;
        break;
    case MODEL_RED_SKELETON_KNIGHT:
        c->Weapon[0].LinkBone = 30;
        c->Weapon[1].LinkBone = 39;
        break;
    case MODEL_DARK_SKULL_SOLDIER:
        c->Weapon[0].LinkBone = 33;
        c->Weapon[1].LinkBone = 20;
        break;
    case MODEL_STATUE_OF_SAINT:
        c->Weapon[0].LinkBone = 1;
        c->Weapon[1].LinkBone = 1;
        break;
    case MODEL_DARK_PHEONIX_SHIELD:
        c->Weapon[0].LinkBone = 27;
        c->Weapon[1].LinkBone = 18;
        break;
    case MODEL_CRUST:
        c->Weapon[0].LinkBone = 36;
        c->Weapon[1].LinkBone = 45;
        break;
    case MODEL_PHANTOM_KNIGHT:
        c->Weapon[0].LinkBone = 30;
        c->Weapon[1].LinkBone = 39;
        break;
    case MODEL_ORC_ARCHER:
        c->Weapon[0].LinkBone = 39;
        c->Weapon[1].LinkBone = 39;
        break;
    case MODEL_ORC:
        c->Weapon[0].LinkBone = 27;
        c->Weapon[1].LinkBone = 38;
        break;
    case MODEL_CURSED_KING:
        c->Weapon[0].LinkBone = 32;
        c->Weapon[1].LinkBone = 43;
        break;
    case MODEL_BEAM_KNIGHT:
        c->Weapon[0].LinkBone = 55;
        c->Weapon[1].LinkBone = 70;
        break;
    case MODEL_TANTALLOS:
        c->Weapon[0].LinkBone = 43;
        break;
    case MODEL_GOLDEN_WHEEL:
        c->Weapon[0].LinkBone = 23;
        break;
    case MODEL_LIZARD:
        c->Weapon[0].LinkBone = 52;
        c->Weapon[1].LinkBone = 65;
        break;
    case MODEL_VALKYRIE:
        c->Weapon[0].LinkBone = 30;
        c->Weapon[1].LinkBone = 39;
        break;
    case MODEL_VEPAR:
        c->Weapon[0].LinkBone = 30;
        c->Weapon[1].LinkBone = 39;
        break;
    case MODEL_DEVIL:
        c->Weapon[0].LinkBone = 16;
        c->Weapon[1].LinkBone = 25;
        break;
    case MODEL_DEATH_KNIGHT:
        c->Weapon[0].LinkBone = 30;
        c->Weapon[1].LinkBone = 39;
        break;
    case MODEL_BALROG:
        c->Weapon[0].LinkBone = 17;
        c->Weapon[1].LinkBone = 28;
        break;
    case MODEL_AGON:
        c->Weapon[0].LinkBone = 39;
        c->Weapon[1].LinkBone = 30;
        break;
    case MODEL_HUNTER:
        c->Weapon[0].LinkBone = 25;
        c->Weapon[1].LinkBone = 16;
        break;
    case MODEL_BEETLE_MONSTER:
        c->Weapon[0].LinkBone = 24;
        c->Weapon[1].LinkBone = 19;
        break;
    case MODEL_GOBLIN:
        c->Weapon[0].LinkBone = 31;
        c->Weapon[1].LinkBone = 22;
        break;
    case MODEL_ICE_QUEEN:
        c->Weapon[0].LinkBone = 26;
        c->Weapon[1].LinkBone = 35;
        break;
    case MODEL_HOMMERD:
    case MODEL_GORGON:
        c->Weapon[0].LinkBone = 30;
        c->Weapon[1].LinkBone = 39;
        break;
    case MODEL_DARK_KNIGHT:
        c->Weapon[0].LinkBone = 26;
        c->Weapon[1].LinkBone = 36;
        break;
    case MODEL_BULL_FIGHTER:
    case MODEL_DEATH_COW:
        c->Weapon[0].LinkBone = 42;
        c->Weapon[1].LinkBone = 33;
        break;
    case MODEL_CYCLOPS:
    case MODEL_LICH:
    case MODEL_GIANT:
        c->Weapon[0].LinkBone = 41;
        c->Weapon[1].LinkBone = 32;
        break;
    case MODEL_HOUND:
        c->Weapon[0].LinkBone = 19;
        c->Weapon[1].LinkBone = 14;
        break;
    case MODEL_HELL_SPIDER:
        c->Weapon[0].LinkBone = 29;
        c->Weapon[1].LinkBone = 38;
        break;
    case MODEL_SOLDIER:
        c->Weapon[0].LinkBone = 20;
        c->Weapon[1].LinkBone = 33;
        break;
    default:
        if (TheMapProcess().ConfigureMonsterLinks(c, Type))
            return;
        c->Weapon[0].LinkBone = 33;
        c->Weapon[1].LinkBone = 42;
        break;
    }
}

CHARACTER *SessionGameplayUnit::CreateCharacter(int Key, int Type, unsigned char PositionX,
                                                unsigned char PositionY, float Rotation)
{
    SessionCharacterPopulationStorage::Acquisition acquisition;
    if (CharactersClient.HasWorldInstance() && Key > 0 && Key != HeroKey)
    {
        acquisition = CharactersClient.ObserveRemote(Key);
    }
    else
    {
        acquisition = {CharactersClient.AcquireLocalByKey(Key), true};
    }
    CHARACTER *c = acquisition.character;
    if (c == nullptr)
        return nullptr;

    OBJECT *o = &c->Object;
    if (o->Live)
    {
        boneManager_.UnregisterBone(c);
        DeletePet(c);
        DeleteCloth(c, o);
        DeleteParts(c);
    }
    CreateCharacterPointer(c, Type, PositionX, PositionY, Rotation);
    g_CharacterClearBuff(o);
    c->Key = static_cast<SHORT>(Key);
    return c;
}

void SessionGameplayUnit::SetCharacterScale(CHARACTER *c)
{
    c->MarkAppearanceChanged();
    if (c->Change)
        return;

    const float priorScale = c->Object.Scale;
    CharacterPresentationDetail::SetCharacterHeadPart(*c);

    if (SceneFlag == CHARACTER_SCENE)
    {
        switch (gCharacterManager.GetBaseClass(c->Class))
        {
        case CLASS_RAGEFIGHTER:
            c->Object.Scale = 1.35f;
            break;
        default:
            c->Object.Scale = 1.2f;
            break;
        }
    }
    else
    {
        if (c->Skin == 0)
        {
            switch (gCharacterManager.GetBaseClass(c->Class))
            {
            case CLASS_WIZARD:
                c->Object.Scale = 0.9f;
                break;
            case CLASS_KNIGHT:
                c->Object.Scale = 0.9f;
                break;
            case CLASS_ELF:
                c->Object.Scale = 0.88f;
                break;
            case CLASS_DARK:
                c->Object.Scale = 0.95f;
                break;
            case CLASS_DARK_LORD:
                c->Object.Scale = 0.92f;
                break;
            case CLASS_SUMMONER:
                c->Object.Scale = 0.90f;
                break;
            case CLASS_RAGEFIGHTER:
                c->Object.Scale = 1.03f;
                break;
            }
        }
        else
        {
            switch (gCharacterManager.GetBaseClass(c->Class))
            {
            case CLASS_WIZARD:
                c->Object.Scale = 0.93f;
                break;
            case CLASS_KNIGHT:
                c->Object.Scale = 0.93f;
                break;
            case CLASS_ELF:
                c->Object.Scale = 0.86f;
                break;
            case CLASS_DARK:
                c->Object.Scale = 0.95f;
                break;
            case CLASS_DARK_LORD:
                c->Object.Scale = 0.92f;
                break;
            case CLASS_SUMMONER:
                c->Object.Scale = 0.90f;
                break;
            case CLASS_RAGEFIGHTER:
                c->Object.Scale = 1.03f;
                break;
            }
        }
    }
    if (c->Object.Scale != priorScale)
        c->Object.EnableBoneMatrix = false;
}

void SessionGameplayUnit::SetCharacterClass(CHARACTER *c)
{
    if (c->Object.Type != MODEL_PLAYER)
    {
        return;
    }

    ITEM *p = CharacterMachine->Equipment;

    if (p[EQUIPMENT_WEAPON_RIGHT].Type == -1)
    {
        c->Weapon[0].Type = -1;
    }
    else
    {
        c->Weapon[0].Type = p[EQUIPMENT_WEAPON_RIGHT].Type + MODEL_ITEM;
    }

    if (p[EQUIPMENT_WEAPON_LEFT].Type == -1)
    {
        c->Weapon[1].Type = -1;
    }
    else
    {
        c->Weapon[1].Type = p[EQUIPMENT_WEAPON_LEFT].Type + MODEL_ITEM;
    }

    if (p[EQUIPMENT_WING].Type == -1)
    {
        c->Wing.Type = -1;
    }
    else
    {
        c->Wing.Type = p[EQUIPMENT_WING].Type + MODEL_ITEM;
    }

    if (p[EQUIPMENT_HELPER].Type == -1)
    {
        c->Helper.Type = -1;
    }
    else
    {
        c->Helper.Type = p[EQUIPMENT_HELPER].Type + MODEL_ITEM;
    }

    c->Weapon[0].Level = p[EQUIPMENT_WEAPON_RIGHT].Level;
    c->Weapon[1].Level = p[EQUIPMENT_WEAPON_LEFT].Level;
    c->Weapon[0].ExcellentFlags = p[EQUIPMENT_WEAPON_RIGHT].ExcellentFlags;
    c->Weapon[1].ExcellentFlags = p[EQUIPMENT_WEAPON_LEFT].ExcellentFlags;
    c->Weapon[0].AncientDiscriminator = p[EQUIPMENT_WEAPON_RIGHT].AncientDiscriminator;
    c->Weapon[1].AncientDiscriminator = p[EQUIPMENT_WEAPON_LEFT].AncientDiscriminator;
    c->Wing.Level = p[EQUIPMENT_WING].Level;
    c->Helper.Level = p[EQUIPMENT_HELPER].Level;

    bool Success = true;

    if (gMapManager.InChaosCastle() == true)
        Success = false;

    if (Engine::Object::IsSitOrPoseAction(c->Object.CurrentAction))
    {
        Success = false;
    }
    if (Engine::Object::IsAttackAction(c->Object.CurrentAction))
    {
        Success = false;
    }

    if (Success)
    {
        SetPlayerStop(c);
    }

    if (p[EQUIPMENT_HELM].Type == -1)
    {
        c->BodyPart[BODYPART_HELM].Type = static_cast<int>(MODEL_BODY_HELM) + c->SkinIndex;
        c->BodyPart[BODYPART_HELM].Level = 0;
        c->BodyPart[BODYPART_HELM].ExcellentFlags = 0;
        c->BodyPart[BODYPART_HELM].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_HELM].Type = p[EQUIPMENT_HELM].Type + MODEL_ITEM;
        c->BodyPart[BODYPART_HELM].Level = p[EQUIPMENT_HELM].Level;
        c->BodyPart[BODYPART_HELM].ExcellentFlags = p[EQUIPMENT_HELM].ExcellentFlags;
        c->BodyPart[BODYPART_HELM].AncientDiscriminator = p[EQUIPMENT_HELM].AncientDiscriminator;
    }

    if (p[EQUIPMENT_ARMOR].Type == -1)
    {
        c->BodyPart[BODYPART_ARMOR].Type = static_cast<int>(MODEL_BODY_ARMOR) + c->SkinIndex;
        c->BodyPart[BODYPART_ARMOR].Level = 0;
        c->BodyPart[BODYPART_ARMOR].ExcellentFlags = 0;
        c->BodyPart[BODYPART_ARMOR].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_ARMOR].Type = p[EQUIPMENT_ARMOR].Type + MODEL_ITEM;
        c->BodyPart[BODYPART_ARMOR].Level = p[EQUIPMENT_ARMOR].Level;
        c->BodyPart[BODYPART_ARMOR].ExcellentFlags = p[EQUIPMENT_ARMOR].ExcellentFlags;
        c->BodyPart[BODYPART_ARMOR].AncientDiscriminator = p[EQUIPMENT_ARMOR].AncientDiscriminator;
    }

    if (p[EQUIPMENT_PANTS].Type == -1)
    {
        c->BodyPart[BODYPART_PANTS].Type = static_cast<int>(MODEL_BODY_PANTS) + c->SkinIndex;
        c->BodyPart[BODYPART_PANTS].Level = 0;
        c->BodyPart[BODYPART_PANTS].ExcellentFlags = 0;
        c->BodyPart[BODYPART_PANTS].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_PANTS].Type = p[EQUIPMENT_PANTS].Type + MODEL_ITEM;
        c->BodyPart[BODYPART_PANTS].Level = p[EQUIPMENT_PANTS].Level;
        c->BodyPart[BODYPART_PANTS].ExcellentFlags = p[EQUIPMENT_PANTS].ExcellentFlags;
        c->BodyPart[BODYPART_PANTS].AncientDiscriminator = p[EQUIPMENT_PANTS].AncientDiscriminator;
    }

    if (p[EQUIPMENT_GLOVES].Type == -1)
    {
        c->BodyPart[BODYPART_GLOVES].Type = static_cast<int>(MODEL_BODY_GLOVES) + c->SkinIndex;
        c->BodyPart[BODYPART_GLOVES].Level = 0;
        c->BodyPart[BODYPART_GLOVES].ExcellentFlags = 0;
        c->BodyPart[BODYPART_GLOVES].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_GLOVES].Type = p[EQUIPMENT_GLOVES].Type + MODEL_ITEM;
        c->BodyPart[BODYPART_GLOVES].Level = p[EQUIPMENT_GLOVES].Level;
        c->BodyPart[BODYPART_GLOVES].ExcellentFlags = p[EQUIPMENT_GLOVES].ExcellentFlags;
        c->BodyPart[BODYPART_GLOVES].AncientDiscriminator =
            p[EQUIPMENT_GLOVES].AncientDiscriminator;
    }

    if (p[EQUIPMENT_BOOTS].Type == -1)
    {
        c->BodyPart[BODYPART_BOOTS].Type = static_cast<int>(MODEL_BODY_BOOTS) + c->SkinIndex;
        c->BodyPart[BODYPART_BOOTS].Level = 0;
        c->BodyPart[BODYPART_BOOTS].ExcellentFlags = 0;
        c->BodyPart[BODYPART_BOOTS].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_BOOTS].Type = p[EQUIPMENT_BOOTS].Type + MODEL_ITEM;
        c->BodyPart[BODYPART_BOOTS].Level = p[EQUIPMENT_BOOTS].Level;
        c->BodyPart[BODYPART_BOOTS].ExcellentFlags = p[EQUIPMENT_BOOTS].ExcellentFlags;
        c->BodyPart[BODYPART_BOOTS].AncientDiscriminator = p[EQUIPMENT_BOOTS].AncientDiscriminator;
    }

    ChangeChaosCastleUnit(c);

    SetCharacterScale(c);

    if (c == Hero)
    {
        CheckFullSet(Hero);
    }

    CharacterMachine->CalculateAll();
}

void SessionGameplayUnit::SetChangeClass(CHARACTER *c)
{
    if (c->Object.Type != MODEL_PLAYER)
        return;

    bool Success = true;

    if (Engine::Object::IsSitOrPoseAction(c->Object.CurrentAction))
        Success = false;
    if (Engine::Object::IsAttackAction(c->Object.CurrentAction))
        Success = false;
    if (Success)
        SetPlayerStop(c);

    if (c->BodyPart[BODYPART_HELM].Type >= MODEL_BODY_HELM)
    {
        c->BodyPart[BODYPART_HELM].Type = static_cast<int>(MODEL_BODY_HELM) + c->SkinIndex;
        c->BodyPart[BODYPART_HELM].Level = 0;
        c->BodyPart[BODYPART_HELM].ExcellentFlags = 0;
        c->BodyPart[BODYPART_HELM].AncientDiscriminator = 0;
    }
    if (c->BodyPart[BODYPART_ARMOR].Type >= MODEL_BODY_ARMOR)
    {
        c->BodyPart[BODYPART_ARMOR].Type = static_cast<int>(MODEL_BODY_ARMOR) + c->SkinIndex;
        c->BodyPart[BODYPART_ARMOR].Level = 0;
        c->BodyPart[BODYPART_ARMOR].ExcellentFlags = 0;
        c->BodyPart[BODYPART_ARMOR].AncientDiscriminator = 0;
    }
    if (c->BodyPart[BODYPART_PANTS].Type >= MODEL_BODY_PANTS)
    {
        c->BodyPart[BODYPART_PANTS].Type = static_cast<int>(MODEL_BODY_PANTS) + c->SkinIndex;
        c->BodyPart[BODYPART_PANTS].Level = 0;
        c->BodyPart[BODYPART_PANTS].ExcellentFlags = 0;
        c->BodyPart[BODYPART_PANTS].AncientDiscriminator = 0;
    }
    if (c->BodyPart[BODYPART_GLOVES].Type >= MODEL_BODY_GLOVES)
    {
        c->BodyPart[BODYPART_GLOVES].Type = static_cast<int>(MODEL_BODY_GLOVES) + c->SkinIndex;
        c->BodyPart[BODYPART_GLOVES].Level = 0;
        c->BodyPart[BODYPART_GLOVES].ExcellentFlags = 0;
        c->BodyPart[BODYPART_GLOVES].AncientDiscriminator = 0;
    }
    if (c->BodyPart[BODYPART_BOOTS].Type >= MODEL_BODY_BOOTS)
    {
        c->BodyPart[BODYPART_BOOTS].Type = static_cast<int>(MODEL_BODY_BOOTS) + c->SkinIndex;
        c->BodyPart[BODYPART_BOOTS].Level = 0;
        c->BodyPart[BODYPART_BOOTS].ExcellentFlags = 0;
        c->BodyPart[BODYPART_BOOTS].AncientDiscriminator = 0;
    }

    SetCharacterScale(c);
}

DWORD GetGuildRelationShipTextColor(BYTE GuildRelationShip)
{
    DWORD dwColor = 0;

    if (GuildRelationShip == GR_NONE)
        dwColor = (255 << 24) + (255 << 16) + (230 << 8) + (230);
    else if (GuildRelationShip == GR_RIVAL || GuildRelationShip == GR_RIVALUNION)
        dwColor = (255 << 24) + (0 << 16) + (30 << 8) + (255);
    else
        dwColor = (255 << 24) + (0 << 16) + (255 << 8) + (200);

    return dwColor;
}

DWORD GetGuildRelationShipBGColor(BYTE GuildRelationShip)
{
    DWORD dwColor = 0;

    if (GuildRelationShip == GR_NONE)
        dwColor = (150 << 24) + (50 << 16) + (30 << 8) + (10);
    else if (GuildRelationShip == GR_RIVAL || GuildRelationShip == GR_RIVALUNION)
        dwColor = (150 << 24) + (0 << 16) + (0 << 8) + (0);
    else
        dwColor = (150 << 24) + (80 << 16) + (50 << 8) + (20);

    return dwColor;
}

CHARACTER *SessionGameplayUnit::FindCharacterByID(wchar_t *szName)
{
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        if (c->Object.Live && !wcscmp(szName, c->ID))
        {
            return c;
        }
    }
    return NULL;
}

CHARACTER *SessionGameplayUnit::FindCharacterByKey(int Key)
{
    const int index = FindCharacterIndex(Key);
    return CharactersClient.IsValidIndex(index) ? &CharactersClient[index] : nullptr;
}

int LevelConvert(BYTE Level)
{
    switch (Level)
    {
    case 0:
        return 0;
        break;
    case 1:
        return 3;
        break;
    case 2:
        return 5;
        break;
    case 3:
        return 7;
        break;
    case 4:
        return 9;
        break;
    case 5:
        return 11;
        break;
    case 6:
        return 13;
        break;
    case 7:
        return 15;
        break;
    }
    return 0;
}

void MakeElfHelper(CHARACTER *c)
{
    OBJECT *o = &c->Object;
    c->Wing.Type = MODEL_WINGS_OF_SPIRITS;
    c->BodyPart[BODYPART_HELM].Type = MODEL_RED_SPIRIT_HELM;
    c->BodyPart[BODYPART_ARMOR].Type = MODEL_RED_SPRIT_ARMOR;
    c->BodyPart[BODYPART_PANTS].Type = MODEL_RED_SPIRIT_PANTS;
    c->BodyPart[BODYPART_GLOVES].Type = MODEL_RED_SPIRIT_GLOVES;
    c->BodyPart[BODYPART_BOOTS].Type = MODEL_RED_SPIRIT_BOOTS;
    c->BodyPart[BODYPART_HELM].Level = 13;
    c->BodyPart[BODYPART_ARMOR].Level = 13;
    c->BodyPart[BODYPART_PANTS].Level = 13;
    c->BodyPart[BODYPART_GLOVES].Level = 13;
    c->BodyPart[BODYPART_BOOTS].Level = 13;

    o->Scale = 1.f;
    o->CurrentAction = PLAYER_STOP_FLY;
    o->BoundingBoxMax[2] += 70.f;
}

void SessionGameplayUnit::ChangeCharacterExt(int Key, BYTE *Equipment, CHARACTER *pCharacter,
                                             OBJECT *pHelper)
{
    CHARACTER *c;
    if (pCharacter == NULL)
        c = &CharactersClient[Key];
    else
        c = pCharacter;

    OBJECT *o = &c->Object;
    if (o->Type != MODEL_PLAYER)
        return;

    BYTE Type = 0;
    BYTE ExtBit = 0;
    short ExtType = 0;

    Type = Equipment[0];
    ExtType = Equipment[11] & 0xF0;
    ExtType = (ExtType << 4) | Type;

    int ItemLevels = ((int)Equipment[5] << 16) + ((int)Equipment[6] << 8) + ((int)Equipment[7]);
    c->Wing.Level = 0;
    c->Helper.Level = 0;
    if (ExtType == 0x0FFF)
    {
        c->Weapon[0].Type = -1;
        c->Weapon[0].ExcellentFlags = 0;
        c->Weapon[0].AncientDiscriminator = 0;
    }
    else
    {
        c->Weapon[0].Type = MODEL_SWORD + ExtType;
        c->Weapon[0].Level = LevelConvert((ItemLevels >> 0) & 7);
        c->Weapon[0].ExcellentFlags = (Equipment[9] & 4) / 4;
        c->Weapon[0].AncientDiscriminator = (Equipment[10] & 4) / 4;
    }

    Type = Equipment[1];
    ExtType = Equipment[12] & 240;
    ExtType = (ExtType << 4) | Type;

    if (ExtType == 0x0FFF)
    {
        c->Weapon[1].Type = -1;
        c->Weapon[1].ExcellentFlags = 0;
        c->Weapon[1].AncientDiscriminator = 0;
    }
    else
    {
        if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD &&
            (static_cast<int>(MODEL_LEGENDARY_STAFF) - MODEL_SWORD) == ExtType)
        {
            ITEM *pEquipmentItemSlot = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];
            PET_INFO *pPetInfo = GetPetInfo(pEquipmentItemSlot);
            CreatePetDarkSpirit(c);
            if (!gMapManager.InChaosCastle())
                if (auto *pet = ResolvePetSystem(c))
                    pet->SetPetInfo(pPetInfo);
        }
        else
        {
            c->Weapon[1].Type = MODEL_SWORD + ExtType;
        }

        c->Weapon[1].Level = LevelConvert((ItemLevels >> 3) & 7);
        c->Weapon[1].ExcellentFlags = (Equipment[9] & 2) / 2;
        c->Weapon[1].AncientDiscriminator = (Equipment[10] & 2) / 2;
    }

    Type = (Equipment[4] >> 2) & 3;

    //신규캐릭터 추가로 인한 날개 인덱스 확장 구조변경
    if (Type == 1) //1차 날개
    {
        Type = Equipment[8] & 0x07;
        switch (Type)
        {
        case 4:
            c->Wing.Type = MODEL_WING_OF_CURSE;
            break;
        default:
            c->Wing.Type = MODEL_WING + Type - 1;
            break;
        }
    }
    else if (Type == 2) //2차 날개
    {
        Type = Equipment[8] & 0x07;
        switch (Type)
        {
        case 5:
            c->Wing.Type = MODEL_CAPE_OF_LORD;
            break;
        case 6:
            c->Wing.Type = MODEL_WINGS_OF_DESPAIR;
            break;
        case 7:
            c->Wing.Type = MODEL_CAPE_OF_FIGHTER;
            break;
        default:
            c->Wing.Type = MODEL_WINGS_OF_SATAN + Type;
            break;
        }
    }
    else if (Type == 3) //3차 날개
    {
        Type = Equipment[8] & 0x07;
        switch (Type)
        {
        case 0: //작은날개
        {
            Type = (Equipment[16] >> 5);
            c->Wing.Type = MODEL_SEED_SPHERE_EARTH_5 + Type;
        }
        break;
        case 6:
            c->Wing.Type = MODEL_WING_OF_DIMENSION;
            break;
        case 7:
            c->Wing.Type = MODEL_CAPE_OF_OVERRULE;
            break;
        default:
            c->Wing.Type = MODEL_SCROLL_OF_FIRE_SCREAM + Type;
            break;
        }
    }
    else
    {
        c->Wing.Type = -1;
        c->Wing.ExcellentFlags = 0;
        c->Wing.AncientDiscriminator = 0;
    }

    if (pHelper == NULL)
    {
        DeleteMount(o);
        g_petProcess.DeletePet(c, c->Helper.Type - MODEL_ITEM, true);
    }
    else
    {
        pHelper->Live = false;
    }
    Type = Equipment[4] & 3;
    if (Type == 3)
    {
        Type = Equipment[9] & 0x01;
        if (Type == 1)
        {
            c->Helper.Type = MODEL_HORN_OF_DINORANT;
            if (pHelper == NULL)
                CreateMount(MODEL_PEGASUS, o->Position, o);
            else
                CreateMountSub(MODEL_PEGASUS, o->Position, o, pHelper);
        }
        else
        {
            c->Helper.Type = -1;
            c->Helper.ExcellentFlags = 0;
            c->Helper.AncientDiscriminator = 0;
        }
    }
    else
    {
        BYTE _temp = Equipment[15] & 0xE0;

        if (32 == _temp || 64 == _temp || 128 == _temp || 224 == _temp || 160 == _temp ||
            96 == _temp)
        {
            short _type = 0;
            switch (_temp)
            {
            case MONSTER_STONE_GOLEM:
                _type = 64;
                break;
            case MONSTER_ORC_ARCHER:
                _type = 65;
                break;
            case MONSTER_GIANT_OGRE_6:
                _type = 67;
                break;
            case MONSTER_GUARDSMAN:
                _type = 80;
                break;
            case MONSTER_SCHRIKER_1:
                _type = 106;
                break;
            case MONSTER_CHIEF_SKELETON_WARRIOR_3:
                _type = 123;
                break;
            }

            c->Helper.Type = MODEL_HELPER + _type;

            g_petProcess.CreatePet(ITEM_HELPER + _type, c->Helper.Type, o->Position, c);
        }
        else
        {
            c->Helper.Type = MODEL_HELPER + Type;
            int HelperType = 0;
            BOOL bCreateHelper = TRUE;
            switch (Type)
            {
            case 0:
                HelperType = MODEL_HELPER;
                break;
            case 2:
                HelperType = MODEL_UNICON;
                break;
            case 3:
                HelperType = MODEL_PEGASUS;
                break;
            default:
                bCreateHelper = FALSE;
                break;
            }
            if (bCreateHelper == TRUE)
            {
                if (pHelper == NULL)
                    CreateMount(HelperType, o->Position, o);
                else
                    CreateMountSub(HelperType, o->Position, o, pHelper);
            }
        }
    }

    Type = Equipment[11] & 0x01;
    if (Type == 1)
    {
        c->Helper.Type = MODEL_DARK_HORSE_ITEM;
        if (pHelper == NULL)
            CreateMount(MODEL_DARK_HORSE, o->Position, o);
        else
            CreateMountSub(MODEL_DARK_HORSE, o->Position, o, pHelper);
    }

    Type = Equipment[11] & 0x04;
    if (Type == 4)
    {
        c->Helper.Type = MODEL_HORN_OF_FENRIR;

        Type = Equipment[15] & 3;

        int iFenrirType = Equipment[16] & 1;
        if (iFenrirType == 1)
        {
            Type = 0x04;
        }

        c->Helper.ExcellentFlags = Type;
        if (Type == 0x01)
        {
            if (pHelper == NULL)
                CreateMount(MODEL_FENRIR_BLACK, o->Position, o);
            else
                CreateMountSub(MODEL_FENRIR_BLACK, o->Position, o, pHelper);
        }
        else if (Type == 0x02)
        {
            if (pHelper == NULL)
                CreateMount(MODEL_FENRIR_BLUE, o->Position, o);
            else
                CreateMountSub(MODEL_FENRIR_BLUE, o->Position, o, pHelper);
        }
        else if (Type == 0x04)
        {
            if (pHelper == NULL)
                CreateMount(MODEL_FENRIR_GOLD, o->Position, o);
            else
                CreateMountSub(MODEL_FENRIR_GOLD, o->Position, o, pHelper);
        }
        else
        {
            if (pHelper == NULL)
                CreateMount(MODEL_FENRIR_RED, o->Position, o);
            else
                CreateMountSub(MODEL_FENRIR_RED, o->Position, o, pHelper);
        }
    }

    DeleteParts(c);
    Type = (Equipment[11] >> 1) & 0x01;
    if (Type == 1)
    {
        if (c->EtcPart <= 0 || c->EtcPart > 3)
        {
            c->EtcPart = PARTS_LION;
        }
    }
    else
    {
        if (c->EtcPart <= 0 || c->EtcPart > 3)
        {
            c->EtcPart = 0;
        }
    }

    if (c->Change)
        return;

    ExtType = (Equipment[2] >> 4) + ((Equipment[8] >> 7) & 1) * 16 + (Equipment[12] & 15) * 32;
    if (ExtType == 0x1FF)
    {
        c->BodyPart[BODYPART_HELM].Type = static_cast<int>(MODEL_BODY_HELM) + c->SkinIndex;
        c->BodyPart[BODYPART_HELM].Level = 0;
        c->BodyPart[BODYPART_HELM].ExcellentFlags = 0;
        c->BodyPart[BODYPART_HELM].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_HELM].Type = MODEL_HELM + ExtType;
        c->BodyPart[BODYPART_HELM].Level = LevelConvert((ItemLevels >> 6) & 7);
        c->BodyPart[BODYPART_HELM].ExcellentFlags = (Equipment[9] & 128) / 128;
        c->BodyPart[BODYPART_HELM].AncientDiscriminator = (Equipment[10] & 128) / 128;
    }

    ExtType =
        (Equipment[2] & 15) + ((Equipment[8] >> 6) & 1) * 16 + ((Equipment[13] >> 4) & 15) * 32;
    if (ExtType == 0x1FF)
    {
        c->BodyPart[BODYPART_ARMOR].Type = static_cast<int>(MODEL_BODY_ARMOR) + c->SkinIndex;
        c->BodyPart[BODYPART_ARMOR].Level = 0;
        c->BodyPart[BODYPART_ARMOR].ExcellentFlags = 0;
        c->BodyPart[BODYPART_ARMOR].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_ARMOR + ExtType;
        c->BodyPart[BODYPART_ARMOR].Level = LevelConvert((ItemLevels >> 9) & 7);
        c->BodyPart[BODYPART_ARMOR].ExcellentFlags = (Equipment[9] & 64) / 64;
        c->BodyPart[BODYPART_ARMOR].AncientDiscriminator = (Equipment[10] & 64) / 64;
    }

    ExtType = (Equipment[3] >> 4) + ((Equipment[8] >> 5) & 1) * 16 + (Equipment[13] & 15) * 32;
    if (ExtType == 0x1FF)
    {
        c->BodyPart[BODYPART_PANTS].Type = static_cast<int>(MODEL_BODY_PANTS) + c->SkinIndex;
        c->BodyPart[BODYPART_PANTS].Level = 0;
        c->BodyPart[BODYPART_PANTS].ExcellentFlags = 0;
        c->BodyPart[BODYPART_PANTS].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_PANTS].Type = MODEL_PANTS + ExtType;
        c->BodyPart[BODYPART_PANTS].Level = LevelConvert((ItemLevels >> 12) & 7);
        c->BodyPart[BODYPART_PANTS].ExcellentFlags = (Equipment[9] & 32) / 32;
        c->BodyPart[BODYPART_PANTS].AncientDiscriminator = (Equipment[10] & 32) / 32;
    }

    ExtType =
        (Equipment[3] & 15) + ((Equipment[8] >> 4) & 1) * 16 + ((Equipment[14] >> 4) & 15) * 32;
    if (ExtType == 0x1FF)
    {
        c->BodyPart[BODYPART_GLOVES].Type = static_cast<int>(MODEL_BODY_GLOVES) + c->SkinIndex;
        c->BodyPart[BODYPART_GLOVES].Level = 0;
        c->BodyPart[BODYPART_GLOVES].ExcellentFlags = 0;
        c->BodyPart[BODYPART_GLOVES].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_GLOVES + ExtType;
        c->BodyPart[BODYPART_GLOVES].Level = LevelConvert((ItemLevels >> 15) & 7);
        c->BodyPart[BODYPART_GLOVES].ExcellentFlags = (Equipment[9] & 16) / 16;
        c->BodyPart[BODYPART_GLOVES].AncientDiscriminator = (Equipment[10] & 16) / 16;
    }

    ExtType = (Equipment[4] >> 4) + ((Equipment[8] >> 3) & 1) * 16 + (Equipment[14] & 15) * 32;
    if (ExtType == 0x1FF)
    {
        c->BodyPart[BODYPART_BOOTS].Type = static_cast<int>(MODEL_BODY_BOOTS) + c->SkinIndex;
        c->BodyPart[BODYPART_BOOTS].Level = 0;
        c->BodyPart[BODYPART_BOOTS].ExcellentFlags = 0;
        c->BodyPart[BODYPART_BOOTS].AncientDiscriminator = 0;
    }
    else
    {
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_BOOTS + ExtType;
        c->BodyPart[BODYPART_BOOTS].Level = LevelConvert((ItemLevels >> 18) & 7);
        c->BodyPart[BODYPART_BOOTS].ExcellentFlags = (Equipment[9] & 8) / 8;
        c->BodyPart[BODYPART_BOOTS].AncientDiscriminator = (Equipment[10] & 8) / 8;
    }

    c->ExtendState = Equipment[10] & 0x01;

    ChangeChaosCastleUnit(c);
    SetCharacterScale(c);
}

void SessionGameplayUnit::ReadEquipmentExtended(int Key, BYTE flags, BYTE *Equipment,
                                                CHARACTER *pCharacter, OBJECT *pHelper)
{
    CHARACTER *c;
    if (pCharacter == NULL)
        c = &CharactersClient[Key];
    else
        c = pCharacter;

    OBJECT *o = &c->Object;
    if (o->Type != MODEL_PLAYER)
        return;

    c->ExtendState = (flags & 0x10) > 0;
    int offset = 0;
    for (int i = 0; i < 2; i++)
    {
        c->Weapon[i].Type = -1;
        c->Weapon[i].ExcellentFlags = 0;
        c->Weapon[i].AncientDiscriminator = 0;
        if (Equipment[offset] != 0xFF && Equipment[offset + 1] != 0xFF)
        {
            short number = MAKEWORD(Equipment[offset + 1], Equipment[offset] & 0xF);
            BYTE group = (Equipment[offset] & 0xF0) >> 4;
            bool isAncient = Equipment[offset + 2] & 0x04;
            bool isExcellent = Equipment[offset + 2] & 0x08;
            BYTE glowLevel = (Equipment[offset + 2] & 0xF0) >> 4;
            if (number > MAX_ITEM_INDEX)
            {
                // not supported yet!
            }
            else if (group == ITEM_GROUP_HELPER && number == ITEM_NUMBER_DARK_SPIRIT)
            {
                ITEM *pEquipmentItemSlot = &CharacterMachine->Equipment[i];
                PET_INFO *pPetInfo = GetPetInfo(pEquipmentItemSlot);
                CreatePetDarkSpirit(c);
                if (!gMapManager.InChaosCastle())
                    if (auto *pet = ResolvePetSystem(c))
                        pet->SetPetInfo(pPetInfo);
            }
            else
            {
                auto modelOffset = group * MAX_ITEM_INDEX + number;
                c->Weapon[i].Type = MODEL_ITEM + modelOffset;
                c->Weapon[i].Level = LevelConvert(glowLevel);
                c->Weapon[i].ExcellentFlags = isExcellent;
                c->Weapon[i].AncientDiscriminator = isAncient;
            }
        }

        offset += 3;
    }

    DeleteParts(c);
    if (c->Change)
    {
        // todo: is this the correct place to return?
        return;
    }

    short bodyParts[] = {0,
                         MODEL_BODY_HELM,
                         MODEL_BODY_ARMOR,
                         MODEL_BODY_PANTS,
                         MODEL_BODY_GLOVES,
                         MODEL_BODY_BOOTS};
    for (int i = 1; i < MAX_BODYPART; i++)
    {
        c->BodyPart[i].Type = bodyParts[i] + c->SkinIndex;
        c->BodyPart[i].ExcellentFlags = 0;
        c->BodyPart[i].AncientDiscriminator = 0;
        c->BodyPart[i].Level = 0;

        if (Equipment[offset] != 0xFF && Equipment[offset + 1] != 0xFF)
        {
            short number = MAKEWORD(Equipment[offset + 1], Equipment[offset] & 0xF);
            BYTE group = (Equipment[offset] & 0xF0) >> 4;
            BYTE glowLevel = (Equipment[offset + 2] & 0xF0) >> 4;
            bool isAncient = Equipment[offset + 2] & 0x04;
            bool isExcellent = Equipment[offset + 2] & 0x08;

            if (number > MAX_ITEM_INDEX)
            {
                // not supported
            }
            else
            {
                auto modelOffset = group * MAX_ITEM_INDEX + number;
                c->BodyPart[i].Type = MODEL_ITEM + modelOffset;
                c->BodyPart[i].Level = LevelConvert(glowLevel);
                c->BodyPart[i].ExcellentFlags = isExcellent;
                c->BodyPart[i].AncientDiscriminator = isAncient;
            }
        }

        offset += 3;
    }

    // Wings:
    {
        c->Wing.Type = -1;
        c->Wing.Level = 0;

        if (Equipment[offset] != 0xFF && Equipment[offset + 1] != 0xFF)
        {
            short number = Equipment[offset + 1] + ((Equipment[offset] & 0xF) << 4);
            BYTE group = (Equipment[offset] & 0xF0) >> 4;
            if (number > MAX_ITEM_INDEX)
            {
                // not supported
            }
            else
            {
                auto modelOffset = group * MAX_ITEM_INDEX + number;
                c->Wing.Type = MODEL_ITEM + modelOffset;
            }
        }

        offset += 2;
    }

    // Helper:
    int HelperVariant = 0;
    {
        c->Helper.Type = -1;
        c->Helper.Level = 0;

        if (Equipment[offset] != 0xFF && Equipment[offset + 1] != 0xFF)
        {
            short number = Equipment[offset + 1] + ((Equipment[offset] & 0xF) << 8);
            short itemNumber = number & (MAX_ITEM_INDEX - 1);
            HelperVariant = (Equipment[offset] & 0xE) >> 1;
            BYTE group = (Equipment[offset] & 0xF0) >> 4;
            auto modelOffset = group * MAX_ITEM_INDEX + itemNumber;
            c->Helper.Type = MODEL_ITEM + modelOffset;
        }

        // offset += 2;
    }

    if (pHelper == nullptr)
    {
        DeleteMount(o);
        g_petProcess.DeletePet(c, c->Helper.Type - MODEL_ITEM, true);
    }
    else
    {
        pHelper->Live = false;
    }

    switch (c->Helper.Type)
    {
    case MODEL_GUARDIAN_ANGEL:
    case MODEL_HORN_OF_UNIRIA:
    case MODEL_HORN_OF_DINORANT: {
        int modelType = 0;
        switch (c->Helper.Type)
        {
        case MODEL_GUARDIAN_ANGEL:
            modelType = MODEL_HELPER;
            break;
        case MODEL_HORN_OF_UNIRIA:
            modelType = MODEL_UNICON;
            break;
        case MODEL_HORN_OF_DINORANT:
            modelType = MODEL_PEGASUS;
            break;
        }

        if (pHelper == NULL)
            CreateMount(modelType, o->Position, o);
        else
            CreateMountSub(modelType, o->Position, o, pHelper);
        break;
    }
    case MODEL_DEMON:
    case MODEL_SPIRIT_OF_GUARDIAN:
    case MODEL_PET_RUDOLF:
    case MODEL_PET_PANDA:
    case MODEL_PET_UNICORN:
    case MODEL_PET_SKELETON:
        g_petProcess.CreatePet(c->Helper.Type - MODEL_ITEM, c->Helper.Type, o->Position, c);
        break;
    case MODEL_DARK_HORSE_ITEM:
        if (pHelper == NULL)
            CreateMount(MODEL_DARK_HORSE, o->Position, o);
        else
            CreateMountSub(MODEL_DARK_HORSE, o->Position, o, pHelper);
        break;
    case MODEL_HORN_OF_FENRIR:
        int type = MODEL_FENRIR_RED;
        switch (HelperVariant)
        {
        case 1:
            type = MODEL_FENRIR_BLACK;
            break;
        case 2:
            type = MODEL_FENRIR_BLUE;
            break;
        case 3:
            type = MODEL_FENRIR_GOLD;
            break;
        }
        if (pHelper == NULL)
            CreateMount(type, o->Position, o);
        else
            CreateMountSub(type, o->Position, o, pHelper);
        break;
    }

    c->EtcPart = 0;
    if ((flags & 0x20) > 0)
    {
        c->EtcPart = PARTS_LION;
    }

    ChangeChaosCastleUnit(c);
    SetCharacterScale(c);
}

namespace CharacterPresentationDetail
{
bool IsForcedNpcMonsterType(EMonsterType type)
{
    const int rawType = static_cast<int>(type);
    switch (rawType)
    {
    case 367:
    case 371:
    case 375:
    case 376:
    case 377:
    case 379:
    case 380:
    case 381:
    case 382:
    case 383:
    case 384:
    case 385:
    case 406:
    case 407:
    case 408:
    case 414:
    case 415:
    case 416:
    case 417:
    case 450:
    case 452:
    case 453:
    case 464:
    case 465:
    case 467:
    case 468:
    case 469:
    case 470:
    case 471:
    case 472:
    case 473:
    case 474:
    case 475:
    case 478:
    case 479:
    case 492:
    case 522:
    case 540:
    case 541:
    case 542:
    case 543:
    case 544:
    case 545:
    case 546:
    case 547:
    case 577:
    case 578:
    case 579:
    case static_cast<int>(MONSTER_WANDERING_MERCHANT_ZYRO):
        return true;
    default:
        return false;
    }
}

int DetermineMonsterObjectKind(EMonsterType type)
{
    const int rawType = static_cast<int>(type);

    // Special hard overrides first.
    if (rawType == 451)
    {
        return KIND_TMP;
    }

    // Exception range that must stay monster even though values are > 200.
    if (rawType >= 480 && rawType <= 491)
    {
        return KIND_MONSTER;
    }

    // Explicit NPC overrides.
    if (rawType == 368 || rawType == 369 || rawType == 370 || IsForcedNpcMonsterType(type))
    {
        return KIND_NPC;
    }

    // Legacy fallback classifier.
    if (rawType == 200)
    {
        return KIND_MONSTER;
    }
    if (rawType >= 260)
    {
        return KIND_MONSTER;
    }
    if (rawType > 200)
    {
        return KIND_NPC;
    }
    if (rawType >= 150)
    {
        return KIND_MONSTER;
    }
    if (rawType > 110)
    {
        return KIND_MONSTER;
    }
    if (rawType >= 100)
    {
        return KIND_TRAP;
    }

    return KIND_MONSTER;
}
} // namespace CharacterPresentationDetail

void SessionGameplayUnit::Setting_Monster(CHARACTER *c, EMonsterType Type, int PositionX,
                                          int PositionY)
{
    OBJECT *o;

    int nCastle = BLOODCASTLE_NUM + (gMapManager.ContextMap() - WD_11BLOODCASTLE_END);
    if (nCastle > 0 && nCastle <= BLOODCASTLE_NUM)
    {
        if (Type >= 84 && Type <= 143)
        {
            c->Level = 0;
            c->Object.Scale += int(nCastle / 3) * 0.05f;
        }
    }

    if (c != NULL)
    {
        o = &c->Object;
        for (int i = 0; i < MAX_MONSTER; i++)
        {
            if (Type == MonsterScript[i].Type)
            {
                wcscpy_s(c->ID, MAX_MONSTER_NAME + 1, MonsterScript[i].Name);
                break;
            }
        }

        c->MonsterIndex = Type;
        c->Object.ExtState = 0;
        SetCharacterTarget(*c, HeroIndex);
        o->Kind = CharacterPresentationDetail::DetermineMonsterObjectKind(Type);
    }
}

CHARACTER *SessionGameplayUnit::CreateMonster(EMonsterType Type, int PositionX, int PositionY,
                                              int Key)
{
    if (CharactersClient.HasWorldInstance() && Key > 0 && Key != HeroKey)
    {
        const SessionCharacterPopulationStorage::Acquisition acquisition =
            CharactersClient.ObserveRemote(Key);
        if (acquisition.character == nullptr)
        {
            return nullptr;
        }
    }

    CHARACTER *c = NULL;
    OBJECT *o;
    int Level;

    if (Type == MONSTER_POUCH_OF_BLESSING)
        c = newYearsDayEvent_.CreateMonster(Type, PositionX, PositionY, Key);
    else if (Type == MONSTER_FIRE_FLAME_GHOST)
        c = summerEvent_.CreateMonster(Type, PositionX, PositionY, Key);
    else
        c = TheMapProcess().CreateMonster(Type, PositionX, PositionY, Key);
    if (c != nullptr)
    {
        Setting_Monster(c, Type, PositionX, PositionY);
        return c;
    }

    switch (Type)
    {
    case MONSTER_GUARDSMAN:
        OpenNpc(MODEL_NPC_CLERK); //
        c = CreateCharacter(Key, MODEL_NPC_CLERK, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.f;
        c->Object.SubType = WorldRandom() % 2 + 10;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        wcscpy(c->ID, L"Clerk");
        break;
#ifdef ADD_ELF_SUMMON
    case 276:
        OpenMonsterModel(MONSTER_MODEL_GOLDEN_TITAN);
        c = CreateCharacter(Key, MODEL_GOLDEN_TITAN, PositionX, PositionY);
        c->Object.Scale = 1.45f;
        c->Weapon[0].Type = MODEL_DARK_BREAKER; //MODEL_SWORD+15;
        c->Weapon[0].Level = 5;
        break;
#endif // ADD_ELF_SUMMON
    case MONSTER_GATE_TO_KALIMA_1:
    case MONSTER_GATE_TO_KALIMA_2:
    case MONSTER_GATE_TO_KALIMA_3:
    case MONSTER_GATE_TO_KALIMA_4:
    case MONSTER_GATE_TO_KALIMA_5:
    case MONSTER_GATE_TO_KALIMA_6:
    case MONSTER_GATE_TO_KALIMA_7:
        c = CreateCharacter(Key, MODEL_WARCRAFT, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Weapon[0].Type = -1;
        c->Weapon[0].Level = 0;
        c->Object.Scale = 1.f;
        c->HideShadow = false;
        o = &c->Object;
        o->PriorAnimationFrame = 10.f;
        o->AnimationFrame = 10;
        o->BlendMesh = -1;
        wcscpy(c->ID, L"");
        break;
    case MONSTER_CHAOS_CASTLE_1:
    case MONSTER_CHAOS_CASTLE_3:
    case MONSTER_CHAOS_CASTLE_5:
    case MONSTER_CHAOS_CASTLE_7:
    case MONSTER_CHAOS_CASTLE_9:
    case MONSTER_CHAOS_CASTLE_11:
    case MONSTER_CHAOS_CASTLE_13: {
        OpenMonsterModel(MONSTER_MODEL_CHAOSCASTLE_KNIGHT);
        c = CreateCharacter(Key, MODEL_CHAOS_CASTLE_KNIGHT, PositionX, PositionY);
        c->Object.Scale = 0.9f;
        o = &c->Object;

        c->Weapon[0].Type = MODEL_SWORD_OF_DESTRUCTION;
        c->Weapon[0].Level = 0;
        c->Weapon[1].Type = MODEL_SWORD_OF_DESTRUCTION;
        c->Weapon[1].Level = 0;
    }
    break;

    case MONSTER_CHAOS_CASTLE_2:
    case MONSTER_CHAOS_CASTLE_4:
    case MONSTER_CHAOS_CASTLE_6:
    case MONSTER_CHAOS_CASTLE_8:
    case MONSTER_CHAOS_CASTLE_10:
    case MONSTER_CHAOS_CASTLE_12:
    case MONSTER_CHAOS_CASTLE_14: {
        int randType = 0;

        randType = WorldRandom() % 2;

        OpenMonsterModel(randType == 0 ? MONSTER_MODEL_CHAOSCASTLE_ELF
                                       : MONSTER_MODEL_CHAOSCASTLE_WIZARD);
        c = CreateCharacter(Key, MODEL_CHAOS_CASTLE_ELF + randType, PositionX, PositionY);
        c->Object.Scale = 0.9f;
        o = &c->Object;

        c->Weapon[0].Type = -1;
        c->Weapon[0].Level = 0;
        c->Weapon[1].Type = -1;
        c->Weapon[1].Level = 0;

        if (randType == 0)
        {
            c->Weapon[0].Type = MODEL_GREAT_REIGN_CROSSBOW;
            c->Weapon[0].Level = 0;
        }
        else
        {
            c->Weapon[0].Type = MODEL_LEGENDARY_STAFF;
            c->Weapon[0].Level = 0;
        }
    }
    break;
    case MONSTER_MAGIC_SKELETON_1:
    case MONSTER_MAGIC_SKELETON_2:
    case MONSTER_MAGIC_SKELETON_3:
    case MONSTER_MAGIC_SKELETON_4:
    case MONSTER_MAGIC_SKELETON_5:
    case MONSTER_MAGIC_SKELETON_6:
    case MONSTER_MAGIC_SKELETON_7:
    case MONSTER_MAGIC_SKELETON_8:
        OpenMonsterModel(MONSTER_MODEL_MAGIC_SKELETON);
        c = CreateCharacter(Key, MODEL_MAGIC_SKELETON, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_STAFF;
        c->Weapon[0].Level = 11;
        c->Object.Scale = 1.2f;
        wcscpy(c->ID, L"마법해골");
        break;
    case MONSTER_CASTLE_GATE: // ???
        OpenMonsterModel(MONSTER_MODEL_CASTLE_GATE);
        c = CreateCharacter(Key, MODEL_CASTLE_GATE, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 0.8f;
        c->Object.EnableShadow = false;
        wcscpy(c->ID, L"성문");
        break;
    case MONSTER_STATUE_OF_SAINT_1:
        OpenMonsterModel(MONSTER_MODEL_STATUE_OF_SAINT);
        c = CreateCharacter(Key, MODEL_STATUE_OF_SAINT, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 0.8f;
        c->Object.EnableShadow = false;
        wcscpy(c->ID, L"성자의석관");
        break;
    case MONSTER_STATUE_OF_SAINT_2:
        OpenMonsterModel(MONSTER_MODEL_STATUE_OF_SAINT);
        c = CreateCharacter(Key, MODEL_STATUE_OF_SAINT, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 0.8f;
        c->Object.EnableShadow = false;
        wcscpy(c->ID, L"성자의석관");
        break;
    case MONSTER_STATUE_OF_SAINT_3:
        OpenMonsterModel(MONSTER_MODEL_STATUE_OF_SAINT);
        c = CreateCharacter(Key, MODEL_STATUE_OF_SAINT, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 0.8f;
        c->Object.EnableShadow = false;
        wcscpy(c->ID, L"성자의석관");
        break;
    case MONSTER_CHIEF_SKELETON_WARRIOR_1:
    case MONSTER_CHIEF_SKELETON_WARRIOR_2:
    case MONSTER_CHIEF_SKELETON_WARRIOR_3:
    case MONSTER_CHIEF_SKELETON_WARRIOR_4:
    case MONSTER_CHIEF_SKELETON_WARRIOR_5:
    case MONSTER_CHIEF_SKELETON_WARRIOR_6:
    case MONSTER_CHIEF_SKELETON_WARRIOR_7:
    case MONSTER_CHIEF_SKELETON_WARRIOR_8:
        OpenMonsterModel(MONSTER_MODEL_ORC);
        c = CreateCharacter(Key, MODEL_ORC, PositionX, PositionY);
        c->Object.Scale = 1.1f;
        o = &c->Object;
        break;
    case MONSTER_CHIEF_SKELETON_ARCHER_1:
    case MONSTER_CHIEF_SKELETON_ARCHER_2:
    case MONSTER_CHIEF_SKELETON_ARCHER_3:
    case MONSTER_CHIEF_SKELETON_ARCHER_4:
    case MONSTER_CHIEF_SKELETON_ARCHER_5:
    case MONSTER_CHIEF_SKELETON_ARCHER_6:
    case MONSTER_CHIEF_SKELETON_ARCHER_7:
    case MONSTER_CHIEF_SKELETON_ARCHER_8:
        OpenMonsterModel(MONSTER_MODEL_ORC_ARCHER);
        c = CreateCharacter(Key, MODEL_ORC_ARCHER, PositionX, PositionY);
        c->Object.Scale = 1.1f;
        c->Weapon[1].Type = MODEL_BATTLE_BOW;
        c->Weapon[1].Level = 1;
        o = &c->Object;
        break;
    case MONSTER_DARK_SKULL_SOLDIER_1:
    case MONSTER_DARK_SKULL_SOLDIER_2:
    case MONSTER_DARK_SKULL_SOLDIER_3:
    case MONSTER_DARK_SKULL_SOLDIER_4:
    case MONSTER_DARK_SKULL_SOLDIER_5:
    case MONSTER_DARK_SKULL_SOLDIER_6:
    case MONSTER_DARK_SKULL_SOLDIER_7:
    case MONSTER_DARK_SKULL_SOLDIER_8:
        OpenMonsterModel(MONSTER_MODEL_DARK_SKULL_SOLDIER);
        c = CreateCharacter(Key, MODEL_DARK_SKULL_SOLDIER, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_CRESCENT_AXE;
        c->Weapon[0].Level = 0;
        c->Weapon[1].Type = MODEL_CRESCENT_AXE;
        c->Weapon[1].Level = 0;
        c->Object.Scale = 1.0f;
        wcscpy(c->ID, L"흑해골전사");
        break;
    case MONSTER_GIANT_OGRE_1:
    case MONSTER_GIANT_OGRE_2:
    case MONSTER_GIANT_OGRE_3:
    case MONSTER_GIANT_OGRE_4:
    case MONSTER_GIANT_OGRE_5:
    case MONSTER_GIANT_OGRE_6:
    case MONSTER_GIANT_OGRE_7:
    case MONSTER_GIANT_OGRE_8:
        OpenMonsterModel(MONSTER_MODEL_GIANT_OGRE);
        c = CreateCharacter(Key, MODEL_GIANT_OGRE, PositionX, PositionY);
        c->Object.Scale = 0.8f;
        wcscpy(c->ID, L"자이언트오우거");
        break;
    case MONSTER_RED_SKELETON_KNIGHT_1:
    case MONSTER_RED_SKELETON_KNIGHT_2:
    case MONSTER_RED_SKELETON_KNIGHT_3:
    case MONSTER_RED_SKELETON_KNIGHT_4:
    case MONSTER_RED_SKELETON_KNIGHT_5:
    case MONSTER_RED_SKELETON_KNIGHT_6:
    case MONSTER_RED_SKELETON_KNIGHT_7:
    case MONSTER_RED_SKELETON_KNIGHT_8:
        OpenMonsterModel(MONSTER_MODEL_RED_SKELETON_KNIGHT);
        c = CreateCharacter(Key, MODEL_RED_SKELETON_KNIGHT, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_CHAOS_DRAGON_AXE;

        if (!int((7 + (gMapManager.ContextMap() - WD_11BLOODCASTLE_END)) / 3))
            c->Weapon[0].Level = 8;
        else
            c->Weapon[0].Level = 0;

        c->Object.Scale = 1.19f;
        wcscpy(c->ID, L"붉은해골기사");
        break;
    case MONSTER_GOLDEN_GOBLIN:
        OpenMonsterModel(MONSTER_MODEL_GOBLIN);
        c = CreateCharacter(Key, MODEL_GOBLIN, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_AXE;
        c->Weapon[0].Level = 9;
        c->Object.Scale = 0.8f;
        wcscpy(c->ID, L"고블린");
        break;
    case MONSTER_GOLDEN_DERKON:
        OpenMonsterModel(MONSTER_MODEL_DRAGON);
        c = CreateCharacter(Key, MODEL_DRAGON_, PositionX, PositionY);
        wcscpy(c->ID, L"드래곤");
        c->Object.Scale = 0.9f;
        break;
    case MONSTER_GOLDEN_LIZARD_KING:
        OpenMonsterModel(MONSTER_MODEL_LIZARD);
        c = CreateCharacter(Key, MODEL_LIZARD, PositionX, PositionY);
        c->Object.Scale = 1.4f;
        c->Weapon[0].Type = MODEL_CHAOS_LIGHTNING_STAFF;
        c->Weapon[0].ExcellentFlags = 63;
        break;
    case MONSTER_GOLDEN_VEPAR:
        OpenMonsterModel(MONSTER_MODEL_VEPAR);
        c = CreateCharacter(Key, MODEL_VEPAR, PositionX, PositionY);
        c->Object.Scale = 1.f;
        break;
    case MONSTER_GOLDEN_TANTALLOS: //??
        OpenMonsterModel(MONSTER_MODEL_TANTALLOS);
        c = CreateCharacter(Key, MODEL_TANTALLOS, PositionX, PositionY);
        c->Object.BlendMesh = 2;
        c->Object.BlendMeshLight = 1.f;
        o = &c->Object;
        c->Object.Scale = 1.8f;
        c->Weapon[0].Type = MODEL_SWORD_OF_DESTRUCTION;
        c->Weapon[0].ExcellentFlags = 63;
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 2, o, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 3, o, 30.f);
        break;
    case MONSTER_GOLDEN_WHEEL:
        OpenMonsterModel(MONSTER_MODEL_GOLDEN_WHEEL);
        c = CreateCharacter(Key, MODEL_GOLDEN_WHEEL, PositionX, PositionY);
        c->Object.Scale = 1.4f;
        c->Weapon[0].Type = MODEL_AQUAGOLD_CROSSBOW;
        c->Weapon[0].ExcellentFlags = 63;
        //c->Weapon[0].Type = MODEL_BOW+16;
        o = &c->Object;
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 2, o, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 3, o, 30.f);
        break;
    case MONSTER_MOLT:
        OpenMonsterModel(MONSTER_MODEL_MOLT);
        c = CreateCharacter(Key, MODEL_MOLT, PositionX, PositionY);
        c->Object.Scale = 1.4f;
        break;

    case MONSTER_ALQUAMOS:
        OpenMonsterModel(MONSTER_MODEL_ALQUAMOS);
        c = CreateCharacter(Key, MODEL_ALQUAMOS, PositionX, PositionY);
        c->Object.Scale = 1.f;
        c->Object.BlendMesh = 0;
        break;
    case MONSTER_QUEEN_RAINER:
        OpenMonsterModel(MONSTER_MODEL_QUEEN_RAINER);
        c = CreateCharacter(Key, MODEL_QUEEN_RAINER, PositionX, PositionY);
        c->Object.Scale = 1.3f;
        c->Object.BlendMesh = -2;
        c->Object.BlendMeshLight = 1.f;
        c->Object.m_bRenderShadow = false;
        break;
    case MONSTER_OMEGA_WING:
    case MONSTER_MEGA_CRUST:
    case MONSTER_ALPHA_CRUST:
        OpenMonsterModel(MONSTER_MODEL_CRUST);
        c = CreateCharacter(Key, MODEL_CRUST, PositionX, PositionY);
        if (MONSTER_MEGA_CRUST == Type)
        {
            c->Object.Scale = 1.1f;
            c->Weapon[0].Type = MODEL_THUNDER_BLADE;
            c->Weapon[0].Level = 5;
            c->Weapon[1].Type = MODEL_LEGENDARY_SHIELD;
            c->Weapon[1].Level = 0;
        }
        else
        {
            c->Object.Scale = 1.3f;
            c->Weapon[0].Type = MODEL_THUNDER_BLADE;
            c->Weapon[0].Level = 9;
            c->Weapon[1].Type = MODEL_LEGENDARY_SHIELD;
            c->Weapon[1].Level = 9;
        }
        c->Object.BlendMesh = 1;
        c->Object.BlendMeshLight = 1.f;
        //Models[MODEL_MONSTER01+52].StreamMesh = 1;
        break;

    case MONSTER_PHANTOM_KNIGHT:
        OpenMonsterModel(MONSTER_MODEL_PHANTOM_KNIGHT);
        c = CreateCharacter(Key, MODEL_PHANTOM_KNIGHT, PositionX, PositionY);
        c->Object.Scale = 1.45f;
        c->Weapon[0].Type = MODEL_DARK_BREAKER; //MODEL_SWORD+15;
        c->Weapon[0].Level = 5;
        break;

    case MONSTER_DRAKAN:
    case MONSTER_GREAT_DRAKAN:
        OpenMonsterModel(MONSTER_MODEL_DRAKAN);
        c = CreateCharacter(Key, MODEL_DRAKAN, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        if (Type == MONSTER_GREAT_DRAKAN)
        {
            c->Object.Scale = 1.0f;
        }
        else
        {
            c->Object.Scale = 0.8f;
        }
        Models[c->Object.Type].Meshs[0].NoneBlendMesh = true;
        Models[c->Object.Type].Meshs[1].NoneBlendMesh = false;
        Models[c->Object.Type].Meshs[2].NoneBlendMesh = false;
        Models[c->Object.Type].Meshs[3].NoneBlendMesh = true;
        Models[c->Object.Type].Meshs[4].NoneBlendMesh = true;
        break;
    case MONSTER_DARK_PHOENIX: {
        OpenMonsterModel(MONSTER_MODEL_DARK_PHOENIX_SHIELD);
        OpenMonsterModel(MONSTER_MODEL_DARK_PHOENIX);
        c = CreateCharacter(Key, MODEL_DARK_PHEONIX_SHIELD, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.0f;
        Models[MODEL_DARK_PHEONIX_SHIELD].StreamMesh = 0;
    }
    break;
    case MONSTER_ORC_ARCHER:
        OpenMonsterModel(MONSTER_MODEL_ORC_ARCHER);
        c = CreateCharacter(Key, MODEL_ORC_ARCHER, PositionX, PositionY);
        c->Object.Scale = 1.2f;
        c->Weapon[1].Type = MODEL_BATTLE_BOW;
        c->Weapon[1].Level = 3;
        o = &c->Object;
        o->HiddenMesh = 1;
        break;
    case MONSTER_ORC_ARCHER_OF_DOOM:
        OpenMonsterModel(MONSTER_MODEL_ORC_ARCHER);
        c = CreateCharacter(Key, MODEL_ORC_ARCHER, PositionX, PositionY);
        c->Object.Scale = 1.2f;
        c->Weapon[1].Type = MODEL_BATTLE_BOW;
        c->Weapon[1].Level = 5;
        o = &c->Object;
        o->HiddenMesh = 1;
        break;
    case MONSTER_ELITE_ORC:
        OpenMonsterModel(MONSTER_MODEL_ORC);
        c = CreateCharacter(Key, MODEL_ORC, PositionX, PositionY);
        c->Object.Scale = 1.3f;
        o = &c->Object;
        o->HiddenMesh = 2;
        break;
    case MONSTER_ORC_SOLDIER_OF_DOOM:
        OpenMonsterModel(MONSTER_MODEL_ORC);
        c = CreateCharacter(Key, MODEL_ORC, PositionX, PositionY);
        c->Object.Scale = 1.3f;
        o = &c->Object;
        o->HiddenMesh = 2;
        break;
    case MONSTER_CURSED_KING:
    case MONSTER_WHITE_WIZARD:
        OpenMonsterModel(MONSTER_MODEL_CURSED_KING);
        c = CreateCharacter(Key, MODEL_CURSED_KING, PositionX, PositionY);
        c->Object.Scale = 1.7f;
        o = &c->Object;
        break;
    case MONSTER_EVIL_GOBLIN:
        OpenMonsterModel(MONSTER_MODEL_EVIL_GOBLIN);
        c = CreateCharacter(Key, MODEL_EVIL_GOBLIN, PositionX, PositionY);
        c->Object.Scale = 0.9f;
        wcscpy(c->ID, L"저주받은 고블린");
        o = &c->Object;
        break;
    case MONSTER_CURSED_SANTA:
        OpenMonsterModel(MONSTER_MODEL_CURSED_SANTA);
        c = CreateCharacter(Key, MODEL_CURSED_SANTA, PositionX, PositionY);
        c->Object.Scale = 1.7f;
        wcscpy(c->ID, L"저주받은 산타");
        o = &c->Object;
        break;
    case MONSTER_MUTANT_HERO:
    case MONSTER_MUTANT:
        OpenMonsterModel(MONSTER_MODEL_MUTANT);
        c = CreateCharacter(Key, MODEL_MUTANT, PositionX, PositionY);
        c->Object.Scale = 1.5f;
        o = &c->Object;
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 2, o, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 3, o, 30.f);
        break;
    case MONSTER_DEATH_BEAM_KNIGHT:
    case MONSTER_BEAM_KNIGHT:
        OpenMonsterModel(MONSTER_MODEL_BEAM_KNIGHT);
        c = CreateCharacter(Key, MODEL_BEAM_KNIGHT, PositionX, PositionY);
        if (Type == MONSTER_DEATH_BEAM_KNIGHT)
        {
            c->Object.Scale = 1.9f;
            c->Object.BlendMesh = -2;
            c->Object.BlendMeshLight = 1.f;
        }
        else
        {
            c->Object.Scale = 1.5f;
        }
        o = &c->Object;
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 2, o, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 3, o, 30.f);
        break;
    case MONSTER_BLOODY_WOLF:
        OpenMonsterModel(MONSTER_MODEL_BLOODY_WOLF);
        c = CreateCharacter(Key, MODEL_BLOODY_WOLF, PositionX, PositionY);
        c->Object.Scale = 2.2f;
        o = &c->Object;
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 2, o, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 3, o, 30.f);
        break;
    case MONSTER_TANTALLOS:
    case MONSTER_ZAIKAN:
        OpenMonsterModel(MONSTER_MODEL_TANTALLOS);
        c = CreateCharacter(Key, MODEL_TANTALLOS, PositionX, PositionY);
        c->Object.BlendMesh = 2;
        c->Object.BlendMeshLight = 1.f;
        o = &c->Object;
        if (Type == MONSTER_TANTALLOS)
        {
            c->Object.Scale = 1.8f;
            c->Weapon[0].Type = MODEL_SWORD_OF_DESTRUCTION;
        }
        else
        {
            c->Object.Scale = 2.1f;
            o->SubType = 1;
            c->Weapon[0].Type = MODEL_STAFF_OF_DESTRUCTION;
        }
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 2, o, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 3, o, 30.f);
        break;
    case MONSTER_IRON_WHEEL:
        OpenMonsterModel(MONSTER_MODEL_GOLDEN_WHEEL);
        c = CreateCharacter(Key, MODEL_GOLDEN_WHEEL, PositionX, PositionY);
        c->Object.Scale = 1.4f;
        c->Weapon[0].Type = MODEL_AQUAGOLD_CROSSBOW;
        //c->Weapon[0].Type = MODEL_BOW+16;
        o = &c->Object;
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 2, o, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 3, o, 30.f);
        break;
    case MONSTER_SILVER_VALKYRIE:
        OpenMonsterModel(MONSTER_MODEL_VALKYRIE);
        c = CreateCharacter(Key, MODEL_VALKYRIE, PositionX, PositionY);
        c->Object.Scale = 1.4f;
        c->Weapon[0].Type = MODEL_BLUEWING_CROSSBOW;
        break;
    case MONSTER_GREAT_BAHAMUT:
        OpenMonsterModel(MONSTER_MODEL_BAHAMUT);
        c = CreateCharacter(Key, MODEL_BAHAMUT, PositionX, PositionY);
        c->Object.Scale = 1.f;
        c->Level = 1;
        break;
    case MONSTER_SEA_WORM:
        OpenMonsterModel(MONSTER_MODEL_SEA_WORM);
        c = CreateCharacter(Key, MODEL_SEA_WORM, PositionX, PositionY);
        c->Object.Scale = 1.8f;
        break;
    case MONSTER_HYDRA:
        OpenMonsterModel(MONSTER_MODEL_HYDRA);
        c = CreateCharacter(Key, MODEL_HYDRA, PositionX, PositionY);
        c->Object.Scale = 1.f;
        c->Object.BlendMesh = 5;
        c->Object.BlendMeshLight = 0.f;
        break;
    case MONSTER_LIZARD_KING:
        OpenMonsterModel(MONSTER_MODEL_LIZARD);
        c = CreateCharacter(Key, MODEL_LIZARD, PositionX, PositionY);
        c->Object.Scale = 1.4f;
        c->Weapon[0].Type = MODEL_STAFF_OF_RESURRECTION;
        break;
    case MONSTER_VALKYRIE:
        OpenMonsterModel(MONSTER_MODEL_VALKYRIE);
        c = CreateCharacter(Key, MODEL_VALKYRIE, PositionX, PositionY);
        c->Object.Scale = 1.1f;
        c->Weapon[0].Type = MODEL_BLUEWING_CROSSBOW;
        c->Object.BlendMesh = 0;
        c->Object.BlendMeshLight = 1.f;
        break;
    case MONSTER_VEPAR:
        OpenMonsterModel(MONSTER_MODEL_VEPAR);
        c = CreateCharacter(Key, MODEL_VEPAR, PositionX, PositionY);
        c->Object.Scale = 1.f;
        break;
    case MONSTER_BAHAMUT:
        OpenMonsterModel(MONSTER_MODEL_BAHAMUT);
        c = CreateCharacter(Key, MODEL_BAHAMUT, PositionX, PositionY);
        c->Object.Scale = 0.6f;
        break;
    case MONSTER_BALI:
        OpenMonsterModel(MONSTER_MODEL_BALI);
        c = CreateCharacter(Key, MODEL_BALI, PositionX, PositionY);
        wcscpy(c->ID, L"발리");
        c->Object.Scale = 0.12f;
        break;
    case MONSTER_GOLDEN_DRAGON:
        OpenMonsterModel(MONSTER_MODEL_DRAGON);
        c = CreateCharacter(Key, MODEL_DRAGON_, PositionX, PositionY);
        wcscpy(c->ID, L"드래곤");
        c->Object.Scale = 0.9f;
        break;
    case MONSTER_GOLDEN_BUDGE_DRAGON:
        OpenMonsterModel(MONSTER_MODEL_BUDGE_DRAGON);
        c = CreateCharacter(Key, MODEL_BUDGE_DRAGON, PositionX, PositionY);
        wcscpy(c->ID, L"황금버지드래곤");
        c->Object.Scale = 0.7f;
        break;
    case MONSTER_RED_DRAGON:
        OpenMonsterModel(MONSTER_MODEL_DRAGON);
        c = CreateCharacter(Key, MODEL_DRAGON_, PositionX, PositionY);
        wcscpy(c->ID, L"쿤둔");
        c->Object.Scale = 1.3f;
        Vector(200.f, 150.f, 280.f, c->Object.BoundingBoxMax);
        break;
    case MONSTER_DEATH_COW:
        OpenMonsterModel(MONSTER_MODEL_DEATH_COW);
        c = CreateCharacter(Key, MODEL_DEATH_COW, PositionX, PositionY);
        wcscpy(c->ID, L"데쓰 카우");
        c->Weapon[0].Type = MODEL_GREAT_HAMMER;
        //c->Weapon[0].Type = MODEL_SWORD+14;
        c->Object.Scale = 1.1f;
        //c->Level = 1;
        break;
    case MONSTER_DEATH_KNIGHT:
        OpenMonsterModel(MONSTER_MODEL_DEATH_KNIGHT);
        c = CreateCharacter(Key, MODEL_DEATH_KNIGHT, PositionX, PositionY);
        wcscpy(c->ID, L"데쓰 나이트");
        c->Weapon[0].Type = MODEL_GIANT_SWORD;
        c->Weapon[0].Type = MODEL_LIGHTING_SWORD;
        //c->Weapon[1].Type = MODEL_SHIELD+8;
        c->Object.Scale = 1.3f;
        //c->Level = 1;
        break;
    case MONSTER_POISON_SHADOW:
        OpenMonsterModel(MONSTER_MODEL_SHADOW);
        c = CreateCharacter(Key, MODEL_SHADOW, PositionX, PositionY);
        wcscpy(c->ID, L"포이즌 쉐도우");
        c->Object.Scale = 1.2f;
        c->Level = 1;
        break;
    case MONSTER_BALROG:
    case MONSTER_METAL_BALROG: //발록2
        OpenMonsterModel(MONSTER_MODEL_BALROG);
        c = CreateCharacter(Key, MODEL_BALROG, PositionX, PositionY);
        wcscpy(c->ID, L"발록");
        c->Weapon[0].Type = MODEL_BILL_OF_BALROG;
        c->Weapon[0].Level = 9;
        c->Object.Scale = 1.6f;
        break;
    case MONSTER_DEVIL:
        OpenMonsterModel(MONSTER_MODEL_DEVIL);
        c = CreateCharacter(Key, MODEL_DEVIL, PositionX, PositionY);
        wcscpy(c->ID, L"데빌");
        c->Object.Scale = 1.1f;
        break;
    case MONSTER_SHADOW:
        OpenMonsterModel(MONSTER_MODEL_SHADOW);
        c = CreateCharacter(Key, MODEL_SHADOW, PositionX, PositionY);
        wcscpy(c->ID, L"쉐도우");
        c->Object.Scale = 1.2f;
        break;

    case MONSTER_DEATH_GORGON:
        OpenMonsterModel(MONSTER_MODEL_GORGON);
        c = CreateCharacter(Key, MODEL_GORGON, PositionX, PositionY);
        wcscpy(c->ID, L"데쓰 고르곤");
        c->Object.Scale = 1.3f;
        c->Weapon[0].Type = MODEL_CRESCENT_AXE;
        c->Weapon[1].Type = MODEL_CRESCENT_AXE;
        c->Object.BlendMesh = 1;
        c->Object.BlendMeshLight = 1.f;
        c->Level = 2;
        break;
    case MONSTER_CURSED_WIZARD:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        wcscpy(c->ID, L"저주받은 법사");
        c->BodyPart[BODYPART_HELM].Type = MODEL_LEGENDARY_HELM;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_LEGENDARY_ARMOR;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_LEGENDARY_PANTS;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_LEGENDARY_GLOVES;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_LEGENDARY_BOOTS;
        c->Weapon[0].Type = MODEL_LEGENDARY_STAFF;
        c->Weapon[1].Type = MODEL_LEGENDARY_SHIELD;
        Level = 9;
        c->BodyPart[BODYPART_HELM].Level = Level;
        c->BodyPart[BODYPART_ARMOR].Level = Level;
        c->BodyPart[BODYPART_PANTS].Level = Level;
        c->BodyPart[BODYPART_GLOVES].Level = Level;
        c->BodyPart[BODYPART_BOOTS].Level = Level;
        //c->Weapon[0].Level = Level;
        //c->Weapon[1].Level = Level;
        c->PK = PVP_MURDERER2;
        SetCharacterScale(c);
        if (gMapManager.InDevilSquare() == true)
        {
            c->Object.Scale = 1.0f;
        }
        break;

    case MONSTER_ELITE_GOBLIN:
        OpenMonsterModel(MONSTER_MODEL_GOBLIN);
        c = CreateCharacter(Key, MODEL_GOBLIN, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_MORNING_STAR;
        c->Weapon[1].Type = MODEL_HORN_SHIELD;
        c->Object.Scale = 1.2f;
        c->Level = 1;
        wcscpy(c->ID, L"고블린 대장");
        break;
    case MONSTER_STONE_GOLEM:
        OpenMonsterModel(MONSTER_MODEL_STONE_GOLEM);
        c = CreateCharacter(Key, MODEL_STONE_GOLEM, PositionX, PositionY);
        wcscpy(c->ID, L"돌괴물");
        break;
    case MONSTER_AGON:
        OpenMonsterModel(MONSTER_MODEL_AGON);
        c = CreateCharacter(Key, MODEL_AGON, PositionX, PositionY);
        wcscpy(c->ID, L"아곤");
        c->Object.Scale = 1.3f;
        c->Weapon[0].Type = MODEL_SERPENT_SWORD;
        c->Weapon[1].Type = MODEL_SERPENT_SWORD;
        break;
    case MONSTER_FOREST_MONSTER:
        OpenMonsterModel(MONSTER_MODEL_FOREST_MONSTER);
        c = CreateCharacter(Key, MODEL_FOREST_MONSTER, PositionX, PositionY);
        wcscpy(c->ID, L"숲의괴물");
        c->Object.Scale = 0.75f;
        break;
    case MONSTER_HUNTER:
        OpenMonsterModel(MONSTER_MODEL_HUNTER);
        c = CreateCharacter(Key, MODEL_HUNTER, PositionX, PositionY);
        wcscpy(c->ID, L"헌터");
        c->Weapon[0].Type = MODEL_ARQUEBUS;
        c->Object.Scale = 0.95f;
        break;
    case MONSTER_BEETLE_MONSTER:
        OpenMonsterModel(MONSTER_MODEL_BEETLE_MONSTER);
        c = CreateCharacter(Key, MODEL_BEETLE_MONSTER, PositionX, PositionY);
        c->Weapon[0].Type = MODEL__SPEAR;
        c->Object.Scale = 0.8f;
        wcscpy(c->ID, L"풍뎅이괴물");
        c->Object.BlendMesh = 1;
        break;
    case MONSTER_CHAIN_SCORPION:
        OpenMonsterModel(MONSTER_MODEL_CHAIN_SCORPION);
        c = CreateCharacter(Key, MODEL_CHAIN_SCORPION, PositionX, PositionY);
        c->Object.Scale = 1.1f;
        wcscpy(c->ID, L"고리전갈");
        break;
    case MONSTER_GOBLIN:
        OpenMonsterModel(MONSTER_MODEL_GOBLIN);
        c = CreateCharacter(Key, MODEL_GOBLIN, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_AXE;
        c->Object.Scale = 0.8f;
        wcscpy(c->ID, L"고블린");
        break;
    case MONSTER_ICE_QUEEN:
        OpenMonsterModel(MONSTER_MODEL_ICE_QUEEN);
        c = CreateCharacter(Key, MODEL_ICE_QUEEN, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_ANGELIC_STAFF;
        c->Object.BlendMesh = 2;
        c->Object.BlendMeshLight = 1.f;
        c->Object.Scale = 1.1f;
        c->Object.LightEnable = false;
        c->Level = 3;
        wcscpy(c->ID, L"아이스퀸");
        break;
    case MONSTER_WORM:
        OpenMonsterModel(MONSTER_MODEL_WORM);
        c = CreateCharacter(Key, MODEL_WORM, PositionX, PositionY);
        wcscpy(c->ID, L"웜");
        break;
    case MONSTER_HOMMERD:
        OpenMonsterModel(MONSTER_MODEL_HOMMERD);
        c = CreateCharacter(Key, MODEL_HOMMERD, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_LARKAN_AXE;
        c->Weapon[1].Type = MODEL_BIG_ROUND_SHIELD;
        c->Object.Scale = 1.15f;
        wcscpy(c->ID, L"호머드");
        break;
    case MONSTER_ICE_MONSTER:
        OpenMonsterModel(MONSTER_MODEL_ICE_MONSTER);
        c = CreateCharacter(Key, MODEL_ICE_MONSTER, PositionX, PositionY);
        c->Object.BlendMesh = 0;
        c->Object.BlendMeshLight = 1.f;
        wcscpy(c->ID, L"얼음괴물");
        break;
    case MONSTER_ASSASSIN:
        OpenMonsterModel(MONSTER_MODEL_ASSASSIN);
        c = CreateCharacter(Key, MODEL_ASSASSIN, PositionX, PositionY);
        c->Object.Scale = 0.95f;
        wcscpy(c->ID, L"암살자");
        break;
    case MONSTER_ELITE_YETI:
        OpenMonsterModel(MONSTER_MODEL_ELITE_YETI);
        c = CreateCharacter(Key, MODEL_ELITE_YETI, PositionX, PositionY);
        wcscpy(c->ID, L"설인 대장");
        c->Object.Scale = 1.4f;
        break;
    case MONSTER_YETI:
        OpenMonsterModel(MONSTER_MODEL_YETI);
        c = CreateCharacter(Key, MODEL_YETI, PositionX, PositionY);
        wcscpy(c->ID, L"설인");
        c->Object.Scale = 1.1f;
        break;
    case MONSTER_GORGON:
        OpenMonsterModel(MONSTER_MODEL_GORGON);
        c = CreateCharacter(Key, MODEL_GORGON, PositionX, PositionY);
        wcscpy(c->ID, L"고르곤");
        c->Object.Scale = 1.5f;
        c->Weapon[0].Type = MODEL_GORGON_STAFF;
        c->Object.BlendMesh = 1;
        c->Object.BlendMeshLight = 1.f;
        break;
    case MONSTER_SPIDER:
        OpenMonsterModel(MONSTER_MODEL_SPIDER);
        c = CreateCharacter(Key, MODEL_SPIDER, PositionX, PositionY);
        wcscpy(c->ID, L"거미");
        c->Object.Scale = 0.4f;
        break;
    case MONSTER_CYCLOPS:
        OpenMonsterModel(MONSTER_MODEL_CYCLOPS);
        c = CreateCharacter(Key, MODEL_CYCLOPS, PositionX, PositionY);
        wcscpy(c->ID, L"싸이크롭스");
        c->Weapon[0].Type = MODEL_CRESCENT_AXE;
        //c->Weapon[1].Type = MODEL_MACE+2;
        //c->Object.HiddenMesh = 2;
        break;
    case MONSTER_BULL_FIGHTER:
    case MONSTER_ELITE_BULL_FIGHTER:
    case MONSTER_POISON_BULL:
    default:
        OpenMonsterModel(MONSTER_MODEL_BULL_FIGHTER);
        c = CreateCharacter(Key, MODEL_BULL_FIGHTER, PositionX, PositionY);
        if (Type == MONSTER_BULL_FIGHTER)
        {
            c->Object.HiddenMesh = 0;
            wcscpy(c->ID, L"소뿔전사");
            c->Object.Scale = 0.8f;
            c->Weapon[0].Type = MODEL_NIKKEA_AXE;
        }
        else if (Type == MONSTER_ELITE_BULL_FIGHTER)
        {
            c->Weapon[0].Type = MODEL_BERDYSH;
            wcscpy(c->ID, L"소뿔전사 대장");
            c->Object.Scale = 1.15f;
            c->Level = 1;
        }
        else if (Type == MONSTER_POISON_BULL)
        {
            c->Weapon[0].Type = MODEL_GREAT_SCYTHE;
            wcscpy(c->ID, L"포이즌 소뿔전사");
            c->Object.Scale = 1.f;
            c->Level = 2;

            g_CharacterRegisterBuff((&c->Object), eDeBuff_Poison);
        }
        break;
    case MONSTER_GHOST:
        OpenMonsterModel(MONSTER_MODEL_GHOST);
        c = CreateCharacter(Key, MODEL_GHOST_MONSTER, PositionX, PositionY);
        wcscpy(c->ID, L"고스트");
        c->Object.AlphaTarget = 0.4f;
        c->MoveSpeed = 15;
        c->Blood = true;
        break;
    case MONSTER_LARVA:
        OpenMonsterModel(MONSTER_MODEL_LARVA);
        c = CreateCharacter(Key, MODEL_LARVA, PositionX, PositionY);
        wcscpy(c->ID, L"유충");
        c->Object.Scale = 0.6f;
        break;
    case MONSTER_HELL_SPIDER:
        OpenMonsterModel(MONSTER_MODEL_HELL_SPIDER);
        c = CreateCharacter(Key, MODEL_HELL_SPIDER, PositionX, PositionY);
        wcscpy(c->ID, L"헬스파이더");
        c->Weapon[0].Type = MODEL_SERPENT_STAFF;
        c->Object.Scale = 1.1f;
        break;
    case MONSTER_HOUND:
    case MONSTER_HELL_HOUND:
        OpenMonsterModel(MONSTER_MODEL_HOUND);
        c = CreateCharacter(Key, MODEL_HOUND, PositionX, PositionY);
        if (Type == MONSTER_HOUND)
        {
            c->Object.HiddenMesh = 0;
            wcscpy(c->ID, L"하운드");
            c->Object.Scale = 0.85f;
            c->Weapon[0].Type = MODEL_SWORD_OF_ASSASSIN;
        }
        if (Type == MONSTER_HELL_HOUND)
        {
            c->Object.HiddenMesh = 1;
            c->Weapon[0].Type = MODEL_FALCHION;
            c->Weapon[1].Type = MODEL_PLATE_SHIELD;
            wcscpy(c->ID, L"헬하운드");
            c->Object.Scale = 1.1f;
            c->Level = 1;
        }
        break;

    case MONSTER_BUDGE_DRAGON:
        OpenMonsterModel(MONSTER_MODEL_BUDGE_DRAGON);
        c = CreateCharacter(Key, MODEL_BUDGE_DRAGON, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown2");
        c->Object.Scale = 0.5f;
        break;

    case MONSTER_DARK_KNIGHT:
        OpenMonsterModel(MONSTER_MODEL_DARK_KNIGHT);
        c = CreateCharacter(Key, MODEL_DARK_KNIGHT, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown10");
        c->Object.Scale = 0.8f;
        c->Level = 1;
        c->Weapon[0].Type = MODEL_DOUBLE_BLADE;
        break;
    case MONSTER_LICH:
    case MONSTER_THUNDER_LICH:
        OpenMonsterModel(MONSTER_MODEL_LICH);
        c = CreateCharacter(Key, MODEL_LICH, PositionX, PositionY);
        if (Type == MONSTER_LICH)
        {
            wcscpy(c->ID, L"리치");
            c->Weapon[0].Type = MODEL_SERPENT_STAFF;
            c->Object.Scale = 0.85f;
        }
        else
        {
            wcscpy(c->ID, L"썬더 리치");
            c->Weapon[0].Type = MODEL_THUNDER_STAFF;
            c->Level = 1;
            c->Object.Scale = 1.1f;
        }
        break;
    case MONSTER_GIANT:
        OpenMonsterModel(MONSTER_MODEL_GIANT);
        c = CreateCharacter(Key, MODEL_GIANT, PositionX, PositionY);
        wcscpy(c->ID, L"자이언트");
        c->Weapon[0].Type = MODEL_DOUBLE_AXE;
        c->Weapon[1].Type = MODEL_DOUBLE_AXE;
        c->Object.Scale = 1.6f;
        break;

    case MONSTER_SKELETON_WARRIOR:
    case MONSTER_DEATH_KING:
    case MONSTER_DEATH_BONE:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        wcscpy(c->ID, L"해골전사");
        c->Object.SubType = MODEL_SKELETON1;
        c->Blood = true;
        if (Type == 14)
        {
            c->Object.Scale = 0.95f;
            c->Weapon[0].Type = MODEL_GLADIUS;
            c->Weapon[1].Type = MODEL_BUCKLER;
        }
        else if (Type == 56)
        {
            c->Object.Scale = 0.8f;
            c->Weapon[0].Type = MODEL_GREAT_SCYTHE;
        }
        else
        {
            c->Level = 1;
            c->Object.Scale = 1.4f;
            c->Weapon[0].Type = MODEL_BILL_OF_BALROG;
        }
        break;
    case MONSTER_SKELETON_ARCHER:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        wcscpy(c->ID, L"해골궁수");
        c->Object.Scale = 1.1f;
        c->Weapon[1].Type = MODEL_ELVEN_BOW;
        c->Object.SubType = MODEL_SKELETON2;
        c->Level = 1;
        c->Blood = true;
        break;
    case MONSTER_ELITE_SKELETON:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        wcscpy(c->ID, L"해골전사 대장");
        c->Object.Scale = 1.2f;
        c->Weapon[0].Type = MODEL_TOMAHAWK;
        c->Weapon[1].Type = MODEL_SKULL_SHIELD;
        c->Object.SubType = MODEL_SKELETON3;
        c->Level = 1;
        c->Blood = true;
        break;
    case MONSTER_ELITE_SKILL_SOLDIER:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        ::wcscpy(c->ID, L"엘리트 해골전사");
        c->Object.Scale = 0.95f;
        c->Object.SubType = MODEL_SKELETON_PCBANG;
        break;
    case MONSTER_JACK_OLANTERN:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        ::wcscpy(c->ID, L"잭 오랜턴");
        c->Object.Scale = 0.95f;
        c->Object.SubType = MODEL_HALLOWEEN;
        break;
    case MONSTER_SANTA:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        ::wcscpy(c->ID, L"크리스마스 걸");
        c->Object.Scale = 0.85f;
        c->Object.SubType = MODEL_XMAS_EVENT_CHANGE_GIRL;
        break;
    case MONSTER_GAMEMASTER:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        ::wcscpy(c->ID, L"GameMaster");
        c->Object.Scale = 1.0f;
        c->Object.SubType = MODEL_GM_CHARACTER;
        break;
    case MONSTER_GOLDEN_TITAN:
        OpenMonsterModel(MONSTER_MODEL_TITAN);
        c = CreateCharacter(Key, MODEL_TITAN, PositionX, PositionY);
        wcscpy(c->ID, L"타이탄");
        c->Object.Scale = 1.8f;
        c->Object.BlendMesh = 2;
        c->Object.BlendMeshLight = 1.f;
        o = &c->Object;
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 2, o, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 3, o, 30.f);
        break;
    case MONSTER_GOLDEN_SOLDIER:
    case MONSTER_SOLDIER:
        OpenMonsterModel(MONSTER_MODEL_SOLDIER);
        c = CreateCharacter(Key, MODEL_SOLDIER, PositionX, PositionY);
        wcscpy(c->ID, L"솔져");
        c->Weapon[1].Type = MODEL_AQUAGOLD_CROSSBOW;
        if (Type == 54)
            c->Object.Scale = 1.1f;
        else
            c->Object.Scale = 1.3f;
        break;
    case MONSTER_LANCE_TRAP:
        c = CreateCharacter(Key, 39, PositionX, PositionY);
        break;
    case MONSTER_IRON_STICK_TRAP:
        c = CreateCharacter(Key, 40, PositionX, PositionY);
        break;
    case MONSTER_FIRE_TRAP:
        c = CreateCharacter(Key, 51, PositionX, PositionY);
        break;
    case MONSTER_METEORITE_TRAP:
        c = CreateCharacter(Key, 25, PositionX, PositionY);
        break;
    case MONSTER_LASER_TRAP:
        c = CreateCharacter(Key, 51, PositionX, PositionY);
        break;
    case MONSTER_SOCCERBALL:
        c = CreateCharacter(Key, MODEL_BALL, PositionX, PositionY);
        o = &c->Object;
        o->BlendMesh = 2;
        o->Scale = 1.8f;
        c->Level = 1;
        break;
    case MONSTER_PET_TRAINER:
        OpenNpc(MODEL_NPC_BREEDER);
        c = CreateCharacter(Key, MODEL_NPC_BREEDER, PositionX, PositionY);
        wcscpy(c->ID, L"조련사 NPC");
        break;

#ifdef _PVP_MURDERER_HERO_ITEM
    case 227:
        OpenNpc(MODEL_MASTER);
        c = CreateCharacter(Key, MODEL_MASTER, PositionX, PositionY);
        wcscpy(c->ID, L"살인마상점");
        break;

    case 228:
        OpenNpc(MODEL_HERO_SHOP);
        c = CreateCharacter(Key, MODEL_HERO_SHOP, PositionX, PositionY);
        wcscpy(c->ID, L"영웅상점");
        break;
#endif // _PVP_MURDERER_HERO_ITEM

    case MONSTER_MARLON:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        wcscpy(c->ID, L"말론");
        c->BodyPart[BODYPART_HELM].Type = MODEL_PLATE_HELM;
        c->BodyPart[BODYPART_HELM].Level = 7;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_PLATE_ARMOR;
        c->BodyPart[BODYPART_ARMOR].Level = 7;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_PLATE_PANTS;
        c->BodyPart[BODYPART_PANTS].Level = 7;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_PLATE_GLOVES;
        c->BodyPart[BODYPART_GLOVES].Level = 7;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_PLATE_BOOTS;
        c->BodyPart[BODYPART_BOOTS].Level = 7;
        c->Weapon[0].Type = MODEL_BERDYSH;
        c->Weapon[0].Level = 8;
        c->Weapon[1].Type = -1;
        SetCharacterScale(c);
        c->Object.m_bpcroom = false;
        break;
    case MONSTER_ALEX:
        OpenNpc(MODEL_MERCHANT_MAN);
        c = CreateCharacter(Key, MODEL_MERCHANT_MAN, PositionX, PositionY);
        wcscpy(c->ID, L"로랜추가상인");
        c->BodyPart[BODYPART_HELM].Type = MODEL_MERCHANT_MAN_HEAD;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_MERCHANT_MAN_UPPER + 1;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_MERCHANT_MAN_GLOVES + 1;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_MERCHANT_MAN_BOOTS;
        break;
    case MONSTER_THOMPSON_THE_MERCHANT:
        OpenNpc(MODEL_DEVIAS_TRADER);
        c = CreateCharacter(Key, MODEL_DEVIAS_TRADER, PositionX, PositionY);
        wcscpy(c->ID, L"데비추가상인");
        break;

    case MONSTER_ARCHANGEL:
        OpenNpc(MODEL_NPC_ARCHANGEL);
        c = CreateCharacter(Key, MODEL_NPC_ARCHANGEL, PositionX, PositionY);
        o = &c->Object;
        o->Scale = 1.f;
        o->Kind = KIND_NPC;
        break;
    case MONSTER_MESSENGER_OF_ARCH:
        OpenNpc(MODEL_NPC_ARCHANGEL_MESSENGER);
        c = CreateCharacter(Key, MODEL_NPC_ARCHANGEL_MESSENGER, PositionX, PositionY);
        o = &c->Object;
        o->Scale = 1.f;
        o->Kind = KIND_NPC;
        break;

    case MONSTER_GOBLIN_GATE:
        OpenMonsterModel(MONSTER_MODEL_GOBLIN);
        c = CreateCharacter(Key, MODEL_GOBLIN, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_STAFF;
        c->Weapon[0].Level = 4;
        c->Object.Scale = 1.5f;
        c->Object.Kind = KIND_NPC;
        SetAction(&c->Object, 0);
        break;

    case MONSTER_SEVINA_THE_PRIESTESS:
        OpenNpc(MODEL_NPC_SEVINA);
        c = CreateCharacter(Key, MODEL_NPC_SEVINA, PositionX, PositionY);
        o = &c->Object;
        o->Scale = 1.f;
        o->Kind = KIND_NPC;
        break;

    case MONSTER_GOLDEN_ARCHER:
        OpenNpc(MODEL_PLAYER);
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        o = &c->Object;
        o->SubType = MODEL_SKELETON2;
        o->Scale = 1.0f;
        o->Kind = KIND_NPC;
        c->Level = 8;
        break;
    case MONSTER_CHARON:
        OpenNpc(MODEL_NPC_DEVILSQUARE);
        c = CreateCharacter(Key, MODEL_NPC_DEVILSQUARE, PositionX, PositionY);
        break;
    case MONSTER_OSBOURNE:
        OpenNpc(MODEL_REFINERY_NPC);
        c = CreateCharacter(Key, MODEL_REFINERY_NPC, PositionX, PositionY);
        o = &c->Object;
        break;
    case MONSTER_JERRIDON: //환원
        OpenNpc(MODEL_RECOVERY_NPC);
        c = CreateCharacter(Key, MODEL_RECOVERY_NPC, PositionX, PositionY);
        o = &c->Object;
        break;
    case MONSTER_CHAOS_GOBLIN:
        OpenNpc(MODEL_MIX_NPC);
        c = CreateCharacter(Key, MODEL_MIX_NPC, PositionX, PositionY);
        o = &c->Object;
        o->BlendMesh = 1;
        break;
    case MONSTER_ARENA_GUARD:
        OpenNpc(MODEL_TOURNAMENT);
        c = CreateCharacter(Key, MODEL_TOURNAMENT, PositionX, PositionY);
        break;
    case MONSTER_BAZ_THE_VAULT_KEEPER:
        OpenNpc(MODEL_STORAGE);
        c = CreateCharacter(Key, MODEL_STORAGE, PositionX, PositionY);
        break;
    case MONSTER_GUILD_MASTER:
        OpenNpc(MODEL_MASTER);
        c = CreateCharacter(Key, MODEL_MASTER, PositionX, PositionY);
        wcscpy(c->ID, L"마스터");
        break;
    case MONSTER_LAHAP:
        OpenNpc(MODEL_NPC_SERBIS);
        c = CreateCharacter(Key, MODEL_NPC_SERBIS, PositionX, PositionY);
        wcscpy(c->ID, L"세르비스");
        break;
    case MONSTER_ELF_SOLDIER:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        MakeElfHelper(c);
        wcscpy(c->ID, L"페이아");
        o = &c->Object;
        CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 42, o, 15.f);
        break;
    case MONSTER_ELF_LALA:
        OpenNpc(MODEL_ELF_WIZARD);
        c = CreateCharacter(Key, MODEL_ELF_WIZARD, PositionX, PositionY);
        wcscpy(c->ID, L"라라 요정");
        o = &c->Object;
        o->BlendMesh = 1;
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 140.f;
        break;
    case MONSTER_EO_THE_CRAFTSMAN:
        OpenNpc(MODEL_ELF_MERCHANT);
        c = CreateCharacter(Key, MODEL_ELF_MERCHANT, PositionX, PositionY);
        wcscpy(c->ID, L"장인");
        break;
    case MONSTER_CAREN_THE_BARMAID:
        OpenNpc(MODEL_SNOW_MERCHANT);
        c = CreateCharacter(Key, MODEL_SNOW_MERCHANT, PositionX, PositionY);
        wcscpy(c->ID, L"술집마담");
        break;
    case MONSTER_IZABEL_THE_WIZARD:
        OpenNpc(MODEL_SNOW_WIZARD);
        c = CreateCharacter(Key, MODEL_SNOW_WIZARD, PositionX, PositionY);
        wcscpy(c->ID, L"마법사");
        break;
    case MONSTER_ZIENNA_THE_WEAPONS_MERCHANT:
        OpenNpc(MODEL_SNOW_SMITH);
        c = CreateCharacter(Key, MODEL_SNOW_SMITH, PositionX, PositionY);
        wcscpy(c->ID, L"무기상인");
        break;
    case MONSTER_CROSSBOW_GUARD:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        wcscpy(c->ID, L"경비병");
        c->BodyPart[BODYPART_HELM].Type = MODEL_PLATE_HELM;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_PLATE_ARMOR;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_PLATE_PANTS;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_PLATE_GLOVES;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_PLATE_BOOTS;
        c->Weapon[0].Type = MODEL_LIGHT_CROSSBOW;
        c->Weapon[1].Type = MODEL_BOLT;
        SetCharacterScale(c);
        break;
    case MONSTER_WANDERING_MERCHANT_MARTIN:
        OpenNpc(MODEL_MERCHANT_MAN);
        c = CreateCharacter(Key, MODEL_MERCHANT_MAN, PositionX, PositionY);
        wcscpy(c->ID, L"떠돌이 상인");
        c->BodyPart[BODYPART_HELM].Type = MODEL_MERCHANT_MAN_HEAD + 1;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_MERCHANT_MAN_UPPER + 1;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_MERCHANT_MAN_GLOVES + 1;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_MERCHANT_MAN_BOOTS + 1;
        break;
    case MONSTER_BERDYSH_GUARD:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        wcscpy(c->ID, L"경비병");
        c->BodyPart[BODYPART_HELM].Type = MODEL_PLATE_HELM;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_PLATE_ARMOR;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_PLATE_PANTS;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_PLATE_GLOVES;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_PLATE_BOOTS;
        c->Weapon[0].Type = MODEL_BERDYSH;
        SetCharacterScale(c);
        break;
    case MONSTER_WANDERING_MERCHANT_HAROLD:
        OpenNpc(MODEL_MERCHANT_MAN);
        c = CreateCharacter(Key, MODEL_MERCHANT_MAN, PositionX, PositionY);
        wcscpy(c->ID, L"떠돌이 상인");
        c->BodyPart[BODYPART_HELM].Type = MODEL_MERCHANT_MAN_HEAD;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_MERCHANT_MAN_UPPER;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_MERCHANT_MAN_GLOVES;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_MERCHANT_MAN_BOOTS;
        break;
    case MONSTER_WANDERING_MERCHANT_ZYRO:
        OpenNpc(MODEL_GAMBLE_NPC_MOSS);
        c = CreateCharacter(Key, MODEL_GAMBLE_NPC_MOSS, PositionX, PositionY);
        wcscpy(c->ID, L"떠돌이 상인");
        c->Object.LifeTime = 100;
        c->Object.Scale = 0.8f;
        c->Object.m_fEdgeScale = 1.1f;
        for (int i = 0; i < 6; i++)
        {
            Models[MODEL_GAMBLE_NPC_MOSS].Actions[i].PlaySpeed = 0.33f;
        }
        break;
    case MONSTER_HANZO_THE_BLACKSMITH:
        OpenNpc(MODEL_SMITH);
        c = CreateCharacter(Key, MODEL_SMITH, PositionX, PositionY);
        wcscpy(c->ID, L"대장장이 한스");
        c->Object.Scale = 0.95f;
        break;
    case MONSTER_POTION_GIRL_AMY:
        OpenNpc(MODEL_MERCHANT_GIRL);
        c = CreateCharacter(Key, MODEL_MERCHANT_GIRL, PositionX, PositionY);
        wcscpy(c->ID, L"물약파는 소녀");
        c->BodyPart[BODYPART_HELM].Type = MODEL_MERCHANT_GIRL_HEAD;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_MERCHANT_GIRL_UPPER;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_MERCHANT_GIRL_LOWER;
        break;
    case MONSTER_PASI_THE_MAGE:
        OpenNpc(MODEL_SCIENTIST);
        c = CreateCharacter(Key, MODEL_SCIENTIST, PositionX, PositionY);
        wcscpy(c->ID, L"마법사 파시");
        break;
    case MONSTER_LUMEN_THE_BARMAID:
        OpenNpc(MODEL_MERCHANT_FEMALE);
        c = CreateCharacter(Key, MODEL_MERCHANT_FEMALE, PositionX, PositionY);
        wcscpy(c->ID, L"술집마담 리아먼");
        c->BodyPart[BODYPART_HELM].Type = MODEL_MERCHANT_FEMALE_HEAD + 1;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_MERCHANT_FEMALE_UPPER + 1;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_MERCHANT_FEMALE_LOWER + 1;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_MERCHANT_FEMALE_BOOTS + 1;
        break;
    case MONSTER_WOLF_STATUS:
        OpenNpc(MODEL_CRYWOLF_STATUE);
        c = CreateCharacter(Key, MODEL_CRYWOLF_STATUE, PositionX, PositionY);
        wcscpy(c->ID, L"석상");
        c->Object.Live = false;
        break;
    case MONSTER_WOLF_ALTAR1:
        OpenNpc(MODEL_CRYWOLF_ALTAR1);
        c = CreateCharacter(Key, MODEL_CRYWOLF_ALTAR1, PositionX, PositionY);
        wcscpy(c->ID, L"제단1");
        c->Object.Position[2] -= 10.0f;
        c->Object.HiddenMesh = -2;
        c->Object.Visible = false;
        c->Object.EnableShadow = false;
        break;
    case MONSTER_WOLF_ALTAR2:
        OpenNpc(MODEL_CRYWOLF_ALTAR2);
        c = CreateCharacter(Key, MODEL_CRYWOLF_ALTAR2, PositionX, PositionY);
        wcscpy(c->ID, L"제단2");
        c->Object.HiddenMesh = -2;
        c->Object.Position[2] -= 10.0f;
        c->Object.Visible = false;
        c->Object.EnableShadow = false;
        break;
    case MONSTER_WOLF_ALTAR3:
        OpenNpc(MODEL_CRYWOLF_ALTAR3);
        c = CreateCharacter(Key, MODEL_CRYWOLF_ALTAR3, PositionX, PositionY);
        wcscpy(c->ID, L"제단3");
        c->Object.HiddenMesh = -2;
        c->Object.Position[2] -= 10.0f;
        c->Object.Visible = false;
        c->Object.EnableShadow = false;
        break;
    case MONSTER_WOLF_ALTAR4:
        OpenNpc(MODEL_CRYWOLF_ALTAR4);
        c = CreateCharacter(Key, MODEL_CRYWOLF_ALTAR4, PositionX, PositionY);
        wcscpy(c->ID, L"제단4");
        c->Object.HiddenMesh = -2;
        c->Object.Position[2] -= 10.0f;
        c->Object.Visible = false;
        c->Object.EnableShadow = false;
        break;
    case MONSTER_WOLF_ALTAR5:
        OpenNpc(MODEL_CRYWOLF_ALTAR5);
        c = CreateCharacter(Key, MODEL_CRYWOLF_ALTAR5, PositionX, PositionY);
        wcscpy(c->ID, L"제단5");
        c->Object.HiddenMesh = -2;
        c->Object.Position[2] -= 10.0f;
        c->Object.Visible = false;
        c->Object.EnableShadow = false;
        break;
    case MONSTER_ELPHIS:
        OpenNpc(MODEL_SMELTING_NPC);
        c = CreateCharacter(Key, MODEL_SMELTING_NPC, PositionX + 1, PositionY - 1);
        wcscpy(c->ID, L"제련의탑NPC");
        c->Object.Scale = 2.5f;
        c->Object.EnableShadow = false;
        c->Object.m_bRenderShadow = false;
        break;
    case MONSTER_FIREWORKS_GIRL:
        OpenNpc(MODEL_WEDDING_NPC);
        c = CreateCharacter(Key, MODEL_WEDDING_NPC, PositionX, PositionY);
        wcscpy(c->ID, L"WeddingNPC");
        c->Object.Scale = 1.1f;
        c->Object.EnableShadow = false;
        c->Object.m_bRenderShadow = false;
        break;
    case MONSTER_LUKE_THE_HELPER:
    case MONSTER_LEO_THE_HELPER:
    case MONSTER_HELPER_ELLEN:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        wcscpy(c->ID, L"HelperName");
        c->BodyPart[BODYPART_HELM].Type = MODEL_PLATE_HELM;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_PLATE_ARMOR;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_PLATE_PANTS;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_PLATE_GLOVES;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_PLATE_BOOTS;
        c->Weapon[0].Type = -1;
        SetCharacterScale(c);
        c->Object.m_bpcroom = true;
        break;
    case MONSTER_ORACLE_LAYLA:
        OpenNpc(MODEL_KALIMA_SHOP);
        c = CreateCharacter(Key, MODEL_KALIMA_SHOP, PositionX, PositionY);
        c->Object.Position[2] += 140.0f;
        wcscpy(c->ID, L"KalimaShop");
        break;
    case MONSTER_CHAOS_CARD_MASTER: {
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        wcscpy(c->ID, L"ChaosCard");
        c->BodyPart[BODYPART_HELM].Type = MODEL_VENOM_MIST_HELM;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_VENOM_MIST_ARMOR;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_VENOM_MIST_PANTS;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_VENOM_MIST_GLOVES;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_VENOM_MIST_BOOTS;
        c->Wing.Type = MODEL_WINGS_OF_HEAVEN;
        int iLevel = 9;
        c->BodyPart[BODYPART_HELM].Level = iLevel;
        c->BodyPart[BODYPART_ARMOR].Level = iLevel;
        c->BodyPart[BODYPART_PANTS].Level = iLevel;
        c->BodyPart[BODYPART_GLOVES].Level = iLevel;
        c->BodyPart[BODYPART_BOOTS].Level = iLevel;
        c->Weapon[0].Type = -1;
        SetCharacterScale(c);
        c->Object.SubType = Type;
    }
    break;
    case MONSTER_PAMELA_THE_SUPPLIER: {
        OpenNpc(MODEL_BC_NPC1);
        c = CreateCharacter(Key, MODEL_BC_NPC1, PositionX, PositionY);
        wcscpy(c->ID, L"공성 NPC");
        c->Object.Scale = 1.0f;
        c->Object.Angle[2] = 0.f;
        CreateObject(MODEL_BC_BOX, c->Object.Position, c->Object.Angle);
    }
    break;
    case MONSTER_ANGELA_THE_SUPPLIER: {
        OpenNpc(MODEL_BC_NPC2);
        c = CreateCharacter(Key, MODEL_BC_NPC2, PositionX, PositionY);
        wcscpy(c->ID, L"공성 NPC");
        c->Object.Scale = 1.0f;
        c->Object.Angle[2] = 90.f;
        CreateObject(MODEL_BC_BOX, c->Object.Position, c->Object.Angle);
    }
    break;
    case MONSTER_PRIEST_DEVIN:
        OpenNpc(MODEL_NPC_DEVIN);
        c = CreateCharacter(Key, MODEL_NPC_DEVIN, PositionX, PositionY);
        wcscpy(c->ID, L"사제데빈");
        break;
    case MONSTER_WEREWOLF_QUARREL:
        OpenNpc(MODEL_NPC_QUARREL);
        c = CreateCharacter(Key, MODEL_NPC_QUARREL, PositionX, PositionY);
        wcscpy(c->ID, L"웨어울프쿼렐");
        c->Object.Scale = 1.9f;
        break;
    case MONSTER_GATEKEEPER:
        OpenNpc(MODEL_NPC_CASTEL_GATE);
        c = CreateCharacter(Key, MODEL_NPC_CASTEL_GATE, PositionX, PositionY, 90.f);
        wcscpy(c->ID, L"성문");
        o = &c->Object;
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 240.f;
        c->Object.Scale = 1.2f;
        c->Object.m_fEdgeScale = 1.1f;
        c->Object.EnableShadow = false;
        c->Object.m_bRenderShadow = false;
        break;
    case MONSTER_LUNAR_RABBIT: {
        OpenMonsterModel(MONSTER_MODEL_LUNAR_RABBIT);
        c = CreateCharacter(Key, MODEL_LUNAR_RABBIT, PositionX, PositionY);
        wcscpy(c->ID, L"달토끼");
        c->Object.Scale = 0.8f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        c->Object.SubType = WorldRandom() % 3;
        c->Object.m_iAnimation = 0;

        boneManager_.RegisterBone(c, L"Rabbit_1", CharacterSocket::Rabbit_1); // Bip01 Spine
        boneManager_.RegisterBone(c, L"Rabbit_2", CharacterSocket::Rabbit_2); // Bip01 Head
        boneManager_.RegisterBone(c, L"Rabbit_3", CharacterSocket::Rabbit_3); // Bip01 Neck1
        boneManager_.RegisterBone(c, L"Rabbit_4", CharacterSocket::Rabbit_4); // Bip01 Pelvis
    }

    break;
    case MONSTER_CHERRY_BLOSSOM_SPIRIT: {
        OpenNpc(MODEL_NPC_CHERRYBLOSSOM);
        c = CreateCharacter(Key, MODEL_NPC_CHERRYBLOSSOM, PositionX, PositionY);
        c->Object.Scale = 0.65f;
        c->Object.m_fEdgeScale = 1.08f;
        o = &c->Object;
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 170.f;
        wcscpy(c->ID, L"벚꽃의정령");
    }
    break;
    case MONSTER_CHERRY_BLOSSOM_TREE: {
        OpenNpc(MODEL_NPC_CHERRYBLOSSOMTREE);
        c = CreateCharacter(Key, MODEL_NPC_CHERRYBLOSSOMTREE, PositionX, PositionY);
        c->Object.Scale = 1.0f;
        c->Object.m_fEdgeScale = 0.0f;
        c->Object.m_bRenderShadow = false;
        wcscpy(c->ID, L"벚꽃나무");
    }
    break;

    case MONSTER_DAVID:
        OpenNpc(MODEL_LUCKYITEM_NPC);
        c = CreateCharacter(Key, MODEL_LUCKYITEM_NPC, PositionX, PositionY);
        wcscpy(c->ID, L"Lucky Item NPC");
        c->Object.Scale = 0.95f;
        c->Object.m_fEdgeScale = 1.2f;
        Models[MODEL_LUCKYITEM_NPC].Actions[0].PlaySpeed = 0.45f;
        Models[MODEL_LUCKYITEM_NPC].Actions[1].PlaySpeed = 0.5f;

        //	Models[MODEL_LUCKYITEM_NPC].Actions[0].PlaySpeed = 50.0f;
        //	Models[MODEL_LUCKYITEM_NPC].Actions[1].PlaySpeed = 50.0f;
        break;
    case MONSTER_SEED_MASTER:
        OpenNpc(MODEL_SEED_MASTER);
        c = CreateCharacter(Key, MODEL_SEED_MASTER, PositionX, PositionY);
        wcscpy(c->ID, L"시드마스터");
        c->Object.Scale = 1.1f;
        c->Object.m_fEdgeScale = 1.2f;
        break;
    case MONSTER_SEED_RESEARCHER:
        OpenNpc(MODEL_SEED_INVESTIGATOR);
        c = CreateCharacter(Key, MODEL_SEED_INVESTIGATOR, PositionX, PositionY);
        wcscpy(c->ID, L"시드연구가");
        c->Object.Scale = 0.9f;
        c->Object.m_fEdgeScale = 1.15f;
        //Models[MODEL_SEED_INVESTIGATOR].Actions[0].PlaySpeed = 0.2f;
        //Models[MODEL_SEED_INVESTIGATOR].Actions[1].PlaySpeed = 0.1f;
        break;
    case MONSTER_REINIT_HELPER: {
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        //c->Class = 2;
        wcscpy(c->ID, L"초기화 도우미");

        c->BodyPart[BODYPART_HELM].Type = MODEL_PLATE_HELM;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_PLATE_ARMOR;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_PLATE_PANTS;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_PLATE_GLOVES;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_PLATE_BOOTS;

        c->Object.m_fEdgeScale = 1.15f;
        c->Weapon[0].Type = MODEL_LIGHT_CROSSBOW;
        c->Weapon[1].Type = MODEL_BOLT;
        SetCharacterScale(c);
    }
    break;
    case MONSTER_TRANSFORMED_SNOWMAN:
        OpenNpc(MODEL_XMAS2008_SNOWMAN);
        c = CreateCharacter(Key, MODEL_XMAS2008_SNOWMAN, PositionX, PositionY);
        ::wcscpy(c->ID, L"Unknown");
        c->Object.LifeTime = 100;
        c->Object.Scale = 1.3f;
        break;
#ifdef PJH_ADD_PANDA_CHANGERING
    case MONSTER_TRANSFORMED_PANDA:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        ::wcscpy(c->ID, L"Unknown");
        c->Object.SubType = MODEL_PANDA;
        break;
#endif //PJH_ADD_PANDA_CHANGERING
    case MONSTER_TRANSFORMED_SKELETON:
        c = CreateCharacter(Key, MODEL_PLAYER, PositionX, PositionY);
        ::wcscpy(c->ID, L"Unknown");
        c->Object.SubType = MODEL_SKELETON_CHANGED;
        break;
    case MONSTER_LITTLE_SANTA_YELLOW:
    case MONSTER_LITTLE_SANTA_GREEN:
    case MONSTER_LITTLE_SANTA_RED:
    case MONSTER_LITTLE_SANTA_BLUE:
    case MONSTER_LITTLE_SANTA_WHITE:
    case MONSTER_LITTLE_SANTA_BLACK:
    case MONSTER_LITTLE_SANTA_ORANGE:
    case MONSTER_LITTLE_SANTA_PINK: {
        int _Model_NpcIndex = MODEL_LITTLESANTA + (Type - 468);

        OpenNpc(_Model_NpcIndex);
        c = CreateCharacter(Key, _Model_NpcIndex, PositionX, PositionY);

        c->Object.Scale = 0.43f;

        for (int i = 0; i < 5; i++)
        {
            if (i < 2 || i == 4)
            {
                //xmassanta_stand_1~2 || xmassanta_idle3
                Models[_Model_NpcIndex].Actions[i].PlaySpeed = 0.4f;
            }
            else // if(i >= 2 && i < 4)
            {
                //xmassanta_idle1~2
                Models[_Model_NpcIndex].Actions[i].PlaySpeed = 0.5f;
            }
        }
        wcscpy(c->ID, L"little santa");
    }
    break;
    case MONSTER_DELGADO:
        //델가도
        OpenNpc(MODEL_NPC_SERBIS);
        c = CreateCharacter(Key, MODEL_NPC_SERBIS, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        break;
    case MONSTER_GATEKEEPER_TITUS:
        // 결투장 문지기 NPC 타이투스
        OpenNpc(MODEL_DUEL_NPC_TITUS);
        c = CreateCharacter(Key, MODEL_DUEL_NPC_TITUS, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.1f;
        c->Object.m_fEdgeScale = 1.2f;
        break;
    case MONSTER_MOSS_THE_MERCHANT: {
        OpenNpc(MODEL_GAMBLE_NPC_MOSS);
        c = CreateCharacter(Key, MODEL_GAMBLE_NPC_MOSS, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.LifeTime = 100;
        c->Object.Scale = 0.8f;
        c->Object.m_fEdgeScale = 1.1f;

        for (int i = 0; i < 6; i++)
        {
            Models[MODEL_GAMBLE_NPC_MOSS].Actions[i].PlaySpeed = 0.33f;
        }
    }
    break;
    case MONSTER_GOLDEN_RABBIT:
        OpenMonsterModel(MONSTER_MODEL_RABBIT);
        c = CreateCharacter(Key, MODEL_RABBIT, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.0f * 0.95f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;
    case MONSTER_GOLDEN_DARK_KNIGHT:
        OpenMonsterModel(MONSTER_MODEL_DARK_KNIGHT);
        c = CreateCharacter(Key, MODEL_DARK_KNIGHT, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 0.8f;
        c->Level = 1;
        c->Weapon[0].Type = MODEL_DOUBLE_BLADE;
        break;
        break;
    case MONSTER_GOLDEN_DEVIL:
        OpenMonsterModel(MONSTER_MODEL_DEVIL);
        c = CreateCharacter(Key, MODEL_DEVIL, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.1f;
        break;
    case MONSTER_GOLDEN_STONE_GOLEM:
        OpenMonsterModel(MONSTER_MODEL_GOLDEN_STONE_GOLEM);
        c = CreateCharacter(Key, MODEL_GOLDEN_STONE_GOLEM, PositionX, PositionY);
        c->Object.Scale = 1.35f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        boneManager_.RegisterBone(c, L"Monster101_L_Arm", CharacterSocket::Monster101_L_Arm);
        boneManager_.RegisterBone(c, L"Monster101_R_Arm", CharacterSocket::Monster101_R_Arm);
        boneManager_.RegisterBone(c, L"Monster101_Head", CharacterSocket::Monster101_Head);
        break;
    case MONSTER_GOLDEN_CRUST:
        OpenMonsterModel(MONSTER_MODEL_CRUST);
        c = CreateCharacter(Key, MODEL_CRUST, PositionX, PositionY);
        c->Object.Scale = 1.1f;
        c->Weapon[0].Type = MODEL_THUNDER_BLADE;
        c->Weapon[0].Level = 5;
        c->Weapon[1].Type = MODEL_LEGENDARY_SHIELD;
        c->Weapon[1].Level = 0;
        c->Object.BlendMesh = 1;
        c->Object.BlendMeshLight = 1.f;
        break;
    case MONSTER_GOLDEN_SATYROS:
        OpenMonsterModel(MONSTER_MODEL_SATYROS);
        c = CreateCharacter(Key, MODEL_SATYROS, PositionX, PositionY);
        c->Object.Scale = 1.3f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        wcscpy(c->ID, L"Unknown");
        break;
    case MONSTER_GOLDEN_TWIN_TAIL:
        OpenMonsterModel(MONSTER_MODEL_TWIN_TAIL);
        c = CreateCharacter(Key, MODEL_TWIN_TAIL, PositionX, PositionY);
        c->Object.Scale = 1.3f;
        c->Object.Angle[0] = 0.0f;
        c->Object.Gravity = 0.0f;
        c->Object.Distance = (float)(WorldRandom() % 20) / 10.0f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        boneManager_.RegisterBone(c, L"Twintail_Hair24", CharacterSocket::Twintail_Hair24);
        boneManager_.RegisterBone(c, L"Twintail_Hair32", CharacterSocket::Twintail_Hair32);
        break;
    case MONSTER_GOLDEN_IRON_KNIGHT:
        OpenMonsterModel(MONSTER_MODEL_IRON_KNIGHT);
        c = CreateCharacter(Key, MODEL_IRON_KNIGHT, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.5f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;
    case MONSTER_GOLDEN_NAPIN:
        OpenMonsterModel(MONSTER_MODEL_NAPIN);
        c = CreateCharacter(Key, MODEL_NAPIN, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 0.95f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;
    case MONSTER_GOLDEN_GREAT_DRAGON:
        OpenMonsterModel(MONSTER_MODEL_DRAGON);
        c = CreateCharacter(Key, MODEL_DRAGON_, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 0.88f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;
    case MONSTER_LUGARD:
        OpenNpc(MODEL_DOPPELGANGER_NPC_LUGARD);
        c = CreateCharacter(Key, MODEL_DOPPELGANGER_NPC_LUGARD, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.1f;
        c->Object.m_fEdgeScale = 1.2f;
        break;
    case MONSTER_COMPENSATION_BOX:
        OpenNpc(MODEL_DOPPELGANGER_NPC_BOX);
        c = CreateCharacter(Key, MODEL_DOPPELGANGER_NPC_BOX, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 2.3f;
        c->Object.m_fEdgeScale = 1.1f;
        break;
    case MONSTER_GOLDEN_COMPENSATION_BOX:
        OpenNpc(MODEL_DOPPELGANGER_NPC_GOLDENBOX);
        c = CreateCharacter(Key, MODEL_DOPPELGANGER_NPC_GOLDENBOX, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 3.3f;
        c->Object.m_fEdgeScale = 1.1f;
        break;
    case MONSTER_GENS_DUPRIAN:
        OpenNpc(MODAL_GENS_NPC_DUPRIAN);
        c = CreateCharacter(Key, MODAL_GENS_NPC_DUPRIAN, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.0f;
        break;
    case MONSTER_GENS_VANERT:
        OpenNpc(MODAL_GENS_NPC_BARNERT);
        c = CreateCharacter(Key, MODAL_GENS_NPC_BARNERT, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.0f;
        break;
    case MONSTER_CHRISTINE_THE_GENERAL_GOODS_MERCHANT:
        OpenNpc(MODEL_UNITEDMARKETPLACE_CHRISTIN);
        c = CreateCharacter(Key, MODEL_UNITEDMARKETPLACE_CHRISTIN, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.1f;
        c->Object.m_fEdgeScale = 1.2f;
        break;
    case MONSTER_JEWELER_RAUL:
        OpenNpc(MODEL_UNITEDMARKETPLACE_RAUL);
        c = CreateCharacter(Key, MODEL_UNITEDMARKETPLACE_RAUL, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.0f;
        c->Object.m_fEdgeScale = 1.15f;
        break;
    case MONSTER_MARKET_UNION_MEMBER_JULIA:
        OpenNpc(MODEL_UNITEDMARKETPLACE_JULIA);
        c = CreateCharacter(Key, MODEL_UNITEDMARKETPLACE_JULIA, PositionX, PositionY);
        wcscpy(c->ID, L"Unknown");
        c->Object.Scale = 1.0f;
        c->Object.m_fEdgeScale = 1.1f;
        break;

    case MONSTER_MERCENARY_GUILD_FELICIA:
        OpenNpc(MODEL_TERSIA);
        c = CreateCharacter(Key, MODEL_TERSIA, PositionX, PositionY);
        wcscpy(c->ID, L"길드관리인 테르시아");
        c->Object.Scale = 0.93f;
        break;
    case MONSTER_PRIESTESS_VEINA:
        OpenNpc(MODEL_BENA);
        c = CreateCharacter(Key, MODEL_BENA, PositionX, PositionY);
        wcscpy(c->ID, L"신녀 베이나");
        c->Object.Position[2] += 145.0f;
        break;
    case MONSTER_LEINA_THE_GENERAL_GOODS_MERCHANT:
        OpenNpc(MODEL_KARUTAN_NPC_REINA);
        c = CreateCharacter(Key, MODEL_KARUTAN_NPC_REINA, PositionX, PositionY);
        wcscpy(c->ID, L"잡화상인 레이나");
        c->Object.Scale = 1.1f;
        c->Object.m_fEdgeScale = 1.2f;
        break;
    case MONSTER_WEAPONS_MERCHANT_BOLO:
        OpenNpc(MODEL_KARUTAN_NPC_VOLVO);
        c = CreateCharacter(Key, MODEL_KARUTAN_NPC_VOLVO, PositionX, PositionY);
        wcscpy(c->ID, L"무기상인 볼로");
        c->Object.Scale = 0.9f;
        break;
    }

    Setting_Monster(c, Type, PositionX, PositionY);

    return c;
}

CHARACTER *SessionGameplayUnit::CreateHero(int Index, CLASS_TYPE Class, int Skin, float x, float y,
                                           float Rotate)
{
    CHARACTER *c = CharactersClient.AcquireLocalAt(Index, Index);
    if (c == nullptr)
        return nullptr;
    if (!CharactersClient.HasWorldInstance() && Index == 0)
        Hero = c;
    OBJECT *o = &c->Object;
    CreateCharacterPointer(c, MODEL_PLAYER, 0, 0, Rotate);
    Vector(0.3f, 0.3f, 0.3f, o->Light);
    c->Key = Index;
    o->Position[0] = x;
    o->Position[1] = y;
    if (SceneFlag == CHARACTER_SCENE)
        o->Position[2] = 163.f;
    c->Class = Class;
    c->SkinIndex = gCharacterManager.GetSkinModelIndex(c->Class);
    c->Skin = Skin;

    g_CharacterClearBuff(o);

    if (SceneFlag == LOG_IN_SCENE)
    {
        c->BodyPart[BODYPART_HELM].Type = MODEL_DARK_MASTER_MASK;
        c->BodyPart[BODYPART_HELM].Level = 7;
        c->BodyPart[BODYPART_ARMOR].Type = MODEL_DARK_MASTER_ARMOR;
        c->BodyPart[BODYPART_ARMOR].Level = 7;
        c->BodyPart[BODYPART_PANTS].Type = MODEL_DARK_MASTER_PANTS;
        c->BodyPart[BODYPART_PANTS].Level = 7;
        c->BodyPart[BODYPART_GLOVES].Type = MODEL_DARK_MASTER_GLOVES;
        c->BodyPart[BODYPART_GLOVES].Level = 7;
        c->BodyPart[BODYPART_BOOTS].Type = MODEL_DARK_MASTER_BOOTS;
        c->BodyPart[BODYPART_BOOTS].Level = 7;
        c->Weapon[0].Type = MODEL_SOLEIL_SCEPTER;
        c->Weapon[1].Type = MODEL_DARK_RAVEN_ITEM;
        c->Weapon[0].Level = 13;
        c->Wing.Type = MODEL_CAPE_OF_LORD;
        c->Helper.Type = MODEL_DARK_HORSE_ITEM;
        //c->Helper.Level = 13;
    }
    else
    {
        c->BodyPart[BODYPART_HELM].Type = static_cast<int>(MODEL_BODY_HELM) + c->SkinIndex;
        c->BodyPart[BODYPART_ARMOR].Type = static_cast<int>(MODEL_BODY_ARMOR) + c->SkinIndex;
        c->BodyPart[BODYPART_PANTS].Type = static_cast<int>(MODEL_BODY_PANTS) + c->SkinIndex;
        c->BodyPart[BODYPART_GLOVES].Type = static_cast<int>(MODEL_BODY_GLOVES) + c->SkinIndex;
        c->BodyPart[BODYPART_BOOTS].Type = static_cast<int>(MODEL_BODY_BOOTS) + c->SkinIndex;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        c->Wing.Type = -1;
        c->Helper.Type = -1;
    }

    UnRegisterBuff(eBuff_GMEffect, o);

    c->CtlCode = 0;
    SetCharacterScale(c);
    SetPlayerStop(c);
    return c;
}

CHARACTER *SessionGameplayUnit::CreateHellGate(char *ID, int Key, EMonsterType Index, int x, int y,
                                               int CreateFlag)
{
    CHARACTER *portal = CreateMonster(Index, x, y, Key);
    portal->Level = Index - 152 + 1;
    wchar_t portalText[100];
    wchar_t name[sizeof portal->ID];

    CMultiLanguage::ConvertFromUtf8(name, ID);

    mu_swprintf(portalText, portal->ID, name);

    if (portal->Level == 7)
        portal->Object.SubType = 1;

    memcpy(portal->ID, portalText, sizeof portalText);

    if (CreateFlag)
    {
        CreateJoint(BITMAP_JOINT_THUNDER + 1, portal->Object.Position, portal->Object.Position,
                    portal->Object.Angle, 1, NULL, 60.f + WorldRandom() % 10);
        CreateJoint(BITMAP_JOINT_THUNDER + 1, portal->Object.Position, portal->Object.Position,
                    portal->Object.Angle, 1, NULL, 50.f + WorldRandom() % 10);
        CreateJoint(BITMAP_JOINT_THUNDER + 1, portal->Object.Position, portal->Object.Position,
                    portal->Object.Angle, 1, NULL, 50.f + WorldRandom() % 10);
        CreateJoint(BITMAP_JOINT_THUNDER + 1, portal->Object.Position, portal->Object.Position,
                    portal->Object.Angle, 1, NULL, 60.f + WorldRandom() % 10);
    }
    return portal;
}

BOOL SessionGameplayUnit::PlayMonsterSoundGlobal(OBJECT *pObject)
{
    float fDis_x, fDis_y;
    fDis_x = pObject->Position[0] - Hero->Object.Position[0];
    fDis_y = pObject->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (pObject->Type)
    {
    case MODEL_CURSED_SANTA:
        if (pObject->CurrentAction == MONSTER01_STOP1)
        {
            // 			if (rand_fps_check(10))
            {
                if (rand_fps_check(2))
                    PlayBuffer(SOUND_XMAS_SANTA_IDLE_1);
                else
                    PlayBuffer(SOUND_XMAS_SANTA_IDLE_2);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_WALK)
        {
            //if (rand_fps_check(10))
            {
                if (rand_fps_check(2))
                    PlayBuffer(SOUND_XMAS_SANTA_WALK_1);
                else
                    PlayBuffer(SOUND_XMAS_SANTA_WALK_2);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_XMAS_SANTA_ATTACK_1);
        }
        else if (pObject->CurrentAction == MONSTER01_SHOCK)
        {
            if (rand_fps_check(2))
                PlayBuffer(SOUND_XMAS_SANTA_DAMAGE_1);
            else
                PlayBuffer(SOUND_XMAS_SANTA_DAMAGE_2);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_XMAS_SANTA_DEATH_1);
        }
        return TRUE;

    case MODEL_XMAS2008_SNOWMAN:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            PlayBuffer(SOUND_XMAS_SNOWMAN_WALK_1);
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1)
        {
            PlayBuffer(SOUND_XMAS_SNOWMAN_ATTACK_1);
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_XMAS_SNOWMAN_ATTACK_2);
        }
        else if (pObject->CurrentAction == MONSTER01_SHOCK)
        {
            PlayBuffer(SOUND_XMAS_SNOWMAN_DAMAGE_1);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            if (pObject->LifeTime == 100)
            {
                PlayBuffer(SOUND_XMAS_SNOWMAN_DEATH_1);
            }
        }
        return TRUE;
    case MODEL_DUEL_NPC_TITUS:
        if (pObject->CurrentAction == MONSTER01_STOP1)
        {
            PlayBuffer(SOUND_DUEL_NPC_IDLE_1);
        }
        return TRUE;
    case MODEL_DOPPELGANGER_NPC_LUGARD:
        if (pObject->CurrentAction == MONSTER01_STOP1)
        {
            if (rand_fps_check(2))
                PlayBuffer(SOUND_DOPPELGANGER_LUGARD_BREATH);
        }
        return TRUE;
    case MODEL_DOPPELGANGER_NPC_BOX:
        if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_DOPPELGANGER_JEWELBOX_OPEN);
        }
        return TRUE;
    case MODEL_DOPPELGANGER_NPC_GOLDENBOX:
        if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_DOPPELGANGER_JEWELBOX_OPEN);
        }
        return TRUE;
    }

    return FALSE;
}

bool IsPlayer(CHARACTER *c)
{
    return c && c->Object.Kind == KIND_PLAYER;
}

bool IsMonster(CHARACTER *c)
{
    return c && c->Object.Kind == KIND_MONSTER;
}

bool SessionGameplayUnit::CharacterVisibleToObserver(const CHARACTER &character)
{
    return character.Object.Live &&
           !CharacterPresentationDetail::IsExpiredXmasCharacter(character.Object) &&
           !(&character == Hero && (0x04 & Hero->CtlCode) && SceneFlag == MAIN_SCENE) &&
           TheMapProcess().CanObserveCharacter(character);
}

CLASS_TYPE CCharacterManager::ChangeServerClassTypeToClientClassType(
    const SERVER_CLASS_TYPE byServerClassType) const
{
    switch (byServerClassType)
    {
    case DarkWizard:
        return CLASS_WIZARD;
    case SoulMaster:
        return CLASS_SOULMASTER;
    case GrandMaster:
        return CLASS_GRANDMASTER;
    case DarkKnight:
        return CLASS_KNIGHT;
    case BladeKnight:
        return CLASS_BLADEKNIGHT;
    case BladeMaster:
        return CLASS_BLADEMASTER;
    case FairyElf:
        return CLASS_ELF;
    case MuseElf:
        return CLASS_MUSEELF;
    case HighElf:
        return CLASS_HIGHELF;
    case MagicGladiator:
        return CLASS_DARK;
    case DuelMaster:
        return CLASS_DUELMASTER;
    case DarkLord:
        return CLASS_DARK_LORD;
    case LordEmperor:
        return CLASS_LORDEMPEROR;
    case Summoner:
        return CLASS_SUMMONER;
    case BloodySummoner:
        return CLASS_BLOODYSUMMONER;
    case DimensionMaster:
        return CLASS_DIMENSIONMASTER;
    case RageFighter:
        return CLASS_RAGEFIGHTER;
    case FistMaster:
        return CLASS_TEMPLENIGHT;
    }

    return CLASS_WIZARD;
}

CLASS_TYPE CCharacterManager::GetBaseClass(CLASS_TYPE iClass) const
{
    switch (iClass)
    {
    case CLASS_GRANDMASTER:
    case CLASS_SOULMASTER:
        return CLASS_WIZARD;
    case CLASS_BLADEKNIGHT:
    case CLASS_BLADEMASTER:
        return CLASS_KNIGHT;
    case CLASS_MUSEELF:
    case CLASS_HIGHELF:
        return CLASS_ELF;
    case CLASS_BLOODYSUMMONER:
    case CLASS_DIMENSIONMASTER:
        return CLASS_SUMMONER;
    case CLASS_DUELMASTER:
        return CLASS_DARK;
    case CLASS_LORDEMPEROR:
        return CLASS_DARK_LORD;
    case CLASS_TEMPLENIGHT:
        return CLASS_RAGEFIGHTER;
    }

    return iClass;
}

bool CCharacterManager::IsSecondClass(const CLASS_TYPE byClass) const
{
    switch (byClass)
    {
    case CLASS_SOULMASTER:
    case CLASS_BLADEKNIGHT:
    case CLASS_MUSEELF:
    case CLASS_BLOODYSUMMONER:
        return true;
    }

    return false;
}

bool CCharacterManager::IsThirdClass(const CLASS_TYPE byClass) const
{
    switch (byClass)
    {
    case CLASS_GRANDMASTER:
    case CLASS_BLADEMASTER:
    case CLASS_HIGHELF:
    case CLASS_DIMENSIONMASTER:
    case CLASS_DUELMASTER:
    case CLASS_LORDEMPEROR:
    case CLASS_TEMPLENIGHT:
        return true;
    }

    return false;
}

bool CCharacterManager::IsMasterLevel(const CLASS_TYPE byClass) const
{
    return this->IsThirdClass(byClass);
}

bool CCharacterManager::IsMasterExperienceActive(const CLASS_TYPE byClass, const int level) const
{
    return this->IsMasterLevel(byClass) &&
           level >= CharacterRulesDetail::kMasterExperienceUnlockLevel;
}

const wchar_t *CCharacterManager::GetCharacterClassText(const CLASS_TYPE byCharacterClass) const
{
    const auto it =
        std::find_if(CharacterRulesDetail::kClassTextEntries.begin(),
                     CharacterRulesDetail::kClassTextEntries.end(),
                     [byCharacterClass](const CharacterRulesDetail::ClassTextEntry &entry) {
                         return entry.type == byCharacterClass;
                     });
    return (it != CharacterRulesDetail::kClassTextEntries.end())
               ? I18N::Game::Lookup(it->textIndex)
               : I18N::Game::Lookup(CharacterRulesDetail::kDefaultClassTextIndex);
}

CLASS_SKIN_INDEX CCharacterManager::GetSkinModelIndex(const CLASS_TYPE byClass) const
{
    switch (byClass)
    {
    case CLASS_BLOODYSUMMONER:
        return SKIN_CLASS_BLOODYSUMMONER;
    case CLASS_GRANDMASTER:
        return SKIN_CLASS_GRANDMASTER;
    case CLASS_BLADEMASTER:
        return SKIN_CLASS_BLADEMASTER;
    case CLASS_HIGHELF:
        return SKIN_CLASS_HIGHELF;
    case CLASS_DUELMASTER:
        return SKIN_CLASS_DUELMASTER;
    case CLASS_LORDEMPEROR:
        return SKIN_CLASS_LORDEMPEROR;
    case CLASS_DIMENSIONMASTER:
        return SKIN_CLASS_DIMENSIONMASTER;
    case CLASS_TEMPLENIGHT:
        return SKIN_CLASS_TEMPLENIGHT;
    }

    return static_cast<CLASS_SKIN_INDEX>(byClass);
}

BYTE CCharacterManager::GetStepClass(const CLASS_TYPE byClass) const
{
    if (IsThirdClass(byClass))
    {
        return 3;
    }
    else if (this->IsSecondClass(byClass) == true && this->IsThirdClass(byClass) == false)
    {
        return 2;
    }
    else
    {
        return 1;
    }
}

int CCharacterManager::GetEquipedBowType(const CHARACTER *pChar) const
{
    if (CharacterRulesDetail::IsBowModel(pChar->Weapon[1].Type))
    {
        return BOWTYPE_BOW;
    }
    if (CharacterRulesDetail::IsCrossbowModel(pChar->Weapon[0].Type))
    {
        return BOWTYPE_CROSSBOW;
    }
    return BOWTYPE_NONE;
}

int CCharacterManager::GetEquipedBowType(ITEM *pItem) const
{
    if (CharacterRulesDetail::IsGeneralBowItem(pItem->Type))
    {
        return BOWTYPE_BOW;
    }

    if (CharacterRulesDetail::IsGeneralCrossbowItem(pItem->Type))
    {
        return BOWTYPE_CROSSBOW;
    }
    return BOWTYPE_NONE;
}
//  CSPetSystem.

namespace PetSystemDetail
{
void OwnerBonePosition(const OBJECT &owner, int bone, const vec3_t offset, vec3_t position)
{
    VectorTransform(offset, owner.BoneTransform[bone], position);
    VectorScale(position, owner.Scale, position);
    VectorAdd(position, owner.Position, position);
}
void SampleOwnerBonePosition(const CHARACTER &character, BMD &model, int bone, const vec3_t offset,
                             double time, float fraction, vec3_t position)
{
    const OBJECT &owner = character.Object;
    if (fraction >= 1.f)
    {
        OwnerBonePosition(owner, bone, offset, position);
        return;
    }
    character.WorldVisualPoseSample.SampleBonePosition(model, owner, bone, offset, time, fraction,
                                                       position);
}

} // namespace PetSystemDetail

CSPetSystem::CSPetSystem(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), Random(keeper.RandomForConstruction()), m_PetOwner(nullptr),
      m_PetTarget(nullptr), m_PetCharacter(), m_PetType(PET_TYPE_NONE),
      m_byCommand(PET_CMD_DEFAULT), m_BoneTransforms()
{
    m_PetCharacter.Object.BoneTransform = nullptr;
}

CSPetSystem::~CSPetSystem()
{
    if (auto *gameplay = sessionKeeper_.Gameplay(); gameplay && !effectsRetired_)
    {
        OBJECT *targets[]{&m_PetCharacter.Object};
        gameplay->RetireCharacterEffectTargets(targets);
    }
    m_BoneTransforms.reset();
    m_PetCharacter.Object.BoneTransform = nullptr;
}

void CSPetSystem::SetPetInfo(PET_INFO *info)
{
    if (info && m_PetOwner == Hero)
        m_PetOwner->PetCommands.level = info->m_wLevel;
    if (m_PetOwner)
        petLevel_ = m_PetOwner->PetCommands.level;
}

void CSPetSystem::CreatePetPointer(int Type, unsigned char PositionX, unsigned char PositionY,
                                   float Rotation)
{
    poseSample_ = {};
    CHARACTER *c = &m_PetCharacter;
    OBJECT *o = &c->Object;

    m_PetTarget = nullptr;
    m_targetSource.reset();
    m_byCommand = PET_CMD_DEFAULT;
    petLevel_ = 0;

    o->Initialize();
    c->PositionX = PositionX;
    c->PositionY = PositionY;
    c->TargetX = PositionX;
    c->TargetY = PositionY;

    c->byExtensionSkill = 0;
    c->PetCommands = {};

    int Index = TERRAIN_INDEX_REPEAT(c->PositionX, c->PositionY);
    if ((TerrainWall[Index] & TW_SAFEZONE) == TW_SAFEZONE)
        c->SafeZone = true;
    else
        c->SafeZone = false;

    c->Path.PathNum = 0;
    c->Path.CurrentPath = 0;
    c->Movement = false;
    o->Live = true;
    o->Visible = false;
    o->AlphaEnable = true;
    o->LightEnable = true;
    o->ContrastEnable = false;
    o->EnableBoneMatrix = true;
    o->EnableShadow = false;
    c->Dead = 0;
    c->Blood = false;
    c->GuildTeam = 0;
    c->Run = 0;
    c->GuildMarkIndex = -1;
    c->PK = PVP_NEUTRAL;
    o->Type = Type;
    o->Scale = 0.7f;
    o->Timer = 0.f;
    o->Alpha = 1.f;
    o->AlphaTarget = 1.f;
    o->Velocity = 0.f;
    o->ShadowScale = 0.f;
    o->m_byHurtByDeathstab = 0;
    o->AI = 0;
    o->Velocity = 1.f;
    o->Gravity = 13;
    c->ExtendState = 0;
    c->ExtendStateTime = 0;

    c->GuildStatus = -1;
    c->GuildType = 0;
    c->ProtectGuildMarkWorldTime = 0.0f;

    c->m_byDieType = 0;
    o->m_bActionStart = false;
    o->m_bySkillCount = 0;

    c->Class = CLASS_WIZARD;
    o->PriorAction = 0;
    o->CurrentAction = 0;
    o->AnimationFrame = 0.f;
    o->PriorAnimationFrame = 0;
    c->JumpTime = 0;
    o->HiddenMesh = -1;
    c->MoveSpeed = 10;

    g_CharacterClearBuff(o);

    o->Teleport = TELEPORT_NONE;
    o->Kind = KIND_PET;
    c->Change = false;
    o->SubType = 0;
    c->MonsterIndex = MONSTER_UNDEFINED;
    o->BlendMeshTexCoordU = 0.f;
    o->BlendMeshTexCoordV = 0.f;
    o->Position[0] = (float)(PositionX * TERRAIN_SCALE) + 0.5f * TERRAIN_SCALE;
    o->Position[1] = (float)(PositionY * TERRAIN_SCALE) + 0.5f * TERRAIN_SCALE;

    o->InitialSceneTime = WorldTime;
    o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);

    Vector(0.f, 0.f, Rotation, o->Angle);
    Vector(0.5f, 0.5f, 0.5f, o->Light);
    Vector(-60.f, -60.f, 0.f, o->BoundingBoxMin);
    Vector(50.f, 50.f, 150.f, o->BoundingBoxMax);

    const int boneCount = Models[Type].NumBones;
    if (boneCount > 0)
    {
        m_BoneTransforms = std::make_unique<vec34_t[]>(boneCount);
        o->BoneTransform = m_BoneTransforms.get();
    }
    else
    {
        m_BoneTransforms.reset();
        o->BoneTransform = nullptr;
    }

    int i;
    for (i = 0; i < 2; i++)
    {
        c->Weapon[i].Type = -1;
        c->Weapon[i].Level = 0;
        c->Weapon[i].ExcellentFlags = 0;
    }
    for (i = 0; i < MAX_BODYPART; i++)
    {
        c->BodyPart[i].Type = -1;
        c->BodyPart[i].Level = 0;
        c->BodyPart[i].ExcellentFlags = 0;
        c->BodyPart[i].AncientDiscriminator = 0;
    }

    c->Wing.Type = -1;
    c->Helper.Type = -1;
    c->Flag.Type = -1;

    c->LongRangeAttack = -1;
    c->CollisionTime = 0;
    o->CollisionRange = 200.f;
    c->Rot = 0.f;
    c->Level = 0;
    c->Item = -1;

    o->BlendMesh = -1;
    o->BlendMeshLight = 1.f;

    c->Weapon[0].LinkBone = 0;
    c->Weapon[1].LinkBone = 0;
    m_byCommand = PET_CMD_DEFAULT;
}

bool CSPetSystem::PlayAnimation(OBJECT *o, float frames)
{
    BMD *b = &Models[o->Type];
    float playSpeed = 0.1f;

    switch (m_PetType)
    {
    case PET_TYPE_DARK_SPIRIT:
        playSpeed = 0.4f;
        break;
    }

    b->CurrentAction = o->CurrentAction;
    const ObjectMotionTrace::AnimationPhase phase{o->AnimationFrame, o->PriorAnimationFrame,
                                                  o->CurrentAction, o->PriorAction};
    const bool playing =
        b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction, playSpeed,
                         o->Position, o->Angle, frames);
    o->MotionTrace.AdvanceAnimation(frames, phase, playSpeed * frames);
    return playing;
}

void CSPetSystem::ClearTarget()
{
    m_PetTarget = nullptr;
    m_targetSource.reset();
    m_PetCharacter.TargetCharacter = -1;
    m_PetCharacter.Object.m_bActionStart = false;
    m_byCommand = PET_CMD_DEFAULT;
    SetAI(PET_FLYING);
}

void CSPetSystem::ForgetTargets(const std::unordered_set<const OBJECT *> &targets)
{
    if (m_PetTarget && targets.contains(&m_PetTarget->Object))
        ClearTarget();
}

void CSPetSystem::SetAI(int AI)
{
    m_PetCharacter.Object.AI = AI;
    m_PetCharacter.Object.LifeTime = 0;
}

void CSPetSystem::SetCommand(int Key, std::uint8_t cmd)
{
    m_byCommand = cmd;
    if (m_PetCharacter.Object.AI != PET_ATTACK && m_PetCharacter.Object.AI != PET_ATTACK_MAGIC)
    {
        m_PetCharacter.Object.m_bActionStart = false;
    }
    if (cmd == PET_CMD_TARGET)
    {
        const int index = FindCharacterIndex(Key);
        if (!CharactersClient.IsValidIndex(index))
        {
            ClearTarget();
            return;
        }

        m_PetTarget = &CharactersClient[index];
        m_targetSource = m_PetTarget->SocketSource;

        m_PetCharacter.Object.m_bActionStart = true;
    }
}

void CSPetSystem::SetAttack(int Key, int attackType)
{
    const int index = FindCharacterIndex(Key);
    if (!CharactersClient.IsValidIndex(index) || m_PetOwner == nullptr)
    {
        return;
    }

    m_PetTarget = &CharactersClient[index];
    m_targetSource = m_PetTarget->SocketSource;
    OBJECT *Owner = &m_PetOwner->Object;

    if (g_isCharacterBuff(Owner, eDeBuff_Stun))
    {
        return;
    }
    else if (g_isCharacterBuff(Owner, eBuff_Cloaking))
    {
        m_PetCharacter.TargetCharacter = index;
        SetAI(PET_ATTACK + attackType);
        return;
    }

    m_PetCharacter.TargetCharacter = index;
    m_PetCharacter.AttackTime = 0;
    m_PetCharacter.LastAttackEffectTime = -1;
    SetAI(PET_ATTACK + attackType);

    if (m_PetCharacter.Object.AI == PET_ATTACK)
    {
        OBJECT *o = &m_PetCharacter.Object;

        o->m_bActionStart = true;
        o->Velocity = Random.RangeFloat(0, 9) + 20.f;
        o->Gravity = 0.5f;

        PlayBuffer(SOUND_DSPIRIT_RUSH);
    }
    else if (m_PetCharacter.Object.AI == PET_ATTACK_MAGIC)
    {
        PlayBuffer(SOUND_DSPIRIT_MISSILE);
    }
}

void CSPetSystem::MoveInventory(void)
{
}

CSPetDarkSpirit::CSPetDarkSpirit(SessionKeeper &keeper, CHARACTER *c) : CSPetSystem(keeper)
{
    m_PetType = PET_TYPE_DARK_SPIRIT;
    m_PetOwner = c;

    m_PetCharacter.Object.BoneTransform = NULL;
    CreatePetPointer(MODEL_DARK_SPIRIT, (c->PositionX), (c->PositionY), 0.f);

    m_PetCharacter.Object.Position[2] += 300.f;
    m_PetCharacter.Object.CurrentAction = 0;
}

CSPetDarkSpirit::~CSPetDarkSpirit(void) = default;

namespace PetSystemDetail
{

float PetRushDistance(const OBJECT &pet, float frames)
{
    if (pet.AI == PET_ESCAPE)
        return frames * (pet.Velocity - (frames - 1.f) * 0.5f);
    constexpr float Jerk = 0.2f;
    return frames * (pet.Velocity + pet.Gravity * (frames - 1.f) * 0.5f +
                     Jerk * (frames - 1.f) * (frames - 2.f) / 6.f);
}

float AdvancePetRush(OBJECT &pet, float frames)
{
    const float travel = PetRushDistance(pet, frames);
    if (pet.AI == PET_ESCAPE)
        pet.Velocity -= frames;
    else
    {
        constexpr float Jerk = 0.2f;
        pet.Velocity += pet.Gravity * frames + Jerk * frames * (frames - 1.f) * 0.5f;
        pet.Gravity += Jerk * frames;
    }
    return travel;
}

void RescueDistantPet(OBJECT &pet, const vec3_t ownerPosition, float frames)
{
    vec3_t TargetPosition, Range;
    VectorCopy(ownerPosition, TargetPosition);
    VectorSubtract(TargetPosition, pet.Position, Range);
    float Distance = Range[0] * Range[0] + Range[1] * Range[1];
    if (pet.Position[2] < (TargetPosition[2] - 200.f) || Distance > 409600.f)
    {
        pet.LifeTime += frames;
    }
    if (pet.LifeTime > 90)
    {
        pet.LifeTime = 0;
        VectorCopy(TargetPosition, pet.Position);
        pet.Position[2] += 250.f;
    }
}
} // namespace PetSystemDetail

void CSPetDarkSpirit::MovePet(bool forceRender)
{
    // The lifetime source is cleared at replacement, before any observer advances.
    if (m_PetTarget && !m_targetSource->object)
        ClearTarget();

    CHARACTER *c = &m_PetCharacter;
    OBJECT *o = &c->Object;
    OBJECT *Owner = &m_PetOwner->Object;

    if (g_isCharacterBuff(Owner, eDeBuff_Stun))
        return;

    const int ownerLevel = std::clamp(static_cast<int>(c->Level), 0, 0xFF);
    o->WeaponLevel = static_cast<std::uint8_t>(ownerLevel);

    if (m_PetOwner == Hero && !g_DuelMgr.IsPetDuelEnabled())
    {
        m_byCommand = 0;
        SocketClient->ToGameServer()->SendPetCommandRequest(static_cast<PetType>(GetPetType()),
                                                            PetCommandMode::Normal, 0xFFFF);
        g_DuelMgr.EnablePetDuel(TRUE);
    }

    if (CharactersClient.IsValidIndex(c->TargetCharacter))
    {
        CHARACTER *tc = &CharactersClient[c->TargetCharacter];

        if ((g_isCharacterBuff((&tc->Object), eBuff_Cloaking) ||
             g_isCharacterBuff(Owner, eBuff_Cloaking)) &&
            (o->AI == PET_ATTACK || o->AI == PET_ESCAPE || o->AI == PET_ATTACK_MAGIC))
        {

            float dx = o->Position[0] - Owner->Position[0];
            float dy = o->Position[1] - Owner->Position[1];
            float Distance = sqrtf(dx * dx + dy * dy);

            c->TargetCharacter = -1;
            if (m_PetOwner == Hero)
                SocketClient->ToGameServer()->SendPetCommandRequest(
                    static_cast<PetType>(GetPetType()), PetCommandMode::Normal, 0xFFFF);
            SetAI(PET_STAND);
            if (Distance > 50 ||
                (o->AI != PET_STAND_START && o->AI >= PET_FLYING && o->AI <= PET_STAND))
            {
                SetAI(PET_STAND_START);
                o->Velocity = 3.f;
            }
        }
    }
    else if ((g_isCharacterBuff(Owner, eBuff_Cloaking)) &&
             (o->AI == PET_ATTACK || o->AI == PET_ESCAPE || o->AI == PET_ATTACK_MAGIC))
    {

        float dx = o->Position[0] - Owner->Position[0];
        float dy = o->Position[1] - Owner->Position[1];
        float Distance = sqrtf(dx * dx + dy * dy);

        c->TargetCharacter = -1;
        if (m_PetOwner == Hero)
            SocketClient->ToGameServer()->SendPetCommandRequest(static_cast<PetType>(GetPetType()),
                                                                PetCommandMode::Normal, 0xFFFF);
        SetAI(PET_STAND);
        if (Distance > 50 ||
            (o->AI != PET_STAND_START && o->AI >= PET_FLYING && o->AI <= PET_STAND))
        {
            SetAI(PET_STAND_START);
            o->Velocity = 3.f;
        }
    }

    if (m_PetOwner->SafeZone == true)
    {
        if (o->AI != PET_STAND && o->AI != PET_STAND_START)
        {
            float dx = o->Position[0] - Owner->Position[0];
            float dy = o->Position[1] - Owner->Position[1];
            float Distance = sqrtf(dx * dx + dy * dy);

            SetAI(PET_STAND);
            if (Distance > 50 ||
                (o->AI != PET_STAND_START && o->AI >= PET_FLYING && o->AI <= PET_STAND))
            {
                SetAI(PET_STAND_START);
                o->Velocity = 3.f;
            }
        }
    }
    else if (o->AI == PET_STAND || o->AI == PET_STAND_START)
    {
        SetAI(PET_FLYING);
    }

    AdvanceMotion(FPS_ANIMATION_FACTOR);
    if (rand_fps_check(100 * 60))
    {
        PlayBuffer(SOUND_DSPIRIT_SHOUT, o);
    }

    AdvancePresentation(true, forceRender);
}

void CSPetDarkSpirit::CalcPetInformation(const PET_INFO &Petinfo)
{
}

void CSPetDarkSpirit::Eff_LevelUp(void)
{
    OBJECT *o = &m_PetCharacter.Object;

    vec3_t Angle = {0.f, 0.f, 0.f};
    vec3_t Position = {o->Position[0], o->Position[1], o->Position[2]};

    for (int i = 0; i < 5; ++i)
    {
        CreateJoint(BITMAP_FLARE, Position, Position, Angle, 0, o, 40, 2);
    }
}

void CSPetDarkSpirit::Eff_LevelDown(void)
{
    OBJECT *o = &m_PetCharacter.Object;

    vec3_t Position = {o->Position[0], o->Position[1], o->Position[2]};

    for (int i = 0; i < 15; ++i)
    {
        CreateJoint(BITMAP_FLARE, Position, o->Position, o->Angle, 0, o, 40, 2);
    }
}

void CSPetDarkSpirit::AttackEffect(CHARACTER *c, OBJECT *o)
{
    const float frames = sessionKeeper_.FrameAnimationFactor();
    BMD *b = &Models[o->Type];
    vec3_t p, Pos, Light;

    switch (o->AI)
    {
    case PET_FLY:
    case PET_FLYING:
        if (!g_isCharacterBuff(&m_PetOwner->Object, eBuff_Cloaking))
        {
            Vector(0.3f, 0.4f, 0.7f, Light);
            Vector(0.f, 0.f, 0.f, p);
            constexpr int lastIdleSparkBone = 65; // Authored Dark Spirit sockets.
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames))
            {
                AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                         b->PoseAssetIdentity());
                pose.SampleBonePosition(*b, *o, Random.RangeInt(0, lastIdleSparkBone), p, WorldTime,
                                        birth.FrameFraction(), Pos);
                CreateParticle(BITMAP_SPARK + 1, Pos, o->Angle, Light, 5, 0.8f);
            }
        }
        break;
    case PET_ATTACK:
        if (c->AttackTime >= 0 && c->AttackTime <= 2 && m_PetTarget != NULL)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                for (int i = 0; i < 10; ++i)
                    CreateJoint(BITMAP_LIGHT, position, position, o->Angle, 1, NULL,
                                Random.RangeFloat(0, 39) + 20.f);
            }

            if (c->CheckAttackTime(1))
            {
                vec3_t Angle, Light;

                Vector(45.f, Random.RangeFloat(0, 179) - 90.f, 0.f, Angle);
                Vector(1.f, 0.8f, 0.6f, Light);
                CreateEffect(MODEL_DARKLORD_SKILL, o->Position, Angle, Light, 3);
                c->SetLastAttackEffectTime();
            }
        }
        if (c->AttackTime > 3 && (int)c->AttackTime % 2)
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames))
            {
                if (o->Position[2] > (m_PetOwner->Object.Position[2] + 100.f))
                {
                    Vector(50.f, 0.f, 0.f, p);
                    AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    pose.SampleBonePosition(*b, *o, 6, p, WorldTime, birth.FrameFraction(), Pos);

                    CreateEffect(MODEL_AIR_FORCE, Pos, o->Angle, o->Light, 0, o);
                }
            }
        break;

    case PET_ATTACK_MAGIC:
        if (c->AttackTime < 15)
        {
            if (o->BoneTransform != NULL)
            {
                Vector(1.f, 0.6f, 0.4f, Light);
                Vector(0.f, 0.f, 0.f, p);
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames))
                {
                    ObjectDrawInput draw(o);
                    o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position,
                                          draw.position);
                    AnimationPoseSample pose(draw, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    std::array<vec34_t, MAX_BONES> bones;
                    draw.bones =
                        pose.EvaluateAtTime(*b, *o, WorldTime, birth.FrameFraction(), bones.data());
                    for (int i = Random.RangeInt(0, 1);
                         i < (std::min)(66, static_cast<int>(b->NumBones)); i += 2)
                    {
                        if (!b->Bones[i].Dummy)
                        {
                            b->TransformByObjectBone(Pos, draw, i, p);
                            CreateParticle(BITMAP_LIGHT, Pos, o->Angle, Light, 6, 1.3f);
                        }
                    }
                }
            }
        }
        else if (c->AttackTime >= 15)
        {
            if (c->TargetCharacter != -1)
            {
                CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                OBJECT *to = &tc->Object;

                if (to != NULL)
                {
                    EmitMeshEffects(*b, 1, BITMAP_LIGHT, 1, o->Angle, to);
                }

                SetAI(PET_FLYING);
            }
            c->AttackTime = 0;
            c->LastAttackEffectTime = -1;
        }
        break;

    default:
        break;
    }
}

namespace PetManagerDetail
{

std::wstring SanitizeWideStringFormat(const wchar_t *format)
{
    if (format == nullptr)
    {
        return L"";
    }

    std::wstring sanitized;
    sanitized.reserve(std::wcslen(format) + 1);

    const wchar_t *ptr = format;
    while (*ptr != L'\0')
    {
        if (*ptr != L'%')
        {
            sanitized.push_back(*ptr++);
            continue;
        }

        sanitized.push_back(*ptr++);

        if (*ptr == L'%')
        {
            sanitized.push_back(*ptr++);
            continue;
        }

        bool hasLengthModifier = false;

        while (*ptr && std::wcschr(L"-+ #0", *ptr))
        {
            sanitized.push_back(*ptr++);
        }

        while (*ptr && std::iswdigit(*ptr))
        {
            sanitized.push_back(*ptr++);
        }

        if (*ptr == L'*')
        {
            sanitized.push_back(*ptr++);
        }

        if (*ptr == L'.')
        {
            sanitized.push_back(*ptr++);
            while (*ptr && std::iswdigit(*ptr))
            {
                sanitized.push_back(*ptr++);
            }
            if (*ptr == L'*')
            {
                sanitized.push_back(*ptr++);
            }
        }

        if (*ptr && std::wcschr(L"hljztL", *ptr))
        {
            hasLengthModifier = true;
            wchar_t lengthChar = *ptr;
            sanitized.push_back(lengthChar);
            ++ptr;

            if ((lengthChar == L'h' && *ptr == L'h') || (lengthChar == L'l' && *ptr == L'l'))
            {
                sanitized.push_back(*ptr);
                ++ptr;
            }
        }

        if ((*ptr == L's' || *ptr == L'S') && !hasLengthModifier)
        {
            sanitized.push_back(L'l');
        }

        if (*ptr != L'\0')
        {
            sanitized.push_back(*ptr++);
        }
    }

    return sanitized;
}

std::uint32_t ComposeItemIndex(int sx, int sy)
{
    const auto clampedX = std::clamp(sx, 0, 0xFFFF);
    const auto clampedY = std::clamp(sy, 0, 0xFFFF);
    return static_cast<std::uint32_t>((static_cast<std::uint32_t>(clampedY) << 16) |
                                      static_cast<std::uint32_t>(clampedX));
}

} // namespace PetManagerDetail

CSPetSystem *SessionGameplayUnit::ResolvePetSystem(CHARACTER *character)
{
    return character ? sessionKeeper_.Visual()->FindPetSystem(*character) : nullptr;
}

bool SessionGameplayUnit::IsVirtualKeyPressed(int virtualKey)
{
    return IsKeyDown(virtualKey);
}

void SessionGameplayUnit::ClearRightMouseInputState()
{
    MouseRButtonPop = false;
    MouseRButtonPush = false;
    MouseRButton = false;
    MouseRButtonPress = 0;
}

void SessionGameplayUnit::InitPetManager(void)
{
    g_tabBar = 0;
    gs_PetInfo.m_dwPetType = PET_TYPE_NONE;
}

void SessionGameplayUnit::MovePet(CHARACTER *c)
{
    if (auto *petSystem = ResolvePetSystem(c))
    {
        petSystem->MovePet();
    }
}

bool SessionGameplayUnit::SelectPetCommand(void)
{
    if (gCharacterManager.GetBaseClass(Hero->Class) != CLASS_DARK_LORD)
    {
        return false;
    }

    if (!IsVirtualKeyPressed(PetManagerDetail::kShiftKeyCode))
    {
        return false;
    }

    for (int commandOffset = 0; commandOffset < PetManagerDetail::kPetCommandCount; ++commandOffset)
    {
        const int commandVirtualKey = '1' + commandOffset;
        if (IsVirtualKeyPressed(commandVirtualKey))
        {
            Hero->CurrentSkill = AT_PET_COMMAND_DEFAULT + commandOffset;
            return true;
        }
    }

    return false;
}

void SessionGameplayUnit::SetPetCommand(CHARACTER *c, int Key, std::uint8_t Cmd)
{
    if (c && c->PetCommands.present)
    {
        auto &state = c->PetCommands;
        state.commandTarget = Key;
        const int index = CharactersClient.FindIndexByKey(Key);
        state.commandTargetSource = index < 0 ? nullptr : CharactersClient[index].SocketSource;
        state.command = Cmd;
        state.commandRevision = ++state.sequence;
    }
}

void SessionGameplayUnit::SetAttack(CHARACTER *c, int Key, int attackType)
{
    if (c && c->PetCommands.present)
    {
        auto &state = c->PetCommands;
        state.attackTarget = Key;
        const int index = CharactersClient.FindIndexByKey(Key);
        state.attackTargetSource = index < 0 ? nullptr : CharactersClient[index].SocketSource;
        state.attackType = attackType;
        state.attackRevision = ++state.sequence;
    }
}

void SessionGameplayUnit::DeletePet(CHARACTER *character)
{
    if (!character)
        return;
    if (character->PetCommands.present)
    {
        character->PetCommands.present = false;
        ++character->PetCommands.generation;
    }
    sessionKeeper_.Visual()->AdmitCharacterPet(*character);
}

void SessionGameplayUnit::InitItemBackup(void)
{
    g_renderItemInfoBackup = ITEM{};
    gs_PetInfo.m_dwPetType = PET_TYPE_NONE;
}

void SessionGameplayUnit::SetPetInfo(std::uint8_t InvType, std::uint8_t InvPos, PET_INFO *pPetinfo)
{
    CalcPetInfo(pPetinfo);

    if ((InvType == 0) || (InvType == 254) || (InvType == 255))
    {
        if ((InvPos == EQUIPMENT_HELPER) || (InvPos == EQUIPMENT_WEAPON_LEFT))
        {
            PET_INFO *pHeroPetInfo = Hero->GetEquipedPetInfo(pPetinfo->m_dwPetType);
            std::memcpy(pHeroPetInfo, pPetinfo, sizeof(PET_INFO));

            if (pPetinfo->m_dwPetType == PET_TYPE_DARK_SPIRIT)
            {
                if (auto *heroPetSystem = ResolvePetSystem(Hero))
                {
                    heroPetSystem->SetPetInfo(pHeroPetInfo);

                    if (InvType == 254)
                    {
                        heroPetSystem->Eff_LevelUp();
                    }
                    else if (InvType == 255)
                    {
                        heroPetSystem->Eff_LevelDown();
                    }
                }
            }
            else if (pPetinfo->m_dwPetType == PET_TYPE_DARK_HORSE)
            {
                if (InvType == 254 || InvType == 255)
                {
                    Hero->Object.ExtState = InvType - 253;
                }

                SetPetItemConvert(&CharacterMachine->Equipment[EQUIPMENT_HELPER], pHeroPetInfo);
            }

            CharacterMachine->CalculateAll();

            //return;
        }
    }

    std::memcpy(&gs_PetInfo, pPetinfo, sizeof(PET_INFO));
}

PET_INFO *SessionGameplayUnit::GetPetInfo(ITEM *pItem) const
{
    if (pItem == &CharacterMachine->Equipment[EQUIPMENT_HELPER])
    {
        return Hero->GetEquipedPetInfo(PET_TYPE_DARK_HORSE);
    }
    else if (pItem == &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT])
    {
        return Hero->GetEquipedPetInfo(PET_TYPE_DARK_SPIRIT);
    }
    return &gs_PetInfo;
}

void SessionGameplayUnit::CalcPetInfo(PET_INFO *pPetInfo)
{
    int Charisma = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;
    int Strength = CharacterAttribute->Strength + CharacterAttribute->AddStrength;

    int Level = pPetInfo->m_wLevel + 1;

    switch (pPetInfo->m_dwPetType)
    {
    case PET_TYPE_DARK_SPIRIT:
        pPetInfo->m_dwExp2 = ((10 + Level) * Level * Level * Level * 100);
        pPetInfo->m_wDamageMin = (180 + (pPetInfo->m_wLevel * 15) + (Charisma / 8));
        pPetInfo->m_wDamageMax = (200 + (pPetInfo->m_wLevel * 15) + (Charisma / 4));
        pPetInfo->m_wAttackSpeed = (20 + (pPetInfo->m_wLevel * 4 / 5) + (Charisma / 50));
        pPetInfo->m_wAttackSuccess = (1000 + pPetInfo->m_wLevel) + (pPetInfo->m_wLevel * 15);
        break;

    case PET_TYPE_DARK_HORSE:
        pPetInfo->m_dwExp2 = ((10 + Level) * Level * Level * Level * 100);
        pPetInfo->m_wDamageMin = (Strength / 10) + (Charisma / 10) + (pPetInfo->m_wLevel * 5);
        pPetInfo->m_wDamageMax = pPetInfo->m_wDamageMin + (pPetInfo->m_wDamageMin / 2);
        pPetInfo->m_wAttackSpeed = (20 + (pPetInfo->m_wLevel * 4 / 5) + (Charisma / 50));
        pPetInfo->m_wAttackSuccess = (1000 + pPetInfo->m_wLevel) + (pPetInfo->m_wLevel * 15);
        break;
    }
}

std::uint8_t SessionGameplayUnit::GetPetDefenseBonus(const PET_INFO &pet) const
{
    return static_cast<std::uint8_t>((5 + CharacterAttribute->Dexterity / 20 + pet.m_wLevel * 2) &
                                     0xFF);
}

void SessionGameplayUnit::SetPetItemConvert(ITEM *item, PET_INFO *pet) const
{
    if (item->Type != ITEM_DARK_HORSE_ITEM)
        return;
    int index = -1;
    for (int i = 0; i < item->SpecialNum; ++i)
    {
        if (item->Special[i] == AT_SET_OPTION_IMPROVE_DEFENCE)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
        index = item->SpecialNum++;
    item->Special[index] = AT_SET_OPTION_IMPROVE_DEFENCE;
    item->SpecialValue[index] = GetPetDefenseBonus(*pet);
}

std::uint32_t SessionGameplayUnit::GetPetItemValue(PET_INFO *pPetInfo) const
{
    std::uint32_t gold = 0;

    if (pPetInfo->m_dwPetType == PET_TYPE_NONE)
    {
        return gold;
    }

    switch (pPetInfo->m_dwPetType)
    {
    case PET_TYPE_DARK_HORSE:
        gold = static_cast<std::uint32_t>(pPetInfo->m_wLevel) * 2000000u;
        break;

    case PET_TYPE_DARK_SPIRIT:
        gold = static_cast<std::uint32_t>(pPetInfo->m_wLevel) * 1000000u;
        break;
    }

    return gold;
}

bool SessionGameplayUnit::SendPetCommand(CHARACTER *c, int Index)
{
    auto *petSystem = ResolvePetSystem(c);
    if (petSystem == nullptr)
    {
        return false;
    }

    if (!(MouseRButtonPush || MouseRButton))
    {
        return false;
    }

    if (Index < AT_PET_COMMAND_DEFAULT || Index >= AT_PET_COMMAND_END)
    {
        return false;
    }

    const auto petCommand = static_cast<PetCommandMode>(Index - AT_PET_COMMAND_DEFAULT);
    if (Index == AT_PET_COMMAND_TARGET)
    {
        if (CheckAttack() && SelectedCharacter != -1)
        {
            CHARACTER *targetCharacter = &CharactersClient[SelectedCharacter];
            if (targetCharacter->Object.Kind == KIND_MONSTER ||
                targetCharacter->Object.Kind == KIND_PLAYER)
            {
                SocketClient->ToGameServer()->SendPetCommandRequest(
                    static_cast<PetType>(petSystem->GetPetType()), petCommand,
                    targetCharacter->Key);
            }
        }
    }
    else
    {
        SocketClient->ToGameServer()->SendPetCommandRequest(
            static_cast<PetType>(petSystem->GetPetType()), petCommand,
            PetManagerDetail::kInvalidTargetKey);
    }

    ClearRightMouseInputState();
    return true;
}

void SessionGameplayUnit::CreatePetDarkSpirit(CHARACTER *character)
{
    DeletePet(character);
    if (gMapManager.InChaosCastle())
        return;
    auto &state = character->PetCommands;
    state.present = true;
    ++state.generation;
    state.commandRevision = state.attackRevision = 0;
    state.level = character->Weapon[1].Level;
    if (character == Hero)
    {
        if (const auto *info = GetPetInfo(&CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT]))
            state.level = info->m_wLevel;
    }
    sessionKeeper_.Visual()->AdmitCharacterPet(*character);
}

void SessionGameplayUnit::CreatePetDarkSpirit_Now(CHARACTER *character)
{
    if (character->Weapon[1].Type == MODEL_DARK_RAVEN_ITEM)
        CreatePetDarkSpirit(character);
}
CHARACTER *SessionGameplayUnit::CharacterForMountOwner(OBJECT *owner)
{
    // Only mount admission/deletion needs this reverse lookup.
    for (int index = 0; index < CharactersClient.Size(); ++index)
        if (CharactersClient.IsValidIndex(index) && &CharactersClient[index].Object == owner)
            return &CharactersClient[index];
    return nullptr;
}

void SessionGameplayUnit::DeleteMount(OBJECT *Owner)
{
    if (auto *character = CharacterForMountOwner(Owner))
    {
        if (character->MountState.type != -1)
        {
            character->MountState.type = -1;
            ++character->MountState.generation;
        }
        return;
    }
    for (int i = 0; i < MAX_MOUNTS; i++)
    {
        OBJECT *o = &Mounts[i];
        if (o->Live)
        {
            if (o->Owner == Owner)
                o->Live = false;
        }
    }
}

bool SessionGameplayUnit::CreateMountSub(int Type, vec3_t Position, OBJECT *Owner, OBJECT *o,
                                         int SubType, int LinkBone)
{
    if (!TheMapProcess().CharacterPolicy().mounts)
    {
        return false;
    }

    if (!o->Live)
    {
        o->Type = Type;
        o->Live = true;
        o->MotionTrace.Reset();
        o->EffectMotionFrames = 0.f;
        o->AmbientNoiseFrames = 0.f;
        o->Visible = false;
        o->LightEnable = true;
        o->ContrastEnable = false;
        o->AlphaEnable = false;
        o->EnableBoneMatrix = false;
        o->Owner = Owner;
        o->SubType = SubType;
        o->LinkBone = LinkBone;
        o->HiddenMesh = -1;
        o->BlendMesh = -1;
        o->BlendMeshLight = 1.f;
        o->Scale = 0.7f;
        o->LifeTime = 30;
        o->Alpha = 0.f;
        o->AlphaTarget = 1.f;
        VectorCopy(Position, o->Position);
        VectorCopy(Owner->Angle, o->Angle);
        Vector(3.f, 3.f, 3.f, o->Light);

        //int AnimationFrame = Models[o->Type].NumAnimationKeys[Models[o->Type].CurrentAction];
        o->PriorAction = o->CurrentAction = 0;
        o->HorseSkillTicks = 0.f;
        o->PriorAnimationFrame = 0.f;
        o->AnimationFrame = 0.f;
        o->Velocity = 0.5f;
        switch (o->Type)
        {
        case MODEL_FENRIR_BLACK:
        case MODEL_FENRIR_BLUE:
        case MODEL_FENRIR_RED:
        case MODEL_FENRIR_GOLD:
            o->Scale = 0.9f;
            break;
        case MODEL_DARK_HORSE:
            o->Scale = 1.f;
            break;
        case MODEL_PEGASUS:
        case MODEL_UNICON:
            o->Scale = 0.9f;
            break;
        case MODEL_HELPER:
            o->BlendMesh = 1;
            Vector(Owner->Position[0] + (float)(WorldRandom() % 512 - 256),
                   Owner->Position[1] + (float)(WorldRandom() % 512 - 256),
                   Owner->Position[2] + (float)(WorldRandom() % 128 + 128), o->Position);
            break;
        case MODEL_IMP:
            Vector(Owner->Position[0] + (float)(WorldRandom() % 128 - 64),
                   Owner->Position[1] + (float)(WorldRandom() % 128 - 64), Owner->Position[2],
                   o->Position);
            o->Position[2] =
                RequestTerrainHeight(o->Position[0], o->Position[1]) + (float)(WorldRandom() % 100);
            break;
        }

        return FALSE;
    }

    return TRUE;
}

void SessionGameplayUnit::CreateMount(int Type, vec3_t Position, OBJECT *Owner, int SubType,
                                      int LinkBone)
{
    if (!TheMapProcess().CharacterPolicy().mounts)
        return;

    if (Owner->Type != MODEL_PLAYER && Type != MODEL_HELPER)
        return;

    if (auto *character = CharacterForMountOwner(Owner))
    {
        auto &state = character->MountState;
        state.type = Type;
        state.subType = SubType;
        state.linkBone = LinkBone;
        VectorCopy(Position, state.position);
        ++state.generation;
        return;
    }
    for (int i = 0; i < MAX_MOUNTS; i++)
    {
        OBJECT *o = &Mounts[i];
        if (CreateMountSub(Type, Position, Owner, o, SubType, LinkBone) == FALSE)
        {
            // False means, it has been successful ...
            return;
        }
    }
}

void SessionGameplayUnit::PrepareMountPose(OBJECT &object, AnimationPoseSample *prepared)
{
    auto &model = Models[object.Type];
    model.BodyScale = object.Scale;
    model.BodyHeight = 0.f;
    model.CurrentAction = object.CurrentAction;
    VectorCopy(object.Position, model.BodyOrigin);
    auto *bones = object.BoneTransform ? object.BoneTransform : BoneTransform;
    AnimationPoseSample sample(&object, model.BoneHead, 0.f, true, model.PoseAssetIdentity());
    if (!prepared || *prepared != sample)
    {
        sample.Evaluate(model, bones);
        if (prepared)
            *prepared = sample;
    }
}

bool SessionGameplayUnit::SynchronizeMount(OBJECT &object)
{
    auto *o = &object;
    if (!o->Live || !o->Owner)
        return false;
    bool moving = false;
    switch (o->Type)
    {
    case MODEL_FENRIR_BLACK:
    case MODEL_FENRIR_BLUE:
    case MODEL_FENRIR_RED:
    case MODEL_FENRIR_GOLD:
        VectorCopy(o->Owner->HeadAngle, o->HeadAngle);
        VectorCopy(o->Owner->Position, o->Position);
        VectorCopy(o->Owner->Angle, o->Angle);

        if ((o->Owner->CurrentAction >= PLAYER_FENRIR_ATTACK &&
             o->Owner->CurrentAction <= PLAYER_FENRIR_ATTACK_BOW) ||
            IsAliceRideAction_Fenrir(o->Owner->CurrentAction) == true ||
            o->Owner->CurrentAction == PLAYER_RAGE_FENRIR_ATTACK_RIGHT)
        {
            SetAction(o, FENRIR_ATTACK);
            o->Velocity = 0.4f;
        }
        else if (o->Owner->CurrentAction >= PLAYER_FENRIR_SKILL &&
                 o->Owner->CurrentAction <= PLAYER_FENRIR_SKILL_ONE_LEFT)
        {
            SetAction(o, FENRIR_ATTACK_SKILL);
            o->Velocity = 0.4f;
        }
        else if (o->Owner->CurrentAction >= PLAYER_FENRIR_DAMAGE &&
                 o->Owner->CurrentAction <= PLAYER_FENRIR_DAMAGE_ONE_LEFT)
        {
            SetAction(o, FENRIR_DAMAGE);
            o->Velocity = 0.4f;
        }
        else if (o->Owner->CurrentAction >= PLAYER_FENRIR_STAND &&
                 o->Owner->CurrentAction <= PLAYER_FENRIR_STAND_ONE_LEFT)
        {
            SetAction(o, FENRIR_STAND);
            o->Velocity = 0.4f;
        }
        else if (o->Owner->CurrentAction == PLAYER_DIE1)
        {
            SetAction(o, FENRIR_STAND);
            o->Velocity = 0.4f;
        }
        else if (o->Owner->CurrentAction >= PLAYER_RAGE_FENRIR_DAMAGE &&
                 o->Owner->CurrentAction <= PLAYER_RAGE_FENRIR_DAMAGE_ONE_LEFT)
        {
            SetAction(o, FENRIR_DAMAGE);
            o->Velocity = 0.4f;
        }
        else if (o->Owner->CurrentAction >= PLAYER_RAGE_FENRIR &&
                 o->Owner->CurrentAction <= PLAYER_RAGE_FENRIR_ONE_LEFT)
        {
            SetAction(o, FENRIR_ATTACK_SKILL);
            o->Velocity = 0.4f;
        }
        else if (o->Owner->CurrentAction >= PLAYER_RAGE_FENRIR_STAND &&
                 o->Owner->CurrentAction <= PLAYER_RAGE_FENRIR_STAND_ONE_LEFT)
        {
            SetAction(o, FENRIR_STAND);
            o->Velocity = 0.4f;
        }
        else if (o->Owner->CurrentAction >= PLAYER_SKILL_THRUST &&
                 o->Owner->CurrentAction <= PLAYER_SKILL_HP_UP_OURFORCES)
        {
            SetAction(o, FENRIR_STAND);
            o->Velocity = 0.4f;
        }
        else
        {
            moving = true;
            if (o->Owner->CurrentAction >= PLAYER_FENRIR_WALK &&
                o->Owner->CurrentAction <= PLAYER_FENRIR_WALK_ONE_LEFT)
            {
                SetAction(o, FENRIR_WALK);
                o->Velocity = 1.0f;
            }
            else if (o->Owner->CurrentAction >= PLAYER_FENRIR_RUN &&
                     o->Owner->CurrentAction <= PLAYER_FENRIR_RUN_ONE_LEFT_ELF)
            {
                SetAction(o, FENRIR_RUN);
                o->Velocity = 0.6f;
            }
            else if (o->Owner->CurrentAction >= PLAYER_RAGE_FENRIR_RUN &&
                     o->Owner->CurrentAction <= PLAYER_RAGE_FENRIR_RUN_ONE_LEFT)
            {
                SetAction(o, FENRIR_RUN);
                o->Velocity = 0.6f;
            }
            else if (o->Owner->CurrentAction >= PLAYER_RAGE_FENRIR_WALK &&
                     o->Owner->CurrentAction <= PLAYER_RAGE_FENRIR_WALK_TWO_SWORD)
            {
                SetAction(o, FENRIR_WALK);
                o->Velocity = 1.0f;
            }
        }
        break;
    case MODEL_DARK_HORSE:
        Models[o->Type].BoneHead = 7;
        // Take riders position and angle:
        VectorCopy(o->Owner->HeadAngle, o->HeadAngle);
        VectorCopy(o->Owner->Position, o->Position);
        VectorCopy(o->Owner->Angle, o->Angle);

        if (o->Owner->CurrentAction == PLAYER_ATTACK_DARKHORSE)
        {
            SetAction(o, 3);
            o->Velocity = 0.34f;
        }
        else if (o->Owner->CurrentAction == PLAYER_RUN_RIDE_HORSE)
        {

            SetAction(o, 1);
            o->Velocity = 0.34f;
        }
        else if (o->Owner->CurrentAction >= PLAYER_ATTACK_RIDE_STRIKE &&
                 o->Owner->CurrentAction <= PLAYER_ATTACK_RIDE_ATTACK_MAGIC)
        {
            SetAction(o, 2);
            o->Velocity = 0.34f;
        }
        else if (o->Owner->CurrentAction == PLAYER_IDLE1_DARKHORSE)
        {
            SetAction(o, 5);
            o->Velocity = 1.0f;
        }
        else if (o->Owner->CurrentAction == PLAYER_IDLE2_DARKHORSE)
        {
            SetAction(o, 6);
            o->Velocity = 1.0f;
        }
        else
        {
            SetAction(o, 0);
            o->Velocity = 0.3f;
        }

        break;
    case MODEL_PEGASUS:
    case MODEL_UNICON:
        VectorCopy(o->Owner->Position, o->Position);

        if (o->Type == MODEL_PEGASUS)
        {
            if (TheMapProcess().MountsUseFlyingActions())
                o->Position[2] -= 10.f;
            else if (gMapManager.ContextMap() != -1)
                o->Position[2] -= 30.f;
        }
        VectorCopy(o->Owner->Angle, o->Angle);
        if (o->Owner->CurrentAction >= PLAYER_WALK_MALE &&
                o->Owner->CurrentAction <= PLAYER_RUN_RIDE_WEAPON ||
            o->Owner->CurrentAction == PLAYER_FLY_RIDE ||
            o->Owner->CurrentAction == PLAYER_FLY_RIDE_WEAPON ||
            o->Owner->CurrentAction == PLAYER_RAGE_UNI_RUN ||
            o->Owner->CurrentAction == PLAYER_RAGE_UNI_RUN_ONE_RIGHT)
        {
            moving = true;
            //  페가수스.
            if (o->Type == MODEL_PEGASUS)
            {
                if (TheMapProcess().MountsUseFlyingActions())
                    SetAction(o, 3);
                else
                    SetAction(o, 2);
            }
            else
            {
                SetAction(o, 2);
            }
        }
        else if (o->Owner->CurrentAction == PLAYER_SKILL_RIDER ||
                 o->Owner->CurrentAction == PLAYER_SKILL_RIDER_FLY)
        {
            if (TheMapProcess().MountsUseFlyingActions())
                SetAction(o, 7);
            else
                SetAction(o, 6);
        }
        else if ((o->Owner->CurrentAction >= PLAYER_ATTACK_FIST &&
                  o->Owner->CurrentAction <= PLAYER_ATTACK_RIDE_CROSSBOW) ||
                 IsAliceRideAction_UniDino(o->Owner->CurrentAction) == true)
        {
            if (o->Type == MODEL_PEGASUS)
            {
                if (TheMapProcess().MountsUseFlyingActions())
                    SetAction(o, 5);
                else
                    SetAction(o, 4);
            }
            else
            {
                SetAction(o, 3);
            }
        }
        else
        {
            if (o->Type == MODEL_PEGASUS)
            {
                if (TheMapProcess().MountsUseFlyingActions())
                    SetAction(o, 1);
                else
                    SetAction(o, 0);
            }
            else
            {
                SetAction(o, 0);
            }
        }
        o->Velocity = 0.34f;
        o->Live = o->Owner->Live;
        break;
    }
    return moving;
}

void SessionGameplayUnit::AdvanceFlyingMount(OBJECT *o, const vec3_t target, float flyRange,
                                             float frames)
{
    constexpr float SteeringInterval = 1.f / 16.f, TurnRate = 20.f;
    const float totalFrames = frames;
    vec3_t offset;
    VectorSubtract(target, o->Owner->Position, offset);
    while (frames > 0.f)
    {
        vec3_t ownerPosition, sampledTarget, range;
        o->Owner->MotionTrace.Sample(WorldTime, (totalFrames - frames) / totalFrames,
                                     o->Owner->Position, ownerPosition);
        VectorAdd(ownerPosition, offset, sampledTarget);
        VectorSubtract(sampledTarget, o->Position, range);
        const float distance = range[0] * range[0] + range[1] * range[1];
        if (o->AmbientNoiseFrames <= 0.f)
        {
            o->AmbientNoiseFrames = 1.f;
            o->AmbientVerticalNoise = float(WorldRandom() % 16 - 8);
            o->AmbientSpeedNoise = distance;
        }
        if (o->EffectMotionFrames <= 0.f)
        {
            o->EffectMotionFrames = (std::min)(SteeringInterval, o->AmbientNoiseFrames);
            const float heading = distance >= flyRange * flyRange
                                      ? CreateAngle2D(o->Position, sampledTarget)
                                      : o->Angle[2];
            o->EffectMotionAngleRate[2] = std::clamp(
                FarAngle(o->Angle[2], heading, false) / o->EffectMotionFrames, -TurnRate, TurnRate);
        }
        const float step = (std::min)(frames, o->EffectMotionFrames);
        o->MotionTrace.TurnYaw(step, o->Angle[2], o->Angle[2] + o->EffectMotionAngleRate[2] * step,
                               0.f);
        MoveRotatingPosition(o->Position, o->Angle, o->Direction, 2, o->EffectMotionAngleRate[2],
                             step);
        o->Position[2] += o->AmbientVerticalNoise * step;
        o->EffectMotionFrames -= step;
        o->AmbientNoiseFrames -= step;
        frames -= step;
        o->MotionTrace.Advance(step, o->Position);
        if (o->AmbientNoiseFrames > 0.f)
            continue;
        if (WorldRandom() % 32 == 0)
        {
            float speed;
            if (o->AmbientSpeedNoise >= flyRange * flyRange)
                speed = -float(WorldRandom() % 64 + 128) * 0.1f;
            else
            {
                speed = -float(WorldRandom() % 64 + 16) * 0.1f;
                o->Angle[2] = float(WorldRandom() % 360);
            }
            o->Direction[0] = 0.f;
            o->Direction[1] = speed;
            o->Direction[2] = float(WorldRandom() % 64 - 32) * 0.1f;
        }
        o->Owner->MotionTrace.Sample(WorldTime, (totalFrames - frames) / totalFrames,
                                     o->Owner->Position, ownerPosition);
        if (o->Position[2] < ownerPosition[2] + 100.f)
            o->Direction[2] += 1.5f;
        if (o->Position[2] > ownerPosition[2] + 200.f)
            o->Direction[2] -= 1.5f;
    }
}

bool SessionGameplayUnit::MoveMount(OBJECT *o, bool bForceRender, CHARACTER *characterOwner,
                                    AnimationPoseSample *prepared)
{
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    const float previousFrame = o->AnimationFrame;
    bool moving = false;
    if (o->Live)
        o->MotionTrace.Begin(WorldTime, FPS_ANIMATION_FACTOR, o->Position);
    if (o->Live)
    {
        if (SceneFlag == MAIN_SCENE)
        {
            if (TheMapProcess().CharacterPolicy().nonPlayerMountOwners)
                ;
            else if (!o->Owner->Live || o->Owner->Kind != KIND_PLAYER)
            {
                o->Live = false;
                return TRUE;
            }
        }

        if (!bForceRender)
        {
            if (SceneFlag == CHARACTER_SCENE)
                o->Scale = 1.2f;
            else if (o->Type != MODEL_FENRIR_BLACK && o->Type != MODEL_FENRIR_BLUE &&
                     o->Type != MODEL_FENRIR_RED && o->Type != MODEL_FENRIR_GOLD)
                o->Scale = 1.f;
        }
        Alpha(o, FPS_ANIMATION_FACTOR);

        float FlyRange = 0.0f;
        vec3_t Light, Position;
        vec3_t TargetPosition;
        BMD *b = &Models[o->Type];

        auto *mountBones = o->BoneTransform ? o->BoneTransform : BoneTransform;
        moving = SynchronizeMount(*o);
        o->Visible =
            bForceRender || TestFrustrum2D(o->Position[0] * 0.01f, o->Position[1] * 0.01f, -20.f);
        VectorCopy(o->Owner->Position, TargetPosition);
        switch (o->Type)
        {
        case MODEL_FENRIR_BLACK:
        case MODEL_FENRIR_BLUE:
        case MODEL_FENRIR_RED:
        case MODEL_FENRIR_GOLD:
            if ((TerrainWall[TERRAIN_INDEX_REPEAT((int)(o->Owner->Position[0] / TERRAIN_SCALE),
                                                  (int)(o->Owner->Position[1] / TERRAIN_SCALE))] &
                 TW_SAFEZONE) == TW_SAFEZONE &&
                bForceRender == FALSE)
            {
                o->Alpha = 0.f;
                break;
            }

            if (o->Owner->Teleport == TELEPORT_BEGIN || o->Owner->Teleport == TELEPORT)
            {
                o->Alpha -= (0.1f) * FPS_ANIMATION_FACTOR;
                if (o->Alpha < 0)
                    o->Alpha = 0.f;
            }
            else
            {
                o->Alpha = 1.f;
            }

            if (o->Visible && moving)
            {
                Vector(1.f, 1.f, 1.f, Light);
                if (TheMapProcess().CharacterPolicy().skyTerrain)
                {
                    PrepareMountPose(*o, prepared);
                    bool bWave = false;
                    vec3_t p = {120.f, 0.f, 32.f};

                    if (o->AnimationFrame > 1.f && o->AnimationFrame < 1.2f)
                    {
                        b->TransformPosition(mountBones[22], p, Position); // R Hand
                        Position[0] += 40.f;
                        bWave = true;
                    }
                    else if (o->AnimationFrame > 4.8f && o->AnimationFrame <= 5.0f)
                    {
                        b->TransformPosition(mountBones[44], p, Position); // R Foot
                        Position[0] += 40.f;
                        Position[2] += 700.f;
                        bWave = true;
                    }

                    if (bWave && rand_fps_check(1))
                    {
                        Vector(Position[0], Position[1], Position[2], p);
                        CreateEffect(BITMAP_SHOCK_WAVE, p, o->Angle, Light, 1);

                        for (int i = 0; i < WorldRandom() % 5 + 5; ++i)
                        {
                            Vector(Position[0] + (float)(WorldRandom() % 50 - 25),
                                   Position[1] + (float)(WorldRandom() % 50 - 25),
                                   Position[2] + (float)(WorldRandom() % 16 - 8) - 10, p);
                            CreateParticle(BITMAP_SMOKE, p, o->Angle, Light);
                        }
                    }
                }
            }
            break;
        case MODEL_DARK_HORSE:
            if ((TerrainWall[TERRAIN_INDEX_REPEAT((int)(o->Owner->Position[0] / TERRAIN_SCALE),
                                                  (int)(o->Owner->Position[1] / TERRAIN_SCALE))] &
                 TW_SAFEZONE) == TW_SAFEZONE &&
                bForceRender == FALSE)
            {
                o->Alpha = 0.f;
                break;
            }

            PrepareMountPose(*o, prepared);
            if (o->Visible && o->Owner->CurrentAction == PLAYER_RUN_RIDE_HORSE)
            {
                Vector(1.f, 1.f, 1.f, Light);
                if (TheMapProcess().TerrainIsAirborne())
                {
                    bool bWave = false;
                    vec3_t p = {120.f, 0.f, WorldRandom() % 64 - 32.f};

                    if (o->AnimationFrame > 1.f && o->AnimationFrame < 1.2f)
                    {
                        b->TransformPosition(mountBones[19], p, Position);
                        bWave = true;
                    }
                    else if (o->AnimationFrame > 1.1f && o->AnimationFrame < 1.4f)
                    {
                        b->TransformPosition(mountBones[25], p, Position);
                        bWave = true;
                    }
                    else if (o->AnimationFrame > 1.3f && o->AnimationFrame < 1.6f)
                    {
                        b->TransformPosition(mountBones[32], p, Position);
                        bWave = true;
                    }
                    else if (o->AnimationFrame > 1.5f && o->AnimationFrame <= 1.8f)
                    {
                        b->TransformPosition(mountBones[38], p, Position);
                        bWave = true;
                    }

                    if (bWave)
                    {
                        Vector(Position[0] + (float)(WorldRandom() % 16 - 8),
                               Position[1] + (float)(WorldRandom() % 16 - 8), Position[2], p);
                        CreateEffect(BITMAP_SHOCK_WAVE, p, o->Angle, Light, 1);

                        for (int i = 0; i < WorldRandom() % 5 + 5; ++i)
                        {
                            Vector(Position[0] + (float)(WorldRandom() % 50 - 25),
                                   Position[1] + (float)(WorldRandom() % 50 - 25),
                                   Position[2] + (float)(WorldRandom() % 16 - 8) - 10, p);
                            CreateParticle(BITMAP_SMOKE, p, o->Angle, Light);
                        }
                    }
                }
            }

            if (o->Visible && o->CurrentAction == 3)
            {
                vec3_t Pos1, Pos2, p, p2;

                Vector(60.f, 0.f, 0.f, Pos1);
                Vector(0.f, 0.f, 0.f, Pos2);
                Vector(1.f, 1.f, 1.f, Light);
                b->TransformPosition(mountBones[9], Pos1, p);
                b->TransformPosition(mountBones[9], Pos2, p2);
                CreateBlur(characterOwner ? characterOwner : Hero, p, p2, Light, 0);
            }

            if (o->CurrentAction == 3)
                AdvanceDarkHorseSkill(o, b, o->Visible);
            else
                o->HorseSkillTicks = 0.f;

            if (o->Visible &&
                (characterOwner ? characterOwner->WorldVisualMountBurst : o->Owner->ExtState == 1))
            {
                vec3_t p;
                vec3_t Angle = {0.f, 0.f, 0.f};
                float Matrix[3][4];

                Vector(0.f, 50, 0.f, p);
                for (int i = 0; i < 4; ++i)
                {
                    Angle[2] += 90.f;
                    AngleMatrix(Angle, Matrix);
                    VectorRotate(p, Matrix, Position);
                    VectorAdd(Position, o->Position, Position);

                    for (int j = 0; j < 3; ++j)
                    {
                        CreateJoint(BITMAP_FLARE, Position, Position, Angle, 0, o, 40, 2);
                    }
                }
            }
            if (!characterOwner)
                o->Owner->ExtState = 0;

            o->Live = o->Owner->Live;
            break;
        case MODEL_PEGASUS:
        case MODEL_UNICON:
            if ((TerrainWall[TERRAIN_INDEX_REPEAT((int)(o->Owner->Position[0] / TERRAIN_SCALE),
                                                  (int)(o->Owner->Position[1] / TERRAIN_SCALE))] &
                 TW_SAFEZONE) == TW_SAFEZONE &&
                bForceRender == FALSE)
            {
                o->Alpha = 0.f;
                break;
            }

            if (o->Owner->Teleport == TELEPORT_BEGIN || o->Owner->Teleport == TELEPORT)
            {
                o->Alpha -= (0.1f) * FPS_ANIMATION_FACTOR;
                if (o->Alpha < 0)
                    o->Alpha = 0.f;
            }
            else
            {
                o->Alpha = 1.f;
            }

            o->Live = o->Owner->Live;
            break;
        case MODEL_BUTTERFLY01:
            FlyRange = 100.f;
            Vector(0.4f, 0.6f, 1.f, Light);

            break;
        case MODEL_HELPER:
        case MODEL_IMP:
            FlyRange = 150.f;
            break;
        }
        b->CurrentAction = o->CurrentAction;

        const ObjectMotionTrace::AnimationPhase startPhase{
            o->AnimationFrame, o->PriorAnimationFrame, o->CurrentAction, o->PriorAction};
        b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction, o->Velocity,
                         o->Position, o->Angle);
        o->MotionTrace.AdvanceAnimation(FPS_ANIMATION_FACTOR, startPhase,
                                        o->Velocity * FPS_ANIMATION_FACTOR);

        if (o->Type == MODEL_HELPER || o->Type == MODEL_IMP)
        {
            AdvanceFlyingMount(o, TargetPosition, FlyRange, FPS_ANIMATION_FACTOR);
        }
    }
    if (o->Live)
    {
        PrepareMountPose(*o, prepared);
        o->Visible =
            bForceRender || TestFrustrum2D(o->Position[0] * 0.01f, o->Position[1] * 0.01f, -20.f);
        sessionKeeper_.Visual()->AdvanceMountEmissions(*o, moving);
        if (o->Visible && o->Alpha >= 0.01f && !g_isCharacterBuff(o->Owner, eBuff_Cloaking))
            sessionKeeper_.Visual()->AdvanceFenrirVisual(*o, previousFrame);
        if (o->Visible && o->Type == MODEL_HELPER)
        {
            vec3_t light;
            const float luminosity = static_cast<float>(WorldRandom() % 30 + 70) * 0.01f;
            Vector(luminosity * 0.5f, luminosity * 0.8f, luminosity * 0.6f, light);
            CreateSprite(BITMAP_LIGHT, o->Position, 1.f, light, o);
        }
    }
    return TRUE;
}
void SessionGameplayUnit::MoveMounts()
{
    for (int i = 0; i < MAX_MOUNTS; i++)
    {
        OBJECT *o = &Mounts[i];
        if (MoveMount(o) == FALSE)
            return;
    }
}

SessionCharacterPopulationStorage::SessionCharacterPopulationStorage(CBoneManager &bones) noexcept
    : boneManager_(bones)
{
}

WorldCharacterVisualState &SessionCharacterPopulationStorage::WorldVisuals(int index) noexcept
{
    return worldVisuals_[index];
}
