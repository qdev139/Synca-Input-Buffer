// Synca setup check: reads the current settings and says, in plain words, what is likely to hurt (lag, late clicks,
// settings that cancel each other). Shown by the CHECK MY SETUP button in the Studio tab.
#include <algorithm>
#include <string>
#include <vector>

using namespace geode::prelude;

#include "synca.hpp"
#include "engine.hpp"

namespace synca {

std::string studioAdvice() {
    std::vector<std::string> notes;
    int longest = 0;
    for (int i = 0; i < SM_COUNT; ++i) longest = std::max(longest, studio.modeTicks[i]);

    if (!buf.enabled) {
        notes.push_back("The buffer is OFF (stage). Press the stage key or use the < > in the pause menu.");
    }
    if (buf.enabled && !studio.perMode && longest > buf.ticksAll) {
        notes.push_back(fmt::format("The stage is a preset ({} ticks for every mode), so your gamemode delays (up to {} ticks) are ignored. Turn on 'My delays on every stage' (Studio) or pick the PER MODE stage.", buf.ticksAll, longest));
    }
    if (buf.lowLatency && longest > 4) {
        notes.push_back("Low latency is on: every delay is capped at 1 frame, so the bigger gamemode delays do nothing.");
    }
    if (buf.perfMode && longest > 16) {
        notes.push_back("Performance mode is on with big delays: searches are cut short, so long delays are rarely found. Turn it off if the game runs fine.");
    }
    if (studio.perMode && longest > 32 && buf.userBudget < 8.0) {
        notes.push_back("Delays above 8 frames need time to search. Raise 'Prediction time budget' to 8 ms or more.");
    }
    if (buf.predictMovers && levelScan.done && levelScan.moving > 150) {
        notes.push_back(fmt::format("This level has {} moving objects. Predicted moving hazards only follows the 40 nearest to you; if it still lags, switch it off.", levelScan.moving));
    }
    if (studio.altOn && studio.altMaxTicks > 16) {
        notes.push_back("Alternate assist max delay is above 4 frames: a correction that long can make a fast spam feel late. 8 to 12 ticks is the usual range.");
    }
    if (studio.altOn && studio.altMargin == 0) {
        notes.push_back("Alternate assist margin is 0: corrections that only just fit will be used, and those are the ones that fail. 1 is the usual value.");
    }
    if (studio.adaptiveOn && studio.perMode && studio.adaptiveMax > longest * 2 && longest > 0) {
        notes.push_back("The adaptive buffer may add more than twice your biggest delay at a death spot. Lower 'Adaptive max' if it feels late there.");
    }
    if ((buf.autoRelease || buf.robotAutoRelease || buf.dashAutoRelease || studio.spamOn || studio.shipGuard) && !buf.autoNormalRuns) {
        notes.push_back("Auto assists (auto-release, spam, ship guard) only work in Practice mode. Turn on 'Allow in normal runs' to use them in normal runs (the AUTO INPUTS ON tag will show).");
    }
    if (studio.spamOn && studio.spamCps < studio.spamTriggerCps) {
        notes.push_back("Frame spam starts at a higher CPS than it spams at: it will slow your spam down. Set 'Then spam at' above 'Start when I click'.");
    }
    if (!studio.altOn && longest == 0) {
        notes.push_back("Every gamemode delay is 0, so the buffer has nothing to do. Raise at least one delay in the Gamemodes tab.");
    }

    // what the alternate assist really did this session (needs a few dozen edges before it says anything)
    auto telemetry = sessionTelemetry();
    if (studio.altOn && telemetry.altEdges >= 30) {
        double edges = static_cast<double>(telemetry.altEdges);
        if (telemetry.altMarginRejected / edges > 0.35) {
            notes.push_back(fmt::format("{:.0f}% of the alternate assist's corrections were thrown out by the safety margin. Lower the margin by 1, or switch off 'Survive timing jitter', if it leaves too many edges alone.", 100.0 * telemetry.altMarginRejected / edges));
        }
        if (telemetry.altNoGain / edges > 0.6) {
            notes.push_back(fmt::format("{:.0f}% of the alternate assist's edges found no gain. A longer 'Wave look' or 'Wave horizon' lets it see the corridor earlier.", 100.0 * telemetry.altNoGain / edges));
        }
        if (telemetry.altAdjusted == 0) {
            notes.push_back("The alternate assist has not changed a single edge this session. It only works in a fast wave spam with a steady rhythm.");
        }
    }

    if (studio.altOn && altForecast.n >= 30) {
        double scale = std::max(0.25f, stepScale());
        double mean = static_cast<double>(altForecast.absSum) / altForecast.n / scale;
        double bias = static_cast<double>(altForecast.sum) / altForecast.n / scale;
        if (mean > 3.0) {
            notes.push_back(fmt::format("The alternate assist's rhythm forecast is off by {:.1f} ticks on average. Your spam is uneven: try more samples, a higher tolerance, or keep the zigzag even.", mean));
        }
        if (std::abs(bias) > 1.5) {
            notes.push_back(fmt::format("The forecast is consistently {:.1f} ticks {} than your real spam. Raise or lower 'Click bias' by that much.", std::abs(bias), bias > 0 ? "faster" : "slower"));
        }
    }

    if (notes.empty()) {
        return "Nothing looks wrong. Your settings fit together.";
    }
    std::string out;
    for (auto const& note : notes) out += "- " + note + "\n";
    return out;
}

}  // namespace synca
