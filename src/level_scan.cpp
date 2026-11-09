// Synca level scan: once per level load it finds the moving and toggled solids / hazards and turns the portals into
// the level's gamemode timeline. Split out of main.cpp (v1.8) so it compiles on its own, in parallel with the engine.
#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

#include "synca.hpp"

namespace synca {
    // Level scan: once per level load. Finds the moving and toggled solids / hazards (through the move,
    // rotate, follow, scale and toggle triggers that target their groups), so that movers can be watched
    // before the ghost ever touches them. If this part does not compile with your Geode version, set
    // SYNCA_LEVEL_SCAN to 0 and send me the error.
#ifndef SYNCA_LEVEL_SCAN
#define SYNCA_LEVEL_SCAN 1
#endif
    // One slice of the scan (about 1 ms at most, so even a level with millions of objects never hitches).
    // Returns true once every object has been looked at.
    // Turns the portals into the level's gamemode timeline and marks the crowded parts.
    void buildLevelSections() {
        auto& scan = levelScan;
        scan.sections.clear();
        std::sort(scan.portals.begin(), scan.portals.end(),
            [](std::pair<float, int> const& a, std::pair<float, int> const& b) { return a.first < b.first; });
        LevelSection current;
        current.startX = 0.f;
        current.mode = scan.startMode;
        current.speed = scan.startSpeed;
        current.mini = scan.startMini;
        current.dual = scan.startDual;
        auto finish = [&](float x) {
            current.endX = x;
            if (current.endX - current.startX < 30.f && !scan.sections.empty()) {
                scan.sections.back().endX = x;   // a sliver between two portals: part of the previous stretch
            }
            else if (current.endX > current.startX) {
                scan.sections.push_back(current);
            }
        };
        for (auto const& [x, id] : scan.portals) {
            LevelSection next = current;
            switch (id) {
                case 12: next.mode = SM_CUBE; break;
                case 13: next.mode = SM_SHIP; break;
                case 47: next.mode = SM_BALL; break;
                case 111: next.mode = SM_UFO; break;
                case 660: next.mode = SM_WAVE; break;
                case 745: next.mode = SM_ROBOT; break;
                case 1331: next.mode = SM_SPIDER; break;
                case 1933: next.mode = SM_SWING; break;
                case 101: next.mini = true; break;
                case 99: next.mini = false; break;
                case 286: next.dual = true; break;
                case 287: next.dual = false; break;
                default: {
                    float speed = speedPortalValue(id);
                    if (speed > 0.f) next.speed = speed;
                    break;
                }
            }
            bool changed = next.mode != current.mode || std::abs(next.speed - current.speed) > 0.01f || next.mini != current.mini ||
                next.dual != current.dual;
            if (!changed) {
                continue;
            }
            finish(x);
            current = next;
            current.startX = x;
        }
        finish(std::max(scan.lastX, current.startX + 30.f));

        // how crowded each stretch is (hazards and blocks per 100 units), compared with the whole level
        auto sumBuckets = [](std::vector<uint16_t> const& buckets, float from, float to) {
            int first = std::max(0, static_cast<int>(from / 30.f));
            int last = std::min(static_cast<int>(buckets.size()), static_cast<int>(to / 30.f) + 1);
            int total = 0;
            for (int i = first; i < last; ++i) total += buckets[i];
            return total;
        };
        double everything = 0.0;
        for (auto& section : scan.sections) {
            section.hazards = sumBuckets(scan.hazardBuckets, section.startX, section.endX);
            section.solids = sumBuckets(scan.solidBuckets, section.startX, section.endX);
            everything += section.hazards + section.solids;
        }
        double average = scan.lastX > 0.f ? everything / (scan.lastX / 100.0) : 0.0;
        for (auto& section : scan.sections) {
            float length = section.endX - section.startX;
            double density = length > 0.f ? (section.hazards + section.solids) / (length / 100.0) : 0.0;
            section.busy = length >= 90.f && average > 0.0 && density > average * 1.6;
        }
    }

    bool scanStep(PlayLayer* layer) {
#if SYNCA_LEVEL_SCAN
        if (!layer || !layer->m_objects) {
            return false;
        }
        auto* list = layer->m_objects;
        unsigned total = list->count();
        if (total == 0) {
            return false;   // the level's objects are not there yet
        }
        levelScan.total = total;
        if (!levelScan.startKnown && layer->m_player1) {
            // where the level starts: the gamemode, speed, size and dual state you begin in
            auto* starter = layer->m_player1;
            levelScan.startKnown = true;
            levelScan.startMode = studioModeOf(starter);
            levelScan.startSpeed = starter->m_playerSpeed;
            levelScan.startMini = starter->m_vehicleSize < 1.f;
            levelScan.startDual = layer->m_gameState.m_isDualMode;
        }
        auto bump = [](std::vector<uint16_t>& buckets, int index) {
            if (index >= static_cast<int>(buckets.size())) buckets.resize(static_cast<size_t>(index) + 1, 0);
            if (buckets[index] < 65535) ++buckets[index];
        };
        auto started = std::chrono::steady_clock::now();
        unsigned processed = 0;
        while (levelScan.pos < list->count()) {   // checked every time: the list may change while the level starts
            auto* object = static_cast<GameObject*>(list->objectAtIndex(levelScan.pos));
            ++levelScan.pos;
            if (!object) {
                continue;
            }
            ++levelScan.objects;
            auto type = object->m_objectType;
            float objectX = static_cast<float>(object->m_positionX);
            if (objectX > levelScan.lastX) {
                levelScan.lastX = objectX;
            }
            int bucket = std::clamp(static_cast<int>(objectX / 30.f), 0, 200000);
            if (type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard) {
                ++levelScan.hazards;
                bump(levelScan.hazardBuckets, bucket);
            }
            else if (type == GameObjectType::Solid || type == GameObjectType::Slope) {
                bump(levelScan.solidBuckets, bucket);
            }
            int id = object->m_objectID;
            bool portal = id == 12 || id == 13 || id == 47 || id == 111 || id == 660 || id == 745 || id == 1331 || id == 1933 ||
                id == 99 || id == 101 || id == 286 || id == 287 || speedPortalValue(id) > 0.f;
            if (portal && levelScan.portals.size() < 20000) {
                levelScan.portals.emplace_back(objectX, id);
            }
            if (id == 901 || id == 1346 || id == 1347 || id == 2067) {   // move, rotate, follow, scale triggers
                ++levelScan.moving;
            }
            else if (id == 1049) {                                       // toggle triggers
                ++levelScan.toggled;
            }
            if ((++processed & 2047u) == 0 &&
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() > 1.0) {
                break;
            }
        }
        if (levelScan.pos < list->count()) {
            return false;   // more slices to come
        }
        levelScan.done = true;
        buildLevelSections();
        levelScan.summary = fmt::format("{} objects | {} hazards | {} sections | {} move triggers", levelScan.objects,
            levelScan.hazards, levelScan.sections.size(), levelScan.moving);
        say("level scan: {}", levelScan.summary);
        if (levelScan.moving > 0 && !buf.predictMovers) {
            if (studio.moversAuto) {
                buf.predictMovers = true;
                say("this level has moving objects: predicting them automatically (Studio > Auto assists)");
            }
            else {
                say("this level has moving objects: 'Predict moving hazards' (Studio > Auto assists) lets the ghost follow them");
            }
        }
        return true;
#else
        (void)layer;
        return false;
#endif
    }

    // Starts a fresh scan (used by the RESCAN button and when a level loads).
    void scanLevel(PlayLayer* layer) {
        int attempts = levelScan.tries + 1;
        levelScan = {};
        levelScan.tries = attempts;
        scanStep(layer);
    }

    // The scan used to run only once, inside the level's init, where the objects often do not exist yet (it then
    // found nothing and never looked again; other mods that change when a level loads made it seem to "only work"
    // sometimes). Now it keeps looking, twice a second, until the objects are there (or about 30 seconds passed).
    void ensureLevelScan(PlayLayer* layer) {
#if SYNCA_LEVEL_SCAN
        if (!layer || levelScan.done || levelScan.tries >= 240) {
            return;
        }
        static int cooldown = 0;
        if (--cooldown > 0) {
            return;
        }
        cooldown = 6;
        if (!scanStep(layer) && levelScan.total == 0) {
            if (++levelScan.tries == 240) {
                say("level scan: no level objects were found");
            }
        }
#else
        (void)layer;
#endif
    }
}  // namespace synca
