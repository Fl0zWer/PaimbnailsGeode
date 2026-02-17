#include <Geode/modify/CCEGLView.hpp>
#include <Geode/loader/Log.hpp>
#include "../utils/FramebufferCapture.hpp"

using namespace geode::prelude;

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // lo pongo con prioridad bajisima: despues de otros mods y antes del swap
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4834)
#endif
        auto res = self.setHookPriority("cocos2d::CCEGLView::swapBuffers", -9999);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
        if (res.isErr()) {
            log::warn("[CaptureView] Failed to set hook priority: {}", res.unwrapErr());
        }
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
