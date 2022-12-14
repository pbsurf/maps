#pragma once

#include "tangram.h"
#include <cmath>

using namespace Tangram;

class TouchHandler;
class MapsTracks;
class MapsBookmarks;
class MapsOffline;
class MapsSources;
class MapsSearch;
class PluginManager;

struct Location
{
  double time;
  double lat;
  double lng;
  float poserr;
  double alt;
  float alterr;
  float dir;
  float direrr;
  float spd;
  float spderr;

  LngLat lngLat() const { return LngLat(lng, lat); }
};

namespace YAML { class Node; }

class MapsApp
{
public:
  MapsApp(std::unique_ptr<Platform> p);
  ~MapsApp();  // impl must be outside header to allow unique_ptr members w/ incomplete types

  void drawFrame(double time);  //int w, int h, int display_w, int display_h, double current_time, bool focused);
  void onMouseButton(double time, double x, double y, int button, int action, int mods);
  void onMouseMove(double time, double x, double y, bool pressed);
  void onMouseWheel(double x, double y, double scrollx, double scrolly, bool rotating, bool shoving);
  void onResize(int wWidth, int wHeight, int fWidth, int fHeight);
  void updateLocation(const Location& _loc);
  void updateOrientation(float azimuth, float pitch, float roll);

  void tapEvent(float x, float y);
  void doubleTapEvent(float x, float y);
  void longPressEvent(float x, float y);
  void hoverEvent(float x, float y);

  void loadSceneFile(bool setPosition = false, std::vector<SceneUpdate> updates = {});
  bool needsRender() const { return map->getPlatform().isContinuousRendering(); }
  void getMapBounds(LngLat& lngLatMin, LngLat& lngLatMax);
  //bool textureFromSVG(const char* texname, char* svg, float scale = 1.0f);
  void setPickResult(LngLat pos, std::string namestr, std::string props, int priority = 1);
  YAML::Node readSceneValue(const std::string& yamlPath);

  Location loc;
  float orientation = 0;

  MarkerID pickResultMarker = 0;
  MarkerID pickedMarkerId = 0;
  MarkerID locMarker = 0;
  std::string pickResultProps;
  LngLat pickResultCoord = {NAN, NAN};
  std::string pickLabelStr;
  bool searchActive = false;

  std::vector<SceneUpdate> sceneUpdates;
  std::string sceneFile;
  std::string sceneYaml;
  bool load_async = true;
  bool show_gui = true;
  bool recreate_context = false;
  bool wireframe_mode = false;
  bool single_tile_worker = false;

  float density = 1.0;
  float pixel_scale = 2.0;

  std::unique_ptr<TouchHandler> touchHandler;
  std::unique_ptr<MapsTracks> mapsTracks;
  std::unique_ptr<MapsBookmarks> mapsBookmarks;
  std::unique_ptr<MapsOffline> mapsOffline;
  std::unique_ptr<MapsSources> mapsSources;
  std::unique_ptr<MapsSearch> mapsSearch;
  std::unique_ptr<PluginManager> pluginManager;

  Map* map;

  static Platform* platform;
  static std::string baseDir;
  static std::string apiKey;

private:
  void showGUI();
  void showSceneGUI();
  void showViewportGUI();
  void showDebugFlagsGUI();
  void showSceneVarsGUI();
  void showPickLabelGUI();

  void dumpTileContents(float x, float y);
};
