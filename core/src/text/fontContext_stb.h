#pragma once

#include "gl/glyphTexture.h"
#include "labels/textLabel.h"
#include "style/textStyle.h"
#include "util/fontDescription.h"

#include <bitset>
#include <mutex>

struct FONScontext;

namespace Tangram {

class FontContext {

public:
    static constexpr int max_textures = 64;

    FontContext(Platform& _platform);
    virtual ~FontContext();

    void loadFonts(const std::vector<FontSourceHandle>& fallbacks);
    void releaseAtlas(std::bitset<max_textures> _refs);

    /* Update all textures batches, uploads the data to the GPU */
    void updateTextures(RenderState& rs);

    int getFont(const std::string& _family, const std::string& _style,
                       const std::string& _weight, float _size);

    size_t glyphTextureCount() {
        std::lock_guard<std::mutex> lock(m_textureMutex);
        return m_textures.size();
    }

    void bindTexture(RenderState& rs, int _id, GLuint _unit);

    float maxStrokeWidth() { return m_sdfRadius; }

    bool layoutText(TextStyle::Parameters& _params, const std::string& _text,
                    std::vector<GlyphQuad>& _quads, std::bitset<max_textures>& _refs,
                    glm::vec2& _bbox, TextRange& _textRanges);

    void addFont(const FontDescription& _ft, std::vector<char>&& _data);

    void setPixelScale(float _scale);

    // called for memory warning or almost out of GlyphTextures; tiles and markers must be rebuilt
    void releaseFonts();

private:
    int addTexture();
    void flushTextTexture();
    int loadFontSource(const std::string& _name, const FontSourceHandle& _source);
    int layoutMultiline(TextStyle::Parameters& _params, const std::string& _text,
        TextLabelProperty::Align _align, std::vector<GlyphQuad>& _quads);
    bool layoutLine(TextStyle::Parameters& _params, float x, float y,
        const char* start, const char* end, std::vector<GlyphQuad>& _quads);

    std::mutex m_fontMutex;
    std::mutex m_textureMutex;

    float m_sdfRadius;
    FONScontext* m_fons;
    std::array<int, max_textures> m_atlasRefCount = {{0}};
    std::vector< std::vector<char> > m_sources;
    std::vector<std::unique_ptr<GlyphTexture>> m_textures;
    Platform& m_platform;
};

}
