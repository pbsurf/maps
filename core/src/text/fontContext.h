#pragma once

#ifdef FONTCONTEXT_STB
#include "fontContext_stb.h"
#else

#include "gl/glyphTexture.h"
#include "labels/textLabel.h"
#include "style/textStyle.h"
#include "text/textUtil.h"
#include "util/fontDescription.h"

#include "alfons/alfons.h"
#include "alfons/atlas.h"
#include "alfons/font.h"
#include "alfons/fontManager.h"
#include "alfons/inputSource.h"
#include "alfons/textBatch.h"
#include "alfons/textShaper.h"
#include <bitset>
#include <mutex>

namespace Tangram {

struct FontMetrics {
    float ascender, descender, lineHeight;
};

class FontContext : public alfons::TextureCallback {

public:
    using AtlasID = alfons::AtlasID;
    static constexpr int max_textures = 64;

    FontContext(Platform& _platform);
    virtual ~FontContext() {}

    void loadFonts(const std::vector<FontSourceHandle>& fallbacks);

    /* Synchronized on m_mutex on tile-worker threads
     * Called from alfons when a texture atlas needs to be created
     * Triggered from TextStyleBuilder::prepareLabel
     */
    void addTexture(alfons::AtlasID id, uint16_t width, uint16_t height) override;

    /* Synchronized on m_mutex, called tile-worker threads
     * Called from alfons when a glyph needs to be added the the atlas identified by id
     * Triggered from TextStyleBuilder::prepareLabel
     */
    void addGlyph(alfons::AtlasID id, uint16_t gx, uint16_t gy, uint16_t gw, uint16_t gh,
                  const unsigned char* src, uint16_t pad) override;

    void releaseAtlas(std::bitset<max_textures> _refs);

    /* Update all textures batches, uploads the data to the GPU */
    void updateTextures(RenderState& rs);

    FontHandle getFont(const std::string& _family, const std::string& _style,
                                          const std::string& _weight, float _size);

    size_t glyphTextureCount() {
        std::lock_guard<std::mutex> lock(m_textureMutex);
        return m_textures.size();
    }

    void bindTexture(RenderState& rs, AtlasID _id, GLuint _unit);

    float maxStrokeWidth() { return m_sdfRadius; }

    bool layoutText(TextStyle::Parameters& _params, const icu::UnicodeString& _text,
                    std::vector<GlyphQuad>& _quads, std::bitset<max_textures>& _refs,
                    glm::vec2& _bbox, TextRange& _textRanges);

    struct ScratchBuffer : public alfons::MeshCallback {
        void drawGlyph(const alfons::Quad& q, const alfons::AtlasGlyph& altasGlyph) override {}
        void drawGlyph(const alfons::Rect& q, const alfons::AtlasGlyph& atlasGlyph) override;
        std::vector<GlyphQuad>* quads;
    };

    void addFont(const FontDescription& _ft, std::vector<char>&& _data);  //alfons::InputSource _source);

    void setPixelScale(float _scale);

    void releaseFonts();

private:

    static const std::vector<float> s_fontRasterSizes;

    float m_sdfRadius;
    ScratchBuffer m_scratch;
    std::vector<unsigned char> m_sdfBuffer;

    std::mutex m_fontMutex;
    std::mutex m_textureMutex;

    std::array<int, max_textures> m_atlasRefCount = {{0}};
    alfons::GlyphAtlas m_atlas;

    alfons::FontManager m_alfons;
    std::array<std::shared_ptr<alfons::Font>, 3> m_font;

    std::vector<std::unique_ptr<GlyphTexture>> m_textures;

    // TextShaper to create <LineLayout> for a given text and Font
    alfons::TextShaper m_shaper;

    // TextBatch to 'draw' <LineLayout>s, i.e. creating glyph textures and glyph quads.
    // It is intialized with a TextureCallback implemented by FontContext for adding glyph
    // textures and a MeshCallback implemented by TextStyleBuilder for adding glyph quads.
    alfons::TextBatch m_batch;
    TextWrapper m_textWrapper;

    Platform& m_platform;

};

}
#endif
