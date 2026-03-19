#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListCell.hpp>
#include "../utils/ListThumbnailCarousel.hpp"
#include "../managers/ThumbnailAPI.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelListCell, LevelListCell) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("LevelListCell::loadFromList", geode::Priority::Late);
    }

    struct Fields {
        Ref<ListThumbnailCarousel> m_carousel = nullptr;
        Ref<CCNode> m_listThumbnail = nullptr;
        int m_currentListID = 0;
    };

    // quite el hook de init porque rompia la compilacion

    
    $override
    void loadFromList(GJLevelList* list) {
        LevelListCell::loadFromList(list);

        if (!list) {
            log::warn("PaimonLevelListCell: list is null");
            return;
        }

        m_fields->m_currentListID = list->m_listID;
        log::debug("PaimonLevelListCell: loadFromList called for list ID: {}", list->m_listID);

        // limpio carousel viejo por si la celda se reutiliza
        if (m_fields->m_carousel) {
            m_fields->m_carousel->removeFromParent();
            m_fields->m_carousel = nullptr;
        }
        
        // limpio thumbnail viejo
        if (m_fields->m_listThumbnail) {
            m_fields->m_listThumbnail->removeFromParent();
            m_fields->m_listThumbnail = nullptr;
        }

        // saco los IDs
        std::vector<int> levelIDs;
        
        log::debug("PaimonLevelListCell: m_levels size: {}", list->m_levels.size());

        for (int id : list->m_levels) {
            if (id != 0) {
                levelIDs.push_back(id);
            }
            // limit removed to allow full carousel cycling
        }

        auto size = this->getContentSize();
        // log::info("PaimonLevelListCell: Cell content size: {}, {}", size.width, size.height);
        
        // respaldo si el size vino en 0
        if (size.width == 0 || size.height == 0) {
            size = CCSize(356, 50);
            this->setContentSize(size); // force update content size
        }

        // subo la altura para que se vea como un LevelCell normal
        CCSize carouselSize = size;
        if (carouselSize.height < 90.0f) {
            carouselSize.height = 90.0f;
        }
        
        // creo el carousel
        if (!levelIDs.empty()) {
            auto carousel = ListThumbnailCarousel::create(levelIDs, carouselSize);
            if (carousel) {
                carousel->setID("paimon-thumbnail-carousel"_spr);

                // lo centro y lo subo un poco
                carousel->setPosition({size.width / 2, size.height / 2 + 20.0f});
                
                // detras del texto y los botones
                carousel->setZOrder(-1);
                
                // intento mandar el fondo detras del carousel
                // buscar fondo por tipo en vez de indice fragil
                if (auto bg = this->getChildByType<CCLayerColor>(0)) {
                    bg->setZOrder(-2);
                } else if (auto firstChild = this->getChildByType<CCNode>(0)) {
                    firstChild->setZOrder(-2);
                }
                
                carousel->setOpacity(255); 
                
                this->addChild(carousel);
                m_fields->m_carousel = carousel;
                
                carousel->startCarousel();
                log::debug("PaimonLevelListCell: Carousel created and added at {}, {}", size.width/2, size.height/2);
            } else {
                log::error("PaimonLevelListCell: Failed to create carousel");
            }
        }
    }
};
