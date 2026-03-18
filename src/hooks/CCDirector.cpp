#include <Geode/modify/CCEGLView.hpp>
#include <Geode/loader/Log.hpp>
#include "../features/capture/services/FramebufferCapture.hpp"

using namespace geode::prelude;

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // corre despues de otros mods pero antes del swap original
        // Priority::Last + Pre = ultimo hook antes de llamar al original
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", geode::Priority::Last);
    }

    void swapBuffers() {
        // ejecuto la captura pendiente antes del swap (agarra el back buffer ya renderizado)
        if (FramebufferCapture::hasPendingCapture()) {
            log::debug("[CaptureView] Executing capture in swapBuffers (back buffer)");
            FramebufferCapture::executeIfPending();
        }

        // hago el swap de verdad
        CCEGLView::swapBuffers();

        // proceso los callbacks diferidos despues del swap
        FramebufferCapture::processDeferredCallbacks();
    }
};
