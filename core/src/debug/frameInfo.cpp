#include "debug/frameInfo.h"

#include "debug/textDisplay.h"
#include "gl.h"
#include "gl/glError.h"
#include "gl/primitives.h"
#include "map.h"
#include "scene/scene.h"
#include "tile/tileCache.h"

#include <deque>
#include <ctime>

#define TIME_TO_MS(start, end) (float(end - start) / CLOCKS_PER_SEC * 1000.0f)

#define DEBUG_STATS_MAX_SIZE 128

namespace Tangram {

static float s_lastUpdateTime = 0.0;

static clock_t s_startFrameTime = 0,
    s_endFrameTime = 0,
    s_startUpdateTime = 0,
    s_endUpdateTime = 0;

void FrameInfo::beginUpdate() {

    if (getDebugFlag(DebugFlags::tangram_infos) || getDebugFlag(DebugFlags::tangram_stats)) {
        s_startUpdateTime = clock();
    }

}

void FrameInfo::endUpdate() {

    if (getDebugFlag(DebugFlags::tangram_infos) || getDebugFlag(DebugFlags::tangram_stats)) {
        s_endUpdateTime = clock();
        s_lastUpdateTime = TIME_TO_MS(s_startUpdateTime, s_endUpdateTime);
    }

}

void FrameInfo::beginFrame() {

    if (getDebugFlag(DebugFlags::tangram_infos) || getDebugFlag(DebugFlags::tangram_stats)) {
        s_startFrameTime = clock();
    }

}

struct ProfInfo {
  float avgCpu = 0;
  float avgReal = 0;
  clock_t startCpu = 0;
  std::chrono::steady_clock::time_point startReal = std::chrono::steady_clock::now();
};

static std::map<std::string, ProfInfo> profInfos;

void FrameInfo::begin(const std::string& tag)
{
  if (!getDebugFlag(DebugFlags::tangram_infos)) return;
  auto& entry = profInfos[tag];
  entry.startReal = std::chrono::steady_clock::now();
  entry.startCpu = clock();
}

void FrameInfo::end(const std::string& tag)
{
  static constexpr float alpha = 0.1f;

  if (!getDebugFlag(DebugFlags::tangram_infos)) return;
  auto& entry = profInfos[tag];
  auto endReal = std::chrono::steady_clock::now();
  float dtReal = std::chrono::duration<float>(endReal - entry.startReal).count()*1000.0f;
  entry.startReal = endReal;
  clock_t endCpu = clock();
  float dtCpu = TIME_TO_MS(entry.startCpu, endCpu);
  entry.startCpu = endCpu;

  if(dtReal > 2000) { return; }
  entry.avgReal = entry.avgReal*(1 - alpha) + dtReal*alpha;
  entry.avgCpu = entry.avgCpu*(1 - alpha) + dtCpu*alpha;
}

void FrameInfo::draw(RenderState& rs, const View& _view, const Scene& _scene) {

    if (!getDebugFlag(DebugFlags::tangram_infos) && !getDebugFlag(DebugFlags::tangram_stats)) { return; }

    static std::deque<float> updatetime;
    static std::deque<float> rendertime;
    // Only compute average frame time every 60 frames
    float avgTimeRender = 0.f;
    float avgTimeCpu = 0.f;
    float avgTimeUpdate = 0.f;

    if (profInfos.empty() || getDebugFlag(DebugFlags::tangram_stats)) {
        static int cpt = 0;

        clock_t endCpu = clock();
        static float timeCpu[60] = { 0 };
        static float timeUpdate[60] = { 0 };
        static float timeRender[60] = { 0 };
        timeCpu[cpt] = TIME_TO_MS(s_startFrameTime, endCpu);

        if (updatetime.size() >= DEBUG_STATS_MAX_SIZE) {
            updatetime.pop_front();
        }
        if (rendertime.size() >= DEBUG_STATS_MAX_SIZE) {
            rendertime.pop_front();
        }

        rendertime.push_back(timeRender[cpt]);
        updatetime.push_back(timeUpdate[cpt]);

        // Force opengl to finish commands (for accurate frame time)
        GL::finish();

        s_endFrameTime = clock();
        timeRender[cpt] = TIME_TO_MS(s_startFrameTime, s_endFrameTime);

        if (++cpt == 60) { cpt = 0; }

        timeUpdate[cpt] = s_lastUpdateTime;

        for (int i = 0; i < 60; i++) {
            avgTimeRender += timeRender[i];
            avgTimeCpu += timeCpu[i];
            avgTimeUpdate += timeUpdate[i];
        }
        avgTimeRender /= 60;
        avgTimeCpu /= 60;
        avgTimeUpdate /= 60;
    }

    TileManager& _tileManager = *_scene.tileManager();
    size_t memused = 0;
    size_t features = 0;
    for (const auto& tile : _tileManager.getVisibleTiles()) {
        memused += tile->getMemoryUsage();
        features += tile->getSelectionFeatures().size();
    }

    if (getDebugFlag(DebugFlags::tangram_infos)) {
        std::vector<std::string> debuginfos;

        auto& tiles = _tileManager.getVisibleTiles();
        std::map<int, int> sourceCounts;
        for (auto& tile : tiles) { ++sourceCounts[tile->sourceID()]; }

        std::string countsStr;
        for (auto count : sourceCounts) {
            countsStr += " " + _tileManager.getClientTileSource(count.first)->name() + ":" + std::to_string(count.second);
        }

        debuginfos.push_back("zoom:" + std::to_string(_view.getZoom())
                             + "; pxscale: " + std::to_string(_view.pixelScale()));
        debuginfos.push_back("tiles:" + std::to_string(tiles.size()) + ";" + countsStr);
        debuginfos.push_back("selectable features:"
                             + std::to_string(features));
        debuginfos.push_back("tile cache:" + std::to_string(_tileManager.getTileCache()->getNumEntries()) + " ("
                             + std::to_string(_tileManager.getTileCache()->getMemoryUsage() / 1024) + "KB)");
        debuginfos.push_back("tile size:" + std::to_string(memused / 1024) + "kb");

        if(!profInfos.empty()) {
            end("_Frame");
            for(auto& entry : profInfos) {
                debuginfos.push_back(entry.first + ": " + to_string_with_precision(entry.second.avgReal, 3)
                    + "ms (CPU: " + to_string_with_precision(entry.second.avgCpu, 3) + "ms)");
            }
        } else {
            debuginfos.push_back("avg frame cpu time:" + to_string_with_precision(avgTimeCpu, 2) + "ms");
            debuginfos.push_back("avg frame render time:" + to_string_with_precision(avgTimeRender, 2) + "ms");
            debuginfos.push_back("avg frame update time:" + to_string_with_precision(avgTimeUpdate, 2) + "ms");
            debuginfos.push_back("pos:" + std::to_string(_view.getPosition().x) + "/"
                                 + std::to_string(_view.getPosition().y));
            auto center = _view.getCenterCoordinates();
            debuginfos.push_back("LngLat:" + std::to_string(center.longitude) + ", " + std::to_string(center.latitude));
            debuginfos.push_back("tilt:" + std::to_string(_view.getPitch() * 57.3) + "deg");
        }

        TextDisplay::Instance().draw(rs, debuginfos);
    }

    if (getDebugFlag(DebugFlags::tangram_stats)) {
        const int scale = 5 * _view.pixelScale();

        for (size_t i = 0; i < updatetime.size(); i++) {
            float tupdate = updatetime[i] * scale;
            float trender = rendertime[i] * scale;
            float offsetx = i * 4 * _view.pixelScale();

            Primitives::setColor(rs, 0xfff000);
            Primitives::drawLine(rs, glm::vec2(offsetx, 0), glm::vec2(offsetx, tupdate));
            Primitives::setColor(rs, 0x0000ff);
            Primitives::drawLine(rs, glm::vec2(offsetx, tupdate), glm::vec2(offsetx, tupdate + trender));
        }

        // Draw 16.6ms horizontal line
        Primitives::setColor(rs, 0xff0000);
        Primitives::drawLine(rs, glm::vec2(0.0, 16.6 * scale),
            glm::vec2(DEBUG_STATS_MAX_SIZE * 4 * _view.pixelScale() + 4, 16.6 * scale));
    }
}

}
