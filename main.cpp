// Synca input buffer. Engine derived from "Click Before Frames" by BASU (github.com/raresdalas1009/-predictive-input-buffer).
// Source layout: main.cpp = engine + hooks, menu.cpp = menu, popups.cpp = popups, synca.hpp / synca_state.hpp = shared state;
// studio_actions.inc is included below, studio_ui.inc by menu.cpp (per-gamemode frames, alternate assist,
// toggle / panic keys, keybind menu). Credits and licensing notes are in NOTICE.md.
// Synca v2.9 (cube-style wave, wave auto-release, orb alignment, full ghost state, drift check, in-game console)
//
// When you click (or release, in wave / robot), a hidden "ghost" player is simulated
// ahead with your input. If the ghost dies, the input is retried with 1..N ticks of
// delay and, if a delayed version clearly survives longer, your input is held back
// and delivered that many physics ticks later. 1 tick = 1/240 s = 0.25 frames @ 60fps.
//
// v2 changes:
//  - Bigger windows: F6 stages up to 32 ticks (8 frames) plus a CUSTOM stage (settings).
//  - Smart prediction: ghost uses orbs, pads, gravity portals and speed portals; mode /
//    size / dual / mirror / teleport portals end the prediction as "safe".
//  - Input queue: every input arriving during a delay is shifted by the same amount,
//    so taps and hold lengths are preserved (no more swallowed clicks / 1-tick taps).
//  - Time budget per click so big windows don't hitch.
//  - Fixes: ghost lifetime, init using `this`, time-warp dt, F6 only when GD focused,
//    dead flipGravity hook priority, optional console, in-game console (stage + live log, hidden by default).

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/SliderNode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <map>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>
#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <climits>
#include <cmath>
#include <complex>
#include <deque>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

#include "synca.hpp"

namespace synca {
    constexpr int HOOK_PRIORITY = -1000000;
    constexpr size_t PANEL_LINES = 4;
    constexpr int SPIDER_LAND = 12;  // v2.15: ticks on a surface after a spider input = safe

    constexpr uint64_t typeBit(GameObjectType type) {
        return 1ull << static_cast<int>(type);
    }

    // Objects the ghost always collides with.
    // v2.61: back to the v2.48 set (v2.49 left CollisionObject out; cube and wave got less consistent)
    constexpr uint64_t COLLIDING_TYPES = typeBit(GameObjectType::Solid) | typeBit(GameObjectType::Hazard) | typeBit(GameObjectType::Slope) |
                                         typeBit(GameObjectType::CollisionObject) | typeBit(GameObjectType::AnimatedHazard);

    constexpr uint64_t HAZARD_TYPES = typeBit(GameObjectType::Hazard) | typeBit(GameObjectType::AnimatedHazard);

    // Objects the ghost may also interact with when smart prediction is on.
    constexpr uint64_t INTERACT_TYPES =
        typeBit(GameObjectType::YellowJumpPad) | typeBit(GameObjectType::PinkJumpPad) | typeBit(GameObjectType::RedJumpPad) |
        typeBit(GameObjectType::GravityPad) | typeBit(GameObjectType::SpiderPad) | typeBit(GameObjectType::YellowJumpRing) |
        typeBit(GameObjectType::PinkJumpRing) | typeBit(GameObjectType::RedJumpRing) | typeBit(GameObjectType::GravityRing) |
        typeBit(GameObjectType::GreenRing) | typeBit(GameObjectType::DropRing) | typeBit(GameObjectType::CustomRing) |
        typeBit(GameObjectType::DashRing) | typeBit(GameObjectType::GravityDashRing) | typeBit(GameObjectType::SpiderOrb) |
        typeBit(GameObjectType::InverseGravityPortal) | typeBit(GameObjectType::NormalGravityPortal) |
        typeBit(GameObjectType::GravityTogglePortal);

    // Portals that change too much state to simulate: reaching one ends prediction as "safe".
    constexpr uint64_t STOP_TYPES =
        typeBit(GameObjectType::ShipPortal) | typeBit(GameObjectType::CubePortal) | typeBit(GameObjectType::BallPortal) |
        typeBit(GameObjectType::UfoPortal) | typeBit(GameObjectType::WavePortal) | typeBit(GameObjectType::RobotPortal) |
        typeBit(GameObjectType::SpiderPortal) | typeBit(GameObjectType::SwingPortal) | typeBit(GameObjectType::DualPortal) |
        typeBit(GameObjectType::SoloPortal) | typeBit(GameObjectType::TeleportPortal) | typeBit(GameObjectType::TeleportOrb);

    // v2.79: with "Predict through portals" these switch the ghost's mode instead of
    // ending the prediction (dual / solo / teleport still end it).
    constexpr uint64_t MODE_TYPES =
        typeBit(GameObjectType::ShipPortal) | typeBit(GameObjectType::CubePortal) | typeBit(GameObjectType::BallPortal) |
        typeBit(GameObjectType::UfoPortal) | typeBit(GameObjectType::WavePortal) | typeBit(GameObjectType::RobotPortal) |
        typeBit(GameObjectType::SpiderPortal) | typeBit(GameObjectType::SwingPortal);

    // v2.8: size portals are applied to the ghost instead of ending the prediction.
    // (Mirror portals only flip the screen, so the ghost simply ignores them.)
    constexpr uint64_t SIZE_TYPES = typeBit(GameObjectType::RegularSizePortal) | typeBit(GameObjectType::MiniSizePortal);

    bool hasType(uint64_t mask, GameObject* object) {
        auto type = static_cast<unsigned>(object->m_objectType);
        return type < 64 && ((mask >> type) & 1);
    }

    float speedPortalValue(int id) {
        switch (id) {
            case 200: return 0.7f;   // slow
            case 201: return 0.9f;   // normal
            case 202: return 1.1f;   // fast
            case 203: return 1.3f;   // faster
            case 1334: return 1.6f; // fastest
            default: return 0.f;
        }
    }




    std::string stageText();
    int fullWaveHorizon(bool mini, bool alternate);
    void sendButton(PlayLayer* layer, bool down);

    // v2.23 unlocked FPS: GD runs round(max(1, frameTime * 240)) physics steps per frame,
    // so only 60 / 120 / 240 FPS give 240 steps a second (144 FPS -> 288, 360 FPS -> 360).
    // Every window in this mod was written in 240-per-second ticks, so at other frame rates
    // it covered the wrong amount of time (at 360 FPS the cube's look-ahead ended before a
    // jump could land, so every click looked like a death). `st()` converts a 240-tick
    // amount into physics steps at the current rate. At 60 / 120 / 240 FPS it changes nothing.
    float stepScale() {
        float dt = buf.tickLength;
        if (!(dt > 0.001f && dt < 2.f)) {
            return 1.f;
        }
        return std::clamp(TICK / dt, 0.25f, 8.f);
    }

    int st(int ticks240) {
        if (ticks240 <= 0) {
            return ticks240;
        }
        return std::max(1, static_cast<int>(std::lround(ticks240 * stepScale())));
    }

    int updateAlternatePhase(bool wasDown, int observedTicks) {
        auto& samples = wasDown ? buf.altDownSamples : buf.altUpSamples;
        auto& count = wasDown ? buf.altDownSampleCount : buf.altUpSampleCount;
        auto& next = wasDown ? buf.altDownSampleNext : buf.altUpSampleNext;
        auto& estimate = wasDown ? buf.altDownEvery : buf.altUpEvery;
        int sample = std::clamp(observedTicks, st(2), st(40));
        samples[next] = sample;
        next = (next + 1) % static_cast<int>(samples.size());
        count = std::min(count + 1, static_cast<int>(samples.size()));

        std::array<int, 7> ordered = samples;
        std::sort(ordered.begin(), ordered.begin() + count);
        int median = ordered[count / 2];
        estimate = count == 1 ? median : std::clamp((estimate + median + 1) / 2, st(2), st(40));
        return estimate;
    }

    bool alternatePhaseStable(bool wasDown) {
        auto const& samples = wasDown ? buf.altDownSamples : buf.altUpSamples;
        int count = wasDown ? buf.altDownSampleCount : buf.altUpSampleCount;
        int required = std::clamp(studio.altSamples, 3, static_cast<int>(samples.size()));
        if (count < required) {
            return false;
        }
        int next = wasDown ? buf.altDownSampleNext : buf.altUpSampleNext;
        std::array<int, 7> recent{};
        for (int i = 0; i < count; ++i) {
            int index = (next - 1 - i + static_cast<int>(samples.size())) % static_cast<int>(samples.size());
            recent[i] = samples[index];
        }
        // A single late/early edge is normal in this spam. Range checks treated one
        // outlier as a completely different rhythm and stopped helping at the portal.
        std::array<int, 7> deviations{};
        std::array<int, 7> ordered = recent;
        std::sort(ordered.begin(), ordered.begin() + required);
        int median = ordered[required / 2];
        for (int i = 0; i < required; ++i) deviations[i] = std::abs(recent[i] - median);
        std::sort(deviations.begin(), deviations.begin() + required);
        if (deviations[required / 2] <= st(studio.altTolerance)) {
            return true;
        }

        // USKC's hand-controlled spam can repeat a short/long pair instead of one
        // constant period. Accept a repeating phase pattern only when two cycles agree.
        int history = std::min(count, static_cast<int>(samples.size()));
        int tolerance = st(studio.altTolerance);
        for (int period = 2; period <= history / 2; ++period) {
            bool repeats = true;
            for (int i = 0; i + period < history; ++i) {
                if (std::abs(recent[i] - recent[i + period]) > tolerance) {
                    repeats = false;
                    break;
                }
            }
            if (repeats) return true;
        }
        return false;
    }

    struct AlternatePhaseForecast {
        std::array<int, 7> intervals{};
        int count = 0;
    };

    AlternatePhaseForecast alternatePhaseForecast(bool wasDown) {
        AlternatePhaseForecast forecast;
        auto const& samples = wasDown ? buf.altDownSamples : buf.altUpSamples;
        int sampleCount = wasDown ? buf.altDownSampleCount : buf.altUpSampleCount;
        int count = std::min(sampleCount, std::clamp(studio.altSamples, 1, static_cast<int>(samples.size())));
        int next = wasDown ? buf.altDownSampleNext : buf.altUpSampleNext;
        if (count <= 0) return forecast;

        std::array<int, 7> recent{}; // newest first
        for (int i = 0; i < count; ++i) {
            int index = (next - 1 - i + static_cast<int>(samples.size())) % static_cast<int>(samples.size());
            recent[i] = samples[index];
        }

        int period = 0;
        int tolerance = st(studio.altTolerance);
        for (int candidate = 1; candidate <= count / 2; ++candidate) {
            bool repeats = true;
            for (int i = 0; i + candidate < count; ++i) {
                if (std::abs(recent[i] - recent[i + candidate]) > tolerance) {
                    repeats = false;
                    break;
                }
            }
            if (repeats) {
                period = candidate;
                break;
            }
        }

        if (period > 0) {
            // The newest edge just completed the last sample. Start with the next
            // phase in that observed cycle, then preserve its short/long timing shape.
            forecast.count = period;
            for (int i = 0; i < period; ++i) {
                forecast.intervals[i] = recent[period - 1 - i];
            }
        }
        else {
            std::array<int, 7> ordered = recent;
            std::sort(ordered.begin(), ordered.begin() + count);
            forecast.intervals[0] = ordered[count / 2];
            forecast.count = 1;
        }
        return forecast;
    }

    void resetAlternateRhythm() {
        buf.altDownSamples.fill(0);
        buf.altUpSamples.fill(0);
        buf.altDownSampleCount = buf.altUpSampleCount = 0;
        buf.altDownSampleNext = buf.altUpSampleNext = 0;
        buf.altDownEvery = buf.altUpEvery = st(7);
        buf.altRapid = 0;
        buf.altWaveDelay = 0;
    }

    struct AlternatePredictionScope {
        int previous, previousDown, previousUp;
        std::array<int, 7> previousDownPattern, previousUpPattern;
        int previousDownCount, previousUpCount;
        AlternatePredictionScope(int period, int down, int up,
            AlternatePhaseForecast const& downPattern = {}, AlternatePhaseForecast const& upPattern = {})
          : previous(buf.altPredictionEvery), previousDown(buf.altPredictionDownEvery), previousUp(buf.altPredictionUpEvery),
            previousDownPattern(buf.altPredictionDownPattern), previousUpPattern(buf.altPredictionUpPattern),
            previousDownCount(buf.altPredictionDownPatternCount), previousUpCount(buf.altPredictionUpPatternCount) {
            buf.altPredictionEvery = period;
            buf.altPredictionDownEvery = down;
            buf.altPredictionUpEvery = up;
            buf.altPredictionDownPattern = downPattern.intervals;
            buf.altPredictionUpPattern = upPattern.intervals;
            buf.altPredictionDownPatternCount = downPattern.count;
            buf.altPredictionUpPatternCount = upPattern.count;
        }
        ~AlternatePredictionScope() {
            buf.altPredictionEvery = previous;
            buf.altPredictionDownEvery = previousDown;
            buf.altPredictionUpEvery = previousUp;
            buf.altPredictionDownPattern = previousDownPattern;
            buf.altPredictionUpPattern = previousUpPattern;
            buf.altPredictionDownPatternCount = previousDownCount;
            buf.altPredictionUpPatternCount = previousUpCount;
        }
    };

    // v2.4 moving-object guard: last known position of each object the ghost collided with.
    struct Watched {
        double x;
        double y;
        int movedTick;
        double vx = 0.0;  // v2.27: movement per physics step while it's moving
        double vy = 0.0;
        double pvx = 0.0;  // v2.68: the step before (curved paths: how the direction turns)
        double pvy = 0.0;
        double rot = 0.0;  // v2.68: rotation, and how much it turns per step
        double vrot = 0.0;
    };
    constexpr int MOVER_TICKS = 120;
    constexpr size_t WATCH_LIMIT = 4096;
    std::unordered_map<GameObject*, Watched> watched;

    // v2.4 drift check: the recorded ghost run for your last input and how far you strayed.
    struct Drift {
        std::vector<CCPoint> path;
        bool active = false;
        int index = -1;
        int compared = 0;
        float maxErr = 0.f;
        int maxAt = 0;
    } drift;

    // The game window uses a high content scale factor, so keep this compact in
    // cocos units. The previous 520x286 panel covered nearly the whole screen.

    // Log line colors: saves green, orbs cyan, auto-release purple, misses grey,
    // budget / skips orange, drift yellow.
    ccColor3B lineColor(std::string const& line) {
        auto starts = [&](char const* prefix) { return line.rfind(prefix, 0) == 0; };
        if (starts("SAVED") || starts("MORE TIME")) return { 120, 255, 140 };
        if (starts("ORB") || starts("orb")) return { 90, 220, 255 };
        if (starts("AUTO")) return { 200, 150, 255 };
        if (starts("no save")) return { 175, 175, 190 };
        if (starts("skip") || starts("prediction budget")) return { 255, 180, 80 };
        if (starts("drift")) return { 255, 240, 150 };
        if (starts("ghost killed") || starts("  hitbox")) return { 255, 120, 120 };
        if (starts("perf")) return { 150, 200, 255 };
        if (starts("F6") || starts("stage") || starts("v2") || starts("path") || starts("Click") || starts("CBF")) return { 255, 215, 90 };
        if (starts("pass")) return { 140, 150, 170 };
        if (starts("memory")) return { 255, 150, 220 };
        if (starts("console")) return { 150, 255, 200 };
        return { 255, 255, 255 };
    }

    // In-game console (F9): current stage + the same live log the Windows console shows.
    // Hidden by default; only touched when open, so it costs nothing while closed.
    void refreshPanel() {
        if (!buf.panel) {
            return;
        }
        bool wasVisible = buf.panel->isVisible();
        buf.panel->setVisible(buf.panelOpen);
        if (!buf.panelOpen) {
            return;
        }
        if (!wasVisible) {
            buf.panel->stopAllActions();
            buf.panel->setScale(0.94f);
            buf.panel->runAction(CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)));
        }
        buf.panelTitle->setString(stageText().c_str());
        buf.panelTitle->limitLabelWidth(PANEL_W - 16.f, 0.42f, 0.28f);
        if (buf.panelPerf) {
            buf.panelPerf->setString(fmt::format("LAST  {:.2f} ms  |  {} runs  |  {} ticks",
                buf.lastPerfMs, buf.lastPerfRuns, buf.lastPerfTicks).c_str());
            buf.panelPerf->limitLabelWidth(PANEL_W - 16.f, 0.36f, 0.26f);
        }
        size_t first = buf.logLines.size() > buf.panelLines.size() ? buf.logLines.size() - buf.panelLines.size() : 0;
        for (size_t i = 0; i < buf.panelLines.size(); ++i) {
            auto* label = buf.panelLines[i];
            size_t at = first + i;
            if (at < buf.logLines.size()) {
                auto const& line = buf.logLines[at];
                label->setString(line.c_str());
                label->setColor(lineColor(line));
                label->limitLabelWidth(PANEL_W - 16.f, 0.34f, 0.24f);
            }
            else {
                label->setString("");
            }
        }
    }

    // Log line -> in-game panel history, plus the Windows console if enabled.
    // (Formatting a short string is cheap; it was the synchronous console writes
    //  that hitched, and those still only happen with the console setting on.)
    // v2.29: the full log (not just the 8 lines on screen) for the F8 export, with the
    // physics step each line happened on.
    constexpr size_t EXPORT_LINES = 400;
    std::deque<std::string> exportLines;
    int logClock = 0;  // physics steps since the level started, kept by the update hook

    void sayLine(std::string const& msg) {
        exportLines.push_back(fmt::format("[{:>6}] {}", logClock, msg));
        while (exportLines.size() > EXPORT_LINES) {
            exportLines.pop_front();
        }
        buf.logLines.push_back(msg);
        while (buf.logLines.size() > PANEL_LINES) {
            buf.logLines.pop_front();
        }
        if (buf.console) {
            std::printf("[InputBuffer] %s\n", msg.c_str());
        }
        // v2.71: redrawn once per physics step instead of on every line (a click that logs
        // 4 lines rebuilt all 8 labels 4 times)
        buf.panelDirty = buf.panelOpen;
    }

    bool isGhost(PlayerObject* p) {
        return p && (p == buf.ghost1 || p == buf.ghost2);
    }

    void resetPending() {
        if (!g_p2.active) {
            g_p2.needsReset = true;   // Player 2's stream restarts together with Player 1's
        }
        frameSpam.owned = false;      // frame spam stops with a restart / stage change (the game input is re-synced elsewhere)
        frameSpam.checked = false;
        buf.queue.clear();
        buf.shift = 0;
        buf.releaseShift = 0;
    }

    void readSettings() {
        auto* mod = Mod::get();
        buf.smart = mod->getSettingValue<bool>("smart-prediction");
        buf.budgetMs = mod->getSettingValue<double>("sim-budget-ms");
        buf.autoBudget = mod->getSettingValue<bool>("auto-budget");
        buf.groundHorizon = static_cast<int>(mod->getSettingValue<int64_t>("ground-horizon"));
        buf.waveHorizon = static_cast<int>(mod->getSettingValue<int64_t>("wave-horizon"));
        buf.assumeOrbs = mod->getSettingValue<bool>("orb-assume-hit");
        buf.orbClick = buf.assumeOrbs;
        buf.tryBothOrbs = mod->getSettingValue<bool>("orb-try-both");
        buf.avoidMovers = mod->getSettingValue<bool>("avoid-moving-objects");
        buf.autoRelease = mod->getSettingValue<bool>("wave-auto-release");
        buf.autoWindow = static_cast<int>(mod->getSettingValue<int64_t>("auto-release-window"));
        buf.waveReleaseWindow = static_cast<int>(mod->getSettingValue<int64_t>("wave-release-window"));
        buf.robotReleaseWindow = static_cast<int>(mod->getSettingValue<int64_t>("robot-release-window"));
        buf.dashReleaseWindow = static_cast<int>(mod->getSettingValue<int64_t>("dash-release-window"));
        buf.driftCheck = mod->getSettingValue<bool>("drift-check");
        buf.spamGap = static_cast<int>(mod->getSettingValue<int64_t>("wave-spam-gap"));
        buf.orbAlign = mod->getSettingValue<bool>("orb-align");
        buf.orbTiming = mod->getSettingValue<bool>("orb-timing");
        buf.orbAlignGround = mod->getSettingValue<bool>("orb-align-ground");
        buf.fullState = mod->getSettingValue<bool>("full-ghost-state");
        buf.learn = mod->getSettingValue<bool>("level-memory");
        buf.robotSmart = mod->getSettingValue<bool>("robot-smart");
        buf.spiderSmart = mod->getSettingValue<bool>("spider-smart");
        buf.ufoSmart = mod->getSettingValue<bool>("ufo-smart");
        buf.ballSmart = mod->getSettingValue<bool>("ball-smart");
        buf.swingSmart = mod->getSettingValue<bool>("swing-smart");
        buf.train = mod->getSettingValue<bool>("training-mode");
        buf.perLevel = mod->getSettingValue<bool>("per-level-settings");
        buf.heatmap = mod->getSettingValue<bool>("death-heatmap");
        buf.saveDing = mod->getSettingValue<bool>("save-ding");
        buf.moreTime = mod->getSettingValue<bool>("more-time-death-spots");
        buf.dashAutoRelease = mod->getSettingValue<bool>("dash-auto-release");
        buf.perfLog = mod->getSettingValue<bool>("perf-log");
        buf.shipBuffer = mod->getSettingValue<bool>("ship-buffer");
        buf.lateCrash = static_cast<int>(mod->getSettingValue<int64_t>("cube-late-crash"));
        buf.robustSaves = mod->getSettingValue<bool>("robust-saves");
        buf.ignoreTouching = mod->getSettingValue<bool>("ignore-touching-hazards");
        buf.deathLog = mod->getSettingValue<bool>("death-log");
        buf.explain = mod->getSettingValue<bool>("explain-inputs");
        // v2.35 low latency mode: for players who feel the buffer as input lag. Short searches
        // (no frame hitches), at most 1 frame of delay, no extra "margin" ticks, and clicks
        // made while a delay is waiting aren't shifted (see intercept).
        buf.predictMovers = mod->getSettingValue<bool>("predict-moving-objects");
        if (studio.moversAuto && levelScan.moving > 0) {
            buf.predictMovers = true;   // the level scan found moving hazards: follow them without a setting
        }
        buf.shipMaxDelay = static_cast<int>(mod->getSettingValue<int64_t>("ship-max-delay"));
        buf.shipSpamGap = static_cast<int>(mod->getSettingValue<int64_t>("ship-spam-gap"));
        buf.cbfSync = mod->getSettingValue<bool>("cbf-sync");
        buf.robotAutoRelease = mod->getSettingValue<bool>("robot-auto-release");
        buf.autoNormalRuns = mod->getSettingValue<bool>("auto-release-normal-runs");
        buf.autoClick = mod->getSettingValue<bool>("auto-click");
        buf.spamStreak = std::clamp(static_cast<int>(mod->getSettingValue<int64_t>("wave-spam-streak")), 2, 6);
        buf.portalPredict = mod->getSettingValue<bool>("predict-through-portals");
        buf.tps = std::clamp(static_cast<int>(mod->getSettingValue<int64_t>("tps")), 240, 1000);
        buf.waveV46 = mod->getSettingValue<bool>("wave-v46");
        buf.waveMargin = mod->getSettingValue<bool>("wave-safety-margin");
        buf.orbWindow = static_cast<int>(mod->getSettingValue<int64_t>("orb-align-ticks"));
        // v2.79: read last, so its overrides stick (the wave margin used to be read again after it)
        buf.lowLatency = mod->getSettingValue<bool>("low-latency");
        if (buf.lowLatency) {
            buf.budgetMs = std::min(buf.budgetMs, 3.0);
            buf.robustSaves = false;
            buf.waveMargin = false;
        }
        // v2.71 performance mode: for PCs that drop frames on clicks. Shorter searches, no
        // extra robustness runs, orbs clicked only the normal way (no late-click retries), and
        // the debug runs (drift check, live path view) off.
        buf.perfMode = mod->getSettingValue<bool>("performance-mode");
        if (buf.perfMode) {
            buf.budgetMs = std::min(buf.budgetMs, 4.0);
            buf.robustSaves = false;
            buf.orbTiming = false;
            buf.driftCheck = false;
        }
        buf.userBudget = buf.budgetMs;
        studioReadSettings();
    }

    // v2.76 auto budget: the budget setting is used as it is, and only cut (to 4 ms) while
    // the game is already dropping frames. v2.75 also capped every click at 45% of a frame:
    // at 240+ fps that was 2 ms, too short for a long cube jump's search (Penumbral's first
    // jump: only +1..+5 delays were ever tried, so most clicks got no save).
    void clickBudget() {
        double budget = buf.userBudget;
        if (buf.autoBudget) {
            auto* director = CCDirector::get();
            double interval = director->getAnimationInterval() * 1000.0;
            double frame = director->getDeltaTime() * 1000.0;
            // v2.77: at most about one frame per click (never under 4 ms, so long cube jumps
            // still get searched): 4.2 ms at 240 fps, 7 ms at 144, the setting at 60
            if (interval > 0.0) {
                budget = std::min(budget, std::max(4.0, interval));
            }
            if (interval > 0.0 && frame > std::max(interval * 2.0, 20.0)) {
                budget = std::min(budget, 4.0);
            }
        }
        buf.budgetMs = budget;
    }

    void applyStage() {
        auto const& s = STAGES[buf.stage];
        buf.enabled = s.enabled;
        studioReadSettings();
        studio.perMode = s.custom && s.enabled;
        if (s.custom) {
            // Studio: the Custom stage is per gamemode (see studioMaxTicks); these two only feed
            // the label and any code that still reads them
            buf.ticksAll = studio.modeTicks[SM_CUBE];
            buf.ticksWave = studio.modeTicks[SM_WAVE];
        }
        else {
            buf.ticksAll = s.ticksAll;
            buf.ticksWave = s.ticksWave;
        }
    }

    // v2.14: a run outside practice where auto-release can press for you is marked on screen.
    // v2.46 watermark: whenever the buffer is on (any stage but OFF) the level shows it, and
    // can be hidden with the "hide-watermark" setting (except at a custom TPS).
    void refreshWatermark() {
        if (!buf.watermark) {
            return;
        }
        // v2.57: hidden in practice mode (practice runs can't be submitted, and it crowded the
        // checkpoint buttons); always shown in normal mode while the buffer is on.
        auto* layer = GameManager::get()->m_playLayer;
        // "Hide watermark" setting: hides the label, except while a custom TPS is active
        // (those runs aren't legit completions, so that warning stays on screen).
        bool hidden = Mod::get()->getSettingValue<bool>("hide-watermark") && buf.tps == 240;
        buf.watermark->setVisible(buf.enabled && !hidden && !(layer && layer->m_isPracticeMode));
        // v2.79: a custom TPS is always shown
        auto tpsText = buf.tps != 240 ? fmt::format("  |  {} TPS", buf.tps) : std::string();
        buf.watermark->setString(fmt::format("Synca  |  {}{}", stageText(), tpsText).c_str());
    }

    void refreshAutoTag() {
        if (!buf.autoTag) {
            return;
        }
        auto* layer = GameManager::get()->m_playLayer;
        bool on = buf.enabled && buf.autoNormalRuns && (buf.autoRelease || buf.robotAutoRelease || buf.dashAutoRelease || buf.autoClick || studio.spamOn) && layer &&
            !layer->m_isPracticeMode;
        buf.autoTag->setString("AUTO INPUTS ON");
        // It can be made fainter (25% at the least) but not removed: in a normal run it is what marks that
        // auto inputs were on. It never shows in Practice, or when no auto input is switched on.
        buf.autoTag->setOpacity(static_cast<GLubyte>(std::clamp(studio.autoTagOpacity, 25, 100) * 255 / 100));
        buf.autoTag->setVisible(on && !studio.autoTagHidden);
    }

    // Optional percentage profile inspired by the user's example: use the 4f stage in
    // one progress band and the per-mode Custom stage in another. Outside both bands the
    // user's saved base stage is restored.
    void updateProgressStageBand(PlayLayer* layer) {
        if (!studio.progressStageBands || !layer || layer->m_isPlatformer || layer->m_levelLength <= 0.f) {
            return;
        }
        int percent = std::clamp(static_cast<int>(layer->getCurrentPercent()), 0, 100);
        int desired = studio.progressBaseStage;
        // Every band can use any stage (fixed delays, PER MODE, or OFF). The last matching band wins.
        for (int i = 0; i < PROGRESS_BANDS; ++i) {
            if (studio.bandStart[i] < studio.bandEnd[i] && percent >= studio.bandStart[i] && percent < studio.bandEnd[i]) {
                desired = studio.bandStage[i];
            }
        }
        desired = std::clamp(desired, 0, STAGE_COUNT - 1);
        if (desired == buf.stage) {
            return;
        }
        buf.stage = desired;
        resetPending();
        if (!buf.autoReleased && buf.appliedInput != buf.realInput) {
            sendButton(layer, buf.realInput);
        }
        applyStage();
        refreshAutoTag();
        refreshWatermark();
        refreshPanel();
        say("progress profile: {}% -> {}", percent, stageText());
    }

    size_t memorySpots();

    // v2.29 F8: copy the console to the clipboard (and save it to console-export.txt in the
    // mod's save folder), with the version, level, physics rate and settings on top, so it
    // can be pasted somewhere in one go.
    void exportConsole() {
        auto* mod = Mod::get();
        auto* layer = GameManager::get()->m_playLayer;
        std::string text;
        text += fmt::format("Synca {} console export\n", mod->getVersion().toVString());
        if (layer && layer->m_level) {
            text += fmt::format("Level: {} (id {}){}\n", std::string(layer->m_level->m_levelName), layer->m_level->m_levelID.value(),
                layer->m_isPracticeMode ? ", practice" : "");
        }
        float dt = buf.tickLength > 0.001f ? buf.tickLength : TICK;
        text += fmt::format("{} | physics ~{:.0f} steps/s\n", stageText(), 60.f / dt);
        text += fmt::format("budget {:g} ms | smart {} | full state {} | robust {} | late crash {}\n", buf.budgetMs,
            buf.smart, buf.fullState, buf.robustSaves, buf.lateCrash);
        text += fmt::format("orbs: align {} (ground {}, {}t) timing {} assume {} both {} | memory {} ({} spots)\n", buf.orbAlign,
            buf.orbAlignGround, buf.orbWindow, buf.orbTiming, buf.assumeOrbs, buf.tryBothOrbs, buf.learn, memorySpots());
        text += fmt::format("wave: v4.6 {} margin {} horizon {} spam gap {} | robot smart {} | spider smart {} | ship {} (max {}t)\n",
            buf.waveV46, buf.waveMargin, buf.waveHorizon, buf.spamGap, buf.robotSmart, buf.spiderSmart, buf.shipBuffer, buf.shipMaxDelay);
        text += fmt::format("auto-release: wave {} ({}t) robot {} ({}t) dash {} ({}t) normal runs {} | auto-click window {}t\n",
            buf.autoRelease, buf.waveReleaseWindow, buf.robotAutoRelease, buf.robotReleaseWindow, buf.dashAutoRelease,
            buf.dashReleaseWindow, buf.autoNormalRuns, buf.autoWindow);
        text += fmt::format("moving objects: avoid {} | predict {}\n", buf.avoidMovers, buf.predictMovers);
        text += fmt::format("debug: drift check {} | path view {} | perf log {} | death log {} | ignore touching {}\n", buf.driftCheck,
            buf.showPath, buf.perfLog, buf.deathLog, buf.ignoreTouching);
        text += fmt::format("studio: per-mode {} | cube {}t ship {}t ball {}t ufo {}t wave {}t robot {}t spider {}t swing {}t | alt {} (max {}t, burst {}t, horizon {}t, modes W/S/B/U/G {},{},{},{},{}, samples {}, tolerance {}t, margin {}t)\n",
            studio.perMode, studio.modeTicks[SM_CUBE], studio.modeTicks[SM_SHIP], studio.modeTicks[SM_BALL], studio.modeTicks[SM_UFO],
            studio.modeTicks[SM_WAVE], studio.modeTicks[SM_ROBOT], studio.modeTicks[SM_SPIDER], studio.modeTicks[SM_SWING], studio.altOn,
            studio.altMaxTicks, studio.altBurstGap, studio.altWaveHorizon, studio.altMode[SM_WAVE], studio.altMode[SM_SHIP], studio.altMode[SM_BALL], studio.altMode[SM_UFO],
            studio.altMode[SM_SWING], studio.altSamples, studio.altTolerance, studio.altMargin);
        text += fmt::format("path: enabled {} | idle trace {} | after-input trace {} | lookahead {}t | refresh {}ms\n",
            buf.showPath, studio.pathShowIdle, studio.pathShowClick, studio.pathHorizon, studio.pathRefreshMs);
        text += fmt::format("explain inputs {} | low latency {} | cbf sync {}{}\n", buf.explain, buf.lowLatency, buf.cbfSync,
            Loader::get()->isModLoaded("syzzi.click_between_frames") ? " (CBF loaded)" : "");
        // v2.39: CBF Extrapolate only changes what's drawn (a hidden player run ahead to "now"),
        // never the real player's physics, so predictions aren't affected by it.
        if (Loader::get()->isModLoaded("square3ang.cbfextrapolate")) {
            text += "CBF Extrapolate loaded (visual only)\n";
        }
        text += "----\n";
        for (auto const& line : exportLines) {
            text += line;
            text += '\n';
        }

        bool copied = geode::utils::clipboard::write(text);
        std::error_code ec;
        std::filesystem::create_directories(mod->getSaveDir(), ec);
        std::ofstream(mod->getSaveDir() / "console-export.txt", std::ios::trunc) << text;
        say("console {} ({} lines) + saved to console-export.txt", copied ? "copied to clipboard" : "NOT copied", exportLines.size());
    }

    std::string stageText() {
        if (!buf.enabled) {
            return "Buffer: OFF";
        }
        if (STAGES[buf.stage].custom) {
            return fmt::format("Buffer (per mode): cube {:g}f / wave {:g}f{}{}", studio.modeTicks[SM_CUBE] * TICK,
                studio.modeTicks[SM_WAVE] * TICK, studio.altOn ? " / ALT" : "", buf.train ? " [training]" : "");
        }
        return fmt::format("Buffer{}: {:g}f / wave {:g}f{}", STAGES[buf.stage].custom ? " (custom)" : "", buf.ticksAll * TICK,
            buf.ticksWave * TICK, buf.train ? " [training]" : "");
    }

    // Level scan: once per level load. Finds the moving and toggled solids / hazards (through the move,
    // rotate, follow, scale and toggle triggers that target their groups), so that movers can be watched
    // before the ghost ever touches them. If this part does not compile with your Geode version, set
    // SYNCA_LEVEL_SCAN to 0 and send me the error.
#ifndef SYNCA_LEVEL_SCAN
#define SYNCA_LEVEL_SCAN 1
#endif
    // One slice of the scan (about 1 ms at most, so even a level with millions of objects never hitches).
    // Returns true once every object has been looked at.
    // Turns the portals into the level's gamemode timeline and marks the crowded parts.
    void buildLevelSections() {
        auto& scan = levelScan;
        scan.sections.clear();
        std::sort(scan.portals.begin(), scan.portals.end(),
            [](std::pair<float, int> const& a, std::pair<float, int> const& b) { return a.first < b.first; });
        LevelSection current;
        current.startX = 0.f;
        current.mode = scan.startMode;
        current.speed = scan.startSpeed;
        current.mini = scan.startMini;
        current.dual = scan.startDual;
        auto finish = [&](float x) {
            current.endX = x;
            if (current.endX - current.startX < 30.f && !scan.sections.empty()) {
                scan.sections.back().endX = x;   // a sliver between two portals: part of the previous stretch
            }
            else if (current.endX > current.startX) {
                scan.sections.push_back(current);
            }
        };
        for (auto const& [x, id] : scan.portals) {
            LevelSection next = current;
            switch (id) {
                case 12: next.mode = SM_CUBE; break;
                case 13: next.mode = SM_SHIP; break;
                case 47: next.mode = SM_BALL; break;
                case 111: next.mode = SM_UFO; break;
                case 660: next.mode = SM_WAVE; break;
                case 745: next.mode = SM_ROBOT; break;
                case 1331: next.mode = SM_SPIDER; break;
                case 1933: next.mode = SM_SWING; break;
                case 101: next.mini = true; break;
                case 99: next.mini = false; break;
                case 286: next.dual = true; break;
                case 287: next.dual = false; break;
                default: {
                    float speed = speedPortalValue(id);
                    if (speed > 0.f) next.speed = speed;
                    break;
                }
            }
            bool changed = next.mode != current.mode || std::abs(next.speed - current.speed) > 0.01f || next.mini != current.mini ||
                next.dual != current.dual;
            if (!changed) {
                continue;
            }
            finish(x);
            current = next;
            current.startX = x;
        }
        finish(std::max(scan.lastX, current.startX + 30.f));

        // how crowded each stretch is (hazards and blocks per 100 units), compared with the whole level
        auto sumBuckets = [](std::vector<uint16_t> const& buckets, float from, float to) {
            int first = std::max(0, static_cast<int>(from / 30.f));
            int last = std::min(static_cast<int>(buckets.size()), static_cast<int>(to / 30.f) + 1);
            int total = 0;
            for (int i = first; i < last; ++i) total += buckets[i];
            return total;
        };
        double everything = 0.0;
        for (auto& section : scan.sections) {
            section.hazards = sumBuckets(scan.hazardBuckets, section.startX, section.endX);
            section.solids = sumBuckets(scan.solidBuckets, section.startX, section.endX);
            everything += section.hazards + section.solids;
        }
        double average = scan.lastX > 0.f ? everything / (scan.lastX / 100.0) : 0.0;
        for (auto& section : scan.sections) {
            float length = section.endX - section.startX;
            double density = length > 0.f ? (section.hazards + section.solids) / (length / 100.0) : 0.0;
            section.busy = length >= 90.f && average > 0.0 && density > average * 1.6;
        }
    }

    bool scanStep(PlayLayer* layer) {
#if SYNCA_LEVEL_SCAN
        if (!layer || !layer->m_objects) {
            return false;
        }
        auto* list = layer->m_objects;
        unsigned total = list->count();
        if (total == 0) {
            return false;   // the level's objects are not there yet
        }
        levelScan.total = total;
        if (!levelScan.startKnown && layer->m_player1) {
            // where the level starts: the gamemode, speed, size and dual state you begin in
            auto* starter = layer->m_player1;
            levelScan.startKnown = true;
            levelScan.startMode = studioModeOf(starter);
            levelScan.startSpeed = starter->m_playerSpeed;
            levelScan.startMini = starter->m_vehicleSize < 1.f;
            levelScan.startDual = layer->m_gameState.m_isDualMode;
        }
        auto bump = [](std::vector<uint16_t>& buckets, int index) {
            if (index >= static_cast<int>(buckets.size())) buckets.resize(static_cast<size_t>(index) + 1, 0);
            if (buckets[index] < 65535) ++buckets[index];
        };
        auto started = std::chrono::steady_clock::now();
        unsigned processed = 0;
        while (levelScan.pos < list->count()) {   // checked every time: the list may change while the level starts
            auto* object = static_cast<GameObject*>(list->objectAtIndex(levelScan.pos));
            ++levelScan.pos;
            if (!object) {
                continue;
            }
            ++levelScan.objects;
            auto type = object->m_objectType;
            float objectX = static_cast<float>(object->m_positionX);
            if (objectX > levelScan.lastX) {
                levelScan.lastX = objectX;
            }
            int bucket = std::clamp(static_cast<int>(objectX / 30.f), 0, 200000);
            if (type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard) {
                ++levelScan.hazards;
                bump(levelScan.hazardBuckets, bucket);
            }
            else if (type == GameObjectType::Solid || type == GameObjectType::Slope) {
                bump(levelScan.solidBuckets, bucket);
            }
            int id = object->m_objectID;
            bool portal = id == 12 || id == 13 || id == 47 || id == 111 || id == 660 || id == 745 || id == 1331 || id == 1933 ||
                id == 99 || id == 101 || id == 286 || id == 287 || speedPortalValue(id) > 0.f;
            if (portal && levelScan.portals.size() < 20000) {
                levelScan.portals.emplace_back(objectX, id);
            }
            if (id == 901 || id == 1346 || id == 1347 || id == 2067) {   // move, rotate, follow, scale triggers
                ++levelScan.moving;
            }
            else if (id == 1049) {                                       // toggle triggers
                ++levelScan.toggled;
            }
            if ((++processed & 2047u) == 0 &&
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() > 1.0) {
                break;
            }
        }
        if (levelScan.pos < list->count()) {
            return false;   // more slices to come
        }
        levelScan.done = true;
        buildLevelSections();
        levelScan.summary = fmt::format("{} objects | {} hazards | {} sections | {} move triggers", levelScan.objects,
            levelScan.hazards, levelScan.sections.size(), levelScan.moving);
        say("level scan: {}", levelScan.summary);
        if (levelScan.moving > 0 && !buf.predictMovers) {
            if (studio.moversAuto) {
                buf.predictMovers = true;
                say("this level has moving objects: predicting them automatically (Studio > Auto assists)");
            }
            else {
                say("this level has moving objects: 'Predict moving hazards' (Studio > Auto assists) lets the ghost follow them");
            }
        }
        return true;
#else
        (void)layer;
        return false;
#endif
    }

    // Starts a fresh scan (used by the RESCAN button and when a level loads).
    void scanLevel(PlayLayer* layer) {
        int attempts = levelScan.tries + 1;
        levelScan = {};
        levelScan.tries = attempts;
        scanStep(layer);
    }

    // The scan used to run only once, inside the level's init, where the objects often do not exist yet (it then
    // found nothing and never looked again; other mods that change when a level loads made it seem to "only work"
    // sometimes). Now it keeps looking, twice a second, until the objects are there (or about 30 seconds passed).
    void ensureLevelScan(PlayLayer* layer) {
#if SYNCA_LEVEL_SCAN
        if (!layer || levelScan.done || levelScan.tries >= 240) {
            return;
        }
        static int cooldown = 0;
        if (--cooldown > 0) {
            return;
        }
        cooldown = 6;
        if (!scanStep(layer) && levelScan.total == 0) {
            if (++levelScan.tries == 240) {
                say("level scan: no level objects were found");
            }
        }
#else
        (void)layer;
#endif
    }

    void dropGhosts() {
        levelScan = {};
        for (auto** g : { &buf.ghost1, &buf.ghost2 }) {
            if (*g) {
                (*g)->removeFromParentAndCleanup(true);
                (*g)->release();
                *g = nullptr;
            }
        }
        if (buf.ghostLayer) {
            buf.ghostLayer->removeAllChildrenWithCleanup(true);
            buf.ghostLayer->release();
            buf.ghostLayer = nullptr;
        }
        // panel nodes are owned by the PlayLayer
        buf.panel = nullptr;
        buf.panelTitle = nullptr;
        buf.panelPerf = nullptr;
        buf.panelLegend.clear();
        buf.panelLines.clear();
        buf.pathNode = nullptr;
        buf.keepNode = nullptr;
        buf.autoTag = nullptr;
        buf.nextModeLabel = nullptr;
        buf.funHud = nullptr;
        buf.funFlash = nullptr;
        buf.watermark = nullptr;
        buf.orbDot = nullptr;
        buf.orbConf = 0;
        buf.orbConfShown = -1;
        buf.simulating = false;
        buf.injecting = false;
        buf.rapid = 0;
        resetAlternateRhythm();
        buf.autoReleased = false;
        buf.autoClicked = false;
        buf.swallowRelease = false;
        buf.recordPath = nullptr;
        watched.clear();
        drift.active = false;
        resetPending();
        // v2.86: a level you left can't leave a "button down" behind for the next one
        buf.realInput = false;
        buf.appliedInput = false;
        buf.owner = nullptr;
    }

    // PERF: v1 created ghosts with the PlayLayer as their parent layer and put them in
    // the object layer. PlayerObject::init adds ~13 particle systems, 2 motion streaks,
    // a wave trail and a ghost trail to that parent layer, so every frame the game was
    // updating and drawing two invisible players' worth of effects (which also kept
    // emitting wherever the ghost was last simulated). Ghosts now live in a detached
    // layer that is never added to the scene: cocos never visits/draws it and its
    // nodes' scheduled updates stay paused, so the ghosts cost nothing between clicks.
    PlayerObject* makeGhost(PlayLayer* layer) {
        if (!buf.ghostLayer) {
            buf.ghostLayer = CCLayer::create();
            buf.ghostLayer->retain();
        }
        auto* ghost = PlayerObject::create(1, 1, layer, buf.ghostLayer, true);
        ghost->retain();
        ghost->setPosition(CCPoint(0.f, 105.f));
        ghost->setVisible(false);
        buf.ghostLayer->addChild(ghost);
        return ghost;
    }

    // v2.24: anything the ghosts left in their layer since they were made (trail fade-outs,
    // effect nodes...) is removed with cleanup, which also takes it off the scheduler and
    // the action manager. Without this every prediction left a few paused nodes behind,
    // and the game iterates all of them every frame: the FPS sank a little with each save.
    void sweepGhostLayer() {
        if (!buf.ghostLayer) {
            return;
        }
        auto* children = buf.ghostLayer->getChildren();
        while (children && children->count() > buf.ghostLayerBase) {
            auto* node = static_cast<CCNode*>(children->lastObject());
            buf.ghostLayer->removeChild(node, true);
        }
    }

    // v2.85: what the game itself thinks: is the jump button down for this player?
    bool gameHolds(PlayerObject* player) {
        if (!player) {
            return false;
        }
        auto it = player->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        return it != player->m_holdingButtons.end() && it->second;
    }

    // Everything that belongs to ONE player's input stream. (Settings, ghosts and UI are shared.)
    struct InputSlot {
        bool realInput = false, appliedInput = false, isPlayer1 = false;
        std::deque<QueuedInput> queue;
        int shift = 0, releaseShift = 0;
        int sinceInput = 999, rapid = 0, altRapid = 0;
        std::array<int, 7> altDownSamples{}, altUpSamples{};
        int altDownSampleCount = 0, altUpSampleCount = 0, altDownSampleNext = 0, altUpSampleNext = 0;
        int altDownEvery = 7, altUpEvery = 7, altWaveDelay = 0;
        bool autoReleased = false, autoClicked = false, swallowRelease = false, autoDash = false;
        int autoClickAge = 0;
    };
    InputSlot slotP2;

    void swapInputSlot(InputSlot& s) {
        std::swap(buf.realInput, s.realInput);
        std::swap(buf.appliedInput, s.appliedInput);
        std::swap(buf.isPlayer1, s.isPlayer1);
        std::swap(buf.queue, s.queue);
        std::swap(buf.shift, s.shift);
        std::swap(buf.releaseShift, s.releaseShift);
        std::swap(buf.sinceInput, s.sinceInput);
        std::swap(buf.rapid, s.rapid);
        std::swap(buf.altRapid, s.altRapid);
        std::swap(buf.altDownSamples, s.altDownSamples);
        std::swap(buf.altUpSamples, s.altUpSamples);
        std::swap(buf.altDownSampleCount, s.altDownSampleCount);
        std::swap(buf.altUpSampleCount, s.altUpSampleCount);
        std::swap(buf.altDownSampleNext, s.altDownSampleNext);
        std::swap(buf.altUpSampleNext, s.altUpSampleNext);
        std::swap(buf.altDownEvery, s.altDownEvery);
        std::swap(buf.altUpEvery, s.altUpEvery);
        std::swap(buf.altWaveDelay, s.altWaveDelay);
        std::swap(buf.autoReleased, s.autoReleased);
        std::swap(buf.autoClicked, s.autoClicked);
        std::swap(buf.swallowRelease, s.swallowRelease);
        std::swap(buf.autoDash, s.autoDash);
        std::swap(buf.autoClickAge, s.autoClickAge);
    }

    // While one of these exists, `buf` holds Player 2's stream; the destructor puts Player 1's back.
    struct P2Context {
        explicit P2Context(PlayLayer* layer) {
            g_p2.active = true;
            swapInputSlot(slotP2);
            if (g_p2.needsReset) {
                g_p2.needsReset = false;
                buf.queue.clear();
                buf.shift = 0;
                buf.releaseShift = 0;
                buf.realInput = buf.appliedInput = gameHolds(layer->m_player2);
                buf.isPlayer1 = false;
                resetAlternateRhythm();
                buf.rapid = 0;
                buf.sinceInput = 999;
                buf.autoReleased = buf.autoClicked = buf.swallowRelease = buf.autoDash = false;
                buf.autoClickAge = 0;
            }
        }
        ~P2Context() {
            swapInputSlot(slotP2);
            g_p2.active = false;
        }
        P2Context(P2Context const&) = delete;
        P2Context& operator=(P2Context const&) = delete;
    };

    // True in a two-player level with both players out: each one then has its own buffer.
    bool p2OwnBuffer(PlayLayer* layer) {
        return studio.p2Buffer && layer && layer->m_player2 && layer->m_level && layer->m_level->m_twoPlayerMode &&
            layer->m_gameState.m_isDualMode;
    }

    void syncHeld(PlayLayer* layer) {
        resetPending();
        buf.realInput = buf.appliedInput = gameHolds(layer->m_player1);
    }

    void sendButton(PlayLayer* layer, bool down) {
        buf.injecting = true;
        buf.appliedInput = down;
        layer->handleButton(down, 1, buf.isPlayer1);
        buf.injecting = false;
    }

    void pressGhost(PlayerObject* ghost, bool down) {
        (ghost == buf.ghost1 ? buf.held1 : buf.held2) = down;
        if (down) {
            ghost->pushButton(PlayerButton::Jump);
        }
        else {
            ghost->releaseButton(PlayerButton::Jump);
        }
    }

    void copyState(PlayerObject* ghost, PlayerObject* real) {
        ghost->setVisible(false);
        ghost->resetTouchedRings(true);
        ghost->copyAttributes(real);
        ghost->setScale(real->getScale());
        ghost->setContentSize(real->getContentSize());
        ghost->m_width = real->m_width;
        ghost->m_height = real->m_height;
        ghost->m_gravityMod = real->m_gravityMod;
        ghost->m_isOnGround = real->m_isOnGround;
        ghost->m_isUpsideDown = real->m_isUpsideDown;
        ghost->m_vehicleSize = real->m_vehicleSize;
        ghost->m_playerSpeed = real->m_playerSpeed;
        ghost->m_isDashing = real->m_isDashing;
        ghost->m_lastActivatedPortal = real->m_lastActivatedPortal;
        if (buf.ghostShiftX != 0.f) {
            // v2.28 robustness check: start the ghost a little ahead of / behind you, the way a
            // real run is a fraction of a tick off from the prediction.
            CCPoint pos = real->getPosition();
            pos.x += buf.ghostShiftX;
            ghost->setPosition(pos);
            ghost->m_position = pos;
        }

        // Button / jump-buffer state. Without this the ghost keeps whatever button
        // state the PREVIOUS simulation left it in (usually "held"), so on some clicks
        // its pushButton() is a no-op or it auto-jumps from hold, and the prediction
        // flips between right and wrong from click to click.
        ghost->m_holdingButtons = real->m_holdingButtons;
        ghost->m_jumpBuffered = real->m_jumpBuffered;
        ghost->m_wasJumpBuffered = real->m_wasJumpBuffered;
        ghost->m_stateJumpBuffered = real->m_stateJumpBuffered;
        ghost->m_stateRingJump = real->m_stateRingJump;
        ghost->m_stateRingJump2 = real->m_stateRingJump2;
        ghost->m_wasRobotJump = real->m_wasRobotJump;
        ghost->m_touchedRing = real->m_touchedRing;
        ghost->m_touchedPad = real->m_touchedPad;
        ghost->m_touchedGravityPortal = real->m_touchedGravityPortal;
        if (!buf.fullState) {
            return;
        }

        // v2.8: everything else the physics step reads, so every mode matches the real
        // player: cube / ball / ufo / wave / robot / spider / swing, mini, upside down,
        // reverse (going left), sideways, dual. copyAttributes() doesn't carry most of
        // it, the slope state above all (wave and cube riding slopes drifted without it).
        ghost->m_isShip = real->m_isShip;
        ghost->m_isBird = real->m_isBird;
        ghost->m_isBall = real->m_isBall;
        ghost->m_isDart = real->m_isDart;
        ghost->m_isRobot = real->m_isRobot;
        ghost->m_isSpider = real->m_isSpider;
        ghost->m_isSwing = real->m_isSwing;
        ghost->m_isGoingLeft = real->m_isGoingLeft;
        ghost->m_isSideways = real->m_isSideways;
        ghost->m_reverseSync = real->m_reverseSync;
        ghost->m_reverseRelated = real->m_reverseRelated;
        ghost->m_maybeReverseSpeed = real->m_maybeReverseSpeed;
        ghost->m_maybeReverseAcceleration = real->m_maybeReverseAcceleration;
        ghost->m_speedMultiplier = real->m_speedMultiplier;
        ghost->m_gravity = real->m_gravity;
        ghost->m_yStart = real->m_yStart;
        ghost->m_fixGravityBug = real->m_fixGravityBug;
        ghost->m_fixRobotJump = real->m_fixRobotJump;
        ghost->m_enable22Changes = real->m_enable22Changes;
        ghost->m_enableImpulseFix = real->m_enableImpulseFix;
        ghost->m_totalTime = real->m_totalTime;

        ghost->m_lastPosition = real->m_lastPosition;  // v2.18: spider / move triggers read it (CBF sets it per tick)

        // vertical motion
        ghost->m_yVelocity = real->m_yVelocity;
        ghost->m_fallSpeed = real->m_fallSpeed;
        ghost->m_yVelocityBeforeSlope = real->m_yVelocityBeforeSlope;
        ghost->m_yVelocityRelated = real->m_yVelocityRelated;
        ghost->m_yVelocityRelated3 = real->m_yVelocityRelated3;
        ghost->m_groundYVelocity = real->m_groundYVelocity;
        ghost->m_accelerationOrSpeed = real->m_accelerationOrSpeed;
        ghost->m_isAccelerating = real->m_isAccelerating;
        ghost->m_maybeIsFalling = real->m_maybeIsFalling;
        ghost->m_isOnGround2 = real->m_isOnGround2;
        ghost->m_isOnGround3 = real->m_isOnGround3;
        ghost->m_isOnGround4 = real->m_isOnGround4;
        ghost->m_stateOnGround = real->m_stateOnGround;
        ghost->m_lastLandTime = real->m_lastLandTime;
        ghost->m_lastJumpTime = real->m_lastJumpTime;
        ghost->m_lastFlipTime = real->m_lastFlipTime;
        ghost->m_lastSpiderFlipTime = real->m_lastSpiderFlipTime;
        ghost->m_lastGroundedPos = real->m_lastGroundedPos;
        ghost->m_blackOrbRelated = real->m_blackOrbRelated;
        ghost->m_padRingRelated = real->m_padRingRelated;
        ghost->m_ringJumpRelated = real->m_ringJumpRelated;

        // slopes
        ghost->m_isOnSlope = real->m_isOnSlope;
        ghost->m_wasOnSlope = real->m_wasOnSlope;
        ghost->m_slopeVelocity = real->m_slopeVelocity;
        ghost->m_maybeUpsideDownSlope = real->m_maybeUpsideDownSlope;
        ghost->m_currentSlope = real->m_currentSlope;
        ghost->m_currentSlope2 = real->m_currentSlope2;
        ghost->m_currentPotentialSlope = real->m_currentPotentialSlope;
        ghost->m_isCollidingWithSlope = real->m_isCollidingWithSlope;
        ghost->m_collidingWithSlopeId = real->m_collidingWithSlopeId;
        ghost->m_slopeFlipGravityRelated = real->m_slopeFlipGravityRelated;
        ghost->m_slopeAngle = real->m_slopeAngle;
        ghost->m_slopeAngleRadians = real->m_slopeAngleRadians;
        ghost->m_slopeRotation = real->m_slopeRotation;
        ghost->m_currentSlopeYVelocity = real->m_currentSlopeYVelocity;
        ghost->m_isCurrentSlopeTop = real->m_isCurrentSlopeTop;
        ghost->m_slopeStartTime = real->m_slopeStartTime;
        ghost->m_slopeEndTime = real->m_slopeEndTime;
        ghost->m_slopeSlidingMaybeRotated = real->m_slopeSlidingMaybeRotated;
        ghost->m_stateDartSlide = real->m_stateDartSlide;

        // what it's standing on / touching
        ghost->m_lastGroundObject = real->m_lastGroundObject;
        ghost->m_preLastGroundObject = real->m_preLastGroundObject;
        ghost->m_maybeLastGroundObject = real->m_maybeLastGroundObject;
        ghost->m_collidedObject = real->m_collidedObject;
        ghost->m_objectSnappedTo = real->m_objectSnappedTo;
        ghost->m_snapDistance = real->m_snapDistance;
        ghost->m_lastCollisionBottom = real->m_lastCollisionBottom;
        ghost->m_lastCollisionTop = real->m_lastCollisionTop;
        ghost->m_lastCollisionLeft = real->m_lastCollisionLeft;
        ghost->m_lastCollisionRight = real->m_lastCollisionRight;
        ghost->m_collidedTopMinY = real->m_collidedTopMinY;
        ghost->m_collidedBottomMaxY = real->m_collidedBottomMaxY;
        ghost->m_collidedLeftMaxX = real->m_collidedLeftMaxX;
        ghost->m_collidedRightMinX = real->m_collidedRightMinX;

        // dash orbs
        ghost->m_dashX = real->m_dashX;
        ghost->m_dashY = real->m_dashY;
        ghost->m_dashAngle = real->m_dashAngle;
        ghost->m_dashStartTime = real->m_dashStartTime;
        ghost->m_dashRing = real->m_dashRing;
        ghost->m_dashFireFrame = real->m_dashFireFrame;

        // force / boost / state triggers already acting on the player
        ghost->m_stateForce = real->m_stateForce;
        ghost->m_stateForceVector = real->m_stateForceVector;
        ghost->m_affectedByForces = real->m_affectedByForces;
        ghost->m_stateBoostX = real->m_stateBoostX;
        ghost->m_stateBoostY = real->m_stateBoostY;
        ghost->m_stateNoStickX = real->m_stateNoStickX;
        ghost->m_stateNoStickY = real->m_stateNoStickY;
        ghost->m_stateHitHead = real->m_stateHitHead;
        ghost->m_stateFlipGravity = real->m_stateFlipGravity;
        ghost->m_stateNoAutoJump = real->m_stateNoAutoJump;
        ghost->m_stateScale = real->m_stateScale;
        ghost->m_maybeStateForce2 = real->m_maybeStateForce2;
    }

    // Your recent input timings, so predictions model what you actually do instead of
    // an infinite hold: cube taps (holding auto-re-jumps) and robot holds (they set the
    // jump height).
    constexpr int HOLD_SAMPLES = 8;

    struct HoldStats {
        int samples[HOLD_SAMPLES] = {};
        int count = 0;
        int index = 0;

        void add(int ticks) {
            samples[index] = std::clamp(ticks, 1, 240);
            index = (index + 1) % HOLD_SAMPLES;
            count = std::min(count + 1, HOLD_SAMPLES);
        }

        // percentile of the recent samples, or `fallback` with no samples yet
        int pick(int fallback, int percent) const {
            if (count == 0) {
                return fallback;
            }
            int tmp[HOLD_SAMPLES];
            std::copy(samples, samples + count, tmp);
            int at = std::min(count - 1, count * percent / 100);
            std::nth_element(tmp, tmp + at, tmp + count);
            return tmp[at];
        }
    };

    HoldStats tapHold, robotHold;
    int tickCount = 0;

    enum class InputKind { Tap, Wave, Robot, Other };
    int pressTick = -1;
    InputKind pressKind = InputKind::Other;

    // Cube / ball / ufo / spider / swing are tapped. Ship, wave and robot are held, and
    // each keeps its own samples so long holds don't pollute the cube's tap length.
    InputKind kindOf(PlayerObject* p) {
        if (!p || p->m_isShip) {
            return InputKind::Other;
        }
        if (p->m_isDart) {
            return InputKind::Wave;
        }
        return p->m_isRobot ? InputKind::Robot : InputKind::Tap;
    }

    void recordRealInput(bool down, PlayerObject* p1) {
        if (down) {
            pressTick = tickCount;
            pressKind = kindOf(p1);
            return;
        }
        if (pressTick < 0) {
            return;
        }
        int held = tickCount - pressTick;
        pressTick = -1;
        if (buf.autoReleased) {
            return;  // the game already let go earlier; this length isn't a real hold
        }
        switch (pressKind) {
            case InputKind::Tap: tapHold.add(held); break;
            case InputKind::Robot: robotHold.add(held); break;
            default: break;
        }
    }

    // A hold that spans a death / restart / new level isn't a real tap length.
    void forgetPress() {
        pressTick = -1;
    }

    int holdEstimate() {
        return tapHold.pick(st(12), 50);  // ~50 ms, a typical tap
    }

    // v2.4 moving-object guard: objects the ghost collided with are watched every tick,
    // and any that moved recently make the decision "don't buffer" (the ghost sees the
    // level frozen at the click, so it tests them in the wrong place).
    void watchObject(GameObject* object) {
        auto [it, added] = watched.try_emplace(object, Watched { object->m_positionX, object->m_positionY, -MOVER_TICKS });
        if (added) {
            it->second.rot = object->getRotation();
        }
        if (!added && tickCount - it->second.movedTick < st(MOVER_TICKS)) {
            buf.sawMover = true;
        }
    }

    // v2.41: objects that moved on the last step, so predictions don't have to search the
    // whole watch list for them.
    std::vector<GameObject*> movingNow;

    void updateWatched() {
        if (watched.size() > WATCH_LIMIT) {
            watched.clear();
        }
        movingNow.clear();
        // v2.41: objects far from you (passed long ago, or far ahead) are dropped instead of
        // being checked every step; predictions never reach further than ~300 units.
        double px = 0.0;
        bool prune = false;
        if (auto* layer = GameManager::get()->m_playLayer; layer && layer->m_player1) {
            px = layer->m_player1->getPosition().x;
            prune = true;
        }
        for (auto it = watched.begin(); it != watched.end();) {
            auto* object = it->first;
            auto& w = it->second;
            if (prune && std::abs(object->m_positionX - px) > 800.0) {
                it = watched.erase(it);
                continue;
            }
            double rot = object->getRotation();
            if (object->m_positionX != w.x || object->m_positionY != w.y || rot != w.rot) {
                // called every step, so the change is the movement per step
                bool consecutive = tickCount - w.movedTick == 1;
                w.pvx = consecutive ? w.vx : 0.0;
                w.pvy = consecutive ? w.vy : 0.0;
                w.vx = object->m_positionX - w.x;
                w.vy = object->m_positionY - w.y;
                double dr = rot - w.rot;
                w.vrot = std::remainder(dr, 360.0);
                w.x = object->m_positionX;
                w.y = object->m_positionY;
                w.rot = rot;
                w.movedTick = tickCount;
                movingNow.push_back(object);
            }
            else if (tickCount - w.movedTick > 1) {
                w.vx = w.vy = w.pvx = w.pvy = w.vrot = 0.0;  // stopped
            }
            ++it;
        }
    }

    // v2.27 moving-object prediction. Objects near your path that are moving right now are
    // moved along their current motion during a prediction (where they'll be k steps
    // later), and put back exactly afterwards. Straight-line movement only: easing,
    // rotation and toggles still aren't predicted.
    // v2.68: also curved paths (rotate triggers move objects around a center, Orbit's saws):
    // when the direction of motion turned between the last two steps, it keeps turning by the
    // same angle each step, and objects that spin keep spinning. Still put back exactly after.
    struct Mover {
        GameObject* object;
        double x, y, vx, vy;
        double turn = 0.0;  // radians the motion direction turns per step (0 = straight)
        double rotX = 0.0, rotY = 0.0, vrot = 0.0;  // both axes kept as they were (skewed objects)
        double ax = 0.0, ay = 0.0;  // v2.75: speeding up / slowing down (easing), per step
        int accelSteps = 0;         // v2.75: how long the easing is followed (until it would stop / over 3x speed)
    };
    std::vector<Mover> movers;

    void collectMovers() {
        movers.clear();
        if (!buf.predictMovers) {
            return;
        }
        for (auto* object : movingNow) {
            auto it = watched.find(object);
            if (it == watched.end()) {
                continue;
            }
            auto const& w = it->second;
            if ((w.vx != 0.0 || w.vy != 0.0 || w.vrot != 0.0) && tickCount - w.movedTick <= 1) {
                Mover m { object, object->m_positionX, object->m_positionY, w.vx, w.vy };
                double speed = std::hypot(w.vx, w.vy);
                double prev = std::hypot(w.pvx, w.pvy);
                if (speed > 1e-6 && prev > 1e-6) {
                    double turn = std::remainder(std::atan2(w.vy, w.vx) - std::atan2(w.pvy, w.pvx), 6.283185307179586);
                    double ratio = speed / prev;
                    // a steady turn at a steady speed; anything wilder stays a straight line
                    if (std::abs(turn) > 1e-4 && std::abs(turn) < 0.5 && ratio > 0.8 && ratio < 1.25) {
                        m.turn = turn;
                    }
                    // v2.75: straight, but faster or slower than the step before: eased movement
                    else if (std::abs(turn) <= 1e-4 && std::abs(ratio - 1.0) > 1e-3 && ratio > 0.5 && ratio < 2.0) {
                        m.ax = w.vx - w.pvx;
                        m.ay = w.vy - w.pvy;
                        double along = (m.ax * w.vx + m.ay * w.vy) / speed;  // + speeding up, - slowing down
                        double steps = along < 0.0 ? speed / -along : 2.0 * speed / along;
                        m.accelSteps = static_cast<int>(std::min(steps, 1000.0));
                    }
                }
                m.rotX = object->getRotationX();
                m.rotY = object->getRotationY();
                m.vrot = w.vrot;
                movers.push_back(m);
            }
        }
    }

    void placeMovers(int steps) {
        for (auto const& m : movers) {
            double x = m.x + m.vx * steps;
            double y = m.y + m.vy * steps;
            if (m.accelSteps > 0 && steps > 0) {
                // eased: the speed keeps changing the same way, then stays (stopped or at 3x)
                int n = std::min(steps, m.accelSteps);
                double grow = 0.5 * n * (n + 1);
                x = m.x + m.vx * steps + m.ax * (grow + static_cast<double>(n) * (steps - n));
                y = m.y + m.vy * steps + m.ay * (grow + static_cast<double>(n) * (steps - n));
            }
            if (m.turn != 0.0 && steps > 0) {
                // sum of the step vector turned by `turn` once more every step
                std::complex<double> v { m.vx, m.vy };
                auto rho = std::polar(1.0, m.turn);
                auto moved = v * rho * (1.0 - std::pow(rho, steps)) / (1.0 - rho);
                x = m.x + moved.real();
                y = m.y + moved.imag();
            }
            if (m.vrot != 0.0) {
                double spun = steps == 0 ? 0.0 : m.vrot * steps;
                m.object->setRotationX(static_cast<float>(m.rotX + spun));
                m.object->setRotationY(static_cast<float>(m.rotY + spun));
            }
            m.object->setPosition({ static_cast<float>(x), static_cast<float>(y) });
            m.object->m_positionX = x;
            m.object->m_positionY = y;
            m.object->setObjectRectDirty(true);
            m.object->m_isOrientedBoxDirty = true;
        }
    }

    // v2.4 drift check: the ghost run for the input you actually made is recorded, then
    // compared tick by tick with where the real player goes, until your next input.
    void finishDrift(char const* why) {
        if (!drift.active) {
            return;
        }
        drift.active = false;
        if (drift.compared >= 8) {
            say("drift {:.1f}u max at t{} over {}t{}", drift.maxErr, drift.maxAt, drift.compared, why);
        }
    }

    void driftStep(PlayerObject* real) {
        if (!drift.active) {
            return;
        }
        if (drift.index >= 0) {
            if (drift.index >= static_cast<int>(drift.path.size())) {
                finishDrift("");
                return;
            }
            float err = ccpDistance(real->getPosition(), drift.path[drift.index]);
            if (err > drift.maxErr) {
                drift.maxErr = err;
                drift.maxAt = drift.index;
            }
            ++drift.compared;
        }
        ++drift.index;
    }

    // v2.13 level memory. Each level is cut into half-block slices along x. Every buffer
    // decision is remembered at the slice where it was made, and ~0.4 s later it is
    // scored: still alive = it worked, died = it didn't. Over your runs this learns, per
    // spot, which delays work there, where saves keep failing (the ghost doesn't match
    // the level there: triggers, moving objects) and where you keep dying. Stored per
    // level in the mod's save folder, so it carries over between sessions.
    constexpr float SLICE = 15.f;          // half a block
    constexpr int OUTCOME_TICKS = 90;      // alive this long after a decision = it worked

    struct Spot {
        uint32_t ok = 0;        // saves here that you survived
        uint32_t bad = 0;       // saves here followed by a death
        uint32_t deaths = 0;    // deaths after any decision here
        uint32_t delaySum = 0;  // sum of the delays of the saves that worked
        uint32_t last = 0;      // v2.79: the delay of the last save that worked here (240/s), 0 = none
    };

    std::unordered_map<int, Spot> memory;
    std::string memoryKey;
    bool memoryDirty = false;
    int memoryUpdates = 0;

    struct PendingOutcome {
        bool active = false;
        int slice = 0;
        int tick = 0;
        int delay = 0;
        bool skipped = false;  // v2.64: a save was held back here because of memory
    } pending;

    // Fun tab state (defined here because decided() records the assist delay into it).
    struct FunState {
        std::deque<int> pressTicks;     // tick of every real press in the last second (for the CPS)
        std::deque<int> edgeTicks;      // the last 9 real presses / releases (for the evenness meter)
        double peakCps = 0.0;
        std::chrono::steady_clock::time_point levelStart = std::chrono::steady_clock::now();
        int attempts = 0;
        float best = 0.f;
        float bestAtAttempt = 0.f;
        bool flashedBest = false;
        bool flashedSpot = false;
        float hardestX = -1.f;
        std::vector<float> deaths;
        float lastDeath = -1.f;
        int streak = 0;
        int lastDelay = 0;
        int lastDelayTick = -1000000;
        int flashUntil = 0;
        int hudCounter = 0;
    } fun;

    int sliceOf(PlayerObject* p) {
        return static_cast<int>(std::floor(p->getPosition().x / SLICE));
    }

    // A spot plus its two neighbours: the same gap is never hit at exactly the same x.
    Spot spotAround(int slice) {
        Spot sum;
        for (int s = slice - 1; s <= slice + 1; ++s) {
            auto it = memory.find(s);
            if (it == memory.end()) {
                continue;
            }
            sum.ok += it->second.ok;
            sum.bad += it->second.bad;
            sum.deaths += it->second.deaths;
            sum.delaySum += it->second.delaySum;
        }
        return sum;
    }

    std::filesystem::path memoryFile() {
        // v2.37: slices keyed by the real position. v2.75: _v3, a fresh start after the
        // prediction fixes of v2.61-v2.74 (old files learned from the buggy versions)
        return Mod::get()->getSaveDir() / "memory" / (memoryKey + "_v3.txt");
    }

    size_t memorySpots() {
        return memory.size();
    }

    void saveMemory() {
        if (!memoryDirty || memoryKey.empty()) {
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories(memoryFile().parent_path(), ec);
        std::ofstream out(memoryFile(), std::ios::trunc);
        for (auto const& [slice, spot] : memory) {
            out << slice << ' ' << spot.ok << ' ' << spot.bad << ' ' << spot.deaths << ' ' << spot.delaySum << ' ' << spot.last << '\n';
        }
        memoryDirty = false;
    }

    void loadMemory(GJGameLevel* level) {
        memory.clear();
        memoryDirty = false;
        memoryUpdates = 0;
        pending.active = false;
        memoryKey.clear();
        if (!level) {
            return;
        }
        int id = level->m_levelID.value();
        if (id > 0) {
            memoryKey = fmt::format("level_{}", id);
        }
        else {
            std::string name = level->m_levelName;
            for (auto& c : name) {
                if (!std::isalnum(static_cast<unsigned char>(c))) {
                    c = '_';
                }
            }
            memoryKey = "local_" + name;
        }
        std::ifstream in(memoryFile());
        // v2.79: lines may have a 6th number (last working delay); older files have 5
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            int slice;
            Spot spot;
            if (!(fields >> slice >> spot.ok >> spot.bad >> spot.deaths >> spot.delaySum)) {
                continue;
            }
            fields >> spot.last;
            memory[slice] = spot;
        }
        // v2.75: memory fades. Opened again more than 12 hours later, everything learned counts
        // a quarter less (the average delay at a spot stays the same), so old mistakes and
        // old death spots don't stick forever.
        std::error_code ec;
        auto written = std::filesystem::last_write_time(memoryFile(), ec);
        if (!ec && std::filesystem::file_time_type::clock::now() - written > std::chrono::hours(12)) {
            for (auto it = memory.begin(); it != memory.end();) {
                auto& s = it->second;
                uint32_t ok = s.ok * 3 / 4;
                s.delaySum = s.ok > 0 ? static_cast<uint32_t>(static_cast<uint64_t>(s.delaySum) * ok / s.ok) : 0;
                s.ok = ok;
                if (ok == 0) {
                    s.last = 0;  // v2.79: nothing that worked is left to reuse
                }
                s.bad = s.bad * 3 / 4;
                s.deaths = s.deaths * 3 / 4;
                it = (s.ok | s.bad | s.deaths) == 0 ? memory.erase(it) : std::next(it);
            }
            memoryDirty = true;
            saveMemory();
        }
        if (!memory.empty()) {
            say("memory: {} spots learned on this level", memory.size());
        }
    }

    void memoryChanged() {
        memoryDirty = true;
        if (++memoryUpdates % 10 == 0) {
            saveMemory();
        }
    }

    void creditPending() {
        if (!pending.active) {
            return;
        }
        pending.active = false;
        if (pending.delay > 0) {
            auto& spot = memory[pending.slice];
            auto delay = static_cast<uint32_t>(std::lround(pending.delay / stepScale()));  // stored at 240/s
            ++spot.ok;
            spot.delaySum += delay;
            // v2.79: the exact delay that just worked is reused next time, and each save that works
            // takes back one old failure here, so a spot you've started making stops being skipped
            spot.last = delay;
            if (spot.bad > 0) {
                --spot.bad;
            }
            memoryChanged();
        }
    }

    // Called for every decision: remembers it until it can be scored.
    void memoryNote(PlayerObject* p1, int delay) {
        if (g_p2.active) return;   // the level memory and path view follow Player 1 only
        if (!buf.learn || memoryKey.empty()) {
            return;
        }
        // A decision that lived long enough before the next one counts as having worked.
        if (pending.active && tickCount - pending.tick >= st(30)) {
            creditPending();
        }
        pending = { true, sliceOf(p1), tickCount, delay, false };
    }

    void memoryTick() {
        if (pending.active && tickCount - pending.tick >= st(OUTCOME_TICKS)) {
            creditPending();
        }
    }

    bool memorySaysSkip(int slice);

    void memoryDeath() {
        if (!pending.active) {
            return;
        }
        pending.active = false;
        auto& spot = memory[pending.slice];
        ++spot.deaths;
        if (pending.delay > 0) {
            ++spot.bad;
        }
        // v2.64: memory held a save back and you died anyway, so leaving the input alone
        // isn't working either: take back one failed save, so after a death or two the mod
        // tries saving here again instead of skipping this spot forever (first jump of
        // Slaughterhouse, after older versions' bad saves there).
        if (pending.skipped) {
            int worst = pending.slice;
            for (int s = pending.slice - 1; s <= pending.slice + 1; ++s) {
                auto it = memory.find(s);
                if (it != memory.end() && it->second.bad > memory[worst].bad) {
                    worst = s;
                }
            }
            if (memory[worst].bad > 0) {
                --memory[worst].bad;
            }
            if (!memorySaysSkip(pending.slice)) {
                say("memory: died with the input left alone, saves will be tried here again");
            }
        }
        memoryChanged();
    }

    // Saves made here keep ending in deaths: the ghost is wrong at this spot, leave it alone.
    bool memorySaysSkip(int slice) {
        if (g_p2.active) return false;
        if (!buf.learn) {
            return false;
        }
        auto spot = spotAround(slice);
        return spot.bad >= 2 && spot.bad > spot.ok;
    }

    // The delay that has worked here before, or 0.
    // v2.79: the last delay that worked (this spot first, then its neighbours) instead of the
    // average of all of them: +4 and +12 both working averaged to +8, which may never have
    // worked (narrow orb timings and wave turns). It's still checked by a prediction before use.
    int memoryDelay(int slice) {
        if (g_p2.active) return 0;
        if (!buf.learn) {
            return 0;
        }
        for (int sl : { slice, slice - 1, slice + 1 }) {
            auto it = memory.find(sl);
            if (it != memory.end() && it->second.last > 0) {
                return st(static_cast<int>(it->second.last));
            }
        }
        auto spot = spotAround(slice);  // older lines: no last delay yet, the average
        if (spot.ok < 2) {
            return 0;
        }
        return st(static_cast<int>(std::lround(static_cast<double>(spot.delaySum) / spot.ok)));
    }

    bool memoryRisky(int slice) {
        auto spot = spotAround(slice);
        return spot.deaths >= 3;
    }

    void snapshotObject(GameObject* object) {
        if (!object || buf.simUsed.contains(object)) {
            return;
        }
        buf.simUsed.insert(object);
        ObjSnapshot s { object, object->m_isActivated, nullptr, false, false, false };
        if (auto* effect = typeinfo_cast<EffectGameObject*>(object)) {
            s.effect = effect;
            s.activated = effect->m_activated;
            s.byP1 = effect->m_activatedByPlayer1;
            s.byP2 = effect->m_activatedByPlayer2;
        }
        buf.snapshots.push_back(s);
    }

    // v2.88: anything a ghost added to the real level during a prediction (the dash start
    // effect of dash orbs, bursts...) is removed afterwards. Dash orbs still flashed before
    // you reached them: those effects go onto the level's layers, not the ghost's own.
    struct NodeMark {
        CCNode* node;
        unsigned count;
    };
    std::vector<NodeMark> levelMarks;

    void markLevelNodes(PlayLayer* layer) {
        levelMarks.clear();
        auto add = [](CCNode* n) {
            if (n) {
                levelMarks.push_back({ n, n->getChildrenCount() });
            }
        };
        add(layer);
        add(layer->m_objectLayer);
        if (layer->m_aboveShaderObjectLayer != layer->m_objectLayer) {
            add(layer->m_aboveShaderObjectLayer);
        }
        if (layer->m_objectLayer && layer->m_objectLayer->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(layer->m_objectLayer->getChildren())) {
                add(child);
            }
        }
    }

    void sweepLevelNodes() {
        for (auto const& m : levelMarks) {
            auto* kids = m.node->getChildren();
            while (kids && kids->count() > m.count) {
                m.node->removeChild(static_cast<CCNode*>(kids->lastObject()), true);
            }
        }
        levelMarks.clear();
    }

    void restoreObjects() {
        sweepLevelNodes();
        for (auto const& s : buf.snapshots) {
            s.object->m_isActivated = s.isActivated;
            if (s.effect) {
                s.effect->m_activated = s.activated;
                s.effect->m_activatedByPlayer1 = s.byP1;
                s.effect->m_activatedByPlayer2 = s.byP2;
            }
        }
        buf.snapshots.clear();
        buf.simUsed.clear();
        buf.orbsUsed.clear();
        buf.simDisabled.clear();
    }

    void advanceGhost(PlayerObject* ghost, float dt) {
        ghost->update(dt);
        ghost->updateRotation(dt);
    }

    void checkGhost(PlayLayer* layer, PlayerObject* ghost, float dt) {
        ghost->m_collisionLogTop->removeAllObjects();
        ghost->m_collisionLogBottom->removeAllObjects();
        ghost->m_collisionLogLeft->removeAllObjects();
        ghost->m_collisionLogRight->removeAllObjects();
        layer->checkCollisions(ghost, dt, false);
    }

    // v2.79: the ghost goes through a mode portal the way the player would (no effects).
    // Mode flags are copied from the real player before every run, so nothing leaks.
    void setGhostMode(PlayerObject* ghost, GameObjectType type) {
        using T = GameObjectType;
        bool now = (type == T::ShipPortal && ghost->m_isShip) || (type == T::BallPortal && ghost->m_isBall) ||
            (type == T::UfoPortal && ghost->m_isBird) || (type == T::WavePortal && ghost->m_isDart) ||
            (type == T::RobotPortal && ghost->m_isRobot) || (type == T::SpiderPortal && ghost->m_isSpider) ||
            (type == T::SwingPortal && ghost->m_isSwing) ||
            (type == T::CubePortal && !ghost->m_isShip && !ghost->m_isBall && !ghost->m_isBird && !ghost->m_isDart &&
                !ghost->m_isRobot && !ghost->m_isSpider && !ghost->m_isSwing);
        if (now) {
            return;
        }
        buf.ghostSwitched = true;
        if (ghost->m_isShip) ghost->toggleFlyMode(false, true);
        if (ghost->m_isBall) ghost->toggleRollMode(false, true);
        if (ghost->m_isBird) ghost->toggleBirdMode(false, true);
        if (ghost->m_isDart) ghost->toggleDartMode(false, true);
        if (ghost->m_isRobot) ghost->toggleRobotMode(false, true);
        if (ghost->m_isSpider) ghost->toggleSpiderMode(false, true);
        if (ghost->m_isSwing) ghost->toggleSwingMode(false, true);
        switch (type) {
            case T::ShipPortal: ghost->toggleFlyMode(true, true); break;
            case T::BallPortal: ghost->toggleRollMode(true, true); break;
            case T::UfoPortal: ghost->toggleBirdMode(true, true); break;
            case T::WavePortal: ghost->toggleDartMode(true, true); break;
            case T::RobotPortal: ghost->toggleRobotMode(true, true); break;
            case T::SpiderPortal: ghost->toggleSpiderMode(true, true); break;
            case T::SwingPortal: ghost->toggleSwingMode(true, true); break;
            default: break;
        }
    }

    bool isFlying(PlayerObject* player) {
        return player->m_isDart || player->m_isBird || player->m_isSwing || player->m_isShip;
    }

    // Orb auto-clicks last one ghost update, then let go (unless the test itself is holding).
    void releaseAutoClicks() {
        if (buf.auto1) {
            if (!buf.held1 && buf.ghost1) {
                buf.ghost1->releaseButton(PlayerButton::Jump);
            }
            buf.auto1 = false;
        }
        if (buf.auto2) {
            if (!buf.held2 && buf.ghost2) {
                buf.ghost2->releaseButton(PlayerButton::Jump);
            }
            buf.auto2 = false;
        }
    }

    enum class SimEnd { Ok, StopSearch };

    // Returns ticks survived after the input (horizon = fully safe).
    // releaseAfter: ticks after the input at which the ghost does the opposite (-1 = never).
    // stopAt: stop as soon as this many ticks were survived (the caller already knows
    // that's good enough), instead of always running the full horizon.
    int simulate(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int releaseAfter,
        int stopAt, SimEnd* end) {
        *end = SimEnd::Ok;
        if (!buf.ghost1) {
            return horizon;
        }
        buf.simulating = true;
        markLevelNodes(layer);
        buf.ghostDied = false;
        buf.ghostHitPortal = false;
        buf.firstOrbTick = -1;
        buf.lastOrbTick = -1;
        buf.laterOrbTouched = false;
        buf.orbUsedTick = -1;

        bool second = dual && buf.ghost2 && p2;
        buf.haveRealRect = buf.ignoreTouching;
        buf.harmless.clear();
        buf.ghostSwitched = false;
        // v2.79: always known, so mode portals you're still inside are skipped (see
        // collisionCheckObjects)
        buf.haveStartRects = true;
        buf.realRect1 = p1->getObjectRect();
        buf.realRect2 = second ? p2->getObjectRect() : buf.realRect1;
        copyState(buf.ghost1, p1);
        if (second) {
            copyState(buf.ghost2, p2);
        }
        buf.held1 = buf.held2 = buf.appliedInput;
        buf.auto1 = buf.auto2 = false;
        buf.autoAt1 = buf.autoAt2 = -1;

        // v4.6 wave: before a delayed input fires, the ghost is in the opposite state
        // (holding before a delayed release, released before a delayed press).
        bool waveRun = p1->m_isDart || (second && p2->m_isDart);
        if (buf.waveV46 && waveRun && delay > 0 && !buf.skipPress) {
            pressGhost(buf.ghost1, !down);
            if (second) {
                pressGhost(buf.ghost2, !down);
            }
        }

        float dt = buf.tickLength;
        if (!(dt > 0.001f && dt < 2.0f)) {
            dt = TICK;
        }

        // (v1 forced a fake !down button event here for delay > 0, which made delayed
        //  runs start from a different jump-buffer state than the "press now" run.
        //  The ghost now simply mirrors the real player's button state, except in wave,
        //  which keeps the v4.6 behaviour above.)

        bool flying = isFlying(p1) || (dual && p2 && isFlying(p2));
        bool spiderRun = buf.spiderSmart && (p1->m_isSpider || (second && p2->m_isSpider));
        // v2.40: a UFO that lands on something has finished its flap, like a cube jump
        bool ufoRun = buf.ufoSmart && (p1->m_isBird || (second && p2->m_isBird));
        // v2.45 ball: a flip in a tight corridor can land after only a few ticks, and a ball
        // often lands right next to a spike on the new surface, so short flips count as
        // landings and the landing has to last longer before the flip is called safe.
        bool ballRun = buf.ballSmart && (p1->m_isBall || (second && p2->m_isBall));
        int total = delay + horizon;
        int survived = 0;
        int airTicks = 0;
        int groundTicks = 0;
        int tick = 0;
        bool died = false;
        bool safe = false;
        bool variableAlternation = buf.altPredictionDownEvery > 0 && buf.altPredictionUpEvery > 0;
        bool predictedDown = down;
        int downPatternIndex = 0;
        int upPatternIndex = 0;
        auto nextPhaseInterval = [&](bool holding) {
            auto const& pattern = holding ? buf.altPredictionDownPattern : buf.altPredictionUpPattern;
            int count = holding ? buf.altPredictionDownPatternCount : buf.altPredictionUpPatternCount;
            int& index = holding ? downPatternIndex : upPatternIndex;
            if (count > 0) {
                int interval = pattern[index % count];
                ++index;
                return std::max(1, interval);
            }
            return holding ? buf.altPredictionDownEvery : buf.altPredictionUpEvery;
        };
        int nextAlternation = delay + (variableAlternation ? nextPhaseInterval(down) + buf.altNextEdgeShift : 0);

        collectMovers();
        for (; tick < total; ++tick) {
            if (!movers.empty()) {
                placeMovers(tick + 1);  // where they'll be after this step
            }
            buf.simTick = tick;
            // v2.26: orb auto-clicks that wait a few ticks into the orb fire here, if the
            // ghost is still inside an orb by then.
            if (buf.autoAt1 >= 0 && tick >= buf.autoAt1) {
                if (!buf.held1 && buf.ghost1->m_touchingRings->count() > 0) {
                    buf.ghost1->pushButton(PlayerButton::Jump);
                    buf.auto1 = true;
                }
                buf.autoAt1 = -2;
            }
            if (second && buf.autoAt2 >= 0 && tick >= buf.autoAt2) {
                if (!buf.held2 && buf.ghost2->m_touchingRings->count() > 0) {
                    buf.ghost2->pushButton(PlayerButton::Jump);
                    buf.auto2 = true;
                }
                buf.autoAt2 = -2;
            }
            if (tick == delay && !buf.skipPress) {
                pressGhost(buf.ghost1, down);
                if (second) {
                    pressGhost(buf.ghost2, down);
                }
            }
            // Alternate assist forecasts the player's observed rhythm in the prediction
            // ghost. Neither prediction pattern sends these future transitions to the real player.
            if (variableAlternation && !buf.skipPress && tick > delay && tick >= nextAlternation) {
                predictedDown = !predictedDown;
                pressGhost(buf.ghost1, predictedDown);
                if (second) {
                    pressGhost(buf.ghost2, predictedDown);
                }
                nextAlternation += nextPhaseInterval(predictedDown);
            }
            else if (buf.altPredictionEvery > 0 && !buf.skipPress && tick > delay &&
                (tick - delay) % buf.altPredictionEvery == 0) {
                int phase = (tick - delay) / buf.altPredictionEvery;
                bool periodicDown = phase % 2 == 1 ? !down : down;
                pressGhost(buf.ghost1, periodicDown);
                if (second) {
                    pressGhost(buf.ghost2, periodicDown);
                }
            }
            if (buf.retapAt >= 0) {
                // v2.15: a second tap (let go, press, let go): "flip back" for the spider
                int t = tick - delay - buf.retapAt;
                if (t == 0 || t == 1 || t == 5) {
                    pressGhost(buf.ghost1, t == 1);
                    if (second) {
                        pressGhost(buf.ghost2, t == 1);
                    }
                }
            }
            // v2.16: a dash lasts as long as you hold, so the ghost doesn't let go mid-dash.
            if (down && releaseAfter >= 0 && tick == delay + releaseAfter && buf.ghost1->m_isDashing) {
                ++releaseAfter;
            }
            if (releaseAfter >= 0 && tick == delay + releaseAfter) {
                // let go of a press (or, for a wave release, press again: the turn back)
                pressGhost(buf.ghost1, !down);
                if (second) {
                    pressGhost(buf.ghost2, !down);
                }
            }

            // v2.18: with CBF the click lands partway through a tick, so the ghost's first
            // step is only the rest of that tick; after it, ghost and game are on the same
            // tick boundaries again.
            float stepDt = tick == 0 ? dt * buf.firstStep : dt;
            advanceGhost(buf.ghost1, stepDt);
            if (second) {
                advanceGhost(buf.ghost2, stepDt);
            }
            releaseAutoClicks();
            if (buf.ghostDied) {
                died = true;
                break;
            }

            // v2.36: after a mid-tick (CBF) click the game finishes the tick and then runs its
            // normal collision check with the full step, so the ghost does the same.
            checkGhost(layer, buf.ghost1, dt);
            if (!buf.ghostDied && second) {
                checkGhost(layer, buf.ghost2, dt);
            }
            if (buf.ghostDied) {
                died = true;
                break;
            }
            if (buf.recordPath) {
                buf.recordPath->push_back(buf.ghost1->getPosition());
            }
            if (buf.ghostHitPortal) {
                safe = tick >= delay;
                break;
            }

            if (tick < delay) {
                continue;
            }
            ++survived;
            if (survived >= stopAt) {
                break;
            }
            // v2.79: after a portal changed the ghost's mode, only surviving the look-ahead counts
            // (the cube's "landed = safe" rule doesn't fit the new mode)
            if ((flying && !ufoRun) || buf.ghostSwitched) {
                continue;
            }

            bool grounded = buf.ghost1->m_isOnGround && (!second || buf.ghost2->m_isOnGround);
            if (grounded && airTicks > 0 && buf.captureKiller) {
                buf.baseTouchedDown = true;
            }
            if (!grounded) {
                ++airTicks;
                groundTicks = 0;
                continue;
            }
            // v2.15 spider: a flip is a teleport with no air time, so the cube's "was in the
            // air, then landed" never happened and every spider run went the full 130 ticks
            // into obstacles that need your next click. Now: standing on a surface for
            // SPIDER_LAND ticks after the input = the flip is done and safe.
            if (spiderRun) {
                if (++groundTicks >= st(SPIDER_LAND)) {
                    safe = true;
                    break;
                }
                continue;
            }
            if (airTicks < st(ballRun ? 2 : 6)) {
                continue;
            }
            if (groundTicks > st(ballRun ? 8 : 2)) {
                safe = true;
                break;
            }
            ++groundTicks;
        }

        if (!movers.empty()) {
            placeMovers(0);  // back exactly where they were
            movers.clear();
        }
        ++buf.simRuns;
        buf.simTicks += tick + 1;

        // Died or hit an unsimulated portal before the delayed input even fired:
        // longer delays can't do better, stop searching.
        if ((died || buf.ghostHitPortal) && tick < delay) {
            *end = SimEnd::StopSearch;
        }
        restoreObjects();
        buf.ghost1->stopAllActions();
        if (buf.ghost2) {
            buf.ghost2->stopAllActions();
        }
        // v2.79: a ghost that used an orb or dashed started its particles (orb burst, dash
        // fire) right where the orb is, before you got there
        if (buf.firstOrbTick >= 0) {
            buf.ghost1->stopParticles();
            if (buf.ghost2) {
                buf.ghost2->stopParticles();
            }
        }
        buf.simulating = false;
        return safe ? horizon : survived;
    }

    // v2.4: simulate() with the orb setting; if that run touched an orb and didn't make
    // it, also try the other choice (click it / skip it) and keep whichever lives longer,
    // like a player who only clicks the orb when they need it.
    // v2.70: when the current click started being processed.
    // v2.79: the whole click shares its time: each search used to get the full budget on its
    // own, on top of the base run, memory try and orb checks before it, so a wave save on
    // GRIEF could take 2-3 frames at 240 fps. Orb retries stop once the click is over time.
    std::chrono::steady_clock::time_point clickStart;
    bool clickTimed = false;

    double clickMs() {
        return clickTimed ? std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clickStart).count() : 0.0;
    }

    bool clickOverTime() {
        return clickMs() > buf.budgetMs * 1.25;
    }

    // what's left of the click's time for a search
    double searchBudget() {
        return clickTimed ? buf.budgetMs - clickMs() : buf.budgetMs;
    }

    struct ClickTimer {
        ClickTimer() {
            clickStart = std::chrono::steady_clock::now();
            clickTimed = true;
        }
        ~ClickTimer() {
            clickTimed = false;
        }
    };

    int simulateOrbs(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int releaseAfter,
        int stopAt, SimEnd* end) {
        buf.orbClick = buf.assumeOrbs;
        buf.touchedOrb = false;
        int first = simulate(layer, p1, p2, dual, delay, down, horizon, releaseAfter, stopAt, end);
        if (!buf.tryBothOrbs || !buf.smart || !buf.touchedOrb || first >= stopAt || clickOverTime()) {
            return first;
        }
        SimEnd firstEnd = *end;
        buf.orbClick = !buf.assumeOrbs;
        int second = simulate(layer, p1, p2, dual, delay, down, horizon, releaseAfter, stopAt, end);
        buf.orbClick = buf.assumeOrbs;
        if (firstEnd != SimEnd::StopSearch) {
            *end = SimEnd::Ok;
        }
        int best = std::max(first, second);
        // v2.26: some orbs only work when clicked at a specific moment inside them (levels
        // like Orbit). The ghost's orb clicks are also tried 3 and 6 ticks into the orb.
        if (buf.orbTiming && best < stopAt && !clickOverTime()) {
            for (int late : { 3, 6 }) {
                buf.orbClick = true;
                buf.orbClickDelay = st(late);
                SimEnd lateEnd;
                int ret = simulate(layer, p1, p2, dual, delay, down, horizon, releaseAfter, stopAt, &lateEnd);
                buf.orbClickDelay = 0;
                buf.orbClick = buf.assumeOrbs;
                best = std::max(best, ret);
                if (best >= stopAt || clickOverTime()) {
                    break;
                }
            }
        }
        return best;
    }

    // Ghost 1's position after every tick of one run (until it dies or is safe).
    std::vector<CCPoint> recordRun(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon,
        int releaseAfter, bool* died = nullptr, bool traceThroughHits = false, int* collisionAt = nullptr) {
        std::vector<CCPoint> path;
        int firstCollision = -1;
        buf.recordPath = &path;
        buf.recordThroughHits = traceThroughHits;
        buf.recordPathCollision = traceThroughHits ? (collisionAt ? collisionAt : &firstCollision) : nullptr;
        if (collisionAt) *collisionAt = -1;
        buf.orbClick = buf.assumeOrbs;
        SimEnd end;
        simulate(layer, p1, p2, dual, delay, down, horizon, releaseAfter, delay + horizon, &end);
        buf.recordPath = nullptr;
        buf.recordPathCollision = nullptr;
        buf.recordThroughHits = false;
        if (died) {
            *died = traceThroughHits
                ? (collisionAt ? *collisionAt >= 0 : firstCollision >= 0)
                : buf.ghostDied;
        }
        return path;
    }

    // v2.11 path view (F10): coral = input now; cyan = buffered route. Gold marks the
    // buffered fire tick, red marks death, and amber dots mark learned memory risks.
    void drawPath(std::vector<CCPoint> const& path, int delay, ccColor4F color, int collisionAt = -1, CCDrawNode* node = nullptr) {
        if (!node) node = buf.pathNode;
        if (!node) return;
        for (size_t i = 1; i < path.size(); ++i) {
            bool pastHit = collisionAt >= 0 && static_cast<int>(i) > collisionAt;
            auto segmentColor = color;
            if (pastHit) segmentColor.a *= 0.32f;
            auto glowColor = segmentColor;
            glowColor.a *= 0.16f;
            node->drawSegment(path[i - 1], path[i], 3.6f, glowColor);
            node->drawSegment(path[i - 1], path[i], 1.45f, segmentColor);
            if (i % 12 == 0) {
                node->drawDot(path[i], 1.9f,
                    { color.r, color.g, color.b, pastHit ? color.a * 0.36f : color.a });
            }
        }
        if (delay > 0 && delay <= static_cast<int>(path.size())) {
            node->drawDot(path[delay - 1], 3.5f, { 1.f, 0.82f, 0.18f, 1.f });
        }
        if (collisionAt >= 0 && collisionAt < static_cast<int>(path.size())) {
            node->drawDot(path[collisionAt], 4.f, { 1.f, 0.1f, 0.24f, 1.f });
        }
        if (!path.empty()) {
            node->drawDot(path.back(), 2.4f, { color.r, color.g, color.b, 0.95f });
        }
    }

    void showPaths(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int chosen, bool down, int horizon, int releaseAfter) {
        if (!buf.showPath || !buf.pathNode) {
            return;
        }
        // An input that passed straight through does not need its route redrawn every single time.
        static std::chrono::steady_clock::time_point lastShown {};
        auto shownNow = std::chrono::steady_clock::now();
        if (chosen <= 0 && lastShown.time_since_epoch().count() != 0 &&
            shownNow - lastShown < std::chrono::milliseconds(std::clamp(studio.pathRefreshMs, 8, 100) * 2)) {
            return;
        }
        lastShown = shownNow;
        bool slowDecision = clickMs() > buf.budgetMs;   // draw only the delayed route when time is short
        bool alternateWave = (p1->m_isDart || (p2 && p2->m_isDart)) && studioAltActive(p1, p2);
        if (alternateWave) {
            bool mini = (p1->m_isDart && p1->m_vehicleSize < 1.f) || (p2 && p2->m_isDart && p2->m_vehicleSize < 1.f);
            horizon = std::max(horizon, fullWaveHorizon(mini, true));
        }
        horizon = std::min(horizon, st(studio.pathHorizon));
        buf.pathNode->clear();
        // Keep the selected input's route visible briefly, then hand back to the
        // live forecast. Forty-five ticks froze the path for ~188 ms at 240 TPS,
        // long enough to hide several USKC spam reversals.
        buf.pathHoldUntil = tickCount + st(3);
        ccColor4F coral { 1.f, 0.38f, 0.28f, 0.92f };
        ccColor4F cyan { 0.12f, 0.82f, 1.f, 0.96f };
        std::vector<CCPoint> now;
        if (studio.pathShowIdle && !slowDecision) {
            int nowHit = -1;
            now = recordRun(layer, p1, p2, dual, 0, down, horizon, releaseAfter, nullptr, true, &nowHit);
            drawPath(now, 0, coral, nowHit);
        }
        if (studio.pathShowClick && chosen > 0) {
            int delayedHit = -1;
            auto delayed = recordRun(layer, p1, p2, dual, chosen, down, horizon, releaseAfter, nullptr, true, &delayedHit);
            drawPath(delayed, chosen, cyan, delayedHit);
            if (buf.keepNode) {
                buf.keepNode->clear();
                drawPath(delayed, chosen, ccColor4F { 0.12f, 0.82f, 1.f, 0.55f }, delayedHit, buf.keepNode);
                buf.keepTick = tickCount;
            }
        }
        // v2.13: spots on this path where the memory has seen you die a lot.
        if (studio.pathShowIdle && buf.learn && !memory.empty()) {
            int last = INT_MIN;
            for (auto const& point : now) {
                int slice = static_cast<int>(std::floor(point.x / SLICE));
                if (slice != last && memoryRisky(slice)) {
                    buf.pathNode->drawDot(point, 3.5f, { 1.f, 0.6f, 0.1f, 0.95f });
                }
                last = slice;
            }
        }
    }

    // v2.4: record the ghost run for the input that was actually made (drift check).
    void startDrift(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int releaseAfter) {
        finishDrift("");
        if (!buf.driftCheck) {
            return;
        }
        drift.path = recordRun(layer, p1, p2, dual, delay, down, horizon, releaseAfter);
        drift.active = !drift.path.empty();
        // v2.38: a normal click is handled before the tick runs, so the first comparison is
        // one update later. A mid-tick (CBF) click is handled inside the tick, so the ghost's
        // first (partial) step ends at the very next tick start: compare from there.
        drift.index = buf.firstStep < 1.f ? 0 : -1;
        drift.compared = 0;
        drift.maxErr = 0.f;
        drift.maxAt = 0;
    }

    // After every buffer decision: drift check + path view for the input the game gets.
    // v2.70: the extras after the search (robust check, drift check, path view) are skipped
    // once the click has used up its time, so one click can't stall a frame for long.
    void decided(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int releaseAfter) {
        if (g_p2.active) return;   // no drift check / path drawing for Player 2
        memoryNote(p1, delay);
        fun.lastDelay = delay;
        fun.lastDelayTick = tickCount;
        if (clickMs() > buf.budgetMs * 1.5) {
            finishDrift("");  // no time left for the debug runs this click
            return;
        }
        startDrift(layer, p1, p2, dual, delay, down, horizon, releaseAfter);
        showPaths(layer, p1, p2, dual, delay, down, horizon, releaseAfter);
    }

    // Full wave look-ahead from the setting (mini = 15 less).
    int fullWaveHorizon(bool mini, bool alternate = false) {
        int wh = alternate ? std::clamp(studio.altWaveHorizon, 20, 480) : std::clamp(buf.waveHorizon, 20, 240);
        return st(mini ? std::max(20, wh - 15) : wh);
    }

    // v2.6 orb alignment: a press made a little before reaching an orb would be wasted
    // (or jump off the ground instead). Find the delay that lands the press inside the
    // orb, like the cube buffer does for jumps. Returns that delay, or 0.
    // v2.78 hard orb timings (Orbit): every tick you're inside the orb is tried (not just the
    // first 6), the orbs after this one are clicked in the prediction too (with a few
    // timings each), and of the timings that are fully safe the middle one is picked, so a
    // real run a hair off still makes it; with robust saves it must also survive starting
    // half a tick ahead / behind.
    int alignOrb(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int horizon, int releaseAfter, int learned, int maxDelay,
        int* landed) {
        int window = st(std::clamp(buf.orbWindow, 1, 64));
        int found = 0;
        SimEnd end;
        buf.orbClick = false;  // the press being tested is the orb click
        buf.orbWasTiming = false;
        buf.orbAfterFirst = true;
        struct Reset {
            ~Reset() {
                buf.orbClick = buf.assumeOrbs;
                buf.orbAfterFirst = false;
                buf.orbClickDelay = 0;
                buf.ghostShiftX = 0.f;
            }
        } reset;

        // one timing k: your click k ticks from now; later orbs clicked on touch, then 3 / 6 /
        // 10 ticks into them if that dies. Returns ticks survived and whether your click used an orb.
        auto tryAt = [&](int k, bool* used) {
            int best = -1;
            *used = false;
            for (int late : { 0, 3, 6, 10 }) {
                buf.orbClickDelay = st(late);
                int ret = simulate(layer, p1, p2, dual, k, true, horizon, releaseAfter, horizon, &end);
                buf.orbClickDelay = 0;
                if (late == 0) {
                    *used = buf.orbUsedTick >= 0;
                }
                best = std::max(best, ret);
                if (ret >= horizon || buf.orbUsedTick < 0 || !buf.laterOrbTouched || !buf.orbTiming || end == SimEnd::StopSearch) {
                    break;
                }
            }
            return best;
        };
        auto robustAt = [&](int k) {
            if (!buf.robustSaves) {
                return true;
            }
            // Timing slack: the click must still use the orb when it lands "orb margin" ticks
            // early or late (default 1 tick, it used to be half a tick), around where your
            // clicks really land (click bias).
            float unit = (311.58f / 240.f) * (std::abs(p1->m_playerSpeed) / 0.9f);
            float shift = std::max(0.3f, static_cast<float>(studio.orbMargin) * unit);
            float bias = static_cast<float>(studio.clickBias) * unit;
            for (float off : { bias + shift, bias - shift }) {
                buf.ghostShiftX = off;
                bool used;
                int ret = tryAt(k, &used);
                buf.ghostShiftX = 0.f;
                if (ret < horizon || !used) {
                    return false;
                }
            }
            return true;
        };

        // the orb timing that worked at this spot on earlier runs is tried first
        if (learned > 0 && learned <= maxDelay) {
            bool used;
            int ret = tryAt(learned, &used);
            if (used && ret >= horizon) {
                *landed = ret;
                buf.orbConf = 2;
                buf.orbConfAge = 0;
                return learned;
            }
        }

        // 1. Cheap probe: without pressing, does the ghost reach an orb soon, and how long
        //    does it stay inside it?
        buf.skipPress = true;
        buf.orbAfterFirst = false;
        int probeLen = std::min(maxDelay, std::max(window, st(40))) + 2;
        simulate(layer, p1, p2, dual, 0, true, probeLen, -1, probeLen, &end);
        buf.skipPress = false;
        buf.orbAfterFirst = true;
        int touch = buf.firstOrbTick;
        int leave = buf.lastOrbTick;
        std::string trace;  // v2.42: what was tried, for the explain log
        if (touch < 0 || touch > window) {
            return 0;
        }

        bool nowUsed;
        int now = tryAt(0, &nowUsed);
        if (nowUsed && (now >= horizon || !buf.orbTiming)) {
            buf.orbConf = 3;
            buf.orbConfAge = 0;
            return 0;  // pressing now uses the orb and is safe: nothing to fix
        }
        // every tick from just before the orb to the last tick inside it
        int from = nowUsed ? 1 : std::max(1, touch - 1);
        int to = std::min(maxDelay, std::max(leave, touch + st(6)));
        int safeStart = -1, safeEnd = -1;
        int bestRet = -1, bestK = 0;
        int bestAt = -1;  // v2.79: when the best timing crashes, counted from now (k + ret)
        // v2.79: orb clicks get more time (1.75x the budget for the scan, 2x with the robustness
        // check): at 240 fps one frame (~4 ms) only covered about 6 ticks of ORBIT's 20-tick orbs
        double orbBudget = buf.budgetMs * 1.75;
        for (int k = from; k <= to; ++k) {
            if (clickMs() > orbBudget) {
                trace += " (budget)";
                break;
            }
            bool used;
            int ret = tryAt(k, &used);
            if (buf.explain) {
                trace += fmt::format(" +{}:{}{}", k, ret, used ? "o" : "-");
            }
            if (end == SimEnd::StopSearch) {
                break;
            }
            if (!used) {
                if (safeStart >= 0) {
                    break;  // past the orb after a safe stretch
                }
                continue;
            }
            if (ret >= horizon) {
                if (safeStart < 0) {
                    safeStart = k;
                }
                safeEnd = k;
            }
            else if (safeStart >= 0) {
                break;  // the first safe stretch is over
            }
            // v2.79: timings are compared by when they crash counted from now, not from each
            // click: on ORBIT, clicking 9 ticks later lived 59 ticks after its click (crash at
            // t68) vs 50 for clicking now (t50), 18 ticks later, but 59 < 50 + 12 so it was
            // never picked
            if (k + ret > bestAt) {
                bestAt = k + ret;
                bestRet = ret;
                bestK = k;
            }
        }
        if (safeStart >= 0) {
            // the middle of the safe stretch, checked from there outwards for robustness
            int mid = (safeStart + safeEnd) / 2;
            found = mid;
            for (int off = 0; off <= safeEnd - safeStart; ++off) {
                int k = off % 2 == 0 ? mid + off / 2 : mid - (off + 1) / 2;
                if (k < safeStart || k > safeEnd) {
                    continue;
                }
                if (clickMs() > buf.budgetMs * 2.0 || robustAt(k)) {
                    found = k;
                    break;
                }
            }
            *landed = horizon;
        }
        else if (bestK > 0 && bestAt >= std::max(now, 0) + st(12)) {
            found = bestK;  // nothing fully safe: the timing that crashes clearly latest
            *landed = bestRet;
        }
        buf.orbWasTiming = found > 0 && nowUsed;
        // confidence dot: a wide safe window is green, a narrow one yellow, a fallback pick or no pick red
        if (safeStart >= 0 && found > 0) {
            int width = safeEnd - safeStart + 1;
            buf.orbConf = width >= st(6) ? 3 : (width >= st(3) ? 2 : 1);
        }
        else {
            buf.orbConf = 1;
        }
        buf.orbConfAge = 0;
        // v2.42 explain: "+k:ret o" = press k ticks later lives ret ticks and used the orb
        if (buf.explain) {
            say("orb: inside t{}-t{}, press now {} -> {}t |{} => {}{}", touch, leave, nowUsed ? "uses it" : "misses it", now,
                trace.empty() ? " (nothing to try)" : trace, found > 0 ? fmt::format("+{}t", found) : std::string("no pick"),
                safeStart >= 0 ? fmt::format(" (safe +{}..+{}t)", safeStart, safeEnd) : std::string());
        }
        return found;
    }

    // v2.1: best delayed run seen by the last search (shown in the "no save" log line)
    int dbgBestK = 0;
    int dbgBestRet = -1;

    // Gap probe: from here, how long until the ceiling if you hold, and until the floor if you let go.
    // Their sum is the free travel of the gap; a tight (~1 block) gap has a small sum. Returns true for
    // a tight gap. Used by gap centering and by frame spam.
    bool probeGap(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int& holdLife, int& idleLife) {
        if (!buf.ghost1 || clickOverTime()) {
            return false;
        }
        int probe = st(32);
        holdLife = probe;
        idleLife = probe;
        {
            AlternatePredictionScope plainRun(-1, -1, -1);
            SimEnd holdEnd = SimEnd::Ok;
            SimEnd idleEnd = SimEnd::Ok;
            holdLife = simulateOrbs(layer, p1, p2, dual, 0, true, probe, -1, probe, &holdEnd);
            if (holdEnd == SimEnd::StopSearch || clickOverTime()) {
                return false;
            }
            idleLife = simulateOrbs(layer, p1, p2, dual, 0, false, probe, -1, probe, &idleEnd);
            if (idleEnd == SimEnd::StopSearch) {
                return false;
            }
        }
        float speedScale = 0.9f / std::max(0.3f, std::abs(p1->m_playerSpeed));
        int gapLimit = std::max(st(4), static_cast<int>(std::lround(st(studio.sfGap) * speedScale)));
        return holdLife < probe && idleLife < probe && holdLife + idleLife <= gapLimit;
    }

    // v2.7 wave scoring, the wave version of the cube's "landed safely". A wave never
    // lands, so a run that keeps going one way eventually hits a wall even in a normal
    // corridor, where you'd simply turn. So: if the run crashes, try turning back a bit
    // before the crash; if that turn survives, you had time to react and the run counts
    // as safe. Only crashes you couldn't have turned away from count as deaths.
    // v2.10: three turn points are tried (4, 8 and 12 ticks before the crash) instead of
    // one, so "you could have turned away" is found whenever it's true.
    constexpr int WAVE_TURN_MARGINS[] = { 4, 8, 12 };
    constexpr int WAVE_MIN_TURN = 6;      // a turn sooner than this after the input is too fast to count
    constexpr int WAVE_ESCAPE = 16;       // the turn must then survive this many ticks

    // `target`: surviving this long is already good enough, so the run stops there and the
    // turn-away checks are skipped (v2.10: saves most of the work in the search).
    int waveScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int target,
        SimEnd* end) {
        int ret = simulateOrbs(layer, p1, p2, dual, delay, down, horizon, -1, target, end);
        if (ret >= target || *end == SimEnd::StopSearch || buf.altPredictionEvery > 0) {
            return ret;
        }
        for (int margin : WAVE_TURN_MARGINS) {
            int turnAt = ret - st(margin);
            if (turnAt < st(WAVE_MIN_TURN)) {
                break;
            }
            int length = std::min(horizon, turnAt + st(WAVE_ESCAPE));
            SimEnd turnEnd;
            int turned = simulateOrbs(layer, p1, p2, dual, delay, down, length, turnAt, length, &turnEnd);
            if (turned >= length) {
                if (buf.captureKiller && buf.baseEscape < 0) {
                    buf.baseEscape = turnAt;
                    buf.baseCrash = ret;
                }
                return horizon;
            }
        }
        return ret;
    }

    // v2.7 wave search, same rules as the cube: the smallest delay that is safe (or
    // clearly better than now) wins, so turns are only held back as long as needed.
    // v2.12 robot scoring. A robot press is a jump whose height you set by when you let go,
    // so "pressing now crashes" only if NO release time saves it. Your usual hold is tried
    // first, then short to long holds. (Before, only your usual hold was tested, so a press
    // that needed a shorter or longer hold looked like a death and got delayed for nothing.)
    constexpr int ROBOT_HOLDS[] = { 6, 14, 26, 45 };
    int robotUsedHold = -1;   // hold length that made the last robot run survive

    // v2.73: quick pass of a robot / UFO / spider search: only the plain run per delay (your
    // usual robot hold, no extra flap / flip). Those modes tried up to 5x more runs per
    // delay, so the search ran out of time after 2-10 delays and missed saves further out.
    bool quickScore = false;

    int robotScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, int horizon, int target, SimEnd* end) {
        int usual = robotHold.pick(st(20), 50);
        int best = simulateOrbs(layer, p1, p2, dual, delay, true, horizon, usual, target, end);
        robotUsedHold = usual;
        if (best >= target || *end == SimEnd::StopSearch || quickScore) {
            return best;
        }
        for (int hold : ROBOT_HOLDS) {
            if (std::abs(st(hold) - usual) < st(3)) {
                continue;
            }
            SimEnd e;
            int ret = simulateOrbs(layer, p1, p2, dual, delay, true, horizon, st(hold), target, &e);
            if (ret > best) {
                best = ret;
                robotUsedHold = st(hold);
            }
            if (best >= target) {
                break;
            }
        }
        return best;
    }

    // v2.15 spider scoring, like the wave's: if the run crashes, tap again (flip back) a
    // little before the crash; if that survives, you could have reacted, so it's safe.
    constexpr int SPIDER_RETAP[] = { 4, 8, 12 };
    constexpr int SPIDER_MIN_RETAP = 4;

    int spiderScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, int horizon, int releaseAfter, int target,
        SimEnd* end) {
        int ret = simulateOrbs(layer, p1, p2, dual, delay, true, horizon, releaseAfter, target, end);
        if (ret >= target || *end == SimEnd::StopSearch || quickScore) {
            return ret;
        }
        for (int margin : SPIDER_RETAP) {
            int at = ret - st(margin);
            if (at < st(SPIDER_MIN_RETAP)) {
                break;
            }
            int length = std::min(horizon, at + st(16));
            buf.retapAt = at;
            SimEnd e;
            int retapped = simulateOrbs(layer, p1, p2, dual, delay, true, length, releaseAfter, length, &e);
            buf.retapAt = -1;
            if (retapped >= length) {
                if (buf.captureKiller && buf.baseEscape < 0) {
                    buf.baseEscape = at;
                    buf.baseCrash = ret;
                }
                return horizon;
            }
        }
        return ret;
    }

    // Same rules as the cube: the smallest delay that is safe (or clearly better) wins.
    // v2.72: the crash happens at the same moment however late the input comes (delay k
    // dies k ticks sooner, for the first SAME_CRASH_TRIES delays): it's the next obstacle's,
    // not this input's, so the rest of the search can't help. Robot and UFO searches spent
    // their whole time budget on exactly that, on almost every input.
    constexpr int SAME_CRASH_TRIES = 4;
    bool crashFixed = false;  // the last search stopped for that reason

    bool sameCrash(int k, int ret, int base, int& streak) {
        if (streak >= 0 && std::abs(k + ret - base) <= 1) {
            ++streak;
        }
        else {
            streak = -1;
        }
        if (streak >= SAME_CRASH_TRIES) {
            crashFixed = true;
            return true;
        }
        return false;
    }

    int searchSmallest(int horizon, int maxDelay, int base, int* landed, std::function<int(int, int, SimEnd*)> const& score,
        double budget = -1.0, bool quiet = false, bool needFull = false) {
        if (budget < 0.0) {
            budget = buf.budgetMs;
        }
        budget = std::min(budget, searchBudget());
        using clock = std::chrono::steady_clock;
        auto start = clock::now();
        int target = needFull ? horizon : std::min(horizon, base + st(12));
        dbgBestK = 0;
        dbgBestRet = -1;
        int streak = 0;
        for (int k = 1; k <= maxDelay; ++k) {
            SimEnd end;
            int ret = score(k, target, &end);
            if (ret > dbgBestRet) {
                dbgBestRet = ret;
                dbgBestK = k;
            }
            if (end == SimEnd::StopSearch) {
                return 0;
            }
            if (ret >= target) {
                *landed = ret;
                return k;
            }
            if (sameCrash(k, ret, base, streak)) {
                return 0;
            }
            if (std::chrono::duration<double, std::milli>(clock::now() - start).count() > budget) {
                if (!quiet) {
                    say("prediction budget hit at +{} ticks", k);
                }
                return 0;
            }
        }
        return 0;
    }

    // v2.73: quick pass over every delay first (half the budget), then the full scoring with
    // whatever time is left.
    int searchTwoPass(int horizon, int maxDelay, int base, int* landed, std::function<int(int, int, SimEnd*)> const& score,
        bool needFull = false) {
        quickScore = true;
        int k = searchSmallest(horizon, maxDelay, base, landed, score, searchBudget() * 0.5, true, needFull);
        quickScore = false;
        if (k > 0) {
            return k;
        }
        crashFixed = false;
        double left = buf.budgetMs - clickMs();
        if (left < 0.5) {
            say("prediction budget hit (quick pass only)");
            return 0;
        }
        return searchSmallest(horizon, maxDelay, base, landed, score, left, false, needFull);
    }

    // v2.25 ship scoring. Like the wave: a crash only counts if you couldn't have recovered.
    // Ship has momentum, so recovery (the opposite input) is tried earlier before the crash:
    // 8, 16 and 24 ticks.
    constexpr int SHIP_RECOVER[] = { 8, 16, 24 };

    int shipScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int target,
        SimEnd* end) {
        int ret = simulateOrbs(layer, p1, p2, dual, delay, down, horizon, -1, target, end);
        if (ret >= target || *end == SimEnd::StopSearch || buf.altPredictionEvery > 0) {
            return ret;
        }
        for (int margin : SHIP_RECOVER) {
            int turnAt = ret - st(margin);
            if (turnAt < st(WAVE_MIN_TURN)) {
                break;
            }
            int length = std::min(horizon, turnAt + st(WAVE_ESCAPE));
            SimEnd turnEnd;
            int turned = simulateOrbs(layer, p1, p2, dual, delay, down, length, turnAt, length, &turnEnd);
            if (turned >= length) {
                if (buf.captureKiller && buf.baseEscape < 0) {
                    buf.baseEscape = turnAt;
                    buf.baseCrash = ret;
                }
                return horizon;
            }
        }
        return ret;
    }

    // v2.40 UFO scoring. One flap and then nothing falls for the whole look-ahead into
    // whatever is below, which your next flap would have avoided. So if the run crashes,
    // try one more flap 6 / 12 / 18 ticks before the crash; if that survives, you could have
    // flapped, so the click counts as safe. Crashing up into something can't be fixed by
    // flapping, so those stay real crashes.
    constexpr int UFO_REFLAP[] = { 6, 12, 18 };

    int ufoScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, int horizon, int releaseAfter, int target,
        SimEnd* end) {
        int ret = simulateOrbs(layer, p1, p2, dual, delay, true, horizon, releaseAfter, target, end);
        if (ret >= target || *end == SimEnd::StopSearch || quickScore || buf.altPredictionEvery > 0) {
            return ret;
        }
        for (int margin : UFO_REFLAP) {
            int at = ret - st(margin);
            if (at < st(4)) {
                break;
            }
            int length = std::min(horizon, at + st(20));
            buf.retapAt = at;
            SimEnd e;
            int reflapped = simulateOrbs(layer, p1, p2, dual, delay, true, length, releaseAfter, length, &e);
            buf.retapAt = -1;
            if (reflapped >= length) {
                if (buf.captureKiller && buf.baseEscape < 0) {
                    buf.baseEscape = at;
                    buf.baseCrash = ret;
                }
                return horizon;
            }
        }
        return ret;
    }

    bool marginUsed = false;  // the last wave save took the +1 tick safety margin

    int searchWave(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, bool down, int horizon, int maxDelay, int base,
        int* landed, bool requireFullSurvival = false, int preferredDelay = 0) {
        marginUsed = false;
        using clock = std::chrono::steady_clock;
        auto start = clock::now();
        double budget = searchBudget();
        int target = requireFullSurvival ? horizon : std::min(horizon, base + st(12));
        dbgBestK = 0;
        dbgBestRet = -1;
        if (requireFullSurvival && preferredDelay > 0 && preferredDelay <= maxDelay) {
            SimEnd preferredEnd;
            int preferredRet = waveScore(layer, p1, p2, dual, preferredDelay, down, horizon, target, &preferredEnd);
            if (preferredEnd == SimEnd::StopSearch) {
                return 0;
            }
            if (preferredRet >= target && preferredEnd != SimEnd::StopSearch) {
                *landed = preferredRet;
                return preferredDelay;
            }
        }
        for (int k = 1; k <= maxDelay; ++k) {
            if (requireFullSurvival && k == preferredDelay) {
                continue;
            }
            SimEnd end;
            int ret = waveScore(layer, p1, p2, dual, k, down, horizon, target, &end);
            if (ret > dbgBestRet) {
                dbgBestRet = ret;
                dbgBestK = k;
            }
            if (end == SimEnd::StopSearch) {
                return 0;
            }
            if (ret >= target) {
                *landed = ret;
                // v2.10 safety margin: the first safe delay is the one that only just clears
                // the spike corner, so a pixel of difference in the real run can still clip
                // it. If the next two delays are safe too, take the middle one: a tick of
                // room on both sides.
                if (buf.waveMargin && k + 2 <= maxDelay) {
                    SimEnd e1, e2;
                    if (waveScore(layer, p1, p2, dual, k + 1, down, horizon, target, &e1) >= target &&
                        waveScore(layer, p1, p2, dual, k + 2, down, horizon, target, &e2) >= target) {
                        marginUsed = true;
                        return k + 1;
                    }
                }
                return k;
            }
            if (std::chrono::duration<double, std::milli>(clock::now() - start).count() > budget) {
                say("prediction budget hit at +{} ticks", k);
                return 0;
            }
        }
        return 0;
    }

    // Alternate Assist scores each real press/release against its own short wave lookahead.
    // It may delay an edge only when that delay buys the configured survival gain. The
    // preferred offset keeps adjacent edges phase-aligned when the same timing still works.
    int searchAlternateWave(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, bool down,
        int horizon, int maxDelay, int base, int preferredDelay, int* landed, int smoothTarget = -1) {
        int target = std::min(horizon, base + st(studio.altWaveMinGain));
        using clock = std::chrono::steady_clock;
        auto start = clock::now();
        double budget = searchBudget();
        dbgBestK = 0;
        dbgBestRet = base;

        // Every candidate is scored to the END of the look-ahead, so candidates can be compared. Order of
        // preference: a correction that survives the whole look-ahead (and, among those, the one that still
        // survives the biggest timing error) > the one that survives longest (it must still reach the min-gain
        // target) > the smaller / earlier one.
        int bestDelay = 0;
        int bestScore = target - 1;
        std::vector<int> safeDelays;   // corrections that survive the whole look-ahead (the first three found)
        auto consider = [&](int delay) {
            SimEnd end;
            int score = waveScore(layer, p1, p2, dual, delay, down, horizon, horizon, &end);
            if (score > dbgBestRet) {
                dbgBestRet = score;
                dbgBestK = delay;
            }
            if (end == SimEnd::StopSearch || score < target) {
                return false;
            }
            if (score >= horizon) {
                safeDelays.push_back(delay);
                if (bestScore < horizon) {
                    bestDelay = delay;
                    bestScore = horizon;
                }
                return static_cast<int>(safeDelays.size()) >= studio.altSafeKeep;
            }
            if (score > bestScore) {
                bestDelay = delay;
                bestScore = score;
            }
            return false;
        };

        // Look order: with an even-zigzag target the delays closest to it come first (so the first three safe ones
        // are the evenest ones); otherwise the carried offset and then the smaller delays, as before.
        std::vector<int> order;
        for (int delay = 1; delay <= maxDelay; ++delay) {
            order.push_back(delay);
        }
        if (smoothTarget >= 1) {
            std::stable_sort(order.begin(), order.end(), [smoothTarget](int a, int b) {
                return std::abs(a - smoothTarget) < std::abs(b - smoothTarget);
            });
        }
        else if (preferredDelay > 0 && preferredDelay <= maxDelay) {
            order.erase(std::remove(order.begin(), order.end(), preferredDelay), order.end());
            order.insert(order.begin(), preferredDelay);
        }
        bool enough = false;
        for (int delay : order) {
            if (enough || clickOverTime()) break;
            enough = consider(delay);
            if (!enough && std::chrono::duration<double, std::milli>(clock::now() - start).count() > budget) {
                say("alternate wave search budget hit at +{}t", delay);
                break;
            }
        }

        if (safeDelays.size() >= 2) {
            // Several corrections survive. Surviving is not the whole story in a tight spam: pick the one that
            // still survives when the real timing is off by 0.5, 1, 1.5 ... ticks either way (around where your
            // clicks really land), i.e. the one with the most room.
            float unit = (311.58f / 240.f) * (std::abs(p1->m_playerSpeed) / 0.9f);
            float bias = static_cast<float>(studio.clickBias) * unit;
            int chosen = safeDelays.front();
            int chosenSlack = -1;
            for (int delay : safeDelays) {
                int slack = 0;
                for (int level = 1; level <= studio.altRoomLevels && !clickOverTime(); ++level) {
                    float shift = 0.5f * static_cast<float>(level) * unit;
                    bool ok = true;
                    for (float offset : { bias + shift, bias - shift }) {
                        buf.ghostShiftX = offset;
                        SimEnd slackEnd;
                        int route = waveScore(layer, p1, p2, dual, delay, down, horizon, horizon, &slackEnd);
                        buf.ghostShiftX = 0.f;
                        if (route < horizon || slackEnd == SimEnd::StopSearch) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) break;
                    slack = level;
                }
                // Enough room is enough: from 2 steps up they count the same, and then the first in the look order wins
                // (the evenest zigzag, or the carried offset). Less room loses to more room.
                int bucket = std::min(slack, 2);
                if (bucket > chosenSlack) {
                    chosen = delay;
                    chosenSlack = bucket;
                }
            }
            bestDelay = chosen;
            bestScore = horizon;
            if (buf.explain) {
                say("alternate wave: {} corrections survive, +{}t has the most room ({} steps)", safeDelays.size(), chosen, chosenSlack);
            }
        }
        if (bestDelay > 0) {
            *landed = bestScore;
            return bestDelay;
        }
        return 0;
    }

    // Find the smallest delay that clearly beats pressing now. 0 = don't buffer.
    int searchDelay(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, bool down, bool anyWave, int horizon, int maxDelay,
        int releaseAfter, int base, int* landed, bool needFull = false, int preferredDelay = 0) {
        using clock = std::chrono::steady_clock;
        auto start = clock::now();
        double budget = searchBudget();
        auto overBudget = [&] {
            return std::chrono::duration<double, std::milli>(clock::now() - start).count() > budget;
        };

        int target = needFull ? horizon : std::min(horizon, base + st(12));
        dbgBestK = 0;
        dbgBestRet = -1;
        int sameStreak = 0;
        int waveBestK = 0;
        int waveBestRet = base;
        if (needFull && preferredDelay > 0 && preferredDelay <= maxDelay) {
            SimEnd preferredEnd;
            int preferredRet = simulateOrbs(layer, p1, p2, dual, preferredDelay, down, horizon, releaseAfter, target, &preferredEnd);
            if (preferredEnd == SimEnd::StopSearch) {
                return 0;
            }
            if (preferredRet >= target) {
                *landed = preferredRet;
                return preferredDelay;
            }
        }
        for (int k = 1; k <= maxDelay; ++k) {
            if (needFull && k == preferredDelay) {
                continue;
            }
            SimEnd end;
            // Wave needs the full run to compare gains; elsewhere reaching `target` is enough.
            int ret = simulateOrbs(layer, p1, p2, dual, k, down, horizon, releaseAfter, anyWave ? horizon : target, &end);
            if (ret > dbgBestRet) {
                dbgBestRet = ret;
                dbgBestK = k;
            }
            if (end == SimEnd::StopSearch) {
                break;
            }
            if (anyWave) {
                // v4.6 wave rule: delaying a turn in a flat corridor naturally adds ~2 ticks
                // per tick of delay against the opposite wall, so only buffer when the delayed
                // run clearly clears a spike, saw or corner. A fully safe run needs
                // 2*k+6 extra ticks and ends the search; otherwise keep the best run that
                // gains at least 2*k+10 and beats the previous best by more than 2.
                if (ret >= horizon && ret - base >= k * 2 + 6) {
                    waveBestK = k;
                    waveBestRet = ret;
                    break;
                }
                if (ret >= base + k * 2 + 10 && ret > waveBestRet + 2) {
                    waveBestK = k;
                    waveBestRet = ret;
                    if (ret >= horizon) {
                        break;
                    }
                }
            }
            else if (ret < target && sameCrash(k, ret, base, sameStreak)) {
                break;
            }
            else if (ret >= target) {
                // v2.63: a delay that only lives 12 ticks longer can still crash later in the same
                // jump (Acheron's first jump: saved, then died on landing). Prefer the smallest
                // delay whose jump lands safely; if none does, keep this one.
                SimEnd full;
                if (ret < horizon && simulateOrbs(layer, p1, p2, dual, k, down, horizon, releaseAfter, horizon, &full) < horizon) {
                    for (int j = k + 1; j <= maxDelay && !overBudget(); ++j) {
                        SimEnd e;
                        int r = simulateOrbs(layer, p1, p2, dual, j, down, horizon, releaseAfter, horizon, &e);
                        if (e == SimEnd::StopSearch) {
                            break;
                        }
                        if (r >= horizon) {
                            *landed = r;
                            return j;
                        }
                    }
                }
                *landed = ret;
                return k;
            }
            if (overBudget()) {
                say("prediction budget hit at +{} ticks", k);
                break;
            }
        }
        if (anyWave && waveBestK > 0) {
            *landed = waveBestRet;
            return waveBestK;
        }
        return 0;
    }

    // v2.75 run check (test bench). Every attempt played from the level start is recorded
    // (the inputs the game got, and where you were on every physics step); the longest one
    // per level is kept. "Check run" replays it on the hidden ghost at the start of your next
    // attempt and reports how long the ghost follows the recording. Run it after an update:
    // if the ghost stops matching much earlier than before, the update changed the physics
    // copy. Moving objects and triggers aren't replayed, so it's a comparison between
    // versions, not a percentage that should reach 100.
    struct BenchRun {
        float startX = 0.f, startY = 0.f, dt = TICK;
        std::vector<std::pair<int, bool>> inputs;
        std::vector<CCPoint> track;
    };
    BenchRun benchLive;
    bool benchRecording = false;
    bool benchNeedStart = false;  // next P1 step is the first of an attempt
    bool benchPending = false;    // "Check run" was pressed
    std::string lastBench;        // last result, for the stats popup

    std::filesystem::path benchFile() {
        return Mod::get()->getSaveDir() / "bench" / (memoryKey + ".txt");
    }

    size_t benchSavedTicks() {
        std::ifstream in(benchFile());
        float x, y, dt;
        size_t ticks = 0;
        if (in >> x >> y >> dt >> ticks) {
            return ticks;
        }
        return 0;
    }

    bool benchLoad(BenchRun& r) {
        std::ifstream in(benchFile());
        size_t ticks = 0, inputs = 0;
        if (!(in >> r.startX >> r.startY >> r.dt >> ticks >> inputs)) {
            return false;
        }
        for (size_t i = 0; i < inputs; ++i) {
            int t, d;
            if (!(in >> t >> d)) {
                return false;
            }
            r.inputs.push_back({ t, d != 0 });
        }
        for (size_t i = 0; i < ticks; ++i) {
            float x, y;
            if (!(in >> x >> y)) {
                return false;
            }
            r.track.push_back({ x, y });
        }
        return !r.track.empty();
    }

    void benchStart(PlayLayer* layer, PlayerObject* p1) {
        benchLive = {};
        benchLive.startX = p1->getPosition().x;
        benchLive.startY = p1->getPosition().y;
        benchLive.dt = buf.tickLength;
        // only runs from the level start (no practice checkpoint)
        bool fromCheckpoint = layer->m_isPracticeMode && layer->m_checkpointArray && layer->m_checkpointArray->count() > 0;
        benchRecording = !memoryKey.empty() && !fromCheckpoint;
    }

    void benchInput(bool down) {
        if (benchRecording) {
            benchLive.inputs.push_back({ static_cast<int>(benchLive.track.size()), down });
        }
    }

    void benchStep(PlayerObject* p1) {
        if (benchRecording) {
            benchLive.track.push_back(p1->getPosition());
            if (benchLive.track.size() > 200000) {
                benchRecording = false;
            }
        }
    }

    // end of an attempt: keep it if it's the longest run from the start so far
    // v2.77: the best run is kept in memory and only written when you leave or finish the
    // level; writing it on every new-best death made a hitch right when you died.
    BenchRun benchBest;
    size_t benchBestTicks = 0;   // longest run known for this level (memory or file)
    bool benchBestDirty = false;

    void benchFinish() {
        if (!benchRecording) {
            return;
        }
        benchRecording = false;
        if (benchLive.track.size() < 240 || benchLive.track.size() <= benchBestTicks) {
            return;
        }
        benchBestTicks = benchLive.track.size();
        benchBest = std::move(benchLive);
        benchLive = {};
        benchBestDirty = true;
    }

    void benchFlush() {
        if (!benchBestDirty || memoryKey.empty()) {
            return;
        }
        benchBestDirty = false;
        std::error_code ec;
        std::filesystem::create_directories(benchFile().parent_path(), ec);
        std::ofstream out(benchFile(), std::ios::trunc);
        out << benchBest.startX << ' ' << benchBest.startY << ' ' << benchBest.dt << ' ' << benchBest.track.size() << ' '
            << benchBest.inputs.size() << '\n';
        for (auto const& [t, d] : benchBest.inputs) {
            out << t << ' ' << (d ? 1 : 0) << '\n';
        }
        for (auto const& p : benchBest.track) {
            out << p.x << ' ' << p.y << '\n';
        }
    }

    void benchCheck(PlayLayer* layer, PlayerObject* p1) {
        benchFlush();
        BenchRun r;
        if (!benchLoad(r)) {
            say("run check: no recorded run for this level yet (play one attempt from the start first)");
            return;
        }
        if (std::abs(r.startX - p1->getPosition().x) > 0.5f || std::abs(r.startY - p1->getPosition().y) > 0.5f) {
            say("run check: this attempt doesn't start where the recording did (start from the beginning, no checkpoint)");
            benchPending = true;
            return;
        }
        buf.simulating = true;
        markLevelNodes(layer);
        buf.ghostDied = false;
        buf.ghostHitPortal = false;
        buf.haveRealRect = false;
        buf.harmless.clear();
        buf.orbClick = false;  // orbs only through the recorded inputs
        buf.retapAt = -1;
        copyState(buf.ghost1, p1);
        buf.held1 = false;
        buf.auto1 = false;
        buf.autoAt1 = -1;
        size_t next = 0;
        int firstOff = -1;
        float maxErr = 0.f;
        int t = 0;
        char const* end = "end of the recording";
        auto clockStart = std::chrono::steady_clock::now();
        for (; t < static_cast<int>(r.track.size()); ++t) {
            while (next < r.inputs.size() && r.inputs[next].first <= t) {
                pressGhost(buf.ghost1, r.inputs[next].second);
                ++next;
            }
            buf.simTick = t;
            advanceGhost(buf.ghost1, r.dt);
            releaseAutoClicks();
            if (!buf.ghostDied) {
                checkGhost(layer, buf.ghost1, r.dt);
            }
            if (buf.ghostDied) {
                end = "the ghost died";
                break;
            }
            if (buf.ghostHitPortal) {
                end = "a mode portal (predictions stop there)";
                break;
            }
            auto g = buf.ghost1->getPosition();
            float err = std::hypot(g.x - r.track[t].x, g.y - r.track[t].y);
            maxErr = std::max(maxErr, err);
            if (err > 3.f && firstOff < 0) {
                firstOff = t;
            }
        }
        restoreObjects();
        buf.ghost1->stopAllActions();
        buf.simulating = false;
        buf.orbClick = buf.assumeOrbs;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clockStart).count();
        int matched = firstOff < 0 ? t : firstOff;
        lastBench = fmt::format("{}/{} steps ({:.0f}%) match, stopped at {}", matched, r.track.size(),
            100.0 * matched / r.track.size(), end);
        say("run check: {} | max error {:.1f}u{} | {:.0f} ms", lastBench, maxErr,
            std::abs(r.dt - buf.tickLength) > 1e-4f ? " | recorded at another frame rate" : "", ms);
        std::error_code ec;
        std::filesystem::create_directories(Mod::get()->getSaveDir(), ec);
        std::ofstream(Mod::get()->getSaveDir() / "run-check-results.txt", std::ios::app)
            << Mod::get()->getVersion().toVString() << ' ' << memoryKey << ": " << lastBench << '\n';
    }

    // v2.65 live path (F10): between clicks the path keeps updating at a wall-clock interval:
    // blue = where you go if you do nothing, yellow = where a click right now takes you.
    // After a click, that click's path (red / green) stays up briefly first.
    void livePath(PlayLayer* layer, PlayerObject* p1) {
        if (!buf.showPath || buf.perfMode || !buf.pathNode || !buf.ghost1 || buf.simulating || p1->m_isDead || layer->m_isPlatformer) {
            return;
        }
        if (tickCount < buf.pathHoldUntil || !buf.queue.empty()) {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        auto refreshEvery = std::chrono::milliseconds(std::clamp(static_cast<int>(static_cast<float>(studio.pathRefreshMs) / perfGuardScale()), 8, 200));
        if (buf.lastPathUpdate.time_since_epoch().count() != 0 && now - buf.lastPathUpdate < refreshEvery) {
            return;
        }
        bool twoPlayer = layer->m_level && layer->m_level->m_twoPlayerMode;
        bool dual = !twoPlayer && layer->m_gameState.m_isDualMode && layer->m_player2;
        auto* p2 = dual ? layer->m_player2 : nullptr;
        bool wave = p1->m_isDart || (p2 && p2->m_isDart);
        bool ship = p1->m_isShip || (p2 && p2->m_isShip);
        bool flyer = isFlying(p1) || (p2 && isFlying(p2));
        bool mini = (p1->m_isDart && p1->m_vehicleSize < 1.f) || (p2 && p2->m_isDart && p2->m_vehicleSize < 1.f);
        bool predictAlt = wave && studio.pathForecast && studioAltActive(p1, p2) &&
            (buf.altDownSampleCount > 0 || buf.altUpSampleCount > 0);
        int horizon = wave ? fullWaveHorizon(mini, predictAlt) : st(flyer ? 48 : std::clamp(buf.groundHorizon, 48, 240));
        horizon = std::min(horizon, st(studio.pathHorizon));
        bool down = !buf.appliedInput;
        int releaseAfter = down && !wave && !ship ? holdEstimate() : -1;

        int altDown = predictAlt ? buf.altDownEvery : -1;
        int altUp = predictAlt ? buf.altUpEvery : -1;
        int altPeriod = predictAlt ? (altDown + altUp + 1) / 2 : -1;
        auto downForecast = predictAlt ? alternatePhaseForecast(true) : AlternatePhaseForecast{};
        auto upForecast = predictAlt ? alternatePhaseForecast(false) : AlternatePhaseForecast{};
        AlternatePredictionScope alternatePathScope(altPeriod, altDown, altUp, downForecast, upForecast);

        buf.pathNode->clear();
        if (studio.pathShowIdle) {
            int idleHit = -1;
            buf.skipPress = true;
            auto idle = recordRun(layer, p1, p2, dual, 0, down, horizon, -1, nullptr, true, &idleHit);
            buf.skipPress = false;
            drawPath(idle, 0, { 0.45f, 0.7f, 1.f, 0.6f }, idleHit);
        }
        if (studio.pathShowClick) {
            int clickHit = -1;
            auto click = recordRun(layer, p1, p2, dual, 0, down, horizon, releaseAfter, nullptr, true, &clickHit);
            drawPath(click, 0, { 1.f, 0.85f, 0.3f, 0.85f }, clickHit);
        }
        buf.lastPathUpdate = std::chrono::steady_clock::now();
    }

    // Spam-safe: queued events keep their order and each one gets its own physics step, so a
    // very short tap (press then release a moment later) is never squashed into one step.
    void queueInput(int ticks, bool down) {
        if (!buf.queue.empty()) {
            ticks = std::max(ticks, buf.queue.back().ticksLeft + 1);
        }
        if (buf.queue.size() >= 24) {
            // something is badly wrong (a stuck queue): keep the newest events only
            buf.queue.pop_front();
            for (size_t i = 0; i < buf.queue.size(); ++i) {
                buf.queue[i].ticksLeft = std::min(buf.queue[i].ticksLeft, static_cast<int>(i));
            }
            ticks = std::max(ticks, static_cast<int>(buf.queue.size()));
        }
        buf.queue.push_back({ ticks, down });
    }

    // v2.51 practice stats: every save (or, in training, every input that would have been
    // saved) is how early you were, in 240/s ticks.
    struct Stats {
        int saves = 0;
        int earlySum = 0;
        int attempts = 0;            // attempts that had at least one save
        std::map<int, int> savesAt;  // level percent -> saves there
        // Alternate Assist is player-edge driven. Keep separate decisions so a
        // run log shows whether it re-timed an edge or passed it through.
        int altEdges = 0;
        int altAdjusted = 0;
        int altNoGain = 0;
        int altMarginRejected = 0;
        int altPortalPass = 0;
        int altDelaySum = 0;
    };
    Stats attemptStats;
    Stats sessionStats;
    std::string statsKey;

    enum class AlternateResult { Adjusted, NoGain, MarginRejected, PortalPass };

    void noteAlternateResult(AlternateResult result, int delay = 0) {
        for (auto* s : { &attemptStats, &sessionStats }) {
            ++s->altEdges;
            switch (result) {
                case AlternateResult::Adjusted:
                    ++s->altAdjusted;
                    s->altDelaySum += delay;
                    break;
                case AlternateResult::NoGain: ++s->altNoGain; break;
                case AlternateResult::MarginRejected: ++s->altMarginRejected; break;
                case AlternateResult::PortalPass: ++s->altPortalPass; break;
            }
        }
    }

    void noteSave(PlayLayer* layer, int ticks) {
        int t = static_cast<int>(std::lround(ticks / stepScale()));
        int pct = std::clamp(static_cast<int>(layer->getCurrentPercent()), 0, 100);
        for (auto* s : { &attemptStats, &sessionStats }) {
            ++s->saves;
            s->earlySum += t;
            ++s->savesAt[pct];
        }
        // v2.56: a ding for every real save (training mode shows its hint instead)
        if (buf.saveDing && !buf.train) {
            FMODAudioEngine::sharedEngine()->playEffect("counter003.ogg");
        }
    }

    std::string attemptSummary() {
        auto const& a = attemptStats;
        std::string text = fmt::format("{} {} | avg {:.1f}t ({:.2f}f) early", a.saves, buf.train ? (a.saves == 1 ? "early click" : "early clicks")
            : (a.saves == 1 ? "save" : "saves"), static_cast<double>(a.earlySum) / a.saves, a.earlySum * TICK / a.saves);
        if (a.altEdges > 0) {
            text += fmt::format(" | alt {}/{} adjusted", a.altAdjusted, a.altEdges);
        }
        return text;
    }

    // At the end of an attempt (death, restart or level complete).
    // v2.52: logged to the console only (no popup after a death)
    void endAttempt() {
        if (attemptStats.saves > 0) {
            ++sessionStats.attempts;
            say("attempt: {}", attemptSummary());
        }
        attemptStats = {};
    }

    // v2.51 training mode: the input goes through untouched, and you're told how early it was.
    bool trainInstead(PlayLayer* layer, int ticks, bool down, char const* what) {
        if (!buf.train) {
            return false;
        }
        noteSave(layer, ticks);
        say("TRAIN {} {}: {}t ({:g}f) early", what, down ? "press" : "release", ticks, ticks * TICK);
        if (buf.hintLabel) {
            buf.hintLabel->setString(fmt::format("{} {:g}f early", down ? "click" : "release", ticks * TICK).c_str());
            buf.hintLabel->stopAllActions();
            buf.hintLabel->setOpacity(230);
            buf.hintLabel->setScale(0.5f);
            buf.hintLabel->runAction(CCSequence::create(CCEaseBackOut::create(CCScaleTo::create(0.12f, 0.6f)), CCDelayTime::create(0.5f),
                CCFadeOut::create(0.4f), nullptr));
        }
        return true;
    }

    // v2.51 per-level settings: your switches (v2.59: not the stage, not low latency) are remembered for each level and
    // come back when you open it again. Outside a level (and in levels you haven't changed
    // anything in) the normal settings apply.
    constexpr char const* PROFILE_KEYS[] = { "robust-saves", "orb-align", "orb-timing", "level-memory",
        "more-time-death-spots", "ship-buffer", "ufo-smart", "ball-smart", "swing-smart", "cbf-sync", "predict-moving-objects",
        "training-mode" };

    struct Globals {
        bool have = false;
        std::vector<bool> values;
    } globals;

    std::string profileKey() {
        return memoryKey.empty() ? std::string() : "profile-" + memoryKey;
    }

    void saveProfile() {
        if (!buf.perLevel || profileKey().empty() || !globals.have) {
            return;
        }
        auto v = matjson::Value::object();
        for (auto* key : PROFILE_KEYS) {
            v[key] = Mod::get()->getSettingValue<bool>(key);
        }
        Mod::get()->setSavedValue(profileKey(), v);
    }

    void restoreGlobals() {
        if (!globals.have) {
            return;
        }
        auto* mod = Mod::get();
        for (size_t i = 0; i < std::size(PROFILE_KEYS); ++i) {
            mod->setSettingValue<bool>(PROFILE_KEYS[i], globals.values[i]);
        }
        globals = {};
        mod->setSavedValue("profile-globals", matjson::Value(nullptr));
        readSettings();
        applyStage();
    }

    void loadProfile() {
        if (!buf.perLevel || profileKey().empty()) {
            return;
        }
        auto* mod = Mod::get();
        globals.have = true;
        globals.values.clear();
        auto backup = matjson::Value::object();
        for (auto* key : PROFILE_KEYS) {
            bool value = mod->getSettingValue<bool>(key);
            globals.values.push_back(value);
            backup[key] = value;
        }
        // kept until the level is left, so a crash mid-level can't leave level settings behind
        mod->setSavedValue("profile-globals", backup);
        if (!mod->hasSavedValue(profileKey())) {
            return;
        }
        auto v = mod->getSavedValue<matjson::Value>(profileKey());
        if (!v.isObject()) {
            return;
        }
        for (auto* key : PROFILE_KEYS) {
            if (v.contains(key)) {
                mod->setSettingValue<bool>(key, v[key].asBool().unwrapOr(mod->getSettingValue<bool>(key)));
            }
        }
        readSettings();
        applyStage();
        say("level settings loaded: {}", stageText());
    }

    // Settings a crash left behind are put back when the game starts.
    void recoverGlobals() {
        auto* mod = Mod::get();
        if (!mod->hasSavedValue("profile-globals")) {
            return;
        }
        auto v = mod->getSavedValue<matjson::Value>("profile-globals");
        if (!v.isObject()) {
            return;
        }
        for (auto* key : PROFILE_KEYS) {
            if (v.contains(key)) {
                mod->setSettingValue<bool>(key, v[key].asBool().unwrapOr(mod->getSettingValue<bool>(key)));
            }
        }
        mod->setSavedValue("profile-globals", matjson::Value(nullptr));
    }

    // v2.51 death heatmap: red marks on the progress bar where level memory saw you die,
    // brighter where you die more.
    void refreshHeatmap() {
        auto* layer = GameManager::get()->m_playLayer;
        if (!buf.heatNode || !layer || !layer->m_progressBar) {
            return;
        }
        buf.heatNode->clear();
        float length = layer->m_levelLength;
        if (!buf.heatmap || length <= 0.f) {
            return;
        }
        auto size = layer->m_progressBar->getContentSize();
        uint32_t most = 0;
        for (auto const& [slice, spot] : memory) {
            most = std::max(most, spot.deaths);
        }
        if (most == 0) {
            return;
        }
        for (auto const& [slice, spot] : memory) {
            if (spot.deaths == 0) {
                continue;
            }
            float pct = std::clamp((slice + 0.5f) * SLICE / length, 0.f, 1.f);
            float x = 2.f + pct * (size.width - 4.f);
            float heat = static_cast<float>(spot.deaths) / most;
            buf.heatNode->drawSegment({ x, 2.f }, { x, size.height - 2.f }, 0.7f, { 1.f, 0.35f + 0.3f * (1.f - heat), 0.55f, 0.35f + 0.6f * heat });
        }
    }

    // v2.51 stats popup text
    std::string statsText() {
        auto const& s = sessionStats;
        std::string text;
        if (s.saves == 0) {
            text += "No early clicks yet this session.\n";
        }
        else {
            text += fmt::format("<cp>{}</c> {} in <cp>{}</c> attempts, on average <cp>{:.1f}t</c> ({:.2f}f) early.\n",
                s.saves, buf.train ? "early clicks" : "saves", s.attempts, static_cast<double>(s.earlySum) / s.saves, s.earlySum * TICK / s.saves);
            std::vector<std::pair<int, int>> spots(s.savesAt.begin(), s.savesAt.end());
            std::sort(spots.begin(), spots.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
            text += "Most early clicks at:";
            for (size_t i = 0; i < std::min<size_t>(3, spots.size()); ++i) {
                text += fmt::format(" <cy>{}%</c> ({}x)", spots[i].first, spots[i].second);
            }
            text += "\n";
        }
        auto* layer = GameManager::get()->m_playLayer;
        if (layer && layer->m_levelLength > 0.f) {
            std::map<int, uint32_t> deathsAt;
            for (auto const& [slice, spot] : memory) {
                if (spot.deaths > 0) {
                    deathsAt[std::clamp(static_cast<int>((slice + 0.5f) * SLICE / layer->m_levelLength * 100.f), 0, 100)] += spot.deaths;
                }
            }
            std::vector<std::pair<int, uint32_t>> spots(deathsAt.begin(), deathsAt.end());
            std::sort(spots.begin(), spots.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
            if (!spots.empty()) {
                text += "Most deaths at:";
                for (size_t i = 0; i < std::min<size_t>(3, spots.size()); ++i) {
                    text += fmt::format(" <cr>{}%</c> ({}x)", spots[i].first, spots[i].second);
                }
                text += "\n";
            }
        }
        if (!lastBench.empty()) {
            text += "Run check: " + lastBench + "\n";
        }
        return text;
    }

    bool intercept(GJBaseGameLayer* self, bool down, bool isPlayer1) {
        ClickTimer timer;
        clickBudget();
        auto* layer = GameManager::get()->m_playLayer;
        if (!layer) {
            return false;
        }

        auto* level = layer->m_level;
        bool twoPlayer = level && level->m_twoPlayerMode;
        // v2.55: a P2-side input in a two-player level only steers Player 2 while dual mode is
        // on; before that it moves Player 1 (Relief's wave parts), so it's buffered like a P1 input.
        if (twoPlayer && !isPlayer1 && layer->m_gameState.m_isDualMode && !g_p2.active) {
            // Player 2 has its own buffer: own queue, rhythm and delay (its own mode / stage).
            if (!p2OwnBuffer(layer)) {
                return false;
            }
            P2Context context(layer);
            return intercept(self, down, false);
        }
        // In two-player levels P2 is someone else: never simulate P1's input on it.
        bool dual = !twoPlayer && layer->m_gameState.m_isDualMode && layer->m_player2;

        buf.isPlayer1 = isPlayer1;
        // v2.79 auto-click: the game already clicked for you, so the click you were about
        // to make (press and its release) is ignored
        if (buf.swallowRelease && !down) {
            buf.swallowRelease = false;
            return true;
        }
        if (buf.autoClicked && down && isPlayer1) {
            buf.autoClicked = false;
            buf.swallowRelease = true;
            return true;
        }
        bool previousRealInput = buf.realInput;
        bool changedInput = previousRealInput != down;
        buf.realInput = down;
        if (buf.autoReleased) {
            // the game already let go for you: your real release has nothing left to do
            buf.autoReleased = false;
            buf.autoDash = false;
            if (down == buf.appliedInput) {
                return true;   // the game is already in that state (the assist got there first)
            }
        }
        if (!buf.enabled) {
            refreshAutoTag();
            buf.appliedInput = down;
            return false;
        }
        // v2.4: the ghost never presses left/right, so platformer predictions are meaningless.
        if (layer->m_isPlatformer) {
            refreshAutoTag();
            buf.appliedInput = down;
            return false;
        }
        // Frame spam is driving the wave: your clicks only tell it you are still spamming.
        if (frameSpam.owned && buf.isPlayer1 && !g_p2.active) {
            frameSpam.lastRealEdge = tickCount;
            return true;
        }

        auto* p1 = g_p2.active ? layer->m_player2 : self->m_player1;   // the player this input steers
        auto* p2 = dual ? layer->m_player2 : nullptr;

        // v2.18 Click Between Frames: a click that arrives in the middle of P1's physics step
        // landed `midTick` of the way through the tick (measured from how far P1 has moved
        // since the tick started). The ghost then starts with the rest of that tick, and
        // delayed inputs are queued one tick sooner, because the game's next tick boundary
        // is already the ghost's tick 1.
        float midTick = 0.f;
        if (buf.cbfSync && buf.inRealUpdate && p1 && std::abs(buf.tickDx) > 0.01f) {
            midTick = std::clamp((p1->getPosition().x - buf.tickStartX) / buf.tickDx, 0.f, 0.999f);
        }
        if (!g_p2.active) {
            buf.cbfHistory = (buf.cbfHistory << 1) | (midTick > 0.f ? 1u : 0u);
            buf.cbfSeen = std::min(buf.cbfSeen + 1, 16);
            if (buf.explain) {
                say("click: {} (inside the physics step: {}, step length {:.2f})", midTick > 0.f ? fmt::format("mid-tick @{:.2f}", midTick) : std::string("on the tick"),
                    buf.inRealUpdate, buf.tickDx);
            }
        }
        buf.firstStep = 1.f - midTick;
        struct FirstStepReset {
            ~FirstStepReset() {
                buf.firstStep = 1.f;
            }
        } firstStepReset;
        int early = midTick > 0.f ? 1 : 0;

        bool anyShip = p1 && (p1->m_isShip || (p2 && p2->m_isShip));
        bool customShipBuffer = studio.perMode && studio.modeTicks[SM_SHIP] > 0;
        if (!p1 || p1->m_isDead || (anyShip && !buf.shipBuffer && !customShipBuffer)) {
            refreshAutoTag();
            resetPending();
            buf.appliedInput = down;
            return false;
        }

        if (down && buf.queue.empty()) {
            buf.releaseShift = 0;
        }

        bool p1Wave = p1->m_isDart;
        bool anyWave = p1Wave || (p2 && p2->m_isDart);
        if (anyWave && buf.waveDirectSpam) {
            resetAlternateRhythm();
            refreshAutoTag();
            resetPending();
            buf.appliedInput = down;
            return false;
        }
        bool mini = (p1Wave && p1->m_vehicleSize < 1.f) || (p2 && p2->m_isDart && p2->m_vehicleSize < 1.f);

        // Wave spam: don't fight fast inputs. Counted for every input, including ones made
        // while a delayed input is waiting (v2.5.1 skipped those, so a spam that started
        // with a buffered click never reached the streak and its taps got collapsed).
        // Once the streak is reached, anything still waiting is dropped and your inputs
        // go straight to the game until you pause for longer than the spam gap.
        // v2.44: ship is steered by feathering, so it gets its own, much shorter spam gap;
        // otherwise nearly every ship input counted as spam and was never buffered.
        int spamGap = st(anyShip && !anyWave ? std::clamp(buf.shipSpamGap, 2, 40) : std::clamp(buf.spamGap, 4, 40));
        int observedGap = buf.sinceInput;
        if (observedGap < spamGap) {
            ++buf.rapid;
        }
        else {
            buf.rapid = 0;
        }
        buf.sinceInput = 0;
        bool anyUfo = p1->m_isBird || (p2 && p2->m_isBird);
        bool anyBall = p1->m_isBall || (p2 && p2->m_isBall);
        bool anySwing = p1->m_isSwing || (p2 && p2->m_isSwing);
        // Alternate Assist observes only real button edges. It forecasts those intervals on
        // the ghost; no background timer is allowed to synthesize a press or release.
        bool altAssist = studioAltActive(p1, p2);
        int alternateGap = anyWave ? st(studio.altBurstGap) : spamGap;
        if (altAssist && changedInput) {
            if (observedGap < alternateGap) {
                // The interval belongs to the state we just left. Each direction keeps its
                // own rhythm estimate. Wave Alt has a longer, separate burst window so one
                // slower phase does not erase the learned cadence at USKC's tight section.
                buf.altRapid = std::min(buf.altRapid + 1, 1000);
                updateAlternatePhase(!down, observedGap);
            }
            else {
                // A real pause starts a new spam rhythm; don't steer with stale timing.
                resetAlternateRhythm();
            }
        }
        else if (!altAssist && changedInput) {
            resetAlternateRhythm();
        }
        int alternateRapid = anyWave ? buf.altRapid : buf.rapid;
        int alternateEvery = -1;
        int alternatePredictionEvery = -1;
        int alternateDownEvery = -1;
        int alternateUpEvery = -1;
        int alternateActivation = anyWave ? 1 : buf.spamStreak;
        if (altAssist && changedInput && alternateRapid >= alternateActivation && observedGap < alternateGap) {
            int fallback = std::clamp(observedGap, st(2), st(40));
            int learnedDown = buf.altDownSampleCount ? buf.altDownEvery : fallback;
            int learnedUp = buf.altUpSampleCount ? buf.altUpEvery : fallback;
            alternateEvery = std::clamp((learnedDown + learnedUp + 1) / 2, st(2), st(40));
            alternateDownEvery = learnedDown;
            alternateUpEvery = learnedUp;
            alternatePredictionEvery = alternateEvery;
        }
        auto downForecast = alternatePredictionEvery > 0 ? alternatePhaseForecast(true) : AlternatePhaseForecast{};
        auto upForecast = alternatePredictionEvery > 0 ? alternatePhaseForecast(false) : AlternatePhaseForecast{};
        AlternatePredictionScope alternatePredictionScope(alternatePredictionEvery, alternateDownEvery, alternateUpEvery,
            downForecast, upForecast);
        bool fullAlternateWave = altAssist && anyWave && changedInput && observedGap < alternateGap &&
            alternateRapid >= 1 && !layer->m_isPlatformer && !buf.train && !buf.waveV46 &&
            !buf.autoReleased && !buf.autoDash && !buf.autoClicked;
        // Frame spam: you are spamming the wave through a tight gap, so the assist takes over the spam.
        if (studio.spamOn && !frameSpam.owned && changedInput && observedGap < spamGap && buf.rapid >= 3 && buf.isPlayer1 &&
            !g_p2.active && !twoPlayer && !dual && p1->m_isDart && !layer->m_isPlatformer && !buf.autoReleased &&
            !buf.autoClicked && (layer->m_isPracticeMode || buf.autoNormalRuns)) {
            int holdLife = 0;
            int idleLife = 0;
            if (probeGap(layer, p1, nullptr, false, holdLife, idleLife)) {
                // The rate is the CPS setting (a click is a press and a release, so 16 CPS = 32 edges a second). The
                // gap only LIMITS it: each phase may use at most about half of the free travel, so a very small gap
                // (fast speed, mini wave) gets faster spam and a normal 1-block gap keeps the rate you chose.
                double wantedEdge = std::max(1, st(240)) / (2.0 * std::max(4, studio.spamCps));
                frameSpam.periodExact = std::min(wantedEdge, std::max(1.0, (holdLife + idleLife) * 0.55));
                frameSpam.period = std::max(1, static_cast<int>(std::lround(frameSpam.periodExact)));
                frameSpam.owned = true;
                frameSpam.holding = down;
                frameSpam.checked = false;
                frameSpam.lastRealEdge = tickCount;
                frameSpam.edgeTime = static_cast<double>(tickCount) + frameSpam.periodExact;
                frameSpam.nextEdge = static_cast<int>(std::lround(frameSpam.edgeTime));
                say("frame spam on: gap {}t of travel, {} CPS wanted, one edge every {:.1f}t", holdLife + idleLife, studio.spamCps, frameSpam.periodExact);
                buf.autoTagLast = tickCount;
                refreshAutoTag();
                buf.appliedInput = down;
                return false;   // this edge goes through; the assist drives from the next tick
            }
        }
        if (buf.explain && alternateEvery > 0) {
            if (alternatePredictionEvery > 0) {
                say("alternate ghost: hold {}t / release {}t ({} / {} phase samples)", alternateDownEvery, alternateUpEvery,
                    downForecast.count, upForecast.count);
            }
        }
        if (!altAssist &&
            (anyWave || anyShip || (anyUfo && buf.ufoSmart) || (anyBall && buf.ballSmart) || (anySwing && buf.swingSmart)) &&
            buf.rapid >= buf.spamStreak) {
            if (buf.explain) {
                say("pass {}: spam ({} fast inputs in a row, gap < {}t)", down ? "press" : "release", buf.rapid, spamGap);
            }
            // v2.47: path view marks inputs that skipped prediction (gray dot on you), so a
            // missing path isn't mistaken for a broken mod
            if (buf.showPath && buf.pathNode) {
                buf.pathNode->clear();
                buf.pathNode->drawDot(p1->getPosition(), 4.f, { 0.6f, 0.6f, 0.65f, 0.9f });
            }
            // v2.66: inputs still waiting aren't dropped any more (a dropped press was a turn
            // that never happened) and aren't all sent at once (v2.49, a zero-length tap):
            // they fire one per tick, in order, starting next tick, and this input right after.
            if (!buf.queue.empty()) {
                for (size_t i = 0; i < buf.queue.size(); ++i) {
                    buf.queue[i].ticksLeft = static_cast<int>(i);
                }
                if (buf.queue.back().down != down) {
                    queueInput(static_cast<int>(buf.queue.size()), down);
                }
                buf.shift = 0;
                return true;
            }
            buf.appliedInput = down;
            return false;
        }

        // v4.6 wave mode only: while a delayed input waits, a quick opposite input fires
        // one tick after it; pressing the same way again cancels that. (The default wave
        // mode shifts it like the cube does, below, so your zigzag keeps its shape.)
        if (!buf.queue.empty() && anyWave && buf.waveV46) {
            auto first = buf.queue.front();
            buf.queue.erase(buf.queue.begin() + 1, buf.queue.end());
            if (down != first.down) {
                queueInput(first.ticksLeft + 1, down);
            }
            return true;
        }

        // A delay is in progress: shift this input by the same amount so your
        // rhythm (tap length, hold length, follow-up clicks) is preserved.
        if (!buf.queue.empty()) {
            int after = buf.queue.back().ticksLeft;
            // low latency: a new press isn't shifted, it goes right after what's already queued
            queueInput(buf.lowLatency && down ? after : std::max(buf.shift - early, after), down);
            return true;
        }

        // Releases only matter for wave and robot.
        if (!p1Wave) {
            bool robot = p1->m_isRobot;
            bool wave2 = false;
            if (p2 && !robot) {
                wave2 = p2->m_isDart;
                if (!wave2) {
                    robot = p2->m_isRobot;
                }
            }
            // v2.16: letting go ends a dash, so dash releases are predicted like robot ones.
            bool dashing = p1->m_isDashing || (p2 && p2->m_isDashing);
            if (!wave2 && !down && !robot && !dashing && !anyShip) {
                if (buf.releaseShift > 0) {
                    // the press was delayed, so its release is delayed by the same amount:
                    // the hold length the ghost was tested with is the one the game gets
                    buf.shift = buf.releaseShift;
                    buf.releaseShift = 0;
                    queueInput(buf.shift, false);
                    return true;
                }
                buf.appliedInput = false;
                return false;
            }
        }

        bool anyFlyer = isFlying(p1) || (p2 && isFlying(p2));  // wave / ufo / swing
        int horizon = st(anyFlyer ? (anySwing ? studio.swingLook : 48) : std::clamp(buf.groundHorizon, 48, 240));
        if (anyWave) {
            if (fullAlternateWave) {
                // Exactly the Wave look setting (12 ticks by default), like the c5least version.
                horizon = st(studio.altWaveLook);
            }
            else {
                horizon = fullWaveHorizon(mini);
            }
        }
        // Gap centering (straight fly). In a ~1 block gap the ghost measures where you are: how long
        // until the ceiling if you hold, how long until the floor if you let go. Their difference is
        // twice the distance to the middle, so each edge can be delayed just enough to bring you back.
        bool straightFly = false;
        int gapHold = -1;
        int gapIdle = -1;
        if (fullAlternateWave && studio.sfOn && !clickOverTime()) {
            int holdLife = 0;
            int idleLife = 0;
            straightFly = probeGap(layer, p1, p2, dual, holdLife, idleLife);
            if (straightFly) {
                gapHold = holdLife;
                gapIdle = idleLife;
            }
        }
        if (studio.perfGuardOn) {
            float guard = perfGuardScale();
            if (guard < 1.f) {
                horizon = std::max(st(8), static_cast<int>(static_cast<float>(horizon) * guard));
            }
        }
        int maxDelay = st(combinedTicks(p1, p2));   // each player's own mode / stage, combined for dual
        if (altAssist && anyWave && changedInput && observedGap < alternateGap && alternateRapid < 1) {
            // Let one complete manual interval establish a first timing estimate. Then the
            // per-edge search can start immediately with a conservative fallback for the
            // phase that has not been sampled yet.
            maxDelay = 0;
        }
        else if (fullAlternateWave) {
            // A correction must finish before the next predicted manual edge. This allows
            // short, independent press/release adjustments without building a delayed queue.
            int phaseRoom = std::max(1, std::min(alternateDownEvery, alternateUpEvery) - 1);
            maxDelay = std::min(st(studio.altMaxTicks), phaseRoom);
        }
        else if (altAssist && alternateRapid >= buf.spamStreak) {
            // Never hold an alternating edge until its opposite edge is due. Keeping the
            // correction shorter than the faster half-cycle prevents a delayed release from
            // stacking behind the next press and making the spam feel lopsided.
            int phaseRoom = std::max(1, std::min(buf.altDownEvery, buf.altUpEvery) - 1);
            maxDelay = std::min({ maxDelay, st(studio.altMaxTicks), phaseRoom });
        }
        if (buf.lowLatency) {
            maxDelay = std::min(maxDelay, st(4));  // at most 1 frame (60 fps) of delay
        }
        if (studio.adaptiveOn && maxDelay > 0 && !fullAlternateWave && !buf.lowLatency) {
            maxDelay += st(adaptiveBonusAt(layer->getCurrentPercent()));
        }
        if (maxDelay <= 0) {
            if (buf.explain) {
                say("pass {}: max delay is 0 for this mode", down ? "press" : "release");
            }
            if (fullAlternateWave) {
                buf.altWaveDelay = 0;
            }
            buf.appliedInput = down;
            return false;
        }

        // Wave releases are predicted separately, so hold there. Robot jump height depends
        // on the hold, so use your usual robot hold (once known); everything else (cube,
        // ball, ufo, spider, swing) re-triggers while held, so model a tap.
        bool anyRobot = p1->m_isRobot || (p2 && p2->m_isRobot);
        int releaseAfter = -1;
        if (down && !anyWave && !anyShip) {
            releaseAfter = anyRobot ? robotHold.pick(-1, 50) : holdEstimate();
        }
        if (alternateEvery > 0 && down && !anyWave && !anyShip && !anyRobot && (anyUfo || anyBall || anySwing)) {
            releaseAfter = alternateEvery;
        }

        // On the ground a press is a jump; only move it onto an orb if the setting says so.
        bool grounded = p1->m_isOnGround && (!p2 || p2->m_isOnGround);
        if (buf.explain && down && grounded && !buf.orbAlignGround && !anyWave && !anyShip && buf.orbAlign && buf.smart) {
            // v2.42: is there an orb right ahead that this ground jump could have been moved onto?
            SimEnd probeEnd;
            buf.skipPress = true;
            buf.orbClick = false;
            int window = st(std::clamp(buf.orbWindow, 1, 64));
            simulate(layer, p1, p2, dual, 0, true, window + 2, -1, window + 2, &probeEnd);
            buf.skipPress = false;
            buf.orbClick = buf.assumeOrbs;
            if (buf.firstOrbTick >= 0 && buf.firstOrbTick <= window) {
                say("orb: you'd touch an orb at t{}, but this press is from the ground ('also from the ground' is off)",
                    buf.firstOrbTick);
            }
        }
        if (down && !anyWave && !anyShip && buf.orbAlign && buf.smart && (!grounded || buf.orbAlignGround)) {
            int orbLanded = 0;
            int k = memorySaysSkip(sliceOf(p1)) ? 0 : alignOrb(layer, p1, p2, dual, horizon, releaseAfter, memoryDelay(sliceOf(p1)), maxDelay, &orbLanded);
            if (k > 0 && trainInstead(layer, k, down, "orb")) {
                decided(layer, p1, p2, dual, 0, down, horizon, releaseAfter);
                buf.appliedInput = down;
                return false;
            }
            if (k > 0) {
                say("ORB {} +{}t ({:g}f) -> {}t", buf.orbWasTiming ? "timing" : "press", k, k * TICK, orbLanded);
                noteSave(layer, k);
                decided(layer, p1, p2, dual, k, down, horizon, releaseAfter);
                buf.shift = k;
                queueInput(k - early, down);
                return true;
            }
        }

        buf.sawMover = false;
        buf.trackMovers = buf.avoidMovers || buf.predictMovers;
        SimEnd end;
        bool cubeStyleWave = anyWave && !buf.waveV46;
        bool robotPress = down && anyRobot && !anyWave && buf.robotSmart;
        bool anySpider = p1->m_isSpider || (p2 && p2->m_isSpider);
        bool spiderPress = down && anySpider && !anyWave && !robotPress && buf.spiderSmart;
        bool ufoPress = down && anyUfo && !anyWave && !anyShip && buf.ufoSmart;  // v2.40
        // v2.51 swing: one flip, then nothing for the whole look-ahead, crashes into what your
        // next flip would have avoided. Scored like the UFO: could one more flip have saved it?
        bool swingPress = down && anySwing && !anyWave && !anyShip && !ufoPress && buf.swingSmart;
        ufoPress = ufoPress || swingPress;
        bool shipInput = anyShip && !anyWave;  // v2.25: press or release, both steer the ship
        buf.captureKiller = true;
        buf.baseTouchedDown = false;
        buf.killerSeen = false;
        buf.baseEscape = -1;
        int base = cubeStyleWave ? waveScore(layer, p1, p2, dual, 0, down, horizon, horizon, &end)
                 : robotPress    ? robotScore(layer, p1, p2, dual, 0, horizon, horizon, &end)
                 : spiderPress   ? spiderScore(layer, p1, p2, dual, 0, horizon, releaseAfter, horizon, &end)
                 : ufoPress      ? ufoScore(layer, p1, p2, dual, 0, horizon, releaseAfter, horizon, &end)
                 : shipInput     ? shipScore(layer, p1, p2, dual, 0, down, horizon, horizon, &end)
                                 : simulateOrbs(layer, p1, p2, dual, 0, down, horizon, releaseAfter, horizon, &end);
        buf.captureKiller = false;
        buf.trackMovers = false;

        // Alternate timing is especially sensitive to inputs that cross a portal boundary.
        // If this ghost cannot model the portal it just reached, let the real input pass
        // untouched instead of applying a delay based on an incomplete path.
        if (altAssist && buf.ghostHitPortal) {
            if (buf.explain) {
                say("pass {}: alternate assist leaves the input untouched at an unsimulated portal", down ? "press" : "release");
            }
            if (fullAlternateWave) {
                noteAlternateResult(AlternateResult::PortalPass);
            }
            buf.altWaveDelay = 0;
            buf.appliedInput = down;
            return false;
        }

        int alternateWaveTarget = fullAlternateWave
            ? std::min(horizon, base + st(studio.altWaveMinGain))
            : horizon;
        // Alt-wave corrections need a little room for click timing jitter. Reuse this check
        // for both newly searched offsets and the phase offset carried across later edges.
        int waveMargin = studio.altMargin;
        float unitX = (311.58f / 240.f) * (std::abs(p1->m_playerSpeed) / 0.9f);   // distance of one tick
        float biasShift = static_cast<float>(studio.clickBias) * unitX;            // where your clicks really land
        auto alternateWaveHasMargin = [&](int delay) {
            if (waveMargin == 0 && studio.clickBias == 0) {
                return true; // the nominal route was already checked by the caller
            }
            float shift = 0.5f * waveMargin * unitX;
            float offsets[2] = { biasShift + shift, biasShift - shift };
            int offsetCount = waveMargin == 0 ? 1 : 2;
            for (int i = 0; i < offsetCount; ++i) {
                if (clickOverTime()) {
                    buf.ghostShiftX = 0.f;
                    return false;
                }
                buf.ghostShiftX = offsets[i];
                SimEnd marginEnd;
                int marginRoute = cubeStyleWave
                    ? waveScore(layer, p1, p2, dual, delay, down, horizon, horizon, &marginEnd)
                    : simulateOrbs(layer, p1, p2, dual, delay, down, horizon, releaseAfter, horizon, &marginEnd);
                buf.ghostShiftX = 0.f;
                if (marginRoute < alternateWaveTarget || marginEnd == SimEnd::StopSearch) {
                    return false;
                }
            }
            return true;
        };

        // v2.30 diagnostics: a ghost that dies within a few ticks while you live means the
        // prediction doesn't match you there. Log what killed it and how both hitboxes compare.
        if (buf.killerSeen && (base <= st(3) || (buf.deathLog && base < horizon))) {
            auto real = p1->getObjectRect();
            auto const& g = buf.ghostDeathRect;
            if (buf.killerType >= 0) {
                auto const& k = buf.killerRect;
                say("ghost killed at t{} by obj {} (type {}{}{}) at dx {:.1f} dy {:.1f} size {:.1f}x{:.1f}", buf.killerTick, buf.killerId,
                    buf.killerType, buf.killerNoTouch ? ", no-touch" : "", buf.killerDisabled ? ", disabled" : "",
                    k.getMidX() - real.getMidX(), k.getMidY() - real.getMidY(), k.size.width, k.size.height);
            }
            else {
                say("ghost killed at t{} by no object (crushed / out of bounds / slope)", buf.killerTick);
            }
            say("  hitbox ghost {:.1f}x{:.1f} vs you {:.1f}x{:.1f}, ghost moved dx {:.1f} dy {:.1f}{}", g.size.width, g.size.height,
                real.size.width, real.size.height, g.getMidX() - real.getMidX(), g.getMidY() - real.getMidY(),
                buf.predictMovers ? " (moving objects predicted)" : "");
            // v2.54: which players exist and which one your clicks move
            auto desc = [](PlayerObject* p) {
                if (!p) {
                    return std::string("none");
                }
                char const* mode = p->m_isDart ? "wave" : p->m_isShip ? "ship" : p->m_isBird ? "ufo" : p->m_isBall ? "ball"
                    : p->m_isRobot ? "robot" : p->m_isSpider ? "spider" : p->m_isSwing ? "swing" : "cube";
                return fmt::format("{}{}{}", mode, p->m_controlsDisabled ? " controls-off" : "", p->isVisible() ? "" : " hidden");
            };
            say("  players: P1 {} | P2 {} | dual {} | predicted {}", desc(layer->m_player1), desc(layer->m_player2),
                layer->m_gameState.m_isDualMode, p1 == layer->m_player1 ? "P1" : "P2");
        }

        // v2.36: with CBF, where inside the tick the click landed (0 = on the tick)
        std::string tickTag = midTick > 0.f ? fmt::format(" @{:.2f}", midTick) : std::string();
        // Even zigzag. Each edge that is delayed by a different amount stretches one phase and squeezes the next
        // ("ziiig zag", "zig zaaag"). So an edge should come as long after the previous one as the learned phase
        // is: if it comes too early it waits for the difference (never more than the correction limit). Your own
        // slower edges simply pass.
        int smoothWant = -1;
        if (fullAlternateWave && studio.altSmooth && maxDelay > 0 && buf.lastAppliedEdgeTick > -1000000) {
            int endingPhase = down ? alternateUpEvery : alternateDownEvery;   // the phase this edge ends (release before a press, hold before a release)
            int sinceApplied = tickCount - buf.lastAppliedEdgeTick;
            if (endingPhase > 0 && sinceApplied < endingPhase * 4) {
                smoothWant = studio.altSmoothPct <= 0 ? -1
                    : std::clamp(static_cast<int>(std::lround((endingPhase - sinceApplied) * studio.altSmoothPct / 100.0)), 0, maxDelay);
            }
        }
        if (base >= horizon) {
            // Gap centering. A steady spam zigzag turns at the top and the bottom of its own swing, so the turns
            // should land symmetrically around the middle of the gap: half of the phase that follows an edge above
            // the middle for a release (top turn), half of it below the middle for a press (bottom turn). (The first
            // version aimed the turn at the exact middle, which only reacted once you were already near a wall.)
            // If this edge would turn too early, it waits just long enough, as long as the ghost says that is safe.
            if (straightFly && gapHold >= 0 && gapIdle >= 0 && maxDelay > 0) {
                int height = (gapIdle - gapHold) / 2;                      // ticks above the middle of the gap
                int follow = down ? alternateDownEvery : alternateUpEvery; // how long the phase after this edge lasts
                if (follow <= 0) follow = st(8);
                int ideal = follow / 2;
                int extend = down ? height + ideal : ideal - height;      // press: keep going down while above the low turn; release: keep going up while below the high turn
                extend = static_cast<int>(std::lround(static_cast<double>(extend) * studio.sfStrength / 100.0));
                if (extend >= std::max(1, st(studio.sfDead))) {
                    // the biggest wait up to the wanted one that the ghost still calls safe (and robust)
                    for (int centerDelay = std::min(extend, maxDelay); centerDelay >= 1; --centerDelay) {
                        if (clickOverTime()) break;
                        SimEnd centerEnd;
                        int centerRoute = cubeStyleWave
                            ? waveScore(layer, p1, p2, dual, centerDelay, down, horizon, horizon, &centerEnd)
                            : simulateOrbs(layer, p1, p2, dual, centerDelay, down, horizon, releaseAfter, horizon, &centerEnd);
                        if (centerRoute >= horizon && centerEnd != SimEnd::StopSearch && alternateWaveHasMargin(centerDelay)) {
                            say("gap centering: {} +{}t (wanted +{}t, {}t {} the middle)", down ? "press" : "release", centerDelay, extend,
                                std::abs(height), height >= 0 ? "above" : "below");
                            noteAlternateResult(AlternateResult::Adjusted, centerDelay);
                            buf.altWaveDelay = 0;
                            decided(layer, p1, p2, dual, centerDelay, down, horizon, releaseAfter);
                            buf.shift = centerDelay;
                            queueInput(centerDelay - early, down);
                            return true;
                        }
                    }
                }
            }
            // Even zigzag: this edge waits so the phase it ends has the learned length (the biggest wait that is safe).
            if (smoothWant >= 1) {
                for (int smoothDelay = smoothWant; smoothDelay >= 1; --smoothDelay) {
                    if (clickOverTime()) break;
                    SimEnd smoothEnd;
                    int smoothRoute = cubeStyleWave
                        ? waveScore(layer, p1, p2, dual, smoothDelay, down, horizon, horizon, &smoothEnd)
                        : simulateOrbs(layer, p1, p2, dual, smoothDelay, down, horizon, releaseAfter, horizon, &smoothEnd);
                    if (smoothRoute >= horizon && smoothEnd != SimEnd::StopSearch && alternateWaveHasMargin(smoothDelay)) {
                        say("alternate wave: even zigzag, {} +{}t ({}t since the last edge, a {}t phase)", down ? "press" : "release",
                            smoothDelay, tickCount - buf.lastAppliedEdgeTick, down ? alternateUpEvery : alternateDownEvery);
                        noteAlternateResult(AlternateResult::Adjusted, smoothDelay);
                        buf.altWaveDelay = 0;
                        decided(layer, p1, p2, dual, smoothDelay, down, horizon, releaseAfter);
                        buf.shift = smoothDelay;
                        queueInput(smoothDelay - early, down);
                        return true;
                    }
                }
            }
            // Keep a previously safe offset across alternating edges. Applying the same
            // delay to each manual edge shifts the whole wave path without changing its
            // press/release spacing, preventing corrections from walking above/below it.
            // (Not while the even-zigzag rule is shaping the spacing: a carried offset would undo it.)
            if (fullAlternateWave && smoothWant < 0 && buf.altWaveDelay > 0 && buf.altWaveDelay <= maxDelay) {
                SimEnd steadyEnd;
                int steadyRoute = cubeStyleWave
                    ? waveScore(layer, p1, p2, dual, buf.altWaveDelay, down, horizon, horizon, &steadyEnd)
                    : simulateOrbs(layer, p1, p2, dual, buf.altWaveDelay, down, horizon, releaseAfter, horizon, &steadyEnd);
                if (steadyRoute >= horizon && steadyEnd != SimEnd::StopSearch && alternateWaveHasMargin(buf.altWaveDelay)) {
                    int steadyDelay = buf.altWaveDelay;
                    say("alternate wave: kept the {}t phase offset", steadyDelay);
                    decided(layer, p1, p2, dual, steadyDelay, down, horizon, releaseAfter);
                    buf.shift = steadyDelay;
                    queueInput(steadyDelay - early, down);
                    return true;
                }
            }
            buf.altWaveDelay = 0;
            // v2.43 more time at death spots. This input is only safe if your next input comes
            // in time, and level memory has seen you die here repeatedly: try small delays
            // that push the crash further away (or remove it), so you get more time to react.
            // Only at those spots, so normal play isn't delayed.
            bool reactMode = cubeStyleWave || shipInput || ufoPress || spiderPress;
            if (buf.moreTime && buf.learn && reactMode && buf.baseEscape >= 0 && spotAround(sliceOf(p1)).deaths >= 2) {
                int crash0 = buf.baseCrash;
                int bestK = 0;
                int bestCrash = crash0;
                int hold = (cubeStyleWave || shipInput) ? -1 : releaseAfter;
                // v2.67: wave / ship input that's already safe (if you keep reacting) is held back
                // at most 1 frame here. Up to 4f (16t) made fast zigzags at death spots feel late:
                // every later click in the rhythm gets shifted by the same amount.
                int moreMax = (cubeStyleWave || shipInput) ? std::min(maxDelay, st(4)) : maxDelay;
                for (int k = 1; k <= moreMax; ++k) {
                    SimEnd e;
                    if (clickOverTime()) {
                        break;
                    }
                    int ret = simulateOrbs(layer, p1, p2, dual, k, down, horizon, hold, horizon, &e);
                    if (e == SimEnd::StopSearch) {
                        break;
                    }
                    if (ret >= horizon) {
                        bestK = k;
                        bestCrash = -1;  // no crash at all
                        break;
                    }
                    int crashAt = k + ret;  // ticks from now
                    if (crashAt >= crash0 + st(8) && crashAt > bestCrash) {
                        bestK = k;
                        bestCrash = crashAt;
                    }
                }
                if (bestK > 0 && !buf.train) {
                    noteSave(layer, bestK);
                    say("MORE TIME {}{} +{}t: crash t{} -> {} (died here {}x)", down ? "press" : "release", tickTag, bestK, crash0,
                        bestCrash < 0 ? std::string("none") : fmt::format("t{}", bestCrash), spotAround(sliceOf(p1)).deaths);
                    decided(layer, p1, p2, dual, bestK, down, horizon, releaseAfter);
                    buf.shift = bestK;
                    queueInput(bestK - early, down);
                    return true;
                }
            }
            if (buf.explain) {
                if (buf.baseEscape >= 0) {
                    say("pass {}{}: safe only if you react by t{} (else crash at t{})", down ? "press" : "release", tickTag,
                        buf.baseEscape, buf.baseCrash);
                }
                else {
                    say("pass {}{}: safe now (survives the {}t look-ahead)", down ? "press" : "release", tickTag, horizon);
                }
            }
            decided(layer, p1, p2, dual, 0, down, horizon, releaseAfter);
            buf.appliedInput = down;
            return false;
        }

        // v2.27: a cube / ball press that only crashes after a normal jump would have landed
        // (~90 ticks) dies to the next obstacle, not this click: no delay helps, so don't
        // spend the whole time budget searching (it was most of the per-click lag).
        bool groundPress = down && !anyFlyer && !anyRobot && !anySpider && !anyWave;
        // v2.63: only once the jump came back down. A long jump (Acheron's first: ~94 ticks in
        // the air, then onto a spike) crashes late too, and that crash *is* this click's.
        if (groundPress && buf.lateCrash > 0 && base >= st(buf.lateCrash) && buf.baseTouchedDown) {
            if (buf.explain) {
                say("pass press: crash only at {}t, after the jump would land (late crash rule)", base);
            }
            decided(layer, p1, p2, dual, 0, down, horizon, releaseAfter);
            buf.appliedInput = down;
            return false;
        }

        // v2.13 memory: saves at this spot kept ending in deaths, so the prediction can't be
        // trusted here. Leave the input alone.
        int slice = sliceOf(p1);
        if (memorySaysSkip(slice)) {
            say("memory: saves here failed before, input left alone");
            buf.altWaveDelay = 0;
            decided(layer, p1, p2, dual, 0, down, horizon, releaseAfter);
            pending.skipped = true;
            buf.appliedInput = down;
            return false;
        }

        int landed = 0;
        int saved = 0;
        bool fromMemory = false;
        crashFixed = false;
        // v2.13 memory: the delay that worked here on earlier runs is tried first, so the
        // same gap gets the same save every time.
        // The same judgement this mode's search uses, for a single delay.
        auto scoreAt = [&](int k, int target, SimEnd* e) {
            return cubeStyleWave ? waveScore(layer, p1, p2, dual, k, down, horizon, target, e)
                 : robotPress    ? robotScore(layer, p1, p2, dual, k, horizon, target, e)
                 : spiderPress   ? spiderScore(layer, p1, p2, dual, k, horizon, releaseAfter, target, e)
                 : ufoPress      ? ufoScore(layer, p1, p2, dual, k, horizon, releaseAfter, target, e)
                 : shipInput     ? shipScore(layer, p1, p2, dual, k, down, horizon, target, e)
                                 : simulateOrbs(layer, p1, p2, dual, k, down, horizon, releaseAfter, target, e);
        };

        // v2.69: a cube / ball jump save has to land safely. A save that only lived 12 ticks
        // longer than pressing now still crashed later in the same jump (Acheron's first jump:
        // +32t..+40t "saves" that died ~130 ticks later, every time); those clicks now go
        // through untouched instead of being moved by up to 10 frames for nothing.
        // v2.75: robot jumps and UFO / swing flaps too (no more half saves there either)
        int needed = fullAlternateWave ? alternateWaveTarget
            : (groundPress || robotPress || ufoPress) ? horizon : std::min(horizon, base + st(12));
        int learned = memoryDelay(slice);
        if (learned > 0 && learned <= maxDelay && !(anyWave && buf.waveV46)) {
            int target = needed;
            SimEnd e;
            int ret = scoreAt(learned, target, &e);
            if (ret >= target) {
                saved = learned;
                landed = ret;
                fromMemory = true;
            }
        }
        if (saved <= 0) {
            saved = fullAlternateWave
                ? searchAlternateWave(layer, p1, p2, dual, down, horizon, maxDelay, base, buf.altWaveDelay, &landed, smoothWant)
                : cubeStyleWave ? searchWave(layer, p1, p2, dual, down, horizon, maxDelay, base, &landed,
                        fullAlternateWave, fullAlternateWave ? buf.altWaveDelay : 0)
                  : robotPress    ? searchTwoPass(horizon, maxDelay, base, &landed, [&](int k, int target, SimEnd* e) {
                        return robotScore(layer, p1, p2, dual, k, horizon, target, e);
                    }, true)
                  : spiderPress   ? searchTwoPass(horizon, maxDelay, base, &landed, [&](int k, int target, SimEnd* e) {
                        return spiderScore(layer, p1, p2, dual, k, horizon, releaseAfter, target, e);
                    })
                  : shipInput     ? searchSmallest(horizon, maxDelay, base, &landed, [&](int k, int target, SimEnd* e) {
                        return shipScore(layer, p1, p2, dual, k, down, horizon, target, e);
                    })
                  : ufoPress      ? searchTwoPass(horizon, maxDelay, base, &landed, [&](int k, int target, SimEnd* e) {
                        return ufoScore(layer, p1, p2, dual, k, horizon, releaseAfter, target, e);
                    }, true)
                                  : searchDelay(layer, p1, p2, dual, down, anyWave, horizon, maxDelay, releaseAfter, base, &landed,
                                        groundPress || fullAlternateWave, fullAlternateWave ? buf.altWaveDelay : 0);
        }
        // Two-edge plan. When no single delay for THIS edge saves you, a delay now may still work if the NEXT
        // edge is delayed too (for example the release that follows). The ghost tries every delay for this
        // edge together with every delay for the next one, and keeps the first pair that survives the whole
        // look-ahead and its safety margin. Only this edge is applied; the next one is planned again when it arrives.
        if (saved <= 0 && fullAlternateWave && studio.altPlan && cubeStyleWave && maxDelay >= 1 &&
            buf.altPredictionDownEvery > 0 && buf.altPredictionUpEvery > 0 && clickMs() < buf.budgetMs * 0.6) {
            struct PlanShiftReset {
                ~PlanShiftReset() { buf.altNextEdgeShift = 0; }
            } planShiftReset;
            struct PlanCandidate {
                int delay;
                int score;
            };
            std::vector<PlanCandidate> planCandidates;
            for (int firstDelay = 1; firstDelay <= maxDelay; ++firstDelay) {
                if (clickOverTime()) break;
                SimEnd firstEnd;
                int firstScore = waveScore(layer, p1, p2, dual, firstDelay, down, horizon, horizon, &firstEnd);
                if (firstEnd == SimEnd::StopSearch) break;
                planCandidates.push_back({ firstDelay, firstScore });
            }
            std::stable_sort(planCandidates.begin(), planCandidates.end(),
                [](PlanCandidate const& a, PlanCandidate const& b) { return a.score > b.score; });
            if (planCandidates.size() > static_cast<size_t>(studio.altPlanWidth)) {
                planCandidates.resize(static_cast<size_t>(studio.altPlanWidth));   // only the most promising first delays get the second-edge search
            }
            int planFirst = 0;
            int planSecond = 0;
            for (auto const& candidate : planCandidates) {
                for (int secondDelay = 0; secondDelay <= maxDelay && planFirst == 0; ++secondDelay) {
                    if (clickOverTime()) break;
                    // the ghost assumes the next edge gets the same delay as this one; this moves it to `secondDelay`
                    buf.altNextEdgeShift = secondDelay - candidate.delay;
                    SimEnd planEnd;
                    int planScore = waveScore(layer, p1, p2, dual, candidate.delay, down, horizon, horizon, &planEnd);
                    if (planEnd == SimEnd::StopSearch) break;
                    if (planScore >= horizon && alternateWaveHasMargin(candidate.delay)) {
                        planFirst = candidate.delay;
                        planSecond = secondDelay;
                    }
                }
                buf.altNextEdgeShift = 0;
                if (planFirst > 0 || clickOverTime()) break;
            }
            buf.altNextEdgeShift = 0;
            if (planFirst > 0) {
                say("alternate wave plan: {} +{}t now, then the next edge +{}t", down ? "press" : "release", planFirst, planSecond);
                noteAlternateResult(AlternateResult::Adjusted, planFirst);
                buf.altWaveDelay = 0;
                decided(layer, p1, p2, dual, planFirst, down, horizon, releaseAfter);
                buf.shift = planFirst;
                queueInput(planFirst - early, down);
                return true;
            }
        }
        if (saved <= 0) {
            if (fullAlternateWave) {
                say("alternate wave: no delay gained {}t within {}t lookahead; input left unchanged",
                    st(studio.altWaveMinGain), horizon);
                noteAlternateResult(AlternateResult::NoGain);
            }
            buf.altWaveDelay = 0;
            say("no save: {}{} dies in {}t (max {}t, best +{}t -> {}t of {}){}", down ? "press" : "release", tickTag, base, maxDelay,
                dbgBestK, dbgBestRet, horizon, !crashFixed ? "" : base < st(30) ? " - too late: waiting only makes it crash sooner" : " - same crash at any timing (next obstacle)");
            decided(layer, p1, p2, dual, 0, down, horizon, releaseAfter);
            buf.appliedInput = down;
            return false;
        }

        // v2.28 robustness check. A save can work in the exact predicted conditions and still
        // fail for real, because a real run is always a hair off (a fraction of a tick, a bit
        // of float error). So the chosen delay must also survive with you starting slightly
        // ahead of and behind where you are (half a tick of movement each way). If it only
        // works at the exact spot, the next few delays are tried for one that survives all
        // three; if none does, the original save is kept, so this never makes things worse.
        bool robustNote = false;
        if (fullAlternateWave || (buf.robustSaves && !(anyWave && buf.waveV46))) {
            int target = needed;
            // half a tick of x movement: normal speed (0.9) covers 311.58 units a second
            float shift = std::max(0.3f, 0.5f * (311.58f / 240.f) * (std::abs(p1->m_playerSpeed) / 0.9f));
            auto robust = [&](int k) {
                if (fullAlternateWave) {
                    return alternateWaveHasMargin(k);
                }
                for (float off : { shift, -shift }) {
                    if (clickOverTime()) {
                        buf.ghostShiftX = 0.f;
                        return false;
                    }
                    buf.ghostShiftX = off;
                    SimEnd e;
                    int ret = scoreAt(k, target, &e);
                    buf.ghostShiftX = 0.f;
                    if (ret < target) {
                        return false;
                    }
                }
                return true;
            };
            if (fullAlternateWave) {
                // A nominally helpful route can still graze a hazard because the real click
                // may land a fraction of a tick ahead or behind the ghost. Keep the correction
                // only when the configured gain survives both small position perturbations.
                bool foundRobust = clickMs() < buf.budgetMs && robust(saved);
                if (foundRobust) {
                    robustNote = true;
                }
                else {
                    std::vector<int> candidates;
                    candidates.reserve(maxDelay);
                    for (int k = 1; k <= maxDelay; ++k) {
                        candidates.push_back(k);
                    }
                    int phaseAnchor = buf.altWaveDelay > 0 ? buf.altWaveDelay : saved;
                    std::stable_sort(candidates.begin(), candidates.end(), [phaseAnchor](int a, int b) {
                        int da = std::abs(a - phaseAnchor);
                        int db = std::abs(b - phaseAnchor);
                        return da == db ? a < b : da < db;
                    });
                    for (int k : candidates) {
                        if (clickOverTime()) {
                            break;
                        }
                        SimEnd e;
                        int ret = scoreAt(k, target, &e);
                        if (ret >= target && e != SimEnd::StopSearch && robust(k)) {
                            saved = k;
                            landed = ret;
                            robustNote = true;
                            foundRobust = true;
                            break;
                        }
                    }
                }
                if (!foundRobust) {
                    say("alternate wave: no timing meets the {}t gain with a small margin; input left unchanged",
                        st(studio.altWaveMinGain));
                    noteAlternateResult(AlternateResult::MarginRejected);
                    buf.altWaveDelay = 0;
                    decided(layer, p1, p2, dual, 0, down, horizon, releaseAfter);
                    buf.appliedInput = down;
                    return false;
                }
            }
            else if (clickMs() < buf.budgetMs && !robust(saved)) {
                for (int k = saved + 1; k <= std::min(maxDelay, saved + st(3)) && !clickOverTime(); ++k) {
                    SimEnd e;
                    int ret = scoreAt(k, target, &e);
                    if (ret >= target && robust(k)) {
                        saved = k;
                        landed = ret;
                        robustNote = true;
                        break;
                    }
                }
            }
        }

        if (buf.avoidMovers) {
            // One extra run of the chosen delay with the moving-object watch on. (Watching
            // in every search run made cube searches hit the time budget.)
            buf.trackMovers = true;
            simulateOrbs(layer, p1, p2, dual, saved, down, horizon, releaseAfter, horizon, &end);
            buf.trackMovers = false;
        }
        if (buf.avoidMovers && !buf.predictMovers && buf.sawMover) {
            say("skip +{}t save: moving objects ahead", saved);
            if (fullAlternateWave) {
                noteAlternateResult(AlternateResult::MarginRejected);
            }
            decided(layer, p1, p2, dual, 0, down, horizon, releaseAfter);
            buf.appliedInput = down;
            return false;
        }

        if (trainInstead(layer, saved, down, swingPress ? "swing" : "")) {
            buf.altWaveDelay = 0;
            decided(layer, p1, p2, dual, 0, down, horizon, releaseAfter);
            buf.appliedInput = down;
            return false;
        }
        if (robotPress) {
            releaseAfter = robotUsedHold;  // the path view / drift check show the hold that works
        }
        noteSave(layer, saved);
        if (fullAlternateWave) {
            noteAlternateResult(AlternateResult::Adjusted, saved);
        }
        say("SAVED {}{} +{}t ({:g}f)  {}->{}  [max {}]{}", down ? "press" : "release", tickTag, saved, saved * TICK, base, landed, maxDelay,
            robustNote ? std::string("  (robust)")
            : fromMemory ? std::string("  (memory)")
            : cubeStyleWave && marginUsed ? std::string("  +margin")
            : robotPress ? fmt::format("  hold {}t", robotUsedHold) : std::string());
        decided(layer, p1, p2, dual, saved, down, horizon, releaseAfter);
        buf.shift = saved;
        buf.altWaveDelay = fullAlternateWave ? saved : 0;
        queueInput(saved - early, down);
        return true;
    }

    void cycleStage(int dir) {
        buf.stage = (buf.stage + dir + STAGE_COUNT) % STAGE_COUNT;
        if (studio.progressStageBands && buf.stage < STAGE_COUNT - 1) {
            studio.progressBaseStage = buf.stage;
            Mod::get()->setSavedValue("studio-progress-base-stage", static_cast<int64_t>(buf.stage));
        }
        // v2.59: the stage is one setting for every level and survives restarts
        Mod::get()->setSavedValue("stage", buf.stage);
        resetPending();
        // A delayed input that was waiting is dropped; the game gets what you're really
        // holding now (otherwise, with the buffer switched off, it never would).
        auto* layer = GameManager::get()->m_playLayer;
        if (layer && !buf.autoReleased && buf.appliedInput != buf.realInput) {
            sendButton(layer, buf.realInput);
        }
        applyStage();
        readSettings();
        saveProfile();
        say("stage -> {}", stageText());
        refreshAutoTag();
        refreshWatermark();
        refreshPanel();
    }

#include "studio_actions.inc"

    // v2.4 wave auto-release (practice mode only): while you hold in wave, check every
    // tick whether holding crashes within the next `autoWindow` ticks. If it does and
    // letting go now clearly survives longer, let go for you. Your own release later is
    // swallowed, and your next press works as normal.
    // Auto assists (rewritten). All of them follow the "last moment" rule: the ghost looks a short way ahead; if
    // carrying on with what the game is doing now would crash, it checks whether switching NOW saves you and
    // whether switching AUTO_MARGIN ticks LATER still would. The switch is made at the last moment where it
    // still works (with a couple of ticks to spare), so your own input is left alone for as long as possible
    // and a save is as late (a jump as high) as it can be.
    constexpr int AUTO_MARGIN = 2;

    // The wave auto-release's plan: the tick to switch on (chosen for room, not just the last moment).
    struct AutoPlan {
        bool valid = false;
        bool toggleTo = false;
        int actTick = 0;
        int replanAt = 0;
        int slack = 0;
        int life = 0;
    } autoPlan;

    // Wave auto-release works in both directions: it lets go when holding would crash, and it presses again
    // when letting go would crash (after it released for you while you were still holding, the old version
    // could not press again and the wave dived into the floor). Once your own input is safe again it is
    // handed back. Robot and dash auto-release only ever let go.
    void tryAutoRelease(PlayLayer* layer, PlayerObject* p1) {
        if ((!buf.autoRelease && !buf.robotAutoRelease && !buf.dashAutoRelease) || layer->m_isPlatformer) {
            return;
        }
        if (!layer->m_isPracticeMode && !buf.autoNormalRuns) {
            static int noticeAttempt = -1;
            if (noticeAttempt != buf.attemptTick) {
                noticeAttempt = buf.attemptTick;
                say("auto assists are on, but this is a normal run: switch on 'Allow in normal runs' (Studio > Auto assists) or use Practice");
            }
            return;
        }
        if (!buf.queue.empty() || !buf.isPlayer1 || !buf.ghost1 || p1->m_isDead || frameSpam.owned) {
            return;
        }
        if (layer->m_level && layer->m_level->m_twoPlayerMode) {
            return;
        }
        bool dual = layer->m_gameState.m_isDualMode && layer->m_player2;
        auto* p2 = dual ? layer->m_player2 : nullptr;
        bool dash = p1->m_isDashing || (p2 && p2->m_isDashing);
        bool wave = !dash && (p1->m_isDart || (p2 && p2->m_isDart));
        bool robot = !dash && !wave && (p1->m_isRobot || (p2 && p2->m_isRobot));
        if (!((dash && buf.dashAutoRelease) || (wave && buf.autoRelease) || (robot && buf.robotAutoRelease))) {
            return;
        }
        bool flyer = isFlying(p1) || (p2 && isFlying(p2));
        bool mini = (p1->m_isDart && p1->m_vehicleSize < 1.f) || (p2 && p2->m_isDart && p2->m_vehicleSize < 1.f);
        bool cur = buf.appliedInput;   // what the game is doing now
        int configured = wave ? buf.waveReleaseWindow : (robot ? buf.robotReleaseWindow : buf.dashReleaseWindow);

        struct AutoReset {
            ~AutoReset() {
                buf.skipPress = false;
                buf.trackMovers = false;
            }
        } autoReset;
        buf.sawMover = false;
        buf.trackMovers = buf.avoidMovers || buf.predictMovers;
        SimEnd end = SimEnd::Ok;

        if (wave) {
            int window = std::max(st(std::clamp(configured, 2, 64)), st(12));
            int horizon = std::min(fullWaveHorizon(mini), st(36));
            if (studio.perfGuardOn) {
                horizon = std::max(st(12), static_cast<int>(static_cast<float>(horizon) * perfGuardScale()));
            }
            // hand the wave back as soon as what you are really holding is safe again
            if (buf.autoReleased && buf.realInput != cur) {
                int back = waveScore(layer, p1, p2, dual, 0, buf.realInput, horizon, horizon, &end);
                if (back >= horizon && end != SimEnd::StopSearch) {
                    buf.autoReleased = false;
                    autoPlan.valid = false;
                    sendButton(layer, buf.realInput);
                    say("AUTO wave: your {} is safe again, handed back", buf.realInput ? "hold" : "release");
                    return;
                }
            }
            buf.skipPress = true;
            buf.orbClick = buf.assumeOrbs;
            int nothing = simulate(layer, p1, p2, dual, 0, cur, window, -1, window, &end);
            buf.skipPress = false;
            if (nothing >= window || buf.ghostHitPortal) {
                autoPlan.valid = false;
                return;
            }
            if (buf.avoidMovers && !buf.predictMovers && buf.sawMover) {
                return;
            }
            // Plan the BEST frame: every tick from now to the crash is a possible switch time; the safe ones are
            // compared for room (how much timing error each survives) and the roomiest wins, the later one on a
            // tie. The plan is kept for a few ticks (the search is the expensive part), then made again.
            if (!autoPlan.valid || tickCount >= autoPlan.replanAt || autoPlan.toggleTo != !cur) {
                autoPlan = {};
                autoPlan.toggleTo = !cur;
                autoPlan.replanAt = tickCount + 3;
                int latest = std::min(nothing, st(12));
                std::vector<std::pair<int, int>> options;   // switch in t ticks, how long it lives
                int bestLife = -1;
                int bestLifeAt = -1;
                for (int t = 0; t <= latest; ++t) {
                    SimEnd optionEnd = SimEnd::Ok;
                    int life = waveScore(layer, p1, p2, dual, t, !cur, horizon, horizon, &optionEnd);
                    if (optionEnd == SimEnd::StopSearch) break;
                    if (life > bestLife) {
                        bestLife = life;
                        bestLifeAt = t;
                    }
                    if (life >= horizon) options.emplace_back(t, life);
                }
                if (!options.empty()) {
                    // compare at most five of the safe times, spread over the safe stretch
                    std::vector<int> picks;
                    size_t count = options.size();
                    for (size_t k : { size_t(0), count / 4, count / 2, (count * 3) / 4, count - 1 }) {
                        int t = options[std::min(k, count - 1)].first;
                        if (std::find(picks.begin(), picks.end(), t) == picks.end()) picks.push_back(t);
                    }
                    float unit = (311.58f / 240.f) * (std::abs(p1->m_playerSpeed) / 0.9f);
                    int chosen = picks.front();
                    int chosenSlack = -1;
                    for (int t : picks) {
                        int slack = 0;
                        for (int level = 1; level <= studio.altRoomLevels; ++level) {
                            bool ok = true;
                            for (float offset : { 0.5f * static_cast<float>(level) * unit, -0.5f * static_cast<float>(level) * unit }) {
                                buf.ghostShiftX = offset;
                                SimEnd slackEnd = SimEnd::Ok;
                                int life = waveScore(layer, p1, p2, dual, t, !cur, horizon, horizon, &slackEnd);
                                buf.ghostShiftX = 0.f;
                                if (life < horizon || slackEnd == SimEnd::StopSearch) {
                                    ok = false;
                                    break;
                                }
                            }
                            if (!ok) break;
                            slack = level;
                        }
                        if (slack > chosenSlack || (slack == chosenSlack && t > chosen)) {
                            chosen = t;
                            chosenSlack = slack;
                        }
                    }
                    autoPlan.valid = true;
                    autoPlan.actTick = tickCount + chosen;
                    autoPlan.slack = chosenSlack;
                    autoPlan.life = horizon;
                    if (buf.explain) {
                        say("auto wave plan: {} safe switch times in the next {}t, best in {}t ({} steps of room)", options.size(), latest, chosen, chosenSlack);
                    }
                }
                else if (bestLife > nothing + st(4) && nothing <= st(2)) {
                    autoPlan.valid = true;                 // nothing is safe any more: the best chance left, now
                    autoPlan.actTick = tickCount + bestLifeAt;
                    autoPlan.life = bestLife;
                    autoPlan.slack = -1;
                }
                else if (buf.explain) {
                    say("auto wave: crash in {}t and no switch time saves it", nothing);
                }
            }
            if (!autoPlan.valid || tickCount < autoPlan.actTick) {
                return;
            }
            // the planned frame: check once more that it still works, then switch
            int check = waveScore(layer, p1, p2, dual, 0, !cur, horizon, horizon, &end);
            if (check < horizon && !(nothing <= st(2) && check > nothing + st(4))) {
                autoPlan.valid = false;
                return;
            }
            autoPlan.valid = false;
            buf.autoReleased = true;
            buf.autoDash = false;
            buf.autoTagLast = tickCount;
            sendButton(layer, !cur);
            say("AUTO wave {}: crash in {}t, switched on the planned frame{}", cur ? "release" : "press", nothing,
                autoPlan.slack >= 0 ? fmt::format(" ({} steps of room)", autoPlan.slack) : std::string(" (best chance left)"));
            return;
        }

        // robot / dash: only letting go is possible, and only while you are holding
        if (!cur || !buf.realInput) {
            return;
        }
        int window = std::max(st(std::clamp(configured, 2, 64)), st(24));
        buf.skipPress = true;
        buf.orbClick = buf.assumeOrbs;
        int hold = simulate(layer, p1, p2, dual, 0, true, window, -1, window, &end);
        buf.skipPress = false;
        if (hold >= window || buf.ghostHitPortal) {
            return;
        }
        int horizon = (dash && flyer) ? st(48) : st(std::clamp(buf.groundHorizon, 48, 240));
        // a jump that has landed counts as saved (the crash after it belongs to the next obstacle)
        auto saved = [&](int life) {
            return life >= horizon || (buf.lateCrash > 0 && life >= st(buf.lateCrash) && buf.baseTouchedDown);
        };
        buf.baseTouchedDown = false;
        int now = simulateOrbs(layer, p1, p2, dual, 0, false, horizon, -1, horizon, &end);
        bool okNow = saved(now);
        buf.baseTouchedDown = false;
        int later = simulateOrbs(layer, p1, p2, dual, AUTO_MARGIN, false, horizon, -1, horizon, &end);
        bool okLater = saved(later);
        if (buf.avoidMovers && !buf.predictMovers && buf.sawMover) {
            return;
        }
        if (!(okNow && !okLater)) {
            if (buf.explain) say("auto {}: holding dies in {}t, release now {} / {}t later {}", robot ? "robot" : "dash", hold,
                okNow ? "saves" : "does not save", AUTO_MARGIN, okLater ? "still saves (waiting)" : "does not save");
            return;
        }
        buf.autoReleased = true;
        buf.autoDash = dash;
        buf.autoTagLast = tickCount;
        sendButton(layer, false);
        say("AUTO release{}: holding dies in {}t, this is the last safe moment (lives {}t)", dash ? " (dash)" : " (robot)", hold, now);
    }

    // Auto-click: while you are not holding, if doing nothing would crash, it clicks for you at the last moment
    // where the click still saves you. A jump that lands counts as saved, so a click is no longer refused just
    // because the NEXT obstacle would kill a ghost that makes no more inputs (that was why it almost never fired).
    // Your own click, if it still comes, is swallowed.
    void tryAutoClick(PlayLayer* layer, PlayerObject* p1) {
        if (!buf.autoClick || !buf.enabled || buf.autoClicked || buf.swallowRelease || buf.autoReleased || layer->m_isPlatformer) {
            return;
        }
        if (!layer->m_isPracticeMode && !buf.autoNormalRuns) {
            return;
        }
        if (!buf.queue.empty() || buf.realInput || buf.appliedInput || !buf.isPlayer1 || !buf.ghost1 || buf.sinceInput < st(4) ||
            frameSpam.owned) {
            return;
        }
        if (layer->m_level && layer->m_level->m_twoPlayerMode) {
            return;
        }
        bool dual = layer->m_gameState.m_isDualMode && layer->m_player2;
        auto* p2 = dual ? layer->m_player2 : nullptr;
        auto skipMode = [](PlayerObject* p) { return p->m_isDart || p->m_isShip || p->m_isDashing; };
        if (p1->m_isDead || skipMode(p1) || (p2 && skipMode(p2))) {
            return;
        }
        bool robot = p1->m_isRobot || (p2 && p2->m_isRobot);
        bool flyer = isFlying(p1) || (p2 && isFlying(p2));

        struct AutoReset {
            ~AutoReset() {
                buf.skipPress = false;
                buf.trackMovers = false;
                buf.orbClick = buf.assumeOrbs;
            }
        } autoReset;
        int window = std::max(st(std::clamp(buf.autoWindow, 2, 24)), st(16));
        buf.sawMover = false;
        buf.skipPress = true;
        buf.orbClick = false;  // no clicks at all in the do-nothing run
        SimEnd end = SimEnd::Ok;
        int idle = simulate(layer, p1, p2, dual, 0, true, window, -1, window, &end);
        buf.skipPress = false;
        buf.orbClick = buf.assumeOrbs;
        if (idle >= window || buf.ghostHitPortal) {
            return;
        }
        int horizon = st(flyer ? 48 : std::clamp(buf.groundHorizon, 48, 240));
        int hold = robot ? robotHold.pick(st(20), 50) : holdEstimate();
        buf.trackMovers = buf.avoidMovers || buf.predictMovers;
        auto saved = [&](int life) {
            return life >= horizon || (!flyer && buf.lateCrash > 0 && life >= st(buf.lateCrash) && buf.baseTouchedDown);
        };
        buf.baseTouchedDown = false;
        int now = simulateOrbs(layer, p1, p2, dual, 0, true, horizon, hold, horizon, &end);
        bool okNow = saved(now);
        buf.baseTouchedDown = false;
        int later = simulateOrbs(layer, p1, p2, dual, AUTO_MARGIN, true, horizon, hold, horizon, &end);
        bool okLater = saved(later);
        if (buf.avoidMovers && !buf.predictMovers && buf.sawMover) {
            return;
        }
        if (!(okNow && !okLater)) {
            if (buf.explain) say("auto-click: doing nothing dies in {}t, click now {} / {}t later {}", idle,
                okNow ? "saves" : "does not save", AUTO_MARGIN, okLater ? "still saves (waiting)" : "does not save");
            return;
        }
        buf.autoClicked = true;
        buf.autoClickAge = 0;
        buf.autoTagLast = tickCount;
        sendButton(layer, true);
        queueInput(hold, false);
        say("AUTO click{}: doing nothing dies in {}t, this is the last safe moment (hold {}t)", robot ? " (robot)" : "", idle, hold);
    }

    // ---------------------------------------------------------------------------------------------------------
    // Fun tab: optional extras (all off by default). Everything is per visit of a level.
    // ---------------------------------------------------------------------------------------------------------

    void funReset() {
        fun = FunState {};
    }

    void funFlash(std::string const& text) {
        fun.flashUntil = tickCount + st(360);   // 1.5 seconds
        if (buf.funFlash) {
            buf.funFlash->setString(text.c_str());
            buf.funFlash->setVisible(true);
        }
    }

    void funOnClick(bool down) {
        if (!studio.funAny) return;
        fun.edgeTicks.push_back(tickCount);
        while (fun.edgeTicks.size() > 9) fun.edgeTicks.pop_front();
        if (down) {
            fun.pressTicks.push_back(tickCount);
            while (!fun.pressTicks.empty() && fun.pressTicks.front() < tickCount - st(240)) fun.pressTicks.pop_front();
            fun.peakCps = std::max(fun.peakCps, static_cast<double>(fun.pressTicks.size()));
            if (studio.fun[13]) {
                FMODAudioEngine::sharedEngine()->playEffect("playSound_01.ogg");
            }
        }
    }

    void funOnAttempt() {
        ++fun.attempts;
        fun.bestAtAttempt = fun.best;
        fun.flashedBest = false;
        fun.flashedSpot = false;
        fun.hardestX = -1.f;
        uint32_t most = 0;
        int slice = 0;
        for (auto const& [sliceIndex, spot] : memory) {
            if (spot.deaths > most) {
                most = spot.deaths;
                slice = sliceIndex;
            }
        }
        if (most >= 3) {
            fun.hardestX = static_cast<float>(slice) * SLICE;
        }
    }

    void funOnDeath(float percent) {
        if (!studio.funAny) return;
        fun.best = std::max(fun.best, percent);
        fun.deaths.push_back(percent);
        if (fun.lastDeath >= 0.f && percent >= fun.lastDeath) ++fun.streak;
        else fun.streak = 0;
        fun.lastDeath = percent;
        if (studio.fun[17]) {
            funFlash(percent >= 95.f ? "SO CLOSE!" : percent >= 75.f ? "ALMOST!" : percent >= 50.f ? "NICE RUN" : percent >= 25.f ? "GETTING THERE" : "AGAIN!");
        }
        else if (studio.fun[16] && fun.streak >= 2) {
            funFlash(fmt::format("IMPROVING x{}", fun.streak));
        }
    }

    void funTick(PlayLayer* layer) {
        if (!buf.funHud || !buf.funFlash) return;
        if (!studio.funAny) {
            if (buf.funHud->isVisible()) buf.funHud->setVisible(false);
            if (buf.funFlash->isVisible()) buf.funFlash->setVisible(false);
            return;
        }
        auto* p1 = layer->m_player1;
        if (!p1) return;
        float percent = layer->getCurrentPercent();
        fun.best = std::max(fun.best, percent);
        if (fun.flashUntil > 0 && tickCount >= fun.flashUntil) {
            buf.funFlash->setVisible(false);
            fun.flashUntil = 0;
        }
        if (!p1->m_isDead) {
            if (studio.fun[14] && !fun.flashedBest && !fun.deaths.empty() && fun.bestAtAttempt >= 1.f && percent > fun.bestAtAttempt + 0.4f) {
                fun.flashedBest = true;
                funFlash(fmt::format("NEW BEST {:.0f}%", percent));
                FMODAudioEngine::sharedEngine()->playEffect("counter003.ogg");
            }
            if (studio.fun[15] && !fun.flashedSpot && fun.hardestX >= 0.f && p1->getPosition().x > fun.hardestX + 45.f) {
                fun.flashedSpot = true;
                funFlash("SPOT CLEARED!");
                FMODAudioEngine::sharedEngine()->playEffect("counter003.ogg");
            }
        }
        if (++fun.hudCounter < 6) return;
        fun.hudCounter = 0;

        std::vector<std::string> lines;
        auto now = std::chrono::steady_clock::now();
        double seconds = std::chrono::duration<double>(now - fun.levelStart).count();
        while (!fun.pressTicks.empty() && fun.pressTicks.front() < tickCount - st(240)) fun.pressTicks.pop_front();
        if (studio.fun[0]) lines.push_back(fmt::format("CPS {}", fun.pressTicks.size()));
        if (studio.fun[1]) lines.push_back(fmt::format("PEAK CPS {:.0f}", fun.peakCps));
        if (studio.fun[2]) lines.push_back(fmt::format("TIME {}:{:02}", static_cast<int>(seconds) / 60, static_cast<int>(seconds) % 60));
        if (studio.fun[3]) lines.push_back(fmt::format("ATTEMPT {}", std::max(1, fun.attempts)));
        if (studio.fun[4]) lines.push_back(fmt::format("BEST {:.0f}%", fun.best));
        if (studio.fun[5]) {
            std::string text = "DIED AT";
            size_t first = fun.deaths.size() > 5 ? fun.deaths.size() - 5 : 0;
            for (size_t i = first; i < fun.deaths.size(); ++i) text += fmt::format(" {:.0f}", fun.deaths[i]);
            lines.push_back(fun.deaths.empty() ? std::string("NO DEATHS YET") : text);
        }
        if (studio.fun[6]) {
            double sum = 0.0;
            for (float d : fun.deaths) sum += d;
            lines.push_back(fun.deaths.empty() ? std::string("AVG DEATH -") : fmt::format("AVG DEATH {:.0f}%", sum / fun.deaths.size()));
        }
        if (studio.fun[7]) {
            bool mini = p1->m_vehicleSize < 1.f;
            bool dual = layer->m_gameState.m_isDualMode;
            lines.push_back(fmt::format("{} {}{}{}", scanModeName(studioModeOf(p1)), scanSpeedName(p1->m_playerSpeed), mini ? " MINI" : "", dual ? " DUAL" : ""));
        }
        if (studio.fun[8]) {
            std::string text = "BUSY PART: SCANNING";
            if (levelScan.done) {
                text = "NO BUSY PART AHEAD";
                float x = p1->getPosition().x;
                float speed = std::max(0.3f, std::abs(p1->m_playerSpeed));
                for (auto const& section : levelScan.sections) {
                    if (section.busy && section.endX > x) {
                        float secondsAway = std::max(0.f, section.startX - x) / (311.58f * speed / 0.9f);
                        text = secondsAway <= 0.05f ? fmt::format("IN A BUSY PART ({})", sectionName(section))
                            : fmt::format("BUSY PART IN {:.1f}s ({})", secondsAway, sectionName(section));
                        break;
                    }
                }
            }
            lines.push_back(text);
        }
        if (studio.fun[9]) lines.push_back(levelHardestText());
        if (studio.fun[10]) {
            std::string text = "EVEN -";
            if (fun.edgeTicks.size() >= 6) {
                std::vector<double> gaps;
                for (size_t i = 1; i < fun.edgeTicks.size(); ++i) gaps.push_back(static_cast<double>(fun.edgeTicks[i] - fun.edgeTicks[i - 1]));
                double mean = 0.0;
                for (double g : gaps) mean += g;
                mean /= gaps.size();
                double variance = 0.0;
                for (double g : gaps) variance += (g - mean) * (g - mean);
                variance /= gaps.size();
                double evenness = mean > 0.0 ? std::clamp(100.0 * (1.0 - std::sqrt(variance) / mean), 0.0, 100.0) : 0.0;
                text = fmt::format("EVEN {:.0f}%", evenness);
            }
            lines.push_back(text);
        }
        if (studio.fun[11]) {
            bool recent = tickCount - fun.lastDelayTick < st(240);
            lines.push_back(recent ? fmt::format("ASSIST +{}t", fun.lastDelay) : std::string("ASSIST 0"));
        }
        if (studio.fun[12]) lines.push_back(fmt::format("SAVES {}", sessionTelemetry().saves));

        std::string text;
        for (auto const& line : lines) text += line + "\n";
        buf.funHud->setString(text.c_str());
        buf.funHud->setVisible(!lines.empty());
        if (studio.fun[18]) {
            float hue = static_cast<float>(std::fmod(seconds * 70.0, 360.0)) * 0.0174533f;
            auto channel = [hue](float shift) { return static_cast<GLubyte>(150.f + 105.f * std::sin(hue + shift)); };
            buf.funHud->setColor({ channel(0.f), channel(2.094f), channel(4.188f) });
        }
        else {
            buf.funHud->setColor({ 215, 234, 243 });
        }
        if (studio.fun[19]) {
            // zen: the Synca on-screen parts get out of the way (the info lines and flashes you asked for stay)
            if (buf.watermark) buf.watermark->setVisible(false);
            if (buf.panel) buf.panel->setVisible(false);
            if (buf.orbDot) buf.orbDot->setVisible(false);
            if (buf.nextModeLabel) buf.nextModeLabel->setVisible(false);
        }
    }

    // The AUTO INPUTS ON tag is brightest for 3 seconds after an auto input and then settles to the opacity set in
    // the Studio tab. It never goes below 25% and it cannot be removed: in a normal run it is what marks that auto
    // inputs were on.
    void updateAutoTagFade() {
        if (!buf.autoTag || !buf.autoTag->isVisible()) {
            return;
        }
        if (studio.autoTagCompact) {
            // Compact: out of the way while you play, but shown in full for the first 3 seconds of every attempt,
            // while the game is paused and on the end screen, so a run with auto inputs is still marked where it counts.
            static int checkCounter = 0;
            static bool screenUp = false;
            if (++checkCounter >= 10) {
                checkCounter = 0;
                auto* scene = CCDirector::get()->getRunningScene();
                screenUp = scene && (scene->getChildByType<PauseLayer>(0) || scene->getChildByType<EndLevelLayer>(0));
            }
            bool show = (tickCount - buf.attemptTick) < st(720) || screenUp;
            buf.autoTag->setOpacity(show ? 255 : 0);
            return;
        }
        float resting = static_cast<float>(std::clamp(studio.autoTagOpacity, 25, 100));
        int age = tickCount - buf.autoTagLast;
        int bright = st(720);   // 3 seconds
        int fade = st(240);     // then one second to settle
        float percent = age < bright ? 100.f
            : (age < bright + fade ? 100.f - (100.f - resting) * static_cast<float>(age - bright) / static_cast<float>(fade) : resting);
        buf.autoTag->setOpacity(static_cast<GLubyte>(std::clamp(percent, 25.f, 100.f) * 255.f / 100.f));
    }

    // "NEXT: WAVE 3x mini in 1.2s" at the top of the screen (Studio > This level). Follows the gamemode timeline the
    // level scan built from the portals.
    void updateNextModeHud(PlayLayer* layer) {
        if (!buf.nextModeLabel) {
            return;
        }
        if (!studio.nextModeHud || !levelScan.done || levelScan.sections.size() < 2 || !layer->m_player1) {
            buf.nextModeLabel->setVisible(false);
            return;
        }
        static int counter = 0;
        if (++counter < 6) {
            return;
        }
        counter = 0;
        float x = layer->m_player1->getPosition().x;
        auto const& sections = levelScan.sections;
        size_t index = 0;
        while (index + 1 < sections.size() && sections[index + 1].startX <= x) {
            ++index;
        }
        if (index + 1 >= sections.size()) {
            buf.nextModeLabel->setVisible(false);
            return;
        }
        auto const& next = sections[index + 1];
        float speed = std::max(0.3f, std::abs(layer->m_player1->m_playerSpeed));
        float seconds = (next.startX - x) / (311.58f * speed / 0.9f);
        buf.nextModeLabel->setString(fmt::format("NEXT: {}{} in {:.1f}s", sectionName(next), next.busy ? " (busy)" : "", seconds).c_str());
        buf.nextModeLabel->setVisible(true);
    }

    // Ship guard (for tight ship corridors such as the ship part of Tidal Wave): inside the chosen level range,
    // when carrying on would hit the ceiling or the floor, the ship is switched (hold <-> let go) at the last
    // moment where that still saves it, and handed back to your own input as soon as that is safe again.
    void tryShipGuard(PlayLayer* layer, PlayerObject* p1) {
        if (!studio.shipGuard || !buf.enabled || layer->m_isPlatformer || p1->m_isDead || frameSpam.owned) {
            return;
        }
        if (!layer->m_isPracticeMode && !buf.autoNormalRuns) {
            return;
        }
        if (!buf.queue.empty() || !buf.isPlayer1 || !buf.ghost1 || (layer->m_level && layer->m_level->m_twoPlayerMode)) {
            return;
        }
        bool dual = layer->m_gameState.m_isDualMode && layer->m_player2;
        auto* p2 = dual ? layer->m_player2 : nullptr;
        if (!(p1->m_isShip || (p2 && p2->m_isShip))) {
            return;
        }
        float percent = layer->getCurrentPercent();
        if (percent < static_cast<float>(studio.shipGuardFrom) || percent > static_cast<float>(studio.shipGuardTo)) {
            return;
        }
        struct GuardReset {
            ~GuardReset() {
                buf.skipPress = false;
                buf.trackMovers = false;
            }
        } guardReset;
        buf.sawMover = false;
        buf.trackMovers = buf.avoidMovers || buf.predictMovers;
        bool cur = buf.appliedInput;
        int horizon = st(36);
        SimEnd end = SimEnd::Ok;
        if (buf.autoReleased && buf.realInput != cur) {
            int back = simulateOrbs(layer, p1, p2, dual, 0, buf.realInput, horizon, -1, horizon, &end);
            if (back >= horizon && end != SimEnd::StopSearch) {
                buf.autoReleased = false;
                sendButton(layer, buf.realInput);
                say("SHIP GUARD: your {} is safe again, handed back", buf.realInput ? "hold" : "release");
                return;
            }
        }
        int window = st(20);
        buf.skipPress = true;
        buf.orbClick = buf.assumeOrbs;
        int nothing = simulate(layer, p1, p2, dual, 0, cur, window, -1, window, &end);
        buf.skipPress = false;
        if (nothing >= window || buf.ghostHitPortal) {
            return;
        }
        int now = simulateOrbs(layer, p1, p2, dual, 0, !cur, horizon, -1, horizon, &end);
        int later = simulateOrbs(layer, p1, p2, dual, AUTO_MARGIN, !cur, horizon, -1, horizon, &end);
        bool safeNow = now >= horizon;
        bool safeLater = later >= horizon;
        if (buf.avoidMovers && !buf.predictMovers && buf.sawMover) {
            return;
        }
        bool lastResort = !safeNow && nothing <= st(2) && now > nothing + st(4);
        if (!((safeNow && !safeLater) || lastResort)) {
            return;
        }
        buf.autoReleased = true;
        buf.autoDash = false;
        buf.autoTagLast = tickCount;
        sendButton(layer, !cur);
        say("SHIP GUARD {}: crash in {}t at {:.0f}%, {}", cur ? "release" : "press", nothing, percent,
            safeNow ? "this is the last safe moment" : "best chance left");
    }

    void endFrameSpam(PlayLayer* layer, char const* why) {
        if (!frameSpam.owned) {
            return;
        }
        frameSpam.owned = false;
        frameSpam.checked = false;
        if (layer && buf.appliedInput != buf.realInput) {
            sendButton(layer, buf.realInput);   // hand the wave back to what you are really holding
        }
        say("frame spam off ({})", why);
    }

    // One physics tick of frame spam: toggle the wave every `spamTicks` ticks. Before each toggle the ghost
    // measures where you are in the gap; if the toggle would push you toward a wall it waits a little
    // longer (the same centring as gap centering), so the spam stays in the middle.
    void tickFrameSpam(PlayLayer* layer, PlayerObject* p1) {
        if (!studio.spamOn || !buf.enabled || p1->m_isDead || !p1->m_isDart || layer->m_isPlatformer) {
            endFrameSpam(layer, "left the wave or switched off");
            return;
        }
        if (tickCount - frameSpam.lastRealEdge > st(40)) {
            endFrameSpam(layer, "you stopped spamming");
            return;
        }
        if (tickCount < frameSpam.nextEdge) {
            return;
        }
        if (!frameSpam.checked) {
            frameSpam.checked = true;
            int holdLife = 0;
            int idleLife = 0;
            if (!probeGap(layer, p1, nullptr, false, holdLife, idleLife)) {
                endFrameSpam(layer, "left the gap");
                return;
            }
            double wantedEdge = std::max(1, st(240)) / (2.0 * std::max(4, studio.spamCps));
            frameSpam.periodExact = std::min(wantedEdge, std::max(1.0, (holdLife + idleLife) * 0.55));
            frameSpam.period = std::max(1, static_cast<int>(std::lround(frameSpam.periodExact)));
            int height = (idleLife - holdLife) / 2;                    // ticks above the middle of the gap
            int ideal = frameSpam.period / 2;                          // a steady spam turns at +-half of one phase
            // holding -> release is the top turn (wait while below it); released -> press is the bottom turn (wait while above it)
            int extend = frameSpam.holding ? ideal - height : height + ideal;
            extend = static_cast<int>(std::lround(static_cast<double>(extend) * studio.sfStrength / 100.0));
            if (extend >= std::max(1, st(studio.sfDead))) {
                frameSpam.nextEdge = tickCount + std::clamp(extend, 1, frameSpam.period * 2);
                return;
            }
        }
        frameSpam.checked = false;
        frameSpam.holding = !frameSpam.holding;
        buf.autoTagLast = tickCount;
        sendButton(layer, frameSpam.holding);
        // fractional bookkeeping keeps the average rate exact (7.5 ticks = a 7, then an 8, ...)
        frameSpam.edgeTime = std::max(frameSpam.edgeTime + frameSpam.periodExact, static_cast<double>(tickCount) + 1.0);
        frameSpam.nextEdge = static_cast<int>(std::lround(frameSpam.edgeTime));
    }

    void deliverPending(PlayLayer* layer, PlayerObject* player) {
        if (buf.autoClicked && ++buf.autoClickAge > st(24)) {
            buf.autoClicked = false;  // your click never came: the next one is yours again
        }
        if (buf.sinceInput <= 998) {
            ++buf.sinceInput;
        }
        // v2.85: nothing is sent to the game while you're dead (a press then carried over into
        // the next attempt)
        if (player->m_isDead) {
            refreshAutoTag();
            return;
        }
        if ((buf.avoidMovers || buf.predictMovers) && !watched.empty() && !g_p2.active) {
            updateWatched();
        }
        if (frameSpam.owned && !g_p2.active) {
            tickFrameSpam(layer, player);
            return;
        }

        bool twoPlayer = layer->m_level && layer->m_level->m_twoPlayerMode;
        bool dual = !twoPlayer && layer->m_gameState.m_isDualMode && layer->m_player2;
        bool customShipBuffer = studio.perMode && studio.modeTicks[SM_SHIP] > 0;
        if (!buf.shipBuffer && !customShipBuffer && !studio.shipGuard &&
            (player->m_isShip || (dual && layer->m_player2->m_isShip))) {
            resetPending();
            buf.autoReleased = false;
            if (buf.appliedInput != buf.realInput) {
                sendButton(layer, buf.realInput);
            }
            return;
        }
        // Left wave / robot while auto-released: the game goes back to what you're really holding.
        bool autoMode = player->m_isDart || player->m_isRobot || (studio.shipGuard && player->m_isShip) ||
            (dual && (layer->m_player2->m_isDart || layer->m_player2->m_isRobot || (studio.shipGuard && layer->m_player2->m_isShip)));
        if (buf.autoReleased && !buf.autoDash && !autoMode) {
            buf.autoReleased = false;
        }

        if (buf.queue.empty()) {
            if (buf.appliedInput != buf.realInput && !buf.autoReleased) {
                sendButton(layer, buf.realInput);
            }
            tryAutoRelease(layer, player);
            tryAutoClick(layer, player);
            tryShipGuard(layer, player);
            sweepGhostLayer();
            return;
        }

        // Check before decrementing: an input queued with N ticks fires right before
        // physics step N, which is exactly what simulate(delay = N) tested.
        while (!buf.queue.empty() && buf.queue.front().ticksLeft <= 0) {
            bool down = buf.queue.front().down;
            buf.queue.pop_front();
            buf.releaseShift = down ? buf.shift : 0;
            if (down != buf.appliedInput) {
                sendButton(layer, down);
            }
        }
        for (auto& q : buf.queue) {
            --q.ticksLeft;
        }
        if (buf.queue.empty()) {
            buf.shift = 0;
        }
    }

    // Small entry points for the menu (it lives in menu.cpp and cannot see the engine's internals).
    // Where you die most in this level (from the level memory), with the gamemode stretch it is in.
    std::string levelHardestText() {
        int bestSlice = 0;
        uint32_t most = 0;
        for (auto const& [slice, spot] : memory) {
            if (spot.deaths > most) {
                most = spot.deaths;
                bestSlice = slice;
            }
        }
        if (most == 0) {
            return "Most deaths: none recorded here yet";
        }
        float x = static_cast<float>(bestSlice) * SLICE;
        if (!levelScan.done || levelScan.lastX <= 0.f) {
            return fmt::format("Most deaths: x {:.0f} ({} deaths)", x, most);
        }
        int percent = std::clamp(static_cast<int>(x / levelScan.lastX * 100.f), 0, 100);
        std::string where;
        for (auto const& section : levelScan.sections) {
            if (x >= section.startX && x < section.endX) {
                where = " " + sectionName(section);
                break;
            }
        }
        return fmt::format("Most deaths: {}%{} ({} deaths)", percent, where, most);
    }

    void forgetLevelMemory() {
        memory.clear();
        pending.active = false;
        memoryDirty = true;
        saveMemory();
        refreshHeatmap();
        say("memory for this level cleared");
    }

    void requestRunCheck() {
        benchPending = true;
        say("run check: runs at the start of your next attempt from the beginning (restart the level)");
    }

    MenuTelemetry sessionTelemetry() {
        return { sessionStats.altEdges, sessionStats.altAdjusted, sessionStats.saves, memory.size() };
    }

    // HUD layout (Studio tab): the watermark, the console panel and the orb dot can each be nudged.
    void applyHudLayout() {
        auto win = CCDirector::get()->getWinSize();
        if (buf.watermark) {
            buf.watermark->setPosition({ 6.f + studio.hudOff[0], 4.f + studio.hudOff[1] });
        }
        if (buf.panel) {
            buf.panel->setPosition({ 8.f + studio.hudOff[2], win.height - PANEL_H - 8.f + studio.hudOff[3] });
        }
        if (buf.orbDot) {
            buf.orbDot->setPosition({ win.width / 2.f + studio.hudOff[4], 26.f + studio.hudOff[5] });
        }
    }

    // Orb confidence dot: shown for half a second after every orb decision.
    void refreshOrbDot() {
        if (!buf.orbDot) return;
        bool show = studio.orbConfOn && buf.enabled && buf.orbConf > 0 && buf.orbConfAge < st(120);
        int state = show ? buf.orbConf : 0;
        if (state == buf.orbConfShown) return;
        buf.orbConfShown = state;
        buf.orbDot->clear();
        if (state == 0) return;
        ccColor4F color = state == 3 ? ccColor4F { 0.31f, 0.90f, 0.47f, 1.f }
            : (state == 2 ? ccColor4F { 1.f, 0.80f, 0.31f, 1.f } : ccColor4F { 1.f, 0.35f, 0.35f, 1.f });
        buf.orbDot->drawDot({ 0.f, 0.f }, 9.f, { color.r, color.g, color.b, 0.25f });
        buf.orbDot->drawDot({ 0.f, 0.f }, 5.5f, color);
    }
}

using namespace synca;


// v2.46: the level complete screen says so if the buffer was on during the attempt.
class $modify(BufEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        if (!buf.usedThisAttempt) {
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
        wakeMenuButton();   // the floating Synca button shows again while the game is paused
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

class $modify(BufPlayerObject, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::playSpiderDashEffect", HOOK_PRIORITY);
        // v2.18: run before Click Between Frames, so this sees each physics tick once, with
        // its full dt, and CBF's mid-tick clicks arrive while inRealUpdate is set.
        (void)self.setHookPriority("PlayerObject::update", HOOK_PRIORITY);
        (void)self.setHookPriority("PlayerObject::incrementJumps", HOOK_PRIORITY);
        (void)self.setHookPriority("PlayerObject::ringJump", HOOK_PRIORITY);
    }

    // Runs 240x/sec per player. Ghost steps (during a simulation) skip straight to
    // the original; F6 is no longer polled here (see BufKeyboard below).
    void update(float dt) {
        if (buf.simulating) {
            PlayerObject::update(dt);
            return;
        }
        auto* layer = GameManager::get()->m_playLayer;
        if (layer && this == layer->m_player1) {
            ++tickCount;
            ++logClock;
            if (buf.enabled) {
                buf.usedThisAttempt = true;
            }
            if (buf.panelDirty) {
                buf.panelDirty = false;
                refreshPanel();
            }
            if (benchNeedStart) {
                benchNeedStart = false;
                if (benchPending) {
                    benchPending = false;
                    benchCheck(layer, this);
                }
                benchStart(layer, this);
            }
            driftStep(this);
            memoryTick();
            updateProgressStageBand(layer);
            ensureLevelScan(layer);
            adaptiveProgress(layer->getCurrentPercent());
            perfGuardTick(CCDirector::get()->getDeltaTime() * 1000.f);
            if (buf.orbConfAge < 100000) ++buf.orbConfAge;
            if (buf.keepNode && buf.keepTick > -1000000 && tickCount - buf.keepTick > st(240)) {
                buf.keepNode->clear();
                buf.keepTick = -1000000;
            }
            refreshOrbDot();
            updateAutoTagFade();
            updateNextModeHud(layer);
            funTick(layer);
            if (buf.enabled) {
                deliverPending(layer, this);
                livePath(layer, this);
            }
        }

        if (layer && this == layer->m_player2 && p2OwnBuffer(layer)) {
            P2Context context(layer);
            if (buf.enabled) {
                deliverPending(layer, this);
            }
        }

        bool isP1 = layer && this == layer->m_player1;
        if (isP1) {
            // v2.37: getPosition() is updated every physics step; PlayerObject::m_position lags
            // behind, which kept CBF sync, the drift check, path view and memory slices off.
            buf.tickStartX = getPosition().x;
            buf.inRealUpdate = true;
        }
        PlayerObject::update(dt);
        if (isP1) {
            benchStep(this);
            buf.inRealUpdate = false;
            float dx = getPosition().x - buf.tickStartX;
            if (std::abs(dx) > 0.01f) {
                buf.tickDx = dx;
            }
            // The ghost must step with the dt the real player gets. v2.23: with unlocked FPS the
            // step length jitters from frame to frame, so use a running average; a big jump
            // (FPS cap or time warp changed) is taken over straight away. (P1 only: with CBF,
            // P2 can be stepped in pieces of a tick.)
            if (dt > 0.f) {
                float avg = buf.tickLength;
                buf.tickLength = (avg > 0.f && std::abs(dt - avg) < avg * 0.3f) ? avg * 0.9f + dt * 0.1f : dt;
            }
        }
    }

    void incrementJumps() {
        if (buf.simulating) {
            return;
        }
        PlayerObject::incrementJumps();
    }

    // v2.24: the ghost runs the real player's code, which also spawns visual effects
    // (circles, bump waves, trail fade-outs, spider run sprites, rotation actions). None
    // of it matters for physics, and some of it was added to the real level, so ghosts
    // skip all of it.
    bool ghostEffect() {
        return buf.simulating && isGhost(this);
    }

    void spawnCircle() {
        if (!ghostEffect()) PlayerObject::spawnCircle();
    }

    void spawnDualCircle() {
        if (!ghostEffect()) PlayerObject::spawnDualCircle();
    }

    void spawnPortalCircle(ccColor3B color, float startRadius) {
        if (!ghostEffect()) PlayerObject::spawnPortalCircle(color, startRadius);
    }

    void spawnScaleCircle() {
        if (!ghostEffect()) PlayerObject::spawnScaleCircle();
    }

    void playBumpEffect(int objectType, GameObject* object) {
        if (!ghostEffect()) PlayerObject::playBumpEffect(objectType, object);
    }

    void playSpawnEffect() {
        if (!ghostEffect()) PlayerObject::playSpawnEffect();
    }

    void createFadeOutDartStreak() {
        if (!ghostEffect()) PlayerObject::createFadeOutDartStreak();
    }

    void fadeOutStreak2(float duration) {
        if (!ghostEffect()) PlayerObject::fadeOutStreak2(duration);
    }

    void playDynamicSpiderRun() {
        if (!ghostEffect()) PlayerObject::playDynamicSpiderRun();
    }

    void animatePlatformerJump(float scale) {
        if (!ghostEffect()) PlayerObject::animatePlatformerJump(scale);
    }

    void runBallRotation(float speed) {
        if (!ghostEffect()) PlayerObject::runBallRotation(speed);
    }

    void runBallRotation2() {
        if (!ghostEffect()) PlayerObject::runBallRotation2();
    }

    void runNormalRotation(bool notNormalMode, float speed) {
        if (!ghostEffect()) PlayerObject::runNormalRotation(notNormalMode, speed);
    }

    void toggleGhostEffect(GhostType type) {
        if (!ghostEffect()) PlayerObject::toggleGhostEffect(type);
    }

    void activateStreak() {
        if (!ghostEffect()) PlayerObject::activateStreak();
    }

    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (buf.simulating) {
            return;
        }
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void ringJump(RingObject* ring, bool skipCheck) {
        if (buf.simulating) {
            if (!buf.smart || !isGhost(this)) {
                return;
            }
            if (buf.orbsUsed.contains(ring)) {
                return;
            }
            snapshotObject(ring);
            // v2.43: the game can still refuse the orb inside ringJump, so the orb only counts
            // as used if it actually changed the ghost's motion or gravity.
            double vy = m_yVelocity;
            bool upsideDown = m_isUpsideDown;
            bool dashing = m_isDashing;
            PlayerObject::ringJump(ring, skipCheck);
            if (m_yVelocity != vy || m_isUpsideDown != upsideDown || m_isDashing != dashing) {
                buf.orbsUsed.insert(ring);
                if (buf.orbUsedTick < 0) {
                    buf.orbUsedTick = buf.simTick;
                }
            }
            return;
        }
        PlayerObject::ringJump(ring, skipCheck);
    }

    // Dash orbs don't go through ringJump.
    void startDashing(DashRingObject* ring) {
        if (buf.simulating && isGhost(this)) {
            buf.orbsUsed.insert(ring);
            if (buf.orbUsedTick < 0) {
                buf.orbUsedTick = buf.simTick;
            }
        }
        PlayerObject::startDashing(ring);
    }
};

// v2.81: Geode keybinds (mod settings or Geode's keybinds popup; work without F keys too).
// Only act inside a level.
namespace {
    void keyAction(int action) {
        auto* layer = GameManager::get()->m_playLayer;
        // Open the menu from any scene; only gameplay-specific actions require a PlayLayer.
        if (action == 1) {
            if (layer && !layer->m_isPaused) {
                layer->pauseGame(true);
            }
            openMenu();
            return;
        }
        if (!layer) {
            return;
        }
        switch (action) {
            case 0:
                cycleStage();
                break;
            case 2:
                exportConsole();
                break;
            case 3:
                buf.panelOpen = !buf.panelOpen;
                refreshPanel();
                break;
            case 4:
                buf.showPath = !buf.showPath;
                studio.pathEnabled = buf.showPath;
                Mod::get()->setSavedValue("studio-path-enabled", studio.pathEnabled);
                if (buf.pathNode) {
                    buf.pathNode->clear();
                }
                say("path view {}", buf.showPath ? "on" : "off");
                if (buf.showPath) {
                    say("PATH KEY: coral=now, cyan=buffer, gold=fire, red=death, amber=memory risk");
                }
                break;
            case 6:
                studioToggle();
                break;
            case 7:
                studioPanic();
                break;
            case 5:
                memory.clear();
                pending.active = false;
                memoryDirty = true;
                saveMemory();
                say("memory for this level cleared");
                break;
            case 8:
                buf.waveDirectSpam = !buf.waveDirectSpam;
                resetPending();
                say("wave direct input {}", buf.waveDirectSpam ? "on (buffer bypassed)" : "off");
                break;
        }
    }

    void setupKeybinds() {
        char const* binds[] = { "bind-stage", "bind-menu", "bind-export", "bind-console", "bind-path", "bind-forget",
            "bind-toggle", "bind-panic", "bind-wave-direct" };
        for (int i = 0; i < static_cast<int>(std::size(binds)); ++i) {
            listenForKeybindSettingPresses(binds[i], [i](Keybind const&, bool down, bool repeat, double) {
                if (down && !repeat) {
                    keyAction(i);
                }
                return false;
            });
        }
    }

}

$on_mod(Loaded) {
    setupKeybinds();
}

// v2.79 custom TPS (from the v2.80 build): above 240 the level is stepped in 1/tps pieces.
// update() runs once per step and getModifiedDelta() hands the step through unrounded, so
// each call is exactly one physics step. (Below 240 the game's own step count would need
// patching, so 240 is the floor.) Runs at anything but 240 aren't legit completions.
static bool tpsDriving = false;
static double tpsCarry = 0.0;

class $modify(BufBaseGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        int tps = buf.tps;
        if (tps <= 240 || tpsDriving || !typeinfo_cast<PlayLayer*>(this)) {
            GJBaseGameLayer::update(dt);
            return;
        }
        double step = 1.0 / tps;
        tpsCarry += dt;
        int steps = static_cast<int>(tpsCarry / step);
        int cap = std::max(1, tps / 15);  // after a lag spike, catch up at most 1/15 s
        if (steps > cap) {
            steps = cap;
            tpsCarry = 0.0;
        }
        else {
            tpsCarry -= steps * step;
        }
        tpsDriving = true;
        for (int i = 0; i < steps; ++i) {
            GJBaseGameLayer::update(static_cast<float>(step));
        }
        tpsDriving = false;
    }

    double getModifiedDelta(float dt) {
        if (!tpsDriving) {
            return GJBaseGameLayer::getModifiedDelta(dt);
        }
        m_extraDelta = 0.0;
        if (m_resumeTimer > 0) {
            --m_resumeTimer;
            return 0.0;
        }
        return dt;
    }

    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::canBeActivatedByPlayer", HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::handleButton", HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::collisionCheckObjects", HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::playerTouchedRing", HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::flipGravity", HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::gameEventTriggered", HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::playerWillSwitchMode", HOOK_PRIORITY);
    }

    // During a simulation the ghost may use each object once; the real activation
    // state is snapshotted and restored afterwards so the real player is unaffected.
    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        if (buf.simulating) {
            if (!buf.smart || !isGhost(player) || !object) {
                return false;
            }
            // v2.16: an orb can be checked on every tick you're inside it. Before, the first
            // check already "spent" it, so the ghost could only click an orb on the very
            // first tick it touched it. Now an orb is only spent once the ghost used it.
            if (typeinfo_cast<RingObject*>(object)) {
                if (buf.orbsUsed.contains(object)) {
                    return false;
                }
                snapshotObject(object);
                return true;
            }
            if (buf.simUsed.contains(object)) {
                return false;
            }
            snapshotObject(object);
            return true;
        }
        return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
    }

    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int objectCount, float dt) {
        if (!buf.simulating) {
            // Levels with move / rotate / follow triggers: while the real player's collision check runs, the solids
            // and hazards close to you go on the watch list (a few at a time), so their movement is known before a
            // prediction needs it. This uses only the list the game already built, nothing is scanned.
            if ((buf.predictMovers || buf.avoidMovers) && levelScan.moving > 0 && objects) {
                static int feedCounter = 0;
                auto* playLayer = GameManager::get()->m_playLayer;
                if (++feedCounter >= 4 && playLayer && player == playLayer->m_player1) {
                    feedCounter = 0;
                    int added = 0;
                    for (int i = 0; i < objectCount && added < 160; ++i) {
                        auto* object = (*objects)[i];
                        if (!object) {
                            continue;
                        }
                        auto type = static_cast<unsigned>(object->m_objectType);
                        uint64_t bit = type < 64 ? (1ull << type) : 0;
                        if (!(COLLIDING_TYPES & bit)) {
                            continue;
                        }
                        auto [slot, isNew] = watched.try_emplace(object,
                            Watched { object->m_positionX, object->m_positionY, -MOVER_TICKS });
                        if (isNew) {
                            slot->second.rot = object->getRotation();
                            ++added;
                        }
                    }
                }
            }
            GJBaseGameLayer::collisionCheckObjects(player, objects, objectCount, dt);
            return;
        }
        static gd::vector<GameObject*> relevant;
        relevant.clear();
        if (static_cast<int>(relevant.capacity()) < objectCount) {
            relevant.reserve(objectCount);
        }

        // Hot loop: runs for every nearby object, every ghost tick. One type lookup per
        // object, no bounds-checked at(), and the ghost rect is only fetched if needed.
        uint64_t keep = buf.smart ? (COLLIDING_TYPES | INTERACT_TYPES) : COLLIDING_TYPES;
        bool haveRect = false;
        CCRect ghostRect;
        auto& list = *objects;
        for (int i = 0; i < objectCount; ++i) {
            auto* object = list[i];
            if (!object) {
                continue;
            }
            auto type = static_cast<unsigned>(object->m_objectType);
            uint64_t bit = type < 64 ? (1ull << type) : 0;
            if (keep & bit) {
                // v2.31: a hazard you're touching right now while alive is one the game doesn't
                // let kill you (a spike made to follow the player, noclip...). Seen in GRIEF:
                // a spike always sitting exactly on the player killed every wave prediction on
                // its first tick.
                // Decided on the first ticks and kept by object, so a spike that follows you
                // stays ignored even when "predict moving objects" moves it along.
                if (!buf.simDisabled.empty() && buf.simDisabled.contains(object)) {
                    continue;
                }
                if (buf.haveRealRect && (bit & HAZARD_TYPES)) {
                    if (!buf.harmless.empty() && buf.harmless.contains(object)) {
                        continue;
                    }
                    if (buf.simTick <= 1) {
                        auto const& r = object->getObjectRect();
                        if (r.intersectsRect(buf.realRect1) || r.intersectsRect(buf.realRect2)) {
                            buf.harmless.insert(object);
                            continue;
                        }
                    }
                }
                relevant.push_back(object);
                if (buf.trackMovers && (COLLIDING_TYPES & bit)) {
                    watchObject(object);
                }
                continue;
            }
            if (!buf.smart) {
                continue;
            }
            bool stop = (STOP_TYPES & bit) != 0;
            bool size = (SIZE_TYPES & bit) != 0;
            float speed = (stop || size) ? 0.f : speedPortalValue(object->m_objectID);
            if (!stop && !size && speed <= 0.f) {
                continue;
            }
            if (!haveRect) {
                ghostRect = player->getObjectRect();
                haveRect = true;
            }
            if (ghostRect.intersectsRect(object->getObjectRect())) {
                if (stop) {
                    // v2.79: a mode portal you're still inside when you click was already taken
                    // (you're in its mode now). It stopped every prediction at t0, so nothing right
                    // after a portal was predicted (Amethyst's first pink orb: the click was never
                    // moved onto the orb). Only portals ahead stop a prediction; teleport orbs
                    // still do, since they need a click.
                    if (buf.harmless.contains(object)) {
                        continue;
                    }
                    auto const& r = object->getObjectRect();
                    if (buf.haveStartRects && buf.simTick <= 1 && object->m_objectType != GameObjectType::TeleportOrb &&
                        (r.intersectsRect(buf.realRect1) || r.intersectsRect(buf.realRect2))) {
                        buf.harmless.insert(object);
                        continue;
                    }
                    // v2.79: mode portals ahead are taken by the ghost (once per run)
                    if (buf.portalPredict && buf.fullState && (MODE_TYPES & bit)) {
                        setGhostMode(player, object->m_objectType);
                        buf.harmless.insert(object);
                        continue;
                    }
                    buf.ghostHitPortal = true;
                }
                else if (size) {
                    bool mini = object->m_objectType == GameObjectType::MiniSizePortal;
                    if ((player->m_vehicleSize < 1.f) != mini) {
                        player->togglePlayerScale(mini, true);
                    }
                }
                else {
                    player->m_playerSpeed = speed;
                }
            }
        }
        if (buf.predictMovers && !movers.empty()) {
            // A mover the ghost expects to reach you can sit in a different section of the level than the ones
            // the game handed us (the game only re-sections objects it moved itself), so it is added by hand.
            float ghostX = player->getPosition().x;
            for (auto const& mover : movers) {
                auto* object = mover.object;
                if (!object || std::abs(object->m_positionX - ghostX) > 420.0) {
                    continue;
                }
                auto type = static_cast<unsigned>(object->m_objectType);
                uint64_t bit = type < 64 ? (1ull << type) : 0;
                if (!(COLLIDING_TYPES & bit)) {
                    continue;
                }
                if (!buf.simDisabled.empty() && buf.simDisabled.contains(object)) {
                    continue;
                }
                if (!buf.harmless.empty() && buf.harmless.contains(object)) {
                    continue;
                }
                if (std::find(relevant.begin(), relevant.end(), object) != relevant.end()) {
                    continue;
                }
                relevant.push_back(object);
                if (buf.trackMovers) {
                    watchObject(object);
                }
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, &relevant, static_cast<int>(relevant.size()), dt);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!buf.simulating && button == 1 && !buf.injecting) {
            auto* p1 = this->m_player1;
            auto* levelLayer = GameManager::get()->m_playLayer;
            bool ownP2 = !isPlayer1 && p2OwnBuffer(levelLayer);
            if (!ownP2) {
                if (down || (p1 && (p1->m_isDart || p1->m_isRobot))) {
                    finishDrift("");
                }
                recordRealInput(down, p1);
                funOnClick(down);
            }
        }
        if (!buf.simulating && button == 1 && !buf.injecting) {
            // v2.21 performance log: what this click cost, measured around the whole decision.
            buf.simRuns = 0;
            buf.simTicks = 0;
            auto start = std::chrono::steady_clock::now();
            bool held = intercept(this, down, isPlayer1);
            sweepGhostLayer();
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            buf.lastPerfMs = ms;
            buf.lastPerfRuns = buf.simRuns;
            buf.lastPerfTicks = buf.simTicks;
            if (buf.panelOpen) {
                buf.panelDirty = true;
            }
            if (buf.perfLog && buf.simRuns > 0) {
                say("perf: {:.2f} ms, {} runs, {} ghost ticks ({:.1f} us/tick)", ms, buf.simRuns, buf.simTicks,
                    ms * 1000.0 / static_cast<double>(std::max(1L, buf.simTicks)));
            }
            if (held) {
                return;
            }
        }
        if (!buf.simulating && button == 1) {
            benchInput(down);  // v2.75: what the game really gets (your clicks and delayed ones)
            if (isPlayer1 && !g_p2.active) {
                buf.lastAppliedEdgeTick = tickCount;   // the even-zigzag logic measures phase lengths from here
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (buf.simulating) {
            if (!buf.smart || !isGhost(player)) {
                return;
            }
            snapshotObject(ring);
            buf.touchedOrb = true;
            if (buf.firstOrbTick < 0) {
                buf.firstOrbTick = buf.simTick;
            }
            buf.lastOrbTick = buf.simTick;
            if (buf.orbUsedTick >= 0 && !buf.orbsUsed.contains(ring)) {
                buf.laterOrbTouched = true;
            }
        }
        GJBaseGameLayer::playerTouchedRing(player, ring);

        // v2.2 calculation mode: assume you click every orb you touch, on time. Without
        // this the ghost (which only has the one click being tested) misses the next orb in a
        // chain, falls, and the mod thinks a perfectly good click is a death.
        bool autoOrb = buf.orbClick || (buf.orbAfterFirst && buf.orbUsedTick >= 0 && !buf.orbsUsed.contains(ring));
        if (buf.simulating && autoOrb) {
            bool first = player == buf.ghost1;
            bool& held = first ? buf.held1 : buf.held2;
            bool& autoClick = first ? buf.auto1 : buf.auto2;
            int& autoAt = first ? buf.autoAt1 : buf.autoAt2;
            int customOrbDelay = studioOrbDelay(ring->m_objectType);
            int orbDelay = customOrbDelay > 0 ? customOrbDelay : buf.orbClickDelay;
            if (orbDelay > 0) {
                if (!held && autoAt == -1) {
                    autoAt = buf.simTick + orbDelay;  // per-orb timing or the automatic late-click probe
                }
            }
            else if (!held && !autoClick) {
                player->pushButton(PlayerButton::Jump);
                autoClick = true;
            }
        }
    }

    // Gravity portals / blue pads / blue orbs: flip only the ghost, no layer effects.
    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        if (buf.simulating) {
            if (buf.smart && isGhost(player)) {
                player->flipGravity(flip, true);
            }
            return;
        }
        GJBaseGameLayer::flipGravity(player, flip, noEffects);
    }

    // Never let ghost activity fire event triggers in the real level.
    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (buf.simulating) {
            return;
        }
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }

    // v2.74: nothing the hidden ghost touches during a prediction may change the real level.
    // Toggle orbs, orbs / pads / collectibles with groups, item and counter triggers could
    // otherwise toggle or spawn groups and change item counters for real (once per
    // prediction run, many times a click), which broke triggers in some levels.
    void toggleGroupTriggered(int group, bool activate, gd::vector<int> const& remapKeys, int triggerID, int controlID) {
        if (buf.simulating) {
            // v2.75: a toggle orb that switches a group off: the ghost stops colliding with
            // that group for the rest of this prediction (the real objects aren't touched)
            if (!activate) {
                if (auto* objects = this->getGroup(group)) {
                    for (auto* object : CCArrayExt<GameObject*>(objects)) {
                        buf.simDisabled.insert(object);
                    }
                }
            }
            return;
        }
        GJBaseGameLayer::toggleGroupTriggered(group, activate, remapKeys, triggerID, controlID);
    }

    void spawnGroup(int group, bool ordered, double delay, gd::vector<int> const& remapKeys, int triggerID, int controlID) {
        if (buf.simulating) {
            return;
        }
        GJBaseGameLayer::spawnGroup(group, ordered, delay, remapKeys, triggerID, controlID);
    }

    void toggleGroup(int id, bool activate) {
        if (buf.simulating) {
            return;
        }
        GJBaseGameLayer::toggleGroup(id, activate);
    }

    void pickupItem(EffectGameObject* object) {
        if (buf.simulating) {
            return;
        }
        GJBaseGameLayer::pickupItem(object);
    }

    void updateCounters(int itemId, int value) {
        if (buf.simulating) {
            return;
        }
        GJBaseGameLayer::updateCounters(itemId, value);
    }

    void activateItemEditTrigger(ItemTriggerGameObject* object) {
        if (buf.simulating) {
            return;
        }
        GJBaseGameLayer::activateItemEditTrigger(object);
    }

    void activateItemCompareTrigger(ItemTriggerGameObject* object, gd::vector<int> const& remapKeys) {
        if (buf.simulating) {
            return;
        }
        GJBaseGameLayer::activateItemCompareTrigger(object, remapKeys);
    }

    // Safety net in case a mode portal slips through: end prediction, change nothing.
    void playerWillSwitchMode(PlayerObject* player, GameObject* object) {
        if (buf.simulating) {
            buf.ghostHitPortal = true;
            return;
        }
        GJBaseGameLayer::playerWillSwitchMode(player, object);
    }
};
