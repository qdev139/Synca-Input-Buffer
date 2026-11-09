// Synca engine: core engine: settings, log, ghost handling, input slots, moving-object watch.
// (split out of main.cpp; shared types and declarations are in engine.hpp)
#include "engine.hpp"

namespace synca {


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
        if (count > 0) {
            int error = sample - estimate;
            altForecast.n = std::min(altForecast.n + 1, 400);
            if (altForecast.n >= 400) { altForecast.absSum = altForecast.absSum * 3 / 4; altForecast.sum = altForecast.sum * 3 / 4; }   // old samples fade
            altForecast.absSum += std::abs(error);
            altForecast.sum += error;
            altForecast.last = error;
        }
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
        if (tickCount < watchdogUntil) {
            budget = std::min(budget, 3.0);
        }
        buf.budgetMs = budget;
    }


    // "FORECAST 1.2t late (bias +0.3t, 40 edges)": how the alternate assist's rhythm forecast compares with what you really did
    std::string forecastText() {
        if (altForecast.n < 6) return "FORECAST -";
        double scale = std::max(0.25f, stepScale());
        double mean = static_cast<double>(altForecast.absSum) / altForecast.n / scale;
        double bias = static_cast<double>(altForecast.sum) / altForecast.n / scale;
        return fmt::format("FORECAST off {:.1f}t (bias {:+.1f}t, {} edges)", mean, bias, altForecast.n);
    }

    void applyStage() {
        auto const& s = STAGES[buf.stage];
        buf.enabled = s.enabled;
        studioReadSettings();
        // v1.10: with "My gamemode delays on every stage" (default) the per-gamemode delays are used on every stage, not only on the
        // Custom one. A preset stage such as 8F used to ignore the sliders: everything stayed at 32 ticks however high they were set.
        studio.perMode = s.enabled && (s.custom || studio.stageBypass);
        if (studio.perMode) {
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
        if (!studio.progressStageBands || !layer || layer->m_isPlatformer || layer->m_levelLength <= 0.f || bandsPausedByHand) {
            return;
        }
        int percent = std::clamp(static_cast<int>(layer->getCurrentPercent()), 0, 100);
        int desired = studio.progressBaseStage;
        int activeBand = -1;
        // Every band can use any stage (fixed delays, PER MODE, or OFF). The last matching band wins.
        for (int i = 0; i < PROGRESS_BANDS; ++i) {
            if (studio.bandStart[i] < studio.bandEnd[i] && percent >= studio.bandStart[i] && percent < studio.bandEnd[i]) {
                desired = studio.bandStage[i];
                activeBand = i;
            }
        }
        // v1.13: the stage only follows the bands when you ENTER or LEAVE one. It used to be forced every step, so inside a band any
        // stage you chose by hand (menu, pause menu, key) snapped straight back to the band'"'"'s stage - the "stuck at 0.5f" bug.
        static int lastBand = -2;
        if (activeBand == lastBand) {
            return;
        }
        lastBand = activeBand;
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


    // the short stage name: OFF, PER MODE or 0.5F / 1F / 2F ... (pause menu readout)
    std::string stageShortText() {
        if (!buf.enabled) return "OFF";
        if (STAGES[buf.stage].custom) return "PER MODE";
        return fmt::format("{:g}F", STAGES[buf.stage].ticksAll * TICK);
    }

    std::string stageText() {
        if (!buf.enabled) {
            return "Buffer: OFF";
        }
        if (studio.perMode && !STAGES[buf.stage].custom) {
            return fmt::format("Buffer {:g}f stage, using your gamemode delays: cube {:g}f / wave {:g}f{}", STAGES[buf.stage].ticksAll * TICK,
                studio.modeTicks[SM_CUBE] * TICK, studio.modeTicks[SM_WAVE] * TICK, studio.altOn ? " / ALT" : "");
        }
        if (STAGES[buf.stage].custom) {
            return fmt::format("Buffer (per mode): cube {:g}f / wave {:g}f{}{}", studio.modeTicks[SM_CUBE] * TICK,
                studio.modeTicks[SM_WAVE] * TICK, studio.altOn ? " / ALT" : "", buf.train ? " [training]" : "");
        }
        return fmt::format("Buffer{}: {:g}f / wave {:g}f{}", STAGES[buf.stage].custom ? " (custom)" : "", buf.ticksAll * TICK,
            buf.ticksWave * TICK, buf.train ? " [training]" : "");
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
            if (prune && std::abs(object->m_positionX - px) > 500.0) {
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


    void collectMovers() {
        movers.clear();
        if (!buf.predictMovers) {
            return;
        }
        // v1.9: only objects close to you matter (predictions reach ~300 units) and only the nearest few are placed on every
        // simulated step. A level with hundreds of moving objects made every prediction place all of them: that was the lag.
        double px = 0.0;
        double py = 0.0;
        if (auto* layer = GameManager::get()->m_playLayer; layer && layer->m_player1) {
            px = layer->m_player1->getPosition().x;
            py = layer->m_player1->getPosition().y;
        }
        for (auto* object : movingNow) {
            auto it = watched.find(object);
            if (it == watched.end()) {
                continue;
            }
            if (std::abs(object->m_positionX - px) > 360.0 || std::abs(object->m_positionY - py) > 240.0) {
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
        constexpr size_t MAX_MOVERS = 40;
        if (movers.size() > MAX_MOVERS) {
            std::nth_element(movers.begin(), movers.begin() + MAX_MOVERS, movers.end(), [px, py](Mover const& a, Mover const& b) {
                return std::hypot(a.x - px, a.y - py) < std::hypot(b.x - px, b.y - py);
            });
            movers.resize(MAX_MOVERS);
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
        // v1.12: the real trail, with how far each point was from the ghost's prediction (the path view draws it)
        if (buf.showPath) {
            auto here = real->getPosition();
            if (!realTrail.empty() && here.x < realTrail.back().pos.x - 30.f) {
                realTrail.clear();   // a new attempt
            }
            float err = -1.f;
            if (drift.active && drift.index >= 0 && drift.index < static_cast<int>(drift.path.size())) {
                err = ccpDistance(here, drift.path[drift.index]);
            }
            realTrail.push_back({ here, err });
            while (realTrail.size() > 120) realTrail.pop_front();
        }
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

}  // namespace synca
