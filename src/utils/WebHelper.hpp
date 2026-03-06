#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/function.hpp>
#include <string>

/**
 * WebHelper — Centralized async web dispatch for Paimbnails.
 *
 * Uses Geode v5 native async::spawn + WebFuture for non-blocking requests.
 * The callback is guaranteed to run on the main (Cocos2d-x) thread.
 */
namespace WebHelper {

/**
 * Dispatch a web request asynchronously (fire-and-forget).
 * The callback is guaranteed to run on the main thread.
 *
 * @param req    The prepared WebRequest (moved in).
 * @param method "GET" or "POST".
 * @param url    The target URL.
 * @param cb     Callback receiving the WebResponse on the main thread.
 */
inline void dispatch(
    geode::utils::web::WebRequest&& req,
    std::string const& method,
    std::string const& url,
    geode::CopyableFunction<void(geode::utils::web::WebResponse)> cb
) {
    auto future = (method == "POST")
        ? req.post(url)
        : req.get(url);

    auto safeCb = std::make_shared<decltype(cb)>(std::move(cb));

    geode::async::spawn(std::move(future), [safeCb](geode::utils::web::WebResponse res) {
        if (safeCb && *safeCb) {
            (*safeCb)(std::move(res));
        }
    });
}

} // namespace WebHelper

