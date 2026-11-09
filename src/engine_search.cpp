// Synca engine: orb alignment, scoring per gamemode and the delay searches.
// (split out of main.cpp; shared types and declarations are in engine.hpp)
#include "engine.hpp"

namespace synca {


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
        // v1.9: the alignment reach follows the gamemode delay too. A big delay (120 ticks) used to be cut to the "Orb alignment: max
        // delay" setting (16 by default), so the first orbs of a level like Tidal Wave were never moved onto.
        int window = st(std::clamp(buf.orbWindow, 1, 120));
        window = std::max(window, std::min(maxDelay, st(120)));
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


    bool sameCrash(int k, int ret, int base, int& streak) {
        if (longDelayBypass) {
            return false;
        }
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


    // v5.12: a long delay range (120 ticks) used to be scanned one tick at a time, so the time budget ran out
    // around +32t (8 frames at 60 fps) and the later, bigger delays were never even tried. Now the first 4 frames
    // are scanned tick by tick and the rest one frame (4t at 60 fps) at a time; when a frame step works, the
    // ticks just before it are checked so the smallest working delay is still the one used.
    int delayStep(int k) {
        if (longDelayBypass) {
            return 1;   // v1.15: a frame-perfect jump is a window only a tick or two wide: with a long delay set, every tick is tested
        }
        return k < st(16) ? 1 : std::max(1, st(4));
    }


    int nextDelay(int k, int maxDelay) {
        int n = k + delayStep(k);
        return (n > maxDelay && k < maxDelay) ? maxDelay : n;
    }


    int searchSmallest(int horizon, int maxDelay, int base, int* landed, std::function<int(int, int, SimEnd*)> const& score,
        double budget, bool quiet, bool needFull) {
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
        int lastFail = 0;
        for (int k = 1; k <= maxDelay; k = nextDelay(k, maxDelay)) {
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
                for (int j = lastFail + 1; j < k; ++j) {   // smallest delay inside the step that just worked
                    SimEnd e2;
                    int r2 = score(j, target, &e2);
                    if (e2 == SimEnd::StopSearch) {
                        break;
                    }
                    if (r2 >= target) {
                        *landed = r2;
                        return j;
                    }
                }
                *landed = ret;
                return k;
            }
            lastFail = k;
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
        bool needFull) {
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


    int searchWave(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, bool down, int horizon, int maxDelay, int base,
        int* landed, bool requireFullSurvival, int preferredDelay) {
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
        int horizon, int maxDelay, int base, int preferredDelay, int* landed, int smoothTarget) {
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
        int releaseAfter, int base, int* landed, bool needFull, int preferredDelay) {
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
        int lastFail = 0;
        for (int k = 1; k <= maxDelay; k = anyWave ? k + 1 : nextDelay(k, maxDelay)) {
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
                for (int j = lastFail + 1; j < k && !overBudget(); ++j) {   // v5.12: smallest delay inside the step that worked
                    SimEnd e2;
                    int r2 = simulateOrbs(layer, p1, p2, dual, j, down, horizon, releaseAfter, target, &e2);
                    if (e2 == SimEnd::StopSearch) {
                        break;
                    }
                    if (r2 >= target) {
                        k = j;
                        ret = r2;
                        break;
                    }
                }
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
            lastFail = k;
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

}  // namespace synca
