#include <Geode/modify/CCEGLView.hpp>
#include <Geode/loader/Log.hpp>
#include "../features/capture/services/FramebufferCapture.hpp"

using namespace geode::prelude;

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // ultimo antes del original, asi capturamos el back buffer limpio
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", geode::Priority::Last);
    }

    void swapBuffers() {
        // capturar antes del swap pa agarrar el frame completo
        if (FramebufferCapture::hasPendingCapture()) {
            log::debug("[CaptureView] Executing capture in swapBuffers (back buffer)");
            FramebufferCapture::executeIfPending();
        }

        CCEGLView::swapBuffers();

        FramebufferCapture::processDeferredCallbacks();
    }
};
