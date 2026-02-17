#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListCell.hpp>
#include "../utils/ListThumbnailCarousel.hpp"
#include "../managers/ThumbnailAPI.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelListCell, LevelListCell) {
    struct Fields {
        Ref<ListThumbnailCarousel> m_carousel = nullptr;
        Ref<CCNode> m_listThumbnail = nullptr;
        int m_currentListID = 0;
    };

    // removed init hook as it caused compilation errors

    
    void loadFromList(GJLevelList* list) {
        LevelListCell::loadFromList(list);

        if (!list) {
            log::warn("PaimonLevelListCell: list is null");
            return;
        }

        m_fields->m_currentListID = list->m_listID;
        log::info("PaimonLevelListCell: loadFromList called for list ID: {}", list->m_listID);

        // remove existing carousel if any (for cell reuse)
        if (m_fields->m_carousel) {
            m_fields->m_carousel->removeFromParent();
            m_fields->m_carousel = nullptr;
        }
        
        // remove existing list thumbnail if any
        if (m_fields->m_listThumbnail) {
            m_fields->m_listThumbnail->removeFromParent();
            m_fields->m_listThumbnail = nullptr;
        }

        // get level ids
        std::vector<int> levelIDs;
        
        // check if m_levels is accessible
        log::info("PaimonLevelListCell: m_levels size: {}", list->m_levels.size());

        for (int id : list->m_levels) {
            if (id != 0) {
                levelIDs.push_back(id);
            }
            // limit removed to allow full carousel cycling
        }

        auto size = this->getContentSize();
        // log::info("PaimonLevelListCell: Cell content size: {}, {}", size.width, size.height);
        
        // fallback if size is 0
        if (size.width == 0 || size.height == 0) {
            size = CCSize(356, 50);
            this->setContentSize(size); // force update content size
        }

        // force height to match LevelCell (approx 90)
        // this ensures the thumbnail covers the entire cell and looks better
        CCSize carouselSize = size;
        if (carouselSize.height < 90.0f) {
            carouselSize.height = 90.0f;
        }
        
        // 1. Create carousel (Default behavior)
        if (!levelIDs.empty()) {
            auto carousel = ListThumbnailCarousel::create(levelIDs, carouselSize);
            if (carousel) {
                carousel->setID("paimon-thumbnail-carousel"_spr);

                // center the carousel in the cell and move up 20px (requested +5px more)
                carousel->setPosition({size.width / 2, size.height / 2 + 20.0f});
                
                // z=-1 to be behind text/buttons
                carousel->setZOrder(-1);
                
                // attempt to push background behind the carousel
                // the background is usually the first child
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
                log::info("PaimonLevelListCell: Carousel created and added at {}, {}", size.width/2, size.height/2);
            } else {
                log::error("PaimonLevelListCell: Failed to create carousel");
            }
        }
    }
};
