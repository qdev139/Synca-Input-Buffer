// Synca menu: the in-game settings window (tabs, pages, Studio tools). It talks to the engine only
// through the entry points declared in synca.hpp, so it compiles on its own.
#include "synca.hpp"

#if __has_include(<Geode/ui/Scrollbar.hpp>)
#include <Geode/ui/Scrollbar.hpp>
#define SYNCA_HAS_SCROLLBAR 1
#else
#define SYNCA_HAS_SCROLLBAR 0
#endif

namespace synca {

// v2.44 in-game menu: pause menu button or F7 in a level. Stage, the main switches,
// status, and shortcuts to all settings / copy log / forget level.
// Settings that live on the focused tabs, for the search on the All tab.
constexpr char const* JUMP_LABELS[] = {
    "Cube delay",
    "Ship delay",
    "Ball delay",
    "UFO delay",
    "Wave delay",
    "Robot delay",
    "Spider delay",
    "Swing delay",
    "Safe / balanced / aggressive presets",
    "Yellow orb ticks",
    "Pink orb ticks",
    "Blue orb ticks",
    "Red orb ticks",
    "Black orb ticks",
    "Green orb ticks",
    "Spider orb ticks",
    "Dash orb ticks",
    "Link orb values",
    "Wave release window",
    "Robot release window",
    "Dash release window",
    "Keybind: buffer on / off",
    "Keybind: panic",
    "Keybind: change stage",
    "Keybind: open menu",
    "Keybind: path view",
    "Keybind: console",
    "Keybind: copy log",
    "Keybind: forget level",
    "Keybind: wave direct input",
    "Path view / predicted route",
    "Path horizon",
    "Alternate assist",
    "USKC wave preset",
    "Max correction",
    "Rhythm samples",
    "Timing tolerance",
    "Safety margin",
    "Burst gap cutoff",
    "Wave look",
    "Minimum gain",
    "Alternate path length",
    "Path refresh",
    "Gap centering (straight fly, 1-block gaps)",
    "Progress stage bands",
    "Auto switch by level percent",
    "Colour theme",
    "HUD position / layout",
    "Presets (save / load)",
    "Click bias early late",
    "Orb margin",
    "Orb confidence dot",
    "Swing look-ahead",
    "Adaptive buffer",
    "Performance guard"
};
constexpr int JUMP_TABS[] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8 };
constexpr int JUMP_COUNT = static_cast<int>(sizeof(JUMP_TABS) / sizeof(JUMP_TABS[0]));

class BufferMenu : public CCLayer {
    inline static BufferMenu* s_instance = nullptr;
    struct Option {
        char const* key;    // mod setting, or nullptr for the runtime path view
        char const* label;
        int tab;
    };
    static constexpr Option OPTIONS[] = {
        { "low-latency", "Low latency", 0 },
        { "robust-saves", "Robust saves", 0 },
        { "ship-buffer", "Ship buffering", 0 },
        { "cbf-sync", "CBF sync", 0 },
        { "show-pause-button", "Pause menu button", 0 },
        { "smart-prediction", "Orb / portal prediction", 0 },
        { "orb-align", "Orb alignment", 1 },
        { "orb-timing", "Orb timing", 1 },
        { "level-memory", "Level memory", 1 },
        { "more-time-death-spots", "More time at deaths", 1 },
        { "ufo-smart", "Smart UFO", 1 },
        { "ball-smart", "Smart ball", 1 },
        { "swing-smart", "Smart swing", 1 },
        { "avoid-moving-objects", "Avoid moving hazards", 1 },
        { "predict-moving-objects", "Predict moving hazards", 1 },
        { "training-mode", "Training hints", 1 },
        { "auto-click", "Auto-click (assist)", 1 },
        { "wave-auto-release", "Wave auto-release", 1 },
        { "wave-safety-margin", "Wave safety margin", 1 },
        { "robot-smart", "Smart robot jumps", 1 },
        { "spider-smart", "Smart spider flips", 1 },
        { "robot-auto-release", "Robot auto-release", 1 },
        { "dash-auto-release", "Dash auto-release", 1 },
        { "auto-release-normal-runs", "Auto input in normal runs", 1 },
        { "death-heatmap", "Death heatmap", 2 },
        { "save-ding", "Ding on save", 2 },
        { "hide-watermark", "Hide watermark", 2 },
        { "performance-mode", "Performance mode", 2 },
        { "explain-inputs", "Explain inputs (debug)", 2 },
    };

    // Plum, rose and soft ivory keep the interface legible without fighting the game behind it.
    struct Theme {
        ccColor3B main, soft, accent, bg;
    };

public:
    static Theme currentTheme() {
        return { themeColor({ 176, 78, 244 }), themeColor({ 244, 224, 255 }), themeColor({ 255, 157, 220 }), themeColor({ 14, 6, 21 }) };
    }

private:
    Theme m_t = currentTheme();

    CCNode* m_mainLayer = nullptr;
    CCNode* m_sidebarLayer = nullptr;
    CCNode* m_configLayer = nullptr;
    CCMenu* m_buttonMenu = nullptr;
    CCMenu* m_configButtonMenu = nullptr;
    CCLabelBMFont* m_stage = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_cfgStage = nullptr;
    CCLabelBMFont* m_cfgTiming = nullptr;
    CCLabelBMFont* m_cfgTps = nullptr;
    CCLabelBMFont* m_cfgTelemetry = nullptr;
    CCLabelBMFont* m_tpsLabel = nullptr;
    CCNode* m_tpsCard = nullptr;

    static constexpr int TPS_MIN = 240;
    static constexpr int TPS_MAX = 1000;
    std::vector<CCLabelBMFont*> m_labels;
    std::vector<CCMenuItemSpriteExtra*> m_toggles;
    std::vector<int> m_rowOptionIndices;
    std::vector<CCNode*> m_rows;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;
    std::vector<CCNode*> m_tabHighlights;
    std::vector<CCLabelBMFont*> m_tabLabels;
    std::vector<CCSprite*> m_tabIcons;
    ScrollLayer* m_tabScroll = nullptr;     // the list of tabs scrolls
    CCMenu* m_tabMenu = nullptr;
    CCNode* m_tabScrollbar = nullptr;
    CCNode* m_stdScrollbar = nullptr;
    CCNode* m_customScrollbar = nullptr;
    std::function<CCNode*(ScrollLayer*, CCNode*, float, float)> m_makeScrollbar;
    ScrollLayer* m_scroll = nullptr;
    ScrollLayer* m_customScroll = nullptr;
    geode::TextInput* m_allSearchInput = nullptr;
    std::string m_allSearchQuery;
    CCNode* m_customPage = nullptr;
    CCMenu* m_customMenu = nullptr;
    std::vector<geode::SliderNode*> m_customSliders;
    std::vector<geode::TextInput*> m_customInputs;
    std::vector<CCMenuItemSpriteExtra*> m_specialToggles;
    std::vector<CCLabelBMFont*> m_bandLabels;   // per band: title, stage name
    std::vector<geode::TextInput*> m_bandInputs; // per band: from % box, to % box
    bool m_syncingInputs = false;               // true while the menu itself fills a number box
    std::string m_keyWarning;                   // shown under the key list when a key is used twice
    std::vector<CCLabelBMFont*> m_studioLabels; // Studio tab: theme, HUD values, preset status, click bias
    std::vector<CCLabelBMFont*> m_specialValues;
    std::vector<int> m_specialValueTags;
    CCLabelBMFont* m_configTitle = nullptr;
    CCLabelBMFont* m_helpBody = nullptr;
    CCLabelBMFont* m_helpTitle = nullptr;
    CCLabelBMFont* m_helpText = nullptr;
    CCLayerColor* m_helpOverlay = nullptr;
    CCMenu* m_helpMenu = nullptr;
    CCLabelBMFont* m_captureHint = nullptr;
    std::vector<CCLabelBMFont*> m_keyLabels;
    CCMenuItemSpriteExtra* m_tpsMinus = nullptr;
    CCMenuItemSpriteExtra* m_tpsPlus = nullptr;
    geode::SliderNode* m_tpsSlider = nullptr;
    geode::TextInput* m_tpsInput = nullptr;
    float m_scaleX = 1.f;
    float m_scaleY = 1.f;
    CCPoint m_openPosition = CCPointZero;
    CCPoint m_configOpenPosition = CCPointZero;
    float m_leftScale = 1.f;    // size of the left panel (the corner handle drags it)
    float m_rightScale = 1.f;   // size of the right panel
    CCPoint m_sidebarHome = CCPointZero;
    CCNode* m_leftHandle = nullptr;
    CCNode* m_rightHandle = nullptr;
    int m_dragging = 0;         // 1 = left handle, 2 = right handle
    CCPoint m_dragFrom = CCPointZero;
    CCDrawNode* m_graphNode = nullptr;
    CCLabelBMFont* m_graphInfo = nullptr;
    CCLabelBMFont* m_graphForecast = nullptr;
    CCNode* m_calibBeat = nullptr;
    CCLabelBMFont* m_calibText = nullptr;
    bool m_calibOn = false;
    std::vector<double> m_calibOffsets;
    std::chrono::steady_clock::time_point m_calibStart {};
    int m_calibBeatPlayed = 0;
    CCDrawNode* m_calibDraw = nullptr;
    double m_calibMs = 0.0;
    bool m_calibDone = false;
    float m_dragScale = 1.f;
    bool m_closing = false;
    bool m_helpOpen = false;
    bool m_syncingSlider = false;
    int m_tab = 0;
    int m_captureKeyIndex = -1;

    static void pulse(CCNode* node, float scale) {
        node->runAction(CCRepeatForever::create(CCSequence::create(CCEaseSineInOut::create(CCScaleTo::create(1.25f, scale * 1.05f)),
            CCEaseSineInOut::create(CCScaleTo::create(1.25f, scale)), nullptr)));
    }

    // A new page: the content slides in from the right a few points and settles.
    static void slideIn(CCNode* node, CCPoint base) {
        if (!node) return;
        node->stopAllActions();
        if (studio.fxLevel < 1) {
            node->setPosition(base);
            return;
        }
        node->setPosition({ base.x + 22.f, base.y });
        node->runAction(CCEaseOut::create(CCMoveTo::create(0.24f, base), 2.5f));
    }

    static void disableMenuItemAnimation(CCMenuItemSpriteExtra* item) {
        if (!item) return;
        item->m_animationEnabled = false;
        item->m_scaleMultiplier = 1.f;
        item->setScale(1.f);
    }

    CCPoint layoutPoint(float x, float yFromTop) const {
        return { x, MENU_HEIGHT - yFromTop };
    }

    CCPoint customPoint(float x, float yFromTop) const {
        return { x - CONFIG_PANEL_X + SETTINGS_W / 2.f, MENU_HEIGHT - yFromTop };
    }

    CCNode* roundedPanel(float width, float height, ccColor4B color, float radius = -1.f) {
        return gelPanel(width, height, color, radius);
    }

    CCNode* toggleVisual(bool value, float width, float height) {
        auto* body = roundedPanel(width, height, value ? ccc4(176, 64, 240, 255) : ccc4(38, 26, 52, 255), 9.f);
        if (!body) return nullptr;
        if (value && studio.fxLevel >= 1) {
            // a soft glow ring around an ON switch (it breathes on FULL)
            auto* glow = CCDrawNode::create();
            glow->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
            glow->setContentSize({ width, height });
            glow->ignoreAnchorPointForPosition(false);
            glow->setAnchorPoint({ 0.5f, 0.5f });
            glow->setPosition({ width / 2.f, height / 2.f });
            ccColor3B glowC = themeColor({ 226, 128, 255 });
            for (int i = 3; i >= 1; --i) {
                auto ring = gelShape(-1.6f * i, -1.6f * i, width + 3.2f * i, height + 3.2f * i, 9.f + 1.6f * i, 9.f + 1.6f * i);
                glow->drawPolygon(ring.data(), static_cast<unsigned int>(ring.size()), { 0.f, 0.f, 0.f, 0.f }, 1.3f, neonF(glowC, 0.26f - 0.07f * i));
            }
            body->addChild(glow, -1);
            if (studio.fxLevel >= 2) {
                glow->runAction(CCRepeatForever::create(CCSequence::create(CCEaseSineInOut::create(CCScaleTo::create(0.95f, 1.05f)),
                    CCEaseSineInOut::create(CCScaleTo::create(0.95f, 1.f)), nullptr)));
            }
        }
        auto* label = CCLabelBMFont::create(value ? "ON" : "OFF", "bigFont.fnt");
        label->limitLabelWidth(width - 8.f, 0.42f, 0.32f);
        label->setColor(value ? ccColor3B { 255, 246, 255 } : ccColor3B { 176, 156, 196 });
        label->setPosition({ width / 2.f, height / 2.f });
        body->addChild(label);
        return body;
    }

    CCMenuItemSpriteExtra* toggleItem(int tag, SEL_MenuHandler handler, bool value,
        float width = 68.f, float height = 34.f) {
        auto* item = CCMenuItemSpriteExtra::create(toggleVisual(value, width, height),
            toggleVisual(value, width, height), this, handler);
        disableMenuItemAnimation(item);
        item->setTag(tag);
        item->setContentSize({ width, height });
        return item;
    }

    CCMenuItemSpriteExtra* actionItem(char const* text, SEL_MenuHandler handler, int tag,
        float width, float height, ccColor4B fill = ccc4(70, 31, 61, 255)) {
        auto makeSprite = [&](ccColor4B color) -> CCNode* {
            auto* body = roundedPanel(width, height, color);
            if (!body) return nullptr;
            auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
            label->limitLabelWidth(width - 14.f, 0.58f, 0.42f);
            label->setColor({ 255, 249, 255 });
            label->setPosition({ width / 2.f, height / 2.f });
            body->addChild(label);
            return body;
        };
        auto* item = CCMenuItemSpriteExtra::create(makeSprite(fill),
            makeSprite(ccc4(std::min<int>(fill.r + 48, 255), std::min<int>(fill.g + 34, 255),
                std::min<int>(fill.b + 48, 255), 255)), this, handler);
        disableMenuItemAnimation(item);
        item->setTag(tag);
        item->setContentSize({ width, height });
        return item;
    }

    // the glowing fill of a slider: a purple bar with a halo from the left end to the handle, drawn here because the library's own bar
    // image was hidden behind the groove
    struct GlowFill : CCDrawNode {
        geode::SliderNode* slider = nullptr;
        static GlowFill* make(geode::SliderNode* owner) {
            auto* ret = new GlowFill();
            if (ret->init()) {
                ret->autorelease();
                ret->slider = owner;
                ret->scheduleUpdate();
                return ret;
            }
            delete ret;
            return nullptr;
        }
        void update(float) override {
            this->clear();
            if (!slider || !slider->isVisible()) return;
            auto* thumb = slider->getThumb();
            float x1 = thumb->getPositionX();
            float y = thumb->getPositionY();
            float x0 = 7.f;
            if (x1 <= x0 + 1.f) return;
            this->drawSegment({ x0, y }, { x1, y }, 8.5f, neonPremul({ 0.7f, 0.36f, 1.f, 0.10f }));
            this->drawSegment({ x0, y }, { x1, y }, 6.f, neonPremul({ 0.72f, 0.4f, 1.f, 0.22f }));
            this->drawSegment({ x0, y }, { x1, y }, 3.4f, neonPremul({ 0.82f, 0.52f, 1.f, 1.f }));
            this->drawSegment({ x0, y + 0.8f }, { x1, y + 0.8f }, 1.1f, neonPremul({ 1.f, 1.f, 1.f, 0.55f }));
        }
    };

    geode::SliderNode* styledSlider(float width, float min, float max, float value,
        geode::SliderNode::SliderCallback callback = {}) {
        auto* groove = geode::NineSlice::create("synca-slider-groove.png"_spr, {}, { 2.f, 10.f, 2.f, 10.f });
        auto* slider = geode::SliderNode::createCustom(
            CCSprite::create("synca-slider-thumb.png"_spr),
            CCSprite::create("synca-slider-thumb-selected.png"_spr), groove,
            "synca-slider-fill.png"_spr, std::move(callback), { 0.f, 4.f });
        slider->setMin(min);
        slider->setMax(max);
        slider->setSnapStep(1.f);
        slider->setContentSize({ width, 24.f });
        slider->getThumb()->setScale(1.45f);
        slider->getGroove()->setColor({ 86, 52, 128 });
        slider->getBar()->setVisible(false);
        slider->getGroove()->setOpacity(215);
        slider->getGroove()->setZOrder(-10);
        slider->getThumb()->setZOrder(20);
        if (auto* fill = GlowFill::make(slider)) {
            slider->addChild(fill, 1);   // between the groove and the handle
        }
        slider->getThumb()->setColor({ 255, 255, 255 });   // white-hot line, like the scrollbar thumb
        slider->setTouchPriority(kCCMenuHandlerPriority - 1);
        slider->setValue(std::clamp(value, min, max));
        return slider;
    }

    geode::TextInput* numberInput(float width, char const* placeholder, size_t maxChars = 3) {
        auto* input = geode::TextInput::create(width, placeholder, "bigFont.fnt");
        input->setCommonFilter(geode::CommonFilter::Uint);
        input->setMaxCharCount(maxChars);
        input->setTextAlign(geode::TextInputAlign::Center);
        input->setScale(0.96f);
        // Same look as the key plates on the Keybinds page: a dark rounded plate with light cyan text.
        input->getBGSprite()->setVisible(false);
        auto inputSize = input->getContentSize();
        if (auto* plate = gelPanel(inputSize.width, std::max(25.f, inputSize.height - 4.f), ccc4(24, 14, 36, 255), 8.f, false)) {
            plate->setPosition({ inputSize.width / 2.f, inputSize.height / 2.f });
            input->addChild(plate, -1);
        }
        input->getInputNode()->setLabelNormalColor({ 178, 233, 244 });
        input->getInputNode()->setLabelPlaceholderColor({ 100, 130, 150 });
        return input;
    }

    void colorLabels() {
        for (size_t i = 0; i < m_labels.size() && i < m_rowOptionIndices.size(); ++i) {
            if (!m_labels[i]) continue;
            int option = m_rowOptionIndices[i];
            if (option < 0 || option >= static_cast<int>(std::size(OPTIONS))) continue;
            m_labels[i]->setColor(optionValue(OPTIONS[option]) ? m_t.soft : ccColor3B { 151, 174, 188 });
        }
    }

    void buildOptionRows(int tab) {
        if (!m_scroll) return;
        constexpr float SCROLL_W = 292.f;
        constexpr float ROW_H = 52.f;
        auto normalized = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        };
        std::string query = m_searchMode ? normalized(m_allSearchQuery) : std::string{};
        m_tpsCard = nullptr;
        m_tpsLabel = nullptr;
        m_tpsSlider = nullptr;
        m_tpsInput = nullptr;
        m_scroll->m_contentLayer->removeAllChildrenWithCleanup(true);
        m_labels.clear();
        m_toggles.clear();
        m_rows.clear();
        m_rowOptionIndices.clear();
        for (int i = 0; i < static_cast<int>(std::size(OPTIONS)); ++i) {
            if (!m_searchMode && OPTIONS[i].tab != tab) continue;
            if (!query.empty() && normalized(std::string(OPTIONS[i].label)).find(query) == std::string::npos &&
                (!OPTIONS[i].key || normalized(std::string(OPTIONS[i].key)).find(query) == std::string::npos)) {
                continue;
            }
            auto* row = CCNode::create();
            row->setContentSize({ SCROLL_W - 14.f, ROW_H });
            auto* rowBg = gelPanel(SCROLL_W - 14.f, ROW_H - 4.f, ccc4(52, 24, 53, 250), 11.f);
            row->addChildAtPosition(rowBg, Anchor::Center);
            auto* label = CCLabelBMFont::create(OPTIONS[i].label, "bigFont.fnt");
            label->setAnchorPoint({ 0.f, 0.5f });
            label->limitLabelWidth(SCROLL_W - 118.f, 0.62f, 0.34f);
            label->setColor(optionValue(OPTIONS[i]) ? m_t.soft : ccColor3B { 151, 174, 188 });
            row->addChildAtPosition(label, Anchor::Left, { 13.f, 0.f });
            auto* rowMenu = CCMenu::create();
            rowMenu->setContentSize(row->getContentSize());
            rowMenu->ignoreAnchorPointForPosition(false);
            rowMenu->setAnchorPoint({ 0.f, 0.f });
            rowMenu->setPosition({ 0.f, 0.f });
            auto* toggler = toggleItem(i, menu_selector(BufferMenu::onToggle), optionValue(OPTIONS[i]), 70.f, 34.f);
            toggler->setPosition({ SCROLL_W - 14.f - 14.f - 35.f, ROW_H / 2.f });
            rowMenu->addChild(toggler);
            row->addChild(rowMenu);
            m_scroll->m_contentLayer->addChild(row);
            m_rows.push_back(row);
            m_labels.push_back(label);
            m_toggles.push_back(toggler);
            m_rowOptionIndices.push_back(i);
        }

        if (!query.empty()) {
            // Settings that live on the focused tabs: tap the button to jump straight there.
            static constexpr char const* TAB_LABELS[] = { "Basic", "Smart", "Visuals", "Modes", "Orbs", "Keys", "Graph", "Path", "Studio" };
            for (int jumpIndex = 0; jumpIndex < JUMP_COUNT; ++jumpIndex) {
                char const* jumpLabel = JUMP_LABELS[jumpIndex];
                int jumpTab = JUMP_TABS[jumpIndex];
                if (normalized(std::string(jumpLabel)).find(query) == std::string::npos) continue;
                auto* row = CCNode::create();
                row->setContentSize({ SCROLL_W - 14.f, ROW_H });
                auto* rowBg = gelPanel(SCROLL_W - 14.f, ROW_H - 4.f, ccc4(44, 26, 66, 250), 11.f);
                row->addChildAtPosition(rowBg, Anchor::Center);
                auto* label = CCLabelBMFont::create(jumpLabel, "bigFont.fnt");
                label->setAnchorPoint({ 0.f, 0.5f });
                label->limitLabelWidth(SCROLL_W - 118.f, 0.56f, 0.30f);
                label->setColor({ 215, 234, 243 });
                row->addChildAtPosition(label, Anchor::Left, { 13.f, 0.f });
                auto* rowMenu = CCMenu::create();
                rowMenu->setContentSize(row->getContentSize());
                rowMenu->ignoreAnchorPointForPosition(false);
                rowMenu->setAnchorPoint({ 0.f, 0.f });
                rowMenu->setPosition({ 0.f, 0.f });
                auto* go = actionItem(TAB_LABELS[jumpTab], menu_selector(BufferMenu::onJump), jumpTab, 70.f, 28.f, ccc4(46, 56, 112, 255));
                go->setPosition({ SCROLL_W - 14.f - 14.f - 35.f, ROW_H / 2.f });
                rowMenu->addChild(go);
                row->addChild(rowMenu);
                m_scroll->m_contentLayer->addChild(row);
            }
        }

        if (tab == 2) {
            auto* row = CCNode::create();
            row->setContentSize({ SCROLL_W - 14.f, 78.f });
            auto* bg = gelPanel(SCROLL_W - 14.f, 76.f, ccc4(66, 30, 62, 255), 12.f);
            row->addChildAtPosition(bg, Anchor::Center);
            m_tpsLabel = CCLabelBMFont::create("PHYSICS RATE", "bigFont.fnt");
            m_tpsLabel->setAnchorPoint({ 0.f, 0.5f });
            m_tpsLabel->setColor(m_t.soft);
            m_tpsLabel->limitLabelWidth(105.f, 0.52f, 0.40f);
            row->addChildAtPosition(m_tpsLabel, Anchor::Left, { 14.f, 20.f });
            m_tpsInput = numberInput(52.f, "TPS", 4);
            m_tpsInput->setString(fmt::format("{}", buf.tps));
            m_tpsSlider = styledSlider(150.f, static_cast<float>(TPS_MIN), static_cast<float>(TPS_MAX),
                static_cast<float>(buf.tps), [this](geode::SliderNode*, float value) { onTpsSlider(value); });
            m_tpsSlider->setSnapStep(10.f);
            m_tpsSlider->linkTextInput(m_tpsInput, 0);
            row->addChildAtPosition(m_tpsSlider, Anchor::Left, { 14.f + 75.f, -13.f });
            row->addChildAtPosition(m_tpsInput, Anchor::Right, { -14.f - 26.f, -13.f });
            m_tpsCard = row;
            m_scroll->m_contentLayer->addChild(row);
        }
        m_scroll->m_contentLayer->updateLayout();
        themeLabels(m_scroll->m_contentLayer);
    }

    void selectTab(int tab) {
        static constexpr char const* TITLES[] = { "INPUT", "SMART PREDICTION", "VISUALS",
            "GAMEMODES", "ORB TIMING", "KEYBINDS", "RHYTHM GRAPH", "PATH VIEW", "STUDIO TOOLS", "ASSIST TUNING", "STRAIGHT FLY",
            "FRAME SPAM", "FUN" };
        static constexpr char const* HELP[] = {
            R"(THE BUFFER

The ghost checks your next
input. If a nearby tick
looks safer, it delays the
input until then.

4 ticks = 1 frame.

Use Gamemodes to tune
each mode.)",
            R"(SMART OPTIONS

These options help the
ghost understand orbs,
portals, moving hazards
and each gamemode.

If a level behaves
strangely, try switching
off its related option.)",
            R"(VISUAL TOOLS

The heatmap marks places
you died. Performance mode
limits prediction work.

TPS changes the simulation
rate for the buffer.)",
            "Set the custom input delay for each movement mode.",
            "Tune each orb and the wave, robot, and dash release windows.",
            "Click a key, then press its replacement. Escape cancels capture.",
            "The rhythm graph shows the down and up intervals the alternate assist has learned from your spam, and how far its forecast is from what you really do.\n\nManual calibration: press START and tap on every beat. It measures how late or early you click and can set Click bias for you.\n\nThe search box at the top finds any setting; the grey suggestion is a forecast of what you are typing.",
            "Path & Alternate is the home for the long route preview, edge-by-edge timing assist, and optional progress-stage bands. Fast wave spam uses a 12-tick look by default and only delays a real press or release when the ghost finds the minimum gain. Rhythm prediction runs on the ghost only; it never generates wave taps. Optional wave auto-release is shown here and is practice-only unless normal-run auto inputs are enabled.",
            "Studio holds the colour theme, HUD positions, six preset slots, and extra tuning: click bias, orb margin, swing look, the adaptive buffer, and the optional performance guard.",
            "Assist tuning is the alternate wave assist: how it learns your rhythm, how far it may correct, the even-zigzag rule, the two-edge plan and the progress bands. The ASSIST PRESET buttons set the whole group at once.",
            "Straight fly keeps you centred in 1-block gaps, and the ship guard switches the ship at the last safe moment in tight corridors (set the level range, for example 35 to 45 for the ship part of Tidal Wave).",
            "Frame spam takes over once your own clicks reach the first CPS number and sends an even spam at the second one (wave, or UFO / ball / swing if you switch that on). It stops when you stop.",
            "Fun has on-screen buffer info (stage, assist delay, saves, save rate, why a click got no save), run info (death history, the next busy part), sounds and flashes, and text. Everything is optional and off by default."
        };
        if (m_searchMode) {   // picking a tab ends a search
            m_searchMode = false;
            m_allSearchQuery.clear();
            if (m_allSearchInput) m_allSearchInput->setString("", false);
            updateSearchHint();
        }
        m_tab = std::clamp(tab, 0, TAB_COUNT - 1);
        Mod::get()->setSavedValue("studio-last-tab", static_cast<int64_t>(m_tab));
        for (size_t i = 0; i < m_rows.size(); ++i) m_rows[i]->setVisible(true);
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            m_tabs[i]->setOpacity(0);
            if (i < m_tabHighlights.size()) {
                auto* highlight = m_tabHighlights[i];
                highlight->stopAllActions();
                highlight->setVisible(static_cast<int>(i) == m_tab);
                if (static_cast<int>(i) == m_tab) {
                    highlight->setPosition(tabCenter(static_cast<int>(i)));
                    highlight->setScale(0.94f);
                    highlight->runAction(CCEaseBackOut::create(CCScaleTo::create(0.20f, 1.f)));
                }
            }
            if (i < m_tabLabels.size()) m_tabLabels[i]->setColor(static_cast<int>(i) == m_tab
                ? themeColor({ 255, 238, 249 }) : themeColor({ 194, 164, 187 }));
        }
        scrollTabIntoView(m_tab);
        m_configTitle->setString(TITLES[m_tab]);
        m_configTitle->stopAllActions();
        bool standardPage = standardNow();
        if (m_status) m_status->setVisible(false);
        if (m_allSearchInput) {
            m_allSearchInput->setVisible(true);
            m_allSearchInput->setEnabled(!m_helpOpen);
        }
        if (m_scroll) {
            m_scroll->setVisible(standardPage);
            if (m_stdScrollbar) m_stdScrollbar->setVisible(standardPage);
            m_scroll->setTouchEnabled(standardPage && !m_helpOpen);
            m_scroll->setPosition(layoutPoint(CONFIG_PANEL_X, 350.f));
            if (standardPage) slideIn(m_scroll, layoutPoint(CONFIG_PANEL_X, 350.f));
            if (standardPage) buildOptionRows(m_tab);
        }
        if (m_tpsCard) m_tpsCard->setVisible(m_tab == 2);
        if (m_customPage) {
            bool customPage = customNow();
            if (m_customScroll) {
                m_customScroll->setVisible(customPage);
                if (m_customScrollbar) m_customScrollbar->setVisible(customPage);
                m_customScroll->setTouchEnabled(customPage && !m_helpOpen);
            }
            if (m_tab != 5) m_captureKeyIndex = -1;
            if (customPage) {
                if (m_customMenu) m_customMenu->setEnabled(false);
                float pageHeight = pageHeightFor(m_tab);
                m_customScroll->m_contentLayer->setContentSize({ SETTINGS_W, pageHeight });
                m_customPage->setContentSize({ SETTINGS_W, pageHeight });
                m_customSliders.clear();
                m_customInputs.clear();
                m_customPage->removeAllChildrenWithCleanup(true);
                m_customMenu = nullptr;
                m_keyLabels.clear();
                m_specialValues.clear();
                m_specialValueTags.clear();
                m_specialToggles.clear();
                m_bandLabels.clear();
                m_bandInputs.clear();
                m_studioLabels.clear();
                m_captureHint = nullptr;
                m_customMenu = CCMenu::create();
                m_customMenu->setContentSize(m_customPage->getContentSize());
                m_customMenu->ignoreAnchorPointForPosition(false);
                m_customMenu->setAnchorPoint({ 0.f, 0.f });
                m_customMenu->setPosition({ 0.f, 0.f });
                m_customPage->addChild(m_customMenu);
                if (m_tab == 3) buildModePage();
                else if (m_tab == 4) buildOrbPage();
                else if (m_tab == 5) buildKeybindPage();
                else if (m_tab == 6) buildGraphPage();
                else if (m_tab == 8) buildStudioPage();
                else if (m_tab == 12) buildFunPage();
                else buildPathAssistPage();   // tabs 7, 9, 10 and 11 are sections of the same builder
                m_customPage->reorderChild(m_customMenu, 10);
                themeLabels(m_customPage);
                refreshSpecialPage();
                if (m_customScroll) m_customScroll->scrollToTop();
                slideIn(m_customScroll, layoutPoint(CONFIG_PANEL_X, 350.f));
            }
        }
        bool customControlsEnabled = customNow();
        if (m_customMenu) m_customMenu->setEnabled(customControlsEnabled);
        for (auto* slider : m_customSliders) slider->setEnabled(customControlsEnabled);
        for (auto* input : m_customInputs) input->setEnabled(customControlsEnabled);
    }

    // Recolours labels once (a flag in the label's user data stops a second rotation).
    void themeLabels(CCNode* node) {
        if (!node || studio.themeIndex == 0) return;
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
            if (!label->getUserData()) {
                label->setColor(themeColor(label->getColor()));
                label->setUserData(reinterpret_cast<void*>(1));
            }
        }
        if (auto* kids = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(kids)) themeLabels(child);
        }
    }

    static bool isCustomTab(int tab) {
        return tab >= 3 && tab <= 12;   // everything except the three standard tabs
    }

    // Tab numbers stay as they were (0 - 8) with the new ones after them; this is the order they are listed in.
    // The tabs are listed in four groups with a small heading each. Tab numbers stay as they were.
    static int tabAtPosition(int position) {
        static constexpr int ORDER[TAB_COUNT] = { 0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 6, 8, 12 };
        return ORDER[std::clamp(position, 0, TAB_COUNT - 1)];
    }

    static int positionOfTab(int tab) {
        for (int position = 0; position < TAB_COUNT; ++position) {
            if (tabAtPosition(position) == tab) return position;
        }
        return 0;
    }

    static constexpr int GROUP_COUNT = 4;
    static int groupStart(int group) {
        static constexpr int START[GROUP_COUNT] = { 0, 3, 6, 10 };
        return START[std::clamp(group, 0, GROUP_COUNT - 1)];
    }

    static int groupOfPosition(int position) {
        int group = 0;
        for (int g = 0; g < GROUP_COUNT; ++g) {
            if (position >= groupStart(g)) group = g;
        }
        return group;
    }

    static float tabContentHeight() {
        return std::max(TAB_VIEW_H, static_cast<float>(TAB_COUNT) * TAB_PITCH + static_cast<float>(GROUP_COUNT) * TAB_HEADER + 12.f);
    }

    // where a group heading sits inside the scrolling list (y counted from the bottom of the list)
    static float headerCenterY(int group) {
        return tabContentHeight() - 6.f - TAB_PITCH * static_cast<float>(groupStart(group)) - TAB_HEADER * static_cast<float>(group) - TAB_HEADER * 0.5f;
    }

    // where a tab sits inside the scrolling list
    static CCPoint tabCenter(int tab) {
        int position = positionOfTab(tab);
        int group = groupOfPosition(position);
        return { TAB_VIEW_W / 2.f, tabContentHeight() - 6.f - TAB_PITCH * (static_cast<float>(position) + 0.5f) - TAB_HEADER * static_cast<float>(group + 1) };
    }

    void scrollTabIntoView(int tab) {
        if (!m_tabScroll) return;
        float viewHeight = m_tabScroll->getContentHeight();
        float minY = std::min(0.f, viewHeight - tabContentHeight());
        float wanted = viewHeight / 2.f - tabCenter(tab).y;
        m_tabScroll->m_contentLayer->setPositionY(std::clamp(wanted, minY, 0.f));
    }

    static float pageHeightFor(int tab) {
        switch (tab) {
            case 3: return MODE_PAGE_HEIGHT;
            case 4: return ORB_PAGE_HEIGHT;
            case 5: return KEY_PAGE_HEIGHT;
            case 6: return GRAPH_PAGE_HEIGHT;
            case 7: return PATH_PAGE_HEIGHT;
            case 8: return STUDIO_PAGE_HEIGHT;
            case 9: return ALT_PAGE_HEIGHT;
            case 10: return GAP_PAGE_HEIGHT;
            case 11: return SPAM_PAGE_HEIGHT;
            case 12: return FUN_PAGE_HEIGHT;
            default: return MENU_HEIGHT;
        }
    }

    static bool optionValue(Option const& o) {
        return o.key ? Mod::get()->getSettingValue<bool>(o.key) : buf.showPath;
    }

    static constexpr int KEYBIND_COUNT = 9;
    static constexpr char const* KEYBIND_KEYS[KEYBIND_COUNT] = { "bind-toggle", "bind-panic", "bind-stage", "bind-menu",
        "bind-path", "bind-console", "bind-export", "bind-forget", "bind-wave-direct" };

    void buildModePage();
    void buildOrbPage();
    void buildKeybindPage();
    void buildPathAssistPage();
    void refreshSpecialPage();
    void onModeStep(CCObject* sender);
    void onModeSlider(int mode, float value);
    void onModePower(CCObject* sender);
    void onAltToggle(CCObject* sender);
    void onUSKCPreset(CCObject* sender);
    void onPathToggle(CCObject* sender);
    void onModePreset(CCObject* sender);
    void onModeUseCustom(CCObject* sender);
    void onOrbStep(CCObject* sender);
    void onOrbSlider(int orb, float value);
    void onReleaseStep(CCObject* sender);
    void onReleaseSlider(int row, float value);
    void onOrbLink(CCObject* sender);
    void onOrbLinkAll(CCObject* sender);
    void onTpsStep(CCObject* sender);
    void onTpsSlider(float value);
    void onAltSlider(float value);
    void onAltSamplesSlider(float value);
    void onAltToleranceSlider(float value);
    void onAltMarginSlider(float value);
    void onAltBurstGapSlider(float value);
    void onAltWaveHorizonSlider(float value);
    void onAltWaveLookSlider(float value);
    void onAltWaveMinGainSlider(float value);
    void onPathRefreshSlider(float value);
    void onPathHorizonSlider(float value);
    void onBandStep(CCObject* sender);
    void onModeLink(CCObject* sender);
    void onRescan(CCObject* sender);
    void onAssistPreset(CCObject* sender);
    void onBandInput(int band, int field, std::string const& text);
    void onBuiltinPreset(CCObject* sender);
    void buildStudioPage();
    void onTheme(CCObject* sender);
    void onFxLevel(CCObject* sender);
    void onHudStep(CCObject* sender);
    void onHudReset(CCObject* sender);
    void onPresetSave(CCObject* sender);
    void onPresetLoad(CCObject* sender);
    void onSetupCopy(CCObject* sender);
    void onSetupCheck(CCObject* sender);
    void onCheckUpdate(CCObject* sender);
    void onReplayWelcome(CCObject* sender);
    void onSetupPaste(CCObject* sender);
    void onTuneStep(CCObject* sender);
    void onStudioSlider(int which, float value);
    void onStudioToggle(CCObject* sender);
    void onBandStage(CCObject* sender);
    void onSfSlider(int which, float value);
    void onMoreSlider(int which, float value);
    void buildFunPage();
    void buildGraphPage();
    void drawRhythmGraph();
    void onCalibStart(CCObject* sender);
    void onCalibTap(CCObject* sender);
    void onCalibApply(CCObject* sender);
    void onCaptureKey(CCObject* sender);
    void acceptCapturedKey(enumKeyCodes key);

    // ---- search (replaces the All tab): a box at the top finds any setting, with a forecast of what you are typing
    bool m_searchMode = false;
    CCLabelBMFont* m_searchHint = nullptr;
    CCMenuItemLabel* m_searchHintItem = nullptr;
    std::string m_searchSuggestion;

    bool standardNow() const { return m_searchMode || m_tab <= 2; }
    bool customNow() const { return !m_searchMode && isCustomTab(m_tab); }

    static std::string lowerCase(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    // the first setting (or jump) name that starts with the typed text, else one that contains it
    std::string predictSearch(std::string const& typed) {
        std::string q = lowerCase(typed);
        if (q.empty()) return {};
        std::string contains;
        auto consider = [&](char const* name, bool& exact) -> bool {
            std::string n = lowerCase(name);
            if (n.size() > q.size() && n.rfind(q, 0) == 0) { exact = true; return true; }
            if (contains.empty() && n != q && n.find(q) != std::string::npos) contains = name;
            return false;
        };
        for (auto const& option : OPTIONS) {
            bool exact = false;
            if (consider(option.label, exact)) return option.label;
        }
        for (int i = 0; i < JUMP_COUNT; ++i) {
            bool exact = false;
            if (consider(JUMP_LABELS[i], exact)) return JUMP_LABELS[i];
        }
        return contains;
    }

    void updateSearchHint() {
        m_searchSuggestion = predictSearch(m_allSearchQuery);
        if (!m_searchHintItem) return;
        bool show = !m_searchSuggestion.empty();
        m_searchHintItem->setVisible(show);
        m_searchHintItem->setEnabled(show);
        if (show) {
            m_searchHint->setString(fmt::format("> {}", m_searchSuggestion).c_str());
            m_searchHintItem->setContentSize(m_searchHint->getContentSize());
        }
    }

    // ---- the two drop-down panels at the top middle of the screen: SOCIALS and CHANGELOG. They slide in and out with a back animation;
    // opening one closes the other first.
    CCNode* m_infoPanel[2] = { nullptr, nullptr };
    CCNode* m_infoArrow[2] = { nullptr, nullptr };
    float m_infoHeight[2] = { 128.f, 330.f };
    int m_infoOpen = -1;
    ScrollLayer* m_changeScroll = nullptr;
    static constexpr float INFO_X = 524.f;       // the middle between the two side panels
    static constexpr float INFO_W = 340.f;

    float infoRestY(int i) const { return MENU_HEIGHT - 46.f - m_infoHeight[i] / 2.f; }
    float infoHiddenY(int i) const { return MENU_HEIGHT + m_infoHeight[i] / 2.f + 30.f; }

    void showInfo(int i, float delay) {
        auto* panel = m_infoPanel[i];
        if (!panel) return;
        panel->stopAllActions();
        panel->setVisible(true);
        panel->setPosition({ INFO_X, infoHiddenY(i) });
        panel->runAction(CCSequence::create(CCDelayTime::create(delay),
            CCEaseBackOut::create(CCMoveTo::create(0.45f, CCPoint{ INFO_X, infoRestY(i) })), nullptr));
        m_infoArrow[i]->stopAllActions();
        m_infoArrow[i]->runAction(CCRotateTo::create(0.25f, 180.f));
        if (i == 1 && m_changeScroll) {
            m_changeScroll->setTouchEnabled(true);
            m_changeScroll->scrollToTop();
        }
    }

    void hideInfo(int i) {
        auto* panel = m_infoPanel[i];
        if (!panel) return;
        panel->stopAllActions();
        panel->runAction(CCSequence::create(
            CCEaseBackIn::create(CCMoveTo::create(0.35f, CCPoint{ INFO_X, infoHiddenY(i) })), CCHide::create(), nullptr));
        m_infoArrow[i]->stopAllActions();
        m_infoArrow[i]->runAction(CCRotateTo::create(0.25f, 0.f));
        if (i == 1 && m_changeScroll) m_changeScroll->setTouchEnabled(false);
    }

    void onInfoToggle(CCObject* sender) {
        int i = sender->getTag();
        if (i < 0 || i > 1) return;
        if (m_infoOpen == i) {
            hideInfo(i);
            m_infoOpen = -1;
            return;
        }
        float delay = 0.f;
        if (m_infoOpen >= 0) {
            hideInfo(m_infoOpen);
            delay = 0.3f;   // the first one leaves before the second one arrives
        }
        showInfo(i, delay);
        m_infoOpen = i;
    }

    void onSocialLink(CCObject* sender) {
        static constexpr char const* KEYS[4] = { "social-youtube", "social-tiktok", "social-discord", "social-github" };
        static constexpr char const* NAMES[4] = { "YouTube", "TikTok", "Discord", "GitHub" };
        int index = std::clamp(sender->getTag(), 0, 3);
        std::string url = Mod::get()->getSettingValue<std::string>(KEYS[index]);
        if (url.empty() && index == 3) {
            std::string repo = Mod::get()->getSettingValue<std::string>("update-repo");
            if (!repo.empty()) url = "https://github.com/" + repo;
        }
        if (url.empty()) {
            geode::Notification::create(fmt::format("The {} link is not set yet (mod settings > Socials).", NAMES[index]), geode::NotificationIcon::Warning, 2.f)->show();
            return;
        }
        geode::utils::web::openLinkInBrowser(url);
    }

    void buildInfoPanels() {
        // the two arrow buttons
        auto* infoMenu = CCMenu::create();
        infoMenu->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(infoMenu, 60);
        static constexpr char const* BUTTON_NAMES[2] = { "SOCIALS", "CHANGELOG" };
        for (int i = 0; i < 2; ++i) {
            float w = i == 0 ? 112.f : 134.f;
            auto* body = CCNode::create();
            body->setContentSize({ w, 24.f });
            body->ignoreAnchorPointForPosition(false);
            body->setAnchorPoint({ 0.5f, 0.5f });
            if (auto* pill = gelPanel(w, 24.f, ccc4(86, 36, 130, 255), 9.f, false)) {
                pill->setPosition({ w / 2.f, 12.f });
                body->addChild(pill);
            }
            auto* arrow = CCDrawNode::create();
            CCPoint tri[3] = { { -5.f, 3.f }, { 5.f, 3.f }, { 0.f, -4.f } };
            arrow->drawPolygon(tri, 3, neonPremul({ 1.f, 0.88f, 0.69f, 1.f }), 0.f, neonPremul({ 0.f, 0.f, 0.f, 0.f }));
            arrow->setPosition({ 16.f, 12.f });
            body->addChild(arrow, 2);
            m_infoArrow[i] = arrow;
            auto* label = CCLabelBMFont::create(BUTTON_NAMES[i], "bigFont.fnt");
            label->limitLabelWidth(w - 40.f, 0.42f, 0.25f);
            label->setPosition({ w / 2.f + 8.f, 12.5f });
            body->addChild(label, 2);
            auto* item = CCMenuItemSpriteExtra::create(body, nullptr, this, menu_selector(BufferMenu::onInfoToggle));
            item->setTag(i);
            item->setPosition(layoutPoint(INFO_X + (i == 0 ? -70.f : 74.f), 20.f));
            infoMenu->addChild(item);
        }

        // panel frames
        for (int i = 0; i < 2; ++i) {
            float h = m_infoHeight[i];
            auto* panel = CCNode::create();
            panel->setContentSize({ INFO_W, h });
            panel->ignoreAnchorPointForPosition(false);
            panel->setAnchorPoint({ 0.5f, 0.5f });
            panel->setPosition({ INFO_X, infoHiddenY(i) });
            panel->setVisible(false);
            if (auto* back = gelPanel(INFO_W, h, ccc4(34, 16, 48, 252), 16.f)) {
                back->setPosition({ INFO_W / 2.f, h / 2.f });
                panel->addChild(back);
            }
            if (auto* band = gelPanel(INFO_W - 20.f, 24.f, ccc4(150, 56, 200, 255), 9.f, false)) {
                band->setPosition({ INFO_W / 2.f, h - 20.f });
                panel->addChild(band, 1);
            }
            auto* title = CCLabelBMFont::create(i == 0 ? "SOCIALS" : "CHANGELOG", "bigFont.fnt");
            title->setScale(0.5f);
            title->setPosition({ INFO_W / 2.f, h - 19.f });
            panel->addChild(title, 2);
            m_mainLayer->addChild(panel, 250);
            m_infoPanel[i] = panel;
        }

        // socials: four clickable pictures with their names
        {
            static constexpr char const* ICONS[4] = { "social-youtube.png", "social-tiktok.png", "social-discord.png", "social-github.png" };
            static constexpr char const* NAMES[4] = { "YouTube", "TikTok", "Discord", "GitHub" };
            auto* menu = CCMenu::create();
            menu->setPosition({ 0.f, 0.f });
            m_infoPanel[0]->addChild(menu, 3);
            for (int i = 0; i < 4; ++i) {
                auto* body = CCNode::create();
                body->setContentSize({ 66.f, 82.f });
                body->ignoreAnchorPointForPosition(false);
                body->setAnchorPoint({ 0.5f, 0.5f });
                char const* iconName = ICONS[i];
                std::string path = Mod::get()->expandSpriteName(iconName).c_str();
                if (auto* icon = CCSprite::create(path.c_str())) {
                    icon->setScale(54.f / std::max(1.f, icon->getContentSize().width));
                    icon->setPosition({ 33.f, 52.f });
                    body->addChild(icon);
                }
                auto* name = CCLabelBMFont::create(NAMES[i], "bigFont.fnt");
                name->limitLabelWidth(64.f, 0.36f, 0.22f);
                name->setPosition({ 33.f, 12.f });
                body->addChild(name);
                auto* item = CCMenuItemSpriteExtra::create(body, nullptr, this, menu_selector(BufferMenu::onSocialLink));
                item->setTag(i);
                item->setPosition({ INFO_W / 2.f + (i - 1.5f) * 76.f, m_infoHeight[0] / 2.f - 18.f });
                menu->addChild(item);
            }
        }

        // changelog: a scrollable list, newest first, with ADDED / FIXED / REWORKED / REMOVED tags
        {
            struct Entry { char const* tag; char const* text; };
            struct Version { char const* name; std::vector<Entry> entries; };
            std::vector<Version> versions = {
                { "v1.15.0", { { "ADDED", "The path is long enough for the whole delay window (up to 320 ticks)." },
                               { "REWORKED", "Long cube / ball delays test every single tick, so a frame-perfect jump is never skipped." },
                               { "REWORKED", "The route search covers a 120 tick window every 1-2 ticks." } } },
                { "v1.14", { { "ADDED", "Menu music: the game's menu music fades out, Synca's fades in, and the other way round." },
                             { "ADDED", "Update popup that installs a new release for you; sounds for the lightning intro." },
                             { "FIXED", "Sounds were missing from the package. The package is much smaller." },
                             { "REWORKED", "Sliders: a glowing purple fill and a white line handle." } } },
                { "v1.13", { { "ADDED", "Graph tab with the learned rhythm and an osu-style manual calibration." },
                             { "ADDED", "Search box with a forecast, mouse wheel scrolling and new scrollbars." },
                             { "ADDED", "Welcome slides and the lightning intro; your own game icons and orbs in the menu." },
                             { "FIXED", "The stage snapped back when Auto switch by level % was on." },
                             { "FIXED", "Path and auto wave release lag in wave and ball." } } },
                { "v1.12", { { "ADDED", "Alternate assist forecast error, real-vs-ghost trail, ghost audit and death-log.txt." } } },
                { "v1.11", { { "ADDED", "Long-delay bypass and a resize handle for the F9 terminal." },
                             { "FIXED", "Stage cycle and the pause menu readout." } } },
                { "v1.10", { { "FIXED", "Per-gamemode delays are used on every stage if you switch that on." },
                             { "ADDED", "Frame spam: a straight line in tiny gaps." },
                             { "REWORKED", "The engine was split into modules." } } },
                { "v1.9", { { "FIXED", "Freeze with auto wave release at 10 CPS; lag with predicted moving hazards." },
                            { "ADDED", "CHECK MY SETUP, a watchdog, orb timings up to 120 ticks." } } },
            };
            float scrollW = INFO_W - 24.f;
            float scrollH = m_infoHeight[1] - 52.f;
            m_changeScroll = ScrollLayer::create({ scrollW, scrollH });
            if (m_changeScroll) {
                m_changeScroll->setStealingTouches(true);
                m_changeScroll->ignoreAnchorPointForPosition(false);
                m_changeScroll->setAnchorPoint({ 0.5f, 0.5f });
                m_changeScroll->setMouseEnabled(false);
                m_changeScroll->setTouchEnabled(false);
                m_changeScroll->setPosition({ INFO_W / 2.f, scrollH / 2.f + 8.f });
                m_infoPanel[1]->addChild(m_changeScroll, 3);
                struct Placed { CCNode* node; float top; };
                std::vector<Placed> placed;
                float cursor = 6.f;
                for (auto const& version : versions) {
                    auto* head = CCLabelBMFont::create(version.name, "bigFont.fnt");
                    head->setScale(0.56f);
                    head->setAnchorPoint({ 0.f, 1.f });
                    head->setColor({ 255, 238, 249 });
                    placed.push_back({ head, cursor });
                    m_changeScroll->m_contentLayer->addChild(head);
                    cursor += 32.f;
                    for (auto const& entry : version.entries) {
                        std::string tag = entry.tag;
                        ccColor3B color = tag == "ADDED" ? ccColor3B{ 140, 230, 160 } : tag == "FIXED" ? ccColor3B{ 255, 184, 110 }
                            : tag == "REWORKED" ? ccColor3B{ 140, 190, 255 } : ccColor3B{ 255, 130, 140 };
                        auto* tagLabel = CCLabelBMFont::create(entry.tag, "bigFont.fnt");
                        tagLabel->setScale(0.36f);
                        tagLabel->setAnchorPoint({ 0.f, 1.f });
                        tagLabel->setColor(color);
                        placed.push_back({ tagLabel, cursor + 2.f });
                        float textScale = 0.66f;
                        std::string wrapped = wrapText("chatFont.fnt", entry.text, textScale, scrollW - 118.f);
                        auto* text = CCLabelBMFont::create(wrapped.c_str(), "chatFont.fnt");
                        text->setScale(textScale);
                        text->setAnchorPoint({ 0.f, 1.f });
                        text->setColor({ 222, 210, 238 });
                        placed.push_back({ text, cursor });
                        m_changeScroll->m_contentLayer->addChild(tagLabel);
                        m_changeScroll->m_contentLayer->addChild(text);
                        cursor += std::max(18.f, text->getScaledContentSize().height) + 10.f;
                    }
                    cursor += 8.f;
                }
                float contentH = std::max(scrollH, cursor + 6.f);
                m_changeScroll->m_contentLayer->setContentSize({ scrollW, contentH });
                for (auto const& p : placed) {
                    bool isTag = dynamic_cast<CCLabelBMFont*>(p.node) && p.node->getScale() < 0.4f;
                    p.node->setPosition({ isTag ? 8.f : (dynamic_cast<CCLabelBMFont*>(p.node) && p.node->getScale() > 0.45f && p.node->getScale() < 0.47f ? 8.f : 100.f), contentH - p.top });
                }
                m_changeScroll->scrollToTop();
                addScrollbar(m_changeScroll, m_infoPanel[1], INFO_W - 8.f, MENU_HEIGHT - (scrollH / 2.f + 8.f), scrollH);
            }
        }
    }

    void onSearchHint(CCObject*) {
        if (m_searchSuggestion.empty()) return;
        std::string chosen = m_searchSuggestion;
        if (m_allSearchInput) m_allSearchInput->setString(chosen, false);
        applySearch(chosen);
    }

    void applySearch(std::string const& query) {
        m_allSearchQuery = query;
        if (query.empty()) {
            bool wasSearching = m_searchMode;
            m_searchMode = false;
            updateSearchHint();
            if (wasSearching) selectTab(m_tab);
            return;
        }
        m_searchMode = true;
        if (m_configTitle) m_configTitle->setString("SEARCH RESULTS");
        if (m_customScroll) {
            m_customScroll->setVisible(false);
            m_customScroll->setTouchEnabled(false);
        }
        if (m_customMenu) m_customMenu->setEnabled(false);
        for (auto* slider : m_customSliders) slider->setEnabled(false);
        for (auto* input : m_customInputs) input->setEnabled(false);
        if (m_tpsCard) m_tpsCard = nullptr;
        if (m_scroll) {
            m_scroll->setVisible(!m_helpOpen);
            m_scroll->setTouchEnabled(!m_helpOpen);
            m_scroll->setPosition(layoutPoint(CONFIG_PANEL_X, 350.f));
            buildOptionRows(0);
            m_scroll->scrollToTop();
        }
        updateSearchHint();
    }

    // ---- scrolling: the mouse wheel over either panel, and our own scrollbars (drag the thumb, or click the track)
    struct ScrollBar {
        ScrollLayer* target;
        CCDrawNode* draw;
        float height;
    };
    std::vector<ScrollBar> m_bars;
    int m_barDrag = -1;
    float m_barGrab = 0.f;

    void addScrollbar(ScrollLayer* target, CCNode* parent, float x, float y, float height) {
        auto* draw = CCDrawNode::create();
        draw->setPosition(layoutPoint(x, y));
        parent->addChild(draw, 7);
        m_bars.push_back({ target, draw, height });
    }

    // thumb size and where it sits (centre, counted from the middle of the track); false = nothing to scroll
    bool barGeometry(ScrollBar const& bar, float& thumbH, float& thumbCenter, float& progress) const {
        if (!bar.target || !bar.target->isVisible()) return false;
        float viewH = bar.target->getContentHeight();
        float contentH = bar.target->m_contentLayer->getContentHeight();
        if (contentH <= viewH + 1.f) return false;
        float trackH = bar.height - 8.f;
        thumbH = std::max(30.f, trackH * viewH / contentH);
        float minY = viewH - contentH;
        progress = std::clamp((bar.target->m_contentLayer->getPositionY() - minY) / -minY, 0.f, 1.f);
        thumbCenter = trackH / 2.f - thumbH / 2.f - progress * (trackH - thumbH);
        return true;
    }

    void onScrollbarUpdate(float) {
        for (auto& bar : m_bars) {
            bar.draw->clear();
            float thumbH, thumbCenter, progress;
            if (!barGeometry(bar, thumbH, thumbCenter, progress)) continue;
            float trackH = bar.height - 8.f;
            bar.draw->drawRect({ -3.f, -trackH / 2.f }, { 3.f, trackH / 2.f }, neonPremul({ 0.16f, 0.08f, 0.24f, 0.85f }), 0.f, neonPremul({ 0.f, 0.f, 0.f, 0.f }));
            for (int layer = 4; layer >= 1; --layer) {   // a soft white halo
                float spread = 2.5f + layer * 2.2f;
                bar.draw->drawRect({ -spread, thumbCenter - thumbH / 2.f - layer * 1.5f }, { spread, thumbCenter + thumbH / 2.f + layer * 1.5f },
                    neonPremul({ 1.f, 1.f, 1.f, 0.07f + (4 - layer) * 0.03f }), 0.f, neonPremul({ 0.f, 0.f, 0.f, 0.f }));
            }
            bar.draw->drawRect({ -3.5f, thumbCenter - thumbH / 2.f }, { 3.5f, thumbCenter + thumbH / 2.f }, neonPremul({ 1.f, 1.f, 1.f, 1.f }), 0.f, neonPremul({ 0.f, 0.f, 0.f, 0.f }));
        }
    }

    void setBarProgress(ScrollBar const& bar, float progress) {
        float viewH = bar.target->getContentHeight();
        float contentH = bar.target->m_contentLayer->getContentHeight();
        float minY = viewH - contentH;
        bar.target->m_contentLayer->setPositionY(minY + std::clamp(progress, 0.f, 1.f) * -minY);
    }

    void scrollWheel(float y, float x) override {
        if (m_closing || m_helpOpen) return;
        auto local = m_mainLayer->convertToNodeSpace(geode::cocos::getMousePos());
        ScrollLayer* target = nullptr;
        if (m_infoOpen == 1 && m_changeScroll && m_infoPanel[1] && m_infoPanel[1]->isVisible()) {
            auto panelPoint = m_infoPanel[1]->convertToNodeSpace(geode::cocos::getMousePos());
            auto panelSize = m_infoPanel[1]->getContentSize();
            if (panelPoint.x >= 0.f && panelPoint.x <= panelSize.width && panelPoint.y >= 0.f && panelPoint.y <= panelSize.height) target = m_changeScroll;
        }
        if (target) {}
        else if (local.x < SIDEBAR_WIDTH + 12.f) target = m_tabScroll;
        else if (local.x > MENU_WIDTH - CONFIG_PANEL_W - 12.f) {
            target = (m_scroll && m_scroll->isVisible()) ? m_scroll : (m_customScroll && m_customScroll->isVisible() ? m_customScroll : nullptr);
        }
        if (!target || y == 0.f) return;
        float viewH = target->getContentHeight();
        float contentH = target->m_contentLayer->getContentHeight();
        if (contentH <= viewH) return;
        bool invert = Mod::get()->getSavedValue<bool>("studio-wheel-invert", false);
        // one notch = about half a row; wheel down (positive) shows what is below
        float step = (y > 0.f ? 1.f : -1.f) * (invert ? -1.f : 1.f) * 54.f;
        float minY = viewH - contentH;
        target->m_contentLayer->setPositionY(std::clamp(target->m_contentLayer->getPositionY() + step, minY, 0.f));
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        m_barDrag = -1;
        if (m_closing || m_captureKeyIndex >= 0 || m_helpOpen) return true;
        for (size_t i = 0; i < m_bars.size(); ++i) {
            float thumbH, thumbCenter, progress;
            if (!barGeometry(m_bars[i], thumbH, thumbCenter, progress)) continue;
            auto local = m_bars[i].draw->convertTouchToNodeSpace(touch);
            if (std::abs(local.x) > 12.f || std::abs(local.y) > m_bars[i].height / 2.f) continue;
            m_barDrag = static_cast<int>(i);
            m_barGrab = std::abs(local.y - thumbCenter) <= thumbH / 2.f ? local.y - thumbCenter : 0.f;   // grab the thumb where it is, or jump to the click
            ccTouchMoved(touch, nullptr);
            return true;
        }
        return true;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (m_barDrag < 0 || m_barDrag >= static_cast<int>(m_bars.size())) return;
        auto const& bar = m_bars[m_barDrag];
        float thumbH, thumbCenter, progress;
        if (!barGeometry(bar, thumbH, thumbCenter, progress)) return;
        float trackH = bar.height - 8.f;
        float localY = bar.draw->convertTouchToNodeSpace(touch).y - m_barGrab;
        setBarProgress(bar, (trackH / 2.f - thumbH / 2.f - localY) / std::max(1.f, trackH - thumbH));
    }

    void ccTouchEnded(CCTouch*, CCEvent*) override {
        m_barDrag = -1;
    }

    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        m_barDrag = -1;
    }

    void refresh() {
        std::string stageLabel = !buf.enabled ? "OFF"
            : STAGES[buf.stage].custom ? "PER MODE"
            : fmt::format("{:g}F", STAGES[buf.stage].ticksAll * TICK);
        m_stage->setString(stageLabel.c_str());
        m_stage->limitLabelWidth(82.f, 0.54f, 0.42f);
        float dt = buf.tickLength > 0.001f ? buf.tickLength : TICK;
        m_status->setString(fmt::format("{:.0f} TPS / {}", 60.f / dt, cbfStatusText()).c_str());
        if (m_cfgStage) {
            std::string bufferValue = !buf.enabled ? "OFF" : STAGES[buf.stage].custom
                ? "PER MODE"
                : fmt::format("{:g}f / {:g}f", buf.ticksAll * TICK, buf.ticksWave * TICK);
            m_cfgStage->setString(bufferValue.c_str());
            m_cfgStage->limitLabelWidth(92.f, 0.46f, 0.36f);
        }
        if (m_cfgTiming) {
            m_cfgTiming->setString(fmt::format("WAVE {}t   ROBOT {}t   DASH {}t",
                buf.waveReleaseWindow, buf.robotReleaseWindow, buf.dashReleaseWindow).c_str());
            m_cfgTiming->limitLabelWidth(266.f, 0.46f, 0.34f);
        }
        if (m_cfgTps) {
            m_cfgTps->setString(fmt::format("TPS {}   {}   MEMORY {}   HEAT {}", buf.tps,
                cbfStatusText(), buf.learn ? "ON" : "OFF", buf.heatmap ? "ON" : "OFF").c_str());
            m_cfgTps->limitLabelWidth(266.f, 0.42f, 0.30f);
        }
        if (m_cfgTelemetry) {
            auto telemetry = sessionTelemetry();
            if (telemetry.altEdges > 0) {
                m_cfgTelemetry->setString(fmt::format("ALT {}/{}  |  {} saves / {} spots",
                    telemetry.altAdjusted, telemetry.altEdges, telemetry.saves, telemetry.spots).c_str());
            }
            else {
                m_cfgTelemetry->setString(fmt::format("{} saves / {} spots", telemetry.saves, telemetry.spots).c_str());
            }
            m_cfgTelemetry->limitLabelWidth(266.f, 0.42f, 0.32f);
        }
        if (m_tpsLabel) m_tpsLabel->setString(fmt::format("PHYSICS TPS  {}", buf.tps).c_str());
        if (m_tpsSlider && m_tpsInput && !m_syncingSlider) {
            m_syncingSlider = true;
            m_tpsSlider->setValue(static_cast<float>(buf.tps));
            m_tpsInput->setString(fmt::format("{}", buf.tps));
            m_syncingSlider = false;
        }
        colorLabels();
        for (size_t i = 0; i < m_toggles.size() && i < m_rowOptionIndices.size(); ++i) {
            updateToggleVisual(m_toggles[i], optionValue(OPTIONS[m_rowOptionIndices[i]]));
        }
    }

    void onFastRefresh(float) {
        if (m_closing) return;
        refresh();
        if (m_tab == 6 && !m_searchMode) drawRhythmGraph();
    }

    void onCalibFrame(float);

    void onPeriodicUpdate(float) {
        if (m_closing) return;

        readSettings();
        studioReadSettings();
        if (studio.orbTicksLinked) {
            int canonical = studio.orbTicks[SO_YELLOW];
            bool changed = false;
            for (int i = 0; i < SO_COUNT; ++i) {
                if (studio.orbTicks[i] == canonical) continue;
                Mod::get()->setSettingValue<int64_t>(STUDIO_ORBS[i].key, canonical);
                changed = true;
            }
            if (changed) studioReadSettings();
        }

        refresh();
        refreshSpecialPage();
    }

    void onTab(CCObject* sender) {
        selectTab(sender->getTag());
    }

    void onNavTab(CCObject* sender) {
        selectTab(sender->getTag());
    }

    void onJump(CCObject* sender) {
        selectTab(sender->getTag());
    }

    void onHelp(CCObject*) {
        static constexpr char const* TITLES[] = {
            "BASIC // INPUT HELP", "SMART // PREDICTION HELP", "VISUALS // FEEDBACK HELP",
            "GAMEMODE // FRAMES HELP", "ORB // TIMING HELP", "KEYBINDS // CONTROL HELP",
            "GRAPH // RHYTHM HELP", "PATH // VIEW HELP", "STUDIO // TOOLS HELP", "ASSIST // TUNING HELP",
            "STRAIGHT FLY // GUARD HELP", "FRAME SPAM // HELP", "FUN // HELP"
        };
        static constexpr char const* TEXT[] = {
            "Synca simulates a short ghost run to compare nearby input\n"
            "timings. It holds an input only when a nearby tick survives\n"
            "better. Your movement stays yours; Synca adjusts timing.\n\n"
            "At 60 FPS, 4 ticks equal 1 frame. Stage arrows choose a\n"
            "preset buffer. Gamemodes sets a separate delay for each\n"
            "mode. Start small and raise it only when you need it.",
            "Prediction options add context for orbs, portals, moving\n"
            "hazards, and each movement mode. Enable only the assists\n"
            "you want. If a level behaves unexpectedly, disable its\n"
            "related option first.\n\n"
            "Release windows are available for wave, robot, and dash\n"
            "on the Orbs tab. Prediction tools do not send extra input.",
            "The heatmap marks previous deaths. These visuals do not\n"
            "change your inputs.\n\n"
            "Performance mode limits extra prediction work. Physics\n"
            "TPS changes the simulation rate in steps of 10. 240 TPS\n"
            "matches the normal 60 FPS setting.",
            "Each card sets a delay for one movement mode. The step\n"
            "buttons change it by -1 frame, -1 tick, +1 tick, or +1\n"
            "frame. Drag the slider or enter an exact tick value.\n\n"
            "Four ticks equal one frame. Custom uses the Ship row too;\n"
            "preset stages use the Ship buffer switch in settings.\n"
            "Presets are a starting point you can adjust afterward.",
            "Set each orb timing in ticks. 0 / AUTO leaves timing\n"
            "automatic. Link orb values keeps them matched to the\n"
            "yellow orb and synchronizes every linked value.\n\n"
            "The lower controls set wave, robot, and dash release\n"
            "windows. Drag a slider or enter a number; settings are\n"
            "saved automatically.",
            "Choose Set key, then press the replacement key. Escape\n"
            "cancels capture. The menu key toggles this panel; each\n"
            "action shows its current binding.\n\n"
            "Avoid assigning the same key to two actions. You can also\n"
            "open Synca from the pause screen.",
            "The All tab shows every general switch in one list. Search\n"
            "by part of a setting name. Gamemode delays, orb timing,\n"
            "keybinds, and Path & Alternate stay in focused tabs.",
            "Path View draws the raw route and the route after your\n"
            "input. Choose either trace and set how far ahead it runs.\n\n"
            "Alternate Assist waits until your press and release timing\n"
            "is consistent. More samples are steadier but adapt more\n"
            "slowly. Timing tolerance controls accepted variation. Max\n"
            "correction sets how far an input can move.\n\n"
            "Safety margin checks the route a little ahead and behind\n"
            "the predicted position. Larger margins are more cautious.\n"
            "USKC WAVE applies a quick-learning wave setup. Corrections\n"
            "are also capped below your fastest learned edge interval,\n"
            "so the queue cannot run away from your manual rhythm. The\n"
            "assist only adjusts inputs you make.",
            "Theme recolours the whole menu (the menu reopens). HUD\n"
            "position nudges the watermark, console panel and orb dot.\n"
            "Presets save and load every tuning value in six slots.\n\n"
            "Click bias tells the assist whether your clicks land late\n"
            "(+) or early (-). Orb margin is the timing slack an orb\n"
            "click must survive. Adaptive buffer adds a little delay\n"
            "where you keep dying and removes it as you improve. The\n"
            "performance guard shortens look-ahead when frames get long.\n"
            "Own buffer for Player 2 gives each player in a 2-player level\n"
            "its own queue, rhythm and delay (its own mode or stage).",
            "The alternate assist waits until your press and release timing\n"
            "is consistent, then re-times single edges. ASSIST PRESET sets\n"
            "the whole group. Even zigzag keeps phases the learned length,\n"
            "plan width and safe choices kept set how hard it searches,\n"
            "and room levels how much timing error a correction must survive.",
            "Straight fly centres you in 1-block gaps (it needs the alternate\n"
            "assist). Ship guard switches the ship at the last safe moment\n"
            "inside the level range you set, for example 35 to 45 for the\n"
            "ship part of Tidal Wave. Auto inputs need Practice or Allow in\n"
            "normal runs.",
            "Frame spam starts when you spam the wave through a tight gap,\n"
            "sends an even spam at the clicks per second you set (a click is\n"
            "a press and a release) and keeps it centred. A gap too small for\n"
            "that rate gets faster spam. It stops when you stop clicking.",
            "Fun is optional extras: on-screen info, sounds and flashes, and\n"
            "text. Every switch is off by default."
        };
        if (!m_helpOverlay || !m_helpTitle || !m_helpText) return;
        m_helpTitle->setString(TITLES[m_tab]);
        m_helpText->setString(TEXT[m_tab]);
        m_helpOpen = true;
        m_helpOverlay->setVisible(true);
        setInputEnabled(false);
        // SliderNode and TextInput handle touches directly, so hide their pages while
        // the help sheet is up instead of only disabling the CCMenu.
        if (m_customScroll) {
            m_customScroll->setVisible(false);
            m_customScroll->setTouchEnabled(false);
        }
        if (m_allSearchInput) m_allSearchInput->setVisible(false);
        if (m_scroll) {
            m_scroll->setTouchEnabled(false);
            m_scroll->setVisible(false);
        }
    }

    void onHelpClose(CCObject*) {
        if (!m_helpOpen) return;
        m_helpOpen = false;
        m_helpOverlay->setVisible(false);
        setInputEnabled(true);
        if (m_customScroll) {
            bool customPage = customNow();
            m_customScroll->setVisible(customPage);
            m_customScroll->setTouchEnabled(customPage);
        }
        if (m_allSearchInput) m_allSearchInput->setVisible(true);
        if (m_scroll) {
            bool standardPage = standardNow();
            m_scroll->setVisible(standardPage);
            m_scroll->setTouchEnabled(standardPage);
        }
    }

    void onToggle(CCObject* sender) {
        auto const& o = OPTIONS[sender->getTag()];
        bool value = !optionValue(o);
        if (o.key) {
            Mod::get()->setSettingValue<bool>(o.key, value);
            readSettings();
            saveProfile();
            refreshHeatmap();
            refreshWatermark();
            refreshAutoTag();
        }
        else {
            buf.showPath = value;
            if (buf.pathNode) {
                buf.pathNode->clear();
            }
        }
        updateToggleVisual(static_cast<CCMenuItemSpriteExtra*>(sender), value);
        say("{} {}", o.label, value ? "on" : "off");
        refresh();
    }

    void onStage(CCObject* sender) {
        cycleStage(sender->getTag());
        refresh();
    }

    // v2.79: physics TPS slider (240-1000, steps of 10)
    void onTps(CCObject* sender) {
        float value = static_cast<SliderThumb*>(sender)->getValue();
        int tps = TPS_MIN + static_cast<int>(std::lround(value * (TPS_MAX - TPS_MIN) / 10.f)) * 10;
        Mod::get()->setSettingValue<int64_t>("tps", tps);
        readSettings();
        refreshWatermark();
        if (m_tpsLabel) m_tpsLabel->setString(fmt::format("PHYSICS TPS  {}{}", tps, tps == 240 ? " (normal)" : "").c_str());
    }

    void onCopyLog(CCObject*) {
        exportConsole();
    }

    void onConsole(CCObject*) {
        buf.panelOpen = !buf.panelOpen;
        refreshPanel();
        say("console {}", buf.panelOpen ? "shown" : "hidden");
    }

    // v2.75 presets: stage + the switches that matter most, in one click
    void onPreset(CCObject* sender) {
        auto* mod = Mod::get();
        int which = sender->getTag();
        int stage = which == 0 ? 2 : which == 1 ? 3 : 5;  // 2f / 4f / 8f
        mod->setSettingValue<bool>("robust-saves", true);
        mod->setSettingValue<bool>("wave-safety-margin", which != 2);
        mod->setSettingValue<bool>("more-time-death-spots", which != 0);
        mod->setSettingValue<bool>("low-latency", false);
        buf.stage = stage;
        mod->setSavedValue("stage", buf.stage);
        if (studio.progressStageBands) {
            studio.progressBaseStage = buf.stage;
            mod->setSavedValue("studio-progress-base-stage", static_cast<int64_t>(buf.stage));
        }
        resetPending();
        applyStage();
        readSettings();
        refreshWatermark();
        refreshPanel();
        say("preset: {} -> {}", which == 0 ? "Safe" : which == 1 ? "Balanced" : "Aggressive", stageText());
        refresh();
    }

    void onRunCheck(CCObject*) {
        requestRunCheck();
        FLAlertLayer::create("Run check",
            "Restart the level from the beginning: your longest recorded run is replayed on the hidden player and the "
            "result shows in the console (F9) and in Stats.",
            "OK")->show();
    }

    void onStats(CCObject*) {
        FLAlertLayer::create("Practice stats", statsText(), "OK")->show();
    }

    void onForget(CCObject*) {
        forgetLevelMemory();
        refresh();
    }

    CCMenuItemSpriteExtra* stepButton(char const* text, SEL_MenuHandler handler, int tag, float x, float y) {
        constexpr float BUTTON_W = 44.f;
        constexpr float BUTTON_H = 30.f;
        auto makeSprite = [&](ccColor4B color) -> CCNode* {
            auto* background = roundedPanel(BUTTON_W, BUTTON_H, color);
            if (!background) return nullptr;
            auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
            label->limitLabelWidth(BUTTON_W - 5.f, 0.58f, 0.42f);
            label->setColor({ 255, 250, 255 });
            label->setPosition({ BUTTON_W / 2.f, BUTTON_H / 2.f });
            background->addChild(label);
            return background;
        };
        auto* item = CCMenuItemSpriteExtra::create(
            makeSprite(ccc4(18, 53, 70, 245)), makeSprite(ccc4(40, 112, 137, 255)),
            this, handler);
        disableMenuItemAnimation(item);
        item->setTag(tag);
        item->setContentSize({ BUTTON_W, BUTTON_H });
        item->setPosition(customPoint(x, y));
        m_customMenu->addChild(item);
        return item;
    }

    void updateToggleVisual(CCMenuItemSpriteExtra* item, bool value) {
        if (!item) return;
        auto hitSize = item->getContentSize();
        item->setNormalImage(toggleVisual(value, hitSize.width, hitSize.height));
        item->setSelectedImage(toggleVisual(value, hitSize.width, hitSize.height));
        item->setContentSize(hitSize);
    }

    void finishClose() {
        if (s_instance == this) s_instance = nullptr;
        this->removeFromParentAndCleanup(true);
    }

    void onExit() override {
        if (s_instance == this) s_instance = nullptr;
        unscheduleAllSelectors();
        if (m_configLayer) m_configLayer->stopAllActions();
        CCLayer::onExit();
    }

    void setInputEnabled(bool enabled) {
        bool active = enabled && !m_closing && !m_helpOpen;
        if (m_buttonMenu) m_buttonMenu->setEnabled(active);
        if (m_tabMenu) m_tabMenu->setEnabled(active);
        if (m_tabScroll) m_tabScroll->setTouchEnabled(active);
        if (m_configButtonMenu) m_configButtonMenu->setEnabled(active);
        bool customPage = customNow();
        bool standardPage = standardNow();
        if (m_customMenu) m_customMenu->setEnabled(active && customPage);
        for (auto* slider : m_customSliders) slider->setEnabled(active && customPage);
        for (auto* input : m_customInputs) input->setEnabled(active && customPage);
        if (m_scroll) m_scroll->setTouchEnabled(active && standardPage);
        if (m_customScroll) m_customScroll->setTouchEnabled(active && customPage);
        if (m_allSearchInput) m_allSearchInput->setEnabled(active);
    }

    void reopen() {
        if (!m_configLayer) return;
        playMenuIntro();
        m_configLayer->stopAllActions();
        m_closing = false;
        if (m_helpOpen) {
            m_helpOpen = false;
            if (m_helpOverlay) m_helpOverlay->setVisible(false);
        }
        if (m_customScroll) m_customScroll->setVisible(customNow());
        if (m_allSearchInput) m_allSearchInput->setVisible(true);
        if (m_scroll) m_scroll->setVisible(standardNow());
        setInputEnabled(true);
        applyPanelScales(false);
        if (m_sidebarLayer) {
            m_sidebarLayer->stopAllActions();
            m_sidebarLayer->setPosition({ -SIDEBAR_WIDTH * m_leftScale, m_sidebarHome.y });
            m_sidebarLayer->runAction(CCEaseBackOut::create(
                CCMoveTo::create(0.38f, m_sidebarHome)));
        }
        m_configLayer->runAction(CCEaseInOut::create(
            CCMoveTo::create(0.34f, m_configOpenPosition), 3.f));
    }

protected:
    bool init() {
        constexpr float WIDTH = MENU_WIDTH;
        constexpr float HEIGHT = MENU_HEIGHT;
        if (!CCLayer::init()) {
            return false;
        }
        auto win = CCDirector::get()->getWinSize();
        this->setContentSize(win);
        this->setPosition({ 0.f, 0.f });
        this->setTouchEnabled(true);
        this->setKeyboardEnabled(true);
        this->setKeypadEnabled(true);
        this->setTouchPriority(kCCMenuHandlerPriority + 1);

        m_mainLayer = CCNode::create();
        m_mainLayer->setContentSize({ WIDTH, HEIGHT });
        m_mainLayer->setAnchorPoint({ 0.f, 0.f });
        m_mainLayer->setPosition({ 0.f, 0.f });
        this->addChild(m_mainLayer);

        m_sidebarLayer = CCNode::create();
        m_sidebarLayer->setContentSize({ WIDTH, HEIGHT });
        m_sidebarLayer->setAnchorPoint({ 0.f, 0.f });
        m_sidebarLayer->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(m_sidebarLayer, 4);

        m_configLayer = CCNode::create();
        m_configLayer->setContentSize({ WIDTH, HEIGHT });
        m_configLayer->setAnchorPoint({ 0.f, 0.f });
        m_configLayer->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(m_configLayer, 5);

        m_buttonMenu = CCMenu::create();
        m_buttonMenu->setContentSize({ WIDTH, HEIGHT });
        m_buttonMenu->ignoreAnchorPointForPosition(false);
        m_buttonMenu->setAnchorPoint({ 0.f, 0.f });
        m_buttonMenu->setPosition({ 0.f, 0.f });
        m_sidebarLayer->addChild(m_buttonMenu, 100);

        m_configButtonMenu = CCMenu::create();
        m_configButtonMenu->setContentSize({ WIDTH, HEIGHT });
        m_configButtonMenu->ignoreAnchorPointForPosition(false);
        m_configButtonMenu->setAnchorPoint({ 0.f, 0.f });
        m_configButtonMenu->setPosition({ 0.f, 0.f });
        m_configLayer->addChild(m_configButtonMenu, 100);

        auto addPlate = [&](float x, float y, float width, float height, ccColor4B color, int z = 0) {
            auto* plate = CCLayerColor::create(color, width, height);
            plate->ignoreAnchorPointForPosition(false);
            plate->setPosition(layoutPoint(x, y));
            m_sidebarLayer->addChild(plate, z);
            return plate;
        };
        auto addRoundedPlate = [&](CCNode* parent, float x, float y, float width, float height, ccColor4B color, int z = 0) {
            auto* plate = roundedPanel(width, height, color);
            if (!plate) return static_cast<CCNode*>(nullptr);
            plate->setPosition(layoutPoint(x, y));
            parent->addChild(plate, z);
            return plate;
        };
        auto addLabel = [&](char const* text, float x, float y, float width, float scale,
                            ccColor3B color, bool left = false) {
            auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
            if (left) label->setAnchorPoint({ 0.f, 0.5f });
            label->limitLabelWidth(width, scale, scale * 0.68f);
            label->setColor(color);
            label->setPosition(layoutPoint(x, y));
            m_sidebarLayer->addChild(label, 3);
            return label;
        };
        auto addConfigLabel = [&](char const* text, float x, float y, float width, float scale,
                                  ccColor3B color, bool left = false) {
            auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
            if (left) label->setAnchorPoint({ 0.f, 0.5f });
            label->limitLabelWidth(width, scale, scale * 0.68f);
            label->setColor(color);
            label->setPosition(layoutPoint(x, y));
            m_configLayer->addChild(label, 3);
            return label;
        };
        auto addRightLabel = [&](char const* text, float x, float y, float width, float scale,
                                 ccColor3B color) {
            auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
            label->setAnchorPoint({ 1.f, 0.5f });
            label->limitLabelWidth(width, scale, scale * 0.68f);
            label->setColor(color);
            label->setPosition(layoutPoint(x, y));
            m_configLayer->addChild(label, 3);
            return label;
        };

        // Keep the level visible between two opaque, independent side panels.
        auto* dimmer = CCLayerColor::create(ccc4(13, 6, 20, 96), WIDTH, HEIGHT);
        dimmer->ignoreAnchorPointForPosition(false);
        dimmer->setPosition(layoutPoint(WIDTH / 2.f, HEIGHT / 2.f));
        m_mainLayer->addChild(dimmer, -12);
        constexpr float LEFT_EDGE = 188.f;
        constexpr float RIGHT_X = CONFIG_PANEL_X;
        constexpr float RIGHT_W = CONFIG_PANEL_W;
        if (auto* panel = NeonPanel::create(LEFT_EDGE, HEIGHT - 32.f, 1)) {
            panel->setPosition(layoutPoint(94.f, 340.f));
            m_sidebarLayer->addChild(panel, -8);
        }
        if (auto* panel = NeonPanel::create(RIGHT_W, HEIGHT - 32.f, -1)) {
            panel->setPosition(layoutPoint(RIGHT_X, 340.f));
            m_configLayer->addChild(panel, -8);
        }
        addRoundedPlate(m_sidebarLayer, 94.f, 64.f, LEFT_EDGE - 16.f, 96.f, ccc4(43, 19, 44, 255), -6);
        addRoundedPlate(m_configLayer, RIGHT_X, 108.f, RIGHT_W - 24.f, 88.f, ccc4(64, 27, 63, 255), -6);  // header card
        addPlate(94.f, 114.f, LEFT_EDGE - 24.f, 1.f, ccc4(235, 157, 209, 100), -5);

        if (auto* halo = studio.fxLevel >= 1 ? CCSprite::create("sparkle.png"_spr) : nullptr) {
            halo->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
            halo->setColor(themeColor({ 190, 90, 255 }));
            halo->setOpacity(120);
            halo->setScale(2.4f);
            halo->setPosition(layoutPoint(42.f, 62.f));
            m_sidebarLayer->addChild(halo, 2);
            if (studio.fxLevel >= 2) halo->runAction(CCRepeatForever::create(CCSequence::create(
                CCSpawn::create(CCFadeTo::create(1.3f, 190), CCEaseSineInOut::create(CCScaleTo::create(1.3f, 2.8f)), nullptr),
                CCSpawn::create(CCFadeTo::create(1.3f, 90), CCEaseSineInOut::create(CCScaleTo::create(1.3f, 2.25f)), nullptr), nullptr)));
        }
        if (auto* logo = CCSprite::create("synca-mark.png"_spr)) {
            logo->setScale(1.5f);
            logo->setPosition(layoutPoint(42.f, 62.f));
            m_sidebarLayer->addChild(logo, 3);
            if (studio.fxLevel >= 2) pulse(logo, 1.5f);
        }
        if (auto* sparkle = studio.fxLevel >= 2 ? CCSprite::create("sparkle.png"_spr) : nullptr) {
            sparkle->setColor({ 255, 187, 226 });
            sparkle->setOpacity(190);
            sparkle->setScale(0.35f);
            sparkle->setPosition(layoutPoint(166.f, 34.f));
            m_sidebarLayer->addChild(sparkle, 4);
            sparkle->runAction(CCRepeatForever::create(CCSequence::create(
                CCSpawn::create(CCFadeTo::create(0.7f, 245), CCEaseSineInOut::create(CCScaleTo::create(0.7f, 0.48f)), nullptr),
                CCSpawn::create(CCFadeTo::create(0.7f, 105), CCEaseSineInOut::create(CCScaleTo::create(0.7f, 0.32f)), nullptr),
                nullptr)));
        }

        if (auto* wordmark = CCSprite::create("synca-wordmark.png"_spr)) {
            wordmark->setAnchorPoint({ 0.f, 0.5f });
            wordmark->setScale(std::min(0.78f, 259.f / std::max(1.f, wordmark->getContentSize().width)));   // 60% bigger than 162 / 0.49
            wordmark->setPosition(layoutPoint(84.f, 56.f));
            m_sidebarLayer->addChild(wordmark, 3);
            if (studio.fxLevel >= 2) wordmark->runAction(CCRepeatForever::create(CCSequence::create(CCFadeTo::create(1.6f, 215), CCFadeTo::create(1.6f, 255), nullptr)));
        }
        else {
            addLabel("Synca", 78.f, 54.f, 98.f, 1.0f, { 255, 238, 249 }, true);
        }
        auto* subtitle = CCLabelBMFont::create("INPUT BUFFER STUDIO", "chatFont.fnt");
        subtitle->setAnchorPoint({ 0.f, 0.5f });
        subtitle->limitLabelWidth(150.f, 0.56f, 0.40f);
        subtitle->setColor({ 222, 184, 255 });
        subtitle->setAnchorPoint({ 0.5f, 0.5f });
        subtitle->setPosition(layoutPoint(94.f, 100.f));
        m_sidebarLayer->addChild(subtitle, 3);

        addConfigLabel("SETTINGS", 888.f, 82.f, 120.f, 0.40f, { 255, 157, 220 }, true);

        addConfigLabel("STAGE", 880.f, 38.f, 48.f, 0.42f, { 214, 171, 201 }, true);
        addRoundedPlate(m_configLayer, 1020.f, 38.f, 96.f, 34.f, ccc4(76, 28, 66, 255), 1);
        m_stage = CCLabelBMFont::create("", "bigFont.fnt");
        m_stage->setColor({ 255, 238, 249 });
        m_stage->limitLabelWidth(82.f, 0.54f, 0.42f);
        m_stage->setPosition(layoutPoint(1020.f, 38.f));
        m_configLayer->addChild(m_stage, 3);

        auto* prev = actionItem("<", menu_selector(BufferMenu::onStage), -1, 32.f, 32.f, ccc4(98, 34, 78, 255));
        auto* next = actionItem(">", menu_selector(BufferMenu::onStage), 1, 32.f, 32.f, ccc4(98, 34, 78, 255));
        prev->setPosition(layoutPoint(940.f, 38.f));
        next->setPosition(layoutPoint(1100.f, 38.f));
        m_configButtonMenu->addChild(prev);
        m_configButtonMenu->addChild(next);

        // Scrollbars (they follow the list or page they belong to; only the visible one is shown).
        auto makeScrollbar = [&](ScrollLayer* target, CCNode* parent, float x, float y) -> CCNode* {
#if SYNCA_HAS_SCROLLBAR
            auto* bar = Scrollbar::create(target);
            if (!bar) return nullptr;
            bar->ignoreAnchorPointForPosition(false);
            bar->setAnchorPoint({ 0.5f, 0.5f });
            bar->setPosition(layoutPoint(x, y));
            parent->addChild(bar, 6);
            return bar;
#else
            (void)target; (void)parent; (void)x; (void)y;
            return nullptr;
#endif
        };
        m_makeScrollbar = makeScrollbar;

        static constexpr char const* TAB_NAMES[TAB_COUNT] = { "Basic", "Smart", "Visuals", "Gamemodes", "Orbs", "Keybinds", "Graph",
            "Path", "Studio", "Assist", "Straight fly", "Frame spam", "Fun" };
        static constexpr char const* TAB_ICONS[TAB_COUNT] = {
            "nav-basic.png"_spr, "nav-smart.png"_spr, "nav-visuals.png"_spr,
            "nav-gamemodes.png"_spr, "nav-orbs.png"_spr, "nav-keybinds.png"_spr, "nav-graph.png"_spr,
            "nav-path.png"_spr, "nav-studio.png"_spr, "nav-assist.png"_spr,
            "nav-straight.png"_spr, "nav-spam.png"_spr, "nav-fun.png"_spr
        };
        addLabel("SECTIONS", 22.f, 136.f, 142.f, 0.46f, { 198, 155, 187 }, true);

        // The tab list scrolls (mouse wheel, drag, or the scrollbar), so there is room for as many tabs as needed.
        m_tabScroll = ScrollLayer::create({ TAB_VIEW_W, TAB_VIEW_H });
        if (!m_tabScroll) {
            return false;
        }
        m_tabScroll->setStealingTouches(true);
        m_tabScroll->ignoreAnchorPointForPosition(false);
        m_tabScroll->setAnchorPoint({ 0.5f, 0.5f });
        m_tabScroll->m_contentLayer->setContentSize({ TAB_VIEW_W, tabContentHeight() });
        m_tabScroll->setPosition(layoutPoint(94.f, 364.f));
        m_sidebarLayer->addChild(m_tabScroll, 5);
        auto* tabContent = m_tabScroll->m_contentLayer;
        m_tabMenu = CCMenu::create();
        m_tabMenu->setContentSize(tabContent->getContentSize());
        m_tabMenu->ignoreAnchorPointForPosition(false);
        m_tabMenu->setAnchorPoint({ 0.f, 0.f });
        m_tabMenu->setPosition({ 0.f, 0.f });
        tabContent->addChild(m_tabMenu, 10);
        {
            static constexpr char const* GROUP_NAMES[GROUP_COUNT] = { "GENERAL", "TUNING", "ASSISTS", "MORE" };
            for (int g = 0; g < GROUP_COUNT; ++g) {
                float hy = headerCenterY(g);
                auto* name = CCLabelBMFont::create(GROUP_NAMES[g], "bigFont.fnt");
                name->setAnchorPoint({ 0.f, 0.5f });
                name->limitLabelWidth(90.f, 0.40f, 0.30f);
                name->setColor(themeColor({ 214, 168, 255 }));
                name->setOpacity(210);
                name->setPosition({ 12.f, hy });
                tabContent->addChild(name, 2);
                auto* rule = CCLayerColor::create(ccc4(214, 168, 255, 70), TAB_VIEW_W - 24.f - 80.f, 1.f);
                rule->ignoreAnchorPointForPosition(false);
                rule->setAnchorPoint({ 0.f, 0.5f });
                rule->setPosition({ 12.f + std::min(92.f, name->getScaledContentSize().width + 8.f), hy });
                tabContent->addChild(rule, 2);
            }
        }
        m_tabs.assign(TAB_COUNT, nullptr);
        m_tabHighlights.assign(TAB_COUNT, nullptr);
        m_tabIcons.assign(TAB_COUNT, nullptr);
        m_tabLabels.assign(TAB_COUNT, nullptr);
        for (int position = 0; position < TAB_COUNT; ++position) {
            int t = tabAtPosition(position);
            CCPoint center = tabCenter(t);
            CCNode* highlight = NeonPill::create(TAB_VIEW_W - 8.f, TAB_PITCH - 8.f);
            highlight->setPosition(center);
            highlight->setVisible(false);
            tabContent->addChild(highlight, 1);
            m_tabHighlights[t] = highlight;

            auto* icon = CCSprite::create(TAB_ICONS[t]);
            if (icon) {
                icon->setScale(1.05f);
                icon->setPosition({ 30.f, center.y });
                tabContent->addChild(icon, 2);
            }
            m_tabIcons[t] = icon;
            if (t == 1 && icon) {
                if (auto* starGlow = studio.fxLevel >= 2 ? CCSprite::create("sparkle.png"_spr) : nullptr) {
                    starGlow->setColor({ 255, 218, 117 });
                    starGlow->setOpacity(70);
                    starGlow->setScale(0.46f);
                    starGlow->setPosition({ 30.f, center.y });
                    tabContent->addChild(starGlow, 3);
                    starGlow->runAction(CCRepeatForever::create(CCSequence::create(
                        CCSpawn::create(CCFadeTo::create(0.70f, 190),
                            CCEaseSineInOut::create(CCScaleTo::create(0.70f, 0.62f)), nullptr),
                        CCSpawn::create(CCFadeTo::create(0.70f, 55),
                            CCEaseSineInOut::create(CCScaleTo::create(0.70f, 0.44f)), nullptr),
                        nullptr)));
                }
            }

            auto* label = CCLabelBMFont::create(TAB_NAMES[t], "bigFont.fnt");
            label->setAnchorPoint({ 0.f, 0.5f });
            label->limitLabelWidth(TAB_VIEW_W - 64.f, 0.72f, 0.46f);
            label->setColor({ 194, 164, 187 });
            label->setPosition({ 56.f, center.y });
            tabContent->addChild(label, 2);
            m_tabLabels[t] = label;

            auto* hit = CCLayerColor::create(ccc4(0, 0, 0, 0), TAB_VIEW_W - 8.f, TAB_PITCH - 8.f);
            auto* hitSelected = CCLayerColor::create(ccc4(0, 0, 0, 0), TAB_VIEW_W - 8.f, TAB_PITCH - 8.f);
            auto* tab = CCMenuItemSpriteExtra::create(hit, hitSelected, this, menu_selector(BufferMenu::onNavTab));
            disableMenuItemAnimation(tab);
            tab->setTag(t);
            tab->setContentSize({ TAB_VIEW_W - 8.f, TAB_PITCH - 8.f });
            tab->setPosition(center);
            m_tabMenu->addChild(tab);
            m_tabs[t] = tab;
        }
        m_tabScroll->scrollToTop();
        addScrollbar(m_tabScroll, m_sidebarLayer, 181.f, 364.f, TAB_VIEW_H);

        m_configTitle = CCLabelBMFont::create("", "bigFont.fnt");
        m_configTitle->setAnchorPoint({ 0.f, 0.5f });
        m_configTitle->setColor({ 255, 238, 249 });
        m_configTitle->limitLabelWidth(RIGHT_W - 64.f, 0.66f, 0.46f);
        m_configTitle->setPosition(layoutPoint(888.f, 106.f));
        m_configLayer->addChild(m_configTitle, 3);
        m_status = CCLabelBMFont::create("", "chatFont.fnt");
        m_status->setAnchorPoint({ 0.f, 0.5f });
        m_status->limitLabelWidth(RIGHT_W - 64.f, 0.52f, 0.36f);
        m_status->setColor({ 204, 157, 192 });
        m_status->setPosition(layoutPoint(888.f, 134.f));
        m_configLayer->addChild(m_status, 3);

        auto addConfigCaption = [&](char const* text, float x, float y, float width, float scale = 0.38f) {
            return addConfigLabel(text, x, y, width, scale, { 189, 150, 179 }, true);
        };
        auto addConfigValue = [&](char const* text, float x, float y, float width, float scale = 0.46f) {
            return addRightLabel(text, x, y, width, scale, { 255, 225, 244 });
        };
        addRoundedPlate(m_configLayer, RIGHT_X, 606.f, RIGHT_W - 24.f, 98.f, ccc4(50, 23, 52, 255), 2);
        addConfigCaption("LIVE STATUS", 888.f, 570.f, 130.f, 0.42f);
        addConfigCaption("BUFFER", 888.f, 589.f, 56.f, 0.36f);
        m_cfgStage = addConfigValue("PER MODE", 1152.f, 589.f, 94.f, 0.42f);
        m_cfgTiming = addConfigLabel("WAVE 6t   ROBOT 6t   DASH 6t", 888.f, 609.f,
            280.f, 0.46f, { 215, 234, 243 }, true);
        m_cfgTps = addConfigLabel("TPS 240   CBF ON   MEMORY ON   HEAT ON", 888.f, 628.f,
            280.f, 0.42f, { 189, 212, 225 }, true);
        m_cfgTelemetry = addConfigLabel("0 saves / 0 spots", 888.f, 646.f, 270.f, 0.42f,
            { 169, 199, 214 }, true);

        m_scroll = ScrollLayer::create({ SETTINGS_W, SETTINGS_HEIGHT });
        if (!m_scroll) {
            return false;
        }
        m_scroll->setStealingTouches(true);
        m_scroll->ignoreAnchorPointForPosition(false);
        m_scroll->setAnchorPoint({ 0.5f, 0.5f });
        m_scroll->m_contentLayer->setLayout(ColumnLayout::create()
                ->setGap(9.f)
                ->setAxisReverse(true)
                ->setAxisAlignment(AxisAlignment::End)
                ->setAutoGrowAxis(m_scroll->getContentHeight()));
        m_scroll->setPosition(layoutPoint(CONFIG_PANEL_X, 350.f));
        m_configLayer->addChild(m_scroll, 5);

        m_customScroll = ScrollLayer::create({ SETTINGS_W, SETTINGS_HEIGHT });
        if (!m_customScroll) {
            return false;
        }
        m_customScroll->setStealingTouches(true);
        m_customScroll->ignoreAnchorPointForPosition(false);
        m_customScroll->setAnchorPoint({ 0.5f, 0.5f });
        m_customScroll->m_contentLayer->setContentSize({ SETTINGS_W, MENU_HEIGHT });
        m_customScroll->setPosition(layoutPoint(CONFIG_PANEL_X, 350.f));
        m_configLayer->addChild(m_customScroll, 5);
        addScrollbar(m_scroll, m_configLayer, 1172.f, 350.f, SETTINGS_HEIGHT);
        addScrollbar(m_customScroll, m_configLayer, 1172.f, 350.f, SETTINGS_HEIGHT);
        setMouseEnabled(true);   // our own wheel handling (below) drives all three lists
        for (auto* list : { m_tabScroll, m_scroll, m_customScroll }) {
            if (list) list->setMouseEnabled(false);
        }
        schedule(schedule_selector(BufferMenu::onScrollbarUpdate));

        m_allSearchInput = geode::TextInput::create(SETTINGS_W - 16.f, "Search settings...", "bigFont.fnt");
        m_allSearchInput->setCommonFilter(geode::CommonFilter::Any);
        m_allSearchInput->setMaxCharCount(48);
        m_allSearchInput->setTextAlign(geode::TextInputAlign::Left);
        m_allSearchInput->setScale(0.96f);
        m_allSearchInput->getBGSprite()->setVisible(false);
        {
            auto inputSize = m_allSearchInput->getContentSize();
            if (auto* plate = gelPanel(inputSize.width, std::max(25.f, inputSize.height - 4.f), ccc4(24, 14, 36, 255), 8.f, false)) {
                plate->setPosition({ inputSize.width / 2.f, inputSize.height / 2.f });
                m_allSearchInput->addChild(plate, -1);
            }
        }
        m_allSearchInput->getInputNode()->setLabelNormalColor({ 240, 225, 255 });
        m_allSearchInput->getInputNode()->setLabelPlaceholderColor({ 150, 120, 185 });
        m_allSearchInput->setPosition(layoutPoint(CONFIG_PANEL_X, 134.f));
        m_allSearchInput->setVisible(true);
        m_allSearchInput->setEnabled(true);
        m_allSearchInput->setCallback([this](std::string const& query) {
            applySearch(query);
        });
        m_configLayer->addChild(m_allSearchInput, 6);
        // the forecast: what the typed text would most likely become (click it to take it)
        m_searchHint = CCLabelBMFont::create("", "chatFont.fnt");
        m_searchHint->setColor({ 214, 168, 255 });
        m_searchHintItem = CCMenuItemLabel::create(m_searchHint, this, menu_selector(BufferMenu::onSearchHint));
        m_searchHintItem->setAnchorPoint({ 1.f, 0.5f });
        m_searchHintItem->setPosition(layoutPoint(1150.f, 106.f));
        m_searchHintItem->setScale(0.62f);
        m_searchHintItem->setVisible(false);
        m_configButtonMenu->addChild(m_searchHintItem);

        m_customPage = CCNode::create();
        m_customPage->setContentSize({ SETTINGS_W, MENU_HEIGHT });
        m_customPage->setAnchorPoint({ 0.f, 0.f });
        m_customPage->setPosition({ 0.f, 0.f });
        m_customScroll->m_contentLayer->addChild(m_customPage);

        auto* helpButton = actionItem("HELP", menu_selector(BufferMenu::onHelp), 0,
            96.f, 42.f, ccc4(74, 33, 67, 255));
        helpButton->setPosition(layoutPoint(94.f, 610.f));
        m_buttonMenu->addChild(helpButton);

        auto* close = actionItem("X", menu_selector(BufferMenu::onClose), 0, 38.f, 34.f, ccc4(71, 33, 57, 255));
        close->setPosition(layoutPoint(1150.f, 38.f));
        m_configButtonMenu->addChild(close);

        m_helpOverlay = CCLayerColor::create(ccc4(12, 6, 20, 248), WIDTH, HEIGHT);
        m_helpOverlay->ignoreAnchorPointForPosition(false);
        m_helpOverlay->setAnchorPoint({ 0.f, 0.f });
        m_helpOverlay->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(m_helpOverlay, 200);

        auto* helpPanel = roundedPanel(840.f, 520.f, ccc4(34, 16, 39, 255));
        helpPanel->setPosition(layoutPoint(WIDTH / 2.f, HEIGHT / 2.f));
        m_helpOverlay->addChild(helpPanel);

        m_helpTitle = CCLabelBMFont::create("TAB HELP", "bigFont.fnt");
        m_helpTitle->setColor({ 255, 238, 249 });
        m_helpTitle->setScale(0.90f);
        m_helpTitle->setPosition(layoutPoint(WIDTH / 2.f, 128.f));
        m_helpOverlay->addChild(m_helpTitle);

        m_helpText = CCLabelBMFont::create("", "chatFont.fnt");
        m_helpText->setAnchorPoint({ 0.f, 1.f });
        m_helpText->setColor({ 244, 222, 238 });
        m_helpText->limitLabelWidth(760.f, 0.94f, 0.78f);
        m_helpText->setPosition(layoutPoint(210.f, 190.f));
        m_helpOverlay->addChild(m_helpText);

        m_helpMenu = CCMenu::create();
        m_helpMenu->setContentSize({ WIDTH, HEIGHT });
        m_helpMenu->ignoreAnchorPointForPosition(false);
        m_helpMenu->setAnchorPoint({ 0.f, 0.f });
        m_helpMenu->setPosition({ 0.f, 0.f });
        m_helpOverlay->addChild(m_helpMenu, 1);
        auto* helpX = actionItem("X", menu_selector(BufferMenu::onHelpClose), 0, 42.f, 42.f, ccc4(71, 33, 57, 255));
        helpX->setPosition(layoutPoint(950.f, 112.f));
        m_helpMenu->addChild(helpX);
        m_helpOverlay->setVisible(false);

        selectTab(static_cast<int>(Mod::get()->getSavedValue<int64_t>("studio-last-tab", 0)));   // opens on the tab you used last
        buildInfoPanels();
        themeLabels(m_mainLayer);
        refresh();
        schedule(schedule_selector(BufferMenu::onPeriodicUpdate), 1.f);
        schedule(schedule_selector(BufferMenu::onFastRefresh), 0.15f);   // light: follows stage changes made in the pause menu
        return true;
    }

    void keyDown(enumKeyCodes key, double p1) override {
        if (m_captureKeyIndex >= 0) {
            acceptCapturedKey(key);
            return;
        }
        if (key == KEY_Escape) {
            if (m_helpOpen) onHelpClose(nullptr);
            else onClose(nullptr);
            return;
        }
        CCLayer::keyDown(key, p1);
    }

    // Applies the two panel sizes. The left panel grows from its left edge and the right panel from its right edge, both about the
    // middle of the screen height, so each stays against its side of the screen.
    void applyPanelScales(bool place) {
        if (!m_sidebarLayer || !m_configLayer) return;
        m_sidebarLayer->setScale(m_leftScale);
        m_configLayer->setScale(m_rightScale);
        // v1.11: both panels grow from their bottom outer corner, so the drag handle (at the bottom) can never leave the screen
        m_sidebarHome = CCPoint{ 0.f, 0.f };
        m_configOpenPosition = CCPoint{ MENU_WIDTH * (1.f - m_rightScale), 0.f };
        if (place && !m_closing) {
            m_sidebarLayer->setPosition(m_sidebarHome);
            m_configLayer->setPosition(m_configOpenPosition);
        }
    }

    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, kCCMenuHandlerPriority + 1, true);
    }

public:
    void show() {
        m_mainLayer->stopAllActions();
        m_buttonMenu->stopAllActions();
        if (m_sidebarLayer) m_sidebarLayer->stopAllActions();
        m_configLayer->stopAllActions();
        auto win = CCDirector::get()->getWinSize();
        float scale = std::min(win.width / MENU_WIDTH, win.height / MENU_HEIGHT);
        m_scaleX = scale;
        m_scaleY = scale;
        m_mainLayer->setScaleX(m_scaleX);
        m_mainLayer->setScaleY(m_scaleY);
        m_openPosition = CCPoint{ (win.width - MENU_WIDTH * scale) / 2.f,
            (win.height - MENU_HEIGHT * scale) / 2.f };
        m_mainLayer->setPosition(m_openPosition);
        applyPanelScales(false);
        if (m_sidebarLayer) {
            m_sidebarLayer->setPosition({ -SIDEBAR_WIDTH * m_leftScale, m_sidebarHome.y });
            m_sidebarLayer->runAction(CCEaseBackOut::create(
                CCMoveTo::create(0.42f, m_sidebarHome)));
        }
        m_configLayer->setPosition({ m_configOpenPosition.x + CONFIG_SLIDE_DISTANCE, m_configOpenPosition.y });
        m_configLayer->runAction(CCEaseInOut::create(
            CCMoveTo::create(0.36f, m_configOpenPosition), 3.f));
    }

    void onClose(CCObject*) {
        if (m_closing) {
            return;   // already closing: another Escape / back press must not restart the animation
        }
        m_closing = true;
        stopMenuIntro();
        setInputEnabled(false);
        if (m_helpOverlay) m_helpOverlay->setVisible(false);
        m_helpOpen = false;
        m_configLayer->stopAllActions();
        if (m_sidebarLayer) {
            m_sidebarLayer->stopAllActions();
            m_sidebarLayer->runAction(CCEaseInOut::create(
                CCMoveTo::create(0.34f, { -SIDEBAR_WIDTH * m_leftScale, m_sidebarHome.y }), 3.f));
        }
        m_configLayer->runAction(CCSequence::create(
            CCEaseInOut::create(CCMoveTo::create(0.34f,
                { m_configOpenPosition.x + CONFIG_SLIDE_DISTANCE, m_configOpenPosition.y }), 3.f),
            CCCallFunc::create(this, callfunc_selector(BufferMenu::finishClose)), nullptr));
    }

    static constexpr float MENU_WIDTH = 1180.f;
    static constexpr float MENU_HEIGHT = 680.f;
    static constexpr float PATH_PAGE_HEIGHT = 560.f;
    static constexpr float STUDIO_PAGE_HEIGHT = 3104.f;
    static constexpr float MODE_PAGE_HEIGHT = 700.f;
    static constexpr float KEY_PAGE_HEIGHT = 780.f;
    static constexpr float ORB_PAGE_HEIGHT = 800.f;
    static constexpr float ALT_PAGE_HEIGHT = 1578.f;
    static constexpr float GAP_PAGE_HEIGHT = 700.f;
    static constexpr float SPAM_PAGE_HEIGHT = 600.f;
    static constexpr float FUN_PAGE_HEIGHT = 1120.f;
    static constexpr float GRAPH_PAGE_HEIGHT = 760.f;
    static constexpr int TAB_COUNT = 13;
    static constexpr float TAB_PITCH = 58.f;     // each tab is taller than before
    static constexpr float TAB_HEADER = 26.f;    // the small heading above each group of tabs
    static constexpr float TAB_VIEW_W = 160.f;
    static constexpr float TAB_VIEW_H = 424.f;
    static constexpr float SIDEBAR_WIDTH = 188.f;
    static constexpr float CONFIG_PANEL_X = 1020.f;
    static constexpr float CONFIG_PANEL_W = 320.f;
    static constexpr float CONFIG_SLIDE_DISTANCE = 320.f;
    static constexpr float SETTINGS_W = 292.f;
    static constexpr float SETTINGS_HEIGHT = 390.f;

    static BufferMenu* create() {
        auto* ret = new BufferMenu();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    static void open() {
        if (s_instance) {
            if (s_instance->m_closing) s_instance->reopen();
            else s_instance->onClose(nullptr);
            return;
        }
        auto* scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        if (auto* menu = create()) {
            s_instance = menu;
            scene->addChild(menu, 9999);
            playMenuIntro();
            menu->show();
        }
    }
};

#include "studio_ui.inc"
#include "graph_ui.inc"

void openMenu() {
    BufferMenu::open();
}

MenuTheme currentMenuTheme() {
    auto theme = BufferMenu::currentTheme();
    return { theme.main, theme.soft, theme.accent, theme.bg };
}

}  // namespace synca
