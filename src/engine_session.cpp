// Synca engine: run check, live path, statistics, profiles, heat map.
// (split out of main.cpp; shared types and declarations are in engine.hpp)
#include "engine.hpp"

namespace synca {


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
        // v1.13: the path view was allowed to spend half of every refresh interval (12 ms of every 24) on simulations; in wave and
        // ball, where every simulation is expensive, that was the lag. It now backs off to 8x what the last refresh cost (never less
        // than the setting), and the route search gets a few milliseconds at most.
        static double lastRouteMs = 0.0;
        double wanted = static_cast<double>(studio.pathRefreshMs) / perfGuardScale();
        wanted = std::max(wanted, lastRouteMs * 8.0);
        auto refreshEvery = std::chrono::milliseconds(std::clamp(static_cast<int>(wanted), 8, 400));
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
        // v1.15: the path is as long as the delay window needs: a click held for up to 120 ticks, then the whole jump
        int windowTicks = st(combinedTicks(p1, p2));
        if (!wave && !flyer) horizon = std::max(horizon, std::min(st(320), windowTicks + st(110)));
        horizon = std::min(horizon, std::max(st(studio.pathHorizon), std::min(st(320), windowTicks + st(110))));
        bool down = !buf.appliedInput;
        int releaseAfter = down && !wave && !ship ? holdEstimate() : -1;

        int altDown = predictAlt ? buf.altDownEvery : -1;
        int altUp = predictAlt ? buf.altUpEvery : -1;
        int altPeriod = predictAlt ? (altDown + altUp + 1) / 2 : -1;
        auto downForecast = predictAlt ? alternatePhaseForecast(true) : AlternatePhaseForecast{};
        auto upForecast = predictAlt ? alternatePhaseForecast(false) : AlternatePhaseForecast{};
        AlternatePredictionScope alternatePathScope(altPeriod, altDown, altUp, downForecast, upForecast);

        buf.pathNode->clear();
        drawRealTrail();
        if (studio.pathShowIdle || studio.pathShowClick) {
            // v5.12: one route, the one the level wants (see chooseRoute), not "idle" and "click" side by side
            int maxDelay = st(combinedTicks(p1, p2));
            auto routeStart = std::chrono::steady_clock::now();
            bool heavy = wave || p1->m_isBall || (p2 && p2->m_isBall);
            auto route = chooseRoute(layer, p1, p2, dual, down, horizon, releaseAfter, maxDelay, heavy ? 2.5 : (maxDelay > st(32) ? 7.0 : 3.5));
            lastRouteMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - routeStart).count();
            // v1.8: the route is yellow. It was picked by scanning ahead for death objects, so it keeps the most room it
            // can; the death objects it passes are marked (pale yellow = room, orange = a close shave).
            ccColor4F yellow { 1.f, 0.86f, 0.22f, 0.95f };
            drawPath(route.path, route.idle ? 0 : route.delay, yellow, route.hit);
            if (!route.idle && route.delay == 0 && !route.path.empty()) {
                neonDisc(buf.pathNode, p1->getPosition(), 3.4f, { 1.f, 1.f, 1.f, 1.f }, 12);   // click right now
            }
            // v1.13: mode / speed / size portals the route passes (cyan diamonds), so a change of mode ahead is visible on the path
            if (!route.path.empty() && levelScan.done) {
                float x0 = route.path.front().x;
                float x1 = route.path.back().x;
                for (auto const& [portalX, portalId] : levelScan.portals) {
                    if (portalX < x0 || portalX > x1) continue;
                    CCPoint at = route.path.back();
                    for (auto const& point : route.path) {
                        if (point.x >= portalX) { at = point; break; }
                    }
                    neonDisc(buf.pathNode, { portalX, at.y }, 4.2f, neonPremul({ 0.3f, 0.9f, 1.f, 0.95f }), 4);
                }
            }
            if (!route.path.empty() && !hazardPts.empty()) {
                float x0 = route.path.front().x - 20.f;
                float x1 = route.path.back().x + 20.f;
                auto it = std::lower_bound(hazardPts.begin(), hazardPts.end(), x0, [](CCPoint const& a, float x) { return a.x < x; });
                int drawn = 0;
                for (; it != hazardPts.end() && it->x <= x1 && drawn < 60; ++it) {
                    float nearest = 1000.f;
                    for (size_t i = 0; i < route.path.size(); i += 3) {
                        nearest = std::min(nearest, std::hypot(it->x - route.path[i].x, it->y - route.path[i].y));
                    }
                    if (nearest > 90.f) continue;
                    bool close = nearest < 26.f;
                    neonDisc(buf.pathNode, *it, close ? 3.2f : 2.2f,
                        neonPremul(close ? ccColor4F { 1.f, 0.5f, 0.1f, 0.95f } : ccColor4F { 1.f, 0.95f, 0.55f, 0.6f }), 10);
                    ++drawn;
                }
            }
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


    void noteAlternateResult(AlternateResult result, int delay) {
        altRecent.push_back({ tickCount, result, delay });
        while (altRecent.size() > 8) altRecent.pop_front();
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

    // v1.8: when you die within a second of an alternate-assist decision, write down what the assist did on its last
    // edges (console + alt-deaths.txt in the save folder), so a spot like Mesmerized can be tuned from facts.
    void altDeathReport(float percent) {
        if (!studio.altOn || altRecent.empty() || tickCount - altRecent.back().tick > st(240)) {
            return;
        }
        std::string text;
        for (auto const& edge : altRecent) {
            if (tickCount - edge.tick > st(480)) continue;
            char const* name = edge.result == AlternateResult::Adjusted ? "moved" : edge.result == AlternateResult::NoGain ? "no gain"
                : edge.result == AlternateResult::MarginRejected ? "margin rejected" : "portal pass";
            text += fmt::format(" [{} {}t ago{}]", name, static_cast<int>(std::lround((tickCount - edge.tick) / stepScale())),
                edge.result == AlternateResult::Adjusted ? fmt::format(" +{}t", edge.delay) : std::string());
        }
        say("alt assist death at {:.0f}%:{}", percent, text);
        std::error_code ec;
        std::filesystem::create_directories(Mod::get()->getSaveDir(), ec);
        std::ofstream(Mod::get()->getSaveDir() / "alt-deaths.txt", std::ios::app)
            << memoryKey << " " << fmt::format("{:.0f}%", percent) << text << '\n';
        altRecent.clear();
    }



    void noteSave(PlayLayer* layer, int ticks) {
        int t = static_cast<int>(std::lround(ticks / stepScale()));
        int pct = std::clamp(static_cast<int>(layer->getCurrentPercent()), 0, 100);
        for (auto* s : { &attemptStats, &sessionStats }) {
            ++s->saves;
            s->earlySum += t;
            ++s->savesAt[pct];
        }
        if (studio.funAny) {
            ++fun.saveCount;
            ++fun.attemptSaves;
            fun.earlySum += t;
            fun.verdict = fmt::format("saved +{}t", t);
            fun.verdictTick = tickCount;
            fun.savePcts.push_back(pct);
            while (fun.savePcts.size() > 5) fun.savePcts.pop_front();
            if (studio.fun[16] && !buf.train) {
                funFlash(fmt::format("BUFFERED +{}t", t));
            }
        }
        // v2.56: a ding for every real save (training mode shows its hint instead)
        if (buf.saveDing && !buf.train) {
            FMODAudioEngine::sharedEngine()->playEffect("counter003.ogg");
        }
    }


    // v1.12 death snapshot: one line per death in death-log.txt (save folder): where, which mode, what the assist did with the last
    // click and how good the forecast was. Reproducible facts instead of "it does not work on this level".
    void deathSnapshot(PlayLayer* layer, float percent) {
        auto* p1 = layer ? layer->m_player1 : nullptr;
        if (!p1) {
            return;
        }
        std::string last = tickCount - fun.verdictTick < st(480) ? fun.verdict : std::string("no recent click");
        std::string line = fmt::format("{} {:.0f}% | {} {}{} | stage {} | last click: {}", memoryKey.empty() ? std::string("?") : memoryKey, percent,
            scanModeName(studioModeOf(p1)), scanSpeedName(p1->m_playerSpeed), p1->m_isUpsideDown ? " upside-down" : "", stageShortText(), last);
        if (studio.altOn) {
            line += " | " + forecastText();
        }
        std::error_code ec;
        std::filesystem::create_directories(Mod::get()->getSaveDir(), ec);
        std::ofstream(Mod::get()->getSaveDir() / "death-log.txt", std::ios::app) << line << "\n";
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
        v["setup"] = studioPresetSnapshot();
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
        if (!globals.setup.empty()) {
            studioPresetApply(globals.setup);
            studioReadSettings();
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
        globals.setup = studioPresetSnapshot();
        backup["setup"] = globals.setup;
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
        if (v.contains("setup")) {
            studioPresetApply(v["setup"].asString().unwrapOr(""));
            studioReadSettings();
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
        if (v.contains("setup")) {
            studioPresetApply(v["setup"].asString().unwrapOr(""));
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

}  // namespace synca
