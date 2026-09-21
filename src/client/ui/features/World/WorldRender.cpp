#include "ui/features/World/WorldRender.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/ResourceData.h"
#include "data/WorldData.h"
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
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "render/FrameTape.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

namespace UI::Modern::PC::World
{
class RmlMapNamePanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "World", "map_name.rml")),
          design_(path_, {"Panel-Size", "Title-FadeSeconds", "Title-HoldSeconds"}),
          host_(keeper, "map-title-" + std::to_string(keeper.Id().RawValue()), path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        panel_ = nullptr;
        labels_.fill(nullptr);
        text_ = {};
        alpha_.reset();
        host_.Release();
    }
    bool Bind()
    {
        panel_ = host_.Document()->GetElementById("panel");
        constexpr std::array ids{"tfMapName", "tfStrife", "tfOccupyGuild"};
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            labels_[i] = host_.Document()->GetElementById(ids[i]);
            if (!labels_[i])
                return false;
        }
        return panel_ != nullptr;
    }
    bool Prepare(int width, int height, bool visible, float alpha,
                 const std::array<std::wstring, 3> &text)
    {
        if (!panel_ && !visible)
            return true;
        const auto size = design_.Values(0);
        if (!host_.Ensure(width, height, size[0], size[1]))
            return false;
        if (!panel_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        bool dirty = false;
        if (text_ != text)
        {
            for (std::size_t i = 0; i < text.size(); ++i)
                labels_[i]->SetInnerRML(
                    Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(text[i].c_str())));
            host_.Document()->GetElementById("art-17")->SetProperty(
                "display", text[2].empty() ? "none" : "block");
            text_ = text;
            dirty = true;
        }
        if (alpha_ != alpha)
        {
            panel_->SetProperty(Rml::PropertyId::Opacity, Rml::Property(alpha, Rml::Unit::NUMBER));
            alpha_ = alpha;
            dirty = true;
        }
        return host_.CaptureIfDirty(dirty);
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    Rml::Element *panel_ = nullptr;
    std::array<Rml::Element *, 3> labels_{};
    std::array<std::wstring, 3> text_;
    std::optional<float> alpha_;
};
RmlMapNamePanel::RmlMapNamePanel(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlMapNamePanel::~RmlMapNamePanel() = default;
void RmlMapNamePanel::Release()
{
    impl_->Release();
}
float RmlMapNamePanel::FadeSeconds() const
{
    return impl_->design_.Number(1);
}
float RmlMapNamePanel::HoldSeconds() const
{
    return impl_->design_.Number(2);
}
bool RmlMapNamePanel::PrepareOnWorker(int width, int height, bool visible, float alpha,
                                      const std::array<std::wstring, 3> &text)
{
    return impl_->Prepare(width, height, visible, alpha, text);
}
bool RmlMapNamePanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::World

namespace UI::Modern::PC::World
{
class RmlMonsterInfoLayer::Impl final
{
  public:
    struct Row
    {
        Rml::Element *root, *name, *hp;
        RmlMuProgressBar progress;
        std::optional<Content> content;
    };
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "World", "monster_info.rml")),
          design_(path_, {"World-Raise", "Target-Anchor", "Panel-Reference"}),
          host_(keeper, "monster-info-" + std::to_string(keeper.Id().RawValue()), path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        rows_.clear();
        container_ = prototype_ = nullptr;
        count_ = 0;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        container_ = document.GetElementById("monsters");
        auto *prototype = document.GetElementById("prototype");
        if (!container_ || !prototype)
            return false;
        prototype_ = prototype->GetFirstChild();
        return prototype_ != nullptr;
    }
    void Grow(std::size_t count)
    {
        while (rows_.size() < count)
        {
            auto *root = container_->AppendChild(prototype_->Clone());
            auto *hp = root->GetLastChild();
            rows_.push_back({root, root->GetFirstChild(), hp, {}, {}});
            rows_.back().progress.Bind(*hp->GetLastChild()->GetLastChild());
        }
    }
    bool Apply(Row &row, const Content &next, float scaleX, float scaleY, bool resized)
    {
        if (!resized && row.content == next)
            return false;
        if (!row.content || row.content->name != next.name)
            row.name->SetInnerRML(
                Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(next.name.data())));
        if (!row.content || row.content->showName != next.showName)
            row.name->SetProperty("display", next.showName ? "block" : "none");
        if (!row.content || row.content->showHealth != next.showHealth)
            row.hp->SetProperty("display", next.showHealth ? "block" : "none");
        row.root->SetProperty(Rml::PropertyId::Left, Rml::Property(next.x * scaleX, Rml::Unit::PX));
        row.root->SetProperty(Rml::PropertyId::Top, Rml::Property(next.y * scaleY, Rml::Unit::PX));
        row.progress.SetProgress(next.health < 0 ? 1 : next.health, 1);
        row.content = next;
        return true;
    }
    bool Prepare(int width, int height, std::span<const Content> content)
    {
        if (!container_ && content.empty())
            return true;
        if (!host_.Ensure(width, height))
            return false;
        if (!container_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(!content.empty()))
            return false;
        Grow(content.size());
        const auto viewport = host_.Viewport();
        const auto reference = design_.Values(2);
        const float scaleX = viewport.width / reference[0], scaleY = viewport.height / reference[1];
        const bool resized = scaleX != scaleX_ || scaleY != scaleY_;
        bool dirty = count_ != content.size();
        for (std::size_t i = 0; i < content.size(); ++i)
        {
            if (i >= count_)
                rows_[i].root->SetProperty("display", "block");
            dirty = Apply(rows_[i], content[i], scaleX, scaleY, resized) || dirty;
        }
        for (std::size_t i = content.size(); i < count_; ++i)
            rows_[i].root->SetProperty("display", "none");
        count_ = content.size();
        scaleX_ = scaleX;
        scaleY_ = scaleY;
        return host_.CaptureIfDirty(dirty);
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    Rml::Element *container_ = nullptr, *prototype_ = nullptr;
    std::vector<Row> rows_;
    std::size_t count_ = 0;
    float scaleX_ = 0, scaleY_ = 0;
};
RmlMonsterInfoLayer::RmlMonsterInfoLayer(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlMonsterInfoLayer::~RmlMonsterInfoLayer() = default;
void RmlMonsterInfoLayer::Release()
{
    impl_->Release();
}
float RmlMonsterInfoLayer::WorldRaise() const
{
    return impl_->design_.Number(0);
}
std::array<float, 2> RmlMonsterInfoLayer::TargetAnchor() const
{
    const auto anchor = impl_->design_.Values(1);
    return {anchor[0], anchor[1]};
}
bool RmlMonsterInfoLayer::PrepareOnWorker(int width, int height, std::span<const Content> content)
{
    return impl_->Prepare(width, height, content);
}
bool RmlMonsterInfoLayer::Record(LegacyRenderFacade &facade) const
{
    return !impl_->container_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::World

namespace UI::Modern
{
namespace
{
enum class StoreMetric
{
    Width,
    OneTitleHeight,
    TwoTitleHeight,
    Left,
    Top,
    WorldRaise,
    HighlightPeriod,
    RightMargin
};
const RmlUiDesign &StoreDesign()
{
    static const RmlUiDesign design("Data/UI/PC/World/store_label.rml",
                                    {"StoreLabel-Width", "StoreLabel-OneTitleHeight",
                                     "StoreLabel-TwoTitleHeight", "StoreLabel-Left",
                                     "StoreLabel-Top", "StoreLabel-WorldRaise",
                                     "StoreLabel-HighlightPeriod", "StoreLabel-RightMargin"});
    return design;
}

const char *ToneClass(RmlPlayerNameTone tone) noexcept
{
    switch (tone)
    {
    case RmlPlayerNameTone::Hero:
        return " hero";
    case RmlPlayerNameTone::Caution:
        return " caution";
    case RmlPlayerNameTone::Murderer1:
        return " murderer-one";
    case RmlPlayerNameTone::Murderer2:
        return " murderer-two";
    case RmlPlayerNameTone::GameMaster:
        return " game-master";
    default:
        return "";
    }
}

void SetText(Rml::Element &element, const wchar_t *text)
{
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(text)));
}

const char *GuildToneClass(RmlGuildNameTone tone) noexcept
{
    switch (tone)
    {
    case RmlGuildNameTone::Friendly:
        return " guild-friendly";
    case RmlGuildNameTone::Hostile:
        return " guild-hostile";
    default:
        return "";
    }
}

} // namespace

class RmlPlayerNameLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "player-name-layer-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "World", "store_label.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Stage(const RmlPlayerNameRequest &request)
    {
        pending_ = request;
        hasPending_ = true;
    }

    bool Prepare(int viewportWidth, int viewportHeight)
    {
        if (!hasPending_)
        {
            return true;
        }
        hasPending_ = false;
        if (!created_ && (!pending_.visible || pending_.labels.empty()))
        {
            active_ = pending_;
            return true;
        }

        if (!EnsureDocument(viewportWidth, viewportHeight) ||
            !EnsureLabelCount(pending_.labels.size()))
        {
            return false;
        }
        const RmlUiScaledViewport viewport = host_.Viewport();

        const bool scaleChanged = activeScale_ != viewport.scale;
        const bool countChanged = active_.labels.size() != pending_.labels.size();
        bool dirty = scaleChanged || countChanged || active_.visible != pending_.visible;
        for (std::size_t index = 0; index < pending_.labels.size(); ++index)
        {
            const RmlPlayerName *previous =
                !scaleChanged && !countChanged ? &active_.labels[index] : nullptr;
            dirty = ApplyLabel(index, pending_.labels[index], previous, viewport.scale) || dirty;
        }

        active_ = pending_;
        activeScale_ = viewport.scale;
        if (!host_.SetVisible(active_.visible && !active_.labels.empty()))
        {
            return false;
        }
        return host_.CaptureIfDirty(dirty);
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return !created_ || host_.Record(facade);
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight))
        {
            return false;
        }
        if (created_)
        {
            return true;
        }
        root_ = host_.Document()->GetElementById("player-name-layer");
        if (root_ == nullptr)
        {
            Release();
            return false;
        }
        created_ = true;
        return true;
    }

    bool EnsureLabelCount(std::size_t count)
    {
        if (labels_.size() == count)
        {
            return true;
        }

        Rml::String markup;
        for (std::size_t index = 0; index < count; ++index)
        {
            markup += Rml::CreateString(
                "<div id=\"player-label-%zu\" class=\"player-label\">"
                "<div class=\"normal-block\">"
                "<div class=\"name-stack\"><div class=\"name-text\">"
                "<div id=\"gens-mark-%zu\" class=\"gens-mark\"></div>"
                "<div id=\"guild-name-%zu\" class=\"name-line guild-line\"></div>"
                "<div id=\"player-id-%zu\" class=\"name-line player-id\"></div>"
                "</div></div>"
                "<div id=\"castle-mark-%zu\" class=\"castle-mark\"></div>"
                "</div>"
                "<div class=\"store-label PlayerStore\">"
                "<div class=\"store-frame gfx-scale-grid-parts\"><i class=\"hudPlayerName.Bitmap57\"/><i class=\"hudPlayerName.Bitmap58\"/><i class=\"hudPlayerName.Bitmap56\"/><i class=\"hudPlayerName.Bitmap55\"/><i class=\"hudPlayerName.Bitmap54\"/><i class=\"hudPlayerName.Bitmap52\"/><i class=\"hudPlayerName.Bitmap53\"/><i class=\"hudPlayerName.Bitmap51\"/><i class=\"hudPlayerName.Bitmap50\"/></div>"
                "<span id=\"store-tag-%zu\" class=\"store-tag MULabel\"></span>"
                "<span id=\"store-name-%zu\" class=\"store-name MULabel\"></span>"
                "<div id=\"store-title-top-%zu\" class=\"store-title MULabel\"></div>"
                "<div id=\"store-title-bottom-%zu\" class=\"store-title MULabel\"></div>"
                "</div>"
                "<div class=\"chat-anchor\"><div class=\"chat-bubble\">"
                "<div class=\"chat-frame\"><i class=\"hudPlayerName.Bitmap14\"/><i class=\"hudPlayerName.Bitmap13\"/><i class=\"hudPlayerName.Bitmap15\"/><i class=\"hudPlayerName.Bitmap21\"/><i class=\"hudPlayerName.Bitmap20\"/><i class=\"hudPlayerName.Bitmap19\"/><i class=\"hudPlayerName.Bitmap18\"/><i class=\"hudPlayerName.Bitmap17\"/><i class=\"hudPlayerName.Bitmap16\"/></div>"
                "<span id=\"chat-text-%zu\" class=\"chat-text\"></span><span class=\"chat-tail\"></span>"
                "</div></div></div>",
                index, index, index, index, index, index, index, index, index, index);
        }
        root_->SetInnerRML(markup);

        labels_.resize(count);
        gensMarks_.resize(count);
        castleMarks_.resize(count);
        guildNames_.resize(count);
        playerIds_.resize(count);
        storeTags_.resize(count);
        storeNames_.resize(count);
        storeTopTitles_.resize(count);
        storeBottomTitles_.resize(count);
        chatTexts_.resize(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            labels_[index] = Find("player-label", index);
            gensMarks_[index] = Find("gens-mark", index);
            castleMarks_[index] = Find("castle-mark", index);
            guildNames_[index] = Find("guild-name", index);
            playerIds_[index] = Find("player-id", index);
            storeTags_[index] = Find("store-tag", index);
            storeNames_[index] = Find("store-name", index);
            storeTopTitles_[index] = Find("store-title-top", index);
            storeBottomTitles_[index] = Find("store-title-bottom", index);
            chatTexts_[index] = Find("chat-text", index);
        }
        return AllBound(labels_) && AllBound(gensMarks_) && AllBound(castleMarks_) &&
               AllBound(guildNames_) && AllBound(playerIds_) && AllBound(storeTags_) &&
               AllBound(storeNames_) && AllBound(storeTopTitles_) && AllBound(storeBottomTitles_) &&
               AllBound(chatTexts_);
    }

    Rml::Element *Find(const char *prefix, std::size_t index)
    {
        return host_.Document()->GetElementById(Rml::CreateString("%s-%zu", prefix, index));
    }

    static bool AllBound(const std::vector<Rml::Element *> &elements)
    {
        return std::find(elements.begin(), elements.end(), nullptr) == elements.end();
    }

    bool ApplyLabel(std::size_t index, const RmlPlayerName &label, const RmlPlayerName *previous,
                    float scale)
    {
        bool dirty = false;
        if (previous == nullptr || previous->anchorX != label.anchorX)
        {
            labels_[index]->SetProperty(
                "left",
                Rml::CreateString("%dpx", static_cast<int>(std::lround(label.anchorX / scale))));
            dirty = true;
        }
        if (previous == nullptr || previous->anchorY != label.anchorY)
        {
            labels_[index]->SetProperty(
                "top",
                Rml::CreateString("%dpx", static_cast<int>(std::lround(label.anchorY / scale))));
            dirty = true;
        }

        const bool hasGuild = label.guildName[0] != L'\0';
        const bool hasChat = label.chat[0] != L'\0';
        const bool hasBottomTitle = label.storeTitleBottom[0] != L'\0';
        if (previous == nullptr || previous->storeOpen != label.storeOpen ||
            previous->guildTone != label.guildTone || previous->tone != label.tone ||
            previous->highlighted != label.highlighted || previous->gensFrame != label.gensFrame ||
            previous->castleFrame != label.castleFrame ||
            (previous->guildName[0] != L'\0') != hasGuild ||
            (previous->chat[0] != L'\0') != hasChat ||
            (previous->storeTitleBottom[0] != L'\0') != hasBottomTitle)
        {
            labels_[index]->SetClassNames(
                std::string("player-label") + ToneClass(label.tone) +
                GuildToneClass(label.guildTone) + (label.storeOpen ? " store-open" : "") +
                (hasGuild ? " has-guild" : "") + (hasChat ? " has-chat" : "") +
                (hasBottomTitle ? " long-store-title" : "") +
                (label.highlighted ? " highlighted" : ""));
            gensMarks_[index]->SetClassNames(
                label.gensFrame > 0 ? Rml::CreateString("gens-mark gens-%d", label.gensFrame)
                                    : "gens-mark");
            castleMarks_[index]->SetClassNames(
                label.castleFrame > 0
                    ? Rml::CreateString("castle-mark castle-%d", label.castleFrame)
                    : "castle-mark");
            dirty = true;
        }
        if (previous == nullptr || (previous->guildName[0] != L'\0') != hasGuild)
        {
            guildNames_[index]->SetProperty("display", hasGuild ? "block" : "none");
            dirty = true;
        }

        dirty = ApplyText(*playerIds_[index], label.name,
                          previous == nullptr ? nullptr : &previous->name) ||
                dirty;
        dirty = ApplyText(*guildNames_[index], label.guildName,
                          previous == nullptr ? nullptr : &previous->guildName) ||
                dirty;
        dirty = ApplyText(*storeNames_[index], label.name,
                          previous == nullptr ? nullptr : &previous->name) ||
                dirty;
        dirty = ApplyText(*storeTopTitles_[index], label.storeTitleTop,
                          previous == nullptr ? nullptr : &previous->storeTitleTop) ||
                dirty;
        dirty = ApplyText(*storeBottomTitles_[index], label.storeTitleBottom,
                          previous == nullptr ? nullptr : &previous->storeTitleBottom) ||
                dirty;
        dirty = ApplyText(*chatTexts_[index], label.chat,
                          previous == nullptr ? nullptr : &previous->chat) ||
                dirty;

        if (previous == nullptr)
        {
            SetText(*storeTags_[index], I18N::Game::Store);
            dirty = true;
        }
        return dirty;
    }

    template <std::size_t Size>
    static bool ApplyText(Rml::Element &element, const std::array<wchar_t, Size> &value,
                          const std::array<wchar_t, Size> *previous)
    {
        if (previous != nullptr && *previous == value)
        {
            return false;
        }
        SetText(element, value.data());
        return true;
    }

    void Release()
    {
        root_ = nullptr;
        labels_.clear();
        gensMarks_.clear();
        castleMarks_.clear();
        guildNames_.clear();
        playerIds_.clear();
        storeTags_.clear();
        storeNames_.clear();
        storeTopTitles_.clear();
        storeBottomTitles_.clear();
        chatTexts_.clear();
        active_ = {};
        pending_ = {};
        activeScale_ = 0.0F;
        created_ = false;
        hasPending_ = false;
        host_.Release();
    }

    RmlDocumentHost host_;
    Rml::Element *root_ = nullptr;
    std::vector<Rml::Element *> labels_;
    std::vector<Rml::Element *> gensMarks_;
    std::vector<Rml::Element *> castleMarks_;
    std::vector<Rml::Element *> guildNames_;
    std::vector<Rml::Element *> playerIds_;
    std::vector<Rml::Element *> storeTags_;
    std::vector<Rml::Element *> storeNames_;
    std::vector<Rml::Element *> storeTopTitles_;
    std::vector<Rml::Element *> storeBottomTitles_;
    std::vector<Rml::Element *> chatTexts_;
    RmlPlayerNameRequest active_;
    RmlPlayerNameRequest pending_;
    float activeScale_ = 0.0F;
    bool created_ = false;
    bool hasPending_ = false;
};

RmlPlayerNameLayer::RmlPlayerNameLayer(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}

RmlPlayerNameLayer::~RmlPlayerNameLayer() = default;

int RmlPlayerNameLayer::StoreWidth()
{
    return StoreDesign().Number<int>(StoreMetric::Width);
}
float RmlPlayerNameLayer::StoreLeft()
{
    return StoreDesign().Number(StoreMetric::Left);
}
float RmlPlayerNameLayer::StoreTop()
{
    return StoreDesign().Number(StoreMetric::Top);
}
float RmlPlayerNameLayer::WorldRaise()
{
    return StoreDesign().Number(StoreMetric::WorldRaise);
}
float RmlPlayerNameLayer::StoreHighlightPeriod()
{
    return StoreDesign().Number(StoreMetric::HighlightPeriod);
}
int RmlPlayerNameLayer::StoreRightMargin()
{
    return StoreDesign().Number<int>(StoreMetric::RightMargin);
}

int RmlPlayerNameLayer::StoreHeightFor(const RmlPlayerName &label) noexcept
{
    return StoreDesign().Number<int>(label.storeTitleBottom[0] != L'\0'
                                         ? StoreMetric::TwoTitleHeight
                                         : StoreMetric::OneTitleHeight);
}

void RmlPlayerNameLayer::Stage(const RmlPlayerNameRequest &request)
{
    impl_->Stage(request);
}

bool RmlPlayerNameLayer::PrepareOnWorker(int viewportWidth, int viewportHeight)
{
    return impl_->Prepare(viewportWidth, viewportHeight);
}

bool RmlPlayerNameLayer::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}
} // namespace UI::Modern

// Desc: implementation of the CCharInfoBalloonMng class.
// producer: Ahn Sang-Kyu

// 함수 이름 : Create()
// 함수 설명 : 캐릭터 정보 풍선 매니저 생성.
//			   (캐릭터 선택씬에서 쓰임. 풍선 5개 생성.)

// 함수 이름 : Render()
// 함수 설명 : 캐릭터 정보 풍선들 렌더.
void CCharInfoBalloonMng::Render()
{
    if (!m_isInitialized)
        return;

    for (auto &balloon : m_charInfoBalloons)
        balloon.Render();
}

// MainScene.cpp - Main game scene implementation

void SessionRenderUnit::RenderMainSceneWorldUi()
{
    BeginBitmap();
    auto *map = g_pNewUISystem->GetUI_pNewUIMiniMap();
    if (map && map->IsVisible())
        map->Render();
    RenderObjectDescription();

    RenderInterface(true);
    RenderTournamentInterface();
    EndBitmap();
}

/**
 * @brief Renders UI elements and overlays for the main scene.
 */
void SessionRenderUnit::RenderMainSceneUI()
{
    if (!BeginRenderTapePass(RenderTapePass::UserInterface))
        return;
    RenderMainSceneWorldUi();

    g_pPartyManager->Render();
    BeginBitmap();
    RenderInfomation();

#ifdef ENABLE_EDIT
    RenderDebugWindow();
#endif //ENABLE_EDIT

    EndBitmap();
    BeginBitmap();

    if (auto *manager = g_pNewUISystem->GetNewUIManager())
    {
        (void)manager->Render();
    }

    EndBitmap();
    BeginBitmap();

    RenderCursor();

    EndBitmap();
}

// OMF-01785
// OMF-01786
// OMF-01787
// OMF-01788
// OMF-01789
// OMF-01790
// OMF-01793
void SessionLegacyCalls::RenderMainSceneUI()
{
    return sessionKeeper_.Renderer()->RenderMainSceneUI();
} // OMF-01794
// OMF-01795

using namespace SEASON3B;

void CNewUIMiniMap::SetPos(int, int)
{
}

void CNewUIMiniMap::StageMarkers(bool occupied)
{
    if (markerFilter_ == occupied)
        return;
    markers_.clear();
    for (const auto &marker : m_Mini_Map_Data)
    {
        if (!marker.Kind)
            break;
        if (occupied && marker.Kind == 1 &&
            !(marker.Location[0] == 228 && marker.Location[1] == 48))
            continue;
        WorldMinimapData::Marker target{static_cast<std::uint8_t>(marker.Kind),
                                        {marker.Location[0], marker.Location[1]},
                                        marker.Rotation};
        std::copy_n(marker.Name, target.name.size(), target.name.begin());
        markers_.push_back(target);
    }
    markerFilter_ = occupied;
    ++markerRevision_;
}

bool CNewUIMiniMap::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, image_ ? &*image_ : nullptr, heroX_,
                                  heroY_, heroHeading_, markers_, markerRevision_,
                                  imageOriginPixels_);
}

bool CNewUIMiniMap::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}

void CNewUIMiniMap::UnloadImages()
{
    panel_.Release();
    visible_ = m_bSuccess = false;
    image_.reset();
    markers_.clear();
    markerFilter_.reset();
    memset(m_Mini_Map_Data, 0, sizeof(m_Mini_Map_Data));
    memset(m_Btn_Loc, 0, sizeof(m_Btn_Loc));
    DeleteBitmap(IMAGE_MINIMAP_INTERFACE);
}

void CUIMapName::Render()
{
    panel_.Record(sessionKeeper_.Renderer()->LegacyRender());
}
bool CUIMapName::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, alpha_, text_);
}

constexpr int GROUND_ITEM_LABEL_BUILD_BUDGET_PER_FRAME = 32;

// Construction/Destruction

void SEASON3B::CNewUINameWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool SEASON3B::CNewUINameWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderName();
    RenderTimes();
    RenderBooleans();
    monsterInfo_.Record(renderer_.LegacyRender());
    DrawPersonalShopTitleImp();
    DisableAlphaBlend();
    return true;
}

void SEASON3B::CNewUINameWindow::RenderName()
{
#ifndef GUILD_WAR_EVENT
    if (gMapManager.InChaosCastle() == true && (SelectedNpc != -1 || SelectedCharacter != -1))
    {
        return;
    }
#endif //GUILD_WAR_EVENT

    if (SelectedItem != -1 || SelectedNpc != -1 || SelectedCharacter != -1)
    {
        if (SelectedCharacter == -1 && SelectedItem != -1)
        {
            RenderItemName(SelectedItem, &Items[SelectedItem].Object, &Items[SelectedItem].Item,
                           false);
        }
    }

    if (m_bShowItemName || IsRepeat(VK_MENU))
    {
        SetGroundItemLabelBuildBudget(GROUND_ITEM_LABEL_BUILD_BUDGET_PER_FRAME);

        bool renderLabels = true;

        if (renderLabels)
        {
            for (int i = 0; i < MAX_ITEMS; i++)
            {
                OBJECT *o = &Items[i].Object;
                if (o->Live)
                {
                    if (o->Visible && i != SelectedItem)
                    {
                        RenderItemName(i, o, &Items[i].Item, true);
                    }
                }
            }
        }
    }
}

void SEASON3B::CNewUINameWindow::StageMonsterInfo()
{
    monsters_.clear();
#ifndef GUILD_WAR_EVENT
    const bool hideTarget =
        gMapManager.InChaosCastle() && (SelectedNpc != -1 || SelectedCharacter != -1);
#else
    const bool hideTarget = false;
#endif
    if (!hideTarget && SelectedCharacter != -1)
    {
        const auto &character = CharactersClient[SelectedCharacter];
        if (character.Object.Kind == KIND_MONSTER)
        {
            auto &info = monsters_.emplace_back();
            std::wcsncpy(info.name.data(), character.ID, info.name.size() - 1);
            const auto anchor = monsterInfo_.TargetAnchor();
            info.x = static_cast<int>(anchor[0]);
            info.y = static_cast<int>(anchor[1]);
            info.health = character.HealthStatus;
            info.showName = true;
            info.showHealth = character.HealthStatus > 0;
        }
    }
    if (m_bShowMonsterHealthBar)
        StageOverheadMonsters();
}
void SEASON3B::CNewUINameWindow::StageOverheadMonsters()
{
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        const auto &character = CharactersClient[i];
        const auto &object = character.Object;
        if (!object.Live || !object.Visible || object.Alpha <= 0 || character.Dead > 0 ||
            object.Kind != KIND_MONSTER)
            continue;
        vec3_t position, cameraPosition;
        Vector(object.Position[0], object.Position[1],
               object.Position[2] + object.BoundingBoxMax[2] + monsterInfo_.WorldRaise(), position);
        VectorTransform(position, g_Camera.Matrix, cameraPosition);
        if (cameraPosition[2] >= 0)
            continue;
        auto &info = monsters_.emplace_back();
        cameraProjection_.WorldToScreen(g_Camera, position, &info.x, &info.y);
        info.health = character.HealthStatus;
    }
}
bool SEASON3B::CNewUINameWindow::PrepareModernUiOnWorker(int width, int height)
{
    return monsterInfo_.PrepareOnWorker(width, height, monsters_);
}

// Includes mirror ZzzInterface.cpp, the unit these were extracted from.

// ※

void SessionRenderUnit::StagePlayerNames(UI::Modern::RmlPlayerNameRequest &request)
{
    std::sort(request.labels.begin(), request.labels.end(),
              [](const UI::Modern::RmlPlayerName &left, const UI::Modern::RmlPlayerName &right) {
                  return left.characterKey < right.characterKey;
              });
    const std::size_t storeCount = request.labels.size();
    request.labels.reserve(storeCount + ChatTextDetail::MAX_CHAT);

    const auto appendPlayerText = [this](UI::Modern::RmlPlayerName &label,
                                         const ChatTextDetail::CHAT &chat) {
        if (chat.Owner->GuildMarkIndex >= 0)
        {
            const auto &mark = GuildMark[chat.Owner->GuildMarkIndex];
            if (mark.UnionName[0] != L'\0')
            {
                std::swprintf(label.guildName.data(), label.guildName.size(), L"< %ls - %ls >",
                              mark.UnionName, mark.GuildName);
            }
            else if (mark.GuildName[0] != L'\0')
            {
                std::swprintf(label.guildName.data(), label.guildName.size(), L"< %ls >",
                              mark.GuildName);
            }
        }
        if (chat.LifeTime[0] > 0)
        {
            std::swprintf(label.chat.data(), label.chat.size(), L"%ls: %ls", chat.ID, chat.Text[0]);
        }
    };

    for (const ChatTextDetail::CHAT &chat : Chat)
    {
        if ((chat.IDLifeTime <= 0 && chat.LifeTime[0] <= 0) || chat.Owner == nullptr ||
            chat.Owner->Object.Kind != KIND_PLAYER)
        {
            continue;
        }

        const auto store =
            std::lower_bound(request.labels.begin(), request.labels.begin() + storeCount,
                             chat.Owner->Key, [](const UI::Modern::RmlPlayerName &label, int key) {
                                 return label.characterKey < key;
                             });
        if (store != request.labels.begin() + storeCount && store->characterKey == chat.Owner->Key)
        {
            appendPlayerText(*store, chat);
            continue;
        }

        OBJECT &object = chat.Owner->Object;
        if (!object.Live || !object.Visible)
        {
            continue;
        }

        UI::Modern::RmlPlayerName label;
        label.characterKey = chat.Owner->Key;
        std::wcsncpy(label.name.data(), chat.ID, label.name.size() - 1);
        appendPlayerText(label, chat);

        vec3_t position;
        Vector(object.Position[0], object.Position[1],
               object.Position[2] + object.BoundingBoxMax[2] +
                   UI::Modern::RmlPlayerNameLayer::WorldRaise(),
               position);
        int screenX = 0;
        int screenY = 0;
        cameraProjection_.WorldToScreen(g_Camera, position, &screenX, &screenY);
        label.anchorX =
            screenX * static_cast<int>(ModernUiLogicalViewportWidth()) / REFERENCE_WIDTH;
        label.anchorY =
            screenY * static_cast<int>(ModernUiLogicalViewportHeight()) / REFERENCE_HEIGHT;

        if (chat.Owner == Hero)
        {
            label.guildTone = UI::Modern::RmlGuildNameTone::Friendly;
            label.tone = UI::Modern::RmlPlayerNameTone::Hero;
        }
        else
        {
            const BYTE relationship = chat.Owner->GuildMarkIndex == Hero->GuildMarkIndex
                                          ? GR_UNION
                                          : chat.Owner->GuildRelationShip;
            label.guildTone = relationship == GR_NONE
                                  ? UI::Modern::RmlGuildNameTone::Neutral
                                  : (relationship == GR_RIVAL || relationship == GR_RIVALUNION
                                         ? UI::Modern::RmlGuildNameTone::Hostile
                                         : UI::Modern::RmlGuildNameTone::Friendly);
            switch (chat.Color)
            {
            case PVP_CAUTION:
                label.tone = UI::Modern::RmlPlayerNameTone::Caution;
                break;
            case PVP_MURDERER1:
                label.tone = UI::Modern::RmlPlayerNameTone::Murderer1;
                break;
            case PVP_MURDERER2:
                label.tone = UI::Modern::RmlPlayerNameTone::Murderer2;
                break;
            default:
                break;
            }
        }

        if (g_isCharacterBuff((&object), eBuff_GMEffect) ||
            chat.Owner->CtlCode == CTLCODE_20OPERATOR || chat.Owner->CtlCode == CTLCODE_08OPERATOR)
        {
            label.tone = UI::Modern::RmlPlayerNameTone::GameMaster;
        }
        if (chat.Owner->m_byGensInfluence == 1 || chat.Owner->m_byGensInfluence == 2)
        {
            const int rank = chat.Owner->GensRanking >= 1 && chat.Owner->GensRanking <= 14
                                 ? chat.Owner->GensRanking
                                 : 14;
            label.gensFrame = rank + (chat.Owner->m_byGensInfluence == 2 ? 14 : 0);
        }
        if (IsBattleCastleStart() && chat.Owner->EtcPart >= PARTS_ATTACK_TEAM_MARK &&
            chat.Owner->EtcPart <= PARTS_DEFENSE_KING_TEAM_MARK)
        {
            label.castleFrame = chat.Owner->EtcPart - PARTS_ATTACK_TEAM_MARK + 1;
        }
        request.labels.push_back(label);
    }

    request.visible = !request.labels.empty();
    playerNameLayer_.Stage(request);
}

CCharInfoBalloon::CCharInfoBalloon(SessionKeeper &keeper)
    : CSprite(keeper), cameraProjection_(keeper.CameraProjectionObject()), m_pCharInfo(nullptr)
{
    I18N::RegisterLocaleObserver(&CCharInfoBalloon::OnLocaleChanged, this);
}

CCharInfoBalloon::~CCharInfoBalloon()
{
    I18N::UnregisterLocaleObserver(&CCharInfoBalloon::OnLocaleChanged, this);
}

void CCharInfoBalloon::OnLocaleChanged(void *ctx) noexcept
{
    auto *self = static_cast<CCharInfoBalloon *>(ctx);
    if (self->m_pCharInfo != nullptr)
    {
        self->SetInfo();
    }
}

void CCharInfoBalloon::Create(CHARACTER *pCharInfo)
{
    CSprite::Create(118, 54, BITMAP_LOG_IN + 7, 0, nullptr, 59, 54);

    m_pCharInfo = pCharInfo;
    m_dwNameColor = 0;
    std::fill(std::begin(m_szName), std::end(m_szName), L'\0');
    std::fill(std::begin(m_szGuild), std::end(m_szGuild), L'\0');
    std::fill(std::begin(m_szClass), std::end(m_szClass), L'\0');
}

void CCharInfoBalloon::Render()
{
    if (m_pCharInfo == nullptr || !CSprite::m_bShow)
        return;

    CSprite::Render();

    vec3_t afPos;
    VectorCopy(m_pCharInfo->Object.Position, afPos);
    afPos[2] += 350.0f;

    int nPosX, nPosY;
    cameraProjection_.WorldToScreen(g_Camera, afPos, &nPosX, &nPosY);

    CSprite::SetPosition(int(nPosX * g_fScreenRate_x), int(nPosY * g_fScreenRate_y));

    g_RenderText.SetFont(LegacyFontRole::Fixed);
    g_RenderText.SetBgColor(0);

    const int spriteX = CSprite::GetXPos();
    const int spriteY = CSprite::GetYPos();
    const int spriteW = CSprite::GetWidth();

    const int nTextPosX = int(spriteX / g_fScreenRate_x);

    g_RenderText.SetTextColor(m_dwNameColor);
    g_RenderText.RenderText(nTextPosX, int((spriteY + 6) / g_fScreenRate_y), m_szName,
                            spriteW / g_fScreenRate_x, 0, RT3_SORT_CENTER);

    g_RenderText.SetTextColor(CLRDW_WHITE);
    g_RenderText.RenderText(nTextPosX, int((spriteY + 22) / g_fScreenRate_y), m_szGuild,
                            spriteW / g_fScreenRate_x, 0, RT3_SORT_CENTER);

    g_RenderText.SetTextColor(CLRDW_BR_ORANGE);
    g_RenderText.RenderText(nTextPosX, int((spriteY + 38) / g_fScreenRate_y), m_szClass,
                            spriteW / g_fScreenRate_x, 0, RT3_SORT_CENTER);
}

void CCharInfoBalloon::SetInfo()
{
    if (m_pCharInfo == nullptr)
        return;

    if (!m_pCharInfo->Object.Live)
    {
        CSprite::m_bShow = false;
        return;
    }

    CSprite::m_bShow = true;

    m_dwNameColor = CharacterLabelDetail::ResolveNameColor(m_pCharInfo->CtlCode);

    CharacterLabelDetail::CopyWideString(m_szName, m_pCharInfo->ID);

    const int guildTextIndex =
        CharacterLabelDetail::ResolveGuildTextIndex(m_pCharInfo->GuildStatus);
    mu_swprintf_s(m_szGuild, L"(%ls)", I18N::Game::Lookup(guildTextIndex));
    mu_swprintf_s(m_szClass, L"%ls %d", gCharacterManager.GetCharacterClassText(m_pCharInfo->Class),
                  m_pCharInfo->Level);
}
