// Synca hooks: end screen, pause menu and the mod load hook.
#include "engine.hpp"

using namespace synca;

// v2.46: the level complete screen says so if the buffer was on during the attempt.
class $modify(BufEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        if (!buf.usedThisAttempt) {
            return;
        }
        // v1.13: the top right text follows "Hide watermark" (same rule as the watermark: still shown at a custom TPS)
        if (Mod::get()->getSettingValue<bool>("hide-watermark") && buf.tps == 240) {
            benchFinish();
            benchFlush();
            endAttempt();
            return;
        }
        auto win = CCDirector::get()->getWinSize();
        auto* label = CCLabelBMFont::create("Synca was on", "bigFont.fnt");
        label->setScale(0.35f);
        label->setColor({ 150, 205, 255 });
        label->setOpacity(170);
        label->setAnchorPoint({ 1.f, 1.f });
        label->setPosition({ win.width - 6.f, win.height - 4.f });
        this->addChild(label, 100);
        if (attemptStats.saves > 0) {
            auto* stats = CCLabelBMFont::create(attemptSummary().c_str(), "bigFont.fnt");
            stats->setScale(0.28f);
            stats->setColor({ 255, 190, 225 });
            stats->setOpacity(170);
            stats->setAnchorPoint({ 1.f, 1.f });
            stats->setPosition({ win.width - 6.f, win.height - 16.f });
            this->addChild(stats, 100);
        }
        benchFinish();  // a completed run is the best recording there is
        benchFlush();
        endAttempt();
    }
};

class $modify(BufPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        // v1.6: stage readout with < > buttons, bottom right of the pause menu, so the buffer stage can be changed without the hotkey
        if (Mod::get()->getSettingValue<bool>("show-pause-button")) {   // same switch as the Synca button
            auto win = CCDirector::get()->getWinSize();
            auto* stageMenu = CCMenu::create();
            stageMenu->setPosition({ 0.f, 0.f });
            auto* label = CCLabelBMFont::create(stageShortText().c_str(), "chatFont.fnt");
            label->setScale(0.7f);
            label->setAnchorPoint({ 1.f, 0.5f });
            label->setPosition({ win.width - 130.f, 22.f });
            label->setTag(8801);
            this->addChild(label, 10);
            for (int dir : { -1, 1 }) {
                auto* arrow = CCLabelBMFont::create(dir < 0 ? "<" : ">", "bigFont.fnt");
                arrow->setScale(0.6f);
                auto* item = CCMenuItemSpriteExtra::create(arrow, this, menu_selector(BufPauseLayer::onStageStep));
                item->setTag(dir < 0 ? 1 : 2);
                item->setPosition({ win.width - (dir < 0 ? 100.f : 70.f), 22.f });
                stageMenu->addChild(item);
            }
            this->addChild(stageMenu, 10);
            this->schedule(schedule_selector(BufPauseLayer::updateStageLabel), 0.1f);   // follows the Synca menu in real time
        }
        if (!Mod::get()->getSettingValue<bool>("show-pause-button")) {
            return;
        }
        constexpr float BUTTON_W = 42.f;
        constexpr float BUTTON_H = 42.f;
        auto makeButton = [](bool pressed) -> CCNode* {
            auto* body = CCNode::create();
            if (!body) return nullptr;
            body->setContentSize({ BUTTON_W, BUTTON_H });
            body->ignoreAnchorPointForPosition(false);
            body->setAnchorPoint({ 0.5f, 0.5f });
            if (pressed) {
                if (auto* glow = gelPanel(BUTTON_W, BUTTON_H, ccc4(150, 56, 128, 150), 12.f)) {
                    glow->setPosition({ BUTTON_W / 2.f, BUTTON_H / 2.f });
                    body->addChild(glow);
                }
            }
            if (auto* mark = CCSprite::create("synca-mark.png"_spr)) {
                auto iconSize = mark->getContentSize();
                float fitScale = std::min((BUTTON_W - 4.f) / iconSize.width,
                    (BUTTON_H - 4.f) / iconSize.height);
                mark->setScale(fitScale);
                mark->setPosition({ BUTTON_W / 2.f, BUTTON_H / 2.f });
                body->addChild(mark);
            }
            return body;
        };
        auto* button = CCMenuItemSpriteExtra::create(makeButton(false),
            makeButton(true), this, menu_selector(BufPauseLayer::onBufferMenu));
        button->m_animationEnabled = false;
        button->m_scaleMultiplier = 1.f;
        button->setScale(1.f);
        button->setContentSize({ BUTTON_W, BUTTON_H });
        auto* menu = CCMenu::create();
        menu->addChild(button);
        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({ win.width - BUTTON_W / 2.f - 18.f, BUTTON_H / 2.f + 10.f });
        this->addChild(menu, 10);
    }

    void updateStageLabel(float) {
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(this->getChildByTag(8801))) {
            auto text = stageShortText();
            if (text != label->getString()) {
                label->setString(text.c_str());
            }
        }
    }

    void onStageStep(CCObject* sender) {
        cycleStage(sender->getTag() == 1 ? -1 : 1);
        updateStageLabel(0.f);
    }

    void onBufferMenu(CCObject*) {
        openMenu();
    }
};

$on_mod(Loaded) {
    buf.console = Mod::get()->getSettingValue<bool>("debug-console");
    recoverGlobals();
    // Studio: a fresh install starts on the per-gamemode Custom stage, and the toggle key remembers where to go back to
    buf.stage = std::clamp(static_cast<int>(Mod::get()->getSavedValue<int64_t>("stage", STUDIO_STAGE_CUSTOM)), 0, STAGE_COUNT - 1);
    studio.resumeStage = std::clamp(static_cast<int>(Mod::get()->getSavedValue<int64_t>("resume-stage", STUDIO_STAGE_CUSTOM)), 0, STAGE_COUNT - 2);
    if (buf.stage != STUDIO_STAGE_OFF) {
        studio.resumeStage = buf.stage;
    }
    // v2.62: per-level settings off (switched off once): every level uses the same switches, like v2.43.1
    if (!Mod::get()->getSavedValue<bool>("per-level-v262", false)) {
        Mod::get()->setSettingValue<bool>("per-level-settings", false);
        Mod::get()->setSavedValue("per-level-v262", true);
    }
    // v2.60: low latency is off for every level (switched off once for existing installs too;
    // it's one setting for all levels, not saved per level)
    if (!Mod::get()->getSavedValue<bool>("low-latency-v260", false)) {
        Mod::get()->setSettingValue<bool>("low-latency", false);
        Mod::get()->setSavedValue("low-latency-v260", true);
    }
    // v1.11.2: "My delays on every stage" was on by default for a few versions and made the stage switch look stuck on PER MODE; reset it once
    if (!Mod::get()->getSavedValue<bool>("stage-bypass-reset-v1", false)) {
        Mod::get()->setSavedValue("studio-stage-bypass", false);
        Mod::get()->setSavedValue("stage-bypass-reset-v1", true);
    }
    readSettings();
    buf.showPath = studio.pathEnabled;
    applyStage();
#ifdef GEODE_IS_WINDOWS
    if (buf.console) {
        AllocConsole();
        FILE* out = nullptr;
        freopen_s(&out, "CONOUT$", "w", stdout);
        SetConsoleTitleA("Synca - Live Console");
    }
#endif
    if (Loader::get()->isModLoaded("syzzi.click_between_frames")) {
        say("Click Between Frames detected: mid-tick sync {}", buf.cbfSync ? "on" : "off");
    }
    if (Loader::get()->isModLoaded("square3ang.cbfextrapolate")) {
        say("CBF Extrapolate detected: visual only, predictions unaffected");
    }
    say("Synca {} loaded (engine v2.88). The stage key cycles: 0.5f -> 1f -> 2f -> 4f -> 6f -> 8f -> custom (per gamemode) -> off",
        Mod::get()->getVersion().toVString());
}
