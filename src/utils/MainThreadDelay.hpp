#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <algorithm>

namespace paimon {

class MainThreadDelay final : public cocos2d::CCNode {
public:
    static void schedule(float delaySeconds, geode::CopyableFunction<void()> callback) {
        if (!callback) return;

        auto* task = new MainThreadDelay();
        if (!task || !task->init()) {
            delete task;
            return;
        }

        task->autorelease();
        task->retain();
        task->m_callback = std::move(callback);

        auto* scheduler = cocos2d::CCDirector::sharedDirector()->getScheduler();
        if (!scheduler) {
            task->release();
            return;
        }

        scheduler->scheduleSelector(
            schedule_selector(MainThreadDelay::onDelay),
            task,
            0.f,
            0,
            std::max(0.f, delaySeconds),
            false
        );
    }

private:
    geode::CopyableFunction<void()> m_callback;

    void onDelay(float) {
        if (auto* scheduler = cocos2d::CCDirector::sharedDirector()->getScheduler()) {
            scheduler->unscheduleSelector(schedule_selector(MainThreadDelay::onDelay), this);
        }

        auto callback = std::move(m_callback);
        if (callback) {
            callback();
        }

        this->release();
    }
};

inline void scheduleMainThreadDelay(float delaySeconds, geode::CopyableFunction<void()> callback) {
    MainThreadDelay::schedule(delaySeconds, std::move(callback));
}

} // namespace paimon
