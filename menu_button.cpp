// Synca button: a small draggable button that shows on every screen (title screen, menus, in a level, the pause
// menu) and opens the Synca menu.
// - drag it anywhere (the spot is remembered)
// - tap it to open the menu
// - after 3 seconds without touching it, it fades out completely; touching where it was brings it back
// - in a level that is running (not paused) a faded button lets your touches through, so it can never eat a jump;
//   it comes back when you open the pause menu, or on the next screen
// It lives across scenes, so it is created once.
#include "synca.hpp"

// Geode's OverlayManager keeps a node on top of every screen. (The game's own SceneManager is not in the bindings
// of this Geode version; if OverlayManager is missing too, a fallback below re-attaches the button on each scene change.)
#if __has_include(<Geode/ui/OverlayManager.hpp>)
#include <Geode/ui/OverlayManager.hpp>
#define SYNCA_HAS_OVERLAY 1
#else
#include <Geode/modify/CCDirector.hpp>
#define SYNCA_HAS_OVERLAY 0
#endif

namespace synca {
namespace {

// A level is running (a PlayLayer exists and the pause menu is not open).
bool inActiveLevel() {
    if (!GameManager::get()->m_playLayer) {
        return false;
    }
    auto* scene = CCDirector::get()->getRunningScene();
    return !(scene && scene->getChildByType<PauseLayer>(0));
}

class FloatingButton : public CCLayer {
    static constexpr float FADE_AFTER = 3.f;    // seconds without touching before it disappears
    static constexpr float SIZE = 46.f;         // button size in points
    static constexpr float DRAG_START = 8.f;    // how far a finger must move before it counts as a drag

    CCSprite* m_icon = nullptr;
    CCSprite* m_halo = nullptr;     // soft glow behind the icon
    bool m_touching = false;
    bool m_dragging = false;
    bool m_faded = false;
    bool m_enabled = true;
    float m_idle = 0.f;
    float m_settingTimer = 0.f;
    CCPoint m_down { 0.f, 0.f };
    CCPoint m_grab { 0.f, 0.f };

    CCPoint clampToScreen(CCPoint pos) const {
        auto win = CCDirector::get()->getWinSize();
        float margin = SIZE / 2.f + 4.f;
        pos.x = std::clamp(pos.x, margin, win.width - margin);
        pos.y = std::clamp(pos.y, margin, win.height - margin);
        return pos;
    }

    bool hit(CCPoint point) const {
        auto box = m_icon->boundingBox();
        box.origin.x -= 14.f;
        box.origin.y -= 14.f;
        box.size.width += 28.f;
        box.size.height += 28.f;
        return box.containsPoint(point);
    }

    void savePosition() {
        auto win = CCDirector::get()->getWinSize();
        auto pos = m_icon->getPosition();
        Mod::get()->setSavedValue("menu-button-x", static_cast<double>(pos.x / win.width));
        Mod::get()->setSavedValue("menu-button-y", static_cast<double>(pos.y / win.height));
    }

public:
    static FloatingButton* create() {
        auto* ret = new FloatingButton();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCLayer::init()) {
            return false;
        }
        auto win = CCDirector::get()->getWinSize();
        this->setContentSize(win);
        // above the normal menu buttons and the level's own touch handling, below pop-ups
        this->setTouchPriority(kCCMenuHandlerPriority - 20);
        this->setTouchEnabled(true);

        m_icon = CCSprite::create("synca-mark.png"_spr);
        if (!m_icon) {
            return false;
        }
        auto size = m_icon->getContentSize();
        m_icon->setScale(SIZE / std::max(1.f, std::max(size.width, size.height)));
        double fx = Mod::get()->getSavedValue<double>("menu-button-x", -1.0);
        double fy = Mod::get()->getSavedValue<double>("menu-button-y", -1.0);
        CCPoint start = (fx >= 0.0 && fy >= 0.0)
            ? CCPoint { static_cast<float>(fx * win.width), static_cast<float>(fy * win.height) }
            : CCPoint { win.width - 34.f, win.height - 34.f };   // top-right corner until you move it
        m_icon->setPosition(clampToScreen(start));
        m_halo = CCSprite::create("sparkle.png"_spr);
        if (m_halo) {
            m_halo->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
            m_halo->setColor({ 190, 90, 255 });
            m_halo->setOpacity(150);
            m_halo->setScale(1.1f);
            m_halo->setPosition(m_icon->getPosition());
            this->addChild(m_halo);
            m_halo->runAction(CCRepeatForever::create(CCSequence::create(
                CCSpawn::create(CCFadeTo::create(1.2f, 190), CCEaseSineInOut::create(CCScaleTo::create(1.2f, 1.35f)), nullptr),
                CCSpawn::create(CCFadeTo::create(1.2f, 90), CCEaseSineInOut::create(CCScaleTo::create(1.2f, 1.05f)), nullptr), nullptr)));
        }
        this->addChild(m_icon);

        this->schedule(schedule_selector(FloatingButton::tick));
        return true;
    }

    // every new screen: it shows again for a moment
    void onEnter() override {
        CCLayer::onEnter();
        this->setZOrder(100000);
        if (m_icon) {
            m_icon->setPosition(clampToScreen(m_icon->getPosition()));
            if (m_halo) m_halo->setPosition(m_icon->getPosition());
        }
        wakeUp();
    }

    void wakeUp() {
        m_idle = 0.f;
        if (m_icon && (m_faded || m_icon->getOpacity() < 255)) {
            m_faded = false;
            m_icon->stopAllActions();
            m_icon->setOpacity(255);
            if (m_halo) {
                m_halo->setVisible(true);
            }
        }
    }

    void tick(float dt) {
        m_settingTimer -= dt;
        if (m_settingTimer <= 0.f) {
            m_settingTimer = 0.5f;
            m_enabled = Mod::get()->getSettingValue<bool>("menu-button");
            this->setVisible(m_enabled);
        }
        if (m_touching || m_faded || !m_enabled) {
            return;
        }
        m_idle += dt;
        if (m_idle >= FADE_AFTER) {
            m_faded = true;
            m_icon->stopAllActions();
            m_icon->runAction(CCFadeTo::create(0.6f, 0));
            if (m_halo) {
                m_halo->runAction(CCSequence::create(CCDelayTime::create(0.1f), CCHide::create(), nullptr));
            }
        }
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        if (!m_icon || !m_enabled || !this->isVisible()) {
            return false;
        }
        if (m_faded && inActiveLevel()) {
            return false;   // invisible and a level is running: the touch belongs to the game
        }
        auto point = this->convertTouchToNodeSpace(touch);
        if (!hit(point)) {
            return false;   // not on the button: the touch goes on to whatever is underneath
        }
        m_touching = true;
        m_dragging = false;
        m_down = point;
        m_grab = m_icon->getPosition() - point;
        wakeUp();
        return true;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (!m_touching) {
            return;
        }
        auto point = this->convertTouchToNodeSpace(touch);
        if (!m_dragging && ccpDistance(point, m_down) > DRAG_START) {
            m_dragging = true;
        }
        if (m_dragging) {
            m_icon->setPosition(clampToScreen(point + m_grab));
            if (m_halo) m_halo->setPosition(m_icon->getPosition());
        }
    }

    void ccTouchEnded(CCTouch*, CCEvent*) override {
        if (!m_touching) {
            return;
        }
        m_touching = false;
        m_idle = 0.f;
        if (m_dragging) {
            m_dragging = false;
            savePosition();
        }
        else {
            openMenu();
        }
    }

    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        if (m_touching && m_dragging) {
            savePosition();
        }
        m_touching = false;
        m_dragging = false;
        m_idle = 0.f;
    }
};

FloatingButton* s_button = nullptr;

// Created once; it is then on top of every screen.
void ensureFloatingButton() {
    if (s_button) {
        return;
    }
    auto* button = FloatingButton::create();
    if (!button) {
        return;
    }
    button->setID("synca-menu-button"_spr);
    s_button = button;
#if SYNCA_HAS_OVERLAY
    OverlayManager::get()->addChild(button, 100000);
#else
    button->retain();   // kept alive by us; moved into each new scene by the CCDirector hook below
    if (auto* scene = CCDirector::get()->getRunningScene()) {
        scene->addChild(button, 100000);
    }
#endif
}

}  // namespace

// Called from the title screen hook (the game is fully started by then). One frame later, so the scene exists.
void addMainMenuButton(CCNode*) {
    queueInMainThread([] { ensureFloatingButton(); });
}

#if !SYNCA_HAS_OVERLAY
void moveFloatingButtonTo(CCScene* scene) {
    if (!s_button || !scene) {
        return;
    }
    s_button->removeFromParentAndCleanup(false);
    scene->addChild(s_button, 100000);
}
#endif

// The pause menu opened: show the button again.
void wakeMenuButton() {
    if (s_button) {
        s_button->wakeUp();
    }
}

}  // namespace synca

#if !SYNCA_HAS_OVERLAY
// Fallback: carry the button into every new scene.
class $modify(SyncaButtonDirector, CCDirector) {
    void willSwitchToScene(CCScene* scene) {
        CCDirector::willSwitchToScene(scene);
        synca::moveFloatingButtonTo(scene);
    }
};
#endif
