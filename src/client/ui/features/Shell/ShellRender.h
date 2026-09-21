#pragma once
#include "app/AppWindow.h"
#include "app/ApplicationDiagnostics.h"
#include "data/CharacterData.h"
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
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/WorldPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "session/SessionAudio.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Activities/ActivitiesLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Character
{
class RmlCharacterCreatePanel final
{
  public:
    struct Rect
    {
        float x = 0, y = 0, width = 0, height = 0;
    };
    struct Content
    {
        std::uint64_t revision = 0;
        std::array<std::wstring, MAX_CLASS> classes;
        std::array<bool, MAX_CLASS> enabled{};
        std::array<std::wstring, 5> statTitles, stats;
        std::wstring nameTitle, classTitle, description, ok, cancel;
        int selected = 0;
        bool showCharisma = false;
    };
    struct Changes
    {
        std::optional<int> selected;
        bool ok = false, cancel = false;
    };
    explicit RmlCharacterCreatePanel(SessionKeeper &keeper);
    ~RmlCharacterCreatePanel();
    void Release();
    void ClearName();
    const std::wstring &Name() const;
    std::optional<RmlTextInputArea> TextInputArea() const;
    Rect PreviewBounds() const;
    std::span<const float> ModelParameters(int classIndex) const;
    std::span<const float> PreviewCamera() const;
    bool PrepareOnWorker(int width, int height, bool visible, double effectElapsedMilliseconds,
                         const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Character

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::Login
{
struct RmlLoginContent final
{
    std::wstring serverName;
    std::wstring accountLabel;
    std::wstring passwordLabel;
    std::wstring okLabel;
    std::wstring cancelLabel;
};

class RmlLoginPanel final
{
  public:
    static float Width() noexcept;
    static float Height() noexcept;
    static int LeftFor(int viewportWidth) noexcept;
    static int TopFor(int viewportHeight) noexcept;

    explicit RmlLoginPanel(SessionKeeper &keeper);
    ~RmlLoginPanel();

    void Create();
    void Release();
    void SetPosition(int x, int y);
    void Show(bool show);
    void SetCredentials(const std::wstring &account, const std::wstring &password);
    const std::wstring &Account() const noexcept;
    const std::wstring &Password() const noexcept;
    void FocusInitialInput();
    void FocusAccountInput();
    void FocusPasswordInput();
    bool ProcessInput(const SessionInputEvent &event);
    std::optional<RmlTextInputArea> TextInputArea() const;
    bool PrepareOnWorker(int viewportWidth, int viewportHeight, const RmlLoginContent &content);
    bool Record(LegacyRenderFacade &facade);

    RmlMuButton &OkButton() noexcept
    {
        return buttons_[0];
    }
    RmlMuButton &CancelButton() noexcept
    {
        return buttons_[1];
    }

  private:
    class Impl;

    SessionBoundArray<RmlMuButton, 2> buttons_;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Login

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::Login
{
class RmlLoginSceneButtons final
{
  public:
    static int ButtonWidth() noexcept;
    static int ButtonHeight() noexcept;
    static int HorizontalMargin() noexcept;
    static int ButtonY(int viewportHeight) noexcept;

    explicit RmlLoginSceneButtons(SessionKeeper &keeper);
    ~RmlLoginSceneButtons();

    void Create();
    void Release();
    void SetPosition(int menuX, int creditX, int y);
    void Show(bool show);
    bool ProcessInput(const SessionInputEvent &event);
    bool PrepareOnWorker(int viewportWidth, int viewportHeight);
    bool Record(LegacyRenderFacade &facade);

    RmlMuButton &MenuButton() noexcept
    {
        return buttons_[0];
    }
    RmlMuButton &CreditButton() noexcept
    {
        return buttons_[1];
    }

  private:
    class Impl;

    SessionBoundArray<RmlMuButton, 2> buttons_;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Login
