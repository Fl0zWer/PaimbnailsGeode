#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/function.hpp>
#include <thread>
#include <string>

/**
 * WebHelper — Centralized async web dispatch for Paimbnails.
 *
 * Consolidates the previously duplicated webSpawn() pattern from 4 files
 * into a single reusable helper.
 *
 * Uses std::thread + postSync/getSync + queueInMainThread to work around
 * a known MSVC Internal Compiler Error (ICE) with geode::async::spawn on
 * certain template instantiations. The callback is guaranteed to run on
 * the main (Cocos2d-x) thread.
 *
 * NOTE: When Geode fixes the MSVC ICE for async::spawn with WebFuture,
 * this can be migrated to:
 *   geode::async::spawn(req.get(url), [](WebResponse res) { ... });
 */
namespace WebHelper {

/**
 * Dispatch a web request asynchronously.
 * The callback is guaranteed to run on the main thread.
 *
 * @param req    The prepared WebRequest (moved in).
 * @param method "GET" or "POST".
 * @param url    The target URL.
 * @param cb     Callback receiving the WebResponse on the main thread.
 */
inline void dispatch(
    geode::utils::web::WebRequest&& req,
    std::string method,
    std::string url,
    geode::CopyableFunction<void(geode::utils::web::WebResponse)> cb
) {
    std::thread([req = std::move(req), method = std::move(method),
                 url = std::move(url), cb = std::move(cb)]() mutable {
        auto res = (method == "POST")
            ? req.postSync(url) : req.getSync(url);
        geode::queueInMainThread([cb = std::move(cb), res = std::move(res)]() mutable {
            if (cb) cb(std::move(res));
        });
    }).detach();
}

} // namespace WebHelper


