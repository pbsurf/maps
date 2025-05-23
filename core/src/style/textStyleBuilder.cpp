#include "style/textStyleBuilder.h"

#include "labels/curvedLabel.h"
#include "labels/labelCollider.h"
#include "labels/labelSet.h"
#include "labels/textLabel.h"
#include "labels/textLabels.h"
#include "log.h"
#include "map.h"
#include "marker/marker.h"
#include "selection/featureSelection.h"
#include "scene/drawRule.h"
#include "tile/tile.h"
#include "util/color.h"
#include "util/geom.h"
#include "util/mapProjection.h"
#include "util/lineSampler.h"
#include "view/view.h"

#ifdef FONTCONTEXT_STB
#include <codecvt>
#else
#include "unicode/unistr.h"
#include "unicode/schriter.h"
#include "unicode/brkiter.h"
#include "unicode/locid.h"
#endif

#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cmath>
#include <locale>
#include <mutex>

namespace Tangram {

const static std::string key_name("name");

TextStyleBuilder::TextStyleBuilder(const TextStyle& _style) : m_style(_style) {}

void TextStyleBuilder::setup(const Tile& _tile){
    m_tileSize = MapProjection::tileSize() * m_style.pixelScale();

    // < 1.0 when overzooming a tile
    m_tileScale = pow(2, _tile.getID().s - _tile.getID().z);
    m_tileSize *= m_tileScale;

    m_atlasRefs.reset();

    m_textLabels = std::make_unique<TextLabels>(m_style);
}

void TextStyleBuilder::setup(const Marker& marker, int zoom) {
    float metersPerTile = MapProjection::metersPerTileAtZoom(zoom);

    // In general, a Marker won't cover the same area as a tile, so the effective
    // "tile size" for building a Marker is the size of a tile in pixels multiplied
    // by the ratio of the Marker's extent to the length of a tile side at this zoom.
    m_tileSize = 256 * (marker.extent() / metersPerTile);

    // (Copied from Tile setup function above, purpose unclear)
    m_tileSize *= m_style.pixelScale();

    m_atlasRefs.reset();

    m_textLabels = std::make_unique<TextLabels>(m_style);
}

void TextStyleBuilder::addLayoutItems(LabelCollider& _layout) {
    _layout.addLabels(m_labels);
}

std::unique_ptr<StyledMesh> TextStyleBuilder::build() {

    if (m_quads.empty()) { return nullptr; }

    if (Tangram::getDebugFlag(DebugFlags::draw_all_labels)) {
        m_textLabels->setLabels(m_labels);

        std::vector<GlyphQuad> quads(m_quads);
        m_textLabels->setQuads(std::move(quads), m_atlasRefs);

    } else {

        // TODO this could probably done more elegant

        int quadPos = 0;
        size_t sumQuads = 0;
        size_t sumLabels = 0;
        bool added = false;

        // Determine number of labels and size of final quads vector
        for (auto& label : m_labels) {
            auto* textLabel = static_cast<TextLabel*>(label.get());

            const auto& ranges = textLabel->textRanges();

            bool active = textLabel->state() != Label::State::dead;

            if (ranges.back().end() != quadPos) {
                quadPos = ranges.back().end();
                added = false;
            }

            if (!active) { continue; }

            sumLabels +=1;
            if (!added) {
                added = true;

                for (auto& textRange : ranges) {
                    sumQuads += textRange.length;
                }
            }
        }

        size_t quadEnd = 0;
        size_t quadStart = 0;
        quadPos = 0;

        std::vector<std::unique_ptr<Label>> labels;
        labels.reserve(sumLabels);

        std::vector<GlyphQuad> quads;
        quads.reserve(sumQuads);

        // Add only alive labels
        for (auto& label : m_labels) {
            auto* textLabel = static_cast<TextLabel*>(label.get());

            bool active = textLabel->state() != Label::State::dead;
            if (!active) { continue; }

            auto& ranges = textLabel->textRanges();

            // Add the quads of line-labels only once
            if (ranges.back().end() != quadPos) {
                quadStart = quadEnd;
                quadPos = ranges.back().end();

                for (auto& textRange : ranges) {
                    if (textRange.length > 0) {
                        quadEnd += textRange.length;

                        auto it = m_quads.begin() + textRange.start;
                        quads.insert(quads.end(), it, it + textRange.length);
                    }
                }
            }

            // Update TextRange
            auto start = quadStart;

            for (auto& textRange : ranges) {
                textRange.start = start;
                start += textRange.length;
            }

            labels.push_back(std::move(label));
        }

        m_textLabels->setLabels(labels);
        m_textLabels->setQuads(std::move(quads), m_atlasRefs);
    }

    m_labels.clear();
    m_quads.clear();

    return std::move(m_textLabels);
}

bool getTextSource(const StyleParamKey _key, const DrawRule& _rule, const Properties& _props,
                   std::string& _text) {

    auto& textSource = _rule.findParameter(_key);
    if (textSource.value.is<StyleParam::TextSource>()) {
        for (auto& key : textSource.value.get<StyleParam::TextSource>().keys) {
            _text = _props.getAsString(key);
            if (!_text.empty()) { break; }
        }
    } else if (textSource.value.is<std::string>()) {
        // From function evaluation
        _text = textSource.value.get<std::string>();
    } else if (textSource.value.is<Undefined>()) {
        _text = "";
    } else {
        return false;
    }
    return true;
}

bool TextStyleBuilder::handleBoundaryLabel(const Feature& _feat, const DrawRule& _rule,
                                           const TextStyle::Parameters& _params) {

    if (_feat.geometryType != GeometryType::lines) { return false; }

    LabelAttributes leftAttribs, rightAttribs;
    std::string textLeft = std::move(_params.textLeft);
    std::string textRight = std::move(_params.textRight);

    TextStyle::Parameters rightParams = _params;
    TextStyle::Parameters leftParams = _params;

    // Deactivate offset horizontally for boundary labels
    rightParams.labelOptions.offset.x = 0.0f;
    leftParams.labelOptions.offset.x = 0.0f;

    bool hasLeftLabel = false;
    if (!textLeft.empty()) {
        leftParams.text = std::move(textLeft);
        leftParams.labelOptions.optional = true;
        leftParams.labelOptions.anchors = {LabelProperty::Anchor::top};
        leftParams.labelOptions.buffer = glm::vec2(0);
        hash_combine(leftParams.labelOptions.repeatGroup, leftParams.text);

        hasLeftLabel = prepareLabel(leftParams, Label::Type::line, leftAttribs);
    }

    bool hasRightLabel = false;
    if (!textRight.empty()) {
        rightParams.text = std::move(textRight);
        rightParams.labelOptions.optional = true;
        rightParams.labelOptions.anchors = {LabelProperty::Anchor::bottom};
        rightParams.labelOptions.buffer = glm::vec2(0);
        hash_combine(rightParams.labelOptions.repeatGroup, rightParams.text);

        hasRightLabel = prepareLabel(rightParams, Label::Type::line, rightAttribs);
    }

    if (!hasLeftLabel && !hasRightLabel) {
        return false;
    }

    float labelWidth = std::max(leftAttribs.width, rightAttribs.width);

    auto onAddLabel = [&](glm::vec2 a, glm::vec2 b) {
        Label* left = nullptr;
        Label* right = nullptr;
        if (hasLeftLabel) {
            left = addLabel(Label::Type::line, {{ a, b }}, leftParams, leftAttribs, _rule);
        }
        if (hasRightLabel) {
            right = addLabel(Label::Type::line, {{ a, b }}, rightParams, rightAttribs, _rule);
        }
        if (left && right) {
            left->setRelative(*right, false, false);
            right->setRelative(*left, false, false);
        }
    };

    bool added = false;
    for (auto& line : _feat.lines) {
        added |= addStraightTextLabels(line, labelWidth, onAddLabel);
    }

    return added;
}

bool TextStyleBuilder::addFeature(const Feature& _feat, const DrawRule& _rule) {

    if (!checkRule(_rule)) { return false; }

    TextStyle::Parameters params = applyRule(_rule, _feat.props, false);
    if (!params.font) { return false; }

    Label::Type labelType;
    if (_feat.geometryType == GeometryType::lines) {
        labelType = Label::Type::line;
        params.wordWrap = false;
    } else {
        labelType = Label::Type::point;
    }
    // Keep start position of new quads
    size_t quadsStart = m_quads.size();
    size_t numLabels = m_labels.size();

    if (!params.textLeft.empty() || !params.textRight.empty()) {
        if (!handleBoundaryLabel(_feat, _rule, params)) { return false; }

    } else {
        LabelAttributes attrib;
        if (!prepareLabel(params, labelType, attrib)) { return false; }

        if (_feat.geometryType == GeometryType::points) {
            for (auto& point : _feat.points) {
                auto p = glm::vec2(point);
                addLabel(Label::Type::point, {{ p }}, params, attrib, _rule);
            }

        } else if (_feat.geometryType == GeometryType::polygons) {
            const auto& polygons = _feat.polygons;
            for (const auto& polygon : polygons) {
                if (!polygon.empty()) {
                    glm::vec2 c;
                    c = centroid(polygon.front().begin(), polygon.front().end());
                    addLabel(Label::Type::point, {{ c }}, params, attrib, _rule);
                }
            }

        } else if (_feat.geometryType == GeometryType::lines) {
            addLineTextLabels(_feat, params, attrib, _rule);
        }
    }

    if (numLabels == m_labels.size()) {
        // Drop quads when no label was added
        m_quads.resize(quadsStart);
    }
    return true;
}

#define TANGRAM_NEW_STRAIGHT_LABELS

bool TextStyleBuilder::addStraightTextLabels(const Line& _line, float _labelWidth,
                                             const std::function<void(glm::vec2,glm::vec2)>& _onAddLabel) {

    // Size of pixel in tile coordinates
    float pixelSize = 1.0f/m_tileSize;

    // Minimal length of line needed for the label
    float minLength = _labelWidth * pixelSize;

    // Allow labels to appear later than tile's min-zoom
    minLength *= 0.6f;

    //float tolerance = pow(pixelScale * 2, 2);
    float tolerance = powf(pixelSize * 1.5f, 2);
    float sqDirLimit = powf(1.99f, 2);

    for (size_t i = 0; i < _line.size() - 1; i++) {
        glm::vec2 p0 = glm::vec2(_line[i]);
        glm::vec2 p1 = glm::vec2(_line[i+1]);

        float segmentLength = glm::length(p0 - p1);

        glm::vec2 dir0 = (p0 - p1) / segmentLength;
        glm::vec2 dir1 = dir0;
        glm::vec2 dir2;

        int merged = 0;

        size_t j = i + 2;
        for (; j < _line.size(); j++) {
            glm::vec2 p2 = glm::vec2(_line[j]);

            segmentLength = glm::length(p1 - p2);
            dir2 = (p1 - p2) / segmentLength;

            glm::vec2 pp = glm::vec2(_line[j-1]);
            float d = pointSegmentDistanceSq(pp, p0, p2);
            if (d > tolerance) { break; }

            if ((glm::length2(dir1 + dir2) < sqDirLimit) ||
                (glm::length2(dir0 + dir2) < sqDirLimit)) {
                break;
            }

            merged++;

            p1 = p2;
            dir1 = dir2;
        }

        // place labels at segment-subdivisions
        int run = 1;
        if (merged) { segmentLength = glm::length(p0 - p1); }

        while (segmentLength > minLength && run <= 4) {
            glm::vec2 a = p0;
            glm::vec2 b = glm::vec2(p1 - p0) / float(run);

            for (int r = 0; r < run; r++) {
#ifdef TANGRAM_NEW_STRAIGHT_LABELS
                // no labels outside tile
                const float tol = 2*minLength;
                glm::vec2 z = a+b;
                if((a.x < -tol || a.x > 1+tol || a.y < -tol || a.y > 1+tol) &&
                   (z.x < -tol || z.x > 1+tol || z.y < -tol || z.y > 1+tol)) { continue; }
#endif
                _onAddLabel(a, a+b);

                a += b;
            }
            run *= 2;
            segmentLength /= 2.0f;
        }

        if (i == 0 && j == _line.size()) {
            // Simple straight line
            return true;
        }

        // Skip merged segments in outer loop
        i += merged;

    }
    return false;
}

//#define TANGRAM_NEW_CURVED_LABELS

void TextStyleBuilder::addCurvedTextLabels(const Line& _line, const TextStyle::Parameters& _params,
                                           const LabelAttributes& _attributes, const DrawRule& _rule) {

    // Size of pixel in tile coordinates
    const float pixelSize = 1.0f/m_tileSize;
    // length of line needed for the label
    const float labelLength = _attributes.width * pixelSize;
    // Allow labels to appear later than tile's min-zoom
    const float minLength = labelLength * 0.6f;

    // Chord length for minimal ~120 degree inner angles (squared)
    // sin(60)*2
    const float sqDirLimit = powf(1.7f, 2);
    // Range to check for angle changes
    const float sampleWindow = pixelSize * 50;

    // Minimal ~10 degree counts as change of direction
    // cross(dir1,dir2) < sin(10)
    const float flipTolerance = 0.17f;

    LineSampler<std::vector<glm::vec3>> sampler;

    sampler.set(_line);

    if (sampler.sumLength() < minLength) { return; }

    struct LineRange {
        size_t start, end;
        int flips;
        float sumAngle;
    };

    std::vector<LineRange> ranges;
    size_t npoints = sampler.numPoints();  // this can be less than _line.size() if _line has duplicate points

    for (size_t i = 0; i < npoints-1; i++) {
#ifdef TANGRAM_NEW_CURVED_LABELS
        // only process labels starting in this tile
        const auto& p0 = sampler.point(i);
        const float tol = 0.0005f;
        if(p0.x < -tol || p0.x > 1+tol || p0.y < -tol || p0.y > 1+tol) continue;  // !_params.keepTileEdges
#endif
        int flips = 0;
        float lastAngle = 0;
        float sumAngle = 0;
        size_t lastBreak = 0;

        glm::vec2 dir1 = sampler.segmentDirection(i);

        for (size_t j = i + 1; j < npoints-1; j++) {
            glm::vec2 dir2 = sampler.segmentDirection(j);
            bool splitLine = false;

            if (glm::length2(dir1 + dir2) < sqDirLimit) {
                // Split if the angle between current and next segment is
                // not whithin 120 < a < 240 degree
                splitLine = true;
            } else {
                // Take cross product of direction (unit-) vectors of the current
                // and next segment. The magnitude of the cross product is the sine
                // angle between dir1 and dir2.
                float angle = perpDotProduct(dir1, dir2);

                if (std::abs(angle) > flipTolerance) {
                    if (lastAngle > 0) {
                        if (angle < 0) { flips++; }
                    } else if (lastAngle < 0) {
                        if (angle > 0) { flips++; }
                    }
                    lastAngle = angle;
                }

                // Limit number of direction changes (Avoid squiggly labels)
                if (flips > 2) {
                    splitLine = true;
                } else {
                    sumAngle += std::abs(angle);
                }
            }

            if (!splitLine) {
                // Go back within window to check for hard direction changes
                for (int k = j - 1; k >= int(i); k--){
                    if (glm::length2(sampler.segmentDirection(k) + dir2) < sqDirLimit) {
                        splitLine = true;
                    }
                    if (sampler.point(k).z < sampler.point(j).z - sampleWindow) {
                        break;
                    }
                }
            }

#ifdef TANGRAM_NEW_CURVED_LABELS
            float length = sampler.point(j).z - sampler.point(i).z;
            if (length > labelLength || (length > minLength && splitLine)) {
                // label candidate spacing to avoid excessive number of candidates; not related to repeat_distance!
                const float labelPeriod = 30 * pixelSize;
                int seg = int(sampler.point(i).z/labelPeriod);
                int segprev = ranges.empty() ? seg+1 : int(sampler.point(ranges.back().start).z/labelPeriod);
                if (seg == segprev) {
                    if (ranges.back().sumAngle <= sumAngle)
                      break;  // discard current placement
                    ranges.pop_back();  // discard last placement
                }
                ranges.push_back(LineRange{i, j+1, flips, sumAngle});
                break;
            } else if (splitLine) {
                break;
            } else {
                dir1 = dir2;
            }
#else
            if (splitLine) {
                float length = sampler.point(j).z - sampler.point(i).z;
                if (length > minLength) {
                    ranges.push_back(LineRange{i, j+1, flips, sumAngle});
                }

                lastBreak = j;
                break;

            } else {
                dir1 = dir2;
            }
#endif
        }
#ifndef TANGRAM_NEW_CURVED_LABELS
        // Add segment from 'i' unless line got split.
        if (lastBreak == 0) {
            float length = sampler.sumLength() - sampler.point(i).z;
            if (length > minLength) {
                ranges.push_back(LineRange{i, npoints, flips, sumAngle});
            }
        }
#endif
    }

    for (auto& range : ranges) {
        glm::vec2 center;
        glm::vec2 rotation;
        float startLen = sampler.point(range.start).z;
        float length = (sampler.point(range.end-1).z - startLen);
        float mid = startLen + length * 0.5f;

        sampler.sample(mid, center, rotation);
        size_t offset = sampler.curSegment();

        std::vector<glm::vec3> l;
        l.reserve(range.end - range.start + 1);

        for (size_t j = range.start; j < range.end; j++) {
            auto p = sampler.point(j);
            l.emplace_back(p.x, p.y, 0.f);

            if (j == offset) {
                l.push_back(glm::vec3(center, 0.f));
            }
        }
        size_t anchor = offset - range.start + 1;

        // NB: Just some heuristic to prefer longer and less curvy parts..
        float prio = (1.f + range.sumAngle) / length;

        uint32_t selectionColor = 0;
        if (_params.interactive) {
            selectionColor = _rule.featureSelection->nextColorIdentifier();
        }

        m_labels.emplace_back(new CurvedLabel(l, _params.labelOptions, prio,
                                               {_attributes.fill,
                                                       _attributes.stroke,
                                                       _attributes.fontScale,
                                                       selectionColor},
                                               {_attributes.width, _attributes.height},
                                               *m_textLabels, _attributes.textRanges,
                                               TextLabelProperty::Align::center,
                                               anchor));
#ifdef DEBUG
        m_labels.back()->debugTag = _params.text;
#endif
    }
}

void TextStyleBuilder::addLineTextLabels(const Feature& _feat, const TextStyle::Parameters& _params,
                                         const LabelAttributes& _attributes, const DrawRule& _rule) {

    auto straightLabelCb = [&](glm::vec2 a, glm::vec2 b) {
        addLabel(Label::Type::line, {{ a, b }}, _params, _attributes, _rule);
    };

    for (auto& line : _feat.lines) {

        if (!addStraightTextLabels(line, _attributes.width, straightLabelCb) &&
            line.size() > 2 && !_params.hasComplexShaping &&
            // TODO: support line offset for curved labels
            _params.labelOptions.offset == glm::vec2(0)) {
            addCurvedTextLabels(line, _params, _attributes, _rule);
        }
    }
}

bool TextStyleBuilder::checkRule(const DrawRule& _rule) const {
    if (_rule.hasParameterSet(StyleParamKey::text_font_family) ||
        _rule.hasParameterSet(StyleParamKey::text_font_fill) ||
        _rule.hasParameterSet(StyleParamKey::text_font_size) ||
        _rule.hasParameterSet(StyleParamKey::text_font_stroke_color) ||
        _rule.hasParameterSet(StyleParamKey::text_font_stroke_width) ||
        _rule.hasParameterSet(StyleParamKey::text_font_style) ||
        _rule.hasParameterSet(StyleParamKey::text_font_weight)) {
        return true;
    }
    return false;
}

TextStyle::Parameters TextStyleBuilder::applyRule(const DrawRule& _rule,
                                                  const Properties& _props,
                                                  bool _iconText) const {

    const static std::string defaultWeight("400");
    const static std::string defaultStyle("regular");
    const static std::string defaultFamily("default");

    TextStyle::Parameters p;

    bool hasLeft = getTextSource(StyleParamKey::text_source_left, _rule, _props, p.textLeft);
    bool hasRight = getTextSource(StyleParamKey::text_source_right, _rule, _props, p.textRight);

    if (!hasLeft && !hasRight) {
        if (!getTextSource(StyleParamKey::text_source, _rule, _props, p.text)) {
            // Use default key
            p.text = _props.getString(key_name);
        }
    }

    if (p.text.empty() && p.textLeft.empty() && p.textRight.empty()) {
        return p;
    }

    auto fontFamily = _rule.get<std::string>(StyleParamKey::text_font_family);
    fontFamily = (!fontFamily) ? &defaultFamily : fontFamily;

    auto fontWeight = _rule.get<std::string>(StyleParamKey::text_font_weight);
    fontWeight = (!fontWeight) ? &defaultWeight : fontWeight;

    auto fontStyle = _rule.get<std::string>(StyleParamKey::text_font_style);
    fontStyle = (!fontStyle) ? &defaultStyle : fontStyle;

    _rule.get(StyleParamKey::text_font_size, p.fontSize);
    p.fontSize *= m_style.pixelScale();

    p.font = m_style.context()->getFont(*fontFamily, *fontStyle, *fontWeight, p.fontSize);
    if (!p.font) {
        LOGW("Missing font for %s / %s / %s / %d", fontFamily->c_str(), fontStyle->c_str(), fontWeight->c_str(), p.fontSize);
        return p;
    }
    _rule.get(StyleParamKey::text_font_fill, p.fill);
    float alpha = 1;
    if (_rule.get(StyleParamKey::text_font_alpha, alpha)) {
        p.fill = Color(p.fill).withAlpha(alpha).abgr;
    }

    _rule.get(StyleParamKey::text_font_stroke_color, p.strokeColor);
    float strokeAlpha = 1;
    if (_rule.get(StyleParamKey::text_font_stroke_alpha, strokeAlpha)) {
        p.strokeColor = Color(p.strokeColor).withAlpha(strokeAlpha).abgr;
    }

    _rule.get(StyleParamKey::text_font_stroke_width, p.strokeWidth);
    p.strokeWidth *= m_style.pixelScale();

    _rule.get(StyleParamKey::transition_hide_time, p.labelOptions.hideTransition.time);
    _rule.get(StyleParamKey::transition_selected_time, p.labelOptions.selectTransition.time);
    _rule.get(StyleParamKey::transition_show_time, p.labelOptions.showTransition.time);

    _rule.get(StyleParamKey::text_max_lines, p.maxLines);

    size_t repeatGroupHash = 0;
    std::string repeatGroup;
    StyleParam::Width repeatDistance;
    glm::vec2 defaultBuffer = glm::vec2(p.fontSize * 0.25f);

    if (_iconText) {
        _rule.get(StyleParamKey::text_priority, p.labelOptions.priority);
        _rule.get(StyleParamKey::text_collide, p.labelOptions.collide);
        if (!_rule.get(StyleParamKey::text_interactive, p.interactive)) {
            _rule.get(StyleParamKey::interactive, p.interactive);
        }
        _rule.get(StyleParamKey::text_offset, p.labelOptions.offset);
        p.labelOptions.offset *= m_style.pixelScale();

        _rule.get(StyleParamKey::text_anchor, p.labelOptions.anchors);
        if (p.labelOptions.anchors.count == 0) {
            p.labelOptions.anchors.anchor = { {LabelProperty::Anchor::bottom, LabelProperty::Anchor::top,
                                               LabelProperty::Anchor::right, LabelProperty::Anchor::left} };
            p.labelOptions.anchors.count = 4;
        }

        // child text's repeat group params
        if (_rule.get(StyleParamKey::text_repeat_distance, repeatDistance)) {
            p.labelOptions.repeatDistance = repeatDistance.value;
        } else {
            p.labelOptions.repeatDistance = MapProjection::tileSize();
        }

        if (p.labelOptions.repeatDistance > 0.f) {
            if (_rule.get(StyleParamKey::text_repeat_group, repeatGroup)) {
                hash_combine(repeatGroupHash, repeatGroup);
            } else if (_rule.get(StyleParamKey::repeat_group, repeatGroup)) { //inherit from parent point
                hash_combine(repeatGroupHash, repeatGroup);
            } else {
                repeatGroupHash = _rule.getParamSetHash();
            }
        }

        _rule.get(StyleParamKey::text_transition_hide_time, p.labelOptions.hideTransition.time);
        _rule.get(StyleParamKey::text_transition_selected_time, p.labelOptions.selectTransition.time);
        _rule.get(StyleParamKey::text_transition_show_time, p.labelOptions.showTransition.time);

        if (!_rule.get(StyleParamKey::text_buffer, p.labelOptions.buffer)) {
            p.labelOptions.buffer = defaultBuffer;
        }
    } else {
        _rule.get(StyleParamKey::priority, p.labelOptions.priority);
        _rule.get(StyleParamKey::collide, p.labelOptions.collide);
        _rule.get(StyleParamKey::interactive, p.interactive);
        _rule.get(StyleParamKey::offset, p.labelOptions.offset);
        p.labelOptions.offset *= m_style.pixelScale();

        _rule.get(StyleParamKey::anchor, p.labelOptions.anchors);
        if (p.labelOptions.anchors.count == 0) {
            p.labelOptions.anchors = {LabelProperty::Anchor::center};
        }

        if (_rule.get(StyleParamKey::repeat_distance, repeatDistance)) {
            p.labelOptions.repeatDistance = repeatDistance.value;
        } else {
            p.labelOptions.repeatDistance = MapProjection::tileSize();
        }

        if (p.labelOptions.repeatDistance > 0.f) {
            if (_rule.get(StyleParamKey::repeat_group, repeatGroup)) {
                hash_combine(repeatGroupHash, repeatGroup);
            } else {
                repeatGroupHash = _rule.getParamSetHash();
            }
        }

        if (!_rule.get(StyleParamKey::buffer, p.labelOptions.buffer)) {
            p.labelOptions.buffer = defaultBuffer;
        }
    }

    if (p.labelOptions.repeatDistance > 0.f) {
        hash_combine(repeatGroupHash, p.text);
        p.labelOptions.repeatGroup = repeatGroupHash;
        p.labelOptions.repeatDistance *= m_style.pixelScale();
    }

    _rule.get(StyleParamKey::text_wrap, p.maxLineWidth);

    if (auto* transform = _rule.get<std::string>(StyleParamKey::text_transform)) {
        TextLabelProperty::transform(*transform, p.transform);
    }

    if (auto* align = _rule.get<std::string>(StyleParamKey::text_align)) {
        bool res = TextLabelProperty::align(*align, p.align);
        if (!res && p.labelOptions.anchors.count > 0) {
            p.align = TextLabelProperty::alignFromAnchor(p.labelOptions.anchors[0]);
        }
    }

    _rule.get(StyleParamKey::text_optional, p.labelOptions.optional);

    std::hash<TextStyle::Parameters> hash;
    p.labelOptions.paramHash = hash(p);

    p.lineSpacing = 2 * m_style.pixelScale();

    if (p.interactive) {
        p.labelOptions.featureId = _rule.selectionColor;
    }

    return p;
}

#ifdef FONTCONTEXT_STB

static std::wstring to_wstring(std::string const& s) {
  std::wstring_convert<std::codecvt_utf8<wchar_t> > conv;
  return conv.from_bytes(s);
}

static std::string to_string(std::wstring const& s) {
  std::wstring_convert<std::codecvt_utf8<wchar_t> > conv;
  return conv.to_bytes(s);
}

void applyTextTransform(const TextStyle::Parameters& _params, std::string& _string) {
  static std::locale userlocale("");

  if(_params.transform == TextLabelProperty::Transform::none)
    return;
  auto _wstring = to_wstring(_string);
  switch (_params.transform) {
  case TextLabelProperty::Transform::capitalize: {
      bool was_space = true;
      for (auto& c : _wstring) {
          if(was_space)
              c = std::toupper(c, userlocale);
          was_space = std::isspace(c, userlocale);
      }
      break;
  }
  case TextLabelProperty::Transform::lowercase:
      for (auto& c : _wstring)
        c = std::tolower(c, userlocale);
      break;
  case TextLabelProperty::Transform::uppercase:
      for (auto& c : _wstring)
        c = std::toupper(c, userlocale);
      break;
  default:
      break;
  }
  _string = to_string(_wstring);
}

#else

void applyTextTransform(const TextStyle::Parameters& _params,
                        icu::UnicodeString& _string) {

    icu::Locale loc("en");

    switch (_params.transform) {
    case TextLabelProperty::Transform::capitalize: {
        UErrorCode status{U_ZERO_ERROR};
        auto *wordIterator = icu::BreakIterator::createWordInstance(loc, status);

        if (U_SUCCESS(status)) { _string.toTitle(wordIterator); }

        delete wordIterator;
        break;
    }
    case TextLabelProperty::Transform::lowercase:
        _string.toLower(loc);
        break;
    case TextLabelProperty::Transform::uppercase:
        _string.toUpper(loc);
        break;
    default:
        break;
    }
}

bool isComplexShapingScript(const icu::UnicodeString& _text) {

    // Taken from:
    // https://github.com/tangrams/tangram/blob/labels-rebase/src/styles/text/canvas_text.js#L538-L553
    // See also http://r12a.github.io/scripts/featurelist/

    icu::StringCharacterIterator iterator(_text);
    for (UChar c = iterator.first(); c != icu::CharacterIterator::DONE; c = iterator.next()) {
        if (c >= u'\u0600' && c <= u'\u18AF') {
            if ((c <= u'\u06FF') ||                   // Arabic:     "\u0600-\u06FF"
                (c >= u'\u1800' && c <= u'\u18AF')) { // Mongolian:  "\u1800-\u18AF"
                return true;
            }
        }
    }
    return false;
}
#endif

bool TextStyleBuilder::prepareLabel(TextStyle::Parameters& _params, Label::Type _type,
                                    LabelAttributes& _attributes) {

    if (_params.text.empty() || _params.fontSize <= 0.f) {
        // Nothing to render!
        return false;
    }

#ifdef FONTCONTEXT_STB
    auto& text = _params.text;
    applyTextTransform(_params, text);
    _params.fontScale = 1.f;
#else
    auto text = icu::UnicodeString::fromUTF8(_params.text);

    applyTextTransform(_params, text);

    if (_type == Label::Type::line) {
        _params.hasComplexShaping = isComplexShapingScript(text);
    }

    // Scale factor by which the texture glyphs are scaled to match fontSize
    _params.fontScale = _params.fontSize / _params.font->size();
#endif

    // Stroke width is normalized by the distance of the SDF spread, then
    // scaled to 255 and packed into the "alpha" channel of stroke.
    // Maximal strokeWidth is 3px, attribute is normalized to 0-1 range.

    auto ctx = m_style.context();

    uint32_t strokeAttrib = std::max(_params.strokeWidth / ctx->maxStrokeWidth() * 255.f, 0.f);
    if (strokeAttrib > 255) {
        LOGN("stroke_width too large: %f / %f", _params.strokeWidth, strokeAttrib/255.f);
        strokeAttrib = 255;
    }
    _attributes.stroke = (_params.strokeColor & 0x00ffffff) + (strokeAttrib << 24);
    _attributes.fill = _params.fill;
    _attributes.fontScale = std::min(int(_params.fontScale * 64.f), 255);
    _attributes.quadsStart = m_quads.size();
    _attributes.textRanges = TextRange{};

    glm::vec2 bbox(0);
    if (ctx->layoutText(_params, text, m_quads, m_atlasRefs, bbox, _attributes.textRanges)) {

        int start = _attributes.quadsStart;
        for (auto& range : _attributes.textRanges) {
            assert(range.start == start);
            assert(range.length >= 0);
            start += range.length;
        }
        _attributes.width = bbox.x;
        _attributes.height = bbox.y;
        return true;
    }

    return false;
}

Label* TextStyleBuilder::addLabel(Label::Type _type, TextLabel::Coordinates _coordinates,
                                const TextStyle::Parameters& _params, const LabelAttributes& _attributes,
                                const DrawRule& _rule) {

    uint32_t selectionColor = 0;

    if (_params.interactive) {
        //selectionColor = _rule.featureSelection->nextColorIdentifier();
        selectionColor = _rule.featureSelection ?  // copied from PointStyleBuilder::addLabel()
              _rule.featureSelection->nextColorIdentifier() : _rule.selectionColor;
    }

    m_labels.emplace_back(new TextLabel(_coordinates, _type, _params.labelOptions,
                                        {_attributes.fill,
                                         _attributes.stroke,
                                         _attributes.fontScale,
                                         selectionColor},
                                        {_attributes.width, _attributes.height},
                                        *m_textLabels, _attributes.textRanges,
                                        _params.align));
#ifdef DEBUG
    m_labels.back()->debugTag = _params.text;
#endif

    return m_labels.back().get();
}

}
