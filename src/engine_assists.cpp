// Synca engine: auto release / click, ship guard, frame spam, HUD updates.
// (split out of main.cpp; shared types and declarations are in engine.hpp)
#include "engine.hpp"

namespace synca {


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

        // v1.9: this runs on every physics step and had no time limit. While you alternate fast (10 CPS) the wave is always
        // near a crash, so it planned (dozens of simulations) over and over and the game froze for seconds. Now: it stays out
        // of a fast spam you are already handling, and each step gets a small time budget (a half-made plan is dropped
        // and tried again a few steps later).
        if (wave && buf.rapid >= buf.spamStreak && buf.sinceInput < st(30)) {
            autoPlan.valid = false;
            return;
        }
        if (tickCount < autoCooldownUntil) {
            return;
        }
        ClickTimer autoTimer;
        struct BudgetRestore {
            double value;
            ~BudgetRestore() { buf.budgetMs = value; }
        } budgetRestore { buf.budgetMs };
        buf.budgetMs = std::min(buf.budgetMs, 2.5);

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
                    if (t > 0 && clickOverTime()) break;
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
                        if (clickOverTime()) break;
                        int slack = 0;
                        for (int level = 1; level <= studio.altRoomLevels; ++level) {
                            if (clickOverTime()) break;
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
            if (clickOverTime()) {
                autoCooldownUntil = tickCount + st(6);   // out of time: skip a few steps instead of stalling the frame
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
        frameSpam.recentCount = 0;
        if (layer && buf.appliedInput != buf.realInput) {
            sendButton(layer, buf.realInput);   // hand the wave back to what you are really holding
        }
        say("frame spam off ({})", why);
    }


    // One physics tick of frame spam: toggle the wave every `spamTicks` ticks. Before each toggle the ghost
    // measures where you are in the gap; if the toggle would push you toward a wall it waits a little
    // longer (the same centring as gap centering), so the spam stays in the middle.
    void tickFrameSpam(PlayLayer* layer, PlayerObject* p1) {
        if (!studio.spamOn || !buf.enabled || p1->m_isDead || layer->m_isPlatformer ||
            !(p1->m_isDart || (studio.spamOther && (p1->m_isBird || p1->m_isBall || p1->m_isSwing)))) {
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
        if (!p1->m_isDart) {
            // UFO / ball / swing: no gap to centre in, just an even spam at the chosen rate
            frameSpam.holding = !frameSpam.holding;
            buf.autoTagLast = tickCount;
            sendButton(layer, frameSpam.holding);
            frameSpam.edgeTime = std::max(frameSpam.edgeTime + frameSpam.periodExact, static_cast<double>(tickCount) + 1.0);
            frameSpam.nextEdge = static_cast<int>(std::lround(frameSpam.edgeTime));
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
            frameSpam.periodExact = std::min(wantedEdge, std::max(1.0, (holdLife + idleLife) * studio.spamGapPct / 100.0));
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
        return { sessionStats.altEdges, sessionStats.altAdjusted, sessionStats.saves, memory.size(), sessionStats.altNoGain, sessionStats.altMarginRejected };
    }


    // HUD layout (Studio tab): the watermark, the console panel and the orb dot can each be nudged.
    void applyHudLayout() {
        auto win = CCDirector::get()->getWinSize();
        if (buf.watermark) {
            buf.watermark->setPosition({ 6.f + studio.hudOff[0], 4.f + studio.hudOff[1] });
        }
        if (buf.panel) {
            // the terminal keeps its top left corner where it is when it is resized
            buf.panel->setScale(studio.terminalScale);
            buf.panel->setPosition({ 8.f + studio.hudOff[2], win.height - PANEL_H * studio.terminalScale - 8.f + studio.hudOff[3] });
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
        neonDisc(buf.orbDot, { 0.f, 0.f }, 9.f, neonPremul({ color.r, color.g, color.b, 0.22f }), 16);
        neonDisc(buf.orbDot, { 0.f, 0.f }, 5.5f, color, 14);
    }

}  // namespace synca
