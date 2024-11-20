#include "util/geom.h"

#include "glm/gtx/norm.hpp"
#include <cmath>
#include <algorithm>

namespace Tangram {

float mapRange01(float value, float inputMin, float inputMax) {
    if (inputMin == inputMax) {
        return value > inputMin ? 1 : 0;
    }
    return clamp01((value - inputMin) / (inputMax - inputMin));
}

glm::vec4 worldToScreenSpace(const glm::mat4& mvp, const glm::vec4& worldPosition, const glm::vec2& screenSize, bool& behindCamera) {
    glm::vec4 clip = worldToClipSpace(mvp, worldPosition);
    glm::vec3 ndc = clipSpaceToNdc(clip);
    glm::vec2 screenPosition = ndcToScreenSpace(ndc, screenSize);
    behindCamera = clipSpaceIsBehindCamera(clip);
    return glm::vec4(screenPosition, ndc.z, 1/clip.w);  // matches gl_FragCoord
}

float pointSegmentDistanceSq(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    // http://stackoverflow.com/questions/849211/shortest-distance-between-a-point-and-a-line-segment
    glm::vec2 segment(b - a);
    float lengthSq = glm::length2(segment);
    if (lengthSq != 0) {
        float t = glm::dot(p - a, segment) / lengthSq;
        if (t > 1) {
            return glm::length2(p - b);
        } else if (t > 0) {
            return glm::length2(p - (a + segment * t));
        }
    }
    return glm::length2(p - a);
}

float pointSegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    return std::sqrt(pointSegmentDistanceSq(p, a, b));
}

bool clipLine(glm::vec2& a, glm::vec2& b, glm::vec2 min, glm::vec2 max) {
    glm::vec2 dr = b - a;
    float t0 = 0, t1 = 1;
    //if (isZero(dx) && isZero(dy) && pointInside(a)) return;

    auto clipT = [&](float p, float q){
        if (q == 0) return p <= 0;
        if (q > 0) { t0 = std::max(t0, p/q); }
        else { t1 = std::min(t1, p/q); }
        return t0 < t1;
    };

    if (clipT(min.x - a.x, dr.x) && clipT(a.x - max.x, -dr.x) &&
        clipT(min.y - a.y, dr.y) && clipT(a.y - max.y, -dr.y)) {

        if (t1 < 1) { b = a + t1*dr; }
        if (t0 > 0) { a = a + t0*dr; }
        return true;
    }
    return false;
}

}
