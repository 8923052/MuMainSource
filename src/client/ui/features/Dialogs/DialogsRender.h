#pragma once

#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern
{
class RmlContextMenuPanel final
{
  public:
    struct Content
    {
        std::wstring title;
        std::vector<std::wstring> labels;
        int x = 0, y = 0;
        bool visible = false;
        bool dismissOnOutsideRelease = true;
    };
    RmlContextMenuPanel(SessionKeeper &keeper, const char *group, const char *document,
                        const char *contextName);
    ~RmlContextMenuPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    int TakeCommand();
    bool TakeDismiss();
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern
{
enum class RmlMessageBoxMode
{
    MessageOnly,
    Cancel,
    Ok,
    OkCancel,
    Text,
    Password,
    Number,
};

std::wstring SanitizeRmlMessageBoxNumber(std::wstring_view value);

struct RmlMessageBoxContent final
{
    std::wstring firstLine;
    std::wstring secondLine;
    std::wstring okLabel;
    std::wstring cancelLabel;
    std::wstring title;
};

class RmlMessageBoxPanel final
{
  public:
    static int Width() noexcept;
    static int Height() noexcept;
    static int ButtonWidth() noexcept;
    static int ButtonHeight() noexcept;
    static int SingleButtonX() noexcept;
    static int OkButtonX() noexcept;
    static int CancelButtonX() noexcept;
    static int ButtonY() noexcept;

    // Exact Caution.gfx mcCaution units. RmlDocumentHost applies RmlUiScale.
    static int CautionWidth() noexcept;
    static int CautionHeight() noexcept;
    static int CautionButtonWidth() noexcept;
    static int CautionButtonHeight() noexcept;
    static int CautionSingleButtonX() noexcept;
    static int CautionOkButtonX() noexcept;
    static int CautionCancelButtonX() noexcept;
    static int CautionBottomY() noexcept;
    static int CautionMessageHeight() noexcept;

    explicit RmlMessageBoxPanel(SessionKeeper &keeper);
    ~RmlMessageBoxPanel();

    void Create();
    void CreateS16Caution();
    void Release();
    void SetPosition(int x, int y);
    void SetMode(RmlMessageBoxMode mode);
    void Show(bool show);
    void SetInputValue(const std::wstring &value);
    const std::wstring &InputValue() const noexcept;
    void FocusInput();
    void SetPassword(const std::wstring &password);
    const std::wstring &Password() const noexcept;
    void FocusPasswordInput();
    bool ProcessInput(const SessionInputEvent &event);
    std::optional<RmlTextInputArea> TextInputArea() const;
    bool PrepareOnWorker(int viewportWidth, int viewportHeight,
                         const RmlMessageBoxContent &content);
    bool Record(LegacyRenderFacade &facade) const;
    int PositionX() const noexcept
    {
        return x_;
    }
    int PositionY() const noexcept
    {
        return y_;
    }
    int CurrentHeight() const noexcept
    {
        return height_;
    }

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
    int x_ = 0;
    int y_ = 0;
    int height_ = Height();
    bool s16Caution_ = false;
};
} // namespace UI::Modern

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Help
{
class RmlHelpPanel final
{
  public:
    struct Content
    {
        std::uint64_t revision = 0;
        std::array<std::wstring, 5> labels;
        std::array<RmlMuScrollingList::Data, 2> rows;
    };
    struct Changes
    {
        int tab = -1;
        bool close = false, focus = false;
    };
    explicit RmlHelpPanel(SessionKeeper &keeper);
    ~RmlHelpPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, int tab, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Help

namespace UI::Modern::PC::Option
{
inline constexpr int TextureIndex = PC::Common::TextureIndex + 1;
}

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::Option
{
std::array<int, 2> RmlOptionViewportFor(int viewportWidth, int viewportHeight,
                                        float maximumScale) noexcept;

struct RmlOptionChoices final
{
    std::vector<std::wstring> fonts;
    std::vector<std::wstring> languages;
    std::vector<std::wstring> resolutions;
};

struct RmlOptionContent final
{
    std::wstring title;
    std::wstring automaticAttack;
    std::wstring whisperSound;
    std::wstring nameDisplay;
    std::wstring soundVolume;
    std::wstring musicVolume;
    std::wstring slideHelp;
    std::wstring effectLimitation;
    std::wstring renderFullEffects;
    std::wstring font;
    std::wstring language;
    std::wstring resolution;
    std::wstring windowedMode;
    std::wstring close;
};

struct RmlOptionValues final
{
    bool automaticAttack = false;
    bool whisperSound = false;
    bool nameDisplay = true;
    int soundVolume = 0;
    int musicVolume = 0;
    bool slideHelp = false;
    int effectLevel = 0;
    bool renderFullEffects = false;
    int font = 0;
    int language = 0;
    int resolution = 0;
    bool windowedMode = false;

    bool operator==(const RmlOptionValues &) const = default;
};

struct RmlOptionChanges final
{
    std::optional<bool> automaticAttack;
    std::optional<bool> whisperSound;
    std::optional<bool> nameDisplay;
    std::optional<int> soundVolume;
    std::optional<int> musicVolume;
    std::optional<bool> slideHelp;
    std::optional<int> effectLevel;
    std::optional<bool> renderFullEffects;
    std::optional<int> font;
    std::optional<int> language;
    std::optional<int> resolution;
    std::optional<bool> windowedMode;
    bool close = false;
};

class RmlOptionPanel final
{
  public:
    static int Width() noexcept;
    static int Height() noexcept;

    explicit RmlOptionPanel(SessionKeeper &keeper);
    ~RmlOptionPanel();

    void Configure(RmlOptionChoices choices);
    void Release();
    void SetPosition(int x, int y);
    void Show(bool show);
    bool ProcessInput(const SessionInputEvent &event);
    RmlOptionChanges TakeChanges();
    bool PrepareOnWorker(int viewportWidth, int viewportHeight, const RmlOptionContent &content,
                         const RmlOptionValues &values);
    bool Record(LegacyRenderFacade &facade);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Option

class LegacyRenderFacade;
class SessionKeeper;

namespace UI::Modern::PC::ServerMessage
{
struct RmlServerMessageContent final
{
    static constexpr std::size_t LineCapacity = 5;
    std::array<std::wstring, LineCapacity> lines;
    std::size_t lineCount = 0;

    bool operator==(const RmlServerMessageContent &) const = default;
};

class RmlServerMessagePanel final
{
  public:
    static int Width();
    static int Height();
    static int Left();
    static int Top(int viewportHeight);

    explicit RmlServerMessagePanel(SessionKeeper &keeper);
    ~RmlServerMessagePanel();

    void Release();
    void SetPosition(int x, int y);
    void Show(bool show);
    bool PrepareOnWorker(int viewportWidth, int viewportHeight,
                         const RmlServerMessageContent &content);
    bool Record(LegacyRenderFacade &facade);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::ServerMessage

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::SystemMenu
{
enum class RmlSystemMenuMode : std::uint8_t
{
    Login,
    Character,
    Game,
};

struct RmlSystemMenuContent final
{
    std::wstring title;
    std::wstring exit;
    std::wstring server;
    std::wstring character;
    std::wstring option;
    std::wstring close;
};

class RmlSystemMenuPanel final
{
  public:
    static int Width();

    explicit RmlSystemMenuPanel(SessionKeeper &keeper);
    ~RmlSystemMenuPanel();

    static int HeightFor(RmlSystemMenuMode mode) noexcept;

    void Create(RmlSystemMenuMode mode);
    void Release();
    void SetPosition(int x, int y);
    void Show(bool show);
    bool ProcessInput(const SessionInputEvent &event);
    bool PrepareOnWorker(int viewportWidth, int viewportHeight,
                         const RmlSystemMenuContent &content);
    bool Record(LegacyRenderFacade &facade);

    RmlMuButton &ExitButton() noexcept
    {
        return buttons_[0];
    }
    RmlMuButton &ServerButton() noexcept
    {
        return buttons_[1];
    }
    RmlMuButton &CharacterButton() noexcept
    {
        return buttons_[2];
    }
    RmlMuButton &OptionButton() noexcept
    {
        return buttons_[3];
    }
    RmlMuButton &CloseButton() noexcept
    {
        return buttons_[4];
    }

  private:
    class Impl;

    SessionBoundArray<RmlMuButton, 5> buttons_;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::SystemMenu
