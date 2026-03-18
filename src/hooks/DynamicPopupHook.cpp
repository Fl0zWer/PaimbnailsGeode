// DynamicPopupHook.cpp  –  Paimbnails popup animation system
// Hooks FLAlertLayer::show() / keyBackClicked() for Paimbnails popups only.
// Hooks CCMenuItemSpriteExtra::activate() to capture button world-position.
//
// Styles:
//   paimonUI     – expands from the button with a 3-phase spring settle
//   slide-up     – rises from below with deceleration
//   slide-down   – drops from above
//   zoom-fade    – quick scale burst from center
//   elastic      – bouncy elastic overshoot
//   bounce       – drops in with bounce at the end
//   flip         – 3D-ish horizontal flip via scaleX
//   fold         – vertical fold-open via scaleY
//   pop-rotate   – scale + subtle rotation swing

#include <Geode/modify/FLAlertLayer.hpp>
#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/DynamicPopupRegistry.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// ═══════════════════════════════════════════════════════════════════════
// Button origin capture
// ═══════════════════════════════════════════════════════════════════════

class $modify(PaimonButtonOriginCapture, CCMenuItemSpriteExtra) {
    static void onModify(auto& self) {
        // Capturar el origen antes de cualquier logica de activacion/mods posteriores.
        (void)self.setHookPriorityPre("CCMenuItemSpriteExtra::activate", geode::Priority::First);
    }

    $override
    void activate() {
        auto sz = this->getContentSize();
        paimon::storeButtonOrigin(
            this->convertToWorldSpace({sz.width / 2.f, sz.height / 2.f})
        );
        CCMenuItemSpriteExtra::activate();
    }
};

// ═══════════════════════════════════════════════════════════════════════
// Main hook – entry & exit animations
// ═══════════════════════════════════════════════════════════════════════

class $modify(PaimonDynamicPopupHook, FLAlertLayer) {

    struct Fields {
        bool    m_exiting = false;
        CCPoint m_origin  = {-1.f, -1.f};
        CCPoint m_finalPos= {0.f, 0.f};
    };

    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("FLAlertLayer::show",           geode::Priority::Late);
        (void)self.setHookPriorityPost("FLAlertLayer::keyBackClicked", geode::Priority::Late);
    }

    // ── helpers ──────────────────────────────────────────────────

    bool isPaimonPopup() {
        return paimon::isDynamicPopup(this)
            && Mod::get()->getSettingValue<bool>("dynamic-popup-enabled");
    }

    float getSpeed() {
        return static_cast<float>(
            Mod::get()->getSettingValue<double>("dynamic-popup-speed")
        );
    }

    std::string getStyle() {
        return Mod::get()->getSettingValue<std::string>("dynamic-popup-style");
    }

    CCPoint worldToMLParent(CCPoint wp) {
        auto* p = m_mainLayer->getParent();
        return p ? p->convertToNodeSpace(wp) : wp;
    }

    // Consume or discard the stored button origin; always stores fallback.
    CCPoint resolveOrigin(CCPoint const& fallback) {
        CCPoint o = fallback;
        if (paimon::hasButtonOrigin())
            o = worldToMLParent(paimon::consumeButtonOrigin());
        else
            paimon::consumeButtonOrigin(); // discard stale
        m_fields->m_origin = o;
        return o;
    }

    // ── ENTRY ────────────────────────────────────────────────────

    void runEntryAnimation() {
        auto* ml = m_mainLayer;
        if (!ml) return;
        ml->stopAllActions();

        float       spd = getSpeed();
        std::string sty = getStyle();
        CCPoint     fp  = ml->getPosition();
        m_fields->m_finalPos = fp;

        // ── paimonUI ──────────────────────────────────────────────
        if (sty == "paimonUI") {
            CCPoint org = resolveOrigin(fp);

            ml->setScale(0.0f);
            ml->setPosition(org);

            float dur = 0.42f / spd;

            // Phase 1: fast expansion 0 → 1.06 with exponential out  (70%)
            // Phase 2: soft settle    1.06 → 0.985                   (18%)
            // Phase 3: final snap     0.985 → 1.00                   (12%)
            auto phase1 = CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.70f, 1.06f));
            auto phase2 = CCEaseSineInOut::create(CCScaleTo::create(dur * 0.18f, 0.985f));
            auto phase3 = CCEaseSineOut::create(CCScaleTo::create(dur * 0.12f, 1.00f));
            ml->runAction(CCSequence::create(phase1, phase2, phase3, nullptr));

            // Movement: smooth exponential ease to final position
            ml->runAction(
                CCEaseExponentialOut::create(CCMoveTo::create(dur * 0.70f, fp))
            );

        // ── slide-up ──────────────────────────────────────────────
        } else if (sty == "slide-up") {
            resolveOrigin(fp);
            ml->setScale(0.96f);
            ml->setPosition(fp + CCPoint(0.f, -55.f));

            float dur = 0.38f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialOut::create(CCMoveTo::create(dur, fp)),
                CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)),
                nullptr
            ));

        // ── slide-down ────────────────────────────────────────────
        } else if (sty == "slide-down") {
            resolveOrigin(fp);
            ml->setScale(0.96f);
            ml->setPosition(fp + CCPoint(0.f, 55.f));

            float dur = 0.38f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialOut::create(CCMoveTo::create(dur, fp)),
                CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)),
                nullptr
            ));

        // ── zoom-fade ─────────────────────────────────────────────
        } else if (sty == "zoom-fade") {
            resolveOrigin(fp);
            ml->setScale(0.70f);

            float dur = 0.28f / spd;
            ml->runAction(
                CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f))
            );

        // ── elastic ───────────────────────────────────────────────
        } else if (sty == "elastic") {
            resolveOrigin(fp);
            ml->setScale(0.0f);

            float dur = 0.55f / spd;
            ml->runAction(
                CCEaseElasticOut::create(CCScaleTo::create(dur, 1.00f), 0.35f)
            );

        // ── bounce ────────────────────────────────────────────────
        } else if (sty == "bounce") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setPosition(fp + CCPoint(0.f, 30.f));

            float dur = 0.50f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseBounceOut::create(CCScaleTo::create(dur, 1.00f)),
                CCEaseBounceOut::create(CCMoveTo::create(dur, fp)),
                nullptr
            ));

        // ── flip ──────────────────────────────────────────────────
        } else if (sty == "flip") {
            resolveOrigin(fp);
            ml->setScaleX(0.0f);
            ml->setScaleY(1.0f);

            float dur = 0.35f / spd;
            // scaleX: 0→1.04→1.0  with overshoot feel
            auto flipX1 = CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.75f, 1.04f, 1.0f));
            auto flipX2 = CCEaseSineOut::create(CCScaleTo::create(dur * 0.25f, 1.0f, 1.0f));
            ml->runAction(CCSequence::create(flipX1, flipX2, nullptr));

        // ── fold ──────────────────────────────────────────────────
        } else if (sty == "fold") {
            resolveOrigin(fp);
            ml->setScaleX(1.0f);
            ml->setScaleY(0.0f);

            float dur = 0.35f / spd;
            auto foldY1 = CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.75f, 1.0f, 1.04f));
            auto foldY2 = CCEaseSineOut::create(CCScaleTo::create(dur * 0.25f, 1.0f, 1.0f));
            ml->runAction(CCSequence::create(foldY1, foldY2, nullptr));

        // ── pop-rotate ────────────────────────────────────────────
        } else if (sty == "pop-rotate") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setRotation(-8.f);

            float dur = 0.40f / spd;
            // Scale: 0→1.05→1.0
            auto s1 = CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.70f, 1.05f));
            auto s2 = CCEaseSineOut::create(CCScaleTo::create(dur * 0.30f, 1.00f));
            ml->runAction(CCSequence::create(s1, s2, nullptr));
            // Rotation: -8→2→0
            auto r1 = CCEaseExponentialOut::create(CCRotateTo::create(dur * 0.65f, 2.f));
            auto r2 = CCEaseSineOut::create(CCRotateTo::create(dur * 0.35f, 0.f));
            ml->runAction(CCSequence::create(r1, r2, nullptr));

        // ── fallback ──────────────────────────────────────────────
        } else {
            resolveOrigin(fp);
            ml->setScale(0.70f);
            float dur = 0.28f / spd;
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)));
        }
    }

    // ── EXIT ─────────────────────────────────────────────────────

    void runExitAnimation() {
        auto* ml = m_mainLayer;
        if (!ml) { FLAlertLayer::keyBackClicked(); return; }

        ml->stopAllActions();
        this->stopAllActions();
        this->retain();

        float       spd = getSpeed();
        std::string sty = getStyle();
        CCPoint     org = m_fields->m_origin;
        CCPoint     pos = ml->getPosition();
        if (org.x < 0.f) org = pos;

        float dur = 0.f;

        // ── paimonUI ─────────────────────────────────────────────
        if (sty == "paimonUI") {
            dur = 0.25f / spd;
            // Accelerating shrink back toward origin
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f), 2.8f),
                CCEaseIn::create(CCMoveTo::create(dur, org), 2.8f),
                nullptr
            ));

        // ── slide-up ─────────────────────────────────────────────
        } else if (sty == "slide-up") {
            dur = 0.20f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.96f), 2.f),
                CCEaseIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, -40.f)), 2.f),
                nullptr
            ));

        // ── slide-down ───────────────────────────────────────────
        } else if (sty == "slide-down") {
            dur = 0.20f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.96f), 2.f),
                CCEaseIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, 40.f)), 2.f),
                nullptr
            ));

        // ── zoom-fade ────────────────────────────────────────────
        } else if (sty == "zoom-fade") {
            dur = 0.16f / spd;
            ml->runAction(
                CCEaseIn::create(CCScaleTo::create(dur, 0.70f), 2.f)
            );

        // ── elastic ──────────────────────────────────────────────
        } else if (sty == "elastic") {
            dur = 0.22f / spd;
            ml->runAction(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f), 2.5f)
            );

        // ── bounce ───────────────────────────────────────────────
        } else if (sty == "bounce") {
            dur = 0.22f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f), 2.f),
                CCEaseIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, 20.f)), 2.f),
                nullptr
            ));

        // ── flip ─────────────────────────────────────────────────
        } else if (sty == "flip") {
            dur = 0.22f / spd;
            ml->runAction(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f, 1.0f), 2.5f)
            );

        // ── fold ─────────────────────────────────────────────────
        } else if (sty == "fold") {
            dur = 0.22f / spd;
            ml->runAction(
                CCEaseIn::create(CCScaleTo::create(dur, 1.0f, 0.0f), 2.5f)
            );

        // ── pop-rotate ───────────────────────────────────────────
        } else if (sty == "pop-rotate") {
            dur = 0.22f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f), 2.5f),
                CCEaseIn::create(CCRotateTo::create(dur, 8.f), 2.f),
                nullptr
            ));

        // ── fallback ─────────────────────────────────────────────
        } else {
            dur = 0.16f / spd;
            ml->runAction(CCEaseIn::create(CCScaleTo::create(dur, 0.70f), 2.f));
        }

        // Deferred close on 'this' (never on m_mainLayer)
        this->runAction(CCSequence::create(
            CCDelayTime::create(dur + 0.01f),
            CCCallFunc::create(this, callfunc_selector(PaimonDynamicPopupHook::finishExit)),
            nullptr
        ));
    }

    void finishExit() {
        paimon::unmarkDynamicPopup(this);
        this->scheduleOnce(schedule_selector(PaimonDynamicPopupHook::deferredClose), 0.f);
    }

    void deferredClose(float) {
        this->release();
        FLAlertLayer::keyBackClicked();
    }

    // ── hooks ────────────────────────────────────────────────────

    $override
    void show() {
        FLAlertLayer::show();
        if (!isPaimonPopup()) return;
        runEntryAnimation();
    }

    $override
    void keyBackClicked() {
        if (!isPaimonPopup() || m_fields->m_exiting) {
            FLAlertLayer::keyBackClicked();
            return;
        }
        m_fields->m_exiting = true;
        this->setKeypadEnabled(false);
        runExitAnimation();
    }
};
