// Synca hooks: PlayLayer and the small object hooks.
#include "engine.hpp"

using namespace synca;

// The drag handle in the corner of the F9 terminal: dragging it left to right changes the terminal's size.
class TerminalResizeHandle : public CCLayer {
public:
    CCPoint m_from = CCPointZero;
    float m_startScale = 1.f;
    bool m_dragging = false;

    static TerminalResizeHandle* create() {
        auto* ret = new TerminalResizeHandle();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCLayer::init()) return false;
        this->setContentSize({ 18.f, 18.f });
        this->setTouchEnabled(true);
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, -600, true);
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        for (CCNode* node = this; node; node = node->getParent()) {
            if (!node->isVisible()) return false;   // the terminal is hidden: nothing to grab
        }
        auto local = this->convertTouchToNodeSpace(touch);
        auto size = this->getContentSize();
        if (local.x < -8.f || local.x > size.width + 8.f || local.y < -8.f || local.y > size.height + 8.f) return false;
        m_dragging = true;
        m_from = touch->getLocation();
        m_startScale = studio.terminalScale;
        return true;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (!m_dragging) return;
        float dx = touch->getLocation().x - m_from.x;
        studio.terminalScale = std::clamp(m_startScale + dx / 260.f, 0.6f, 1.8f);
        applyHudLayout();
    }

    void ccTouchEnded(CCTouch*, CCEvent*) override {
        if (m_dragging) {
            Mod::get()->setSavedValue("studio-terminal-scale", static_cast<double>(studio.terminalScale));
        }
        m_dragging = false;
    }

    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override {
        ccTouchEnded(touch, event);
    }
};

class $modify(BufPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", HOOK_PRIORITY);
        (void)self.setHookPriority("PlayLayer::playEndAnimationToPos", HOOK_PRIORITY);
        (void)self.setHookPriority("PlayLayer::pauseGame", HOOK_PRIORITY);
    }

    void pauseGame(bool paused) {
        PlayLayer::pauseGame(paused);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (buf.simulating && isGhost(player)) {
            if (buf.recordThroughHits && buf.recordPathCollision) {
                if (*buf.recordPathCollision < 0 && buf.recordPath) {
                    *buf.recordPathCollision = static_cast<int>(buf.recordPath->size());
                }
                return;
            }
            buf.ghostDied = true;
            // v2.30 diagnostics: remember the first thing that kills the press-now ghost
            if (buf.captureKiller && !buf.killerSeen) {
                buf.killerSeen = true;
                buf.killerTick = buf.simTick;
                buf.ghostDeathRect = player->getObjectRect();
                if (object) {
                    buf.killerId = object->m_objectID;
                    buf.killerType = static_cast<int>(object->m_objectType);
                    buf.killerNoTouch = object->m_isNoTouch;
                    buf.killerDisabled = object->m_isDisabled;
                    buf.killerRect = object->getObjectRect();
                }
                else {
                    buf.killerId = 0;
                    buf.killerType = -1;
                }
            }
            return;
        }
        // v2.34: let the game (and noclip / other mods) decide first, and only reset if you
        // really died. With noclip this is called every tick you're inside a spike; resetting
        // before it marked the button released each tick, the next tick pressed it again to
        // match you, and a held button turned into 100-200 clicks a second.
        bool wasDead = player && player->m_isDead;
        PlayLayer::destroyPlayer(player, object);
        if (!player || wasDead || !player->m_isDead) {
            return;
        }
        adaptiveDeath(this->getCurrentPercent());
        altDeathReport(this->getCurrentPercent());
        deathSnapshot(this, this->getCurrentPercent());
        funOnDeath(this->getCurrentPercent());
        // v2.84: if the game still has the button down (a delayed release that hadn't fired
        // yet, or an auto press), let go now. Dropping the queue alone left the game holding,
        // so the next attempt started with the icon holding by itself until you clicked.
        // If you really are still holding, it's pressed again once the attempt starts.
        if (buf.appliedInput && buf.owner == this) {
            sendButton(this, false);
        }
        refreshAutoTag();
        resetPending();
        buf.appliedInput = false;
        buf.autoReleased = false;
        buf.autoClicked = false;
        buf.swallowRelease = false;
        buf.rapid = 0;
        resetAlternateRhythm();
        forgetPress();
        memoryDeath();
        benchFinish();
        refreshHeatmap();
        endAttempt();
        finishDrift(" (died)");
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }
        dropGhosts();
        readSettings();
        applyStage();
        exportLines.clear();
        logClock = 0;
        loadMemory(level);
        if (statsKey != memoryKey) {
            statsKey = memoryKey;
            sessionStats = {};
        }
        attemptStats = {};
        benchNeedStart = true;
        benchRecording = false;
        benchBest = {};
        benchBestDirty = false;
        benchBestTicks = benchSavedTicks();
        loadProfile();
        adaptiveLoad(memoryKey);
        scanLevel(this);
        perfGuard = {};
        if (buf.saveDing) {
            FMODAudioEngine::sharedEngine()->preloadEffect("counter003.ogg");  // v2.58: no hitch on the first ding
        }
        buf.ghost1 = makeGhost(this);
        buf.ghost2 = makeGhost(this);
        buf.ghostLayerBase = buf.ghostLayer->getChildrenCount();

        // In-game console (F9): hidden unless the player opens it.
        constexpr float W = PANEL_W;
        constexpr float H = PANEL_H;
        auto win = CCDirector::get()->getWinSize();
        auto consoleTheme = currentMenuTheme();
        buf.panel = CCNode::create();
        buf.panel->setPosition({ 8.f, win.height - H - 8.f });
        buf.panel->setZOrder(1000);

        // Same look as the menu: a rounded gradient panel with a glossy header band.
        auto* frame = gelPanel(W, H, ccc4(36, 16, 42, 244), 12.f);
        frame->setPosition({ W / 2.f, H / 2.f });
        buf.panel->addChild(frame);
        auto* header = gelPanel(W - 12.f, 19.f, ccc4(172, 60, 140, 255), 8.f);
        header->setPosition({ W / 2.f, H - 15.5f });
        buf.panel->addChild(header);

        if (auto* logo = CCSprite::create("synca-mark.png"_spr)) {
            logo->setScale(0.20f);
            logo->setPosition({ 16.f, H - 15.f });
            logo->runAction(CCRepeatForever::create(CCSequence::create(
                CCEaseSineInOut::create(CCScaleTo::create(1.25f, 0.212f)),
                CCEaseSineInOut::create(CCScaleTo::create(1.25f, 0.20f)), nullptr)));
            buf.panel->addChild(logo);
        }
        auto* title = CCLabelBMFont::create("SYNCA // TERMINAL", "bigFont.fnt");
        title->setScale(0.30f);
        title->setAnchorPoint({ 0.f, 0.5f });
        title->setPosition({ 29.f, H - 15.f });
        title->setColor(consoleTheme.soft);
        buf.panel->addChild(title);

        auto* version = CCLabelBMFont::create(Mod::get()->getVersion().toVString().c_str(), "chatFont.fnt");
        version->setScale(0.34f);
        version->setOpacity(210);
        version->setAnchorPoint({ 1.f, 0.5f });
        version->setPosition({ W - 8.f, H - 15.f });
        buf.panel->addChild(version);

        {
            if (auto* handle = TerminalResizeHandle::create()) {
                handle->setPosition({ W - 22.f, 4.f });
                if (auto* body = gelPanel(16.f, 16.f, ccc4(214, 110, 255, 235), 6.f)) {
                    body->setPosition({ 9.f, 9.f });
                    handle->addChild(body);
                }
                if (auto* grip = CCLabelBMFont::create("::", "bigFont.fnt")) {
                    grip->setScale(0.28f);
                    grip->setRotation(-45.f);
                    grip->setPosition({ 9.f, 9.f });
                    handle->addChild(grip, 1);
                }
                buf.panel->addChild(handle, 6);
            }
            applyHudLayout();
        }

        buf.panelTitle = CCLabelBMFont::create("", "bigFont.fnt");
        buf.panelTitle->setScale(0.42f);
        buf.panelTitle->setColor(consoleTheme.accent);
        buf.panelTitle->setAnchorPoint({ 0.f, 0.5f });
        buf.panelTitle->setPosition({ 8.f, H - 39.f });
        buf.panel->addChild(buf.panelTitle);

        buf.panelPerf = CCLabelBMFont::create("", "chatFont.fnt");
        buf.panelPerf->setScale(0.36f);
        buf.panelPerf->setColor(consoleTheme.soft);
        buf.panelPerf->setAnchorPoint({ 0.f, 0.5f });
        buf.panelPerf->setPosition({ 8.f, H - 50.f });
        buf.panel->addChild(buf.panelPerf);

        buf.panelLegend.clear();
        struct LegendEntry { char const* label; ccColor3B color; float x; };
        LegendEntry legend[] = {
            { "PATH", consoleTheme.accent, 8.f },
            { "NOW", { 255, 118, 112 }, 42.f },
            { "BUF", { 255, 185, 229 }, 72.f },
            { "FIRE", { 255, 219, 140 }, 101.f },
            { "DEATH", { 255, 117, 147 }, 133.f },
            { "MEM", { 255, 191, 133 }, 178.f },
        };
        for (auto const& entry : legend) {
            auto* tag = CCLabelBMFont::create(entry.label, "chatFont.fnt");
            tag->setScale(0.28f);
            tag->setColor(entry.color);
            tag->setAnchorPoint({ 0.f, 0.5f });
            tag->setPosition({ entry.x, H - 61.f });
            buf.panel->addChild(tag);
            buf.panelLegend.push_back(tag);
        }

        buf.panelLines.clear();
        for (size_t i = 0; i < PANEL_LINES; ++i) {
            auto* line = CCLabelBMFont::create("", "chatFont.fnt");
            line->setScale(0.34f);
            line->setAnchorPoint({ 0.f, 0.5f });
            line->setPosition({ 8.f, H - 70.f - i * 8.f });
            buf.panel->addChild(line);
            buf.panelLines.push_back(line);
        }

        // v2.79: the hint shows the keys you set
        auto* mod = Mod::get();
        auto keyName = [mod](char const* key) {
            auto binds = mod->getSettingValue<std::vector<Keybind>>(key);
            return binds.empty() ? std::string("-") : binds.front().toString();
        };
        auto hintText = fmt::format("{} COPY LOG    {} CONSOLE", keyName("bind-export"), keyName("bind-console"));
        auto* hint = CCLabelBMFont::create(hintText.c_str(), "chatFont.fnt");
        hint->limitLabelWidth(W - 16.f, 0.28f, 0.20f);
        hint->setColor({ 235, 190, 221 });
        hint->setAnchorPoint({ 0.5f, 0.5f });
        hint->setPosition({ W / 2.f, 6.f });
        buf.panel->addChild(hint);

        this->addChild(buf.panel);
        refreshPanel();

        buf.watermark = CCLabelBMFont::create("", "bigFont.fnt");
        buf.watermark->setScale(0.28f);
        buf.watermark->setColor({ 150, 205, 255 });
        buf.watermark->setOpacity(153);
        buf.watermark->setAnchorPoint({ 0.f, 0.f });
        buf.watermark->setPosition({ 6.f, 4.f });
        buf.watermark->setZOrder(1000);
        this->addChild(buf.watermark);
        buf.usedThisAttempt = false;
        refreshWatermark();

        buf.orbDot = CCDrawNode::create();
        buf.orbDot->setZOrder(1000);
        this->addChild(buf.orbDot);
        buf.orbConf = 0;
        buf.orbConfAge = 100000;
        buf.orbConfShown = -1;
        applyHudLayout();

        buf.autoTag = CCLabelBMFont::create("AUTO INPUTS ON", "bigFont.fnt");
        buf.autoTag->setScale(0.24f);
        buf.autoTag->setColor({ 255, 120, 120 });
        buf.autoTag->setOpacity(153);  // 60%, like the console; still readable in a recording
        buf.autoTag->setAnchorPoint({ 1.f, 0.f });
        buf.autoTag->setPosition({ win.width - 6.f, 4.f });
        buf.autoTag->setZOrder(1000);
        this->addChild(buf.autoTag);
        refreshAutoTag();

        buf.nextModeLabel = CCLabelBMFont::create("", "bigFont.fnt");
        buf.nextModeLabel->setScale(0.42f);
        buf.nextModeLabel->setOpacity(215);
        buf.nextModeLabel->setColor({ 210, 240, 255 });
        buf.nextModeLabel->setAnchorPoint({ 0.5f, 1.f });
        buf.nextModeLabel->setPosition({ win.width / 2.f, win.height - 24.f });
        buf.nextModeLabel->setZOrder(1000);
        buf.nextModeLabel->setVisible(false);
        this->addChild(buf.nextModeLabel);

        buf.funHud = CCLabelBMFont::create("", "bigFont.fnt");
        buf.funHud->setScale(0.26f);
        buf.funHud->setOpacity(215);
        buf.funHud->setColor({ 215, 234, 243 });
        buf.funHud->setAnchorPoint({ 0.f, 1.f });
        buf.funHud->setAlignment(kCCTextAlignmentLeft);
        buf.funHud->setPosition({ 8.f, win.height - 74.f });
        buf.funHud->setZOrder(1000);
        buf.funHud->setVisible(false);
        this->addChild(buf.funHud);
        buf.funFlash = CCLabelBMFont::create("", "goldFont.fnt");
        buf.funFlash->setScale(0.7f);
        buf.funFlash->setPosition({ win.width / 2.f, win.height * 0.74f });
        buf.funFlash->setZOrder(1001);
        buf.funFlash->setVisible(false);
        this->addChild(buf.funFlash);
        funReset();
        hazardsScanned = false;

        buf.hintLabel = CCLabelBMFont::create("", "bigFont.fnt");
        buf.hintLabel->setColor({ 255, 170, 215 });
        buf.hintLabel->setOpacity(0);
        buf.hintLabel->setPosition({ win.width / 2.f, win.height - 40.f });
        buf.hintLabel->setZOrder(1000);
        this->addChild(buf.hintLabel);

        buf.heatNode = nullptr;
        if (m_progressBar) {
            buf.heatNode = CCDrawNode::create();
            m_progressBar->addChild(buf.heatNode, 5);
            refreshHeatmap();
        }

        // Path view lives in the object layer, so it scrolls with the level.
        buf.pathNode = CCDrawNode::create();
        buf.keepNode = CCDrawNode::create();
        buf.keepTick = -1000000;
        // v2.48: above the level's shader effects when it has that layer; inside the shader
        // layer, dark or glow shaders could hide the path completely.
        (m_aboveShaderObjectLayer ? m_aboveShaderObjectLayer : m_objectLayer)->addChild(buf.keepNode, 9998);
        (m_aboveShaderObjectLayer ? m_aboveShaderObjectLayer : m_objectLayer)->addChild(buf.pathNode, 9999);

        buf.realInput = false;
        buf.appliedInput = false;
        buf.isPlayer1 = true;
        buf.sinceInput = 999;
        buf.rapid = 0;
        forgetPress();
        buf.owner = this;
        return true;
    }

    void onQuit() {
        benchFinish();
        benchFlush();
        benchRecording = false;
        saveMemory();
        // v2.58: changes made in the settings popup during the level are kept for this level;
        // before, leaving the level silently put the old values back.
        saveProfile();
        adaptiveSave(memoryKey);
        restoreGlobals();
        buf.hintLabel = nullptr;
        buf.heatNode = nullptr;
        memoryKey.clear();
        dropGhosts();
        PlayLayer::onQuit();
    }

    void resetLevel() {
        adaptive.lastBucket = -1;
        funOnAttempt();
        buf.attemptTick = tickCount;
        buf.lastAppliedEdgeTick = -1000000;
        benchFinish();
        benchNeedStart = true;
        endAttempt();
        buf.usedThisAttempt = false;
        // v2.84: same on a restart (R, checkpoints, practice): never start an attempt with the
        // game holding a button you already let go of
        // v2.86: the game also restarts the attempt while the level is still loading; sending
        // a button then (v2.84/v2.85) crashed when you left a level and opened one again
        if (buf.owner != this) {
            PlayLayer::resetLevel();
            return;
        }
        if (buf.appliedInput && !buf.realInput) {
            sendButton(this, false);
        }
        resetPending();
        // A restart/checkpoint is a new attempt even if destroyPlayer never ran.
        // Drop the old phase samples so the path learns this attempt's actual cadence.
        resetAlternateRhythm();
        buf.rapid = 0;
        buf.sinceInput = 999;
        forgetPress();
        refreshAutoTag();
        if (buf.pathNode) {
            buf.pathNode->clear();
        }
        if (buf.keepNode) {
            buf.keepNode->clear();
            buf.keepTick = -1000000;
        }
        buf.pathHoldUntil = 0;
        buf.lastPathUpdate = {};
        buf.autoReleased = false;
        buf.autoClicked = false;
        buf.swallowRelease = false;
        drift.active = false;
        PlayLayer::resetLevel();
        // v2.85: start every attempt from the game's own button state. A release you made while
        // your icon was dead never reached the mod, so it still thought you were holding and
        // pressed for you as the attempt started (the cube jumped by itself until you clicked).
        // Now the mod only presses when you press.
        syncHeld(this);
    }

    void togglePracticeMode(bool practiceMode) {
        PlayLayer::togglePracticeMode(practiceMode);
        refreshWatermark();
        refreshAutoTag();
    }

    void playEndAnimationToPos(CCPoint position) {
        if (buf.simulating) {
            return;
        }
        PlayLayer::playEndAnimationToPos(position);
    }

    // v2.79: the level's own circle effect (orb / pad hits) is never spawned by a ghost
    void spawnCircle() {
        if (buf.simulating) {
            return;
        }
        PlayLayer::spawnCircle();
    }
};

class $modify(BufEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool noUI) {
        bool ok = LevelEditorLayer::init(level, noUI);
        dropGhosts();
        return ok;
    }
};

class $modify(BufHardStreak, HardStreak) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("HardStreak::addPoint", HOOK_PRIORITY);
    }

    void addPoint(CCPoint point) {
        if (buf.simulating) {
            return;
        }
        HardStreak::addPoint(point);
    }
};

class $modify(BufEffectGameObject, EffectGameObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("EffectGameObject::triggerObject", HOOK_PRIORITY);
    }

    void triggerObject(GJBaseGameLayer* layer, int uniqueID, gd::vector<int> const* remapKeys) {
        if (buf.simulating) {
            return;
        }
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }

    // v2.79: visual only, never from a ghost
    void playTriggerEffect() {
        if (buf.simulating) {
            return;
        }
        EffectGameObject::playTriggerEffect();
    }
};

class $modify(BufGameObject, GameObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GameObject::playShineEffect", HOOK_PRIORITY);
    }

    void playShineEffect() {
        if (buf.simulating) {
            return;
        }
        GameObject::playShineEffect();
    }

    // v2.79: visual only, never from a ghost
    void playDestroyObjectAnim(GJBaseGameLayer* layer) {
        if (buf.simulating) {
            return;
        }
        GameObject::playDestroyObjectAnim(layer);
    }
};

// v2.79: an orb the ghost uses while trying ticks played its "used" animation on the real
// level (seen on dash orbs before you reached them). The state is restored afterwards
// anyway, so ghosts skip the visuals.
class $modify(BufRingObject, RingObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("RingObject::powerOnObject", HOOK_PRIORITY);
        (void)self.setHookPriority("RingObject::spawnCircle", HOOK_PRIORITY);
    }

    void powerOnObject(int state) {
        if (buf.simulating) {
            return;
        }
        RingObject::powerOnObject(state);
    }

    void spawnCircle() {
        if (buf.simulating) {
            return;
        }
        RingObject::spawnCircle();
    }

    void triggerActivated(float xPosition) {
        if (buf.simulating) {
            return;
        }
        RingObject::triggerActivated(xPosition);
    }
};
