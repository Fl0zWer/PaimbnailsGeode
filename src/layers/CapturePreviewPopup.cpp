#include "CapturePreviewPopup.hpp"
#include "../utils/PaimonNotification.hpp"
#include "CaptureEditPopup.hpp"
#include "CaptureLayerEditorPopup.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../utils/Localization.hpp"
#include "../utils/FramebufferCapture.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../utils/PlayerToggleHelper.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace geode::prelude;
using namespace cocos2d;

CapturePreviewPopup* CapturePreviewPopup::create(
    CCTexture2D* texture, 
    int levelID,
    std::shared_ptr<uint8_t> buffer,
    int width,
    int height,
    geode::CopyableFunction<void(bool, int, std::shared_ptr<uint8_t>, int, int, std::string, std::string)> callback,
    geode::CopyableFunction<void(bool, CapturePreviewPopup*)> recaptureCallback,
    bool isPlayerHidden,
    bool isModerator
) {
    log::info("[CapturePreviewPopup] Creando popup de vista previa");
    
    if (!texture) {
        log::error("[CapturePreviewPopup] No se pudo crear popup de vista previa: la textura es nula");
        return nullptr;
    }
    
    auto ret = new CapturePreviewPopup();
    
    ret->m_texture = texture;
    ret->m_levelID = levelID;
    ret->m_buffer = buffer;
    ret->m_width = width;
    ret->m_height = height;
    ret->m_callback = std::move(callback);
    ret->m_recaptureCallback = std::move(recaptureCallback);
    ret->m_isPlayerHidden = isPlayerHidden;
    ret->m_isModerator = isModerator;
    
    if (ret && ret->init()) {
        ret->autorelease();
        log::info("[CapturePreviewPopup] Popup de vista previa creado exitosamente");
        return ret;
    }
    log::error("[CapturePreviewPopup] No se pudo inicializar el popup de vista previa");
    
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void CapturePreviewPopup::updateContent(CCTexture2D* texture, std::shared_ptr<uint8_t> buffer, int width, int height) {
    if (!texture) return;

    m_texture = texture;
    m_buffer = buffer;
    m_width = width;
    m_height = height;

    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }

    m_previewSprite = CCSprite::createWithTexture(m_texture);
    if (m_previewSprite && m_clippingNode) {
        m_previewSprite->setPosition(m_clippingNode->getContentSize() / 2);
        m_clippingNode->addChild(m_previewSprite);
        updatePreviewScale();
    }
}

CapturePreviewPopup::~CapturePreviewPopup() {
    // m_texture is a Ref<T>, automatically releases
}

bool CapturePreviewPopup::init() {
    if (!Popup::init(320.f, 240.f)) return false;

    log::info("[CapturePreviewPopup] Configurando popup de vista previa");
    
    this->setTitle(Localization::get().getString("preview.title").c_str());

        auto content = this->m_mainLayer->getContentSize();
        log::debug("[CapturePreviewPopup] Tamano de la capa principal: {}x{}", content.width, content.height);

        if (!m_texture) {
            log::error("[CapturePreviewPopup] La textura es nula en la configuracion");
            return false;
        }
        
        m_previewSprite = CCSprite::createWithTexture(m_texture);
        if (!m_previewSprite) {
            log::error("[CapturePreviewPopup] No se pudo crear el sprite de vista previa");
            return false;
        }
        m_previewSprite->setAnchorPoint({0.5f, 0.5f});

        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        m_texture->setTexParameters(&params);

        const float kHorizontalMargin = 11.5f;
        const float kVerticalTotalMargin = 62.f;
        float previewAreaWidth = content.width - 2.f * kHorizontalMargin;
        float previewAreaHeight = content.height - kVerticalTotalMargin;

        updatePreviewScale();

        m_clippingNode = CCClippingNode::create();
        if (!m_clippingNode) {
            log::error("[CapturePreviewPopup] No se pudo crear el nodo de recorte");
            return false;
        }
        m_clippingNode->setContentSize({previewAreaWidth, previewAreaHeight});
        m_clippingNode->ignoreAnchorPointForPosition(false);
        m_clippingNode->setAnchorPoint({0.5f, 0.5f});
        m_clippingNode->setPosition({content.width * 0.5f, content.height * 0.5f + 5.f});
        
        auto stencil = CCScale9Sprite::create("square02_001.png");
        if (!stencil) {
            log::warn("[CapturePreviewPopup] No se pudo crear el sprite de plantilla, usando alternativa");
            stencil = CCScale9Sprite::create();
        }
        stencil->ignoreAnchorPointForPosition(false);
        stencil->setContentSize(m_clippingNode->getContentSize());
        stencil->setPosition(m_clippingNode->getContentSize() / 2);
        m_clippingNode->setStencil(stencil);
        
        m_previewSprite->setPosition(m_clippingNode->getContentSize() / 2);
        m_clippingNode->addChild(m_previewSprite);

        m_fillMode = true;
        updatePreviewScale();
        
        this->m_mainLayer->addChild(m_clippingNode);
        log::debug("[CapturePreviewPopup] Nodo de recorte agregado exitosamente");

        auto buttonMenu = CCMenu::create();

        auto scaleSprite = [](CCSprite* spr, float targetSize) {
            if (!spr) return;
            float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
            if (currentSize > 1.0f) {
                spr->setScale(targetSize / currentSize);
            } else {
                spr->setScale(1.0f);
            }
        };
        
        auto cancelIcon = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        if (!cancelIcon) cancelIcon = CCSprite::createWithSpriteFrameName("edit_delIcon_001.png");
        if (cancelIcon) cancelIcon->setScale(0.4f);
        auto cancelCircle = CircleButtonSprite::create(cancelIcon, CircleBaseColor::Green, CircleBaseSize::Medium);
        CCSprite* cancelSpr = typeinfo_cast<CCSprite*>(cancelCircle);
        if (!cancelSpr) cancelSpr = cancelCircle;
        scaleSprite(cancelSpr, 30.0f);

        auto okIcon = CCSprite::createWithSpriteFrameName("GJ_completesIcon_001.png");
        if (!okIcon) okIcon = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        if (okIcon) okIcon->setScale(0.55f);
        auto okCircle = CircleButtonSprite::create(okIcon, CircleBaseColor::Green, CircleBaseSize::Medium);
        CCSprite* okSpr = typeinfo_cast<CCSprite*>(okCircle);
        if (!okSpr) okSpr = okCircle;
        scaleSprite(okSpr, 30.0f);

        auto downloadSpr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png");
        if (!downloadSpr) {
            downloadSpr = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
        }
        scaleSprite(downloadSpr, 28.0f);

        auto editSpr = CCSprite::createWithSpriteFrameName("GJ_editBtn_001.png");
        if (!editSpr) {
            log::error("[CapturePreviewPopup] No se pudo crear el sprite del boton de editar");
            return false;
        }
        scaleSprite(editSpr, 30.0f);

        if (!cancelSpr || !okSpr) {
            log::error("[CapturePreviewPopup] No se pudieron crear los sprites de los botones");
            return false;
        }

        auto cancelBtn = CCMenuItemSpriteExtra::create(cancelSpr, this, menu_selector(CapturePreviewPopup::onCancelBtn));
        cancelBtn->setID("cancel-button"_spr);

        auto downloadBtn = CCMenuItemSpriteExtra::create(downloadSpr, this, menu_selector(CapturePreviewPopup::onDownloadBtn));
        downloadBtn->setID("download-button"_spr);

        auto editBtn = CCMenuItemSpriteExtra::create(editSpr, this, menu_selector(CapturePreviewPopup::onEditBtn));
        editBtn->setID("edit-button"_spr);

        auto okBtn = CCMenuItemSpriteExtra::create(okSpr, this, menu_selector(CapturePreviewPopup::onAcceptBtn));
        okBtn->setID("ok-button"_spr);
        okBtn->setTag(100);

        PaimonButtonHighlighter::registerButton(cancelBtn);
        PaimonButtonHighlighter::registerButton(downloadBtn);
        PaimonButtonHighlighter::registerButton(editBtn);
        PaimonButtonHighlighter::registerButton(okBtn);

        buttonMenu->setID("button-menu"_spr);
        buttonMenu->addChild(cancelBtn);
        buttonMenu->addChild(downloadBtn);
        buttonMenu->addChild(editBtn);
        buttonMenu->addChild(okBtn);

        buttonMenu->alignItemsHorizontallyWithPadding(22.f);
        buttonMenu->setPosition({content.width * 0.5f, 20.f});
        
        this->m_mainLayer->addChild(buttonMenu);
        
        log::info("[CapturePreviewPopup] Configuracion del popup de vista previa completada");

        return true;
}

void CapturePreviewPopup::onTogglePlayerBtn(CCObject* sender) {
    if (!sender) return;
    m_isPlayerHidden = !m_isPlayerHidden;
    if (m_recaptureCallback) {
        m_recaptureCallback(m_isPlayerHidden, this);
    } else {
        liveRecapture(true);
    }
}

void CapturePreviewPopup::updatePreviewScale() {
    if (!m_previewSprite || !this->m_mainLayer) return;
    auto content = this->m_mainLayer->getContentSize();
    const float kHorizontalMargin = 11.5f;
    const float kVerticalTotalMargin = 62.f;
    float previewAreaWidth = content.width - 2.f * kHorizontalMargin;
    float previewAreaHeight = content.height - kVerticalTotalMargin;

    float sx = previewAreaWidth / m_previewSprite->getContentWidth();
    float sy = previewAreaHeight / m_previewSprite->getContentHeight();
    float scale = m_fillMode ? std::max(sx, sy) : std::min(sx, sy);

    m_previewSprite->setScale(scale);
    if (m_clippingNode) {
        m_clippingNode->setContentSize({previewAreaWidth, previewAreaHeight});
    }
    log::debug("[CapturePreviewPopup] mode={}, area={}x{}, img={}x{}, scale={}",
        m_fillMode ? "fill" : "fit",
        previewAreaWidth, previewAreaHeight,
        m_previewSprite->getContentWidth(), m_previewSprite->getContentHeight(),
        scale);
}

void CapturePreviewPopup::onToggleFillBtn(CCObject* sender) {
    if (!sender) return;

    m_fillMode = !m_fillMode;
    updatePreviewScale();
    
    std::string msg = m_fillMode ? Localization::get().getString("preview.fill_mode_active") : Localization::get().getString("preview.fit_mode_active");
    PaimonNotify::create(msg.c_str(), NotificationIcon::Info)->show();
}

void CapturePreviewPopup::onClose(CCObject* sender) {
    log::info("[CapturePreviewPopup] Popup de vista previa cerrado");

    CaptureLayerEditorPopup::restoreAllLayers();

    if (!m_callbackExecuted && m_callback) {
        log::info("[CapturePreviewPopup] Ejecutando callback desde onClose (cerrado con X)");
        m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
        m_callbackExecuted = true;
    }
    
    Popup::onClose(sender);
}

void CapturePreviewPopup::recapture() {
    log::info("[CapturePreviewPopup] Capturando nuevamente el nivel {}", m_levelID);

    if (FramebufferCapture::hasPendingCapture()) {
        PaimonNotify::create(
            Localization::get().getString("layers.recapturing").c_str(),
            NotificationIcon::Warning
        )->show();
        return;
    }

    Ref<CapturePreviewPopup> safeRef = this;
    this->setVisible(false);

    FramebufferCapture::requestCapture(m_levelID,
        [safeRef](bool success, CCTexture2D* texture,
               std::shared_ptr<uint8_t> rgbaData, int width, int height) {
            Loader::get()->queueInMainThread(
                [safeRef, success, texture, rgbaData, width, height]() {
                    if (success && texture && rgbaData) {
                        safeRef->updateContent(texture, rgbaData, width, height);
                        log::info("[CapturePreviewPopup] Recapture OK: {}x{}",
                                  width, height);
                    } else {
                        log::error("[CapturePreviewPopup] La recaptura fallo");
                        PaimonNotify::create(
                            Localization::get().getString("layers.recapture_error").c_str(),
                            NotificationIcon::Error
                        )->show();
                    }
                    safeRef->setVisible(true);
                });
        });
}

void CapturePreviewPopup::liveRecapture(bool updateBuffer) {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;

    auto* pl = PlayLayer::get();
    if (!pl) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto* view   = CCEGLView::sharedOpenGLView();
    if (!view) return;

    auto frameSize = view->getFrameSize();
    int  w = static_cast<int>(frameSize.width);
    int  h = static_cast<int>(frameSize.height);

    std::vector<std::pair<CCNode*, bool>> hiddenSystem;

    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child || !child->isVisible() || child == pl) continue;

        bool isSystem = false;
        if (typeinfo_cast<FLAlertLayer*>(child)) isSystem = true;
        else {
            std::string cls = typeid(*child).name();
            if (cls.find("PauseLayer") != std::string::npos) isSystem = true;
        }

        if (isSystem) {
            hiddenSystem.push_back({child, true});
            child->setVisible(false);
        }
    }

    PlayerVisState p1State, p2State;
    if (m_isPlayerHidden) {
        paimTogglePlayer(pl->m_player1, p1State, true);
        paimTogglePlayer(pl->m_player2, p2State, true);
    }

    auto* rt = CCRenderTexture::create(w, h, kCCTexture2DPixelFormat_RGBA8888);
    if (!rt) {
        if (m_isPlayerHidden) {
            paimTogglePlayer(pl->m_player1, p1State, false);
            paimTogglePlayer(pl->m_player2, p2State, false);
        }
        for (auto& [node, _] : hiddenSystem) node->setVisible(true);
        return;
    }

    rt->begin();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    kmGLPushMatrix();
    float scaleX = static_cast<float>(w) / winSize.width;
    float scaleY = static_cast<float>(h) / winSize.height;
    kmGLScalef(scaleX, scaleY, 1.0f);
    scene->visit();
    kmGLPopMatrix();

    rt->end();

    if (m_isPlayerHidden) {
        paimTogglePlayer(pl->m_player1, p1State, false);
        paimTogglePlayer(pl->m_player2, p2State, false);
    }

    for (auto& [node, _] : hiddenSystem) node->setVisible(true);

    if (updateBuffer) {
        auto* img = rt->newCCImage(true);
        if (!img) return;

        int W = img->getWidth();
        int H = img->getHeight();
        unsigned char* data = img->getData();
        if (!data || W <= 0 || H <= 0) { img->release(); return; }

        size_t dataSize = static_cast<size_t>(W) * H * 4;
        std::shared_ptr<uint8_t> buffer(new uint8_t[dataSize],
                                        std::default_delete<uint8_t[]>());
        memcpy(buffer.get(), data, dataSize);
        img->release();

        auto* tex = new CCTexture2D();
        if (!tex->initWithData(buffer.get(), kCCTexture2DPixelFormat_RGBA8888,
                               W, H, CCSize(W, H))) {
            tex->release();
            return;
        }
        tex->setAntiAliasTexParameters();
        tex->autorelease();

        updateContent(tex, buffer, W, H);
        log::info("[CapturePreviewPopup] liveRecapture FULL OK: {}x{}", W, H);

    } else {
        auto* rtSprite = rt->getSprite();
        if (!rtSprite || !m_previewSprite) return;

        auto* rtTex = rtSprite->getTexture();
        if (!rtTex) return;

        m_previewSprite->setTexture(rtTex);
        m_previewSprite->setTextureRect(CCRect(0, 0, w, h));
        m_previewSprite->setFlipY(true);

        if (m_clippingNode) {
            m_previewSprite->setPosition(m_clippingNode->getContentSize() / 2);
        }
        updatePreviewScale();

        log::debug("[CapturePreviewPopup] Recaptura rapida exitosa: {}x{}", w, h);
    }
}

void CapturePreviewPopup::onAcceptBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Usuario acepto la captura para nivel {}", m_levelID);
    
    m_callbackExecuted = true;
    
    ThumbnailLoader::get().invalidateLevel(m_levelID);
    
    if (m_callback) {
        m_callback(true, m_levelID, m_buffer, m_width, m_height, "", "");
    }
    
    this->onClose(nullptr);
}

void CapturePreviewPopup::onEditBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Abriendo popup de edicion");

    auto editPopup = CaptureEditPopup::create(this);
    if (editPopup) {
        editPopup->show();
    }
}

void CapturePreviewPopup::onCancelBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Usuario cancelo la captura");
    
    m_callbackExecuted = true;
    
    if (m_callback) {
        m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
    }
    
    this->onClose(nullptr);
}

void CapturePreviewPopup::onCropBtn(CCObject* sender) {
    if (!sender) return;

    if (m_isCropped) {
        PaimonNotify::create(Localization::get().getString("preview.borders_removed").c_str(), NotificationIcon::Info)->show();
        return;
    }
    
    log::info("[CapturePreviewPopup] Detectando bordes negros");
    
    auto cropRect = detectBlackBorders();
    
    if (cropRect.width == m_width && cropRect.height == m_height) {
        log::info("[CapturePreviewPopup] No se detectaron bordes negros");
        PaimonNotify::create(Localization::get().getString("preview.no_borders").c_str(), NotificationIcon::Info)->show();
        return;
    }
    
    log::info("[CapturePreviewPopup] Bordes detectados: x={}, y={}, w={}, h={}", 
              cropRect.x, cropRect.y, cropRect.width, cropRect.height);
    
    applyCrop(cropRect);
    m_isCropped = true;
    
    PaimonNotify::create(Localization::get().getString("preview.borders_deleted").c_str(), NotificationIcon::Success)->show();
}

CapturePreviewPopup::CropRect CapturePreviewPopup::detectBlackBorders() {
    const int THRESHOLD = 20;
    const float BLACK_PERCENTAGE = 0.85f;
    const int SAMPLE_STEP = 4;
    
    const uint8_t* data = m_buffer.get();
    
    log::info("[CapturePreviewPopup] Iniciando deteccion de bordes en imagen {}x{}", m_width, m_height);
    
    auto isBlackPixel = [&](int x, int y) -> bool {
        int idx = (y * m_width + x) * 4;
        int r = data[idx];
        int g = data[idx + 1];
        int b = data[idx + 2];
        return (r <= THRESHOLD && g <= THRESHOLD && b <= THRESHOLD);
    };
    
    auto isBlackLine = [&](int linePos, bool isHorizontal) -> bool {
        int blackCount = 0;
        int totalSamples = 0;
        
        if (isHorizontal) {
            for (int x = 0; x < m_width; x += SAMPLE_STEP) {
                if (isBlackPixel(x, linePos)) blackCount++;
                totalSamples++;
            }
        } else {
            for (int y = 0; y < m_height; y += SAMPLE_STEP) {
                if (isBlackPixel(linePos, y)) blackCount++;
                totalSamples++;
            }
        }
        
        float blackRatio = (float)blackCount / totalSamples;
        return blackRatio >= BLACK_PERCENTAGE;
    };
    
    int top = 0;
    for (int y = 0; y < m_height / 2; ++y) {
        if (!isBlackLine(y, true)) {
            top = y;
            break;
        }
    }
    
    int bottom = m_height - 1;
    for (int y = m_height - 1; y >= m_height / 2; --y) {
        if (!isBlackLine(y, true)) {
            bottom = y;
            break;
        }
    }
    
    int left = 0;
    for (int x = 0; x < m_width / 2; ++x) {
        if (!isBlackLine(x, false)) {
            left = x;
            break;
        }
    }
    
    int right = m_width - 1;
    for (int x = m_width - 1; x >= m_width / 2; --x) {
        if (!isBlackLine(x, false)) {
            right = x;
            break;
        }
    }
    
    int cropWidth = right - left + 1;
    int cropHeight = bottom - top + 1;
    float cropRatio = (float)(cropWidth * cropHeight) / (m_width * m_height);
    
    log::info("[CapturePreviewPopup] Bordes detectados: L={}, T={}, R={}, B={} ({}x{}, {:.1f}% del original)", 
              left, top, right, bottom, cropWidth, cropHeight, cropRatio * 100.0f);
    
    if (cropRatio < 0.30f) {
        log::warn("[CapturePreviewPopup] Recorte demasiado agresivo ({:.1f}%), usando imagen original", cropRatio * 100.0f);
        return { 0, 0, m_width, m_height };
    }
    
    if (cropRatio > 0.99f) {
        log::info("[CapturePreviewPopup] No se detectaron bordes significativos");
        return { 0, 0, m_width, m_height };
    }
    
    return { left, top, cropWidth, cropHeight };
}

void CapturePreviewPopup::applyCrop(const CropRect& rect) {
    log::info("[CapturePreviewPopup] Aplicando recorte: {}x{} @ ({}, {})", rect.width, rect.height, rect.x, rect.y);
    
    size_t newSize = rect.width * rect.height * 4;
    std::shared_ptr<uint8_t> croppedBuffer(new uint8_t[newSize], std::default_delete<uint8_t[]>());
    
    const uint8_t* srcData = m_buffer.get();
    uint8_t* dstData = croppedBuffer.get();
    
    for (int y = 0; y < rect.height; ++y) {
        int srcY = rect.y + y;
        const uint8_t* srcRow = srcData + (srcY * m_width + rect.x) * 4;
        uint8_t* dstRow = dstData + y * rect.width * 4;
        memcpy(dstRow, srcRow, rect.width * 4);
    }
    
    m_buffer = croppedBuffer;
    m_width = rect.width;
    m_height = rect.height;
    
    auto* newTexture = new CCTexture2D();
    if (newTexture->initWithData(
        croppedBuffer.get(),
        kCCTexture2DPixelFormat_RGBA8888,
        rect.width,
        rect.height,
        CCSize(rect.width, rect.height)
    )) {
        newTexture->setAntiAliasTexParameters();
        newTexture->autorelease();
        m_texture = newTexture;
        
        if (m_previewSprite) {
            m_previewSprite->setTexture(newTexture);
            updatePreviewScale();
            
            if (m_clippingNode) {
                m_previewSprite->setPosition(m_clippingNode->getContentSize() / 2);
            }
            
            log::info("[CapturePreviewPopup] Textura y sprite actualizados exitosamente");
        }
    } else {
        log::error("[CapturePreviewPopup] No se pudo crear nueva textura");
        newTexture->release();
    }
}

void CapturePreviewPopup::onDownloadBtn(CCObject* sender) {
    if (!sender) return;
    if (!m_buffer || m_width <= 0 || m_height <= 0) {
        PaimonNotify::create(Localization::get().getString("preview.no_image").c_str(), NotificationIcon::Error)->show();
        return;
    }

    auto downloadDir = Mod::get()->getSaveDir() / "downloaded_thumbnails";
    std::error_code ec;
    if (!std::filesystem::exists(downloadDir)) {
        std::filesystem::create_directory(downloadDir, ec);
        if (ec) {
            log::error("Failed to create download directory: {}", ec.message());
            PaimonNotify::create(Localization::get().getString("preview.folder_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "thumbnail_" << m_levelID << "_" << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S") << ".png";
    auto filePath = downloadDir / ss.str();

    size_t dataSize = m_width * m_height * 4;
    
    auto img = new CCImage();
    if (img->initWithImageData(const_cast<uint8_t*>(m_buffer.get()), dataSize, CCImage::kFmtRawData, m_width, m_height, 8)) {
        std::thread([img, filePath = filePath, levelID = m_levelID]() {
            if (img->saveToFile(geode::utils::string::pathToString(filePath).c_str(), false)) {
                geode::Loader::get()->queueInMainThread([filePath, levelID]() {
                    PaimonNotify::create(Localization::get().getString("preview.downloaded").c_str(), geode::NotificationIcon::Success)->show();
                    log::info("[CapturePreviewPopup] Miniatura guardada en: {}", geode::utils::string::pathToString(filePath));
                    ThumbnailLoader::get().invalidateLevel(levelID);
                });
            } else {
                geode::Loader::get()->queueInMainThread([]() {
                    PaimonNotify::create(Localization::get().getString("preview.save_error").c_str(), geode::NotificationIcon::Error)->show();
                });
            }
            img->release();
        }).detach();
    } else {
        img->release();
        PaimonNotify::create(Localization::get().getString("preview.process_error").c_str(), NotificationIcon::Error)->show();
    }
}
