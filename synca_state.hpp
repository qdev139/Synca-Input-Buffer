#pragma once
// (included by synca.hpp, do not include directly)
// ---------------------------------------------------------------------------------------------
// Input Buffer Studio additions, part 1: state and helpers.
//
// Included by synca.hpp INSIDE namespace synca, right after the Buffer struct. It relies on the
// includes pulled in by synca.hpp, so it has no #include lines of its own.
//
// What lives here:
//   * per-gamemode max delay ("custom frame for each gamemode"), read from mod settings
//   * state for the alternate assist
//   * the small helpers the engine calls from intercept() / applyStage() / readSettings()
// ---------------------------------------------------------------------------------------------

// One entry per gamemode. Delays are stored in 240/s ticks, like everything else in the engine:
// 4 ticks = 1 frame at 60 fps, so the menus show both.
enum StudioMode { SM_CUBE = 0, SM_SHIP, SM_BALL, SM_UFO, SM_WAVE, SM_ROBOT, SM_SPIDER, SM_SWING, SM_COUNT };

constexpr int PROGRESS_BANDS = 4;

enum StudioOrb { SO_YELLOW = 0, SO_PINK, SO_BLUE, SO_RED, SO_BLACK, SO_GREEN, SO_SPIDER, SO_DASH_GREEN, SO_DASH_PINK, SO_COUNT };

struct StudioOrbInfo {
    char const* key;
    char const* name;
    char const* sprite;
    GameObjectType type;
};

constexpr StudioOrbInfo STUDIO_ORBS[SO_COUNT] = {
    { "orb-ticks-yellow", "Yellow", "orb-yellow.png", GameObjectType::YellowJumpRing },
    { "orb-ticks-pink", "Pink", "orb-pink.png", GameObjectType::PinkJumpRing },
    { "orb-ticks-blue", "Blue", "orb-blue.png", GameObjectType::GravityRing },
    { "orb-ticks-red", "Red", "orb-red.png", GameObjectType::RedJumpRing },
    { "orb-ticks-black", "Black", "orb-black.png", GameObjectType::DropRing },
    { "orb-ticks-green", "Green", "orb-green.png", GameObjectType::GreenRing },
    { "orb-ticks-spider", "Spider", "orb-spider.png", GameObjectType::SpiderOrb },
    { "orb-ticks-dash-green", "Green dash", "orb-dash-green.png", GameObjectType::DashRing },
    { "orb-ticks-dash-pink", "Pink dash", "orb-dash-pink.png", GameObjectType::GravityDashRing },
};

struct StudioModeInfo {
    char const* key;     // mod setting holding the max delay in ticks (0 = buffer off for this mode)
    char const* name;    // shown in the menus
    int fallback;        // what the on/off switch brings back if no earlier value was remembered
    int maxTicks;        // upper limit of the setting
};

constexpr StudioModeInfo STUDIO_MODES[SM_COUNT] = {
    { "delay-cube", "Cube", 24, 120 },
    { "delay-ship", "Ship", 4, 120 },
    { "delay-ball", "Ball", 24, 120 },
    { "delay-ufo", "UFO", 16, 120 },
    { "delay-wave", "Wave", 12, 120 },
    { "delay-robot", "Robot", 24, 120 },
    { "delay-spider", "Spider", 16, 120 },
    { "delay-swing", "Swing", 16, 120 },
};

// The Fun tab: 20 optional extras. 0-12 are lines of on-screen info, 13-16 sounds and flashes, 17-19 text and look.
constexpr int FUN_COUNT = 20;
constexpr char const* FUN_NAMES[FUN_COUNT] = {
    "Clicks per second", "Peak clicks per second", "Time in this level", "Attempt number", "Best % this visit",
    "Last 5 deaths", "Average death %", "Mode and speed now", "Next busy part", "Hardest spot",
    "Spam evenness meter", "Assist delay shown", "Saves this level",
    "Click sound", "New best flash", "Spot cleared flash", "Improving streak flash",
    "Text after a death", "Rainbow info text", "Zen mode (hide Synca HUD)"
};

struct Studio {
    // true while the Custom stage is selected: the per-gamemode delays below are used instead
    // of the preset stage's two numbers
    bool perMode = false;
    int modeTicks[SM_COUNT] = { 24, 4, 24, 16, 12, 24, 16, 16 };

    // Path & Alternate Assist tab owns these settings and the predictor's runtime state.
    bool altOn = false;
    // USKC preset learns quickly and keeps corrections short; every correction is
    // clamped again to the observed phase so it cannot trail the next manual edge.
    int altMaxTicks = 8;
    bool altMode[SM_COUNT] = { false, true, true, true, true, false, false, true };
    int altSamples = 3;
    int altTolerance = 3;
    int altMargin = 1;
    int altBurstGap = 30;
    int altWaveLook = 12;
    // Tight alternate corridors often have a useful correction that only adds one
    // simulation tick before the next direction change.  Requiring a larger gain
    // discarded those edges and made the assist feel like raw input.
    int altWaveMinGain = 1;
    int altWaveHorizon = 300;
    // Gap centering (straight fly): a ~1 block gap flown with steady spam. The ghost measures how
    // far you are from the middle of the gap and the edge that would push you further toward
    // the wall is delayed just long enough to bring you back toward the centre.
    bool nextModeHud = false;    // show the next gamemode (from the level scan) while playing
    bool autoTagHidden = false;  // the AUTO INPUTS ON text is not shown at all
    bool autoTagCompact = false; // the AUTO INPUTS ON tag only shows at the start of an attempt, when paused and on the end screen
    int autoTagOpacity = 60;  // percent; the AUTO INPUTS ON tag can be dimmed (never below 25) but not removed
    bool fun[FUN_COUNT] = {};
    bool funAny = false;
    bool moversAuto = true;   // levels with moving hazards: predict them automatically
    bool modesLinked = false; // Gamemodes tab: one slider moves every mode together
    bool spamOn = false;      // frame spam (CPS cap) in 1-block gaps
    int spamTicks = 4;        // (old, unused)
    int spamCps = 16;         // frame spam: clicks per second it produces (a click = press + release)
    bool pathForecast = false; // path view: also draw the spam it expects from you (off: only what you hold now)
    int altSmoothPct = 100;   // even zigzag strength
    int altPlanWidth = 4;     // how many first delays get the second-edge search
    int altSafeKeep = 3;      // how many safe corrections are compared for room
    int altRoomLevels = 4;    // how many half-tick steps of timing error a correction is tested against
    bool shipGuard = false;   // ship: switches at the last safe moment in tight corridors
    int shipGuardFrom = 0;
    int shipGuardTo = 100;
    bool altSmooth = true;    // alternate wave: keep the zigzag even (each edge waits so the phases keep the learned length)
    bool altPlan = true;  // alternate wave: when no single delay saves you, also plan the next edge
    bool sfOn = false;
    int sfStrength = 100;  // how much of the way back to the middle one correction goes (percent)
    int sfGap = 26;       // detection: ticks of free travel (hold + release) that still count as a 1 block gap
    int sfDead = 1;       // no correction while you are within this many ticks of the middle
    // Click bias: how many ticks your real clicks tend to land late (+) or early (-). The ghost is
    // checked around that offset, so a correction stays safe where your clicks actually arrive.
    int clickBias = 0;
    int orbMargin = 1;        // ticks of timing slack an orb click must survive (orb alignment)
    bool orbConfOn = true;    // small green / yellow / red dot after an orb decision
    int swingLook = 48;       // swing look-ahead in ticks
    bool adaptiveOn = false;  // add a little extra buffer at places you keep dying, remove it as you improve
    int adaptiveMax = 4;
    bool p2Buffer = true;     // two-player levels: Player 2 gets its own buffer (own queue, rhythm and delay)
    bool perfGuardOn = false; // shorten look-ahead and slow the path preview when frames get long
    int perfGuardMs = 20;
    int themeIndex = 0;
    int hudOff[6] = { 0, 0, 0, 0, 0, 0 };  // watermark x,y / panel x,y / orb dot x,y (pixels)
    bool progressStageBands = false;
    // Progress bands: while the level % is inside [start, end) the band's stage is used.
    // A later band wins where bands overlap; start >= end means the band is unused.
    // Stage = index into STAGES[]: 0..5 fixed delays, 6 = PER MODE (your Gamemodes ticks), 7 = OFF.
    int bandStart[PROGRESS_BANDS] = { 30, 60, 0, 0 };
    int bandEnd[PROGRESS_BANDS] = { 60, 80, 0, 0 };
    int bandStage[PROGRESS_BANDS] = { 3, 6, 0, 0 };
    int progressBaseStage = 6;

    bool pathEnabled = true;
    bool pathShowIdle = true;
    bool pathShowClick = true;
    int pathHorizon = 300;
    int pathRefreshMs = 24;

    // Per-ring ghost click timing. 0 keeps the automatic timing search.
    int orbTicks[SO_COUNT] = {};
    bool orbTicksLinked = false;

    // the stage the toggle key goes back to after switching the buffer off
    int resumeStage = 6;
};

inline Studio studio;

// Which gamemode decides the delay. When both players are active and in different modes the
// order below wins, the same priority the engine already used (wave, then ship, then the rest).
inline int studioModeOf(PlayerObject* player) {
    if (player->m_isDart) {
        return SM_WAVE;
    }
    if (player->m_isShip) {
        return SM_SHIP;
    }
    if (player->m_isBird) {
        return SM_UFO;
    }
    if (player->m_isSwing) {
        return SM_SWING;
    }
    if (player->m_isBall) {
        return SM_BALL;
    }
    if (player->m_isRobot) {
        return SM_ROBOT;
    }
    if (player->m_isSpider) {
        return SM_SPIDER;
    }
    return SM_CUBE;
}

inline int studioModeFor(PlayerObject* p1, PlayerObject* p2) {
    int first = studioModeOf(p1);
    int second = p2 ? studioModeOf(p2) : SM_CUBE;
    constexpr int ORDER[] = { SM_WAVE, SM_SHIP, SM_UFO, SM_SWING, SM_BALL, SM_ROBOT, SM_SPIDER };
    for (int mode : ORDER) {
        if (first == mode || second == mode) {
            return mode;
        }
    }
    return SM_CUBE;
}

// Max delay in 240/s ticks for whatever the players are in right now (Custom stage only).
inline int studioMaxTicks(PlayerObject* p1, PlayerObject* p2) {
    int mode = studioModeFor(p1, p2);
    return std::clamp(studio.modeTicks[mode], 0, STUDIO_MODES[mode].maxTicks);
}

// One player's own delay in 240/s ticks: the per-mode value (Custom stage) or the stage preset
// (2f, 4f ...) for that player's gamemode.
inline int ticksForPlayer(PlayerObject* player) {
    if (!player) return 0;
    if (studio.perMode) {
        int mode = studioModeOf(player);
        return std::clamp(studio.modeTicks[mode], 0, STUDIO_MODES[mode].maxTicks);
    }
    if (player->m_isDart) return buf.ticksWave;
    if (player->m_isShip) return std::clamp(buf.shipMaxDelay, 0, 16);
    return buf.ticksAll;
}

// Dual mode: one input steers both icons, so each icon's own limit applies. A mode set to 0
// is off for that icon (the other one decides); when both are on, the smaller limit wins so
// neither icon is held longer than you allowed for its mode.
inline int combinedTicks(PlayerObject* p1, PlayerObject* p2) {
    int a = ticksForPlayer(p1);
    if (!p2) return a;
    int b = ticksForPlayer(p2);
    if (a <= 0) return b;
    if (b <= 0) return a;
    return std::min(a, b);
}

inline int studioOrbDelay(GameObjectType type) {
    for (int i = 0; i < SO_COUNT; ++i) {
        if (STUDIO_ORBS[i].type == type) {
            return std::clamp(studio.orbTicks[i], 0, 32);
        }
    }
    return 0;
}

// True when the alternate assist should handle this input instead of letting spam through raw.
inline bool studioAltActive(PlayerObject* p1, PlayerObject* p2) {
    if (!studio.altOn || !buf.enabled || !p1) {
        return false;
    }
    int mode = studioModeFor(p1, p2);
    return studio.altMode[mode];
}

inline void studioReadSettings() {
    auto* mod = Mod::get();
    for (int i = 0; i < SM_COUNT; ++i) {
        auto const& info = STUDIO_MODES[i];
        studio.modeTicks[i] = std::clamp(static_cast<int>(mod->getSettingValue<int64_t>(info.key)), 0, info.maxTicks);
    }
    studio.altOn = mod->getSavedValue<bool>("studio-alt-enabled", false);
    studio.altMaxTicks = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-max-delay", 8)), 1, 64);
    studio.altSamples = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-samples", 3)), 3, 7);
    studio.altTolerance = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-tolerance", 3)), 0, 8);
    studio.altMargin = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-margin", 1)), 0, 4);
    studio.altBurstGap = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-burst-gap", 30)), 8, 48);
    studio.altWaveLook = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-wave-look", 12)), 4, 64);
    studio.altWaveMinGain = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-wave-min-gain", 1)), 1, 24);
    studio.altWaveHorizon = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-wave-horizon", 300)), 60, 480);
    studio.altSmooth = mod->getSavedValue<bool>("studio-alt-smooth", true);
    studio.altPlan = mod->getSavedValue<bool>("studio-alt-plan", true);
    studio.nextModeHud = mod->getSavedValue<bool>("studio-next-mode", false);
    studio.autoTagHidden = mod->getSavedValue<bool>("studio-auto-tag-hidden", false);
    studio.autoTagCompact = mod->getSavedValue<bool>("studio-auto-tag-compact", false);
    studio.autoTagOpacity = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-auto-tag-opacity", 60)), 25, 100);
    studio.funAny = false;
    for (int i = 0; i < FUN_COUNT; ++i) {
        studio.fun[i] = mod->getSavedValue<bool>(fmt::format("studio-fun-{}", i), false);
        studio.funAny = studio.funAny || studio.fun[i];
    }
    studio.moversAuto = mod->getSavedValue<bool>("studio-movers-auto", true);
    studio.modesLinked = mod->getSavedValue<bool>("studio-modes-linked", false);
    studio.spamOn = mod->getSavedValue<bool>("studio-frame-spam", false);
    studio.spamTicks = 4;
    studio.spamCps = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-frame-spam-cps", 16)), 6, 40);
    studio.pathForecast = mod->getSavedValue<bool>("studio-path-forecast", false);
    studio.altSmoothPct = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-smooth-pct", 100)), 0, 100);
    studio.altPlanWidth = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-plan-width", 4)), 1, 8);
    studio.altSafeKeep = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-safe-keep", 3)), 2, 6);
    studio.altRoomLevels = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-alt-room-levels", 4)), 1, 6);
    studio.shipGuard = mod->getSavedValue<bool>("studio-ship-guard", false);
    studio.shipGuardFrom = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-ship-guard-from", 0)), 0, 99);
    studio.shipGuardTo = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-ship-guard-to", 100)), 1, 100);
    studio.sfOn = mod->getSavedValue<bool>("studio-sf-enabled", false);
    studio.sfStrength = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-sf-strength", 100)), 10, 100);
    studio.sfGap = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-sf-gap", 26)), 12, 48);
    studio.sfDead = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-sf-dead", 1)), 0, 6);
    studio.clickBias = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-click-bias", 0)), -8, 8);
    studio.orbMargin = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-orb-margin", 1)), 0, 4);
    studio.orbConfOn = mod->getSavedValue<bool>("studio-orb-conf", true);
    studio.swingLook = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-swing-look", 48)), 16, 96);
    studio.adaptiveOn = mod->getSavedValue<bool>("studio-adaptive", false);
    studio.adaptiveMax = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-adaptive-max", 4)), 1, 8);
    studio.p2Buffer = mod->getSavedValue<bool>("studio-p2-buffer", true);
    studio.perfGuardOn = mod->getSavedValue<bool>("studio-perf-guard", false);
    studio.perfGuardMs = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-perf-guard-ms", 20)), 8, 60);
    studio.themeIndex = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-theme", 0)), 0, 5);
    {
        static constexpr char const* HUD_KEYS[6] = { "studio-hud-wm-x", "studio-hud-wm-y", "studio-hud-panel-x",
            "studio-hud-panel-y", "studio-hud-dot-x", "studio-hud-dot-y" };
        for (int i = 0; i < 6; ++i) {
            studio.hudOff[i] = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>(HUD_KEYS[i], 0)), -400, 400);
        }
    }
    studio.progressStageBands = mod->getSavedValue<bool>("studio-progress-stage-bands", false);
    {
        // Bands 1 and 2 start from the old "stage 4" / "stage 7" values so saved settings carry over.
        static constexpr char const* LEGACY_START[2] = { "studio-progress-stage4-start", "studio-progress-stage7-start" };
        static constexpr char const* LEGACY_END[2] = { "studio-progress-stage4-end", "studio-progress-stage7-end" };
        static constexpr int DEFAULT_START[PROGRESS_BANDS] = { 30, 60, 0, 0 };
        static constexpr int DEFAULT_END[PROGRESS_BANDS] = { 60, 80, 0, 0 };
        static constexpr int DEFAULT_STAGE[PROGRESS_BANDS] = { 3, 6, 0, 0 };
        for (int i = 0; i < PROGRESS_BANDS; ++i) {
            int startFallback = i < 2 ? static_cast<int>(mod->getSavedValue<int64_t>(LEGACY_START[i], DEFAULT_START[i])) : DEFAULT_START[i];
            int endFallback = i < 2 ? static_cast<int>(mod->getSavedValue<int64_t>(LEGACY_END[i], DEFAULT_END[i])) : DEFAULT_END[i];
            studio.bandStart[i] = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>(fmt::format("studio-band{}-start", i + 1), startFallback)), 0, 99);
            studio.bandEnd[i] = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>(fmt::format("studio-band{}-end", i + 1), endFallback)), 0, 100);
            studio.bandStage[i] = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>(fmt::format("studio-band{}-stage", i + 1), DEFAULT_STAGE[i])), 0, STAGE_COUNT - 1);
        }
    }
    studio.progressBaseStage = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-progress-base-stage", buf.stage)), 0, STAGE_COUNT - 1);
    studio.altMode[SM_SHIP] = mod->getSavedValue<bool>("studio-alt-ship", true);
    studio.altMode[SM_BALL] = mod->getSavedValue<bool>("studio-alt-ball", true);
    studio.altMode[SM_UFO] = mod->getSavedValue<bool>("studio-alt-ufo", true);
    studio.altMode[SM_WAVE] = mod->getSavedValue<bool>("studio-alt-wave", true);
    studio.altMode[SM_SWING] = mod->getSavedValue<bool>("studio-alt-swing", true);
    studio.pathEnabled = mod->getSavedValue<bool>("studio-path-enabled", true);
    studio.pathShowIdle = mod->getSavedValue<bool>("studio-path-show-idle", true);
    studio.pathShowClick = mod->getSavedValue<bool>("studio-path-show-click", true);
    studio.pathHorizon = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-path-horizon", 300)), 24, 480);
    studio.pathRefreshMs = std::clamp(static_cast<int>(mod->getSavedValue<int64_t>("studio-path-refresh-ms", 24)), 8, 100);
    for (int i = 0; i < SO_COUNT; ++i) {
        studio.orbTicks[i] = std::clamp(static_cast<int>(mod->getSettingValue<int64_t>(STUDIO_ORBS[i].key)), 0, 32);
    }
    studio.orbTicksLinked = mod->getSettingValue<bool>("orb-ticks-linked");
}


// ---------------------------------------------------------------------------------------------
// Colour themes: every panel and label colour is rotated around the colour wheel, so one palette
// serves all themes. Index 0 is the original Synca plum / rose.
// ---------------------------------------------------------------------------------------------
struct StudioTheme {
    char const* name;
    float hueShift;   // degrees
    float saturation; // multiplier
};

constexpr int THEME_COUNT = 6;
constexpr StudioTheme STUDIO_THEMES[THEME_COUNT] = {
    { "SYNCA", 0.f, 1.f }, { "OCEAN", -105.f, 1.f }, { "EMERALD", -160.f, 0.95f },
    { "SUNSET", 100.f, 1.05f }, { "INDIGO", -40.f, 1.f }, { "MONO", 0.f, 0.10f }
};

inline ccColor3B themeColor(ccColor3B c) {
    int index = studio.themeIndex;
    if (index <= 0 || index >= THEME_COUNT) return c;
    float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    float maxV = std::max(r, std::max(g, b));
    float minV = std::min(r, std::min(g, b));
    float delta = maxV - minV;
    float h = 0.f;
    if (delta > 1e-5f) {
        if (maxV == r) h = std::fmod((g - b) / delta, 6.f);
        else if (maxV == g) h = (b - r) / delta + 2.f;
        else h = (r - g) / delta + 4.f;
        h *= 60.f;
        if (h < 0.f) h += 360.f;
    }
    float s = maxV > 1e-5f ? delta / maxV : 0.f;
    float v = maxV;
    h = std::fmod(h + STUDIO_THEMES[index].hueShift + 720.f, 360.f);
    s = std::clamp(s * STUDIO_THEMES[index].saturation, 0.f, 1.f);
    float chroma = v * s;
    float x = chroma * (1.f - std::abs(std::fmod(h / 60.f, 2.f) - 1.f));
    float m = v - chroma;
    float rr = 0.f, gg = 0.f, bb = 0.f;
    int sector = static_cast<int>(h / 60.f) % 6;
    switch (sector) {
        case 0: rr = chroma; gg = x; break;
        case 1: rr = x; gg = chroma; break;
        case 2: gg = chroma; bb = x; break;
        case 3: gg = x; bb = chroma; break;
        case 4: rr = x; bb = chroma; break;
        default: rr = chroma; bb = x; break;
    }
    auto out = [&](float f) { return static_cast<GLubyte>(std::clamp((f + m) * 255.f + 0.5f, 0.f, 255.f)); };
    return { out(rr), out(gg), out(bb) };
}

inline ccColor4B themeColor4(ccColor4B c) {
    auto t = themeColor({ c.r, c.g, c.b });
    return { t.r, t.g, t.b, c.a };
}

// ---------------------------------------------------------------------------------------------
// Adaptive buffer: places you keep dying get a little extra buffer; it fades again as you clear
// them. Works in 3% buckets of the level and only lives for the current session of a level.
// ---------------------------------------------------------------------------------------------
constexpr int ADAPT_BUCKETS = 34;

struct AdaptiveState {
    std::array<int, ADAPT_BUCKETS> bonus {};
    std::array<int, ADAPT_BUCKETS> deaths {};
    std::array<int, ADAPT_BUCKETS> clears {};
    int lastBucket = -1;
};

inline AdaptiveState adaptive;

// The buckets are remembered per level (a text list in the saved data).
inline void adaptiveLoad(std::string const& levelKey) {
    adaptive = {};
    if (levelKey.empty()) return;
    std::string text = Mod::get()->getSavedValue<std::string>("adaptive-" + levelKey, "");
    size_t pos = 0;
    int index = 0;
    while (pos < text.size() && index < ADAPT_BUCKETS) {
        size_t end = text.find(',', pos);
        if (end == std::string::npos) break;
        try {
            adaptive.bonus[index] = std::clamp(std::stoi(text.substr(pos, end - pos)), 0, 8);
        }
        catch (...) {
        }
        ++index;
        pos = end + 1;
    }
}

inline void adaptiveSave(std::string const& levelKey) {
    if (levelKey.empty() || !studio.adaptiveOn) return;
    std::string text;
    bool any = false;
    for (int value : adaptive.bonus) {
        text += fmt::format("{},", value);
        any = any || value > 0;
    }
    Mod::get()->setSavedValue("adaptive-" + levelKey, any ? text : std::string());
}

inline int adaptBucket(float percent) {
    return std::clamp(static_cast<int>(percent) / 3, 0, ADAPT_BUCKETS - 1);
}

inline void adaptiveReset() {
    adaptive = {};
}

// extra delay, in 240-ticks, to add at this level percentage
inline int adaptiveBonusAt(float percent) {
    if (!studio.adaptiveOn) return 0;
    int bucket = adaptBucket(percent);
    int best = 0;
    for (int d = -1; d <= 1; ++d) {
        best = std::max(best, adaptive.bonus[std::clamp(bucket + d, 0, ADAPT_BUCKETS - 1)]);
    }
    return std::min(best, studio.adaptiveMax);
}

inline void adaptiveDeath(float percent) {
    if (!studio.adaptiveOn) return;
    int bucket = adaptBucket(percent);
    adaptive.clears[bucket] = 0;
    if (++adaptive.deaths[bucket] >= 3) {
        adaptive.deaths[bucket] = 0;
        adaptive.bonus[bucket] = std::min(adaptive.bonus[bucket] + 1, studio.adaptiveMax);
    }
    adaptive.lastBucket = -1;
}

inline void adaptiveProgress(float percent) {
    if (!studio.adaptiveOn) return;
    int bucket = adaptBucket(percent);
    if (adaptive.lastBucket >= 0 && bucket > adaptive.lastBucket) {
        for (int i = adaptive.lastBucket; i < bucket; ++i) {
            adaptive.deaths[i] = std::max(0, adaptive.deaths[i] - 1);
            if (++adaptive.clears[i] >= 2 && adaptive.bonus[i] > 0) {
                --adaptive.bonus[i];
                adaptive.clears[i] = 0;
            }
        }
    }
    adaptive.lastBucket = bucket;
}

// ---------------------------------------------------------------------------------------------
// Performance guard (optional): when frames get long, the look-ahead shrinks and the path preview
// refreshes less often. It steps back up once frames are short again.
// ---------------------------------------------------------------------------------------------
struct PerfGuardState {
    float avgMs = 0.f;
    int level = 0;
    int cooldown = 0;
};

inline PerfGuardState perfGuard;

inline float perfGuardScale() {
    if (!studio.perfGuardOn) return 1.f;
    static constexpr float SCALE[3] = { 1.f, 0.7f, 0.45f };
    return SCALE[std::clamp(perfGuard.level, 0, 2)];
}

inline void perfGuardTick(float frameMs) {
    if (!studio.perfGuardOn) {
        perfGuard.level = 0;
        perfGuard.avgMs = 0.f;
        return;
    }
    perfGuard.avgMs = perfGuard.avgMs <= 0.f ? frameMs : perfGuard.avgMs * 0.97f + frameMs * 0.03f;
    if (--perfGuard.cooldown > 0) return;
    perfGuard.cooldown = 90;
    if (perfGuard.avgMs > static_cast<float>(studio.perfGuardMs)) {
        perfGuard.level = std::min(perfGuard.level + 1, 2);
    }
    else if (perfGuard.avgMs < static_cast<float>(studio.perfGuardMs) * 0.7f) {
        perfGuard.level = std::max(perfGuard.level - 1, 0);
    }
}

// ---------------------------------------------------------------------------------------------
// Presets: six named slots that hold every tuning value of the studio. Stored as plain
// "key=value;" text, so they survive updates and need no JSON handling.
// ---------------------------------------------------------------------------------------------
constexpr int PRESET_SLOTS = 6;

// type: i = saved int, b = saved bool, I = setting int, B = setting bool
inline std::vector<std::pair<std::string, char>> studioPresetKeys() {
    std::vector<std::pair<std::string, char>> keys;
    auto add = [&](std::string key, char type) { keys.emplace_back(std::move(key), type); };
    for (char const* key : { "studio-alt-max-delay", "studio-alt-samples", "studio-alt-tolerance", "studio-alt-margin",
             "studio-alt-burst-gap", "studio-alt-wave-look", "studio-alt-wave-min-gain", "studio-alt-wave-horizon",
             "studio-sf-strength", "studio-sf-gap", "studio-sf-dead", "studio-click-bias", "studio-orb-margin",
             "studio-swing-look", "studio-adaptive-max", "studio-perf-guard-ms", "studio-frame-spam-cps", "studio-alt-smooth-pct", "studio-alt-plan-width", "studio-alt-safe-keep",
             "studio-alt-room-levels", "studio-ship-guard-from", "studio-ship-guard-to", "studio-path-horizon",
             "studio-path-refresh-ms" }) {
        add(key, 'i');
    }
    for (char const* key : { "studio-alt-enabled", "studio-alt-wave", "studio-alt-ship", "studio-alt-ball",
             "studio-alt-ufo", "studio-alt-swing", "studio-sf-enabled", "studio-progress-stage-bands",
             "studio-adaptive", "studio-orb-conf", "studio-perf-guard", "studio-p2-buffer", "studio-alt-plan", "studio-alt-smooth", "studio-path-forecast", "studio-ship-guard", "studio-movers-auto", "studio-modes-linked", "studio-frame-spam", "studio-path-enabled",
             "studio-path-show-idle", "studio-path-show-click" }) {
        add(key, 'b');
    }
    for (int band = 1; band <= PROGRESS_BANDS; ++band) {
        for (char const* field : { "start", "end", "stage" }) {
            add(fmt::format("studio-band{}-{}", band, field), 'i');
        }
    }
    for (int i = 0; i < SM_COUNT; ++i) add(STUDIO_MODES[i].key, 'I');
    for (int i = 0; i < SO_COUNT; ++i) add(STUDIO_ORBS[i].key, 'I');
    for (char const* key : { "wave-release-window", "robot-release-window", "dash-release-window" }) add(key, 'I');
    add("orb-ticks-linked", 'B');
    add("sim-budget-ms", 'D');
    add("ground-horizon", 'I');
    add("wave-horizon", 'I');
    add("performance-mode", 'B');
    add("robust-saves", 'B');
    return keys;
}

inline std::string studioPresetSnapshot() {
    auto* mod = Mod::get();
    std::string out;
    for (auto const& [key, type] : studioPresetKeys()) {
        switch (type) {
            case 'i': {
                int64_t value = mod->getSavedValue<int64_t>(key, INT64_MIN);
                if (value != INT64_MIN) out += fmt::format("{}={};", key, value);
                break;
            }
            case 'b': out += fmt::format("{}={};", key, mod->getSavedValue<bool>(key, false) ? 1 : 0); break;
            case 'I': out += fmt::format("{}={};", key, mod->getSettingValue<int64_t>(key)); break;
            case 'B': out += fmt::format("{}={};", key, mod->getSettingValue<bool>(key) ? 1 : 0); break;
            case 'D': out += fmt::format("{}={:.2f};", key, mod->getSettingValue<double>(key)); break;
            default: break;
        }
    }
    return out;
}

inline void studioPresetApply(std::string const& text) {
    auto* mod = Mod::get();
    std::unordered_map<std::string, char> types;
    for (auto const& [key, type] : studioPresetKeys()) types[key] = type;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(';', pos);
        if (end == std::string::npos) end = text.size();
        std::string item = text.substr(pos, end - pos);
        pos = end + 1;
        size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        std::string key = item.substr(0, eq);
        auto found = types.find(key);
        if (found == types.end()) continue;
        int64_t value = 0;
        double number = 0.0;
        try {
            number = std::stod(item.substr(eq + 1));
            value = static_cast<int64_t>(std::llround(number));
        }
        catch (...) {
            continue;
        }
        switch (found->second) {
            case 'i': mod->setSavedValue<int64_t>(key, value); break;
            case 'b': mod->setSavedValue<bool>(key, value != 0); break;
            case 'I': mod->setSettingValue<int64_t>(key, value); break;
            case 'B': mod->setSettingValue<bool>(key, value != 0); break;
            case 'D': mod->setSettingValue<double>(key, number); break;
            default: break;
        }
    }
}

// Built-in presets. Every one sets the same list of values (gamemode delays, orb ticks, release windows,
// alternate assist, path view, prediction horizons, budget, guard), so choosing one changes the whole setup.
// 0 = Safe (small delays, careful), 1 = Balanced, 2 = Aggressive (big delays), 3 = Performance (cheap to run).
// Only the alternate-assist numbers (the "Assist preset" buttons on the Path + Alt tab).
// 0 = Safe, 1 = Balanced, 2 = Aggressive, 3 = Performance (cheapest).
inline std::string studioAssistPreset(int index) {
    index = std::clamp(index, 0, 3);
    auto pick = [index](int safe, int balanced, int aggressive, int performance) {
        int values[4] = { safe, balanced, aggressive, performance };
        return values[index];
    };
    std::string text;
    auto add = [&text](std::string const& key, int64_t value) { text += fmt::format("{}={};", key, value); };
    add("studio-alt-max-delay", pick(4, 8, 12, 6));
    add("studio-alt-samples", pick(4, 3, 3, 3));
    add("studio-alt-tolerance", pick(2, 3, 4, 3));
    add("studio-alt-margin", pick(2, 1, 1, 1));
    add("studio-alt-burst-gap", 30);
    add("studio-alt-wave-look", pick(14, 12, 12, 8));
    add("studio-alt-wave-min-gain", 1);
    add("studio-alt-wave-horizon", pick(120, 96, 96, 48));
    add("studio-alt-plan", pick(1, 1, 1, 0));
    add("studio-alt-smooth", pick(1, 1, 1, 1));
    add("studio-alt-smooth-pct", pick(100, 100, 80, 100));
    add("studio-alt-plan-width", pick(3, 4, 6, 2));
    add("studio-alt-safe-keep", pick(4, 3, 3, 2));
    add("studio-alt-room-levels", pick(5, 4, 3, 3));
    return text;
}

inline std::string studioBuiltinPreset(int index) {
    index = std::clamp(index, 0, 3);
    auto pick = [index](int safe, int balanced, int aggressive, int performance) {
        int values[4] = { safe, balanced, aggressive, performance };
        return values[index];
    };
    std::string text;
    auto add = [&text](std::string const& key, int64_t value) { text += fmt::format("{}={};", key, value); };

    // gamemode delays (240/s ticks; 4 ticks = 1 frame)
    int ground = pick(8, 16, 32, 8);
    int ship = pick(2, 4, 8, 2);
    int wave = pick(8, 8, 16, 8);
    for (int i = 0; i < SM_COUNT; ++i) {
        int ticks = i == SM_WAVE ? wave : (i == SM_SHIP ? ship : ground);
        add(STUDIO_MODES[i].key, std::clamp(ticks, 0, STUDIO_MODES[i].maxTicks));
    }
    // orbs and release windows
    for (int i = 0; i < SO_COUNT; ++i) add(STUDIO_ORBS[i].key, pick(8, 16, 24, 8));
    add("orb-ticks-linked", 1);
    for (char const* key : { "wave-release-window", "robot-release-window", "dash-release-window" }) {
        add(key, pick(16, 32, 48, 16));
    }
    // alternate assist (same numbers as the assist preset buttons)
    text += studioAssistPreset(index);
    add("studio-click-bias", 0);
    add("studio-orb-margin", pick(2, 1, 1, 1));
    add("studio-swing-look", pick(48, 48, 48, 32));
    add("studio-adaptive", 0);
    add("studio-adaptive-max", 4);
    add("studio-p2-buffer", 1);
    // path view
    add("studio-path-enabled", 1);
    add("studio-path-show-idle", 1);
    add("studio-path-show-click", 0);
    add("studio-path-horizon", pick(160, 120, 120, 60));
    add("studio-path-refresh-ms", pick(24, 24, 24, 60));
    // prediction cost
    text += fmt::format("sim-budget-ms={:.2f};", static_cast<double>(pick(10, 8, 8, 4)));
    add("ground-horizon", pick(160, 130, 130, 80));
    add("wave-horizon", pick(80, 65, 65, 40));
    add("robust-saves", pick(1, 1, 1, 0));
    add("performance-mode", pick(0, 0, 0, 1));
    add("studio-perf-guard", pick(0, 0, 0, 1));
    add("studio-perf-guard-ms", 16);
    return text;
}
