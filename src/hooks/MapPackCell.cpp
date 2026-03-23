#include <Geode/Geode.hpp>
#include <Geode/modify/MapPackCell.hpp>
#include "../utils/ListThumbnailCarousel.hpp"
#include <sstream>

using namespace geode::prelude;

class $modify(PaimonMapPackCell, MapPackCell) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("MapPackCell::loadFromMapPack", geode::Priority::Late);
    }

    struct Fields {
        Ref<ListThumbnailCarousel> m_carousel = nullptr;
        Ref<GJMapPack> m_pack = nullptr;
    };

    $override
    void loadFromMapPack(GJMapPack* pack) {
        MapPackCell::loadFromMapPack(pack);

        if (!pack) return;
        
        m_fields->m_pack = pack;

        // quitar carousel viejo si reusan la celda
        if (m_fields->m_carousel) {
            m_fields->m_carousel->removeFromParent();
            m_fields->m_carousel = nullptr;
        }

        // delay pa que el layout ya este armado
        WeakRef<PaimonMapPackCell> self = this;
        Loader::get()->queueInMainThread([self]() {
            auto cellRef = self.lock();
            if (auto* cell = static_cast<PaimonMapPackCell*>(cellRef.data()); cell && cell->getParent()) {
                cell->createCarousel();
            }
        });
    }

    void createCarousel() {
        auto pack = m_fields->m_pack;
        if (!pack) return;

        // sacar ids de niveles del pack
        std::vector<int> levelIDs;
        
        if (pack->m_levels && pack->m_levels->count() > 0) {
            for (auto obj : CCArrayExt<CCObject*>(pack->m_levels)) {
                // probar como CCString
                if (auto str = typeinfo_cast<CCString*>(obj)) {
                    if (auto res = geode::utils::numFromString<int>(str->getCString())) {
                        levelIDs.push_back(res.unwrap());
                    }
                } 
                // o como GJGameLevel
                else if (auto level = typeinfo_cast<GJGameLevel*>(obj)) {
                    levelIDs.push_back(level->m_levelID);
                }
            }
        }

        // si m_levels no tenia nada, parsear el string
        if (levelIDs.empty() && !pack->m_levelStrings.empty()) {
            std::string levelsStr(pack->m_levelStrings.c_str());
            std::stringstream ss(levelsStr);
            std::string segment;
            while (std::getline(ss, segment, ',')) {
                if (auto res = geode::utils::numFromString<int>(segment)) {
                    auto val = res.unwrap();
                    if (val > 0) levelIDs.push_back(val);
                }
            }
        }

        if (levelIDs.empty()) return;

        auto size = this->getContentSize();
        
        // forzar minimo de altura
        CCSize carouselSize = size;
        if (carouselSize.height < 90.0f) {
            carouselSize.height = 90.0f;
        }

        auto carousel = ListThumbnailCarousel::create(levelIDs, carouselSize);
        if (carousel) {
            carousel->setID("paimon-mappack-carousel"_spr);

            // centrar y poner detras del texto
            carousel->setPosition({size.width / 2, size.height / 2});
            
            carousel->setZOrder(-1); 
            
            // el fondo original va mas atras
            if (auto bg = this->getChildByType<CCLayerColor>(0)) {
                bg->setZOrder(-2);
            } else if (auto firstChild = this->getChildByType<CCNode>(0)) {
                firstChild->setZOrder(-2);
            }
            
            carousel->setOpacity(255); 
            
            this->addChild(carousel);
            m_fields->m_carousel = carousel;
            
            carousel->startCarousel();
        }
    }
};
