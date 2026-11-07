// Synca popups: "what's new" after an update, the first-run welcome, and the "new version" notice.
// This is the first file split out of main.cpp: it only needs Geode and the mod's own settings, so it
// is its own translation unit (faster rebuilds, and a compiler problem here cannot affect the engine).

#include "synca.hpp"
#include <Geode/modify/MenuLayer.hpp>
#include <chrono>
#include <string>

using namespace geode::prelude;

namespace {
    bool welcomeQueued = false;

    // What's new, shown once after the mod is updated (not on a first install).
    constexpr char const* SYNCA_CHANGELOG =
        "- Gap centering for 1-block gaps\n"
        "- Smarter alternate wave scan\n"
        "- Glossy menu with colour themes\n"
        "- Studio tab: HUD, presets, click bias\n"
        "- Orb confidence dot, adaptive buffer\n"
        "- Progress bands with custom stages";

    void showWelcomePopupIfNeeded() {
        auto* mod = Mod::get();
        if (welcomeQueued) return;
        std::string current = mod->getVersion().toVString();
        std::string seen = mod->getSavedValue<std::string>("studio-last-seen-version", "");
        bool welcomeSeen = mod->getSavedValue<bool>("studio-welcome-seen", false);
        bool showChangelog = !seen.empty() ? seen != current : welcomeSeen;
        if (welcomeSeen && !showChangelog) return;
        welcomeQueued = true;
        queueInMainThread([showChangelog, current] {
            auto* mod = Mod::get();
            auto* scene = CCScene::get();
            if (!scene || !scene->getChildByID("MenuLayer")) return;
            if (!mod->getSavedValue<bool>("studio-welcome-seen", false)) {
                mod->setSavedValue("studio-welcome-seen", true);
                mod->setSavedValue("studio-last-seen-version", current);
                char const* message =
                    "Press 7 to open Synca. Choose a tab to adjust timing, and use Help for control details.\n"
                    "4 ticks = 1 frame at 60 FPS. Automatic input assists are off by default outside Practice.\n"
                    "Synca is an assist tool: do not use it for records or submissions where assists are not allowed.";
                FLAlertLayer::create("Welcome to Synca", message, "Got it")->show();
                return;
            }
            if (showChangelog) {
                mod->setSavedValue("studio-last-seen-version", current);
                FLAlertLayer::create(nullptr, fmt::format("What's new in Synca {}", current).c_str(), SYNCA_CHANGELOG,
                    "Got it", nullptr, 330.f)->show();
            }
        });
    }

    // Separate popup: a newer release exists. Needs a GitHub "owner/repo" in the mod settings.
    // Off by default (SYNCA_UPDATE_CHECK 0) so the first build is simple; set it to 1 afterwards.
    #ifndef SYNCA_UPDATE_CHECK
    #define SYNCA_UPDATE_CHECK 0   // set to 1 to turn the update popup on (needs Geode's web API)
    #endif
    bool updateCheckStarted = false;

    void checkForUpdateOnce() {
    #if SYNCA_UPDATE_CHECK
        if (updateCheckStarted) return;
        updateCheckStarted = true;
        auto* mod = Mod::get();
        if (!mod->getSettingValue<bool>("check-updates")) return;
        std::string repo = mod->getSettingValue<std::string>("update-repo");
        if (repo.empty() || repo.find('/') == std::string::npos) return;
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now - mod->getSavedValue<int64_t>("studio-update-check-time", 0) < 6 * 3600) return;
        mod->setSavedValue("studio-update-check-time", now);
        geode::async::spawn(
            geode::utils::web::WebRequest()
                .userAgent("Synca-update-check")
                .timeout(std::chrono::seconds(10))
                .get("https://api.github.com/repos/" + repo + "/releases/latest"),
            [repo](geode::utils::web::WebResponse response) {
                if (!response.ok()) return;
                std::string body = response.string().unwrapOr("");
                size_t key = body.find("\"tag_name\"");
                if (key == std::string::npos) return;
                size_t open = body.find('"', body.find(':', key) + 1);
                size_t close = open == std::string::npos ? open : body.find('"', open + 1);
                if (open == std::string::npos || close == std::string::npos) return;
                std::string tag = body.substr(open + 1, close - open - 1);
                auto latest = geode::VersionInfo::parse(tag);
                auto* mod = Mod::get();
                if (!latest.isOk() || !(latest.unwrap() > mod->getVersion())) return;
                if (mod->getSavedValue<std::string>("studio-update-notified", "") == tag) return;
                mod->setSavedValue("studio-update-notified", tag);
                std::string url = "https://github.com/" + repo + "/releases/latest";
                std::string current = mod->getVersion().toVString();
                queueInMainThread([tag, url, current] {
                    geode::createQuickPopup("Synca update available",
                        fmt::format("Version {} is out (you have {}).\nDownload the latest version?", tag, current),
                        "Later", "Download", [url](FLAlertLayer*, bool download) {
                            if (download) geode::utils::web::openLinkInBrowser(url);
                        });
                });
            });
    #endif
    }


}

class $modify(BufMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }
        showWelcomePopupIfNeeded();
        checkForUpdateOnce();
        synca::addMainMenuButton(this);
        return true;
    }
};
