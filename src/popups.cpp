// Synca first-run experience and the update popup.
//
//  * First launch (after the title screen): four slides, a recreation of a modern app welcome sheet:
//      1 welcome, 2 menu look (clean / normal / full), 3 buffer preset (safe / balanced / aggressive / performance / own),
//      4 social links. Closing it plays the lightning intro and then opens the Synca menu.
//  * No other start-up popups (the old welcome / what's new alerts are gone).
//  * Update popup: when a newer GitHub release exists, it offers to download and install it by itself (see UPDATING.md).

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif
#include "synca.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <Geode/modify/MenuLayer.hpp>
#include <chrono>
#include <cmath>
#include <random>
#include <string>

using namespace geode::prelude;

namespace {

constexpr char const* SOCIAL_KEYS[4] = { "social-github", "social-youtube", "social-tiktok", "social-discord" };
constexpr char const* SOCIAL_NAMES[4] = { "GITHUB", "YOUTUBE", "TIKTOK", "DISCORD" };

CCMenuItemSpriteExtra* makeButton(char const* text, float w, float h, bool primary, CCObject* target, SEL_MenuHandler selector, int tag = 0) {
    auto* body = CCNode::create();
    body->setContentSize({ w, h });
    body->ignoreAnchorPointForPosition(false);
    body->setAnchorPoint({ 0.5f, 0.5f });
    if (auto* panel = synca::gelPanel(w, h, primary ? ccc4(150, 56, 220, 255) : ccc4(52, 26, 78, 255), h * 0.36f, false)) {
        panel->setPosition({ w / 2.f, h / 2.f });
        body->addChild(panel);
    }
    auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->limitLabelWidth(w - 12.f, std::min(0.5f, h / 52.f), 0.2f);
    label->setPosition({ w / 2.f, h / 2.f + 1.f });
    body->addChild(label, 2);
    auto* item = CCMenuItemSpriteExtra::create(body, nullptr, target, selector);
    item->setTag(tag);
    return item;
}

// ---------------------------------------------------------------------------------------------------------
// The lightning intro: three strikes, the last one lands in the middle of the screen, and then the menu opens.
// ---------------------------------------------------------------------------------------------------------
void playSyncaSound(char const* file) {
    FMODAudioEngine::sharedEngine()->playEffect(Mod::get()->expandSpriteName(file).c_str());
}

class LightningIntro : public CCLayerColor {
    CCDrawNode* m_bolts = nullptr;
    CCLayerColor* m_flash = nullptr;
    CCSprite* m_mark = nullptr;
    float m_markScale = 1.f;
    float m_time = 0.f;
    int m_strike = 0;
    bool m_opened = false;
    std::mt19937 m_rng { std::random_device{}() };

    float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(m_rng); }

    void jag(CCPoint a, CCPoint b, float spread, int depth, std::vector<CCPoint>& out) {
        if (depth == 0) { out.push_back(b); return; }
        CCPoint mid = { (a.x + b.x) / 2.f + rnd(-spread, spread), (a.y + b.y) / 2.f + rnd(-spread * 0.4f, spread * 0.4f) };
        jag(a, mid, spread / 2.f, depth - 1, out);
        jag(mid, b, spread / 2.f, depth - 1, out);
    }

    void drawBolt(CCPoint from, CCPoint to, float width, int branches) {
        std::vector<CCPoint> points { from };
        jag(from, to, 38.f * width / 3.f, 5, points);
        for (size_t i = 1; i < points.size(); ++i) {
            m_bolts->drawSegment(points[i - 1], points[i], width * 3.2f, synca::neonPremul({ 0.72f, 0.38f, 1.f, 0.30f }));
            m_bolts->drawSegment(points[i - 1], points[i], width, synca::neonPremul({ 1.f, 0.95f, 1.f, 1.f }));
        }
        for (int b = 0; b < branches; ++b) {
            size_t at = static_cast<size_t>(rnd(4.f, static_cast<float>(points.size() - 6)));
            CCPoint start = points[at];
            CCPoint end = { start.x + rnd(-90.f, 90.f), start.y - rnd(40.f, 110.f) };
            drawBolt(start, end, width * 0.55f, 0);
        }
    }

    void strike(bool last) {
        auto win = CCDirector::get()->getWinSize();
        m_bolts->clear();
        CCPoint target = last ? CCPoint { win.width / 2.f, win.height / 2.f }
                              : CCPoint { win.width / 2.f + rnd(-120.f, 120.f), win.height * rnd(0.25f, 0.5f) };
        drawBolt({ win.width / 2.f + rnd(-70.f, 70.f), win.height + 10.f }, target, last ? 3.4f : 2.4f, last ? 4 : 2);
        m_flash->setOpacity(last ? 255 : 190);
        m_flash->stopAllActions();
        m_flash->runAction(CCFadeTo::create(last ? 0.5f : 0.28f, 0));
        if (!last) playSyncaSound("synca-lightning.ogg");   // one sound for each of the two lightning strikes
        if (last) playSyncaSound("synca-appearing.ogg");   // the logo appears
        if (last && m_mark) {
            m_mark->setVisible(true);
            m_mark->setScale(m_markScale * 0.1f);
            m_mark->runAction(CCEaseBackOut::create(CCScaleTo::create(0.35f, m_markScale)));
        }
    }

public:
    static void play() {
        auto* scene = CCDirector::get()->getRunningScene();
        if (!scene) {
            synca::openMenu();
            return;
        }
        auto* layer = new LightningIntro();
        if (layer->initWithColor(ccc4(8, 4, 18, 0))) {
            layer->autorelease();
            scene->addChild(layer, 9500);
            layer->build();
        }
        else {
            delete layer;
            synca::openMenu();
        }
    }

    void build() {
        auto win = CCDirector::get()->getWinSize();
        this->setTouchEnabled(true);
        this->runAction(CCFadeTo::create(0.25f, 210));   // the room goes dark before the first strike
        m_bolts = CCDrawNode::create();
        this->addChild(m_bolts, 2);
        m_flash = CCLayerColor::create(ccc4(235, 215, 255, 0), win.width, win.height);
        this->addChild(m_flash, 5);
        m_mark = CCSprite::create("synca-logo-2k.png"_spr);
        if (m_mark) {
            m_markScale = win.height * 0.55f / std::max(1.f, m_mark->getContentSize().height);
            m_mark->setPosition({ win.width / 2.f, win.height / 2.f });
            m_mark->setVisible(false);
            this->addChild(m_mark, 3);
        }
        this->scheduleUpdate();
    }

    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, kCCMenuHandlerPriority + 2, true);
    }

    bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }

    void update(float dt) override {
        m_time += dt;
        static constexpr float TIMES[3] = { 0.30f, 0.62f, 0.92f };
        if (m_strike < 3 && m_time >= TIMES[m_strike]) {
            strike(m_strike == 2);
            ++m_strike;
        }
        if (m_strike == 3 && !m_opened && m_time >= 1.18f) {
            m_opened = true;
            synca::openMenu();   // the menu opens under the fading lightning (it starts the menu music itself)
        }
        if (m_opened && m_time >= 1.18f + 0.1f && !m_fading) {
            m_fading = true;
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (child == m_flash) continue;
                if (auto* rgba = typeinfo_cast<CCNodeRGBA*>(child)) rgba->runAction(CCFadeOut::create(0.5f));
            }
            m_bolts->runAction(CCFadeOut::create(0.4f));
            this->runAction(CCSequence::create(CCFadeTo::create(0.5f, 0), CCCallFunc::create(this, callfunc_selector(LightningIntro::finish)), nullptr));
        }
    }

    bool m_fading = false;

    void finish() {
        this->unscheduleUpdate();
        this->removeFromParentAndCleanup(true);
    }
};

// ---------------------------------------------------------------------------------------------------------
// The welcome slides.
// ---------------------------------------------------------------------------------------------------------
class OnboardingLayer : public CCLayerColor {
    static constexpr int SLIDES = 4;
    int m_slide = 0;
    int m_fx = 1;        // 0 clean, 1 normal, 2 full
    int m_preset = 1;    // 0 safe, 1 balanced, 2 aggressive, 3 performance, 4 own (keep my settings)
    CCNode* m_content = nullptr;
    CCDrawNode* m_dots = nullptr;
    CCSprite* m_dotGlow = nullptr;
    CCMenu* m_menu = nullptr;
    CCMenu* m_slideMenu = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_choices;
    CCNode* m_choiceGlow = nullptr;

public:
    static OnboardingLayer* create() {
        auto* ret = new OnboardingLayer();
        if (ret->initWithColor(ccc4(13, 9, 22, 255)) && ret->setup()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, kCCMenuHandlerPriority + 1, true);
    }

    bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }

    bool setup() {
        auto win = CCDirector::get()->getWinSize();
        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        m_fx = std::clamp(synca::studio.fxLevel, 0, 2);

        m_dots = CCDrawNode::create();
        this->addChild(m_dots, 3);
        m_dotGlow = CCSprite::create("synca-dot-glow.png"_spr);
        if (m_dotGlow) {
            m_dotGlow->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
            m_dotGlow->setColor({ 190, 120, 255 });
            m_dotGlow->setScale(54.f / std::max(1.f, m_dotGlow->getContentSize().width));
            m_dotGlow->runAction(CCRepeatForever::create(CCSequence::create(CCFadeTo::create(0.9f, 255), CCFadeTo::create(0.9f, 150), nullptr)));
            this->addChild(m_dotGlow, 2);
        }
        m_content = CCNode::create();
        this->addChild(m_content, 2);
        {
            CCPoint at { win.width * 0.25f, win.height * 0.50f };
            if (auto* halo = CCSprite::create("synca-star-2k.png"_spr)) {   // the star behind the logo, breathing with it
                float base = win.height * 0.98f / std::max(1.f, halo->getContentSize().height);
                halo->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
                halo->setColor({ 196, 130, 255 });
                halo->setOpacity(170);
                halo->setPosition(at);
                halo->setScale(base);
                halo->runAction(CCRepeatForever::create(CCSequence::create(
                    CCSpawn::create(CCFadeTo::create(1.1f, 240), CCEaseSineInOut::create(CCScaleTo::create(1.1f, base * 1.08f)), nullptr),
                    CCSpawn::create(CCFadeTo::create(1.1f, 120), CCEaseSineInOut::create(CCScaleTo::create(1.1f, base * 0.93f)), nullptr), nullptr)));
                this->addChild(halo, 1);
            }
            if (auto* logo = CCSprite::create("synca-logo-2k.png"_spr)) {
                float base = win.height * 0.56f / std::max(1.f, logo->getContentSize().height);
                logo->setScale(base);
                logo->setPosition(at);
                logo->runAction(CCRepeatForever::create(CCSequence::create(
                    CCEaseSineInOut::create(CCScaleTo::create(0.9f, base * 1.08f)),
                    CCEaseSineInOut::create(CCScaleTo::create(0.9f, base * 0.96f)), nullptr)));
                this->addChild(logo, 2);
            }
        }

        m_menu = CCMenu::create();
        m_menu->setPosition({ 0.f, 0.f });
        this->addChild(m_menu, 10);
        auto* skip = makeButton("SKIP", 52.f, 18.f, false, this, menu_selector(OnboardingLayer::onSkip));
        skip->setPosition({ win.width - 38.f, win.height - 20.f });
        m_menu->addChild(skip);
        m_next = makeButton("NEXT", 100.f, 26.f, true, this, menu_selector(OnboardingLayer::onNext));
        m_next->setPosition({ win.width - 64.f, 26.f });
        m_menu->addChild(m_next);
        m_back = makeButton("BACK", 64.f, 20.f, false, this, menu_selector(OnboardingLayer::onBack));
        m_back->setPosition({ 44.f, 26.f });
        m_menu->addChild(m_back);

        showSlide(0);
        return true;
    }

    CCMenuItemSpriteExtra* m_next = nullptr;
    CCMenuItemSpriteExtra* m_back = nullptr;

    void keyBackClicked() override {}

    void drawDots() {
        auto win = CCDirector::get()->getWinSize();
        m_dots->clear();
        for (int i = 0; i < SLIDES; ++i) {
            bool on = i == m_slide;
            CCPoint at = { win.width / 2.f + (i - (SLIDES - 1) / 2.f) * 12.f, win.height - 20.f };
            if (on && m_dotGlow) m_dotGlow->setPosition(at);
            synca::neonDisc(m_dots, at, on ? 2.8f : 2.f, synca::neonPremul(on ? ccColor4F { 1.f, 1.f, 1.f, 1.f } : ccColor4F { 1.f, 1.f, 1.f, 0.28f }), 24);
        }
    }

    CCLabelBMFont* text(char const* value, float x, float y, float scale, ccColor3B color, bool big, float width = 0.f, bool center = false) {
        char const* font = big ? "bigFont.fnt" : "chatFont.fnt";
        std::string shown = width > 0.f ? synca::wrapText(font, value, scale, width) : std::string(value);
        CCLabelBMFont* label = CCLabelBMFont::create(shown.c_str(), font);
        if (center) label->setAlignment(kCCTextAlignmentCenter);
        label->setScale(scale);
        label->setColor(color);
        label->setAnchorPoint({ center ? 0.5f : 0.f, 1.f });
        label->setPosition({ x, y });
        m_content->addChild(label, 2);
        return label;
    }

    // The left illustration: glowing ribbons with depth (a dark copy offset behind each one, so they read as 3D), fading out at both
    // ends, with glossy beads riding on them and a short run of dashes underneath.
    void illustration(float cx, float cy) {
        auto* art = CCDrawNode::create();
        constexpr int STEPS = 60;
        struct Ribbon { float phase, amp, yOff, width; ccColor4F color; };
        Ribbon ribbons[] = {
            { 0.0f, 26.f, 10.f, 2.6f, { 0.72f, 0.40f, 1.f, 1.f } },
            { 1.1f, 20.f, -8.f, 2.0f, { 0.55f, 0.32f, 0.95f, 1.f } },
            { 2.2f, 30.f, -26.f, 1.5f, { 0.85f, 0.55f, 1.f, 1.f } },
            { 3.3f, 16.f, 28.f, 1.2f, { 0.5f, 0.3f, 0.9f, 1.f } },
        };
        float halfWidth = 125.f;
        std::vector<CCPoint> beadSpots;
        for (auto const& ribbon : ribbons) {
            CCPoint previous {};
            for (int i = 0; i <= STEPS; ++i) {
                float t = static_cast<float>(i) / STEPS;
                CCPoint point = { cx - halfWidth + t * halfWidth * 2.f,
                    cy + ribbon.yOff + std::sin(t * 6.2831f * 0.9f + ribbon.phase) * ribbon.amp * (0.55f + 0.45f * std::sin(t * 3.14159f)) };
                float fade = std::pow(std::sin(t * 3.14159f), 0.8f);   // fades to nothing at both ends
                if (i > 0) {
                    art->drawSegment({ previous.x + 5.f, previous.y - 6.f }, { point.x + 5.f, point.y - 6.f }, ribbon.width * 1.6f,
                        synca::neonPremul({ 0.12f, 0.04f, 0.28f, 0.7f * fade }));                       // the depth: a dark copy behind
                    art->drawSegment(previous, point, ribbon.width * 3.2f, synca::neonPremul({ ribbon.color.r, ribbon.color.g, ribbon.color.b, 0.16f * fade }));   // glow
                    art->drawSegment(previous, point, ribbon.width, synca::neonPremul({ ribbon.color.r, ribbon.color.g, ribbon.color.b, 0.95f * fade }));
                }
                if (i == 14 || i == 31 || i == 46) beadSpots.push_back(point);
                previous = point;
            }
        }
        for (auto const& spot : beadSpots) {   // beads: shadow, body, highlight
            synca::neonDisc(art, { spot.x + 3.f, spot.y - 4.f }, 6.f, synca::neonPremul({ 0.1f, 0.03f, 0.24f, 0.6f }), 14);
            synca::neonDisc(art, spot, 6.f, synca::neonPremul({ 0.65f, 0.35f, 1.f, 1.f }), 14);
            synca::neonDisc(art, { spot.x - 1.8f, spot.y + 2.f }, 2.4f, synca::neonPremul({ 1.f, 1.f, 1.f, 0.9f }), 10);
        }
        for (int row = 0; row < 3; ++row) {   // dashes that fade out to the right
            for (int piece = 0; piece < 6; ++piece) {
                float x0 = cx - 100.f + row * 16.f + piece * 17.f;
                float alpha = std::max(0.f, 1.f - piece / 6.f) * (row == 1 ? 0.5f : 0.95f);
                art->drawSegment({ x0 + 2.f, cy - 74.f - row * 9.f - 3.f }, { x0 + 11.f, cy - 74.f - row * 9.f - 3.f }, 1.8f, synca::neonPremul({ 0.1f, 0.03f, 0.24f, 0.6f * alpha }));
                art->drawSegment({ x0, cy - 74.f - row * 9.f }, { x0 + 9.f, cy - 74.f - row * 9.f }, 1.8f,
                    synca::neonPremul(row == 1 ? ccColor4F { 0.6f, 0.6f, 0.7f, alpha } : ccColor4F { 0.72f, 0.4f, 1.f, alpha }));
            }
        }
        m_content->addChild(art, 1);
    }

    void refreshChoices()
 {
        for (size_t i = 0; i < m_choices.size(); ++i) {
            int selected = m_slide == 1 ? m_fx : m_preset;
            auto* item = m_choices[i];
            item->setScale(static_cast<int>(i) == selected ? 1.08f : 0.92f);
        }
    }

    void onChoice(CCObject* sender) {
        int index = sender->getTag();
        if (m_slide == 1) m_fx = index;
        else m_preset = index;
        refreshChoices();
    }

    void onSocial(CCObject* sender) {
        int index = sender->getTag();
        std::string url = Mod::get()->getSettingValue<std::string>(SOCIAL_KEYS[index]);
        if (url.empty() && index == 0) {
            std::string repo = Mod::get()->getSettingValue<std::string>("update-repo");
            if (!repo.empty()) url = "https://github.com/" + repo;
        }
        if (url.empty()) {
            Notification::create(fmt::format("The {} link is not set yet (mod settings > Socials).", SOCIAL_NAMES[index]), NotificationIcon::Warning, 2.f)->show();
            return;
        }
        geode::utils::web::openLinkInBrowser(url);
    }

    void showSlide(int index) {
        auto win = CCDirector::get()->getWinSize();
        m_slide = std::clamp(index, 0, SLIDES - 1);
        m_content->removeAllChildrenWithCleanup(true);
        if (m_slideMenu) m_slideMenu->removeFromParentAndCleanup(true);
        m_slideMenu = CCMenu::create();
        m_slideMenu->setPosition({ 0.f, 0.f });
        m_content->addChild(m_slideMenu, 5);
        m_choices.clear();
        drawDots();
        m_back->setVisible(m_slide > 0);
        m_back->setEnabled(m_slide > 0);
        float tx = win.width * 0.53f;
        float top = win.height * 0.80f;
        float textW = win.width * 0.40f;
        float rowA = win.height * 0.30f;
        float rowB = rowA - 30.f;
        ccColor3B white { 255, 255, 255 };
        ccColor3B soft { 190, 176, 214 };
        if (m_slide == 0) {
            text("WELCOME", tx, top, 0.7f, white, true);
            text("TO SYNCA", tx, top - 30.f, 0.7f, white, true);
            auto* body = text("Synca is an input buffer for Geometry Dash. A hidden ghost of your icon tests every click, finds the safest moment for it, and shows you the route. Tune it for each gamemode, then let the assists handle the rest.",
                tx, top - 70.f, 0.55f, soft, false, textW);
            text("It is an assist tool: do not use it where assists are not allowed.", tx, top - 70.f - body->getScaledContentSize().height - 12.f, 0.46f, { 160, 140, 200 }, false, textW);
        }
        else if (m_slide == 1) {

            text("PICK YOUR", tx, top, 0.7f, white, true);
            text("MENU LOOK", tx, top - 30.f, 0.7f, white, true);
            text("Full adds glow, light rays and animation. The lightning intro plays whichever you choose. You can change this later in Studio.",
                tx, top - 70.f, 0.55f, soft, false, textW);
            static constexpr char const* NAMES[3] = { "CLEAN", "NORMAL", "FULL" };
            for (int i = 0; i < 3; ++i) {
                auto* item = makeButton(NAMES[i], 72.f, 24.f, true, this, menu_selector(OnboardingLayer::onChoice), i);
                item->setPosition({ tx + 36.f + i * 80.f, rowA });
                m_slideMenu->addChild(item);
                m_choices.push_back(item);
            }
            refreshChoices();
        }
        else if (m_slide == 2) {
            text("CHOOSE YOUR", tx, top, 0.7f, white, true);
            text("PRESET", tx, top - 30.f, 0.7f, white, true);
            text("Safe is careful, Balanced suits most levels, Aggressive uses the biggest delays, Performance is cheap to run. Own keeps the settings you have now.",
                tx, top - 70.f, 0.55f, soft, false, textW);
            static constexpr char const* NAMES[5] = { "SAFE", "BALANCED", "AGGRESSIVE", "PERFORMANCE", "OWN" };
            static constexpr float WIDTHS[5] = { 64.f, 78.f, 86.f, 92.f, 64.f };
            static constexpr float XS[5] = { 32.f, 110.f, 198.f, 46.f, 136.f };
            for (int i = 0; i < 5; ++i) {
                auto* item = makeButton(NAMES[i], WIDTHS[i], 24.f, true, this, menu_selector(OnboardingLayer::onChoice), i);
                item->setPosition({ tx + XS[i], i < 3 ? rowA : rowB });
                m_slideMenu->addChild(item);
                m_choices.push_back(item);
            }
            refreshChoices();
        }
        else {
            text("STAY IN", tx, top, 0.7f, white, true);
            text("TOUCH", tx, top - 30.f, 0.7f, white, true);
            text("Updates, tips and the people who make Synca better. Follow along or say hi.", tx, top - 70.f, 0.55f, soft, false, textW);
            for (int i = 0; i < 4; ++i) {
                auto* item = makeButton(SOCIAL_NAMES[i], 84.f, 24.f, true, this, menu_selector(OnboardingLayer::onSocial), i);
                item->setPosition({ tx + 42.f + (i % 2) * 94.f, i < 2 ? rowA : rowB });
                m_slideMenu->addChild(item);
            }
        }
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(findLabel(m_next))) label->setString(m_slide == SLIDES - 1 ? "GET STARTED" : "NEXT");
        m_content->setPositionX(24.f);
        m_content->runAction(CCEaseSineOut::create(CCMoveTo::create(0.28f, { 0.f, 0.f })));
    }

    static CCNode* findLabel(CCNode* node) {
        if (!node) return nullptr;
        if (typeinfo_cast<CCLabelBMFont*>(node)) return node;
        if (auto* kids = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(kids)) {
                if (auto* found = findLabel(child)) return found;
            }
        }
        return nullptr;
    }

    void onNext(CCObject*) {
        if (m_slide >= SLIDES - 1) finish(true);
        else showSlide(m_slide + 1);
    }

    void onBack(CCObject*) { showSlide(m_slide - 1); }
    void onSkip(CCObject*) { finish(false); }

    void finish(bool applyChoices) {
        auto* mod = Mod::get();
        if (applyChoices) {
            mod->setSavedValue("studio-fx-level", static_cast<int64_t>(m_fx));
            if (m_preset < 4) {
                synca::studioPresetApply(synca::studioBuiltinPreset(m_preset));
                synca::studioSetStage(synca::STUDIO_STAGE_CUSTOM);
            }
            synca::studioReadSettings();
        }
        mod->setSavedValue("studio-onboarded", true);
        mod->setSavedValue("studio-welcome-seen", true);
        mod->setSavedValue("studio-last-seen-version", mod->getVersion().toVString());
        this->removeFromParentAndCleanup(true);
        LightningIntro::play();
    }
};

bool onboardingQueued = false;

void showOnboardingIfNeeded() {
    auto* mod = Mod::get();
    if (!mod->getSavedValue<bool>("onboarding-layout-v7", false)) {   // the slides were redone: show them once more
        mod->setSavedValue("onboarding-layout-v7", true);
        mod->setSavedValue("studio-onboarded", false);
    }
    if (onboardingQueued || mod->getSavedValue<bool>("studio-onboarded", false)) return;
    onboardingQueued = true;
    queueInMainThread([] {
        auto* scene = CCScene::get();
        if (!scene || !scene->getChildByID("MenuLayer")) {
            onboardingQueued = false;
            return;
        }
        if (auto* layer = OnboardingLayer::create()) scene->addChild(layer, 9400);
    });
}

// ---------------------------------------------------------------------------------------------------------
// The update popup: a newer GitHub release exists -> offer to download and install it by itself.
// (Windows only, like the mod. The download runs through curl.exe, which ships with Windows 10 and 11, on a background thread:
//  the game's own async web classes crash the MSVC compiler in this project.)
// ---------------------------------------------------------------------------------------------------------
bool updateCheckStarted = false;

#ifdef GEODE_IS_WINDOWS
std::wstring widen(std::string const& text) {
    return std::filesystem::path(text).wstring();
}

// curl.exe -> a file; true when curl succeeded (exit code 0 and an HTTP status below 400)
bool downloadTo(std::string const& url, std::filesystem::path const& out, int seconds) {
    std::wstring cmd = L"curl.exe -sL -f -A Synca-update -m " + std::to_wstring(seconds) + L" -o \"" + out.wstring() + L"\" \"" + widen(url) + L"\"";
    STARTUPINFOW si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, static_cast<DWORD>((seconds + 5) * 1000));
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}
#endif

void installUpdate(std::string repo, std::string tag) {
#ifdef GEODE_IS_WINDOWS
    Notification::create("Downloading the Synca update...", NotificationIcon::Loading, 3.f)->show();
    std::string url = "https://github.com/" + repo + "/releases/latest/download/" + Mod::get()->getID() + ".geode";
    std::filesystem::path installed = Mod::get()->getPackagePath();
    std::filesystem::path downloaded = Mod::get()->getSaveDir() / "update.geode.tmp";
    std::thread([url, tag, installed, downloaded] {
        bool ok = downloadTo(url, downloaded, 120);
        std::string problem;
        if (!ok) {
            problem = "The download failed. Open the release page and install it by hand.";
        }
        else {
            std::ifstream in(downloaded, std::ios::binary);
            char magic[2] = { 0, 0 };
            in.read(magic, 2);
            in.close();
            std::error_code ec;
            auto size = std::filesystem::file_size(downloaded, ec);
            if (ec || size < 1024 || magic[0] != 'P' || magic[1] != 'K') {   // a .geode is a zip file
                problem = "That download is not a Synca package. Nothing was changed.";
            }
            else {
                std::filesystem::path staged = installed;
                staged += ".new";
                std::filesystem::copy_file(downloaded, staged, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) std::filesystem::rename(staged, installed, ec);   // staged copy + rename: an interrupted update never leaves a broken package
                if (ec) {
                    std::error_code ignore;
                    std::filesystem::remove(staged, ignore);
                    problem = "Could not replace the package. Install it by hand from the release page.";
                }
            }
        }
        std::error_code ignore;
        std::filesystem::remove(downloaded, ignore);
        queueInMainThread([problem, tag] {
            if (!problem.empty()) {
                Notification::create(problem, NotificationIcon::Error, 5.f)->show();
                return;
            }
            FLAlertLayer::create("Synca updated", fmt::format("Version {} is installed.\nRestart Geometry Dash to use it.", tag), "OK")->show();
        });
    }).detach();
#else
    (void)repo; (void)tag;
#endif
}

void checkForUpdate(bool force) {
#ifdef GEODE_IS_WINDOWS
    auto* mod = Mod::get();
    if (!force) {
        if (updateCheckStarted) return;
        updateCheckStarted = true;
        if (!mod->getSettingValue<bool>("check-updates")) return;
    }
    std::string repo = mod->getSettingValue<std::string>("update-repo");
    if (repo.empty() || repo.find('/') == std::string::npos) {
        if (force) Notification::create("Set 'Update source' (owner/repo) in the mod settings first.", NotificationIcon::Warning, 3.f)->show();
        return;
    }
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (!force) {
        if (now - mod->getSavedValue<int64_t>("studio-update-check-time", 0) < 6 * 3600) return;
        mod->setSavedValue("studio-update-check-time", now);
    }
    std::filesystem::path reply = mod->getSaveDir() / "latest-release.json";
    std::string current = mod->getVersion().toVString();
    std::thread([repo, force, reply, current] {
        bool ok = downloadTo("https://api.github.com/repos/" + repo + "/releases/latest", reply, 12);
        std::string body;
        if (ok) {
            std::ifstream in(reply, std::ios::binary);
            body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::error_code ignore;
        std::filesystem::remove(reply, ignore);
        queueInMainThread([repo, force, current, body, ok] {
            if (!ok) {
                if (force) Notification::create("Could not reach GitHub.", NotificationIcon::Error, 3.f)->show();
                return;
            }
            size_t key = body.find("\"tag_name\"");
            if (key == std::string::npos) return;
            size_t open = body.find('"', body.find(':', key) + 1);
            size_t close = open == std::string::npos ? open : body.find('"', open + 1);
            if (open == std::string::npos || close == std::string::npos) return;
            std::string tag = body.substr(open + 1, close - open - 1);
            auto latest = geode::VersionInfo::parse(tag);
            auto* mod = Mod::get();
            if (!latest.isOk() || !(latest.unwrap() > mod->getVersion())) {
                if (force) Notification::create("Synca is up to date.", NotificationIcon::Success, 2.f)->show();
                return;
            }
            if (!force && mod->getSavedValue<std::string>("studio-update-notified", "") == tag) return;
            mod->setSavedValue("studio-update-notified", tag);
            geode::createQuickPopup("Synca update available",
                fmt::format("Version <cy>{}</c> is out (you have {}).\n\n<cg>Update</c> downloads and installs it for you; restart the game afterwards.", tag, current),
                "Later", "Update", [repo, tag](FLAlertLayer*, bool update) {
                    if (update) installUpdate(repo, tag);
                });
        });
    }).detach();
#else
    (void)force;
#endif
}

}  // namespace

namespace synca {
// "REPLAY WELCOME" (Studio tab): closes the menu, then plays the welcome slides again (and the lightning, and the menu).
void replayWelcome() {
    struct Replayer : CCObject {
        void fire(float) {
            if (auto* scene = CCDirector::get()->getRunningScene()) {
                if (auto* layer = OnboardingLayer::create()) scene->addChild(layer, 9400);
            }
        }
    };
    static Replayer* replayer = new Replayer();
    openMenu();   // the menu is open (that is where the button is), so this closes it
    CCDirector::get()->getScheduler()->scheduleSelector(schedule_selector(Replayer::fire), replayer, 0.f, 0, 0.55f, false);
}

// The menu music: while the Synca menu is open the game's menu music fades out and intro.ogg fades in (looping); closing the menu
// fades them back. Never in a level (the pause menu opens the Synca menu there too).
namespace {
struct MenuMusic : CCObject {
    enum class State { Idle, In, Playing, Out } state = State::Idle;
    float level = 0.f;           // 0 = only the game's music, 1 = only ours
    int channelId = -1;
    float gdVolume = 1.f;
    bool gdSaved = false;
    bool scheduled = false;

    static MenuMusic& get() {
        static MenuMusic* instance = new MenuMusic();
        return *instance;
    }

    FMOD::Channel* channel() {
        return channelId >= 0 ? FMODAudioEngine::sharedEngine()->channelForChannelID(channelId) : nullptr;
    }

    void apply() {
        if (auto* c = channel()) c->setVolume(level * 0.85f);
        auto* group = FMODAudioEngine::sharedEngine()->m_backgroundMusicChannel;
        if (group && gdSaved) group->setVolume(gdVolume * (1.f - level));
    }

    void update(float dt) override {
        if (state == State::In) {
            level = std::min(1.f, level + dt / 0.8f);
            if (level >= 1.f) state = State::Playing;
        }
        else if (state == State::Out) {
            level = std::max(0.f, level - dt / 0.8f);
            if (level <= 0.f) {
                if (auto* c = channel()) c->setPaused(true);   // paused, not stopped: the next open continues from here
                state = State::Idle;
                level = 0.f;
                auto* group = FMODAudioEngine::sharedEngine()->m_backgroundMusicChannel;
                if (group && gdSaved) group->setVolume(gdVolume);
                gdSaved = false;
                CCDirector::get()->getScheduler()->unscheduleUpdateForTarget(this);
                scheduled = false;
                return;
            }
        }
        apply();
    }

    void open() {
        if (!Mod::get()->getSettingValue<bool>("menu-intro-sound") || GameManager::get()->m_playLayer) return;
        auto* engine = FMODAudioEngine::sharedEngine();
        if (!gdSaved) {
            if (auto* group = engine->m_backgroundMusicChannel) group->getVolume(&gdVolume);
            gdSaved = true;
        }
        if (auto* paused = channel()) paused->setPaused(false);
        if (channel() == nullptr) {
            channelId = engine->playEffect(Mod::get()->expandSpriteName("synca-intro.ogg").c_str(), 1.f, 0.f, 0.01f);
            if (auto* c = channel()) {
                c->setMode(FMOD_LOOP_NORMAL);
                c->setLoopCount(-1);
            }
        }
        state = State::In;
        if (!scheduled) {
            scheduled = true;
            CCDirector::get()->getScheduler()->scheduleUpdateForTarget(this, 0, false);
        }
    }

    void close() {
        if (state == State::In || state == State::Playing) state = State::Out;
    }
};
}  // namespace

void playMenuIntro() {
    MenuMusic::get().open();
}

void stopMenuIntro() {
    MenuMusic::get().close();
}

void checkForUpdateNow() {
    checkForUpdate(true);
}
}  // namespace synca

class $modify(BufMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }
        showOnboardingIfNeeded();
        checkForUpdate(false);
        return true;
    }
};
