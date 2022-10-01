#include "glfwApp.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_stl.h"

#include "math.h"
#include "rapidxml.hpp"
#include "rapidxml_utils.hpp"

// for building search DB
#include "document.h"  // rapidjson
#include "data/tileData.h"
#include "scene/scene.h"
#include "sqlite3/sqlite3.h"

#ifdef TANGRAM_WINDOWS
#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#endif // TANGRAM_WINDOWS

#define GLFW_INCLUDE_GLEXT
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <atomic>
#include "gl.h"

#ifndef BUILD_NUM_STRING
#define BUILD_NUM_STRING ""
#endif

namespace Tangram {

namespace GlfwApp {

// Forward-declare our callback functions.
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void cursorMoveCallback(GLFWwindow* window, double x, double y);
void scrollCallback(GLFWwindow* window, double scrollx, double scrolly);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void dropCallback(GLFWwindow* window, int count, const char** paths);
void framebufferResizeCallback(GLFWwindow* window, int fWidth, int fHeight);

// Forward-declare GUI functions.
void showSearchGUI();
void showPickLabelGUI();
void showSceneVarsGUI();
void showDebugFlagsGUI();
void showViewportGUI();
void showSceneGUI();
void showMarkerGUI();

constexpr double double_tap_time = 0.5; // seconds
constexpr double scroll_span_multiplier = 0.05; // scaling for zoom and rotation
constexpr double scroll_distance_multiplier = 5.0; // scaling for shove
constexpr double single_tap_time = 0.25; //seconds (to avoid a long press being considered as a tap)

std::string sceneFile = "scene.yaml";
std::string sceneYaml;
std::string apiKey;

bool markerUseStylingPath = true;
std::string markerStylingPath = "layers.touch.point.draw.icons";
std::string markerStylingString = R"RAW(
style: text
text_source: "function() { return 'MARKER'; }"
font:
    family: Open Sans
    size: 12px
    fill: white
)RAW";
std::string polylineStyle = "{ style: lines, interactive: true, color: red, width: 4px, order: 5000 }";


GLFWwindow* main_window = nullptr;
Tangram::Map* map = nullptr;
int width = 800;
int height = 600;
float density = 1.0;
float pixel_scale = 2.0;
bool recreate_context = false;

bool was_panning = false;
double last_time_released = -double_tap_time; // First click should never trigger a double tap
double last_time_pressed = 0.0;
double last_time_moved = 0.0;
double last_x_down = 0.0;
double last_y_down = 0.0;
double last_x_velocity = 0.0;
double last_y_velocity = 0.0;

bool wireframe_mode = false;
bool show_gui = true;
bool load_async = true;
bool add_point_marker_on_click = false;
bool add_polyline_marker_on_click = false;
bool point_markers_position_clipped = false;

std::string pickLabelStr;
std::string gpxFile;
std::vector<MarkerID> trackMarkers;
MarkerID trackHoverMarker = 0;
std::vector<MarkerID> searchMarkers;

std::string searchMarkerStyleStr = R"#(
style: pick-marker
collide: false
offset: [0px, -11px]
order: 900
text:
  text_source: "function() { return \"%s\"; }"
  offset: [0px, -11px]
  priority: %d
  font:
    family: Open Sans
    size: 12px
    fill: black
)#";

struct PointMarker {
    MarkerID markerId;
    LngLat coordinates;
};

std::vector<PointMarker> point_markers;

Tangram::MarkerID polyline_marker = 0;
std::vector<Tangram::LngLat> polyline_marker_coordinates;

Tangram::MarkerID pickResultMarker = 0;

std::vector<SceneUpdate> sceneUpdates;
const char* apiKeyScenePath = "global.sdk_api_key";

void loadSceneFile(bool setPosition, std::vector<SceneUpdate> updates) {

    for (auto& update : updates) {
        bool found = false;
        for (auto& prev : sceneUpdates) {
            if (update.path == prev.path) {
                prev = update;
                found = true;
                break;
            }
        }
        if (!found) { sceneUpdates.push_back(update); }
    }

    if (load_async) {
        if (!sceneYaml.empty()) {
            map->loadSceneYamlAsync(sceneYaml, sceneFile, setPosition, sceneUpdates);
        } else {
            map->loadSceneAsync(sceneFile, setPosition, sceneUpdates);
        }
    } else {
        if (!sceneYaml.empty()) {
            map->loadSceneYaml(sceneYaml, sceneFile, setPosition, sceneUpdates);
        } else {
            map->loadScene(sceneFile, setPosition, sceneUpdates);
        }
    }
}

void parseArgs(int argc, char* argv[]) {
    // Load file from command line, if given.
    int argi = 0;
    while (++argi < argc) {
        if (strcmp(argv[argi - 1], "-f") == 0) {
            sceneFile = std::string(argv[argi]);
            LOG("File from command line: %s\n", argv[argi]);
            break;
        }
        if (strcmp(argv[argi - 1], "-s") == 0) {

            if (argi+1 < argc) {
                sceneYaml = std::string(argv[argi]);
                sceneFile = std::string(argv[argi+1]);
                LOG("Yaml from command line: %s, resource path: %s\n",
                    sceneYaml.c_str(), sceneFile.c_str());
            } else {
                LOG("-s options requires YAML string and resource path");
                exit(1);
            }
            break;
        }
    }
}

void setScene(const std::string& _path, const std::string& _yaml) {
    sceneFile = _path;
    sceneYaml = _yaml;
}

void create(std::unique_ptr<Platform> p, int w, int h) {

    width = w;
    height = h;

    if (!glfwInit()) {
        assert(false);
        return;
    }

    char* nextzenApiKeyEnvVar = getenv("NEXTZEN_API_KEY");
    if (nextzenApiKeyEnvVar && strlen(nextzenApiKeyEnvVar) > 0) {
        apiKey = nextzenApiKeyEnvVar;
    } else {
        LOGW("No API key found!\n\nNextzen data sources require an API key. "
             "Sign up for a key at https://developers.nextzen.org/about.html and then set it from the command line with: "
             "\n\n\texport NEXTZEN_API_KEY=YOUR_KEY_HERE"
             "\n\nOr, if using an IDE on macOS, with: "
             "\n\n\tlaunchctl setenv NEXTZEN_API_KEY YOUR_API_KEY\n");
    }

    if (!apiKey.empty()) {
        sceneUpdates.push_back(SceneUpdate(apiKeyScenePath, apiKey));
    }

    // Setup tangram
    if (!map) {
        map = new Tangram::Map(std::move(p));
    }

    // Build a version string for the window title.
    char versionString[256] = { 0 };
    std::snprintf(versionString, sizeof(versionString), "Tangram ES %d.%d.%d " BUILD_NUM_STRING,
        TANGRAM_VERSION_MAJOR, TANGRAM_VERSION_MINOR, TANGRAM_VERSION_PATCH);

    const char* glsl_version = "#version 120";

    // Create a windowed mode window and its OpenGL context
    glfwWindowHint(GLFW_SAMPLES, 2);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    if (!main_window) {
        main_window = glfwCreateWindow(width, height, versionString, NULL, NULL);
    }
    if (!main_window) {
        glfwTerminate();
    }

    // Make the main_window's context current
    glfwMakeContextCurrent(main_window);
    glfwSwapInterval(1); // Enable vsync

#ifdef TANGRAM_WINDOWS
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
#endif

    // Set input callbacks
    glfwSetFramebufferSizeCallback(main_window, framebufferResizeCallback);
    glfwSetMouseButtonCallback(main_window, mouseButtonCallback);
    glfwSetCursorPosCallback(main_window, cursorMoveCallback);
    glfwSetScrollCallback(main_window, scrollCallback);
    glfwSetKeyCallback(main_window, keyCallback);
    glfwSetDropCallback(main_window, dropCallback);

    glfwSetCharCallback(main_window, ImGui_ImplGlfw_CharCallback);

    // Setup graphics
    map->setupGL();
    int fWidth = 0, fHeight = 0;
    glfwGetFramebufferSize(main_window, &fWidth, &fHeight);
    framebufferResizeCallback(main_window, fWidth, fHeight);

    // Setup ImGui binding
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls
    ImGui_ImplGlfw_InitForOpenGL(main_window, false);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Setup style
    ImGui::StyleColorsDark();
    ImGui::GetIO().FontGlobalScale = 2.0f;

}

void run() {
    loadSceneFile(false);  //true);
    // default position: Alamo Square, SF - overriden by scene camera position if async load
    map->setPickRadius(1.0f);
    map->setZoom(13);
    map->setPosition(-122.434668, 37.776444);

    double lastTime = glfwGetTime();

    // Loop until the user closes the window
    while (!glfwWindowShouldClose(main_window)) {

        if(show_gui) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Create ImGui interface.
            // ImGui::ShowDemoWindow();
            showSceneGUI();
            showViewportGUI();
            showMarkerGUI();
            showDebugFlagsGUI();
            showSceneVarsGUI();
            showSearchGUI();
            showPickLabelGUI();
        }
        double currentTime = glfwGetTime();
        double delta = currentTime - lastTime;
        lastTime = currentTime;

        // Render
        MapState state = map->update(delta);
        if (state.isAnimating()) {
            map->getPlatform().requestRender();
        }

        const bool wireframe = wireframe_mode;
        if(wireframe) {
            glPolygonMode(GL_FRONT, GL_LINE);
            glPolygonMode(GL_BACK, GL_LINE);
        }
        map->render();
        if(wireframe) {
            glPolygonMode(GL_FRONT, GL_FILL);
            glPolygonMode(GL_BACK, GL_FILL);
        }

        if (show_gui) {
            // Render ImGui interface.
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        // Swap front and back buffers
        glfwSwapBuffers(main_window);

        // Poll for and process events
        if (map->getPlatform().isContinuousRendering()) {
            glfwPollEvents();
        } else {
            glfwWaitEvents();
        }
    }
}

void stop(int) {
    if (!glfwWindowShouldClose(main_window)) {
        logMsg("shutdown\n");
        glfwSetWindowShouldClose(main_window, 1);
        glfwPostEmptyEvent();
    } else {
        logMsg("killed!\n");
        exit(1);
    }
}

void destroy() {
    if (map) {
        delete map;
        map = nullptr;
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    if (main_window) {
        glfwDestroyWindow(main_window);
        main_window = nullptr;
    }
    glfwTerminate();
}

template<typename T>
static constexpr T clamp(T val, T min, T max) {
    return val > max ? max : val < min ? min : val;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {

    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    if (ImGui::GetIO().WantCaptureMouse) {
        return; // Imgui is handling this event.
    }

    if (button != GLFW_MOUSE_BUTTON_1) {
        return; // This event is for a mouse button that we don't care about
    }

    double x, y;
    glfwGetCursorPos(window, &x, &y);
    x *= density;
    y *= density;
    double time = glfwGetTime();

    if (was_panning && action == GLFW_RELEASE) {
        was_panning = false;
        auto vx = clamp(last_x_velocity, -2000.0, 2000.0);
        auto vy = clamp(last_y_velocity, -2000.0, 2000.0);
        map->handleFlingGesture(x, y, vx, vy);
        return; // Clicks with movement don't count as taps, so stop here
    }

    if (action == GLFW_PRESS) {
        map->handlePanGesture(0.0f, 0.0f, 0.0f, 0.0f);
        last_x_down = x;
        last_y_down = y;
        last_time_pressed = time;
        return;
    }

    if ((time - last_time_released) < double_tap_time) {
        // Double tap recognized
        const float duration = 0.5f;
        Tangram::LngLat tapped, current;
        map->screenPositionToLngLat(x, y, &tapped.longitude, &tapped.latitude);
        map->getPosition(current.longitude, current.latitude);
        auto pos = map->getCameraPosition();
        pos.zoom += 1.f;
        pos.longitude = tapped.longitude;
        pos.latitude = tapped.latitude;

        map->setCameraPositionEased(pos, duration, EaseType::quint);
    } else if ((time - last_time_pressed) < single_tap_time) {
        // Single tap recognized
        Tangram::LngLat location;
        map->screenPositionToLngLat(x, y, &location.longitude, &location.latitude);
        double xx, yy;
        map->lngLatToScreenPosition(location.longitude, location.latitude, &xx, &yy);

        logMsg("------\n");
        logMsg("LngLat: %f, %f\n", location.longitude, location.latitude);
        logMsg("Clicked:  %f, %f\n", x, y);
        logMsg("Remapped: %f, %f\n", xx, yy);

        map->pickLabelAt(x, y, [&](const LabelPickResult* result) {
            pickLabelStr.clear();
            if (result == nullptr) {
                logMsg("Pick Label result is null.\n");
                return;
            }
            if (pickResultMarker == 0) {
                pickResultMarker = map->markerAdd();
                map->markerSetStylingFromPath(pickResultMarker, "layers.pick-result.draw.pick-marker");
            }
            map->markerSetPoint(pickResultMarker, result->coordinates);
            map->markerSetVisible(pickResultMarker, true);
            for(MarkerID mrkid : searchMarkers)
              map->markerSetVisible(mrkid, false);

            std::string itemId;
            logMsg("Pick label result:\n");
            for (const auto& item : result->touchItem.properties->items()) {
                if(item.key == "id")
                  itemId = Properties::asString(item.value);
                std::string l = "  " + item.key + " = " + Properties::asString(item.value) + "\n";
                //logMsg("  %s = %s\n", item.key.c_str(), Properties::asString(item.value).c_str());
                logMsg(l.c_str());
                pickLabelStr += l;
            }

            // query OSM API with id - append .json to get JSON instead of XML
            if(!itemId.empty()) {
              auto url = Url("https://www.openstreetmap.org/api/0.6/node/" + itemId);
              map->getPlatform().startUrlRequest(url, [url, itemId](UrlResponse&& response) {
                if(response.error) {
                  logMsg("Error fetching %s: %s\n", url.data().c_str(), response.error);
                  return;
                }
                response.content.push_back('\0');
                rapidxml::xml_document<> doc;
                doc.parse<0>(response.content.data());
                auto tag = doc.first_node("osm")->first_node("node")->first_node("tag");
                if(tag) pickLabelStr = "id = " + itemId + "\n";
                while(tag) {
                  auto key = tag->first_attribute("k");
                  auto val = tag->first_attribute("v");
                  pickLabelStr += key->value() + std::string(" = ") + val->value() + std::string("\n");
                  tag = tag->next_sibling("tag");
                }
              });
            }
        });

        if (add_point_marker_on_click) {
            auto marker = map->markerAdd();
            map->markerSetPoint(marker, location);
            if (markerUseStylingPath) {
                map->markerSetStylingFromPath(marker, markerStylingPath.c_str());
            } else {
                map->markerSetStylingFromString(marker, markerStylingString.c_str());
            }

            point_markers.push_back({ marker, location });
        }

        if (add_polyline_marker_on_click) {
            if (polyline_marker_coordinates.empty()) {
                polyline_marker = map->markerAdd();
                map->markerSetStylingFromString(polyline_marker, polylineStyle.c_str());
            }
            polyline_marker_coordinates.push_back(location);
            map->markerSetPolyline(polyline_marker, polyline_marker_coordinates.data(), polyline_marker_coordinates.size());
        }

        map->getPlatform().requestRender();
    }

    last_time_released = time;

}

void cursorMoveCallback(GLFWwindow* window, double x, double y) {

    if (ImGui::GetIO().WantCaptureMouse) {
        return; // Imgui is handling this event.
    }

    x *= density;
    y *= density;

    int action = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1);
    double time = glfwGetTime();

    if (action == GLFW_PRESS) {

        if (was_panning) {
            map->handlePanGesture(last_x_down, last_y_down, x, y);
        }

        was_panning = true;
        last_x_velocity = (x - last_x_down) / (time - last_time_moved);
        last_y_velocity = (y - last_y_down) / (time - last_time_moved);
        last_x_down = x;
        last_y_down = y;

    }

    last_time_moved = time;

}

void scrollCallback(GLFWwindow* window, double scrollx, double scrolly) {

    ImGui_ImplGlfw_ScrollCallback(window, scrollx, scrolly);
    if (ImGui::GetIO().WantCaptureMouse) {
        return; // Imgui is handling this event.
    }

    double x, y;
    glfwGetCursorPos(window, &x, &y);
    x *= density;
    y *= density;

    bool rotating = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    bool shoving = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

    if (shoving) {
        map->handleShoveGesture(scroll_distance_multiplier * scrolly);
    } else if (rotating) {
        map->handleRotateGesture(x, y, scroll_span_multiplier * scrolly);
    } else {
        map->handlePinchGesture(x, y, 1.0 + scroll_span_multiplier * scrolly, 0.f);
    }

}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {

    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
    if (ImGui::GetIO().WantCaptureKeyboard) {
        return; // Imgui is handling this event.
    }

    CameraPosition camera = map->getCameraPosition();

    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_A:
                load_async = !load_async;
                LOG("Toggle async load: %d", load_async);
                break;
            case GLFW_KEY_D:
                show_gui = !show_gui;
                break;
            case GLFW_KEY_BACKSPACE:
                recreate_context = true;
                break;
            case GLFW_KEY_R:
                loadSceneFile();
                break;
            case GLFW_KEY_Z: {
                auto pos = map->getCameraPosition();
                pos.zoom += 1.f;
                map->setCameraPositionEased(pos, 1.5f);
                break;
            }
            case GLFW_KEY_N: {
                auto pos = map->getCameraPosition();
                pos.rotation = 0.f;
                map->setCameraPositionEased(pos, 1.f);
                break;
            }
            case GLFW_KEY_S:
                if (pixel_scale == 1.0) {
                    pixel_scale = 2.0;
                } else if (pixel_scale == 2.0) {
                    pixel_scale = 0.75;
                } else {
                    pixel_scale = 1.0;
                }
                map->setPixelScale(pixel_scale*density);
                break;
            case GLFW_KEY_P:
                loadSceneFile(false, {SceneUpdate{"cameras", "{ main_camera: { type: perspective } }"}});
                break;
            case GLFW_KEY_I:
                loadSceneFile(false, {SceneUpdate{"cameras", "{ main_camera: { type: isometric } }"}});
                break;
            case GLFW_KEY_M:
                map->loadSceneYamlAsync("{ scene: { background: { color: red } } }", std::string(""));
                break;
            case GLFW_KEY_G:
                {
                    static bool geoJSON = false;
                    if (!geoJSON) {
                        loadSceneFile(false,
                                      { SceneUpdate{"sources.osm.type", "GeoJSON"},
                                        SceneUpdate{"sources.osm.url", "https://tile.mapzen.com/mapzen/vector/v1/all/{z}/{x}/{y}.json"}});
                    } else {
                        loadSceneFile(false,
                                      { SceneUpdate{"sources.osm.type", "MVT"},
                                        SceneUpdate{"sources.osm.url", "https://tile.mapzen.com/mapzen/vector/v1/all/{z}/{x}/{y}.mvt"}});
                    }
                    geoJSON = !geoJSON;
                }
                break;
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, true);
                break;
            case GLFW_KEY_F1:
                map->setPosition(-74.00976419448854, 40.70532700869127);
                map->setZoom(16);
                break;
            case GLFW_KEY_F2:
                map->setPosition(8.82, 53.08);
                map->setZoom(14);
                break;
            case GLFW_KEY_F3:
                camera.longitude = 8.82;
                camera.latitude = 53.08;
                camera.zoom = 16.f;
                map->flyTo(camera, -1.f, 2.0);
                break;
            case GLFW_KEY_F4:
                camera.longitude = 8.82;
                camera.latitude = 53.08;
                camera.zoom = 10.f;
                map->flyTo(camera, -1.f, 2.5);
                break;
            case GLFW_KEY_F5:
                camera.longitude = -74.00976419448854;
                camera.latitude = 40.70532700869127;
                camera.zoom = 16.f;
                map->flyTo(camera, 4.);
                break;
            case GLFW_KEY_F6:
                camera.longitude = -122.41;
                camera.latitude = 37.7749;
                camera.zoom = 16.f;
                map->flyTo(camera, -1.f, 4.0);
                break;
            case GLFW_KEY_F7:
                camera.longitude = 139.839478;
                camera.latitude = 35.652832;
                camera.zoom = 16.f;
                map->flyTo(camera, -1.f, 1.0);
                break;
            case GLFW_KEY_F8: // Beijing
                map->setCameraPosition({116.39703, 39.91006, 12.5});
                break;
            case GLFW_KEY_F9: // Bangkok
                map->setCameraPosition({100.49216, 13.7556, 12.5});
                break;
            case GLFW_KEY_F10: // Dhaka
                map->setCameraPosition({90.40166, 23.72909, 14.5});
                break;
            case GLFW_KEY_F11: // Tehran
                map->setCameraPosition({51.42086, 35.7409, 13.5});
                break;
            case GLFW_KEY_W:
                map->onMemoryWarning();
                break;
            default:
                break;
        }
    }
}

void dropCallback(GLFWwindow* window, int count, const char** paths) {

    sceneFile = "file://" + std::string(paths[0]);
    sceneYaml.clear();

    loadSceneFile();
}

void framebufferResizeCallback(GLFWwindow* window, int fWidth, int fHeight) {

    int wWidth = 0, wHeight = 0;
    glfwGetWindowSize(main_window, &wWidth, &wHeight);
    float new_density = (float)fWidth / (float)wWidth;
    if (new_density != density) {
        recreate_context = true;
        density = new_density;
    }
    map->setPixelScale(pixel_scale*density);
    map->resize(fWidth, fHeight);
}

// common fns

template<typename ... Args>
std::string fstring( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ) return "";
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

template<template<class, class...> class Container, class... Container_Params>
Container<std::string, Container_Params... > splitStr(std::string s, const char* delims, bool skip_empty = false)
{
  Container<std::string, Container_Params... > elems;
  std::string item;
  size_t start = 0, end = 0;
  while((end = s.find_first_of(delims, start)) != std::string::npos) {
    if(!skip_empty || end > start)
      elems.insert(elems.end(), s.substr(start, end-start));
    start = end + 1;
  }
  if(start < s.size())
    elems.insert(elems.end(), s.substr(start));
  return elems;
}

// https://stackoverflow.com/questions/27928
double lngLatDist(LngLat r1, LngLat r2) {
    constexpr double p = 3.14159265358979323846/180;
    double a = 0.5 - cos((r2.latitude-r1.latitude)*p)/2 + cos(r1.latitude*p) * cos(r2.latitude*p) * (1-cos((r2.longitude-r1.longitude)*p))/2;
    return 12742 * asin(sqrt(a));  // kilometers
}

// building search DB from tiles
sqlite3* searchDB = NULL;

typedef std::function<void(sqlite3_stmt*)> SQLiteStmtFn;

static bool DB_exec(sqlite3* db, const char* sql, SQLiteStmtFn cb = SQLiteStmtFn())
{
  //if(sqlite3_exec(searchDB, sql, cb ? sqlite_static_helper : NULL, cb ? &cb : NULL, &zErrMsg) != SQLITE_OK) {
  int res;
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
    logMsg("sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(searchDB));
    return false;
  }
  while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
    if(cb)
      cb(stmt);
  }
  if(res != SQLITE_DONE && res != SQLITE_OK)
    logMsg("sqlite3_step error: %s\n", sqlite3_errmsg(searchDB));
  sqlite3_finalize(stmt);
  return true;
}

static LngLat mapCenter;
static bool sortByDist = false;

static void udf_osmSearchRank(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  if(argc != 3) {
    sqlite3_result_error(context, "osmSearchRank - Invalid number of arguments (3 required).", -1);
    return;
  }
  if(sqlite3_value_type(argv[0]) != SQLITE_FLOAT || sqlite3_value_type(argv[1]) != SQLITE_FLOAT || sqlite3_value_type(argv[2]) != SQLITE_FLOAT) {
    sqlite3_result_double(context, -1.0);
    return;
  }
  // sqlite FTS5 rank is roughly -1*number_of_words_in_query; ordered from -\inf to 0
  double rank = sortByDist ? -1.0 : sqlite3_value_double(argv[0]);
  double lon = sqlite3_value_double(argv[1]);  // distance from search center point in meters
  double lat = sqlite3_value_double(argv[2]);  // distance from search center point in meters
  double dist = lngLatDist(mapCenter, LngLat(lon, lat));
  // obviously will want a more sophisticated ranking calculation in the future
  sqlite3_result_double(context, rank/log2(1+dist));
}

// segfault if GLM_FORCE_CTOR_INIT is defined for some units and not others!!!
static LngLat tileCoordToLngLat(const TileID& tileId, const glm::vec2& tileCoord)
{
  double scale = MapProjection::metersPerTileAtZoom(tileId.z);
  ProjectedMeters tileOrigin = MapProjection::tileSouthWestCorner(tileId);
  ProjectedMeters meters = glm::dvec2(tileCoord) * scale + tileOrigin;
  return MapProjection::projectedMetersToLngLat(meters);
}

//search_data:
//    - layer: place
//      fields: name, class

struct SearchData {
  std::string layer;
  std::vector<std::string> fields;
};

std::vector<SearchData> searchData;
std::atomic<int> tileCount{};

static void processTileData(TileTask* task, sqlite3_stmt* stmt)
{
  auto tileData = task->source()->parse(*task);
  for(const Layer& layer : tileData->layers) {
    for(const SearchData& searchdata : searchData) {
      if(searchdata.layer == layer.name) {
        for(const Feature& feature : layer.features) {
          if(feature.props.getString("name").empty() || feature.points.empty())
            continue;  // skip POIs w/o name or geometry
          auto lnglat = tileCoordToLngLat(task->tileId(), feature.points.front());
          std::string tags;
          for(const std::string& field : searchdata.fields) {
            tags += feature.props.getString(field);
            tags += ' ';
          }
          // insert row
          sqlite3_bind_text(stmt, 1, tags.c_str(), tags.size() - 1, SQLITE_STATIC);  // drop trailing separator
          sqlite3_bind_text(stmt, 2, feature.props.toJson().c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_bind_double(stmt, 3, lnglat.longitude);
          sqlite3_bind_double(stmt, 4, lnglat.latitude);
          if (sqlite3_step(stmt) != SQLITE_DONE)
            logMsg("sqlite3_step failed: %d\n", sqlite3_errmsg(sqlite3_db_handle(stmt)));
          sqlite3_clear_bindings(stmt);  // not necessary?
          sqlite3_reset(stmt);  // necessary to reuse statement
        }
      }
    }
  }
}

static bool initSearch()
{
  static const char* dbPath = "/home/mwhite/maps/fts1.sqlite";
  if(sqlite3_open_v2(dbPath, &searchDB, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK)
    return true;
  sqlite3_close(searchDB);
  searchDB = NULL;

  // load search config
  for(int ii = 0; ii < 100; ++ii) {
    std::string layer = map->readSceneValue(fstring("global.search_data#%d.layer", ii));
    if(layer.empty()) break;
    std::string fieldstr = map->readSceneValue(fstring("global.search_data#%d.fields", ii));
    searchData.push_back({layer, splitStr<std::vector>(fieldstr, ", ", true)});
  }
  if(searchData.empty()) {
    //logMsg("No search fields specified, search will be disabled.\n");
    return false;
  }

  // DB doesn't exist - create it
  if(sqlite3_open_v2(dbPath, &searchDB, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
    logMsg("Error creating %s", dbPath);
    sqlite3_close(searchDB);
    searchDB = NULL;
    return false;
  }

  // get bounds from mbtiles DB
  // mbtiles spec: https://github.com/mapbox/mbtiles-spec/blob/master/1.3/spec.md
  sqlite3* tileDB;
  if(sqlite3_open_v2("/home/mwhite/maps/sf.mbtiles", &tileDB, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    logMsg("Error opening tile DB: %s\n", sqlite3_errmsg(tileDB));
    return false;
  }
  int min_row, max_row, min_col, max_col, max_zoom;
  const char* boundsSql = "SELECT min(tile_row), max(tile_row), min(tile_column), max(tile_column), max(zoom_level) FROM tiles WHERE zoom_level = (SELECT max(zoom_level) FROM tiles);";
  DB_exec(tileDB, boundsSql, [&](sqlite3_stmt* stmt){
    min_row = sqlite3_column_int(stmt, 0);
    max_row = sqlite3_column_int(stmt, 1);
    min_col = sqlite3_column_int(stmt, 2);
    max_col = sqlite3_column_int(stmt, 3);
    max_zoom = sqlite3_column_int(stmt, 4);
  });
  sqlite3_close(tileDB);

  Scene* scene = map->getScene();
  auto& tileSources = scene->tileSources();
  auto& tileSrc = tileSources.front();

  //sqlite3_exec(searchDB, "PRAGMA synchronous=OFF", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA count_changes=OFF", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA journal_mode=MEMORY", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA temp_store=MEMORY", NULL, NULL, &errorMessage);
  DB_exec(searchDB, "CREATE VIRTUAL TABLE points_fts USING fts5(tags, props UNINDEXED, lng UNINDEXED, lat UNINDEXED);");
  // search history
  DB_exec(searchDB, "CREATE TABLE history(query TEXT UNIQUE);");

  sqlite3_exec(searchDB, "BEGIN TRANSACTION", NULL, NULL, NULL);
  sqlite3_stmt* stmt;
  char const* strStmt = "INSERT INTO points_fts (tags,props,lng,lat) VALUES (?,?,?,?);";
  if(sqlite3_prepare_v2(searchDB, strStmt, -1, &stmt, NULL) != SQLITE_OK) {
    logMsg("sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(searchDB));
    sqlite3_mutex_leave(sqlite3_db_mutex(searchDB));
    return false;
  }

  tileCount = (max_row-min_row+1)*(max_col-min_col+1);
  auto tilecb = TileTaskCb{[stmt](std::shared_ptr<TileTask> task) {
    if (task->hasData())
      processTileData(task.get(), stmt);
    if(--tileCount == 0) {
      sqlite3_exec(searchDB, "COMMIT TRANSACTION", NULL, NULL, NULL);
      sqlite3_finalize(stmt);  // then ... stmt = NULL;
      logMsg("Search index built.\n");
    }
  }};

  for(int row = min_row; row <= max_row; ++row) {
    for(int col = min_col; col <= max_col; ++col) {
      TileID tileid(col, (1 << max_zoom) - 1 - row, max_zoom);
      tileSrc->loadTileData(std::make_shared<BinaryTileTask>(tileid, tileSrc), tilecb);
    }
  }

  return true;
}

void showSearchGUI()
{
  static int resultOffset = 0;
  static bool searchActive = false;
  static std::vector<std::string> autocomplete;
  static std::vector<rapidjson::Document> results;
  static std::vector<LngLat> respts;
  static std::string searchStr;  // imgui compares to this to determine if text is edited, so make persistant

  if(!searchDB) {
    if(!initSearch())
      return;
    // add search ranking fn
    if(sqlite3_create_function(searchDB, "osmSearchRank", 3, SQLITE_UTF8, 0, udf_osmSearchRank, 0, 0) != SQLITE_OK)
      logMsg("sqlite3_create_function: error creating osmSearchRank");
  }
  if(!ImGui::CollapsingHeader("Search", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  LngLat minLngLat(180, 90);
  LngLat maxLngLat(-180, -90);
  bool ent = ImGui::InputText("Query", &searchStr, ImGuiInputTextFlags_EnterReturnsTrue);
  bool edited = ImGui::IsItemEdited();
  // history (autocomplete)
  if(ent) {
    // IGNORE prevents error from UNIQUE constraint
    DB_exec(searchDB, fstring("INSERT OR IGNORE INTO history (query) VALUES ('%s');", searchStr.c_str()).c_str());
  }
  else {
    if(edited) {
      autocomplete.clear();
      std::string histq = fstring("SELECT * FROM history WHERE query LIKE '%s%%' LIMIT 5;", searchStr.c_str());
      DB_exec(searchDB, histq.c_str(), [&](sqlite3_stmt* stmt){
        autocomplete.emplace_back( (const char*)(sqlite3_column_text(stmt, 0)) );
      });
    }
    if(!autocomplete.empty()) {
      std::vector<const char*> cautoc;
      for(const auto& s : autocomplete)
        cautoc.push_back(s.c_str());

      int currItem;
      if(ImGui::ListBox("History", &currItem, cautoc.data(), cautoc.size())) {
        ent = true;
        searchStr = autocomplete[currItem];
        //autocomplete.clear();
      }
    }
  }

  // sort by distance only?
  bool nextPage = false;
  if (ImGui::Checkbox("Sort by distance", &sortByDist)) {
    ent = true;
  }
  if(ImGui::Button("Clear")) {
    //ImGui::SetKeyboardFocusHere(-1);
    if(searchActive)
      map->updateGlobals({SceneUpdate{"global.search_active", "false"}});
    searchActive = false;
    searchStr.clear();
    autocomplete.clear();
    ent = true;
  }
  else if (!results.empty()) {
    ImGui::SameLine();
    if(ImGui::Button("More"))
      nextPage = !ent && !edited;
  }

  if(ent || edited || nextPage) {
    if(!nextPage) {
      results.clear();
      respts.clear();
    }
    resultOffset = nextPage ? resultOffset + 20 : 0;
    size_t markerIdx = nextPage ? results.size() : 0;
    if(searchStr.size() > 2) {
      map->getPosition(mapCenter.longitude, mapCenter.latitude);
      std::string query = fstring("SELECT props, lng, lat FROM points_fts WHERE points_fts "
          "MATCH '%s*' ORDER BY osmSearchRank(rank, lng, lat) LIMIT 20 OFFSET %d;", searchStr.c_str(), resultOffset);

      DB_exec(searchDB, query.c_str(), [&](sqlite3_stmt* stmt){
        double lng = sqlite3_column_double(stmt, 1);
        double lat = sqlite3_column_double(stmt, 2);
        respts.push_back(LngLat(lng, lat));
        results.emplace_back();
        results.back().Parse((const char*)(sqlite3_column_text(stmt, 0)));
        if(!results.back().HasMember("name")) {
          results.pop_back();
          respts.pop_back();
        }
        else if(ent || nextPage) {
          if(markerIdx >= searchMarkers.size())
            searchMarkers.push_back(map->markerAdd());
          map->markerSetVisible(searchMarkers[markerIdx], true);
          // 2nd value is priority (smaller number means higher priority)
          std::string namestr = results.back()["name"].GetString();
          std::replace(namestr.begin(), namestr.end(), '"', '\'');
          map->markerSetStylingFromString(searchMarkers[markerIdx],
              fstring(searchMarkerStyleStr, namestr.c_str(), markerIdx+2).c_str());
          map->markerSetPoint(searchMarkers[markerIdx], LngLat(lng, lat));
          ++markerIdx;

          if(markerIdx <= 5 || lngLatDist(mapCenter, LngLat(lng, lat)) < 2.0) {
            minLngLat.longitude = std::min(minLngLat.longitude, lng);
            minLngLat.latitude = std::min(minLngLat.latitude, lat);
            maxLngLat.longitude = std::max(maxLngLat.longitude, lng);
            maxLngLat.latitude = std::max(maxLngLat.latitude, lat);
          }
        }
      });
    }
    if(!searchActive && ent && !results.empty()) {
      map->updateGlobals({SceneUpdate{"global.search_active", "true"}});
      searchActive = true;
    }
    for(; markerIdx < searchMarkers.size(); ++markerIdx)
      map->markerSetVisible(searchMarkers[markerIdx], false);
  }

  std::vector<std::string> sresults;
  for (size_t ii = 0; ii < results.size(); ++ii) {
    double distkm = lngLatDist(mapCenter, respts[ii]);
    sresults.push_back(fstring("%s (%.1f km)", results[ii]["name"].GetString(), distkm));
  }

  std::vector<const char*> cresults;
  for(const auto& s : sresults)
    cresults.push_back(s.c_str());

  int currItem;
  double scrx, scry;
  if(ImGui::ListBox("Results", &currItem, cresults.data(), cresults.size())) {
    // if item selected, show info and place single marker
    pickLabelStr.clear();
    for (auto& m : results[currItem].GetObject())
      pickLabelStr += m.name.GetString() + std::string(" = ") + m.value.GetString() + "\n";
    for(MarkerID mrkid : searchMarkers)
      map->markerSetVisible(mrkid, false);

    if (pickResultMarker == 0)
      pickResultMarker = map->markerAdd();
    map->markerSetVisible(pickResultMarker, true);
    // 2nd value is priority (smaller number means higher priority)
    std::string namestr = results[currItem]["name"].GetString();
    std::replace(namestr.begin(), namestr.end(), '"', '\'');
    map->markerSetStylingFromString(pickResultMarker,
        fstring(searchMarkerStyleStr, namestr.c_str(), 2).c_str());
    map->markerSetPoint(pickResultMarker, respts[currItem]);

    // ensure marker is visible
    double lng = respts[currItem].longitude;
    double lat = respts[currItem].latitude;
    if(!map->lngLatToScreenPosition(lng, lat, &scrx, &scry))
      map->flyTo(CameraPosition{lng, lat, 16}, 1.0);  // max(map->getZoom(), 14)
  }
  else if(minLngLat.longitude != 180) {
    map->markerSetVisible(pickResultMarker, false);
    if(!map->lngLatToScreenPosition(minLngLat.longitude, minLngLat.latitude, &scrx, &scry)
        || !map->lngLatToScreenPosition(maxLngLat.longitude, maxLngLat.latitude, &scrx, &scry)) {
      auto pos = map->getEnclosingCameraPosition(minLngLat, maxLngLat);
      pos.zoom = std::min(pos.zoom, 16.0f);
      map->flyTo(pos, 1.0);
    }
  }
}

// bookmarks (saved places)
// - list of bookmark lists; each list has list of places
// - combo box to pick list, then list of places on list; can select place to delete (multi-select w/ cut copy paste?)
// - add, delete, show/hide options for lists
// - schema: 1 table for all places, w/ list name as column vs. separate table for each list?
//  - separate tables supports assigning place to multiple lists better (otherwise we have to duplicate)
//  - when a place is selected, we'd want to show list membership ... this will be done via osm ID! ... easier w/ single table
// ... let's go w/ single table


void showBookmarkGUI()
{
  if (!ImGui::CollapsingHeader("Saved Places"))
    return;
  if (ImGui::Button("Save")) {

  }
}


// GPX tracks

struct TrackPt {
  LngLat pos;
  double dist;
  double elev;
};

std::vector<TrackPt> activeTrack;

// https://www.topografix.com/gpx_manual.asp
void addGPXPolyline(const char* gpxfile)
{
  using namespace rapidxml;  // https://rapidxml.sourceforge.net/manual.html
  file<> xmlFile(gpxfile); // Default template is char
  xml_document<> doc;
  doc.parse<0>(xmlFile.data());
  xml_node<>* trk = doc.first_node("gpx")->first_node("trk");
  if(!trk) logMsg("Error loading %s\n", gpxfile);
  activeTrack.clear();
  while(trk) {
    xml_node<>* trkseg = trk->first_node("trkseg");
    while(trkseg) {
      std::vector<LngLat> track;
      xml_node<>* trkpt = trkseg->first_node("trkpt");
      while(trkpt) {
        xml_attribute<>* lat = trkpt->first_attribute("lat");
        xml_attribute<>* lon = trkpt->first_attribute("lon");
        track.emplace_back(atof(lon->value()), atof(lat->value()));

        xml_node<>* ele = trkpt->first_node("ele");
        double dist = activeTrack.empty() ? 0 : activeTrack.back().dist + lngLatDist(activeTrack.back().pos, track.back());
        activeTrack.push_back({track.back(), dist, atof(ele->value())});

        trkpt = trkpt->next_sibling("trkpt");
      }
      if(!track.empty()) {
        MarkerID marker = map->markerAdd();
        map->markerSetStylingFromString(marker, polylineStyle.c_str());
        map->markerSetPolyline(marker, track.data(), track.size());
        trackMarkers.push_back(marker);
      }
      trkseg = trkseg->next_sibling("trkseg");
    }
    trk = trk->next_sibling("trk");
  }
}

void showSceneGUI() {
    if (ImGui::CollapsingHeader("Scene")) {
        if (ImGui::InputText("Scene URL", &sceneFile, ImGuiInputTextFlags_EnterReturnsTrue)) {
            loadSceneFile();
        }
        if (ImGui::InputText("API key", &apiKey, ImGuiInputTextFlags_EnterReturnsTrue)) {
            loadSceneFile(false, {SceneUpdate{apiKeyScenePath, apiKey}});
        }
        if (ImGui::Button("Reload Scene")) {
            loadSceneFile();
        }
    }
}

void showMarkerGUI() {
    if (ImGui::CollapsingHeader("Markers")) {
        ImGui::Checkbox("Add point markers on click", &add_point_marker_on_click);
        if (ImGui::RadioButton("Use Styling Path", markerUseStylingPath)) { markerUseStylingPath = true; }
        if (markerUseStylingPath) {
            ImGui::InputText("Path", &markerStylingPath);
        }
        if (ImGui::RadioButton("Use Styling String", !markerUseStylingPath)) { markerUseStylingPath = false; }
        if (!markerUseStylingPath) {
            ImGui::InputTextMultiline("String", &markerStylingString);
        }
        if (ImGui::Button("Clear point markers")) {
            for (const auto marker : point_markers) {
                map->markerRemove(marker.markerId);
            }
            point_markers.clear();
        }

        ImGui::Checkbox("Add polyline marker points on click", &add_polyline_marker_on_click);
        if (ImGui::Button("Clear polyline marker")) {
            if (!polyline_marker_coordinates.empty()) {
                map->markerRemove(polyline_marker);
                polyline_marker_coordinates.clear();
            }
        }

        ImGui::Checkbox("Point markers use clipped position", &point_markers_position_clipped);
        if (point_markers_position_clipped) {
            // Move all point markers to "clipped" positions.
            for (const auto& marker : point_markers) {
                double screenClipped[2];
                map->lngLatToScreenPosition(marker.coordinates.longitude, marker.coordinates.latitude, &screenClipped[0], &screenClipped[1], true);
                LngLat lngLatClipped;
                map->screenPositionToLngLat(screenClipped[0], screenClipped[1], &lngLatClipped.longitude, &lngLatClipped.latitude);
                map->markerSetPoint(marker.markerId, lngLatClipped);
            }

            // Display coordinates for last marker.
            if (!point_markers.empty()) {
                auto& last_marker = point_markers.back();
                double screenPosition[2];
                map->lngLatToScreenPosition(last_marker.coordinates.longitude, last_marker.coordinates.latitude, &screenPosition[0], &screenPosition[1]);
                float screenPositionFloat[2] = {static_cast<float>(screenPosition[0]), static_cast<float>(screenPosition[1])};
                ImGui::InputFloat2("Last Marker Screen", screenPositionFloat, 5, ImGuiInputTextFlags_ReadOnly);
                double screenClipped[2];
                map->lngLatToScreenPosition(last_marker.coordinates.longitude, last_marker.coordinates.latitude, &screenClipped[0], &screenClipped[1], true);
                float screenClippedFloat[2] = {static_cast<float>(screenClipped[0]), static_cast<float>(screenClipped[1])};
                ImGui::InputFloat2("Last Marker Clipped", screenClippedFloat, 5, ImGuiInputTextFlags_ReadOnly);
            }
        }

        ImGui::InputText("GPX File", &gpxFile);
        if (ImGui::Button("Add")) {
          addGPXPolyline(gpxFile.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace")) {
          for (auto marker : trackMarkers)
            map->markerRemove(marker);
          addGPXPolyline(gpxFile.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All")) {
          activeTrack.clear();
          for (auto marker : trackMarkers)
            map->markerRemove(marker);
        }

        if(!activeTrack.empty()) {
          size_t N = 200;
          double dd = activeTrack.back().dist/N;
          double d = dd/2;
          std::vector<float> plot;
          plot.reserve(N);
          for(size_t ii = 0, jj = 0; ii < N; ++ii, d += dd) {
            while(activeTrack[jj].dist < d) ++jj;
            double f = (d - activeTrack[jj-1].dist)/(activeTrack[jj].dist - activeTrack[jj-1].dist);
            plot.push_back( f*activeTrack[jj].elev + (1-f)*activeTrack[jj-1].elev );
          }
          ImGui::TextUnformatted("Track elevation");
          ImGui::PlotLines("", plot.data(), plot.size(), 0, NULL, FLT_MAX, FLT_MAX, {0, 250});

          if(ImGui::IsItemHovered()) {
            double s = (ImGui::GetMousePos().x - ImGui::GetItemRectMin().x)/ImGui::GetItemRectSize().x;
            if(s > 0 && s < 1) {
              double sd = s*activeTrack.back().dist;

              size_t jj = 0;
              while(activeTrack[jj].dist < sd) ++jj;
              double f = (sd - activeTrack[jj-1].dist)/(activeTrack[jj].dist - activeTrack[jj-1].dist);
              double lat = f*activeTrack[jj].pos.latitude + (1-f)*activeTrack[jj-1].pos.latitude;
              double lon = f*activeTrack[jj].pos.longitude + (1-f)*activeTrack[jj-1].pos.longitude;
              if(trackHoverMarker == 0) {
                trackHoverMarker = map->markerAdd();
                //map->markerSetStylingFromPath(trackHoverMarker, markerStylingPath.c_str());
                map->markerSetStylingFromString(trackHoverMarker, markerStylingString.c_str());
              }
              map->markerSetVisible(trackHoverMarker, true);
              map->markerSetPoint(trackHoverMarker, LngLat(lon, lat));
              return;
            }
          }
        }
        if(trackHoverMarker > 0)
          map->markerSetVisible(trackHoverMarker, false);
    }
}

void showViewportGUI() {
    if (ImGui::CollapsingHeader("Viewport")) {
        CameraPosition camera = map->getCameraPosition();
        float lngLatZoom[3] = {static_cast<float>(camera.longitude), static_cast<float>(camera.latitude), camera.zoom};
        if (ImGui::InputFloat3("Lng/Lat/Zoom", lngLatZoom, "%.5f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            camera.longitude = lngLatZoom[0];
            camera.latitude = lngLatZoom[1];
            camera.zoom = lngLatZoom[2];
            map->setCameraPosition(camera);
        }
        if (ImGui::SliderAngle("Tilt", &camera.tilt, 0.f, 90.f)) {
            map->setCameraPosition(camera);
        }
        if (ImGui::SliderAngle("Rotation", &camera.rotation, 0.f, 360.f)) {
            map->setCameraPosition(camera);
        }
        EdgePadding padding = map->getPadding();
        if (ImGui::InputInt4("Left/Top/Right/Bottom", &padding.left)) {
            map->setPadding(padding);
        }
    }
}

void showDebugFlagsGUI() {
    if (ImGui::CollapsingHeader("Debug Flags")) {
        bool flag;
        flag = getDebugFlag(DebugFlags::freeze_tiles);
        if (ImGui::Checkbox("Freeze Tiles", &flag)) {
            setDebugFlag(DebugFlags::freeze_tiles, flag);
        }
        flag = getDebugFlag(DebugFlags::proxy_colors);
        if (ImGui::Checkbox("Recolor Proxy Tiles", &flag)) {
            setDebugFlag(DebugFlags::proxy_colors, flag);
        }
        flag = getDebugFlag(DebugFlags::tile_bounds);
        if (ImGui::Checkbox("Show Tile Bounds", &flag)) {
            setDebugFlag(DebugFlags::tile_bounds, flag);
        }
        flag = getDebugFlag(DebugFlags::tile_infos);
        if (ImGui::Checkbox("Show Tile Info", &flag)) {
            setDebugFlag(DebugFlags::tile_infos, flag);
        }
        flag = getDebugFlag(DebugFlags::labels);
        if (ImGui::Checkbox("Show Label Debug Info", &flag)) {
            setDebugFlag(DebugFlags::labels, flag);
        }
        flag = getDebugFlag(DebugFlags::tangram_infos);
        if (ImGui::Checkbox("Show Map Info", &flag)) {
            setDebugFlag(DebugFlags::tangram_infos, flag);
        }
        flag = getDebugFlag(DebugFlags::draw_all_labels);
        if (ImGui::Checkbox("Show All Labels", &flag)) {
            setDebugFlag(DebugFlags::draw_all_labels, flag);
        }
        flag = getDebugFlag(DebugFlags::tangram_stats);
        if (ImGui::Checkbox("Show Frame Stats", &flag)) {
            setDebugFlag(DebugFlags::tangram_stats, flag);
        }
        flag = getDebugFlag(DebugFlags::selection_buffer);
        if (ImGui::Checkbox("Show Selection Buffer", &flag)) {
            setDebugFlag(DebugFlags::selection_buffer, flag);
        }
        ImGui::Checkbox("Wireframe Mode", &wireframe_mode);
    }
}

void showSceneVarsGUI() {
    if (ImGui::CollapsingHeader("Scene Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
        for(int ii = 0; ii < 100; ++ii) {
            std::string name = map->readSceneValue(fstring("global.gui_variables#%d.name", ii));
            if(name.empty()) break;
            std::string label = map->readSceneValue(fstring("global.gui_variables#%d.label", ii));
            std::string reload = map->readSceneValue(fstring("global.gui_variables#%d.reload", ii));
            std::string value = map->readSceneValue("global." + name);
            bool flag = value == "true";
            if (ImGui::Checkbox(label.c_str(), &flag)) {
                // we expect only one checkbox to change per frame, so this is OK
                if(reload == "false")  // ... so default to reloading
                    map->updateGlobals({SceneUpdate{"global." + name, flag ? "true" : "false"}});
                else
                    loadSceneFile(false, {SceneUpdate{"global." + name, flag ? "true" : "false"}});
            }
        }
    }
}

void showPickLabelGUI() {
    if (ImGui::CollapsingHeader("Picked Object", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted(pickLabelStr.c_str());
    }
}

} // namespace GlfwApp

} // namespace Tangram
