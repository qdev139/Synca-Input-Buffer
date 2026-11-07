// Synca shared definitions: the engine state, the studio settings and the small helpers that more than
// one source file needs. main.cpp holds the engine, menu.cpp the menu, popups.cpp the popups.
#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/SliderNode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace synca {
    constexpr float TICK = 0.25f;
    constexpr float PANEL_W = 220.f;
    constexpr float PANEL_H = 112.f;

    struct Stage {
        bool enabled;
        int ticksAll;
        int ticksWave;
        bool custom;
    };

    constexpr Stage STAGES[] = {
        { true, 2, 2, false },
        { true, 4, 4, false },
        { true, 8, 8, false },
        { true, 16, 8, false },
        { true, 24, 12, false },
        { true, 32, 16, false },
        { true, 0, 0, true },
        { false, 0, 0, false },
    };
    constexpr int STAGE_COUNT = sizeof(STAGES) / sizeof(STAGES[0]);

    struct QueuedInput {
        int ticksLeft;
        bool down;
    };

    struct ObjSnapshot {
        GameObject* object;
        bool isActivated;
        EffectGameObject* effect;
        bool activated;
        bool byP1;
        bool byP2;
    };

    struct Buffer {
        PlayerObject* ghost1 = nullptr;
        PlayerObject* ghost2 = nullptr;
        CCLayer* ghostLayer = nullptr;
        unsigned ghostLayerBase = 0;  // v2.24: children the ghost layer has right after the ghosts are made

        CCNode* panel = nullptr;
        CCLabelBMFont* panelTitle = nullptr;               // current stage
        CCLabelBMFont* panelPerf = nullptr;                // compact cost of the latest real input
        std::vector<CCLabelBMFont*> panelLegend;            // path colors explained in the terminal
        std::vector<CCLabelBMFont*> panelLines;            // one label per log line (colored)
        bool panelOpen = false;  // closed by default, remembered for the session
        bool panelDirty = false; // v2.71: new log lines, the panel is redrawn once on the next step
        bool perfMode = false;   // v2.71: performance mode
        double userBudget = 6.0; // v2.75: the budget from the settings (budgetMs is this click's)
        bool autoBudget = true;  // v2.75: budget follows the frame rate
        std::unordered_set<GameObject*> simDisabled;  // v2.75: objects a toggle orb switched off in this prediction
        std::deque<std::string> logLines;

        bool simulating = false;
        bool injecting = false;
        bool ghostDied = false;
        bool ghostHitPortal = false;
        bool realInput = false;
        bool appliedInput = false;
        bool isPlayer1 = true;

        bool enabled = true;
        int stage = 0;
        int ticksAll = 2;
        int ticksWave = 2;

        std::deque<QueuedInput> queue;
        int shift = 0;  // delay applied to inputs while the queue is active

        float tickLength = TICK;
        int sinceInput = 999;
        int rapid = 0;
        int altRapid = 0; // separate burst counter; USKC can have longer uneven phase gaps
        int altPredictionEvery = -1; // ghost-only alternating forecast; never emits real input
        std::array<int, 7> altDownSamples{};
        std::array<int, 7> altUpSamples{};
        int altDownSampleCount = 0, altUpSampleCount = 0;
        int altDownSampleNext = 0, altUpSampleNext = 0;
        int altDownEvery = 7, altUpEvery = 7;
        int altWaveDelay = 0; // last successful edge offset, preferred on the next edge
        int altNextEdgeShift = 0;  // planning: how far the ghost moves its FIRST forecast edge (ticks, may be negative)
        int altPredictionDownEvery = -1; // ghost-only press-hold cadence for wave steering
        int altPredictionUpEvery = -1;   // ghost-only release cadence for wave steering
        std::array<int, 7> altPredictionDownPattern{};
        std::array<int, 7> altPredictionUpPattern{};
        int altPredictionDownPatternCount = 0;
        int altPredictionUpPatternCount = 0;

        bool smart = true;
        bool console = false;
        double budgetMs = 6.0;
        int groundHorizon = 130;     // v2.1: how far ahead cube/ball/spider predictions look
        int releaseShift = 0;        // v2.1: delay of the last delivered press, reused for its release
        int waveHorizon = 65;        // v2.2: how far ahead wave predictions look (mini = 15 less)
        bool assumeOrbs = true;      // v2.2: the ghost clicks every orb it touches ("calculation mode")
        bool held1 = false, held2 = false;   // button state the ghosts are meant to be in
        bool auto1 = false, auto2 = false;   // an orb auto-click is currently held on this ghost
        bool orbTiming = true;       // v2.26: orb clicks at specific timings inside the orb
        int orbClickDelay = 0;       // v2.26: ghost orb auto-clicks wait this many ticks into the orb
        int autoAt1 = -1, autoAt2 = -1;      // v2.26: tick a delayed orb auto-click fires (-1 none, -2 done)
        bool orbWasTiming = false;   // v2.26: the last orb save re-timed a press that was already in the orb

        bool tryBothOrbs = true;     // v2.4: if a run touches an orb and dies, retry with the other orb choice
        bool orbClick = true;        // v2.4: orb choice of the current run
        bool touchedOrb = false;     // v2.4: the current run touched an orb
        bool skipPress = false;      // v2.4: run without pressing anything (auto-release hold check)
        bool avoidMovers = true;     // v2.4: don't buffer when the ghost's path has moving objects
        bool sawMover = false;       // v2.4: a run this decision touched a moving object
        bool trackMovers = false;    // v2.5.1: watch objects only in this run (not in every search run)
        bool autoRelease = false;    // v2.4: wave auto-release (practice mode only)
        int autoWindow = 6;          // auto-click lookahead (ticks)
        int waveReleaseWindow = 6;
        int robotReleaseWindow = 6;
        int dashReleaseWindow = 6;
        bool autoReleased = false;   // v2.4: we released for you; your real release is swallowed
        bool autoClick = false;      // v2.79: click for you when idling dies and a click is fully safe
        bool autoClicked = false;    // v2.79: we clicked for you; your own click right after is swallowed
        bool swallowRelease = false; // v2.79: the release of that swallowed click is swallowed too
        int autoClickAge = 0;        // v2.79: ticks since the auto click
        int spamStreak = 3;          // v2.79: fast inputs in a row that count as spam (setting)
        bool portalPredict = true;   // v2.79: ghosts switch mode at mode portals instead of stopping
        bool ghostSwitched = false;  // v2.79: the ghost changed mode through a portal in this run
        int tps = 240;               // v2.79: physics steps per second (setting)
        PlayLayer* owner = nullptr;  // v2.86: the level the mod has set up (null while one loads)
        bool haveStartRects = false; // v2.79: realRect1/2 hold where you are as this prediction starts (portal check)
        int spamGap = 16;            // v2.5.2: inputs closer than this (ticks) count toward wave spam
        bool waveMargin = true;      // v2.10: center wave saves in the safe window (1 tick of room)
        bool waveV46 = false;        // v2.7: use the old v4.6 wave rules instead of the cube-style ones
        bool orbAlign = true;        // v2.6: hold an early press back until the ghost is inside the orb
        bool orbAlignGround = false; // v2.11.1: also align presses made on the ground (jumps)
        bool ignoreTouching = true;  // v2.31: hazards you touch right now without dying can't kill the ghost
        bool haveRealRect = false;
        CCRect realRect1, realRect2;  // your hitboxes when the prediction starts
        std::unordered_set<GameObject*> harmless;  // hazards on you at the start of this prediction
        int baseEscape = -1;         // v2.36: press-now run only survives if you react by this tick
        int baseCrash = -1;          // v2.36: ...otherwise it crashes here
        bool lowLatency = false;     // v2.35: never chain delays, small budget and max delay
        bool explain = false;        // v2.33: log why an input was passed through untouched
        bool deathLog = false;       // v2.32: log what kills the press-now ghost on every no-save
        bool captureKiller = false;  // v2.30: record what kills the ghost in the press-now run
        bool baseTouchedDown = false;  // v2.63: the press-now ghost was back on the ground at some point
        bool killerSeen = false;
        int killerTick = -1;
        int killerId = 0, killerType = -1;
        bool killerNoTouch = false, killerDisabled = false;
        CCRect killerRect, ghostDeathRect;
        bool robustSaves = true;     // v2.28: a save must also survive with you slightly off
        float ghostShiftX = 0.f;     // v2.28: horizontal offset applied to the ghost's start
        int lateCrash = 90;          // v2.27: cube / ball crashes after this many ticks aren't this click's (0 = off)
        bool predictMovers = false;  // v2.27: move objects along their current motion during predictions
        bool shipBuffer = false;     // v2.25: buffer ship inputs too
        bool waveDirectSpam = false; // User toggle: pass wave inputs through without buffering
        int shipSpamGap = 6;         // v2.44: ship feathering is normal control, only taps closer than this are spam
        int shipMaxDelay = 2;        // v2.25: ship's own max delay (240/s ticks)
        bool perfLog = false;        // v2.21: log time / runs / ghost ticks per click
        int simRuns = 0;             // v2.21: predictions run for the current click
        long simTicks = 0;           // v2.21: ghost ticks stepped for the current click
        double lastPerfMs = 0.0;
        int lastPerfRuns = 0;
        long lastPerfTicks = 0;
        bool cbfSync = true;         // v2.18: line the ghost up with Click Between Frames' mid-tick clicks
        float firstStep = 1.f;       // v2.18: fraction of a tick the ghost's first step lasts
        bool inRealUpdate = false;   // v2.18: inside P1's physics step (a CBF click lands here)
        float tickStartX = 0.f;      // v2.18: P1's x at the start of the current tick
        float tickDx = 0.f;          // v2.18: how far P1 moved in the last full tick
        bool dashAutoRelease = false;  // v2.16: let go of a dash for you when it would crash
        bool autoDash = false;       // v2.16: the current auto-release ended a dash
        std::unordered_set<GameObject*> orbsUsed;  // v2.16: orbs the ghost actually used this run
        bool moreTime = true;        // v2.43: at spots you keep dying, delay to give you more time
        bool ballSmart = true;       // v2.45: ball landing rules + spam guard
        bool swingSmart = true;      // v2.51: swing "could you have flipped back" scoring + spam guard
        bool train = false;          // v2.51: training mode: predict, never delay, show how early you were
        bool perLevel = true;        // v2.51: stage + switches remembered per level
        bool heatmap = true;         // v2.51: death marks on the progress bar
        bool saveDing = true;        // v2.56: ding when a click is saved
        CCLabelBMFont* hintLabel = nullptr;  // v2.51: training hint ("2t early")
        CCDrawNode* heatNode = nullptr;      // v2.51: death marks on the progress bar
        bool ufoSmart = true;        // v2.40: UFO landing + "could you have flapped again" scoring
        bool spiderSmart = true;     // v2.15: spider-aware landing + "could you have flipped back" scoring
        int retapAt = -1;            // v2.15: ticks after the input at which the ghost taps again (-1 = never)
        bool robotSmart = true;      // v2.12: robot presses count as safe if some hold length survives
        bool robotAutoRelease = false;  // v2.12: robot auto-release (practice mode only)
        bool autoNormalRuns = false; // v2.14: let auto-release work outside practice too
        CCLabelBMFont* autoTag = nullptr;  // v2.14: on-screen "AUTO-RELEASE ON" tag
        CCLabelBMFont* watermark = nullptr;  // always-on Synca label while the buffer is on
        CCDrawNode* orbDot = nullptr;        // orb confidence dot: green / yellow / red
        int orbConf = 0;                     // 0 none, 1 red, 2 yellow, 3 green
        int orbConfAge = 100000;             // ticks since the last orb decision
        int orbConfShown = -1;
        int lastAppliedEdgeTick = -1000000;  // tick the game last received a press / release (yours or the assist's)
        uint32_t cbfHistory = 0;             // last clicks: 1 = arrived in the middle of a physics step (Click Between Frames)
        int cbfSeen = 0;                     // how many clicks that history covers (up to 16)
        int attemptTick = 0;                 // tick the current attempt started
        CCLabelBMFont* funHud = nullptr;         // the Fun tab's info lines
        CCLabelBMFont* funFlash = nullptr;       // "NEW BEST", "SPOT CLEARED" ...
        CCLabelBMFont* nextModeLabel = nullptr;  // "NEXT: WAVE 3x in 1.2s" (from the level scan)
        int autoTagLast = -1000000;          // tick of the last auto input (the tag is brightest right after one)
        bool usedThisAttempt = false;      // v2.46: the buffer was on at some point of this attempt
        bool learn = true;           // v2.13: per-level memory learned from your runs
        bool fullState = true;       // v2.11.1: copy the full v2.8 player state into the ghost
        int orbWindow = 16;          // v2.6: how far ahead (ticks) an orb may be for that
        int simTick = 0;             // v2.6: tick of the current run
        int firstOrbTick = -1;       // v2.6: first tick the current run touched an orb
        int lastOrbTick = -1;        // v2.78: last tick the current run was inside an orb
        bool orbAfterFirst = false;  // v2.78: orb alignment: once your click used an orb, click the later ones too
        bool laterOrbTouched = false;  // v2.78: the run reached another orb after the one your click used
        int orbUsedTick = -1;        // v2.6: first tick the current run actually used an orb
        bool driftCheck = false;     // v2.4: log how far the real player strays from the prediction
        std::vector<CCPoint>* recordPath = nullptr;  // v2.4: ghost1 positions of the current run
        int* recordPathCollision = nullptr;          // path-only: continue the trace after its first hit
        bool recordThroughHits = false;
        bool showPath = false;       // v2.11: draw the predicted path (F10)
        CCDrawNode* pathNode = nullptr;  // v2.11: owned by the level's object layer
        CCDrawNode* keepNode = nullptr;  // the last delayed click's route, kept on screen for a moment
        int keepTick = -1000000;
        int pathHoldUntil = 0;       // v2.65: live path waits while a click's path is on screen
        std::chrono::steady_clock::time_point lastPathUpdate{};

        std::unordered_set<GameObject*> simUsed;
        std::vector<ObjSnapshot> snapshots;
    };

    inline Buffer buf;

#include "synca_state.hpp"

    // Two-player levels: Player 2's input stream (queue, rhythm, held state) lives in its own slot and is
    // swapped into `buf` only while a Player 2 input or tick is handled, so every existing function keeps
    // working on `buf` unchanged. Single-player and dual-mode levels never touch it.
    struct P2State {
        bool active = false;      // Player 2's stream is swapped in right now
        bool needsReset = true;   // a new attempt / stage change: start Player 2 from the game's own state
    };

    inline P2State g_p2;

    // STAGES[] ends with the OFF stage; the per-gamemode stage is the one flagged `custom`.
    constexpr int STUDIO_STAGE_OFF = STAGE_COUNT - 1;
    constexpr int STUDIO_STAGE_CUSTOM = 6;

    // Frame spam ("CPS cap"): while you spam the wave through a ~1 block gap, the assist takes over and sends a
    // perfectly even spam (one edge every few ticks), centred in the gap, until you stop spamming.
    struct FrameSpamState {
        bool owned = false;           // the assist is driving the wave
        bool holding = false;         // the state it last sent
        bool checked = false;         // the centring check for the pending edge is done
        int nextEdge = 0;             // tick the next edge is due
        int period = 4;               // ticks between edges right now (shrinks in small gaps)
        int lastRealEdge = -1000000;  // last tick you really clicked or released while it was driving
        double periodExact = 8.0;     // edge spacing in ticks, fractional (16 CPS = 7.5 ticks between edges)
        double edgeTime = 0.0;        // when the next edge is due, fractional, so the average rate stays exact
    };
    inline FrameSpamState frameSpam;

    // What the clicks really do (not just whether a CBF mod is loaded: Mega Hack has its own CBF).
    inline std::string cbfStatusText() {
        if (buf.cbfSeen < 3) {
            return "CBF ?";
        }
        uint32_t mask = buf.cbfSeen >= 16 ? 0xFFFFu : ((1u << buf.cbfSeen) - 1u);
        int mid = static_cast<int>(std::bitset<32>(buf.cbfHistory & mask).count());
        return mid > 0 ? fmt::format("CBF ON ({}/{})", mid, buf.cbfSeen) : std::string("CBF OFF");
    }

    // Level scan: counts the level's objects and triggers in short slices (it never reads per-object group
    // data, which is what crashed on huge levels). A level with move / rotate / follow / scale triggers is treated
    // as "has moving hazards".
    // One stretch of the level between portals: which gamemode, speed, size and dual state you are in there.
    struct LevelSection {
        float startX = 0.f;
        float endX = 0.f;
        int mode = 0;          // SM_CUBE ... SM_SWING
        float speed = 0.9f;    // the game's speed value: 0.7 / 0.9 / 1.1 / 1.3 / 1.6 = 0.5x / 1x / 2x / 3x / 4x
        bool mini = false;
        bool dual = false;
        int hazards = 0;
        int solids = 0;
        bool busy = false;     // much more crowded than the level's average (spam, tight corridors)
    };

    struct LevelScan {
        bool done = false;
        int tries = 0;          // how many times it found no objects yet (it keeps looking)
        unsigned pos = 0;       // where the current scan has got to
        unsigned total = 0;     // objects in the level
        int objects = 0;
        int hazards = 0;
        int moving = 0;         // move / rotate / follow / scale triggers
        int toggled = 0;        // toggle triggers
        std::string summary;
        // the gamemode timeline (built from the portals once every object was looked at)
        float lastX = 0.f;                              // the right-most object: roughly the end of the level
        bool startKnown = false;
        int startMode = 0;
        float startSpeed = 0.9f;
        bool startMini = false;
        bool startDual = false;
        std::vector<std::pair<float, int>> portals;     // x, object id of every gamemode / speed / size / dual portal
        std::vector<uint16_t> hazardBuckets;            // hazards per 30 units of x
        std::vector<uint16_t> solidBuckets;             // blocks / slopes per 30 units of x
        std::vector<LevelSection> sections;
    };
    inline LevelScan levelScan;

    inline char const* scanModeName(int mode) {
        static constexpr char const* NAMES[SM_COUNT] = { "CUBE", "SHIP", "BALL", "UFO", "WAVE", "ROBOT", "SPIDER", "SWING" };
        return NAMES[std::clamp(mode, 0, SM_COUNT - 1)];
    }

    inline char const* scanSpeedName(float speed) {
        if (speed < 0.8f) return "0.5x";
        if (speed < 1.f) return "1x";
        if (speed < 1.2f) return "2x";
        if (speed < 1.45f) return "3x";
        return "4x";
    }

    // "WAVE 3x mini dual"
    inline std::string sectionName(LevelSection const& section) {
        return fmt::format("{} {}{}{}", scanModeName(section.mode), scanSpeedName(section.speed), section.mini ? " mini" : "",
            section.dual ? " dual" : "");
    }

    // "41%  WAVE 3x mini  (busy)"
    inline std::string sectionText(LevelSection const& section, float levelEnd) {
        int percent = levelEnd > 0.f ? std::clamp(static_cast<int>(section.startX / levelEnd * 100.f), 0, 100) : 0;
        return fmt::format("{}%  {}{}", percent, sectionName(section), section.busy ? "  (busy)" : "");
    }

    struct MenuTelemetry {
        int altEdges;
        int altAdjusted;
        int saves;
        size_t spots;
    };

    struct MenuTheme {
        ccColor3B main, soft, accent, bg;
    };

    // ---- engine entry points (defined in main.cpp)
    void sayLine(std::string const& message);
    template <class... Args>
    void say(fmt::format_string<Args...> f, Args&&... args) {
        sayLine(fmt::format(f, std::forward<Args>(args)...));
    }
    int st(int ticks240);
    void applyStage();
    void readSettings();
    void resetPending();
    void resetAlternateRhythm();
    void refreshPanel();
    void refreshWatermark();
    void refreshAutoTag();
    void refreshHeatmap();
    void applyHudLayout();
    void refreshOrbDot();
    void sendButton(PlayLayer* layer, bool down);
    void saveMemory();
    void saveProfile();
    void exportConsole();
    void updateProgressStageBand(PlayLayer* layer);
    void cycleStage(int dir = 1);
    void studioSetStage(int stage);
    void forgetLevelMemory();
    std::string levelHardestText();
    void scanLevel(PlayLayer* layer);
    void ensureLevelScan(PlayLayer* layer);
    void endFrameSpam(PlayLayer* layer, char const* why);
    void requestRunCheck();
    MenuTelemetry sessionTelemetry();
    std::string stageText();
    std::string statsText();

    // ---- menu entry points (defined in menu.cpp)
    void openMenu();
    void addMainMenuButton(CCNode* parent);   // creates the draggable button that shows on every screen (menu_button.cpp)
    void moveFloatingButtonTo(CCScene* scene);   // fallback only (when Geode's OverlayManager is missing)
    void wakeMenuButton();                    // shows it again (the pause menu opened)
    MenuTheme currentMenuTheme();

// =============================================================================================
// Synca UI look ("neon"): near-black at the top fading into bright violet at the bottom, a rim that runs
// from light pink (top-left) to almost black (bottom), a soft glow in the middle, light rays from the
// top-left corner and thin white circuit lines with a light running along them. All of it is drawn with
// CCDrawNode (no textures), so it stays sharp at any size and follows the colour theme.
// =============================================================================================
inline std::vector<CCPoint> gelShape(float x, float y, float w, float h, float rTop, float rBottom) {
    rTop = std::clamp(rTop, 0.f, std::min(w, h) * 0.5f);
    rBottom = std::clamp(rBottom, 0.f, std::min(w, h) * 0.5f);
    constexpr int STEPS = 7;
    constexpr float HALF_PI = 1.5707963f;
    std::vector<CCPoint> pts;
    pts.reserve(4 * (STEPS + 1));
    auto arc = [&](float cx, float cy, float r, float start) {
        if (r < 0.5f) {
            pts.push_back({ cx, cy });
            return;
        }
        for (int i = 0; i <= STEPS; ++i) {
            float angle = start + HALF_PI * static_cast<float>(i) / STEPS;
            pts.push_back({ cx + r * std::cos(angle), cy + r * std::sin(angle) });
        }
    };
    // counter-clockwise: bottom-right, top-right, top-left, bottom-left
    arc(x + w - rBottom, y + rBottom, rBottom, -HALF_PI);
    arc(x + w - rTop, y + h - rTop, rTop, 0.f);
    arc(x + rTop, y + h - rTop, rTop, HALF_PI);
    arc(x + rBottom, y + rBottom, rBottom, 2.f * HALF_PI);
    return pts;
}

inline void gelPoly(CCDrawNode* draw, std::vector<CCPoint>& pts, ccColor4F color) {
    if (draw && pts.size() >= 3) {
        draw->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), color, 0.f, { 0.f, 0.f, 0.f, 0.f });
    }
}

inline ccColor4F gelTint(ccColor4B c, float lift) {
    auto mix = [&](int v) {
        float f = v / 255.f;
        f = lift >= 0.f ? f + (1.f - f) * lift : f * (1.f + lift);
        return std::clamp(f, 0.f, 1.f);
    };
    return { mix(c.r), mix(c.g), mix(c.b), c.a / 255.f };
}

inline ccColor3B neonMix(ccColor3B a, ccColor3B b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return { static_cast<GLubyte>(a.r + (b.r - a.r) * t + 0.5f), static_cast<GLubyte>(a.g + (b.g - a.g) * t + 0.5f),
        static_cast<GLubyte>(a.b + (b.b - a.b) * t + 0.5f) };
}

// For additive nodes (blend SRC_ALPHA, ONE): the colour as it is, the alpha is the strength.
inline ccColor4F neonF(ccColor3B c, float alpha = 1.f) {
    return { c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha };
}

// For the normal blend of CCDrawNode, which is PREMULTIPLIED (the game's blend is ONE, ONE_MINUS_SRC_ALPHA): a
// see-through colour needs its red, green and blue already multiplied by the alpha. (Passing plain white with a
// small alpha draws solid white.)
inline ccColor4F neonP(ccColor3B c, float alpha) {
    return { c.r / 255.f * alpha, c.g / 255.f * alpha, c.b / 255.f * alpha, alpha };
}

// A round dot made of a polygon (drawDot can come out square in the game).
inline void neonDisc(CCDrawNode* draw, CCPoint centre, float radius, ccColor4F color, int segments = 14) {
    if (!draw || radius <= 0.f) return;
    std::vector<CCPoint> pts;
    pts.reserve(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        pts.push_back({ centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle) });
    }
    draw->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), color, 0.f, { 0.f, 0.f, 0.f, 0.f });
}

// The big panels' gradient, top (0) to bottom (1), recoloured by the theme.
inline ccColor3B neonBody(float t) {
    struct Stop {
        float at;
        ccColor3B color;
    };
    static constexpr Stop STOPS[6] = { { 0.00f, { 12, 5, 15 } }, { 0.22f, { 30, 12, 42 } }, { 0.45f, { 62, 26, 82 } },
        { 0.70f, { 101, 28, 150 } }, { 0.88f, { 125, 14, 196 } }, { 1.00f, { 146, 2, 234 } } };
    t = std::clamp(t, 0.f, 1.f);
    for (int i = 0; i < 5; ++i) {
        if (t <= STOPS[i + 1].at) {
            float u = (t - STOPS[i].at) / (STOPS[i + 1].at - STOPS[i].at);
            return themeColor(neonMix(STOPS[i].color, STOPS[i + 1].color, u));
        }
    }
    return themeColor(STOPS[5].color);
}

// keep the part of a convex polygon where y <= limit
inline std::vector<CCPoint> neonClipBelow(std::vector<CCPoint> const& poly, float limit) {
    std::vector<CCPoint> out;
    size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        CCPoint a = poly[i];
        CCPoint b = poly[(i + 1) % n];
        bool aIn = a.y <= limit;
        bool bIn = b.y <= limit;
        if (aIn) out.push_back(a);
        if (aIn != bIn) {
            float t = (limit - a.y) / (b.y - a.y);
            out.push_back({ a.x + (b.x - a.x) * t, limit });
        }
    }
    return out;
}

// Sutherland-Hodgman: the subject polygon cut to a convex, counter-clockwise clip polygon.
inline std::vector<CCPoint> neonClip(std::vector<CCPoint> subject, std::vector<CCPoint> const& clip) {
    for (size_t i = 0; i < clip.size() && !subject.empty(); ++i) {
        CCPoint a = clip[i];
        CCPoint b = clip[(i + 1) % clip.size()];
        auto side = [&](CCPoint p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
        std::vector<CCPoint> out;
        for (size_t j = 0; j < subject.size(); ++j) {
            CCPoint p = subject[j];
            CCPoint q = subject[(j + 1) % subject.size()];
            float dp = side(p);
            float dq = side(q);
            bool pIn = dp >= 0.f;
            bool qIn = dq >= 0.f;
            if (pIn) out.push_back(p);
            if (pIn != qIn) {
                float t = dp / (dp - dq);
                out.push_back({ p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t });
            }
        }
        subject = std::move(out);
    }
    return subject;
}

inline void neonFill(CCDrawNode* draw, std::vector<CCPoint> pts, ccColor4F color) {
    if (draw && pts.size() >= 3) {
        draw->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), color, 0.f, { 0.f, 0.f, 0.f, 0.f });
    }
}

// Vertical gradient inside a convex shape: the whole shape in the top colour, then the part below each step line
// painted over it in the next colour (opaque layers, so there are no seams).
inline void neonGradient(CCDrawNode* draw, std::vector<CCPoint> const& shape, float yBottom, float yTop,
                         std::function<ccColor3B(float)> colorAt, int steps) {
    steps = std::max(1, steps);
    for (int k = 0; k < steps; ++k) {
        float t = (static_cast<float>(k) + 0.5f) / static_cast<float>(steps);
        float limit = yTop - (yTop - yBottom) * static_cast<float>(k) / static_cast<float>(steps);
        neonFill(draw, k == 0 ? shape : neonClipBelow(shape, limit), neonF(colorAt(t)));
    }
}

// The older translucent look; still used for see-through panels (the terminal, the watermark).
inline CCDrawNode* gelPanelFlat(float w, float h, ccColor4B base, float radius = -1.f, bool shine = true) {
    auto* draw = CCDrawNode::create();
    if (!draw) return nullptr;
    base = themeColor4(base);
    float r = radius >= 0.f ? radius : std::clamp(std::min(w, h) * 0.28f, 4.f, 13.f);
    r = std::min(r, std::min(w, h) * 0.5f);
    draw->setContentSize({ w, h });
    draw->ignoreAnchorPointForPosition(false);
    draw->setAnchorPoint({ 0.5f, 0.5f });
    auto outer = gelShape(0.f, 0.f, w, h, r, r);
    gelPoly(draw, outer, gelTint(base, 0.30f));
    float rim = (w > 24.f && h > 14.f) ? 1.2f : 0.8f;
    float ri = std::max(0.f, r - rim);
    float bw = w - rim * 2.f;
    float bh = h - rim * 2.f;
    auto body = gelShape(rim, rim, bw, bh, ri, ri);
    gelPoly(draw, body, gelTint(base, 0.06f));
    int caps = std::clamp(static_cast<int>(bh / 9.f), 3, 7);
    for (int i = 0; i < caps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(caps);
        float capH = std::max(bh * (1.f - 0.80f * t), ri + 1.f);
        capH = std::min(capH, bh);
        bool full = capH >= bh - 0.01f;
        auto cap = gelShape(rim, rim, bw, capH, full ? ri : 0.f, ri);
        gelPoly(draw, cap, { 0.f, 0.f, 0.f, 0.085f * base.a / 255.f });
    }
    if (shine && h >= 14.f && w >= 20.f) {
        float inset = std::max(2.f, r * 0.45f);
        float gh = bh * 0.40f;
        float gr = std::max(1.f, std::min(ri - inset * 0.5f, gh * 0.5f));
        auto sheen = gelShape(rim + inset, rim + bh - inset - gh, bw - inset * 2.f, gh, gr, gr * 0.6f);
        gelPoly(draw, sheen, { 0.075f, 0.075f, 0.075f, 0.075f });
        auto sheenTop = gelShape(rim + inset, rim + bh - inset - gh * 0.5f, bw - inset * 2.f, gh * 0.5f, gr, gr * 0.5f);
        gelPoly(draw, sheenTop, { 0.06f, 0.06f, 0.06f, 0.06f });
    }
    return draw;
}

// Cards, buttons, switches, number boxes: near-black violet on top, deeper violet at the bottom (brighter for
// buttons and ON switches, judged from the colour the caller passes), with a pink-to-dark rim and a soft sheen.
// Centre-anchored, content size = w x h.
inline CCDrawNode* gelPanel(float w, float h, ccColor4B base, float radius = -1.f, bool shine = true) {
    if (base.a < 250) {
        return gelPanelFlat(w, h, base, radius, shine);
    }
    auto* draw = CCDrawNode::create();
    if (!draw) return nullptr;
    float r = radius >= 0.f ? radius : std::clamp(std::min(w, h) * 0.28f, 4.f, 13.f);
    r = std::min(r, std::min(w, h) * 0.5f);
    draw->setContentSize({ w, h });
    draw->ignoreAnchorPointForPosition(false);
    draw->setAnchorPoint({ 0.5f, 0.5f });

    float v = std::max({ base.r, base.g, base.b }) / 255.f;
    float lit = std::clamp((v - 0.17f) / 0.50f, 0.f, 1.f);   // 0 = dark card, 1 = bright button / ON switch
    ccColor3B topC = themeColor(neonMix({ 15, 7, 21 }, { 40, 16, 64 }, lit));
    ccColor3B botC = themeColor(neonMix({ 50, 21, 76 }, { 150, 36, 238 }, lit));
    ccColor3B rimTop = themeColor(neonMix({ 150, 92, 160 }, { 253, 190, 239 }, 0.35f + 0.65f * lit));
    ccColor3B rimBot = themeColor(neonMix({ 22, 10, 28 }, { 100, 36, 164 }, lit));

    float rim = (w > 24.f && h > 14.f) ? 1.4f : 0.9f;
    auto outer = gelShape(0.f, 0.f, w, h, r, r);
    neonGradient(draw, outer, 0.f, h, [&](float t) { return neonMix(rimTop, rimBot, t); }, std::clamp(static_cast<int>(h / 6.f), 3, 8));
    float ri = std::max(0.f, r - rim);
    auto body = gelShape(rim, rim, w - rim * 2.f, h - rim * 2.f, ri, ri);
    neonGradient(draw, body, rim, h - rim, [&](float t) { return neonMix(topC, botC, std::pow(t, 1.25f)); },
        std::clamp(static_cast<int>(h / 4.f), 4, 14));
    if (shine && h >= 14.f && w >= 20.f) {
        float inset = std::max(2.f, r * 0.45f);
        float gh = (h - rim * 2.f) * 0.36f;
        float gr = std::max(1.f, std::min(ri - inset * 0.5f, gh * 0.5f));
        auto sheen = gelShape(rim + inset, h - rim - inset - gh, w - rim * 2.f - inset * 2.f, gh, gr, gr * 0.6f);
        gelPoly(draw, sheen, { 0.055f, 0.055f, 0.055f, 0.055f });
    }
    return draw;
}

// The two big side panels. Static: the gradient body, thin white circuit lines that leave through one side and
// the pink-to-dark rim. Moving (30 times a second): the glow in the middle, light rays from the top-left corner,
// drifting sparkles and a light that runs along the circuit lines.
// `side` = 1: the circuit lines leave through the right edge, -1: through the left edge.
class NeonPanel : public CCNode {
    struct Mote {
        float x, y, speed, phase, size;
    };
    float m_w = 0.f;
    float m_h = 0.f;
    int m_side = 1;
    std::vector<CCPoint> m_shape;
    CCDrawNode* m_fx = nullptr;
    std::vector<Mote> m_motes;
    std::vector<std::vector<CCPoint>> m_paths;
    std::vector<float> m_lengths;
    float m_time = 0.f;
    float m_accum = 0.f;

    // polyline with its corners rounded (quadratic curves)
    static std::vector<CCPoint> fillet(std::vector<CCPoint> const& pts, float radius) {
        if (pts.size() < 3) return pts;
        std::vector<CCPoint> out;
        out.push_back(pts.front());
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            CCPoint p = pts[i];
            CCPoint prev = pts[i - 1];
            CCPoint next = pts[i + 1];
            float r = std::min({ radius, ccpDistance(prev, p) * 0.5f, ccpDistance(next, p) * 0.5f });
            CCPoint dirPrev = ccpNormalize(prev - p);
            CCPoint dirNext = ccpNormalize(next - p);
            CCPoint a = p + dirPrev * r;
            CCPoint b = p + dirNext * r;
            for (int k = 0; k <= 8; ++k) {
                float u = static_cast<float>(k) / 8.f;
                float inv = 1.f - u;
                out.push_back({ inv * inv * a.x + 2.f * inv * u * p.x + u * u * b.x, inv * inv * a.y + 2.f * inv * u * p.y + u * u * b.y });
            }
        }
        out.push_back(pts.back());
        return out;
    }

    CCPoint pointAt(std::vector<CCPoint> const& path, float distance) const {
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            float len = ccpDistance(path[i], path[i + 1]);
            if (distance <= len || i + 2 == path.size()) {
                float u = len > 0.001f ? std::clamp(distance / len, 0.f, 1.f) : 0.f;
                return { path[i].x + (path[i + 1].x - path[i].x) * u, path[i].y + (path[i + 1].y - path[i].y) * u };
            }
            distance -= len;
        }
        return path.empty() ? CCPoint { 0.f, 0.f } : path.back();
    }

public:
    static NeonPanel* create(float w, float h, int side) {
        auto* ret = new NeonPanel();
        if (ret->init(w, h, side)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(float w, float h, int side) {
        if (!CCNode::init()) return false;
        m_w = w;
        m_h = h;
        m_side = side >= 0 ? 1 : -1;
        this->setContentSize({ w, h });
        this->ignoreAnchorPointForPosition(false);
        this->setAnchorPoint({ 0.5f, 0.5f });
        float r = 14.f;
        m_shape = gelShape(0.f, 0.f, w, h, r, r);

        // 1) body
        auto* body = CCDrawNode::create();
        neonGradient(body, m_shape, 0.f, h, [](float t) { return neonBody(t); }, std::clamp(static_cast<int>(h / 8.f), 24, 90));
        this->addChild(body, 0);

        // 2) moving light (additive)
        m_fx = CCDrawNode::create();
        m_fx->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
        this->addChild(m_fx, 1);

        // 3) circuit lines: horizontal, a rounded step, horizontal again; they leave through one side
        auto X = [&](float x) { return m_side > 0 ? x : w - x; };
        float yA = h * 0.80f;
        float yB = h * 0.28f;
        m_paths.push_back(fillet({ { X(w - 24.f), yA }, { X(w + 34.f), yA }, { X(w + 34.f), yA - 20.f }, { X(w + 92.f), yA - 20.f } }, 11.f));
        m_paths.push_back(fillet({ { X(w - 24.f), yB }, { X(w + 24.f), yB }, { X(w + 24.f), yB + 18.f }, { X(w + 66.f), yB + 18.f } }, 10.f));
        auto* lines = CCDrawNode::create();
        ccColor3B lineGlow = themeColor({ 255, 190, 250 });
        for (auto const& path : m_paths) {
            float length = 0.f;
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                lines->drawSegment(path[i], path[i + 1], 3.0f, neonP(lineGlow, 0.10f));
                lines->drawSegment(path[i], path[i + 1], 0.95f, { 0.92f, 0.92f, 0.92f, 0.92f });
                length += ccpDistance(path[i], path[i + 1]);
            }
            m_lengths.push_back(length);
            neonDisc(lines, path.back(), 4.2f, { 0.95f, 0.95f, 0.95f, 0.95f });
            neonDisc(lines, path.back(), 2.0f, neonF(neonBody(0.15f), 1.f), 10);
            neonDisc(lines, path.front(), 2.2f, { 0.9f, 0.9f, 0.9f, 0.9f }, 10);
        }
        this->addChild(lines, 2);

        // 4) rim: light pink at the top-left, almost black at the bottom
        auto* border = CCDrawNode::create();
        ccColor3B rimLight = themeColor({ 253, 190, 239 });
        ccColor3B rimDark = themeColor({ 12, 7, 11 });
        auto edge = gelShape(1.8f, 1.8f, w - 3.6f, h - 3.6f, r - 1.8f, r - 1.8f);
        for (size_t i = 0; i < edge.size(); ++i) {
            CCPoint a = edge[i];
            CCPoint b = edge[(i + 1) % edge.size()];
            int pieces = std::max(1, static_cast<int>(std::ceil(ccpDistance(a, b) / 7.f)));
            for (int k = 0; k < pieces; ++k) {
                float u0 = static_cast<float>(k) / static_cast<float>(pieces);
                float u1 = static_cast<float>(k + 1) / static_cast<float>(pieces);
                CCPoint p = { a.x + (b.x - a.x) * u0, a.y + (b.y - a.y) * u0 };
                CCPoint q = { a.x + (b.x - a.x) * u1, a.y + (b.y - a.y) * u1 };
                float mx = (p.x + q.x) * 0.5f;
                float my = (p.y + q.y) * 0.5f;
                float t = std::clamp(0.72f * (1.f - my / h) + 0.28f * (mx / w), 0.f, 1.f);
                border->drawSegment(p, q, 1.8f, neonF(neonMix(rimLight, rimDark, std::pow(t, 0.8f)), 1.f));
            }
        }
        this->addChild(border, 3);

        // sparkles
        uint32_t seed = static_cast<uint32_t>(w * 31.f + h * 17.f);
        auto random = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(seed >> 8) / 16777216.f;
        };
        for (int i = 0; i < 12; ++i) {
            m_motes.push_back({ 14.f + random() * (w - 28.f), 10.f + random() * (h - 20.f), 5.f + random() * 9.f, random() * 6.28f, 0.8f + random() * 1.4f });
        }

        redraw();
        this->schedule(schedule_selector(NeonPanel::tick));
        return true;
    }

    void tick(float dt) {
        m_time += dt;
        m_accum += dt;
        if (m_accum < 0.033f) return;
        float step = m_accum;
        m_accum = 0.f;
        for (auto& mote : m_motes) {
            mote.y += mote.speed * step;
            if (mote.y > m_h - 8.f) mote.y = 8.f;
        }
        redraw();
    }

    void redraw() {
        if (!m_fx) return;
        m_fx->clear();
        float t = m_time;
        // glow in the middle: nested ellipses, additive
        ccColor3B glow = themeColor({ 176, 88, 226 });
        CCPoint centre { m_w * 0.5f, m_h * (0.36f + 0.02f * std::sin(t * 0.55f)) };
        constexpr int RINGS = 14;
        for (int k = RINGS; k >= 1; --k) {
            float f = static_cast<float>(k) / RINGS;
            float rx = m_w * 0.47f * f;
            float ry = m_h * 0.27f * f;
            std::vector<CCPoint> ring;
            for (int i = 0; i < 28; ++i) {
                float angle = 6.2831853f * static_cast<float>(i) / 28.f;
                ring.push_back({ centre.x + rx * std::cos(angle), centre.y + ry * std::sin(angle) });
            }
            neonFill(m_fx, ring, neonF(glow, 0.017f * (1.f + 0.30f * std::sin(t * 0.9f))));
        }
        // soft light rays from the top-left corner; each ray is cut into bands that get fainter with distance
        CCPoint origin { 8.f, m_h - 8.f };
        static constexpr float REACH[5] = { 0.f, 0.20f, 0.38f, 0.56f, 0.72f };
        static constexpr float FADE[4] = { 1.f, 0.62f, 0.34f, 0.14f };
        ccColor3B rayC = themeColor({ 255, 226, 250 });
        for (int i = 0; i < 4; ++i) {
            float angle = (-60.f - 11.f * static_cast<float>(i) + 3.5f * std::sin(t * 0.30f + static_cast<float>(i) * 1.3f)) * 0.0174533f;
            float half = (2.6f + 0.9f * static_cast<float>(i)) * 0.0174533f;
            float strength = 0.040f + 0.026f * (0.5f + 0.5f * std::sin(t * 0.8f + static_cast<float>(i) * 1.9f));
            auto dir = [&](float a, float r) { return CCPoint { origin.x + std::cos(a) * r, origin.y + std::sin(a) * r }; };
            for (int band = 0; band < 4; ++band) {
                float r0 = m_h * REACH[band];
                float r1 = m_h * REACH[band + 1] + 1.f;
                std::vector<CCPoint> trapezoid = { dir(angle - half, r0), dir(angle + half, r0), dir(angle + half, r1), dir(angle - half, r1) };
                neonFill(m_fx, neonClip(trapezoid, m_shape), neonF(rayC, strength * FADE[band]));
            }
        }
        // sparkles
        ccColor3B sparkle = themeColor({ 255, 232, 255 });
        for (auto const& mote : m_motes) {
            float twinkle = 0.5f + 0.5f * std::sin(t * 2.0f + mote.phase);
            neonDisc(m_fx, { mote.x + 4.f * std::sin(t * 0.7f + mote.phase), mote.y }, mote.size, neonF(sparkle, 0.10f + 0.34f * twinkle), 8);
        }
        // the light that runs along the circuit lines
        ccColor3B lightC = themeColor({ 255, 236, 252 });
        ccColor3B haloC = themeColor({ 255, 160, 240 });
        for (size_t p = 0; p < m_paths.size(); ++p) {
            float u = std::fmod(t * 0.28f + static_cast<float>(p) * 0.37f, 1.f);
            float distance = u * m_lengths[p];
            for (int k = 6; k >= 0; --k) {
                float back = std::max(0.f, distance - static_cast<float>(k) * 9.f);
                CCPoint pos = pointAt(m_paths[p], back);
                float fade = 1.f - static_cast<float>(k) / 7.f;
                neonDisc(m_fx, pos, 9.f * fade + 2.f, neonF(haloC, 0.10f * fade), 12);
                neonDisc(m_fx, pos, 3.0f * fade + 0.8f, neonF(lightC, 0.85f * fade), 10);
            }
        }
    }
};

// The highlight behind the selected tab: a bright gradient pill with a soft glow and a shine that sweeps across.
class NeonPill : public CCNode {
    float m_w = 0.f;
    float m_h = 0.f;
    std::vector<CCPoint> m_shape;
    CCDrawNode* m_fx = nullptr;
    float m_time = 0.f;
    float m_accum = 0.f;

public:
    static NeonPill* create(float w, float h) {
        auto* ret = new NeonPill();
        if (ret->init(w, h)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(float w, float h) {
        if (!CCNode::init()) return false;
        m_w = w;
        m_h = h;
        float r = std::min(13.f, h * 0.3f);
        m_shape = gelShape(0.f, 0.f, w, h, r, r);
        this->setContentSize({ w, h });
        this->ignoreAnchorPointForPosition(false);
        this->setAnchorPoint({ 0.5f, 0.5f });
        auto* glow = CCDrawNode::create();
        glow->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
        ccColor3B glowC = themeColor({ 220, 110, 255 });
        for (int i = 4; i >= 1; --i) {
            auto ring = gelShape(-1.8f * i, -1.8f * i, w + 3.6f * i, h + 3.6f * i, r + 1.8f * i, r + 1.8f * i);
            glow->drawPolygon(ring.data(), static_cast<unsigned int>(ring.size()), { 0.f, 0.f, 0.f, 0.f }, 1.5f, neonF(glowC, 0.20f - 0.04f * i));
        }
        this->addChild(glow, -1);
        {
            auto* body = CCDrawNode::create();
            ccColor3B rimTop = themeColor({ 255, 214, 247 });
            ccColor3B rimBot = themeColor({ 176, 76, 238 });
            auto outer = gelShape(0.f, 0.f, w, h, r, r);
            neonGradient(body, outer, 0.f, h, [&](float t) { return neonMix(rimTop, rimBot, t); }, 6);
            ccColor3B top = themeColor({ 122, 56, 196 });
            ccColor3B bottom = themeColor({ 214, 108, 255 });
            auto inner = gelShape(1.6f, 1.6f, w - 3.2f, h - 3.2f, std::max(0.f, r - 1.6f), std::max(0.f, r - 1.6f));
            neonGradient(body, inner, 1.6f, h - 1.6f, [&](float t) { return neonMix(top, bottom, std::pow(t, 1.1f)); }, 10);
            float gh = (h - 3.2f) * 0.38f;
            auto sheen = gelShape(5.f, h - 3.2f - 3.f - gh, w - 10.f, gh, std::max(1.f, r - 4.f), 3.f);
            gelPoly(body, sheen, { 0.10f, 0.10f, 0.10f, 0.10f });
            this->addChild(body, 0);
        }
        m_fx = CCDrawNode::create();
        m_fx->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
        this->addChild(m_fx, 1);
        this->schedule(schedule_selector(NeonPill::tick));
        return true;
    }

    void tick(float dt) {
        if (!this->isVisible()) return;
        m_time += dt;
        m_accum += dt;
        if (m_accum < 0.033f) return;
        m_accum = 0.f;
        m_fx->clear();
        ccColor3B inner = themeColor({ 255, 170, 240 });
        auto breathe = gelShape(2.f, 2.f, m_w - 4.f, m_h - 4.f, 10.f, 10.f);
        neonFill(m_fx, breathe, neonF(inner, 0.04f + 0.035f * std::sin(m_time * 2.2f)));
        float cycle = std::fmod(m_time, 3.4f);
        if (cycle < 0.95f) {
            float progress = cycle / 0.95f;
            float cx = -0.25f * m_w + progress * 1.5f * m_w;
            std::vector<CCPoint> band = { { cx - 12.f, 0.f }, { cx + 10.f, 0.f }, { cx + 36.f, m_h }, { cx + 14.f, m_h } };
            neonFill(m_fx, neonClip(band, m_shape), neonF(themeColor({ 255, 240, 255 }), 0.30f * std::sin(progress * 3.14159f)));
        }
    }
};


}  // namespace synca
