#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/function.hpp>
#include <string>

namespace WebHelper {

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

