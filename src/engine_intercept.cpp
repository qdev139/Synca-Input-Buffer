// Synca engine: the input interceptor (every press and release goes through here).
// (split out of main.cpp; shared types and declarations are in engine.hpp)
#include "engine.hpp"

namespace synca {


    bool intercept(GJBaseGameLayer* self, bool down, bool isPlayer1) {
        ClickTimer timer;
        longDelayBypass = false;
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
        // v5.12: it starts when your own click rate reaches "start at" clicks per second (measured over your last
        // 4 real presses), then spams at the "spam at" rate. Before, any 3 fast edges started it.
        double yourCps = 0.0;
        if (down && changedInput && buf.isPlayer1 && !g_p2.active && !frameSpam.owned) {
            auto& fs = frameSpam;
            if (fs.recentCount > 0 && tickCount - fs.recent[fs.recentCount - 1] > st(60)) {
                fs.recentCount = 0;   // a pause of a quarter second: a new burst
            }
            if (fs.recentCount == 4) {
                for (int i = 0; i < 3; ++i) fs.recent[i] = fs.recent[i + 1];
                fs.recentCount = 3;
            }
            fs.recent[fs.recentCount++] = tickCount;
            if (fs.recentCount == 4) {
                int span = std::max(1, fs.recent[3] - fs.recent[0]);
                yourCps = 3.0 * std::max(1, st(240)) / span;
            }
        }
        if (studio.spamOn && !frameSpam.owned && changedInput && down && yourCps >= studio.spamTriggerCps && buf.isPlayer1 &&
            !g_p2.active && !twoPlayer && !dual && (p1->m_isDart || (studio.spamOther && (p1->m_isBird || p1->m_isBall || p1->m_isSwing))) &&
            !layer->m_isPlatformer && !buf.autoReleased && !buf.autoClicked && (layer->m_isPracticeMode || buf.autoNormalRuns)) {
            int holdLife = 0;
            int idleLife = 0;
            bool spamWave = p1->m_isDart;   // the wave also needs a gap to centre in; UFO / ball / swing just spam at the rate
            if (!spamWave || probeGap(layer, p1, nullptr, false, holdLife, idleLife)) {
                // The rate is the CPS setting (a click is a press and a release, so 16 CPS = 32 edges a second). The
                // gap only LIMITS it: each phase may use at most about half of the free travel, so a very small gap
                // (fast speed, mini wave) gets faster spam and a normal 1-block gap keeps the rate you chose.
                double wantedEdge = std::max(1, st(240)) / (2.0 * std::max(4, studio.spamCps));
                frameSpam.periodExact = spamWave ? std::min(wantedEdge, std::max(1.0, (holdLife + idleLife) * studio.spamGapPct / 100.0)) : wantedEdge;
                frameSpam.period = std::max(1, static_cast<int>(std::lround(frameSpam.periodExact)));
                frameSpam.owned = true;
                frameSpam.holding = down;
                frameSpam.checked = false;
                frameSpam.lastRealEdge = tickCount;
                frameSpam.edgeTime = static_cast<double>(tickCount) + frameSpam.periodExact;
                frameSpam.nextEdge = static_cast<int>(std::lround(frameSpam.edgeTime));
                frameSpam.recentCount = 0;
                say("frame spam on: you clicked {:.0f} CPS, spamming {} CPS (gap {}t of travel, one edge every {:.1f}t)", yourCps, studio.spamCps,
                    holdLife + idleLife, frameSpam.periodExact);
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
                neonDisc(buf.pathNode, p1->getPosition(), 3.6f, neonPremul({ 0.6f, 0.6f, 0.65f, 0.9f }), 12);
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
            if (anyUfo) phaseRoom = std::max(phaseRoom, st(3));   // UFO machine gun: a flap needs a little room to move
            maxDelay = std::min({ maxDelay, st(studio.altMaxTicks), phaseRoom });
        }
        if (buf.lowLatency) {
            maxDelay = std::min(maxDelay, st(4));  // at most 1 frame (60 fps) of delay
        }
        if (studio.adaptiveOn && maxDelay > 0 && !fullAlternateWave && !buf.lowLatency) {
            maxDelay += st(adaptiveBonusAt(layer->getCurrentPercent()));
        }
        if (studio.frontierOn && maxDelay > 0 && !fullAlternateWave && !buf.lowLatency && frontierBest >= 5.f &&
            layer->getCurrentPercent() >= frontierBest - 3.f) {
            maxDelay += st(studio.frontierTicks);   // you are at the edge of what you have reached: a little more room
        }
        if (maxDelay > st(32) && !buf.perfMode && !buf.lowLatency) {
            buf.budgetMs = std::min(buf.budgetMs * 1.5, 16.0);   // v1.6: a long delay range gets a longer time budget
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
            int window = st(std::clamp(buf.orbWindow, 1, 120));
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
            // v1.8 jitter check: the forecast of YOUR next edge can be a tick off. A correction that only works when the
            // next manual edge lands exactly where it was forecast is the one that fails on dense spam, so it must also
            // survive that edge arriving 1 tick early or late.
            if (studio.altJitter && waveMargin > 0 && cubeStyleWave && buf.altPredictionDownEvery > 0 && buf.altPredictionUpEvery > 0) {
                for (int jitter : { -st(1), st(1) }) {
                    if (clickOverTime()) {
                        buf.altNextEdgeShift = 0;
                        return false;
                    }
                    buf.altNextEdgeShift = jitter;
                    SimEnd jitterEnd;
                    int jitterRoute = waveScore(layer, p1, p2, dual, delay, down, horizon, horizon, &jitterEnd);
                    buf.altNextEdgeShift = 0;
                    if (jitterRoute < alternateWaveTarget || jitterEnd == SimEnd::StopSearch) {
                        return false;
                    }
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
        // v1.11 long-delay bypass: with a big cube / ball delay (12+ frames) a click made far too early lands, walks into the spike and
        // crashes late - which looks exactly like "the next obstacle's crash" and was passed through untouched, and the search also
        // gave up after 4 delays when the crash stayed at the same moment. With a long delay set, both shortcuts are skipped so the
        // whole range is searched (in frame steps) and the click is held until the jump is right.
        bool longRange = studio.longBypass && groundPress && maxDelay >= st(48);
        longDelayBypass = longRange;
        if (longRange && !buf.perfMode && tickCount >= watchdogUntil) {
            buf.budgetMs = std::max(buf.budgetMs, std::min(buf.userBudget * 2.0, 20.0));   // the whole 120-tick range needs more search time
        }
        if (!longRange && groundPress && buf.lateCrash > 0 && base >= st(buf.lateCrash) && buf.baseTouchedDown) {
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
            : (ufoPress && alternateEvery > 0) ? std::min(horizon, base + st(16))   // v1.17: a UFO machine gun only has to last longer, not forever
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
                    }, !(alternateEvery > 0))
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
            if (studio.funAny) {
                fun.lastWhy = crashFixed ? "same crash at any timing"
                    : clickMs() > buf.budgetMs ? fmt::format("time ran out at +{}t", dbgBestK)
                    : maxDelay <= 0 ? "max delay 0"
                    : fmt::format("no delay up to +{}t works", static_cast<int>(std::lround(maxDelay / stepScale())));
                fun.lastWhyTick = tickCount;
                fun.verdict = "no save: " + fun.lastWhy;
                fun.verdictTick = tickCount;
            }
            say("no save: {}{} dies in {}t (max {}t, best +{}t -> {}t of {}){}", down ? "press" : "release", tickTag, base, maxDelay,
                dbgBestK, dbgBestRet, horizon, !crashFixed ? "" : base < st(30) ? " - too late: waiting only makes it crash sooner" : " - same crash at any timing (next obstacle)");
            decided(layer, p1, p2, dual, 0, down, horizon, releaseAfter);
            buf.appliedInput = down;
            return false;
        }

        // v1.8 safe-window centring. The smallest delay that survives sits right at the edge of the safe window: a
        // hair of drift and it dies. The delays after it are tested too, and when the window is wide enough the save
        // moves toward its middle (at most 1 frame later), so it keeps room on both sides.
        if (studio.centerSaves && groundPress && !fromMemory && !fullAlternateWave && saved > 0 && saved < maxDelay) {
            int reach = std::min(maxDelay, saved + st(8));
            int windowEnd = saved;
            for (int k = saved + 1; k <= reach && !clickOverTime(); ++k) {
                SimEnd e;
                int ret = scoreAt(k, needed, &e);
                if (e == SimEnd::StopSearch || ret < needed) break;
                windowEnd = k;
            }
            int room = windowEnd - saved;
            if (room >= 2) {
                int pick = saved + std::min(room / 2, st(4));
                SimEnd e;
                int ret = scoreAt(pick, needed, &e);
                if (ret >= needed && e != SimEnd::StopSearch) {
                    say("centred in the safe window: +{}t -> +{}t (window +{}t..+{}t)", saved, pick, saved, windowEnd);
                    saved = pick;
                    landed = ret;
                }
            }
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
        if (studio.progressStageBands && GameManager::get()->m_playLayer && !bandsPausedByHand) {
            bandsPausedByHand = true;
            say("auto switch by level %: paused for this level (you picked a stage by hand)");
        }
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

}  // namespace synca
