// Synca engine: the ghost simulation, path drawing and the route chooser.
// (split out of main.cpp; shared types and declarations are in engine.hpp)
#include "engine.hpp"

namespace synca {


    // Returns ticks survived after the input (horizon = fully safe).
    // releaseAfter: ticks after the input at which the ghost does the opposite (-1 = never).
    // stopAt: stop as soon as this many ticks were survived (the caller already knows
    // that's good enough), instead of always running the full horizon.
    // v1.12 ghost state audit (explain mode, at most once a second): compares the ghost with the real player right after the
    // copy. A difference here is why a prediction can disagree with what really happens.
    void ghostAudit(PlayerObject* real, PlayerObject* ghost) {
        static int lastAudit = -1000000;
        if (!real || !ghost || tickCount - lastAudit < st(240)) {
            return;
        }
        lastAudit = tickCount;
        std::string diff;
        auto check = [&](char const* name, bool a, bool b) { if (a != b) diff += fmt::format(" {}({}->{})", name, a, b); };
        check("ship", real->m_isShip, ghost->m_isShip);
        check("ball", real->m_isBall, ghost->m_isBall);
        check("ufo", real->m_isBird, ghost->m_isBird);
        check("wave", real->m_isDart, ghost->m_isDart);
        check("robot", real->m_isRobot, ghost->m_isRobot);
        check("spider", real->m_isSpider, ghost->m_isSpider);
        check("swing", real->m_isSwing, ghost->m_isSwing);
        check("upside-down", real->m_isUpsideDown, ghost->m_isUpsideDown);
        if (std::abs(real->m_vehicleSize - ghost->m_vehicleSize) > 0.01f) diff += " size";
        if (std::abs(real->m_playerSpeed - ghost->m_playerSpeed) > 0.01f) diff += " speed";
        if (std::abs(real->m_yVelocity - ghost->m_yVelocity) > 0.05) diff += fmt::format(" yvel({:.2f}->{:.2f})", real->m_yVelocity, ghost->m_yVelocity);
        if (ccpDistance(real->getPosition(), ghost->getPosition()) > 0.5f) diff += " position";
        say("ghost audit: {}", diff.empty() ? std::string("the ghost matches the real player") : "MISMATCH:" + diff);
    }

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
        if (buf.explain) {
            ghostAudit(p1, buf.ghost1);
        }
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
        int releaseAfter, bool* died, bool traceThroughHits, int* collisionAt) {
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
    void drawPath(std::vector<CCPoint> const& path, int delay, ccColor4F color, int collisionAt, CCDrawNode* node) {
        if (!node) node = buf.pathNode;
        if (!node) return;
        // the draw node blends PREMULTIPLIED colours, so every see-through colour has its rgb multiplied by the alpha
        for (size_t i = 1; i < path.size(); ++i) {
            bool pastHit = collisionAt >= 0 && static_cast<int>(i) > collisionAt;
            auto segmentColor = color;
            if (pastHit) segmentColor.a *= 0.32f;
            segmentColor.a *= 1.f - 0.55f * static_cast<float>(i) / static_cast<float>(path.size());   // fades toward the far end
            auto glowColor = segmentColor;
            glowColor.a *= 0.12f;
            node->drawSegment(path[i - 1], path[i], 2.6f, neonPremul(glowColor));
            node->drawSegment(path[i - 1], path[i], 1.0f, neonPremul(segmentColor));
            if (i % 16 == 0) {
                neonDisc(node, path[i], 1.5f, neonPremul({ color.r, color.g, color.b, pastHit ? color.a * 0.36f : color.a }), 8);
            }
        }
        if (delay > 0 && delay <= static_cast<int>(path.size())) {
            neonDisc(node, path[delay - 1], 3.2f, { 1.f, 0.82f, 0.18f, 1.f }, 12);
        }
        if (collisionAt >= 0 && collisionAt < static_cast<int>(path.size())) {
            neonDisc(node, path[collisionAt], 3.6f, { 1.f, 0.1f, 0.24f, 1.f }, 12);
        }
        if (!path.empty()) {
            neonDisc(node, path.back(), 2.2f, neonPremul({ color.r, color.g, color.b, 0.95f }), 10);
        }
    }


    void ensureHazards(PlayLayer* layer) {
        if (hazardsScanned) {
            return;
        }
        hazardsScanned = true;
        hazardPts.clear();
        solidPts.clear();
        if (!layer || !layer->m_objects) {
            return;
        }
        for (auto* node : CCArrayExt<GameObject*>(layer->m_objects)) {
            if (!node) continue;
            if (node->m_objectType == GameObjectType::Hazard) {
                hazardPts.push_back(node->getPosition());
            }
            else if (node->m_objectType == GameObjectType::Solid || node->m_objectType == GameObjectType::Slope) {
                solidPts.push_back(node->getPosition());
            }
        }
        std::sort(hazardPts.begin(), hazardPts.end(), [](CCPoint const& a, CCPoint const& b) { return a.x < b.x; });
        std::sort(solidPts.begin(), solidPts.end(), [](CCPoint const& a, CCPoint const& b) { return a.x < b.x; });
    }


    float routeClearance(std::vector<CCPoint> const& path) {
        float best = 1000.f;
        if (hazardPts.empty() && solidPts.empty()) {
            return best;
        }
        for (size_t i = 0; i < path.size(); i += 3) {
            auto it = std::lower_bound(hazardPts.begin(), hazardPts.end(), path[i].x - 70.f,
                [](CCPoint const& a, float x) { return a.x < x; });
            for (; it != hazardPts.end() && it->x < path[i].x + 70.f; ++it) {
                best = std::min(best, std::hypot(it->x - path[i].x, it->y - path[i].y));
            }
            auto wall = std::lower_bound(solidPts.begin(), solidPts.end(), path[i].x - 70.f,
                [](CCPoint const& a, float x) { return a.x < x; });
            for (; wall != solidPts.end() && wall->x < path[i].x + 70.f; ++wall) {
                best = std::min(best, std::hypot(wall->x - path[i].x, wall->y - path[i].y) - 15.f);
            }
        }
        return best;
    }


    RouteChoice chooseRoute(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, bool down, int horizon, int releaseAfter,
        int maxDelay, double limitMs) {
        ensureHazards(layer);
        RouteChoice best;
        int bestScore = -2;
        auto started = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
        auto consider = [&](bool idle, int delay) {
            RouteChoice c;
            c.idle = idle;
            c.delay = delay;
            buf.skipPress = idle;
            c.path = recordRun(layer, p1, p2, dual, delay, down, horizon, idle ? -1 : releaseAfter, nullptr, true, &c.hit);
            buf.skipPress = false;
            c.survives = c.hit < 0;
            if (c.survives) {
                c.clear = routeClearance(c.path);
            }
            int score = c.survives ? INT_MAX : c.hit;
            // among routes that survive, the one with clearly more room wins; with similar room the earlier input wins
            bool better = score > bestScore;
            if (!better && c.survives && best.survives) {
                better = c.clear > best.clear + 5.f || (c.clear >= best.clear - 5.f && c.delay < best.delay);
            }
            if (better) {
                bestScore = score;
                best = std::move(c);
            }
            return score == INT_MAX;
        };
        // doing nothing is kept when it survives with room to spare; a close shave looks at the clicks too
        if (consider(true, 0) && best.clear >= 24.f) {
            return best;
        }
        // every few ticks over the whole window (then the bisect below finds the exact smallest delay): a 120 tick window is searched to its end
        std::vector<int> tried;
        for (int t = 0; t <= 120; t += (t < 8 ? 1 : 2)) tried.push_back(t);   // every tick at first, then every second tick
        bool oneRoute = limitMs <= 2.5;   // a tight time limit (wave / ball path view): the first safe route is enough, no comparing
        int lastFailDelay = 0;
        int survivors = 0;
        for (int ticks : tried) {
            int delay = st(ticks);
            if (ticks > 0 && delay > maxDelay) {
                break;
            }
            if (ticks > 0 && elapsed() > limitMs) {
                break;
            }
            if (consider(false, delay)) {
                if (++survivors == 1) {
                    // bisect: the smallest click delay between the last one that failed and this one
                    int lo = lastFailDelay + 1;
                    int hi = delay;
                    while (lo < hi && elapsed() < limitMs * 2.0) {
                        int mid = (lo + hi) / 2;
                        if (consider(false, mid)) hi = mid;
                        else lo = mid + 1;
                    }
                }
                if (survivors >= (oneRoute ? 1 : 3)) {
                    break;   // enough safe routes to compare their room
                }
            }
            else {
                lastFailDelay = delay;
            }
        }
        if (best.hit >= 0 && best.hit + 1 < static_cast<int>(best.path.size())) {
            best.path.resize(static_cast<size_t>(best.hit) + 1);   // nothing past the crash
        }
        return best;
    }


    // v1.12 ghost vs real: the last 2 seconds the real player really flew. Green = right on the ghost's prediction, yellow = a little off,
    // red = the prediction was wrong there, white = nothing to compare with.
    void drawRealTrail() {
        if (!buf.pathNode || realTrail.size() < 2) {
            return;
        }
        for (size_t i = 1; i < realTrail.size(); ++i) {
            float err = realTrail[i].err;
            ccColor4F color = err < 0.f ? ccColor4F { 1.f, 1.f, 1.f, 0.30f }
                : err < 1.5f ? ccColor4F { 0.3f, 1.f, 0.5f, 0.75f }
                : err < 4.f ? ccColor4F { 1.f, 0.9f, 0.3f, 0.8f }
                : ccColor4F { 1.f, 0.3f, 0.3f, 0.9f };
            buf.pathNode->drawSegment(realTrail[i - 1].pos, realTrail[i].pos, 0.9f, neonPremul(color));
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
        {
            int windowTicks = st(combinedTicks(p1, p2));
            horizon = std::min(horizon, std::max(st(studio.pathHorizon), std::min(st(320), windowTicks + st(110))));
        }
        buf.pathNode->clear();
        drawRealTrail();
        // Keep the selected input's route visible briefly, then hand back to the
        // live forecast. Forty-five ticks froze the path for ~188 ms at 240 TPS,
        // long enough to hide several USKC spam reversals.
        buf.pathHoldUntil = tickCount + st(3);
        ccColor4F coral { 1.f, 0.38f, 0.28f, 0.92f };
        ccColor4F cyan { 0.12f, 0.82f, 1.f, 0.96f };
        // v5.12: only the route the input will really take (the buffered click if one was chosen, else the input
        // as it is), cut at the crash. No second "alternative" line.
        std::vector<CCPoint> now;
        bool wantRoute = studio.pathShowIdle || studio.pathShowClick;
        auto trimAtHit = [](std::vector<CCPoint>& path, int hit) {
            if (hit >= 0 && hit + 1 < static_cast<int>(path.size())) path.resize(static_cast<size_t>(hit) + 1);
        };
        if (wantRoute && chosen > 0) {
            int delayedHit = -1;
            auto delayed = recordRun(layer, p1, p2, dual, chosen, down, horizon, releaseAfter, nullptr, true, &delayedHit);
            trimAtHit(delayed, delayedHit);
            drawPath(delayed, chosen, cyan, delayedHit);
            if (buf.keepNode) {
                buf.keepNode->clear();
                drawPath(delayed, chosen, ccColor4F { 0.12f, 0.82f, 1.f, 0.55f }, delayedHit, buf.keepNode);
                buf.keepTick = tickCount;
            }
            now = std::move(delayed);
        }
        else if (wantRoute && !slowDecision) {
            int nowHit = -1;
            now = recordRun(layer, p1, p2, dual, 0, down, horizon, releaseAfter, nullptr, true, &nowHit);
            trimAtHit(now, nowHit);
            drawPath(now, 0, coral, nowHit);
        }
        // v2.13: spots on this path where the memory has seen you die a lot.
        if (studio.pathShowIdle && buf.learn && !memory.empty()) {
            int last = INT_MIN;
            for (auto const& point : now) {
                int slice = static_cast<int>(std::floor(point.x / SLICE));
                if (slice != last && memoryRisky(slice)) {
                    neonDisc(buf.pathNode, point, 3.2f, neonPremul({ 1.f, 0.6f, 0.1f, 0.95f }), 12);
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
        if (fun.verdictTick != tickCount) {   // (the no-save and save paths name their own verdict first)
            fun.verdict = delay > 0 ? fmt::format("saved +{}t", static_cast<int>(std::lround(delay / stepScale()))) : std::string("passed");
            fun.verdictTick = tickCount;
        }
        fun.lastMs = clickMs();
        if (clickMs() > std::max(20.0, buf.userBudget * 3.0)) {
            if (++watchdogStrikes >= 3 && tickCount >= watchdogUntil) {
                watchdogUntil = tickCount + st(2400);
                watchdogStrikes = 0;
                say("watchdog: clicks are taking {:.0f} ms, so the prediction budget is cut to 3 ms for 10 seconds", clickMs());
            }
        }
        else {
            watchdogStrikes = 0;
        }
        if (clickMs() > buf.budgetMs * 1.5) {
            finishDrift("");  // no time left for the debug runs this click
            return;
        }
        startDrift(layer, p1, p2, dual, delay, down, horizon, releaseAfter);
        showPaths(layer, p1, p2, dual, delay, down, horizon, releaseAfter);
    }


    // Full wave look-ahead from the setting (mini = 15 less).
    int fullWaveHorizon(bool mini, bool alternate) {
        int wh = alternate ? std::clamp(studio.altWaveHorizon, 20, 480) : std::clamp(buf.waveHorizon, 20, 240);
        return st(mini ? std::max(20, wh - 15) : wh);
    }

}  // namespace synca
