#include "text/fontContext_stb.h"

#include "log.h"
#include "platform.h"
#include <memory>

#define SDF_WIDTH 6
#define MIN_LINE_WIDTH 4

#include "fontstash.h"

namespace Tangram {

// Building:
//  make linux BUILD_TYPE=Release CMAKE_BUILD_OPTIONS=--verbose CMAKE_OPTIONS="-DTANGRAM_USE_FONTCONTEXT_STB=1"
// Getting rid of harfbuzz-icu-freetype saves about 3 MB in Release build (8.4 -> 5.6 MB)
// - dropping sqlite (mbtiles) gets us to 4.2 MB

// TODO:
// options for texture management (after getting working in single 256x256 texture)
// - add multi-texture support to fontstash
// - get rid of 256x256 hardcoding
// - single 256x(Nx256) texture for fontstash, presented as multiple 256x256 textures ... this for now

// how would we support multiple glyph sizes?
// - separate FONScontext for each size (since cached glyphs can't be shared), sharing font data
// - support multiple textures, and allow glyph size to be set per call
// - split larger glyphs into multiple tiles

// how to set sdf radius for fontstash?  compile time define for now
// - should change to passing in params (on creation) since pixel scale can be changed at runtime!

//const std::vector<float> FontContext::s_fontRasterSizes = { 16, 28, 40 };
constexpr int atlasFontPx = 32;

FontContext::FontContext(Platform& _platform) :
    m_sdfRadius(SDF_WIDTH),
    m_platform(_platform) {
  FONSparams params;
  params.flags = FONS_SDF | FONS_ZERO_TOPLEFT;  //FONS_DELAY_LOAD;
  params.sdfPadding = 4;
  params.sdfPixelDist = 128.0f/SDF_WIDTH/2;  // assumes pixel scale = 2

  m_fons = fonsCreateInternal(&params);
  fonsResetAtlas(m_fons, GlyphTexture::size, GlyphTexture::size, atlasFontPx, atlasFontPx, atlasFontPx);
  m_textures.push_back(std::make_unique<GlyphTexture>());
}

FontContext::~FontContext() {
    //std::lock_guard<std::mutex> lock(m_fontMutex);
    fonsDeleteInternal(m_fons);
    m_fons = NULL;
    m_sources.clear();
}

void FontContext::setPixelScale(float _scale) {
    m_sdfRadius = SDF_WIDTH * _scale;
}

void FontContext::loadFonts() {
    auto fallbacks = m_platform.systemFontFallbacksHandle();

    std::string fn("default");
    int nadded = 0;
    for (const auto& fallback : fallbacks) {
        if (!fallback.isValid()) {
            LOGD("Invalid fallback font");
            continue;
        }
        int fontid = loadFontSource(nadded ? fn + "-" + std::to_string(nadded) : fn, fallback);
        if (fontid < 0) {
            LOGW("Error loading fallback font");
        } else {
            fonsAddFallbackFont(m_fons, -1, fontid);  // add as global fallback
            ++nadded;
        }
    }
    if (!nadded) {
        LOGW("No fallback fonts available!");
    }
}

// Synchronized on m_mutex in layoutText(), called on tile-worker threads
void FontContext::flushTextTexture() {

    //std::lock_guard<std::mutex> lock(m_textureMutex);

    int dirty[4];
    if(m_textures.empty()) return;
    if (fonsValidateTexture(m_fons, dirty)) {
        int iw, ih;
        auto fonsData = (const unsigned char*)fonsGetTextureData(m_fons, &iw, &ih);
        int x = dirty[0];
        int y = dirty[1];
        int w = dirty[2] - dirty[0];
        int h = dirty[3] - dirty[1];

        // hardcoded for 256x256 textures
        int texidx = y >> 8;
        int ytex = y & 255;

        unsigned char* texData = m_textures[texidx]->buffer();
        unsigned char* dst = &texData[x + iw*ytex];
        const unsigned char* src = &fonsData[x + iw*y];
        for (int jj = 0; jj < h; ++jj) {
            std::memcpy(dst + (jj * iw), src + (jj * iw), w);
        }
        m_textures[texidx]->setRowsDirty(ytex, h);
    }
}

void FontContext::releaseAtlas(std::bitset<max_textures> _refs) {
    if (!_refs.any()) { return; }
    std::lock_guard<std::mutex> lock(m_textureMutex);

    for (size_t i = 0; i < m_textures.size(); i++) {
        if (_refs[i]) { m_atlasRefCount[i] -= 1; }
    }
}

void FontContext::updateTextures(RenderState& rs) {
    std::lock_guard<std::mutex> lock(m_textureMutex);

    flushTextTexture();
    for (auto& gt : m_textures) { gt->bind(rs, 0); }
}

void FontContext::bindTexture(RenderState& rs, int _id, GLuint _unit) {
    std::lock_guard<std::mutex> lock(m_textureMutex);

    m_textures[_id]->bind(rs, _unit);
}

// Synchronized on m_mutex in layoutText(), called on tile-worker threads
int FontContext::addTexture() {  //uint16_t width, uint16_t height) {
    //static constexpr int MAX_TEXTURE_SIZE = 4096;

    std::lock_guard<std::mutex> lock(m_textureMutex);

    if (m_textures.size() == max_textures) {
        LOGE("Way too many glyph textures!");
        return -1;
    }

    flushTextTexture();

    // 256x256 size for GlyphTexture is hardcoded in several places
    int iw = GlyphTexture::size, ih = GlyphTexture::size;
    /*if (!m_textures.empty()) {
        iw = m_textures.back()->width();
        ih = m_textures.back()->height();
        if (iw > ih)
          ih = std::min(2*ih, MAX_TEXTURE_SIZE);
        else
          iw = std::min(2*iw, MAX_TEXTURE_SIZE);
    }*/

    // we could set atlas glyph size fairly small, then if larger glyph is needed, split into, e.g. 4 quads
    // - or use a different texture for each size
    // - fontContext uses 3 discrete sizes and picks one closest to requested size; fontScale is set to ratio between requested and actual size

    m_textures.push_back(std::make_unique<GlyphTexture>());  //ih, iw));
    fonsGetAtlasSize(m_fons, &iw, &ih, NULL);
    //fonsResetAtlas(m_fons, iw, ih, atlasFontPx, atlasFontPx, atlasFontPx);
    fonsExpandAtlas(m_fons, iw, ih + GlyphTexture::size);
    return m_textures.size() - 1;
}


bool FontContext::layoutLine(TextStyle::Parameters& _params, int x, int y,
    const char* start, const char* end, std::vector<GlyphQuad>& _quads /*out*/)
{
  if(start == end) return false;
  FONSstate state;
  FONStextIter iter, prevIter;
  FONSquad q;
  float expand = 0.5f;  //state->fontBlur > 0 ? 0.5f + state->fontBlur : 0.5f;
  float scale = _params.fontScale;
  const float pos_scale = TextVertex::position_scale;
  float dx = expand/scale, dy = expand/scale;

  int iw, ih;
  fonsInitState(m_fons, &state);
  fonsSetFont(&state, _params.font);
  fonsSetSize(&state, _params.fontSize * 3);  // _params.fontScale ?

  fonsGetAtlasSize(m_fons, &iw, &ih, NULL);
  fonsTextIterInit(&state, &iter, x, y, start, end, FONS_GLYPH_BITMAP_REQUIRED);
  prevIter = iter;
  while (fonsTextIterNext(&state, &iter, &q)) {
    if (iter.prevGlyphIndex == -1) { // can not retrieve glyph?
      if (addTexture() < 0)
        break;
      fonsGetAtlasSize(m_fons, &iw, &ih, NULL);
      iter = prevIter;
      fonsTextIterNext(&state, &iter, &q); // try again
      if (iter.prevGlyphIndex == -1) // still can not find glyph?
        break;
    }
    prevIter = iter;
    // expand by half pixel - note that we must expand texture coords to match expansion of quad
    //float qsqx = (q.s1 - q.s0)/(q.x1 - q.x0);
    //float qtqy = (q.t1 - q.t0)/(q.y1 - q.y0);
    //q.s0 -= qsqx*dx; q.s1 += qsqx*dx; q.t0 -= qtqy*dy; q.t1 += qtqy*dy;
    //q.x0 -= dx; q.x1 += dx; q.y0 -= dy; q.y1 += dy;

    // tangram uses integers for position (coord * pos_scale) and tex coords (pixels)!
    int x0 = int(q.x0 * pos_scale + 0.5f), y0 = int(q.y0 * pos_scale + 0.5f);
    int x1 = int(q.x1 * pos_scale + 0.5f), y1 = int(q.y1 * pos_scale + 0.5f);
    int s0 = int(q.s0 * iw), t0 = int(q.t0 * ih) & 255;
    int s1 = int(q.s1 * iw), t1 = int(q.t1 * ih) & 255;
    size_t texidx = size_t(q.t1*(ih/256));
    _quads.push_back({texidx,
            {{{x0, y0}, {s0, t0}},
             {{x0, y1}, {s0, t1}},
             {{x1, y0}, {s1, t0}},
             {{x1, y1}, {s1, t1}}}});
  }
  return true;
}

int FontContext::layoutMultiline(TextStyle::Parameters& _params, const std::string& _text,
    TextLabelProperty::Align _align, std::vector<GlyphQuad>& _quads /*out*/)
{
  FONSstate state;
  std::vector<FONStextRow> rows(_params.maxLines);
  const char* start = _text.c_str();
  const char* end = start + _text.size();
  fonsInitState(m_fons, &state);
  fonsSetFont(&state, _params.font);
  fonsSetSize(&state, _params.fontSize * 3);  // _params.fontScale ?
  // pass negative integer for line width to use max chars instead of max width
  size_t nrows = fonsBreakLines(&state, start, end, -float(_params.maxLineWidth), rows.data(), rows.size());
  if (!nrows) return 0;
  rows.resize(nrows);

  float maxRowWidth = 0;
  for (auto& row : rows) {
    maxRowWidth = std::max(maxRowWidth, row.width);
  }

  float x = 0, y = 0, lineh = 0;
  // non-stb FontContext uses bbox limits of row to determine height instead (?)
  fonsVertMetrics(&state, NULL, NULL, &lineh);
  for (size_t ii = 0; ii < nrows; ++ii) {  //auto& row : rows) {
    if (_align == TextLabelProperty::Align::right)
      x = maxRowWidth - rows[ii].width;
    else if (_align == TextLabelProperty::Align::center)
      x = maxRowWidth*0.5f - rows[ii].width*0.5f;
    else
      x = 0;

    if (ii == nrows - 1 && rows[ii].end < end) {
      std::string lastRow(rows[ii].start, rows[ii].end);
      lastRow.append("…");
      layoutLine(_params, x, y, lastRow.c_str(), lastRow.c_str() + lastRow.size(), _quads);
      break;
    }

    layoutLine(_params, x, y, rows[ii].start, rows[ii].end, _quads);
    y += lineh + _params.lineSpacing;
  }
  return nrows;
}

bool FontContext::layoutText(TextStyle::Parameters& _params /*in*/, const std::string& _text /*in*/,
                             std::vector<GlyphQuad>& _quads /*out*/, std::bitset<max_textures>& _refs /*out*/,
                             glm::vec2& _size /*out*/, TextRange& _textRanges /*out*/) {

    std::lock_guard<std::mutex> fontlock(m_fontMutex);

    size_t quadsStart = _quads.size();

    if(_params.wordWrap) {

        std::array<bool, 3> alignments = {};
        if (_params.align != TextLabelProperty::Align::none) {
            alignments[int(_params.align)] = true;
        }

        // Collect possible alignment from anchor fallbacks
        for (int i = 0; i < _params.labelOptions.anchors.count; i++) {
            auto anchor = _params.labelOptions.anchors[i];
            TextLabelProperty::Align alignment = TextLabelProperty::alignFromAnchor(anchor);
            if (alignment != TextLabelProperty::Align::none) {
                alignments[int(alignment)] = true;
            }
        }

        // draw for each alternative alignment
        for (size_t i = 0; i < 3; i++) {
            int rangeStart = _quads.size();
            if (!alignments[i]) {
                _textRanges[i] = Range(rangeStart, 0);
                continue;
            }
            int numLines = layoutMultiline(_params, _text, TextLabelProperty::Align(i), _quads);
            int rangeEnd = _quads.size();
            _textRanges[i] = Range(rangeStart, rangeEnd - rangeStart);
            // For single line text alignments are the same
            if (i == 0 && numLines == 1) {
                _textRanges[1] = Range(rangeEnd, 0);
                _textRanges[2] = Range(rangeEnd, 0);
                break;
            }
        }

    } else {
        layoutLine(_params, 0, 0, _text.c_str(), NULL, _quads);
        int rangeEnd = _quads.size();
        _textRanges[0] = Range(quadsStart, rangeEnd - quadsStart);
        _textRanges[1] = Range(rangeEnd, 0);
        _textRanges[2] = Range(rangeEnd, 0);
    }

    if(quadsStart == _quads.size())
        return false;  // no glyphs

    {
        std::lock_guard<std::mutex> texlock(m_textureMutex);

        isect2d::AABB<glm::vec2> aabb;
        for (auto it = _quads.begin() + quadsStart; it != _quads.end(); ++it) {
          aabb.include(it->quad[0].pos.x, it->quad[0].pos.y);
          aabb.include(it->quad[3].pos.x, it->quad[3].pos.y);  // 4th vertex is opposite 1st (!)
        }

        float width = aabb.max.x - aabb.min.x;
        float height = aabb.max.y - aabb.min.y;
        _size = glm::vec2(width/TextVertex::position_scale, height/TextVertex::position_scale);

        // Offset to center all glyphs around 0/0
        glm::vec2 offset(aabb.min.x + width/2, aabb.min.y + height/2);

        for (auto it = _quads.begin() + quadsStart; it != _quads.end(); ++it) {

            if (!_refs[it->atlas]) {
                _refs[it->atlas] = true;
                m_atlasRefCount[it->atlas] += 1;
            }

            it->quad[0].pos -= offset;
            it->quad[1].pos -= offset;
            it->quad[2].pos -= offset;
            it->quad[3].pos -= offset;
        }

        // Clear unused textures
        //for (size_t i = 0; i < m_textures.size(); i++) {
        //    if (m_atlasRefCount[i] == 0) {
        //        m_atlas.clear(i);
        //        std::memset(m_textures[i]->buffer(), 0, GlyphTexture::size * GlyphTexture::size);
        //    }
        //}
    }

    return true;
}

void FontContext::addFont(const FontDescription& _ft, std::vector<char>&& _source) {

    // NB: Synchronize for calls from download thread
    std::lock_guard<std::mutex> lock(m_fontMutex);

    fonsAddFontMem(m_fons, _ft.alias.c_str(), (unsigned char*)_source.data(), _source.size(), 0);
    // add _source to some list so it doesn't get freed
    m_sources.push_back(std::move(_source));
}

int FontContext::loadFontSource(const std::string& _name, const FontSourceHandle& _source)
{
  if(_source.tag == FontSourceHandle::FontPath) {
      return fonsAddFont(m_fons, _name.c_str(), _source.fontPath.path().c_str());
  }
  if(_source.tag == FontSourceHandle::FontLoader) {
      auto fontData = _source.fontLoader();
      if (fontData.size() > 0) {
        int font = fonsAddFontMem(m_fons, _name.c_str(), (unsigned char*)fontData.data(), fontData.size(), 0);
        m_sources.push_back(std::move(fontData));
        return font;
      }
  }
  // switch to using FontSourceHandle::FontLoader for macOS/iOS - see appleFontFace.mm
  //case FontSourceHandle::FontName:
  return -1;
}

int FontContext::getFont(const std::string& _family, const std::string& _style,
                                                   const std::string& _weight, float _size) {
  {
    std::lock_guard<std::mutex> lock(m_fontMutex);

    std::string alias = FontDescription::Alias(_family, _style, _weight);
    int font = fonsGetFontByName(m_fons, alias.c_str());
    if (font >= 0) return font;

    auto systemFontHandle = m_platform.systemFont(_family, _weight, _style);
    font = loadFontSource(alias, systemFontHandle);
    if (font >= 0) return font;
  }
  if(_family != "default")
    return getFont("default", _style, _weight, _size);
  if(_weight != "normal" && _weight != "400")
    return getFont("default", _style, "400", _size);
  return 0;
}

}
