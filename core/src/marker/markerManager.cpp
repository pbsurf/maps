#include "marker/markerManager.h"

#include "data/tileData.h"
#include "gl/texture.h"
#include "marker/marker.h"
#include "scene/sceneLoader.h"
#include "scene/dataLayer.h"
#include "scene/styleContext.h"
#include "style/style.h"
#include "view/view.h"
#include "labels/labelSet.h"
#include "log.h"
#include "selection/featureSelection.h"

#include <algorithm>

namespace Tangram {

// ':' Delimiter for style params and layer-sublayer naming
static const char DELIMITER = ':';

MarkerManager::MarkerManager(const Scene& _scene, MarkerManager* _oldInst) : m_scene(_scene) {
    if(_oldInst && !_oldInst->m_markers.empty()) {
        m_dirty = true;
        m_markers = std::move(_oldInst->m_markers);
        m_idCounter = _oldInst->m_idCounter;
        for(auto& marker : m_markers) {
            marker->reset();
        }
    }
}

MarkerManager::~MarkerManager() {
    if(!m_markers.empty())
        LOGD("Destroying MarkerManager with %d markers.", int(m_markers.size()));
}


MarkerID MarkerManager::add() {
    m_dirty = true;

    // Add a new empty marker object to the list of markers.
    auto id = ++m_idCounter;
    m_markers.push_back(std::make_unique<Marker>(id));

    // Return a handle for the marker.
    return id;
}

bool MarkerManager::remove(MarkerID markerID) {
    m_dirty = true;

    for (auto it = m_markers.begin(), end = m_markers.end(); it != end; ++it) {
        if (it->get()->id() == markerID) {
            m_markers.erase(it);
            return true;
        }
    }
    return false;
}

bool MarkerManager::setStyling(MarkerID markerID, const char* styling, bool isPath) {
    Marker* marker = getMarkerOrNull(markerID);
    if (!marker) { return false; }

    marker->setStyling(std::string(styling), isPath);
    m_dirty = true;

    return true;
}

bool MarkerManager::setBitmap(MarkerID markerID, int width, int height, float density, const unsigned int* bitmapData) {
    Marker* marker = getMarkerOrNull(markerID);
    if (!marker) { return false; }

    marker->clearMesh();

    TextureOptions options;
    options.displayScale = 1.f / density;
    auto texture = std::make_unique<Texture>(options);
    texture->setPixelData(width, height, sizeof(GLuint),
                          reinterpret_cast<const GLubyte*>(bitmapData),
                          width * height * sizeof(GLuint));
    marker->setTexture(std::move(texture));
    m_dirty = true;

    return true;
}

bool MarkerManager::setVisible(MarkerID markerID, bool visible) {
    Marker* marker = getMarkerOrNull(markerID);
    if (!marker) { return false; }

    marker->setVisible(visible);
    m_dirty = true;

    return true;
}

bool MarkerManager::setDrawOrder(MarkerID markerID, int drawOrder) {
    Marker* marker = getMarkerOrNull(markerID);
    if (!marker) { return false; }

    marker->setDrawOrder(drawOrder);
    m_dirty = true;

    return true;
}

bool MarkerManager::setProperties(MarkerID markerID, Properties&& properties) {
    Marker* marker = getMarkerOrNull(markerID);
    if (!marker) { return false; }
    if (!marker->feature()) { LOGE("Marker geometry must be set before properties!"); return false; }
    marker->clearMesh();
    marker->feature()->props = std::move(properties);
    m_dirty = true;
    return true;
}

bool MarkerManager::setAlternate(MarkerID markerID, MarkerID altID) {
    Marker* marker = getMarkerOrNull(markerID);
    Marker* alt = getMarkerOrNull(altID);
    if (!marker || !alt) { return false; }
    marker->altMarker = alt;
    alt->isAltMarker = true;
    return true;
}

bool MarkerManager::setPoint(MarkerID markerID, LngLat lngLat) {
    Marker* marker = getMarkerOrNull(markerID);
    if (!marker) { return false; }

    marker->clearMesh();

    // If the marker does not have a 'point' feature mesh built, build it.
    if (!marker->feature() || marker->feature()->geometryType != GeometryType::points) {
        auto feature = std::make_unique<Feature>();
        feature->geometryType = GeometryType::points;
        feature->points.emplace_back();
        marker->setFeature(std::move(feature));
    }

    // Update the marker's bounds to the given coordinates.
    auto origin = MapProjection::lngLatToProjectedMeters(lngLat);
    marker->setBounds({ origin, origin });

    m_dirty = true;

    return true;
}

bool MarkerManager::setPointEased(MarkerID markerID, LngLat lngLat, float duration, EaseType ease) {
    Marker* marker = getMarkerOrNull(markerID);
    if (!marker) { return false; }

    m_dirty = true;

    // If the marker does not have a 'point' feature built, set that point immediately.
    if (!marker->feature() || marker->feature()->geometryType != GeometryType::points) {
        return setPoint(markerID, lngLat);
    }

    auto dest = MapProjection::lngLatToProjectedMeters({lngLat.longitude, lngLat.latitude});
    marker->setEase(dest, duration, ease);

    return true;
}

bool MarkerManager::setPolyline(MarkerID markerID, LngLat* coordinates, int count) {
    Marker* marker = getMarkerOrNull(markerID);
    if (!marker) { return false; }

    m_dirty = true;
    marker->clearMesh();

    if (!coordinates || count < 2) { return false; }

    // Build a feature for the new set of polyline points.
    auto feature = std::make_unique<Feature>();
    feature->geometryType = GeometryType::lines;
    feature->lines.emplace_back();
    auto& line = feature->lines.back();

    // Determine the bounds of the polyline.
    BoundingBox bounds;
    bounds.min = { coordinates[0].longitude, coordinates[0].latitude };
    bounds.max = bounds.min;
    for (int i = 0; i < count; ++i) {
        bounds.expand(coordinates[i].longitude, coordinates[i].latitude);
    }
    bounds.min = MapProjection::lngLatToProjectedMeters({bounds.min.x, bounds.min.y});
    bounds.max = MapProjection::lngLatToProjectedMeters({bounds.max.x, bounds.max.y});

    // Update the marker's bounds.
    marker->setBounds(bounds);

    float scale = 1.f / marker->extent();

    // Project and offset the coordinates into the marker-local coordinate system.
    auto origin = marker->origin(); // SW corner.
    for (int i = 0; i < count; ++i) {
        auto degrees = LngLat(coordinates[i].longitude, coordinates[i].latitude);
        auto meters = MapProjection::lngLatToProjectedMeters(degrees);
        line.emplace_back((meters.x - origin.x) * scale, (meters.y - origin.y) * scale);
    }

    // Update the feature data for the marker.
    marker->setFeature(std::move(feature));

    return true;
}

bool MarkerManager::setPolygon(MarkerID markerID, LngLat* coordinates, int* counts, int rings) {
    if (!m_scene.isReady()) { return false; }

    Marker* marker = getMarkerOrNull(markerID);
    if (!marker) { return false; }

    m_dirty = true;
    marker->clearMesh();

    if (!coordinates || !counts || rings < 1) { return false; }

    // Build a feature for the new set of polygon points.
    auto feature = std::make_unique<Feature>();
    feature->geometryType = GeometryType::polygons;
    feature->polygons.emplace_back();
    auto& polygon = feature->polygons.back();

    // Determine the bounds of the polygon.
    BoundingBox bounds;
    LngLat* ring = coordinates;
    for (int i = 0; i < rings; ++i) {
        int count = counts[i];
        for (int j = 0; j < count; ++j) {
            if (i == 0 && j == 0) {
                bounds.min = { ring[0].longitude, ring[0].latitude };
                bounds.max = bounds.min;
            }
            bounds.expand(ring[j].longitude, ring[j].latitude);
        }
        ring += count;
    }
    bounds.min = MapProjection::lngLatToProjectedMeters({bounds.min.x, bounds.min.y});
    bounds.max = MapProjection::lngLatToProjectedMeters({bounds.max.x, bounds.max.y});

    // Update the marker's bounds.
    marker->setBounds(bounds);

    float scale = 1.f / marker->extent();

    // Project and offset the coordinates into the marker-local coordinate system.
    auto origin = marker->origin(); // SW corner.
    ring = coordinates;
    for (int i = 0; i < rings; ++i) {
        int count = counts[i];
        polygon.emplace_back();
        auto& line = polygon.back();
        for (int j = 0; j < count; ++j) {
            auto degrees = LngLat(ring[j].longitude, ring[j].latitude);
            auto meters = MapProjection::lngLatToProjectedMeters(degrees);
            line.emplace_back((meters.x - origin.x) * scale, (meters.y - origin.y) * scale);
        }
        ring += count;
    }

    // Update the feature data for the marker.
    marker->setFeature(std::move(feature));

    return true;
}

MarkerManager::UpdateState MarkerManager::update(const View& _view, float _dt) {
    if (!m_scene.isReady() || (!m_dirty && m_markers.empty())) { return {false, false}; }

    // do this here instead of Scene::update so we don't print every time map is moved
    LOGTInit(">>> update");

    if (!m_styleContext) {
        // First call to update after scene became ready
        // Initialize Stylecontext and StyleBuilders.
        m_styleContext = std::make_unique<StyleContext>();
        m_styleContext->initFunctions(m_scene);
        for (const auto& style : m_scene.styles()) {
            m_styleBuilders[style->getName()] = style->createBuilder();
        }
    }

    m_zoom = _view.getIntegerZoom();

    bool rebuilt = false;
    bool easing = false;
    bool dirty = m_dirty;
    m_dirty = false;

    // Sort the marker list by draw order - now done here instead of in add() and setDrawOrder()
    if (dirty) {
        std::stable_sort(m_markers.begin(), m_markers.end(), Marker::compareByDrawOrder);
    }

    for (auto& marker : m_markers) {
        // skip hidden markers (else we'll end up rendering continuously since buildStyling() doesn't finish)
        if (!marker->isVisible()) { continue; }

        int builtZoom = marker->builtZoomLevel();
        if (m_zoom != builtZoom || !marker->mesh()) {
            if (builtZoom < 0) { buildStyling(*marker); }

            // prevent continuous rendering if marker styling fails
            if (buildMesh(*marker, m_zoom))
                rebuilt = true;
            else
                LOGE("Error building marker mesh.");
        }

        marker->update(_dt, _view);
        easing |= marker->isEasing();
    }
    LOGT("<<< update");

    return {rebuilt || easing || dirty, easing};
}

void MarkerManager::removeAll() {
    m_dirty = true;
    m_markers.clear();
}

void MarkerManager::rebuildAll() {
    if (m_markers.empty()) { return; }

    m_dirty = true;

    for (auto& entry : m_markers) {
        buildStyling(*entry);
        buildMesh(*entry, m_zoom);
    }
}

void MarkerManager::clearMeshes() {
    for (auto& entry : m_markers) {
        entry->clearMesh();
    }
}

const std::vector<std::unique_ptr<Marker>>& MarkerManager::markers() const {
    return m_markers;
}

bool MarkerManager::buildStyling(Marker& marker) {

    const auto& markerStyling = marker.styling();

    // If the Marker styling is a path, find the layers it specifies.
    if (markerStyling.isPath) {
        auto path = markerStyling.string;
        // The DELIMITER used by layers is currently ":", but Marker paths use "." (scene.h).
        std::replace(path.begin(), path.end(), '.', DELIMITER);
        // Start iterating over the delimited path components.
        size_t start = 0, end = 0;
        end = path.find(DELIMITER, start);
        if (path.compare(start, end - start, "layers") != 0) {
            // If the path doesn't begin with 'layers' it isn't a layer heirarchy.
            return false;
        }
        // Find the DataLayer named in our path.
        const SceneLayer* currentLayer = nullptr;
        size_t layerStart = end + 1;
        start = end + 1;
        end = path.find(DELIMITER, start);
        for (const auto& layer : m_scene.layers()) {
            if (path.compare(layerStart, end - layerStart, layer.name()) == 0) {
                currentLayer = &layer;
                marker.mergeRules(layer);
                break;
            }
        }
        // Search sublayers recursively until we can't find another token or layer.
        while (end != std::string::npos && currentLayer != nullptr) {
            start = end + 1;
            end = path.find(DELIMITER, start);
            const auto& layers = currentLayer->sublayers();
            currentLayer = nullptr;
            for (const auto& layer : layers) {
                if (path.compare(layerStart, end - layerStart, layer.name()) == 0) {
                    currentLayer = &layer;
                    marker.mergeRules(layer);
                    break;
                }
            }
        }
        // The last token found should have been "draw".
        if (path.compare(start, end - start, "draw") != 0) {
            return false;
        }
        // The draw group name should come next.
        start = end + 1;
        end = path.find(DELIMITER, start);
        // Find the rule in the merged set whose name matches the final token.
        return marker.finalizeRuleMergingForName(path.substr(start, end - start));
    }


    // If the styling is not a path, try to load it as a string of YAML.
    size_t prevFunctionCount = m_functions.size();

    std::vector<StyleParam> params;
    YAML::Node node = YAML::Load(markerStyling.string);
    if (!node) {
        LOG("Invalid marker styling '%s'", markerStyling.string.c_str());
        return false;
    }
    SceneLoader::applyGlobals(m_scene.config(), node);
    params = SceneLoader::parseStyleParams(node, m_stops, m_functions);

    // The StyleContext initially contains the set of functions from the scene definition, but the parsed style params
    // for the Marker use a separate Marker function list and the function indices are relative to that list. So to get
    // the correct function indices for the StyleContext we offset them by the number of functions in the scene.
    size_t functionIndexOffset = m_scene.functions().size();
    for (auto& p : params) {
        if (p.function >= 0) {
            p.function += functionIndexOffset;
        }
    }
    // Compile any new JS functions used for styling.
    for (auto i = prevFunctionCount; i < m_functions.size(); ++i) {
        m_styleContext->addFunction(m_functions[i]);
    }

    marker.setDrawRuleData(std::make_unique<DrawRuleData>("", 0, std::move(params)));

    return true;
}

bool MarkerManager::buildMesh(Marker& marker, int zoom) {

    marker.clearMesh();

    auto feature = marker.feature();
    auto rule = marker.drawRule();
    if (!feature || !rule) { return false; }

    StyleBuilder* styler = nullptr;
    {
        auto name = rule->getStyleName();
        auto it = m_styleBuilders.find(name);
        if (it != m_styleBuilders.end()) {
            styler = it->second.get();
        } else {
            LOGN("Invalid style %s", name.c_str());
            return false;
        }
    }

    // Apply default draw rules defined for this style
    styler->style().applyDefaultDrawRules(*rule);

    m_styleContext->setTileID(TileID(0, 0, zoom));
    m_styleContext->setFeature(*feature);
    bool valid = marker.evaluateRuleForContext(*m_styleContext);

    if (!valid) { return false; }

    styler->setup(marker, zoom);

    uint32_t selectionColor = 0;
    bool interactive = false;
    if (rule->get(StyleParamKey::interactive, interactive) && interactive) {
        if (selectionColor == 0) {
            selectionColor = m_scene.featureSelection()->nextColorIdentifier();
        }
        rule->selectionColor = selectionColor;
    } else {
        rule->selectionColor = 0;
    }

    if (!styler->addFeature(*feature, *rule)) { return false; }

    marker.setSelectionColor(selectionColor);
    marker.setMesh(styler->style().getID(), zoom, styler->build());

    return true;
}

const Marker* MarkerManager::getMarkerOrNullBySelectionColor(uint32_t selectionColor) const {
    for (const auto& marker : m_markers) {
        if (marker->isVisible() && marker->selectionColor() == selectionColor) {
            return marker.get();
        }
    }

    return nullptr;
}

Marker* MarkerManager::getMarkerOrNull(MarkerID markerID) {
    if (!markerID) { return nullptr; }
    // typical use case is to add marker, then call fns to configure it, so caller is most likely to want
    //  marker at end of list, so we search from end
    for (size_t ii = m_markers.size(); ii--;) {
        if (m_markers[ii]->id() == markerID) { return m_markers[ii].get(); }
    }
    return nullptr;
}

} // namespace Tangram
