#include <Geode/modify/LevelAreaInnerLayer.hpp>
#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/FLAlertLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/LoadingCircle.hpp>
#include "../managers/ThumbnailLoader.hpp"

using namespace geode::prelude;

class SimpleThumbnailPopup : public geode::Popup {
protected:
    void setup(std::pair<CCTexture2D*, std::string> const& params) { // not override
        auto tex = params.first;
        auto title = params.second;
        this->setTitle(title.c_str());
        
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto m_size = this->m_mainLayer->getContentSize();
        
        auto spr = CCSprite::createWithTexture(tex);
        if (spr) {
            float maxWidth = 340.f;
            float maxHeight = 220.f; // espacio pa título y botones
            
            float scaleX = maxWidth / spr->getContentWidth();
            float scaleY = maxHeight / spr->getContentHeight();
            float scale = std::min(scaleX, scaleY);
            if (scale > 1.0f) scale = 1.0f; 
            
            spr->setScale(scale);
            spr->setPosition(m_size / 2);
            this->m_mainLayer->addChild(spr);
        }
        
        this->setZOrder(10500);
        this->setID("SimpleThumbnailPopup"_spr);
    }
    
public:
    static SimpleThumbnailPopup* create(CCTexture2D* tex, std::string const& title) {
        auto ret = new SimpleThumbnailPopup();
        if (ret && ret->init(400.f, 280.f)) {
            ret->setup({tex, title});
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

class $modify(PaimonLevelAreaInnerLayer, LevelAreaInnerLayer) {
    struct Fields {
        std::unordered_map<int, CCSprite*> m_doorThumbnails;
        bool m_thumbnailsAdded = false;
    };

    bool init(bool returning) {
        log::info("[LevelAreaInnerLayer] init() called with returning={}", returning);
        
        if (!LevelAreaInnerLayer::init(returning)) {
            return false;
        }

        log::info("[LevelAreaInnerLayer] Init successful, scheduling thumbnail addition");
        
        // mini delay pa que ya existan las puertas
        this->scheduleOnce(schedule_selector(PaimonLevelAreaInnerLayer::addThumbnailsToDoors), 0.1f);

        return true;
    }

    void addThumbnailsToDoors(float dt) {
        auto fields = m_fields.self();
        if (fields->m_thumbnailsAdded) return;
        fields->m_thumbnailsAdded = true;

        log::info("[LevelAreaInnerLayer] Adding thumbnails to main level doors");
        
        // niveles main del 1 al 21
        std::vector<int> mainLevelIDs;
        for (int i = 1; i <= 21; i++) {
            mainLevelIDs.push_back(i);
        }

        int addedCount = 0;
        for (int levelID : mainLevelIDs) {
            auto doorNode = this->findDoorForLevel(levelID);
            if (doorNode) {
                this->addThumbnailToDoor(doorNode, levelID);
                addedCount++;
            }
        }

        log::info("[LevelAreaInnerLayer] Added {} thumbnails to doors", addedCount);
    }

    CCNode* findDoorForLevel(int levelID) {
        // busco las puertas como se pueda
        auto children = CCArrayExt<CCNode*>(this->getChildren());
        
        for (auto child : children) {
            if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                auto menuChildren = CCArrayExt<CCNode*>(menu->getChildren());
                for (auto menuChild : menuChildren) {
                    if (auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(menuChild)) {
                        int doorTag = menuItem->getTag();
                        if (doorTag == levelID || doorTag == (1000 + levelID)) {
                            return menuItem;
                        }
                    }
                }
            }
        }
        
        return nullptr;
    }

    void addThumbnailToDoor(CCNode* doorNode, int levelID) {
        if (!doorNode) return;

        auto fields = m_fields.self();
        
        if (fields->m_doorThumbnails.find(levelID) != fields->m_doorThumbnails.end()) {
            return;
        }

        log::info("[LevelAreaInnerLayer] Adding thumbnail for level {}", levelID);

        auto doorSize = doorNode->getContentSize();
        std::string fileName = fmt::format("{}.png", levelID);
        ThumbnailLoader::get().requestLoad(
            levelID,
            fileName,
            [this, doorNode, levelID](CCTexture2D* tex, bool) {
                if (!tex || !doorNode) return;

                auto fields = m_fields.self();
                
                auto thumbSprite = CCSprite::createWithTexture(tex);
                if (!thumbSprite) return;

                auto doorSize = doorNode->getContentSize();
                float scale = std::min(
                    (doorSize.width * 0.8f) / thumbSprite->getContentWidth(),
                    (doorSize.height * 0.8f) / thumbSprite->getContentHeight()
                );
                
                thumbSprite->setScale(scale);
                thumbSprite->setPosition(doorSize / 2);
                thumbSprite->setZOrder(-1);
                thumbSprite->setOpacity(180);
                doorNode->addChild(thumbSprite);
                
                if (fields) fields->m_doorThumbnails[levelID] = thumbSprite;
                
                log::info("[LevelAreaInnerLayer] Thumbnail added for level {}", levelID);
            },
            1
        );
    }

    void onExit() {
        LevelAreaInnerLayer::onExit();
        
        auto fields = m_fields.self();
        fields->m_doorThumbnails.clear();
        fields->m_thumbnailsAdded = false;
    }
};


class $modify(InfoBtnHookFLAlertLayer, FLAlertLayer) {
    void show() {
        FLAlertLayer::show();
        
        this->getScheduler()->scheduleSelector(schedule_selector(InfoBtnHookFLAlertLayer::checkAndAddButton), this, 0.0f, 0, 0.0f, false);
    }
    
    void checkAndAddButton(float) {
        // no metas el botón en mi propio popup
        if (this->getID() == "SimpleThumbnailPopup") return;
        
        int foundLevelID = 0;
        
        CCNode* container = this->m_mainLayer ? this->m_mainLayer : this;
        if (container) {
            auto children = container->getChildren();
            if (children) {
                 for (auto* child : CCArrayExt<CCNode*>(children)) {
                      if (auto label = typeinfo_cast<CCLabelBMFont*>(child)) {
                           std::string txt = label->getString();
                           if (txt == "The Tower") foundLevelID = 5001;
                           else if (txt == "The Sewers") foundLevelID = 5002;
                           else if (txt == "The Cellar") foundLevelID = 5003;
                           else if (txt == "The Secret Hollow") foundLevelID = 5004;
                           
                           if (foundLevelID > 0) break;
                      }
                      // a veces el título viene en un textarea
                      if (auto txtArea = typeinfo_cast<TextArea*>(child)) {
                      }
                 }
            }
        }
        
        if (foundLevelID > 0) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            
            CCSprite* iconSpr = nullptr;
            
            // cargo el icono como sea
            auto resPath = Mod::get()->getResourcesDir() / "paim_BotonMostrarThumbnails.png";
            if (std::filesystem::exists(resPath)) {
                iconSpr = CCSprite::create(resPath.generic_string().c_str());
            }

            if (!iconSpr) {
                iconSpr = CCSprite::create("flozwer.paimbnails/paim_BotonMostrarThumbnails.png");
            }
            
            if (!iconSpr) {
                 iconSpr = CCSprite::createWithSpriteFrameName("GJ_cameraBtn_001.png");
            }
            if (!iconSpr) {
                 iconSpr = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
            }
            
            if (!iconSpr) {
                iconSpr = CCSprite::create();
                if (auto sq = CCSprite::createWithSpriteFrameName("GJ_square01.png")) {
                    iconSpr = sq;
                }
            }

            if (iconSpr) {
                iconSpr->setRotation(-90.0f);
                // bajo el icono un 20%
                iconSpr->setScale(0.8f);
                
                // botón circular verde
                auto btnSprite = CircleButtonSprite::create(
                    iconSpr,
                    CircleBaseColor::Green,
                    CircleBaseSize::Small
                );

                if (!btnSprite) return;

                auto btn = CCMenuItemSpriteExtra::create(
                    btnSprite,
                    this,
                    menu_selector(InfoBtnHookFLAlertLayer::onShowThumbnailTheTower)
                );
                btn->setID("paimbnails-tower-btn"_spr);
                btn->setTag(foundLevelID);
    
                if (this->m_buttonMenu) {
                    this->m_buttonMenu->addChild(btn);
                    btn->setPosition({160.f, 100.f}); 
                } else {
                    auto menu = CCMenu::create();
                    menu->setPosition(winSize / 2);
                    menu->addChild(btn);
                    btn->setPosition({160.f, 100.f});
                    
                    container->addChild(menu, 10);
                    menu->setTouchPriority(-500); 
                }
            }
        }
    }
    
    void onShowThumbnailTheTower(CCObject* sender) {
         int levelID = sender->getTag();
         std::string levelName = "Thumbnail";
         
         if (levelID == 5001) levelName = "The Tower";
         else if (levelID == 5002) levelName = "The Sewers";
         else if (levelID == 5003) levelName = "The Cellar";
         else if (levelID == 5004) levelName = "The Secret Hollow";
         
         auto loading = LoadingCircle::create();
         loading->setParentLayer(this);
         loading->setFade(true);
         loading->show();
         
         ThumbnailLoader::get().requestLoad(levelID, "", [this, loading, levelName](CCTexture2D* tex, bool success){
             if (loading) loading->fadeAndRemove();
             
             if (success && tex) {
                  auto popup = SimpleThumbnailPopup::create(tex, levelName);
                  popup->show();
             } else {
                  Notification::create("Thumbnail not found for this level", NotificationIcon::Error)->show();
             }
         });
    }
};
