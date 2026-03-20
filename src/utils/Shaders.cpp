#include "Shaders.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/Log.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace Shaders {

CCGLProgram* getOrCreateShader(char const* key, char const* vertexSrc, char const* fragmentSrc) {
    auto shaderCache = CCShaderCache::sharedShaderCache();
    if (auto program = shaderCache->programForKey(key)) {
        return program;
    }

    auto program = new CCGLProgram();
    program->initWithVertexShaderByteArray(vertexSrc, fragmentSrc);
    program->addAttribute("a_position", kCCVertexAttrib_Position);
    program->addAttribute("a_color", kCCVertexAttrib_Color);
    program->addAttribute("a_texCoord", kCCVertexAttrib_TexCoords);

    if (!program->link()) {
        geode::log::error("failed to link shader: {}", key);
        program->release();
        return nullptr;
    }

    program->updateUniforms();
    shaderCache->addProgram(program, key);
    program->release();
    return program;
}

void applyBlurPass(CCSprite* input, CCRenderTexture* output, CCGLProgram* program, CCSize const& size, float radius) {
    input->setShaderProgram(program);
    input->setPosition(size * 0.5f);

    program->use();
    program->setUniformsForBuiltins();
    program->setUniformLocationWith2f(
        program->getUniformLocationForName("u_screenSize"),
        size.width, size.height
    );
    program->setUniformLocationWith1f(
        program->getUniformLocationForName("u_radius"),
        radius
    );

    output->begin();
    input->visit();
    output->end();
}

float intensityToBlurRadius(float intensity) {
    float normalized = std::clamp((intensity - 1.0f) / 9.0f, 0.0f, 1.0f);
    return 0.02f + (normalized * 0.20f);
}

CCSprite* createBlurredSprite(CCTexture2D* texture, CCSize const& targetSize, float intensity, bool useDirectRadius) {
    if (!texture) return nullptr;
    if (targetSize.width <= 0.f || targetSize.height <= 0.f ||
        targetSize.width > 4096.f || targetSize.height > 4096.f) return nullptr;

    auto srcSprite = CCSprite::createWithTexture(texture);
    if (!srcSprite) return nullptr;

    float scaleX = targetSize.width / texture->getContentSize().width;
    float scaleY = targetSize.height / texture->getContentSize().height;
    float scale = std::max(scaleX, scaleY);

    srcSprite->setScale(scale);
    srcSprite->setAnchorPoint({0.5f, 0.5f});
    srcSprite->setPosition(targetSize * 0.5f);

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);

    auto blurH = getOrCreateShader("blur-horizontal"_spr, vertexShaderCell, fragmentShaderHorizontal);
    auto blurV = getOrCreateShader("blur-vertical"_spr, vertexShaderCell, fragmentShaderVertical);

    if (!blurH || !blurV) {
        return srcSprite;
    }

    auto rtA = CCRenderTexture::create(targetSize.width, targetSize.height);
    auto rtB = CCRenderTexture::create(targetSize.width, targetSize.height);

    if (!rtA || !rtB) {
        return srcSprite;
    }

    float radius = useDirectRadius ? intensity : intensityToBlurRadius(intensity);

    applyBlurPass(srcSprite, rtA, blurH, targetSize, radius);

    auto midSprite = CCSprite::createWithTexture(rtA->getSprite()->getTexture());
    midSprite->setFlipY(true);
    midSprite->setAnchorPoint({0.5f, 0.5f});
    midSprite->setPosition(targetSize * 0.5f);
    midSprite->getTexture()->setTexParameters(&params);

    applyBlurPass(midSprite, rtB, blurV, targetSize, radius);

    if (!useDirectRadius && intensity > 5.0f) {
        auto mid2 = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
        mid2->setFlipY(true);
        mid2->setAnchorPoint({0.5f, 0.5f});
        mid2->setPosition(targetSize * 0.5f);
        mid2->getTexture()->setTexParameters(&params);

        applyBlurPass(mid2, rtA, blurH, targetSize, radius * 0.8f);

        auto mid3 = CCSprite::createWithTexture(rtA->getSprite()->getTexture());
        mid3->setFlipY(true);
        mid3->setAnchorPoint({0.5f, 0.5f});
        mid3->setPosition(targetSize * 0.5f);
        mid3->getTexture()->setTexParameters(&params);

        applyBlurPass(mid3, rtB, blurV, targetSize, radius * 0.8f);
    }

    auto finalSprite = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
    finalSprite->setFlipY(true);
    finalSprite->setAnchorPoint({0.5f, 0.5f});
    finalSprite->getTexture()->setTexParameters(&params);

    return finalSprite;
}

CCGLProgram* getBlurCellShader() {
    return getOrCreateShader("paimon_cell_blur", vertexShaderCell, fragmentShaderBlurCell);
}

CCGLProgram* getBlurSinglePassShader() {
    return getOrCreateShader("blur-single"_spr, vertexShaderCell, fragmentShaderBlurSinglePass);
}

CCGLProgram* getBgShaderProgram(std::string const& shaderName) {
    if (shaderName.empty() || shaderName == "none") return nullptr;
    CCGLProgram* p = nullptr;
    if (shaderName == "grayscale") p = getOrCreateShader("layerbg-gray"_spr, vertexShaderCell, fragmentShaderGrayscale);
    else if (shaderName == "sepia") p = getOrCreateShader("layerbg-sepia"_spr, vertexShaderCell, fragmentShaderSepia);
    else if (shaderName == "vignette") p = getOrCreateShader("layerbg-vignette"_spr, vertexShaderCell, fragmentShaderVignette);
    else if (shaderName == "bloom") p = getOrCreateShader("layerbg-bloom"_spr, vertexShaderCell, fragmentShaderBloom);
    else if (shaderName == "chromatic") p = getOrCreateShader("layerbg-chromatic"_spr, vertexShaderCell, fragmentShaderChromatic);
    else if (shaderName == "pixelate") p = getOrCreateShader("layerbg-pixelate"_spr, vertexShaderCell, fragmentShaderPixelate);
    else if (shaderName == "posterize") p = getOrCreateShader("layerbg-posterize"_spr, vertexShaderCell, fragmentShaderPosterize);
    else if (shaderName == "scanlines") p = getOrCreateShader("layerbg-scanlines"_spr, vertexShaderCell, fragmentShaderScanlines);
    return p;
}

} // namespace Shaders


