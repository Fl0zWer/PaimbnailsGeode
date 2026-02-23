#include <Geode/modify/CCEGLView.hpp>
#include <Geode/loader/Log.hpp>
#include "../utils/FramebufferCapture.hpp"

using namespace geode::prelude;

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // correr despues de otros mods pero antes del swap original
        // Priority::Last + Pre = ultimo hook antes de llamar al original
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", geode::Priority::Last);
    }

    void swapBuffers() {
        // ejecuto la captura pendiente antes del swap (pilla el back buffer ya renderizado)
        if (FramebufferCapture::hasPendingCapture()) {
            log::debug("[CaptureView] Executing capture in swapBuffers (back buffer)");
            FramebufferCapture::executeIfPending();
        }

        // hago el swap de verdad
        CCEGLView::swapBuffers();

        // proceso callbacks diferidos despues del swap
        FramebufferCapture::processDeferredCallbacks();
    }
};
