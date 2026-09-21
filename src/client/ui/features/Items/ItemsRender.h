#pragma once

#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace UI::Modern::PC::Inventory
{
inline constexpr int PrivateStoreTextureIndex = PC::Common::TextureIndex + 13;
} // namespace UI::Modern::PC::Inventory

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern::PC::Inventory
{
class RmlDurabilityLayer final
{
  public:
    static constexpr int EquipmentCount = 12;
    struct Warning
    {
        int type = 0, state = 0;
        bool operator==(const Warning &) const = default;
    };
    struct Content
    {
        std::array<Warning, EquipmentCount> equipment{};
        std::wstring ammunition;
        bool warningsVisible = true;
        bool operator==(const Content &) const = default;
    };
    explicit RmlDurabilityLayer(SessionKeeper &keeper);
    ~RmlDurabilityLayer();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    int HoveredEquipment() const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Inventory

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::Inventory
{
class RmlInventoryExtensionPanel final
{
  public:
    struct Rect
    {
        float x, y, width, height;
    };
    struct Changes
    {
        bool dismiss = false;
        bool focus = false;
    };

    explicit RmlInventoryExtensionPanel(SessionKeeper &keeper);
    ~RmlInventoryExtensionPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, std::size_t bagCount,
                         const std::wstring &title);
    std::optional<bool> ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    void SetSlotFrames(std::span<const int> frames, std::size_t offset);
    bool Record(LegacyRenderFacade &facade) const;
    Rect ReferenceRect() const;
    Rect GridCell(std::size_t bag) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Inventory

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern::PC::Inventory
{
class RmlInventoryPanel final
{
  public:
    enum Button
    {
        Close,
        Repair,
        Store,
        Expand,
        SetOption,
        SocketOption,
        ButtonCount
    };
    struct Rect
    {
        float x, y, width, height;
    };
    struct Content
    {
        std::wstring title, zen;
        std::array<std::wstring, ButtonCount> labels;
        std::array<bool, ButtonCount> enabled{true, true, true, true, true, true};
        std::array<bool, ButtonCount> shown{true, true, true, true, true, true};
        bool showHelm = true, showGloves = true;
        std::array<int, 12> equipmentFrames{};
        std::array<int, 64> gridFrames{};
        bool operator==(const Content &) const = default;
    };
    explicit RmlInventoryPanel(SessionKeeper &keeper);
    ~RmlInventoryPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const Content &content);
    std::optional<bool> ProcessInput(const SessionInputEvent &event);
    bool TakeClick(Button button);
    bool TakeFocus();
    bool Hovered(Button button) const;
    int HoveredEquipment() const;
    bool Record(LegacyRenderFacade &facade) const;
    Rect PanelRect() const;
    Rect GridCell() const;
    Rect EquipmentRect(std::size_t index) const;
    Rect ButtonRect(Button button) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Inventory

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern::PC::Inventory
{
class RmlItemDialogPanel final
{
  public:
    struct Rect
    {
        float x, y, width, height;
    };
    RmlItemDialogPanel(SessionKeeper &keeper, const char *document,
                       const char *group = "Inventory");
    ~RmlItemDialogPanel();
    void Release();
    void SetText(const char *id, std::wstring_view text);
    void SetTextColor(const char *id, std::uint32_t color);
    void SetButtonVisible(const char *id, bool visible);
    void SetButtonEnabled(const char *id, bool enabled);
    void ConfigureList(const char *list, const char *scrollbar, const char *rowTemplate);
    void SetListData(const RmlMuScrollingList::Data &rows);
    std::optional<std::size_t> TakeListSelection();
    void SetKeypadOrder(std::span<const int> order);
    void ConfigureInput(const char *id, int maxLength);
    const std::wstring &InputValue() const;
    std::optional<RmlTextInputArea> TextInputArea() const;
    bool PrepareOnWorker(int width, int height, bool visible = true);
    bool ProcessInput(const SessionInputEvent &event);
    bool ProcessPanelInput(const SessionInputEvent &event);
    bool TakeFocus();
    bool TakeClick(const char *id);
    bool Record(LegacyRenderFacade &facade) const;
    Rect Bounds() const;
    Rect SlotBounds(const char *id) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Inventory

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern::PC::Inventory
{
class RmlItemExplanationPanel final
{
  public:
    struct Line
    {
        std::wstring text;
        std::string style;
        bool operator==(const Line &) const = default;
    };
    struct Row
    {
        std::vector<std::wstring> cells;
        bool available = true;
        bool operator==(const Row &) const = default;
    };
    struct Content
    {
        std::uint64_t revision = 0;
        std::vector<Line> heading, notes;
        std::vector<std::wstring> columns;
        std::vector<Row> rows;
        bool operator==(const Content &) const = default;
    };
    RmlItemExplanationPanel(SessionKeeper &keeper, const char *contextName);
    ~RmlItemExplanationPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Inventory

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern::PC::Inventory
{
// Shop, storage, trade and mix owners supply content and keep their item rules.
class RmlItemPanel final
{
  public:
    struct Rect
    {
        float x, y, width, height;
    };
    RmlItemPanel(SessionKeeper &keeper, const char *document,
                 const char *overlayDocument = nullptr);
    ~RmlItemPanel();
    void Release();
    void SetVisible(bool visible);
    void SetText(const char *id, std::wstring_view text);
    void SetMarkup(const char *id, std::string_view markup);
    void SetShown(const char *id, bool shown);
    void SetEnabled(const char *id, bool enabled);
    void SetFrame(const char *id, int frame);
    void SetSlotFrames(std::span<const int> frames, int offset = 0);
    bool PrepareOnWorker(int width, int height);
    std::optional<bool> ProcessInput(const SessionInputEvent &event);
    bool TakeClick(const char *id);
    bool TakeFocus();
    bool Record(LegacyRenderFacade &facade) const;
    bool RecordOverlay(LegacyRenderFacade &facade) const;
    Rect PanelRect() const;
    Rect GridCell(std::size_t grid = 0) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Inventory

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::Inventory
{
inline constexpr std::size_t RmlPrivateStoreSlotCount = 32;

enum class RmlPrivateStoreMode
{
    Seller,
    Buyer,
};

struct RmlPrivateStoreRect final
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct RmlPrivateStoreContent final
{
    std::wstring title;
    std::wstring shopNameLabel;
    std::wstring shopName;
    std::wstring openLabel;
    std::wstring closeLabel;
    bool shopOpen = false;
    bool openEnabled = true;
    bool showSellerActions = true;

    bool operator==(const RmlPrivateStoreContent &) const = default;
};

struct RmlPrivateStoreChanges final
{
    std::optional<std::wstring> shopName;
    std::optional<std::size_t> clearSlot;
    std::optional<std::size_t> selectSlot;
    bool open = false;
    bool close = false;
    bool dismiss = false;
    bool focus = false;
};

bool RmlPrivateStoreCloseEnabled(RmlPrivateStoreMode mode, bool shopOpen) noexcept;

class RmlPrivateStorePanel final
{
  public:
    static float Width() noexcept;
    static float Height() noexcept;
    static std::size_t GridColumns() noexcept;
    static std::size_t GridRows() noexcept;

    explicit RmlPrivateStorePanel(SessionKeeper &keeper,
                                  RmlPrivateStoreMode mode = RmlPrivateStoreMode::Seller);
    ~RmlPrivateStorePanel();

    RmlPrivateStorePanel(const RmlPrivateStorePanel &) = delete;
    RmlPrivateStorePanel &operator=(const RmlPrivateStorePanel &) = delete;

    void Create();
    void Release();
    bool ProcessInput(const SessionInputEvent &event);
    std::optional<bool> RouteInput(const SessionInputEvent &event);
    RmlPrivateStoreChanges TakeChanges();
    bool PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                         const RmlPrivateStoreContent &content);
    bool Record(LegacyRenderFacade &facade) const;
    RmlPrivateStoreRect ReferenceRect(int viewportWidth, int viewportHeight) const noexcept;
    RmlPrivateStoreRect InventoryGridRect(int viewportWidth, int viewportHeight) const noexcept;
    std::optional<RmlTextInputArea> TextInputArea() const;

  private:
    class Impl;

    std::array<RmlMuButton, 3> buttons_;
    std::array<RmlMuSlot, RmlPrivateStoreSlotCount> slots_;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Inventory

enum SKILL_TOOLTIP_RENDER_POINT
{
    STRP_NONE = 0,
    STRP_TOPLEFT,
    STRP_TOPCENTER,
    STRP_TOPRIGHT,
    STRP_LEFTCENTER,
    STRP_CENTER,
    STRP_RIGHTCENTER,
    STRP_BOTTOMLEFT,
    STRP_BOTTOMCENTER,
    STRP_BOOTOMRIGHT
};

// guild

// text 관련

// party

// inventory

void RenderTipTextList(const int sx, const int sy, int TextNum, int Tab,
                       int iSort = RT3_SORT_CENTER, int iRenderPoint = STRP_NONE,
                       BOOL bUseBG = TRUE);

//  Party.

void RenderItem3D(float sx, float sy, float Width, float Height, int Type, int Level,
                  int excellentFlags, int ancientDiscriminator, bool PickUp = false);
void RenderSkillInfo(int sx, int sy, int Type, int SkillNum = 0, int iRenderPoint = STRP_NONE);

namespace ItemRulesDetail
{
UI::Modern::RmlTooltipColor TooltipColor(int legacyColor) noexcept;
UI::Modern::RmlTooltipBackground TooltipBackground(int legacyColor) noexcept;
} // namespace ItemRulesDetail
