#pragma once

#include "data/WorldData.h"
#include "render/Assets.h"
#include "session/SessionRuntime.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/Texture.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::Character
{
inline constexpr std::size_t RmlCharacterFrameStatCount = 5;

struct RmlCharacterFrameRect final
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct RmlCharacterFrameContent final
{
    std::wstring title;
    std::wstring levelLabel;
    std::wstring level;
    std::wstring classLabel;
    std::wstring characterClass;
    std::wstring serverLabel;
    std::wstring server;
    std::wstring experience;
    std::wstring pointLabel;
    std::wstring points;
    std::array<std::wstring, RmlCharacterFrameStatCount> statLabels{};
    std::array<std::wstring, RmlCharacterFrameStatCount> statValues{};
    std::array<std::wstring, RmlCharacterFrameStatCount - 1> details{};
    std::wstring pet;
    std::wstring masterLevel;
    std::array<bool, RmlCharacterFrameStatCount> canIncrease{};
    bool darkLord = false;

    bool operator==(const RmlCharacterFrameContent &) const = default;
};

class RmlCharacterFramePanel final
{
  public:
    static float Width() noexcept;
    static float Height() noexcept;

    explicit RmlCharacterFramePanel(SessionKeeper &keeper);
    ~RmlCharacterFramePanel();

    RmlCharacterFramePanel(const RmlCharacterFramePanel &) = delete;
    RmlCharacterFramePanel &operator=(const RmlCharacterFramePanel &) = delete;

    void Create();
    void Release();
    bool ProcessInput(const SessionInputEvent &event);
    bool PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                         const RmlCharacterFrameContent &content);
    bool Record(LegacyRenderFacade &facade) const;
    RmlCharacterFrameRect ReferenceRect(int viewportWidth, int viewportHeight) const noexcept;

    RmlMuButton &StatButton(std::size_t index) noexcept
    {
        return buttons_[index];
    }
    RmlMuButton &PetButton() noexcept
    {
        return buttons_[RmlCharacterFrameStatCount];
    }
    RmlMuButton &MasterLevelButton() noexcept
    {
        return buttons_[RmlCharacterFrameStatCount + 1];
    }
    RmlMuButton &CloseButton() noexcept
    {
        return buttons_[RmlCharacterFrameStatCount + 2];
    }

  private:
    class Impl;

    std::array<RmlMuButton, RmlCharacterFrameStatCount + 3> buttons_;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Character

class LegacyRenderFacade;
class SessionKeeper;

namespace UI::Modern
{
struct RmlPetFrameRow final
{
    static constexpr std::size_t NameCapacity = 64;

    wchar_t name[NameCapacity]{};
    int maximum = 0;
    int position = 0;
};

struct RmlPetFrameRequest final
{
    static constexpr int RowCapacity = 5;

    std::array<RmlPetFrameRow, RowCapacity> rows{};
    int x = 0;
    int y = 0;
    int rowCount = 0;
    bool minimized = false;
    ButtonVisualState buttonState = ButtonVisualState::Up;
};

struct RmlPetFrameRect final
{
    float x, y, width, height;
};

class RmlPetFrameLayer final
{
  public:
    static float Width() noexcept;
    static float DragHeight() noexcept;
    static float RowHeight() noexcept;
    static RmlPetFrameRect MinimizeRect() noexcept;

    explicit RmlPetFrameLayer(SessionKeeper &keeper);
    ~RmlPetFrameLayer();

    RmlPetFrameLayer(const RmlPetFrameLayer &) = delete;
    RmlPetFrameLayer &operator=(const RmlPetFrameLayer &) = delete;

    void Stage(const RmlPetFrameRequest &request) noexcept;
    bool PrepareOnWorker(int viewportWidth, int viewportHeight);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::Character
{
inline constexpr std::size_t RmlPetInfoTabCount = 2;
inline constexpr std::size_t RmlPetInfoSkillCount = 4;

struct RmlPetInfoRect final
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct RmlPetInfoContent final
{
    std::wstring title;
    std::array<std::wstring, RmlPetInfoTabCount> tabLabels{};
    std::array<std::wstring, 5> labels{};
    std::array<std::wstring, 5> values{};
    std::wstring commandLabel;
    std::wstring leadershipLabel;
    std::wstring leadership;
    std::array<std::wstring, RmlPetInfoSkillCount> skills{};
    std::wstring missingPet;
    std::uint64_t experience = 0;
    std::uint64_t nextExperience = 0;
    int selectedTab = 0;
    bool petPresent = false;

    bool operator==(const RmlPetInfoContent &) const = default;
};

RmlPetInfoRect RmlPetInfoTabRect(std::size_t index) noexcept;
float RmlPetInfoScaleFor(int viewportWidth, int viewportHeight, float maximumScale) noexcept;

class RmlPetInfoPanel final
{
  public:
    static float Width() noexcept;
    static float Height() noexcept;

    explicit RmlPetInfoPanel(SessionKeeper &keeper);
    ~RmlPetInfoPanel();

    RmlPetInfoPanel(const RmlPetInfoPanel &) = delete;
    RmlPetInfoPanel &operator=(const RmlPetInfoPanel &) = delete;

    void Create();
    void Release();
    bool ProcessInput(const SessionInputEvent &event);
    bool PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                         const RmlPetInfoContent &content);
    bool Record(LegacyRenderFacade &facade) const;
    RmlPetInfoRect ReferenceRect(int viewportWidth, int viewportHeight) const noexcept;

    RmlMuButton &TabButton(std::size_t index) noexcept
    {
        return buttons_[index];
    }
    RmlMuButton &CloseButton() noexcept
    {
        return buttons_[RmlPetInfoTabCount];
    }

  private:
    class Impl;

    std::array<RmlMuButton, RmlPetInfoTabCount + 1> buttons_;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Character

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::Command
{
inline constexpr std::size_t RmlCommandWindowButtonCount = 11;

struct RmlCommandWindowRect final
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

RmlCommandWindowRect RmlCommandWindowButtonRect(std::size_t index) noexcept;
float RmlCommandWindowScaleFor(int viewportWidth, int viewportHeight, float maximumScale) noexcept;

struct RmlCommandWindowContent final
{
    std::wstring title;
    std::array<std::wstring, RmlCommandWindowButtonCount> labels{};
    int selected = -1;

    bool operator==(const RmlCommandWindowContent &) const = default;
};

class RmlCommandWindowPanel final
{
  public:
    static float Width() noexcept;
    static float Height() noexcept;

    explicit RmlCommandWindowPanel(SessionKeeper &keeper);
    ~RmlCommandWindowPanel();

    RmlCommandWindowPanel(const RmlCommandWindowPanel &) = delete;
    RmlCommandWindowPanel &operator=(const RmlCommandWindowPanel &) = delete;

    void Create();
    void Release();
    bool ProcessInput(const SessionInputEvent &event);
    bool PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                         const RmlCommandWindowContent &content);
    bool Record(LegacyRenderFacade &facade) const;
    RmlCommandWindowRect ReferenceRect(int viewportWidth, int viewportHeight) const noexcept;

    RmlMuButton &CommandButton(std::size_t index) noexcept
    {
        return buttons_[index];
    }
    RmlMuButton &CloseButton() noexcept
    {
        return buttons_[RmlCommandWindowButtonCount];
    }

  private:
    class Impl;

    SessionBoundArray<RmlMuButton, RmlCommandWindowButtonCount + 1> buttons_;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Command

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Gens
{
class RmlGensRankingPanel final
{
  public:
    struct Content
    {
        std::uint64_t revision = 0;
        std::array<std::wstring, 14> labels;
        int faction = 0;
        int rank = 0;
    };
    struct Changes
    {
        bool close = false, focus = false;
    };
    explicit RmlGensRankingPanel(SessionKeeper &keeper);
    ~RmlGensRankingPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Gens

namespace UI::Modern::PC::HUD
{
inline constexpr int MainFrameTextureIndex = PC::Common::TextureIndex + 4;
}

namespace UI::Modern::PC::HUD
{
inline constexpr int MoveCommandTextureIndex = PC::Common::TextureIndex + 7;
}

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern
{
class RmlBuffLayer final
{
  public:
    struct Entry
    {
        int id = 0;
        int border = 0;
        bool debuff = false;
        bool operator==(const Entry &) const = default;
    };
    explicit RmlBuffLayer(SessionKeeper &keeper);
    ~RmlBuffLayer();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const std::vector<Entry> &entries);
    bool ProcessInput(const SessionInputEvent &event);
    int TakeCancel();
    int HoveredBuff() const;
    bool HasIcon(int id) const;
    bool HoverAnchor(float &x, float &y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

namespace Rml
{
class Element;
class ElementDocument;
class RenderManager;
} // namespace Rml
namespace UI::Modern
{
class RmlHudMapViewport;
class RmlHudMapMarkers final
{
  public:
    void Bind(Rml::ElementDocument &document);
    void Set(std::span<const WorldMinimapData::Marker> markers);
    void Render(RmlHudMapViewport &viewport, float opacity, bool namesVisible);

  private:
    struct Part
    {
        Rml::Geometry geometry;
        Rml::Texture texture;
    };
    struct Marker
    {
        Rml::Vector2f uv;
        int kind;
        std::string name;
        std::vector<Part> text;
        std::vector<Part> art;
        Rml::Geometry background;
    };
    void Build(Rml::RenderManager &manager, float opacity);
    void BuildLabel(Marker &marker, Rml::RenderManager &manager, float opacity);
    Part Sprite(Rml::RenderManager &manager, int id, Rml::Vector2f offset, float opacity,
                float width = 0);
    std::array<Rml::Element *, 2> labels_{};
    std::array<Rml::Element *, 2> origins_{};
    std::array<Rml::Element *, 5> art_{};
    Part npcImage_;
    std::vector<Marker> markers_;
    std::array<int, 2> fontVersions_{};
    Rml::Mesh backgroundSource_;
    Rml::Colourb backgroundColor_;
    std::array<float, 2> portalGrid_{};
    float labelExtra_ = 0;
    float opacity_ = -1;
};
} // namespace UI::Modern

namespace UI::Modern
{
class RmlHudMapViewport final : public Rml::Element
{
  public:
    explicit RmlHudMapViewport(const Rml::String &tag);
    void ConfigureMask(const std::filesystem::path &mesh, const std::array<float, 4> &rotation);
    bool SetImage(const LogicalRenderAssetMetadata &image);
    bool SetImageOrigin(Rml::Vector2f pixels);
    void ClearImage();
    bool SetView(Rml::Vector2f heroUv, Rml::Vector2f scale, float opacity);
    Rml::Vector2f Project(Rml::Vector2f imageUv) const noexcept;
    Rml::Vector2f ProjectDirection(Rml::Vector2f imageUvDirection) const noexcept;
    Rml::Matrix4f MarkerTransform() const noexcept;
    void BindMarkers(Rml::ElementDocument &document, float rotation);
    void SetMarkers(std::span<const WorldMinimapData::Marker> markers);
    bool SetMarkerView(float opacity, bool namesVisible);

  private:
    void OnRender() override;
    void BuildImage();
    Rml::Vector2f ImagePoint(Rml::Vector2f uv) const noexcept;
    Rml::Geometry mask_, image_;
    Rml::CallbackTexture texture_;
    LogicalRenderAssetRef asset_;
    Rml::Vector2f imageSize_, maskSize_, heroUv_, scale_{1, 1};
    Rml::Vector2f imageOriginPixels_;
    std::array<float, 4> rotation_{1, 0, 0, 1};
    float opacity_ = 1;
    bool imageDirty_ = false;
    RmlHudMapMarkers markers_;
    float markerOpacity_ = 1, markerRotation_ = 0;
    bool namesVisible_ = true;
};
} // namespace UI::Modern

class LegacyRenderFacade;
class SessionKeeper;

namespace UI::Modern
{
struct RmlMainFrameTransform final
{
    float left = 0.0F;
    float top = 0.0F;
    float scale = 1.0F;
};

struct RmlMainFrameRect final
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

RmlMainFrameTransform CalculateRmlMainFrameTransform(int viewportWidth, int viewportHeight,
                                                     float maximumScale) noexcept;
RmlMainFrameRect CalculateRmlMainFrameReferenceRect(const RmlMainFrameTransform &transform,
                                                    int viewportWidth, int viewportHeight, float x,
                                                    float y, float width, float height) noexcept;

enum class RmlMainFrameExperienceStyle : std::uint8_t
{
    Normal,
    Master,
    Fourth,
};

struct RmlMainFrameRequest final
{
    static constexpr int ButtonCount = 6;
    static constexpr int CurrentSkillSlot = 5;
    static constexpr int SkillSlotCount = CurrentSkillSlot + 1;

    std::array<RmlSkillIconState, SkillSlotCount> skillIcons{};
    std::array<float, SkillSlotCount> skillCooldowns{};

    std::array<ButtonVisualState, ButtonCount> buttons{};
    std::int64_t currentExperience = 0;
    std::int64_t nextExperience = 0;
    int life = 0;
    int maximumLife = 1;
    int mana = 0;
    int maximumMana = 1;
    int shield = 0;
    int maximumShield = 1;
    int ability = 0;
    int maximumAbility = 1;
    int experiencePage = 0;
    int selectedHotSkillSlot = -1;
    float experienceRatio = 0.0F;
    bool visible = false;
    bool poisoned = false;
    bool skillSelectionVisible = false;
    bool skillSecondPage = false;
    ButtonVisualState skillPageButton = ButtonVisualState::Up;
    RmlMainFrameExperienceStyle experienceStyle = RmlMainFrameExperienceStyle::Normal;
};

class RmlMainFrameLayer final
{
  public:
    explicit RmlMainFrameLayer(SessionKeeper &keeper);
    ~RmlMainFrameLayer();

    RmlMainFrameLayer(const RmlMainFrameLayer &) = delete;
    RmlMainFrameLayer &operator=(const RmlMainFrameLayer &) = delete;

    void Stage(const RmlMainFrameRequest &request) noexcept;
    bool PrepareOnWorker(int viewportWidth, int viewportHeight);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern
{
class RmlMasterSkillTreePanel final
{
  public:
    static constexpr int SlotCount = 108; // Common.Global.MUDefines.MAX_MASTER_SKILL_SLOT.
    struct Slot
    {
        RmlSkillIconState icon;
        int rank = 0;
        int arrow = 0;
        bool operator==(const Slot &) const = default;
    };
    struct State
    {
        std::array<std::wstring, 7> labels;
        std::array<Slot, SlotCount> slots;
        bool operator==(const State &) const = default;
    };
    struct Hover
    {
        int slot = 0;
        float x = 0, y = 0;
        bool above = false;
    };
    explicit RmlMasterSkillTreePanel(SessionKeeper &keeper);
    ~RmlMasterSkillTreePanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const State &state);
    bool ProcessInput(const SessionInputEvent &event);
    bool Record(LegacyRenderFacade &facade) const;
    bool TakeClose();
    int TakeSkillClick();
    bool OwnsPointer() const;
    Hover Hovered() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

class SessionKeeper;
class LegacyRenderFacade;
namespace UI::Modern
{
class RmlMiniMapPanel final
{
  public:
    explicit RmlMiniMapPanel(SessionKeeper &keeper);
    ~RmlMiniMapPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible,
                         const LogicalRenderAssetMetadata *image, float heroX, float heroY,
                         float heroHeading, std::span<const WorldMinimapData::Marker> markers,
                         std::uint64_t markerRevision, const std::array<int, 2> &imageOriginPixels);
    void AdjustZoom(int direction);
    void AdjustOpacity(int direction);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

class LegacyRenderFacade;
class SessionKeeper;

namespace UI::Modern
{
inline constexpr std::size_t RmlMoveCommandVisibleRows = 12;
inline constexpr std::size_t RmlMoveCommandFavoriteRows = 5;

struct RmlMoveCommandTransform final
{
    float left = 0.0F;
    float top = 0.0F;
    float scale = 1.0F;
};

struct RmlMoveCommandRect final
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

RmlMoveCommandTransform CalculateRmlMoveCommandTransform(float maximumScale) noexcept;
RmlMoveCommandRect CalculateRmlMoveCommandReferenceRect(const RmlMoveCommandTransform &transform,
                                                        int viewportWidth, int viewportHeight,
                                                        float x, float y, float width,
                                                        float height) noexcept;

struct RmlMoveCommandRow final
{
    static constexpr std::size_t StrifeCapacity = 16;
    static constexpr std::size_t MapNameCapacity = 32;
    static constexpr std::size_t ValueCapacity = 16;

    std::array<wchar_t, StrifeCapacity> strife{};
    std::array<wchar_t, MapNameCapacity> mapName{};
    std::array<wchar_t, ValueCapacity> requiredLevel{};
    std::array<wchar_t, ValueCapacity> requiredZen{};
    bool visible = false;
    bool disabled = false;
    bool over = false;
    bool down = false;
    bool selected = false;

    bool operator==(const RmlMoveCommandRow &) const = default;
};

struct RmlMoveCommandRequest final
{
    static constexpr std::size_t LabelCapacity = 64;

    std::array<wchar_t, LabelCapacity> title{};
    std::array<wchar_t, LabelCapacity> strifeLabel{};
    std::array<wchar_t, LabelCapacity> mapLabel{};
    std::array<wchar_t, LabelCapacity> levelLabel{};
    std::array<wchar_t, LabelCapacity> zenLabel{};
    std::array<wchar_t, LabelCapacity> favoriteLabel{};
    std::array<wchar_t, LabelCapacity> showMapLabel{};
    std::array<wchar_t, LabelCapacity> closeLabel{};
    std::array<RmlMoveCommandRow, RmlMoveCommandVisibleRows> rows{};
    std::array<RmlMoveCommandRow, RmlMoveCommandFavoriteRows> favorites{};
    RmlMuScrollBarState scrollBar{};
    bool visible = false;
    ButtonVisualState showMap = ButtonVisualState::Up;
    ButtonVisualState close = ButtonVisualState::Up;

    bool operator==(const RmlMoveCommandRequest &) const = default;
};

class RmlMoveCommandLayer final
{
  public:
    explicit RmlMoveCommandLayer(SessionKeeper &keeper);
    ~RmlMoveCommandLayer();

    RmlMoveCommandLayer(const RmlMoveCommandLayer &) = delete;
    RmlMoveCommandLayer &operator=(const RmlMoveCommandLayer &) = delete;

    void Stage(const RmlMoveCommandRequest &request) noexcept;
    bool PrepareOnWorker(int viewportWidth, int viewportHeight);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

class SessionKeeper;
class LegacyRenderFacade;
namespace UI::Modern
{
class RmlSkillListLayer final
{
  public:
    struct Entry
    {
        RmlSkillIconState icon;
        int skillIndex = -1, hotKey = -1;
        float x = 0, y = 0, width = 0, height = 0, cooldown = 0;
        bool operator==(const Entry &) const = default;
    };
    explicit RmlSkillListLayer(SessionKeeper &keeper);
    ~RmlSkillListLayer();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const std::vector<Entry> &entries);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

class LegacyRenderFacade;
class SessionKeeper;

namespace UI::Modern
{
struct RmlTopMenuTransform final
{
    float left = 0.0F;
    float top = 0.0F;
    float scale = 1.0F;
};

struct RmlTopMenuRect final
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

RmlTopMenuTransform CalculateRmlTopMenuTransform(int viewportWidth, int viewportHeight,
                                                 float maximumScale) noexcept;
RmlTopMenuRect CalculateRmlTopMenuReferenceRect(const RmlTopMenuTransform &transform,
                                                int viewportWidth, int viewportHeight, float x,
                                                float y, float width, float height) noexcept;

struct RmlTopMenuRequest final
{
    static constexpr std::size_t MapNameCapacity = 64;

    wchar_t mapName[MapNameCapacity]{};
    int positionX = 0;
    int positionY = 0;
    bool visible = false;
    bool helperActive = false;
    ButtonVisualState optionButton = ButtonVisualState::Up;
    ButtonVisualState actionButton = ButtonVisualState::Up;
};

class RmlTopMenuLayer final
{
  public:
    explicit RmlTopMenuLayer(SessionKeeper &keeper);
    ~RmlTopMenuLayer();

    RmlTopMenuLayer(const RmlTopMenuLayer &) = delete;
    RmlTopMenuLayer &operator=(const RmlTopMenuLayer &) = delete;

    void Stage(const RmlTopMenuRequest &request) noexcept;
    bool PrepareOnWorker(int viewportWidth, int viewportHeight);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

namespace UI::Modern::PC::HUD
{
inline constexpr int TopMenuTextureIndex = PC::Common::TextureIndex + 6;
}

namespace UI::Modern::PC::MuHelper
{
inline constexpr int TextureIndex = PC::Common::TextureIndex + 10;
}

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::MuHelper
{
inline constexpr std::size_t RmlMuHelperAssignedSkillCount = 6;
inline constexpr std::size_t RmlMuHelperAvailableSkillCount = 10;
inline constexpr std::size_t RmlMuHelperRangeCount = 8;

enum class RmlMuHelperTextId : std::size_t
{
    Title,
    Hunting,
    Obtaining,
    OtherSettings,
    Range,
    UseRegularAttack,
    Potion,
    LongDistanceCounterAttack,
    OriginalPosition,
    Seconds,
    BasicSkill,
    ActivationSkill1,
    ActivationSkill2,
    Delay,
    Condition,
    Setting,
    Combo,
    UseDarkSpirits,
    AutoAttack,
    CeaseAttack,
    AttackTogether,
    Party,
    AutoHeal,
    DrainLife,
    BuffDuration,
    RepairItem,
    PickAll,
    PickSelected,
    Jewel,
    SetItem,
    Zen,
    ExcellentItem,
    AddExtraItem,
    Add,
    Delete,
    AutoAcceptFriend,
    AutoAcceptGuild,
    PvpCounterattack,
    ManualControlYield,
    Initialization,
    SaveSetting,
    AutoRecovery,
    AutoPotion,
    HpStatus,
    ActivationSkill,
    PreCondition,
    MonsterWithinRange,
    MonsterAttackingMe,
    SubCondition,
    MoreThanTwo,
    MoreThanThree,
    MoreThanFour,
    MoreThanFive,
    PreferencePartyHeal,
    HpStatusParty,
    BuffSupport,
    BuffDurationParty,
    TimeCastingBuff,
    Close,
    LootRange,
    SaveSetup,
    MonsterCondition,
    SkillInterval,
    ConcentratedMonsters,
    UseSkillsClosely,
    Count,
};

struct RmlMuHelperFormValues final
{
    bool fallbackBasicAttack = true;
    bool concentratedMonsters = false;
    bool useSkillsClosely = false;
    bool usePotion = false;
    bool longRangeCounter = false;
    bool returnPosition = true;
    int returnSeconds = 10;
    std::array<bool, 2> skillTimer{};
    std::array<bool, 2> skillCondition{};
    std::array<int, 2> skillInterval{};
    bool combo = false;
    bool buffDuration = true;
    bool useDarkRaven = false;
    int darkRavenMode = 0;
    bool supportParty = false;
    bool autoHeal = false;
    bool drainLife = false;
    bool repairItem = false;
    bool pickAll = false;
    bool pickSelected = false;
    bool pickJewel = false;
    bool pickZen = false;
    bool pickAncient = false;
    bool pickExcellent = false;
    bool pickExtra = false;
    bool autoAcceptFriend = false;
    bool autoAcceptGuild = false;
    bool selfDefense = false;
    int manualControlYieldSeconds = 0;
    int potionThreshold = 40;
    int healThreshold = 60;
    bool partyHeal = false;
    int partyHealThreshold = 60;
    bool partyBuffDuration = true;
    int partyBuffInterval = 0;
    int skillPreCondition = 0;
    int skillMobCount = 0;
    std::wstring itemInput;

    bool operator==(const RmlMuHelperFormValues &) const = default;
};

struct RmlMuHelperContent final
{
    std::array<std::wstring, static_cast<std::size_t>(RmlMuHelperTextId::Count)> text{};
    RmlMuHelperFormValues form;
    std::array<int, RmlMuHelperAssignedSkillCount> assignedSkills{};
    std::array<RmlSkillIconState, RmlMuHelperAssignedSkillCount> assignedIcons{};
    std::array<RmlSkillIconState, RmlMuHelperAvailableSkillCount> availableIcons{};
    std::vector<std::wstring> extraItems;
    int characterClass = 0;
    int tab = 0;
    int subPage = -1;
    int huntingRange = 1;
    int obtainingRange = 1;
    std::size_t availableSkillCount = 0;
    bool skillPickerVisible = false;

    std::wstring &operator[](RmlMuHelperTextId id) noexcept
    {
        return text[static_cast<std::size_t>(id)];
    }

    bool operator==(const RmlMuHelperContent &) const = default;
};

struct RmlMuHelperChanges final
{
    std::optional<RmlMuHelperFormValues> form;
    std::optional<int> tab;
    std::optional<int> huntingRange;
    std::optional<int> obtainingRange;
    std::optional<int> openSubPage;
    std::optional<int> assignedSkillSlot;
    std::optional<int> clearSkillSlot;
    std::optional<int> availableSkillSlot;
    std::optional<int> removeItemIndex;
    bool addItem = false;
    bool close = false;
    bool reset = false;
    bool save = false;
    bool closeSubPage = false;
    bool resetSubPage = false;
    bool saveSubPage = false;
};

class RmlMuHelperPanel final
{
  public:
    static float Width() noexcept;
    static float Height() noexcept;

    explicit RmlMuHelperPanel(SessionKeeper &keeper);
    ~RmlMuHelperPanel();

    RmlMuHelperPanel(const RmlMuHelperPanel &) = delete;
    RmlMuHelperPanel &operator=(const RmlMuHelperPanel &) = delete;

    void Create();
    void Release();
    bool ProcessInput(const SessionInputEvent &event);
    bool HasTextInputFocus() const noexcept;
    bool OwnsPointer() const noexcept;
    RmlMuHelperChanges TakeChanges();
    bool PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                         const RmlMuHelperContent &content);
    bool Record(LegacyRenderFacade &facade) const;
    std::optional<RmlTextInputArea> TextInputArea() const;

  private:
    static constexpr std::size_t ButtonCount = 36;

    SessionBoundArray<RmlMuButton, ButtonCount> buttons_;
    SessionBoundArray<RmlMuSlot, RmlMuHelperAssignedSkillCount> assignedSlots_;
    SessionBoundArray<RmlMuSlot, RmlMuHelperAvailableSkillCount> availableSlots_;

    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::MuHelper
