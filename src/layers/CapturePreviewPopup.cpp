#include "CapturePreviewPopup.hpp"
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
    std::function<void(bool, int, std::shared_ptr<uint8_t>, int, int, std::string, std::string)> callback,
    std::function<void(bool, CapturePreviewPopup*)> recaptureCallback,
    bool isPlayerHidden,
    bool isModerator
) {
    log::info("Creating CapturePreviewPopup...");
    
    if (!texture) {
        log::error("Failed to create CapturePreviewPopup: texture is null");
        return nullptr;
    }
    
    auto ret = new CapturePreviewPopup();
    
    texture->retain();
    
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
        log::info("CapturePreviewPopup created successfully");
        return ret;
    }
    log::error("Failed to initialize CapturePreviewPopup");
    
    texture->release();
    
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void CapturePreviewPopup::updateContent(CCTexture2D* texture, std::shared_ptr<uint8_t> buffer, int width, int height) {
    if (!texture) return;

    if (m_texture) {
        m_texture->release();
    }

    texture->retain();
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
    if (m_texture) {
        m_texture->release();
        m_texture = nullptr;
    }
}

bool CapturePreviewPopup::init() {
    if (!Popup::init(320.f, 240.f)) return false;

    log::info("Setting up CapturePreviewPopup...");
    
    try {
        this->setTitle(Localization::get().getString("preview.title").c_str());
        
        auto content = this->m_mainLayer->getContentSize();
        log::debug("Main layer size: {}x{}", content.width, content.height);

        if (!m_texture) {
            log::error("Texture is null in setup");
            return false;
        }
        
        m_previewSprite = CCSprite::createWithTexture(m_texture);
        if (!m_previewSprite) {
            log::error("Failed to create preview sprite");
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
            log::error("Failed to create clipping node");
            return false;
        }
        m_clippingNode->setContentSize({previewAreaWidth, previewAreaHeight});
        m_clippingNode->ignoreAnchorPointForPosition(false);
        m_clippingNode->setAnchorPoint({0.5f, 0.5f});
        m_clippingNode->setPosition({content.width * 0.5f, content.height * 0.5f + 5.f});
        
        auto stencil = CCScale9Sprite::create("square02_001.png");
        if (!stencil) {
            log::warn("Failed to create stencil sprite, using fallback");
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
        log::debug("Clipping node added successfully");

        // menu botones principales
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
        
        // boton cancelar (circulo verde con x)
        auto cancelIcon = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        if (!cancelIcon) cancelIcon = CCSprite::createWithSpriteFrameName("edit_delIcon_001.png");
        if (cancelIcon) cancelIcon->setScale(0.4f);
        auto cancelCircle = CircleButtonSprite::create(cancelIcon, CircleBaseColor::Green, CircleBaseSize::Medium);
        CCSprite* cancelSpr = static_cast<CCSprite*>(cancelCircle);
        scaleSprite(cancelSpr, 30.0f);

        // boton aceptar (circulo verde con check)
        auto okIcon = CCSprite::createWithSpriteFrameName("GJ_completesIcon_001.png");
        if (!okIcon) okIcon = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        if (okIcon) okIcon->setScale(0.55f);
        auto okCircle = CircleButtonSprite::create(okIcon, CircleBaseColor::Green, CircleBaseSize::Medium);
        CCSprite* okSpr = static_cast<CCSprite*>(okCircle);
        scaleSprite(okSpr, 30.0f);

        // boton descargar
        auto downloadSpr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png");
        if (!downloadSpr) {
            downloadSpr = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
        }
        scaleSprite(downloadSpr, 28.0f);

        // boton editar (abre herramientas)
        auto editSpr = CCSprite::createWithSpriteFrameName("GJ_editBtn_001.png");
        if (!editSpr) {
            log::error("Failed to create edit button sprite");
            return false;
        }
        scaleSprite(editSpr, 30.0f);

        if (!cancelSpr || !okSpr) {
            log::error("Failed to create button sprites");
            return false;
        }

        auto cancelBtn = CCMenuItemSpriteExtra::create(cancelSpr, this, menu_selector(CapturePreviewPopup::onCancelBtn));
        cancelBtn->setID("cancel-button");

        auto downloadBtn = CCMenuItemSpriteExtra::create(downloadSpr, this, menu_selector(CapturePreviewPopup::onDownloadBtn));
        downloadBtn->setID("download-button");

        auto editBtn = CCMenuItemSpriteExtra::create(editSpr, this, menu_selector(CapturePreviewPopup::onEditBtn));
        editBtn->setID("edit-button");

        auto okBtn = CCMenuItemSpriteExtra::create(okSpr, this, menu_selector(CapturePreviewPopup::onAcceptBtn));
        okBtn->setID("ok-button");
        okBtn->setTag(100);

        PaimonButtonHighlighter::registerButton(cancelBtn);
        PaimonButtonHighlighter::registerButton(downloadBtn);
        PaimonButtonHighlighter::registerButton(editBtn);
        PaimonButtonHighlighter::registerButton(okBtn);

        buttonMenu->setID("button-menu");
        buttonMenu->addChild(cancelBtn);
        buttonMenu->addChild(downloadBtn);
        buttonMenu->addChild(editBtn);
        buttonMenu->addChild(okBtn);

        buttonMenu->alignItemsHorizontallyWithPadding(22.f);
        buttonMenu->setPosition({content.width * 0.5f, 20.f});
        
        this->m_mainLayer->addChild(buttonMenu);
        
        log::info("CapturePreviewPopup setup completed successfully");

        return true;
    } catch (std::exception& e) {
        log::error("Exception in CapturePreviewPopup::setup: {}", e.what());
        return false;
    } catch (...) {
        log::error("Unknown exception in CapturePreviewPopup::setup");
        return false;
    }
}

void CapturePreviewPopup::onTogglePlayerBtn(CCObject* sender) {
    if (!sender) return;
    if (m_recaptureCallback) {
        m_isPlayerHidden = !m_isPlayerHidden;
        m_recaptureCallback(m_isPlayerHidden, this);
    } else {
        log::warn("No recapture callback provided");
        Notification::create(Localization::get().getString("preview.player_toggle_error").c_str(), NotificationIcon::Error)->show();
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
    
    // recentra sprite al cambiar modo
    if (m_previewSprite && m_clippingNode) {
        m_previewSprite->setPosition(m_clippingNode->getContentSize() / 2);
    }
    
    std::string msg = m_fillMode ? Localization::get().getString("preview.fill_mode_active") : Localization::get().getString("preview.fit_mode_active");
    Notification::create(msg.c_str(), NotificationIcon::Info)->show();
}

void CapturePreviewPopup::onClose(CCObject* sender) {
    log::info("[CapturePreviewPopup] onClose llamado");

    // restaura visibilidad de capas
    CaptureLayerEditorPopup::restoreAllLayers();

    if (!m_callbackExecuted && m_callback) {
        log::info("[CapturePreviewPopup] Ejecutando callback desde onClose (cerrado con X)");
        m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
        m_callbackExecuted = true;
    }
    
    Popup::onClose(sender);
}

void CapturePreviewPopup::recapture() {
    log::info("[CapturePreviewPopup] Recapturing level {}...", m_levelID);

    if (FramebufferCapture::hasPendingCapture()) {
        Notification::create(
            Localization::get().getString("layers.recapturing").c_str(),
            NotificationIcon::Warning
        )->show();
        return;
    }

    this->retain();
    this->setVisible(false);

    FramebufferCapture::requestCapture(m_levelID,
        [this](bool success, CCTexture2D* texture,
               std::shared_ptr<uint8_t> rgbaData, int width, int height) {
            Loader::get()->queueInMainThread(
                [this, success, texture, rgbaData, width, height]() {
                    if (success && texture && rgbaData) {
                        this->updateContent(texture, rgbaData, width, height);
                        log::info("[CapturePreviewPopup] Recapture OK: {}x{}",
                                  width, height);
                    } else {
                        log::error("[CapturePreviewPopup] Recapture failed");
                        Notification::create(
                            Localization::get().getString("layers.recapture_error").c_str(),
                            NotificationIcon::Error
                        )->show();
                    }
                    this->setVisible(true);
                    this->release();
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

    // oculta overlays del sistema
    // popup hereda de flalertlayer, se ocultan todos
    // capas del usuario ya estan configuradas
    // no fuerza ocultar nada mas
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

    // renderiza escena completa a textura
    auto* rt = CCRenderTexture::create(w, h, kCCTexture2DPixelFormat_RGBA8888);
    if (!rt) {
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

    // restaura overlays del sistema
    for (auto& [node, _] : hiddenSystem) node->setVisible(true);

    if (updateBuffer) {
        // ruta completa: lee pixeles para buffer
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
            delete tex;
            return;
        }
        tex->setAntiAliasTexParameters();
        tex->autorelease();

        updateContent(tex, buffer, W, H);
        log::info("[CapturePreviewPopup] liveRecapture FULL OK: {}x{}", W, H);

    } else {
        // ruta rapida: reusa textura (sin gpu-cpu-gpu)
        // salta lectura costosa de pixeles
        // cambio de capas en tiempo real es instantaneo
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

        log::debug("[CapturePreviewPopup] liveRecapture FAST OK: {}x{}", w, h);
    }
}

void CapturePreviewPopup::onAcceptBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Usuario aceptó la captura para nivel {}", m_levelID);
    
    m_callbackExecuted = true;
    
    ThumbnailLoader::get().invalidateLevel(m_levelID);
    
    if (m_callback) {
        m_callback(true, m_levelID, m_buffer, m_width, m_height, "", "");
    }
    
    this->onClose(nullptr);
}

void CapturePreviewPopup::onEditBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Abriendo popup de edición");

    auto editPopup = CaptureEditPopup::create(this);
    if (editPopup) {
        editPopup->show();
    }
}

void CapturePreviewPopup::onCancelBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Usuario canceló la captura");
    
    m_callbackExecuted = true;
    
    if (m_callback) {
        m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
    }
    
    this->onClose(nullptr);
}

void CapturePreviewPopup::onCropBtn(CCObject* sender) {
    if (!sender) return;

    if (m_isCropped) {
        Notification::create(Localization::get().getString("preview.borders_removed").c_str(), NotificationIcon::Info)->show();
        return;
    }
    
    log::info("[CropBtn] Detectando bordes negros...");
    
    auto cropRect = detectBlackBorders();
    
    if (cropRect.width == m_width && cropRect.height == m_height) {
        log::info("[CropBtn] No se detectaron bordes negros");
        Notification::create(Localization::get().getString("preview.no_borders").c_str(), NotificationIcon::Info)->show();
        return;
    }
    
    log::info("[CropBtn] Bordes detectados: x={}, y={}, w={}, h={}", 
              cropRect.x, cropRect.y, cropRect.width, cropRect.height);
    
    applyCrop(cropRect);
    m_isCropped = true;
    
    Notification::create(Localization::get().getString("preview.borders_deleted").c_str(), NotificationIcon::Success)->show();
}

CapturePreviewPopup::CropRect CapturePreviewPopup::detectBlackBorders() {
    const int THRESHOLD = 20;
    const float BLACK_PERCENTAGE = 0.85f;
    const int SAMPLE_STEP = 4;
    
    const uint8_t* data = m_buffer.get();
    
    log::info("[detectBlackBorders] Iniciando detección en imagen {}x{}", m_width, m_height);
    
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
    
    log::info("[detectBlackBorders] Bordes detectados: L={}, T={}, R={}, B={} ({}x{}, {:.1f}% del original)", 
              left, top, right, bottom, cropWidth, cropHeight, cropRatio * 100.0f);
    
    if (cropRatio < 0.30f) {
        log::warn("[detectBlackBorders] Crop demasiado agresivo ({:.1f}%), usando imagen original", cropRatio * 100.0f);
        return { 0, 0, m_width, m_height };
    }
    
    if (cropRatio > 0.99f) {
        log::info("[detectBlackBorders] No se detectaron bordes significativos");
        return { 0, 0, m_width, m_height };
    }
    
    return { left, top, cropWidth, cropHeight };
}

void CapturePreviewPopup::applyCrop(const CropRect& rect) {
    log::info("[applyCrop] Aplicando crop: {}x{} @ ({}, {})", rect.width, rect.height, rect.x, rect.y);
    
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
            
            log::info("[applyCrop] Textura y sprite actualizados exitosamente");
        }
    } else {
        log::error("[applyCrop] No se pudo crear nueva textura");
        delete newTexture;
    }
}

void CapturePreviewPopup::onDownloadBtn(CCObject* sender) {
    if (!sender) return;
    if (!m_buffer || m_width <= 0 || m_height <= 0) {
        Notification::create(Localization::get().getString("preview.no_image").c_str(), NotificationIcon::Error)->show();
        return;
    }

    auto downloadDir = Mod::get()->getSaveDir() / "downloaded_thumbnails";
    std::error_code ec;
    if (!std::filesystem::exists(downloadDir)) {
        std::filesystem::create_directory(downloadDir, ec);
        if (ec) {
            log::error("Failed to create download directory: {}", ec.message());
            Notification::create(Localization::get().getString("preview.folder_error").c_str(), NotificationIcon::Error)->show();
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
            try {
                if (img->saveToFile(filePath.generic_string().c_str(), false)) {
                    geode::Loader::get()->queueInMainThread([filePath, levelID]() {
                        geode::Notification::create(Localization::get().getString("preview.downloaded").c_str(), geode::NotificationIcon::Success)->show();
                        log::info("Thumbnail saved to: {}", filePath.generic_string());
                        ThumbnailLoader::get().invalidateLevel(levelID);
                    });
                } else {
                    geode::Loader::get()->queueInMainThread([]() {
                        geode::Notification::create(Localization::get().getString("preview.save_error").c_str(), geode::NotificationIcon::Error)->show();
                    });
                }
            } catch(...) {
                 geode::Loader::get()->queueInMainThread([]() {
                    log::error("Unknown error in preview save thread");
                });
            }
            img->release();
        }).detach();
    } else {
        img->release();
        Notification::create(Localization::get().getString("preview.process_error").c_str(), NotificationIcon::Error)->show();
    }
}
