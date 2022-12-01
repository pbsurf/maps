#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"
#include "rapidjson/document.h"

struct SearchData {
  std::string layer;
  std::vector<std::string> fields;
};

struct SearchResult
{
  int64_t id;
  LngLat pos;
  float rank;
  MarkerID markerId;
  bool isPinMarker;
  rapidjson::Document tags;  // will eventually be a DuktapeValue? standard osm tag names for now
};

class MapsSearch : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  void showGUI();
  void clearSearch();
  bool indexMBTiles();

  static void indexTileData(TileTask* task, int mapId, const std::vector<SearchData>& searchData);
  static std::vector<SearchData> parseSearchFields(const YAML::Node& node);

  std::vector<MarkerID> pinMarkers;
  std::vector<MarkerID> dotMarkers;

private:
  std::atomic_int tileCount;

  std::vector<SearchResult> listResults;
  std::vector<SearchResult> mapResults;

  float markerRadius = 25;  // in pixels
  float prevZoom = 0;

  bool markerTexturesMade = false;
  bool moreMapResultsAvail = false;

  void offlineListSearch(std::string queryStr, LngLat, LngLat);
  void offlineMapSearch(std::string queryStr, LngLat lnglat00, LngLat lngLat11);

  void onlineListSearch(std::string queryStr, LngLat lngLat00, LngLat lngLat11);
  void onlineMapSearch(std::string queryStr, LngLat lngLat00, LngLat lngLat11);
  void onlineSearch(std::string queryStr, LngLat lngLat00, LngLat lngLat11, bool isMapSearch);

  void onZoom();
  void createMarkers();
  SearchResult& addListResult(int64_t id, double lng, double lat, float rank);
  SearchResult& addMapResult(int64_t id, double lng, double lat, float rank);
  void clearSearchResults(std::vector<SearchResult>& results);
  MarkerID getPinMarker(const SearchResult& res);
  MarkerID getDotMarker(const SearchResult& res);
};