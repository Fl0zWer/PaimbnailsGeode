#include <Geode/modify/DailyLevelPage.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/LevelCell.hpp>
#include "../managers/LocalThumbs.hpp"
#include "../managers/LevelColors.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../utils/Constants.hpp"
#include "../utils/AnimatedGIFSprite.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// dejo daily level page vacio pa evitar conflictos con levelcell
class $modify(PaimonDailyLevelPage, DailyLevelPage) {
    // implementacion vacia
};
