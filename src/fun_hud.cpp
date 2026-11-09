// Synca Fun tab: the optional on-screen info lines, sounds and flashes. Split out of main.cpp (v1.8) so it compiles
// on its own, in parallel with the engine.
#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <deque>
#include <vector>

using namespace geode::prelude;

#include "synca.hpp"
#include "engine.hpp"

namespace synca {
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
            ++fun.pressCount;
        }
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
        fun.attemptSaves = 0;
        fun.bestAtAttempt = fun.best;
        fun.flashedBest = false;
        fun.flashedSpot = false;
        fun.hardestX = -1.f;
        fun.hardestX = memoryHardestX();
    }

    void funOnDeath(float percent) {
        if (!studio.funAny) return;
        fun.best = std::max(fun.best, percent);
        fun.deaths.push_back(percent);
        if (fun.lastDeath >= 0.f && percent >= fun.lastDeath) ++fun.streak;
        else fun.streak = 0;
        fun.lastDeath = percent;
        if (studio.fun[17]) {
            std::string last = tickCount - fun.verdictTick < st(480) ? "  |  last click: " + fun.verdict : std::string();
            funFlash(fmt::format("{:.0f}%  {} SAVE{}{}", percent, fun.attemptSaves, fun.attemptSaves == 1 ? "" : "S", last));
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
        if (studio.fun[0]) lines.push_back(stageText());
        if (studio.fun[1]) {
            bool recent = tickCount - fun.lastDelayTick < st(240);
            int shown = static_cast<int>(std::lround(fun.lastDelay / stepScale()));
            lines.push_back(recent && shown > 0 ? fmt::format("ASSIST +{}t ({:.1f}f)", shown, shown * TICK) : std::string("ASSIST 0"));
        }
        if (studio.fun[2]) lines.push_back(fmt::format("SAVES {} / {}", fun.attemptSaves, fun.saveCount));
        if (studio.fun[3]) {
            lines.push_back(fun.pressCount > 0 ? fmt::format("SAVE RATE {:.0f}%", 100.0 * fun.saveCount / fun.pressCount)
                                               : std::string("SAVE RATE -"));
        }
        if (studio.fun[4]) {
            lines.push_back(fun.saveCount > 0
                ? fmt::format("EARLY BY {:.1f}f", static_cast<double>(fun.earlySum) / stepScale() / fun.saveCount * TICK)
                : std::string("EARLY BY -"));
        }
        if (studio.fun[5]) {
            std::string text = "SAVED AT";
            for (int pct : fun.savePcts) text += fmt::format(" {}%", pct);
            lines.push_back(fun.savePcts.empty() ? std::string("NO SAVES YET") : text);
        }
        if (studio.fun[6]) {
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
            if (studio.altOn) text += "  " + forecastText();
            lines.push_back(text);
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
            std::string text = "DIED AT";
            size_t first = fun.deaths.size() > 5 ? fun.deaths.size() - 5 : 0;
            for (size_t i = first; i < fun.deaths.size(); ++i) text += fmt::format(" {:.0f}", fun.deaths[i]);
            lines.push_back(fun.deaths.empty() ? std::string("NO DEATHS YET") : text);
        }
        if (studio.fun[11]) {
            double sum = 0.0;
            for (float d : fun.deaths) sum += d;
            lines.push_back(fun.deaths.empty() ? std::string("AVG DEATH -") : fmt::format("AVG DEATH {:.0f}%", sum / fun.deaths.size()));
        }
        if (studio.fun[12]) {
            std::string why = tickCount - fun.lastWhyTick < st(480) ? " | no save: " + fun.lastWhy : std::string();
            lines.push_back(fmt::format("PREDICT {:.1f} ms (budget {:.0f}){}", fun.lastMs, buf.budgetMs, why));
        }

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
}  // namespace synca
