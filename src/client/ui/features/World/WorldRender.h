#pragma once
#include "data/CharacterData.h"
#include "render/Sprites.h"
#include "support/CoreMath.h"
#include "ui/runtime/UiRuntime.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

class CameraProjection;

class CCharInfoBalloon : public CSprite
{
  protected:
    CameraProjection &cameraProjection_;
    CHARACTER *m_pCharInfo;
    DWORD m_dwNameColor;
    wchar_t m_szName[64];
    wchar_t m_szGuild[64];
    wchar_t m_szClass[64];

  public:
    explicit CCharInfoBalloon(SessionKeeper &keeper);
    virtual ~CCharInfoBalloon();

    void Create(CHARACTER *pCharInfo);
    void Render();

    void SetInfo();

  private:
    // Re-runs SetInfo() on locale change so the cached guild / class
    // strings displayed over the character flip to the new language
    // without waiting for the next character refresh.
    static void OnLocaleChanged(void *ctx) noexcept;
};

class SessionKeeper;
class LegacyRenderFacade;
namespace UI::Modern::PC::World
{
class RmlMapNamePanel final
{
  public:
    explicit RmlMapNamePanel(SessionKeeper &keeper);
    ~RmlMapNamePanel();
    void Release();
    float FadeSeconds() const;
    float HoldSeconds() const;
    bool PrepareOnWorker(int width, int height, bool visible, float alpha,
                         const std::array<std::wstring, 3> &text);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::World

class SessionKeeper;
class LegacyRenderFacade;
namespace UI::Modern::PC::World
{
class RmlMonsterInfoLayer final
{
  public:
    struct Content
    {
        std::array<wchar_t, MAX_MONSTER_NAME + 1> name{};
        int x = 0, y = 0;
        float health = 0;
        bool showName = false, showHealth = true;
        bool operator==(const Content &) const = default;
    };
    explicit RmlMonsterInfoLayer(SessionKeeper &keeper);
    ~RmlMonsterInfoLayer();
    void Release();
    float WorldRaise() const;
    std::array<float, 2> TargetAnchor() const;
    bool PrepareOnWorker(int width, int height, std::span<const Content> content);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::World

class LegacyRenderFacade;
class SessionKeeper;

namespace UI::Modern
{
enum class RmlPlayerNameTone
{
    Normal,
    Hero,
    Caution,
    Murderer1,
    Murderer2,
    GameMaster,
};

enum class RmlGuildNameTone : std::uint8_t
{
    Neutral,
    Friendly,
    Hostile
};

struct RmlPlayerName final
{
    static constexpr std::size_t TextCapacity = 64;
    static constexpr std::size_t ChatCapacity = 288;

    std::array<wchar_t, TextCapacity> name{};
    std::array<wchar_t, TextCapacity> guildName{};
    std::array<wchar_t, TextCapacity> storeTitleTop{};
    std::array<wchar_t, TextCapacity> storeTitleBottom{};
    std::array<wchar_t, ChatCapacity> chat{};
    int characterKey = -1;
    int anchorX = 0;
    int anchorY = 0;
    int gensFrame = 0;
    int castleFrame = 0;
    RmlGuildNameTone guildTone = RmlGuildNameTone::Neutral;
    RmlPlayerNameTone tone = RmlPlayerNameTone::Normal;
    bool storeOpen = false;
    bool highlighted = false;

    bool operator==(const RmlPlayerName &) const = default;
};

struct RmlPlayerNameRequest final
{
    std::vector<RmlPlayerName> labels;
    bool visible = false;

    bool operator==(const RmlPlayerNameRequest &) const = default;
};

class RmlPlayerNameLayer final
{
  public:
    static int StoreWidth();
    static float StoreLeft();
    static float StoreTop();
    static float WorldRaise();
    static float StoreHighlightPeriod();
    static int StoreRightMargin();

    explicit RmlPlayerNameLayer(SessionKeeper &keeper);
    ~RmlPlayerNameLayer();

    RmlPlayerNameLayer(const RmlPlayerNameLayer &) = delete;
    RmlPlayerNameLayer &operator=(const RmlPlayerNameLayer &) = delete;

    static int StoreHeightFor(const RmlPlayerName &label) noexcept;
    void Stage(const RmlPlayerNameRequest &request);
    bool PrepareOnWorker(int viewportWidth, int viewportHeight);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

namespace UI::Modern::PC::World
{
inline constexpr int StoreLabelTextureIndex = PC::Common::TextureIndex + 14;
} // namespace UI::Modern::PC::World
