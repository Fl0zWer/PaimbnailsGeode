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
#include <Geode/cocos/platform/CCGL.h>
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../utils/PlayerToggleHelper.hpp"
#include "../utils/RenderTexture.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace geode::prelude;
using namespace cocos2d;

// ─── helpers ────────────────────────────────────────────────────────
float CapturePreviewPopup::clampF(float value, float mn, float mx) {
    return std::max(mn, std::min(mx, value));
}

// ─── factory ────────────────────────────────────────────────────────
CapturePreviewPopup* CapturePreviewPopup::create(
    CCTexture2D* texture, int levelID,
    std::shared_ptr<uint8_t> buffer, int width, int height,
    geode::CopyableFunction<void(bool, int, std::shared_ptr<uint8_t>, int, int, std::string, std::string)> callback,
    geode::CopyableFunction<void(bool, CapturePreviewPopup*)> recaptureCallback,
    bool isPlayerHidden, bool isModerator
) {
    if (!texture) return nullptr;

    auto ret = new CapturePreviewPopup();
    ret->m_texture = texture;
    ret->m_levelID = levelID;
    ret->m_buffer  = buffer;
    ret->m_width   = width;
    ret->m_height  = height;
    ret->m_callback          = std::move(callback);
    ret->m_recaptureCallback = std::move(recaptureCallback);
    ret->m_isPlayerHidden    = isPlayerHidden;
    ret->m_isModerator       = isModerator;

    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CapturePreviewPopup::~CapturePreviewPopup() {
    m_activeTouches.clear();
}

// ─── updateContent ──────────────────────────────────────────────────
void CapturePreviewPopup::updateContent(CCTexture2D* texture,
    std::shared_ptr<uint8_t> buffer, int width, int height)
{
    if (!texture) return;

    m_texture  = texture;
    m_buffer   = buffer;
    m_width    = width;
    m_height   = height;
    m_isCropped = false;

    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }

    m_previewSprite = CCSprite::createWithTexture(m_texture);
    if (!m_previewSprite) return;

    m_previewSprite->setAnchorPoint({0.5f, 0.5f});
    m_previewSprite->setFlipY(false);

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    m_texture->setTexParameters(&params);

    m_previewSprite->setID("preview-sprite"_spr);

    if (m_clippingNode) {
        m_previewSprite->setPosition(
            ccp(m_clippingNode->getContentSize().width / 2,
                m_clippingNode->getContentSize().height / 2));
        m_clippingNode->addChild(m_previewSprite, 10);
        // Reset zoom/anchor state on content update
        m_wasZooming = false;
        m_activeTouches.clear();
        updatePreviewScale();
    }
}

// ─── init ───────────────────────────────────────────────────────────
bool CapturePreviewPopup::init() {
    // Popup grande (mismo tamano que LocalThumbnailViewPopup)
    if (!Popup::init(400.f, 280.f)) return false;

    this->setTitle(Localization::get().getString("preview.title").c_str());

    // Nota: la musica se pausa en PlayLayer al presionar la tecla de captura,
    // no aqui, para evitar desincronizacion.

    // Ocultar fondo por defecto del popup (igual que LocalThumbnailViewPopup)
    if (m_bgSprite) m_bgSprite->setVisible(false);

    auto content = m_mainLayer->getContentSize();

    if (!m_texture) return false;

    // ── area de preview ─────────────────────────────────────────
    float maxWidth  = content.width  - 40.f;
    float maxHeight = content.height - 80.f;
    m_viewWidth  = maxWidth;
    m_viewHeight = maxHeight;

    // Stencil para clipping
    auto stencil = CCScale9Sprite::create("square02_001.png");
    if (!stencil) stencil = CCScale9Sprite::create();
    stencil->setContentSize({maxWidth, maxHeight});
    stencil->setPosition(ccp(maxWidth / 2, maxHeight / 2));

    m_clippingNode = CCClippingNode::create(stencil);
    m_clippingNode->setAlphaThreshold(0.1f);
    m_clippingNode->setContentSize({maxWidth, maxHeight});
    m_clippingNode->setAnchorPoint({0.5f, 0.5f});
    m_clippingNode->setPosition(ccp(content.width / 2, content.height / 2 + 5.f));
    m_clippingNode->setID("preview-clip"_spr);
    m_mainLayer->addChild(m_clippingNode, 1);

    // Fondo oscuro dentro del clipping
    auto clippingBg = CCLayerColor::create(ccc4(0, 0, 0, 200));
    clippingBg->setContentSize({maxWidth, maxHeight});
    clippingBg->ignoreAnchorPointForPosition(false);
    clippingBg->setAnchorPoint({0.5f, 0.5f});
    clippingBg->setPosition(ccp(maxWidth / 2, maxHeight / 2));
    m_clippingNode->addChild(clippingBg, -1);

    // Borde decorativo (GJ_square07 como en LocalThumbnailViewPopup)
    auto border = CCScale9Sprite::create("GJ_square07.png");
    if (border) {
        border->setContentSize({maxWidth + 4.f, maxHeight + 4.f});
        border->setPosition(ccp(content.width / 2, content.height / 2 + 5.f));
        border->setID("preview-border"_spr);
        m_mainLayer->addChild(border, 2);
    }

    // ── sprite de preview ────────────────────────────────────────
    m_previewSprite = CCSprite::createWithTexture(m_texture);
    if (!m_previewSprite) return false;

    m_previewSprite->setAnchorPoint({0.5f, 0.5f});
    m_previewSprite->setFlipY(false);
    m_previewSprite->setID("preview-sprite"_spr);

    ccTexParams texParams{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    m_texture->setTexParameters(&texParams);

    // Escalar para fill (max) — igual que LocalThumbnailViewPopup
    float scaleX = maxWidth  / m_previewSprite->getContentSize().width;
    float scaleY = maxHeight / m_previewSprite->getContentSize().height;
    float scale  = std::max(scaleX, scaleY);

    m_previewSprite->setScale(scale);
    m_initialScale = scale;
    m_minScale     = scale;
    m_maxScale     = std::max(4.0f, scale * 6.0f);

    m_previewSprite->setPosition(ccp(maxWidth / 2, maxHeight / 2));
    m_clippingNode->addChild(m_previewSprite, 10);

    // ── boton X (reposicionar al borde del area preview) ────────
    if (m_closeBtn) {
        float topY  = (content.height / 2 + 5.f) + (maxHeight / 2);
        float leftX = (content.width - maxWidth) / 2;
        m_closeBtn->setPosition(ccp(leftX - 3.f, topY + 3.f));
    }

    // ── barra de botones inferior (RowLayout) ───────────────────
    m_buttonMenu = CCMenu::create();
    m_buttonMenu->setID("button-menu"_spr);

    // Helper: normaliza un sprite para que encaje en targetSize x targetSize
    const float targetSize = 30.f;
    auto normalizeSprite = [&](CCSprite* spr) {
        if (!spr) return;
        auto cs = spr->getContentSize();
        float maxDim = std::max(cs.width, cs.height);
        if (maxDim > 0.f) spr->setScale(targetSize / maxDim);
    };

    auto cancelSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
    if (!cancelSpr) cancelSpr = CCSprite::createWithSpriteFrameName("edit_delIcon_001.png");
    normalizeSprite(cancelSpr);
    auto cancelBtn = CCMenuItemSpriteExtra::create(
        cancelSpr, this, menu_selector(CapturePreviewPopup::onCancelBtn));
    cancelBtn->setID("cancel-button"_spr);

    auto downloadSpr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png");
    if (!downloadSpr) downloadSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    normalizeSprite(downloadSpr);
    auto downloadBtn = CCMenuItemSpriteExtra::create(
        downloadSpr, this, menu_selector(CapturePreviewPopup::onDownloadBtn));
    downloadBtn->setID("download-button"_spr);

    auto editSpr = CCSprite::createWithSpriteFrameName("GJ_editBtn_001.png");
    normalizeSprite(editSpr);
    auto editBtn = CCMenuItemSpriteExtra::create(
        editSpr, this, menu_selector(CapturePreviewPopup::onEditBtn));
    editBtn->setID("edit-button"_spr);

    auto okSpr = CCSprite::createWithSpriteFrameName("GJ_completesIcon_001.png");
    if (!okSpr) okSpr = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    normalizeSprite(okSpr);
    auto okBtn = CCMenuItemSpriteExtra::create(
        okSpr, this, menu_selector(CapturePreviewPopup::onAcceptBtn));
    okBtn->setID("ok-button"_spr);

    // Boton recentrar
    auto recenterSpr = CCSprite::createWithSpriteFrameName("gj_findBtnOff_001.png");
    if (!recenterSpr) recenterSpr = CCSprite::createWithSpriteFrameName("GJ_undoBtn_001.png");
    normalizeSprite(recenterSpr);
    auto recenterBtn = CCMenuItemSpriteExtra::create(
        recenterSpr, this, menu_selector(CapturePreviewPopup::onRecenterBtn));
    recenterBtn->setID("recenter-button"_spr);

    m_buttonMenu->addChild(cancelBtn);
    m_buttonMenu->addChild(downloadBtn);
    m_buttonMenu->addChild(recenterBtn);
    m_buttonMenu->addChild(editBtn);
    m_buttonMenu->addChild(okBtn);

    m_buttonMenu->ignoreAnchorPointForPosition(false);
    m_buttonMenu->setAnchorPoint({0.5f, 0.5f});
    m_buttonMenu->setContentSize({content.width - 40.f, 40.f});
    m_buttonMenu->setPosition(ccp(content.width / 2, 45.f));

    auto layout = RowLayout::create();
    layout->setGap(18.f);
    layout->setAxisAlignment(AxisAlignment::Center);
    layout->setCrossAxisAlignment(AxisAlignment::Center);
    m_buttonMenu->setLayout(layout);
    m_buttonMenu->updateLayout();

    m_mainLayer->addChild(m_buttonMenu, 10);

    // ── activar touch/scroll ────────────────────────────────────
    this->setTouchEnabled(true);
#if defined(GEODE_IS_WINDOWS)
    this->setMouseEnabled(true);
    this->setKeypadEnabled(true);
#endif

    return true;
}

// ─── updatePreviewScale ────────────────────────────────────────────
void CapturePreviewPopup::updatePreviewScale() {
    if (!m_previewSprite || m_viewWidth < 1.f || m_viewHeight < 1.f) return;

    float imgW = m_previewSprite->getContentSize().width;
    float imgH = m_previewSprite->getContentSize().height;
    if (imgW < 1.f || imgH < 1.f) return;

    float scaleX = m_viewWidth  / imgW;
    float scaleY = m_viewHeight / imgH;
    float scale  = m_fillMode ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);

    m_previewSprite->setScale(scale);
    m_previewSprite->setAnchorPoint({0.5f, 0.5f});

    m_initialScale = scale;
    m_minScale     = scale;
    m_maxScale     = std::max(4.0f, scale * 6.0f);

    if (m_clippingNode) {
        m_previewSprite->setPosition(
            ccp(m_clippingNode->getContentSize().width / 2,
                m_clippingNode->getContentSize().height / 2));
    }
}

// ─── button handlers ───────────────────────────────────────────────
void CapturePreviewPopup::onTogglePlayerBtn(CCObject* sender) {
    if (!sender) return;
    m_isPlayerHidden = !m_isPlayerHidden;
    if (m_recaptureCallback) {
        m_recaptureCallback(m_isPlayerHidden, this);
    } else {
        liveRecapture(true);
    }
}

void CapturePreviewPopup::onToggleFillBtn(CCObject* sender) {
    if (!sender) return;
    m_fillMode = !m_fillMode;
    updatePreviewScale();
    auto msg = m_fillMode
        ? Localization::get().getString("preview.fill_mode_active")
        : Localization::get().getString("preview.fit_mode_active");
    PaimonNotify::create(msg.c_str(), NotificationIcon::Info)->show();
}

void CapturePreviewPopup::onRecenterBtn(CCObject*) {
    if (!m_previewSprite) return;
    m_previewSprite->stopAllActions();
    m_previewSprite->setAnchorPoint({0.5f, 0.5f});
    updatePreviewScale();
}

void CapturePreviewPopup::onClose(CCObject* sender) {
    CaptureLayerEditorPopup::restoreAllLayers();

    // Reanudar musica
    if (auto* engine = FMODAudioEngine::sharedEngine()) {
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setPaused(false);
        }
    }

    m_activeTouches.clear();

    if (!m_callbackExecuted && m_callback) {
        m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
        m_callbackExecuted = true;
    }
    Popup::onClose(sender);
}

void CapturePreviewPopup::recapture() {
    if (FramebufferCapture::hasPendingCapture()) {
        PaimonNotify::create(
            Localization::get().getString("layers.recapturing").c_str(),
            NotificationIcon::Warning)->show();
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
                    } else {
                        PaimonNotify::create(
                            Localization::get().getString("layers.recapture_error").c_str(),
                            NotificationIcon::Error)->show();
                    }
                    safeRef->setVisible(true);
                });
        });
}

void CapturePreviewPopup::liveRecapture(bool updateBuffer) {
    auto* pl = PlayLayer::get();
    if (!pl) return;

    // Use the same custom RenderTexture as captureScreenshot in PlayLayer
    // It properly adjusts m_fScaleX/Y, m_obScreenSize and design resolution,
    // which ensures ShaderLayer FBOs resolve correctly.
    auto* view = CCEGLView::sharedOpenGLView();
    if (!view) return;
    auto screenSize = view->getFrameSize();
    int w = static_cast<int>(screenSize.width);
    int h = static_cast<int>(screenSize.height);
    if (w <= 0 || h <= 0) return;

    // Hide UI layer
    bool uiWasVisible = false;
    if (pl->m_uiLayer && pl->m_uiLayer->isVisible()) {
        uiWasVisible = true;
        pl->m_uiLayer->setVisible(false);
    }

    PlayerVisState p1State, p2State;
    if (m_isPlayerHidden) {
        paimTogglePlayer(pl->m_player1, p1State, true);
        paimTogglePlayer(pl->m_player2, p2State, true);
    }

    // The custom RenderTexture adjusts CCEGLView scale factors,
    // design resolution and viewport — making ShaderLayer render
    // correctly into our FBO.
    ::RenderTexture rt(w, h);
    rt.begin();
    pl->visit();
    rt.end();
    auto data = rt.getData();

    // Restore state
    if (m_isPlayerHidden) {
        paimTogglePlayer(pl->m_player1, p1State, false);
        paimTogglePlayer(pl->m_player2, p2State, false);
    }
    if (uiWasVisible && pl->m_uiLayer) {
        pl->m_uiLayer->setVisible(true);
    }

    if (!data) return;

    // Vertical flip (glReadPixels reads bottom-to-top)
    int rowSize = w * 4;
    std::vector<uint8_t> tempRow(rowSize);
    uint8_t* buf = data.get();
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* topRow = buf + y * rowSize;
        uint8_t* bottomRow = buf + (h - y - 1) * rowSize;
        std::memcpy(tempRow.data(), topRow, rowSize);
        std::memcpy(topRow, bottomRow, rowSize);
        std::memcpy(bottomRow, tempRow.data(), rowSize);
    }

    if (updateBuffer) {
        size_t dataSize = static_cast<size_t>(w) * h * 4;
        std::shared_ptr<uint8_t> buffer(new uint8_t[dataSize], std::default_delete<uint8_t[]>());
        memcpy(buffer.get(), data.get(), dataSize);

        auto* tex = new CCTexture2D();
        if (!tex->initWithData(buffer.get(), kCCTexture2DPixelFormat_RGBA8888,
                               w, h, CCSize(static_cast<float>(w), static_cast<float>(h)))) {
            tex->release();
            return;
        }
        tex->setAntiAliasTexParameters();
        tex->autorelease();
        updateContent(tex, buffer, w, h);
    } else {
        // Fast path: create texture from data directly for visual-only update
        auto* tex = new CCTexture2D();
        if (!tex->initWithData(data.get(), kCCTexture2DPixelFormat_RGBA8888,
                               w, h, CCSize(static_cast<float>(w), static_cast<float>(h)))) {
            tex->release();
            return;
        }
        tex->setAntiAliasTexParameters();

        if (m_previewSprite) {
            m_previewSprite->setTexture(tex);
            m_previewSprite->setTextureRect(CCRect(0, 0,
                static_cast<float>(w), static_cast<float>(h)));
            m_previewSprite->setFlipY(false);
            updatePreviewScale();
        }
        tex->release();
    }
}

void CapturePreviewPopup::onAcceptBtn(CCObject* sender) {
    if (!sender) return;
    m_callbackExecuted = true;
    ThumbnailLoader::get().invalidateLevel(m_levelID);
    if (m_callback) m_callback(true, m_levelID, m_buffer, m_width, m_height, "", "");
    this->onClose(nullptr);
}

void CapturePreviewPopup::onEditBtn(CCObject* sender) {
    if (!sender) return;
    auto editPopup = CaptureEditPopup::create(this);
    if (editPopup) editPopup->show();
}

void CapturePreviewPopup::onCancelBtn(CCObject* sender) {
    if (!sender) return;
    m_callbackExecuted = true;
    if (m_callback) m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
    this->onClose(nullptr);
}

// ─── crop ──────────────────────────────────────────────────────────
void CapturePreviewPopup::onCropBtn(CCObject* sender) {
    if (!sender) return;
    if (!m_buffer || m_width <= 0 || m_height <= 0) return;

    if (m_isCropped) {
        PaimonNotify::create(Localization::get().getString("preview.borders_removed").c_str(),
            NotificationIcon::Info)->show();
        return;
    }

    auto cropRect = detectBlackBorders();
    if (cropRect.width == m_width && cropRect.height == m_height) {
        PaimonNotify::create(Localization::get().getString("preview.no_borders").c_str(),
            NotificationIcon::Info)->show();
        return;
    }

    applyCrop(cropRect);
    m_isCropped = true;
    PaimonNotify::create(Localization::get().getString("preview.borders_deleted").c_str(),
        NotificationIcon::Success)->show();
}

CapturePreviewPopup::CropRect CapturePreviewPopup::detectBlackBorders() {
    if (!m_buffer || m_width <= 0 || m_height <= 0)
        return {0, 0, m_width, m_height};

    const int THRESHOLD = 20;
    const float BLACK_PERCENTAGE = 0.85f;
    const int SAMPLE_STEP = 4;
    const uint8_t* data = m_buffer.get();

    auto isBlackPixel = [&](int x, int y) -> bool {
        int idx = (y * m_width + x) * 4;
        return data[idx] <= THRESHOLD && data[idx+1] <= THRESHOLD && data[idx+2] <= THRESHOLD;
    };
    auto isBlackLine = [&](int linePos, bool isHorizontal) -> bool {
        int blackCount = 0, totalSamples = 0;
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
        return static_cast<float>(blackCount) / totalSamples >= BLACK_PERCENTAGE;
    };

    int top = 0, bottom = m_height - 1, left = 0, right = m_width - 1;
    for (int y = 0; y < m_height / 2; ++y) { if (!isBlackLine(y, true)) { top = y; break; } }
    for (int y = m_height - 1; y >= m_height / 2; --y) { if (!isBlackLine(y, true)) { bottom = y; break; } }
    for (int x = 0; x < m_width / 2; ++x) { if (!isBlackLine(x, false)) { left = x; break; } }
    for (int x = m_width - 1; x >= m_width / 2; --x) { if (!isBlackLine(x, false)) { right = x; break; } }

    int cropW = right - left + 1;
    int cropH = bottom - top + 1;
    float cropRatio = static_cast<float>(cropW * cropH) / (m_width * m_height);
    if (cropRatio < 0.30f || cropRatio > 0.99f) return {0, 0, m_width, m_height};
    return {left, top, cropW, cropH};
}

void CapturePreviewPopup::applyCrop(const CropRect& rect) {
    size_t newSize = static_cast<size_t>(rect.width) * rect.height * 4;
    std::shared_ptr<uint8_t> croppedBuffer(new uint8_t[newSize], std::default_delete<uint8_t[]>());
    const uint8_t* srcData = m_buffer.get();
    uint8_t* dstData = croppedBuffer.get();

    for (int y = 0; y < rect.height; ++y) {
        int srcY = rect.y + y;
        const uint8_t* srcRow = srcData + (srcY * m_width + rect.x) * 4;
        uint8_t* dstRow = dstData + y * rect.width * 4;
        memcpy(dstRow, srcRow, rect.width * 4);
    }

    auto* newTexture = new CCTexture2D();
    if (newTexture->initWithData(croppedBuffer.get(), kCCTexture2DPixelFormat_RGBA8888,
            rect.width, rect.height,
            CCSize(static_cast<float>(rect.width), static_cast<float>(rect.height)))) {
        newTexture->setAntiAliasTexParameters();
        newTexture->autorelease();
        updateContent(newTexture, croppedBuffer, rect.width, rect.height);
    } else {
        newTexture->release();
    }
}

// ─── download ──────────────────────────────────────────────────────
void CapturePreviewPopup::onDownloadBtn(CCObject* sender) {
    if (!sender) return;
    if (!m_buffer || m_width <= 0 || m_height <= 0) {
        PaimonNotify::create(Localization::get().getString("preview.no_image").c_str(),
            NotificationIcon::Error)->show();
        return;
    }

    auto downloadDir = Mod::get()->getSaveDir() / "downloaded_thumbnails";
    std::error_code ec;
    if (!std::filesystem::exists(downloadDir)) {
        std::filesystem::create_directory(downloadDir, ec);
        if (ec) {
            PaimonNotify::create(Localization::get().getString("preview.folder_error").c_str(),
                NotificationIcon::Error)->show();
            return;
        }
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &in_time_t);
#else
    localtime_r(&in_time_t, &tm_buf);
#endif
    ss << "thumbnail_" << m_levelID << "_" << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".png";
    auto filePath = downloadDir / ss.str();

    size_t dataSize = static_cast<size_t>(m_width) * m_height * 4;
    auto img = new CCImage();
    if (img->initWithImageData(const_cast<uint8_t*>(m_buffer.get()),
            static_cast<int>(dataSize), CCImage::kFmtRawData, m_width, m_height, 8)) {
        std::thread([img, filePath, levelID = m_levelID]() {
            if (img->saveToFile(geode::utils::string::pathToString(filePath).c_str(), false)) {
                geode::Loader::get()->queueInMainThread([filePath, levelID]() {
                    PaimonNotify::create(Localization::get().getString("preview.downloaded").c_str(),
                        geode::NotificationIcon::Success)->show();
                    ThumbnailLoader::get().invalidateLevel(levelID);
                });
            } else {
                geode::Loader::get()->queueInMainThread([]() {
                    PaimonNotify::create(Localization::get().getString("preview.save_error").c_str(),
                        geode::NotificationIcon::Error)->show();
                });
            }
            img->release();
        }).detach();
    } else {
        img->release();
        PaimonNotify::create(Localization::get().getString("preview.process_error").c_str(),
            NotificationIcon::Error)->show();
    }
}

// ═══════════════════════════════════════════════════════════════════
// ZOOM / PAN (copied from LocalThumbnailViewPopup pattern)
// ═══════════════════════════════════════════════════════════════════

void CapturePreviewPopup::clampSpritePosition() {
    if (!m_previewSprite || m_viewWidth <= 0 || m_viewHeight <= 0) return;

    float scale   = m_previewSprite->getScale();
    float spriteW = m_previewSprite->getContentSize().width  * scale;
    float spriteH = m_previewSprite->getContentSize().height * scale;

    CCPoint pos    = m_previewSprite->getPosition();
    CCPoint anchor = m_previewSprite->getAnchorPoint();

    float spriteLeft   = pos.x - spriteW * anchor.x;
    float spriteRight  = pos.x + spriteW * (1.f - anchor.x);
    float spriteBottom = pos.y - spriteH * anchor.y;
    float spriteTop    = pos.y + spriteH * (1.f - anchor.y);

    float newX = pos.x, newY = pos.y;

    if (spriteW <= m_viewWidth) {
        newX = m_viewWidth / 2;
    } else {
        if (spriteLeft  > 0)           newX = spriteW * anchor.x;
        if (spriteRight < m_viewWidth) newX = m_viewWidth - spriteW * (1.f - anchor.x);
    }
    if (spriteH <= m_viewHeight) {
        newY = m_viewHeight / 2;
    } else {
        if (spriteBottom > 0)            newY = spriteH * anchor.y;
        if (spriteTop    < m_viewHeight) newY = m_viewHeight - spriteH * (1.f - anchor.y);
    }

    m_previewSprite->setPosition(ccp(newX, newY));
}

void CapturePreviewPopup::clampSpritePositionAnimated() {
    if (!m_previewSprite || m_viewWidth <= 0 || m_viewHeight <= 0) return;

    float scale   = m_previewSprite->getScale();
    float spriteW = m_previewSprite->getContentSize().width  * scale;
    float spriteH = m_previewSprite->getContentSize().height * scale;

    CCPoint pos    = m_previewSprite->getPosition();
    CCPoint anchor = m_previewSprite->getAnchorPoint();

    float spriteLeft   = pos.x - spriteW * anchor.x;
    float spriteRight  = pos.x + spriteW * (1.f - anchor.x);
    float spriteBottom = pos.y - spriteH * anchor.y;
    float spriteTop    = pos.y + spriteH * (1.f - anchor.y);

    float newX = pos.x, newY = pos.y;
    bool needsAnim = false;

    if (spriteW <= m_viewWidth) {
        if (std::abs(newX - m_viewWidth / 2) > 0.5f) { newX = m_viewWidth / 2; needsAnim = true; }
    } else {
        if (spriteLeft  > 0)           { newX = spriteW * anchor.x;                      needsAnim = true; }
        if (spriteRight < m_viewWidth) { newX = m_viewWidth - spriteW * (1.f - anchor.x); needsAnim = true; }
    }
    if (spriteH <= m_viewHeight) {
        if (std::abs(newY - m_viewHeight / 2) > 0.5f) { newY = m_viewHeight / 2; needsAnim = true; }
    } else {
        if (spriteBottom > 0)            { newY = spriteH * anchor.y;                        needsAnim = true; }
        if (spriteTop    < m_viewHeight) { newY = m_viewHeight - spriteH * (1.f - anchor.y); needsAnim = true; }
    }

    if (needsAnim) {
        m_previewSprite->stopAllActions();
        m_previewSprite->runAction(CCEaseBackOut::create(CCMoveTo::create(0.15f, ccp(newX, newY))));
    }
}

bool CapturePreviewPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (!this->isVisible()) return false;

    auto nodePos = m_mainLayer->convertToNodeSpace(touch->getLocation());
    auto size    = m_mainLayer->getContentSize();
    if (!CCRect(0, 0, size.width, size.height).containsPoint(nodePos)) return false;

    // No capturar toque si esta sobre un boton
    auto isTouchOnMenu = [](CCMenu* menu, CCTouch* t) -> bool {
        if (!menu || !menu->isVisible()) return false;
        auto point = menu->convertTouchToNodeSpace(t);
        for (auto* obj : CCArrayExt<CCObject*>(menu->getChildren())) {
            auto item = typeinfo_cast<CCMenuItem*>(obj);
            if (item && item->isVisible() && item->isEnabled() &&
                item->boundingBox().containsPoint(point))
                return true;
        }
        return false;
    };
    if (isTouchOnMenu(m_buttonMenu, touch)) return false;

    // Segundo dedo → pinch zoom
    if (m_activeTouches.size() == 1) {
        auto firstTouch = *m_activeTouches.begin();
        if (firstTouch == touch) return true;

        auto firstLoc  = firstTouch->getLocation();
        auto secondLoc = touch->getLocation();
        m_touchMidPoint   = (firstLoc + secondLoc) / 2.0f;
        m_savedScale      = m_previewSprite ? m_previewSprite->getScale() : m_initialScale;
        m_initialDistance  = firstLoc.getDistance(secondLoc);

        if (m_previewSprite) {
            auto oldAnchor  = m_previewSprite->getAnchorPoint();
            auto worldPos   = m_previewSprite->convertToWorldSpace(ccp(0, 0));
            float newAX = clampF((m_touchMidPoint.x - worldPos.x) / m_previewSprite->getScaledContentSize().width,  0, 1);
            float newAY = clampF((m_touchMidPoint.y - worldPos.y) / m_previewSprite->getScaledContentSize().height, 0, 1);
            m_previewSprite->setAnchorPoint(ccp(newAX, newAY));
            m_previewSprite->setPosition(ccp(
                m_previewSprite->getPositionX() + m_previewSprite->getScaledContentSize().width  * -(oldAnchor.x - newAX),
                m_previewSprite->getPositionY() + m_previewSprite->getScaledContentSize().height * -(oldAnchor.y - newAY)
            ));
        }
    }

    m_activeTouches.insert(touch);
    return true;
}

void CapturePreviewPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_previewSprite) return;

    if (m_activeTouches.size() == 1) {
        auto delta = touch->getDelta();
        m_previewSprite->setPosition(ccp(
            m_previewSprite->getPositionX() + delta.x,
            m_previewSprite->getPositionY() + delta.y));
        clampSpritePosition();
    } else if (m_activeTouches.size() == 2) {
        m_wasZooming = true;
        auto it = m_activeTouches.begin();
        auto firstTouch  = *it; ++it;
        auto secondTouch = *it;

        auto firstLoc  = firstTouch->getLocation();
        auto secondLoc = secondTouch->getLocation();
        auto center    = (firstLoc + secondLoc) / 2.0f;
        float distNow  = std::max(0.1f, firstLoc.getDistance(secondLoc));

        float mult = std::max(0.0001f, m_initialDistance / distNow);
        float zoom = clampF(m_savedScale / mult, m_minScale, m_maxScale);
        m_previewSprite->setScale(zoom);

        auto centerDiff = m_touchMidPoint - center;
        m_previewSprite->setPosition(m_previewSprite->getPosition() - centerDiff);
        m_touchMidPoint = center;
        clampSpritePosition();
    }
}

void CapturePreviewPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_activeTouches.erase(touch);
    if (!m_previewSprite) return;

    if (m_wasZooming && m_activeTouches.size() == 1) {
        float scale = m_previewSprite->getScale();
        if (scale < m_minScale)
            m_previewSprite->runAction(CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_minScale)));
        else if (scale > m_maxScale)
            m_previewSprite->runAction(CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_maxScale)));
        m_wasZooming = false;
    }
    if (m_activeTouches.empty()) clampSpritePositionAnimated();
}

void CapturePreviewPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_activeTouches.erase(touch);
    m_wasZooming = false;
}

void CapturePreviewPopup::scrollWheel(float x, float y) {
    if (!m_previewSprite || !m_previewSprite->getParent()) return;

    float scrollAmount = y;
    if (std::abs(y) < 0.001f) scrollAmount = -x;

    float zoomFactor  = scrollAmount > 0 ? 1.12f : 0.89f;
    float curScale    = m_previewSprite->getScale();
    float newScale    = clampF(curScale * zoomFactor, m_minScale, m_maxScale);
    if (std::abs(newScale - curScale) < 0.001f) return;

    m_previewSprite->setScale(newScale);
    clampSpritePosition();
}
