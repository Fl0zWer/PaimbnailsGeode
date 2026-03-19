#include "InfoButton.hpp"

using namespace cocos2d;
using namespace geode::prelude;

void PaimonInfoTarget::onInfo(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!item) return;
    auto* dataStr = typeinfo_cast<CCString*>(item->getUserObject());
    if (!dataStr) return;

    std::string raw = dataStr->getCString();
    auto sep = raw.find("\n---\n");
    std::string title = (sep != std::string::npos) ? raw.substr(0, sep) : "Info";
    std::string desc = (sep != std::string::npos) ? raw.substr(sep + 5) : raw;

    auto* alert = FLAlertLayer::create(title.c_str(), desc.c_str(), "OK");
    if (alert) alert->show();
}

PaimonInfoTarget* PaimonInfoTarget::create() {
    auto* ret = new PaimonInfoTarget();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

PaimonInfoTarget* PaimonInfoTarget::shared() {
    static Ref<PaimonInfoTarget> s_inst = nullptr;
    if (!s_inst) {
        s_inst = PaimonInfoTarget::create();
    }
    return s_inst.data();
}

