#include <Geode/Geode.hpp>
#include <Geode/modify/MapPackCell.hpp>
#include "../utils/ListThumbnailCarousel.hpp"
#include <sstream>

using namespace geode::prelude;

class $modify(PaimonMapPackCell, MapPackCell) {
    struct Fields {
        Ref<ListThumbnailCarousel> m_carousel = nullptr;
        Ref<GJMapPack> m_pack = nullptr;
    };

    void loadFromMapPack(GJMapPack* pack) {
        MapPackCell::loadFromMapPack(pack);

        if (!pack) return;
        
        m_fields->m_pack = pack;

        // quitar carousel existente si hay (reuso de celda)
        if (m_fields->m_carousel) {
            m_fields->m_carousel->removeFromParent();
            m_fields->m_carousel = nullptr;
        }

        // retrasar creacion pa que datos y layout esten listos
        this->retain();
        Loader::get()->queueInMainThread([this]() {
            if (this->getParent()) { // comprobar que siga vivo
                this->createCarousel();
            }
            this->release();
        });
    }

    void createCarousel() {
        auto pack = m_fields->m_pack;
        if (!pack) return;

        // parsear ids de niveles
        std::vector<int> levelIDs;
        
        if (pack->m_levels && pack->m_levels->count() > 0) {
            for (auto obj : CCArrayExt<CCObject*>(pack->m_levels)) {
                // intentar como CCString (id nivel)
                if (auto str = typeinfo_cast<CCString*>(obj)) {
                    if (auto res = geode::utils::numFromString<int>(str->getCString())) {
                        levelIDs.push_back(res.unwrap());
                    }
                } 
                // intentar como GJGameLevel
                else if (auto level = typeinfo_cast<GJGameLevel*>(obj)) {
                    levelIDs.push_back(level->m_levelID);
                }
            }
        }

        // fallback: parsear m_levelStrings si m_levels vacio
        if (levelIDs.empty() && !pack->m_levelStrings.empty()) {
            std::string levelsStr(pack->m_levelStrings.c_str());
            std::stringstream ss(levelsStr);
            std::string segment;
            while (std::getline(ss, segment, ',')) {
                if (auto res = geode::utils::numFromString<int>(segment)) {
                    if (res.unwrap() > 0) levelIDs.push_back(res.unwrap());
                }
            }
        }

        if (levelIDs.empty()) return;

        auto size = this->getContentSize();
        
        // forzar altura tipica de celda si hace falta
        CCSize carouselSize = size;
        if (carouselSize.height < 90.0f) {
            carouselSize.height = 90.0f;
        }

        auto carousel = ListThumbnailCarousel::create(levelIDs, carouselSize);
        if (carousel) {
            carousel->setID("paimon-mappack-carousel"_spr);

            // centrar carousel en la celda
            carousel->setPosition({size.width / 2, size.height / 2});
            
            // z=-1 pa quedar detras de texto/botones
            carousel->setZOrder(-1); 
            
            // mandar fondo atras
            if (this->getChildrenCount() > 0) {
                auto bg = static_cast<CCNode*>(this->getChildren()->objectAtIndex(0));
                if (bg) {
                    bg->setZOrder(-2);
                }
            }
            
            carousel->setOpacity(255); 
            
            this->addChild(carousel);
            m_fields->m_carousel = carousel;
            
            carousel->startCarousel();
        }
    }
};
