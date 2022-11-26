#include "glfwApp.h"
#include "mapsapp.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#ifdef TANGRAM_WINDOWS
#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#endif // TANGRAM_WINDOWS

#define GLFW_INCLUDE_GLEXT
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include <cstdlib>
//#include "log.h"
//#include "map.h"
//#include <memory>
#include <limits.h>
//#include <stdlib.h>
#include <unistd.h>

#ifndef BUILD_NUM_STRING
#define BUILD_NUM_STRING ""
#endif

namespace Tangram {

namespace GlfwApp {

GLFWwindow* main_window = nullptr;
MapsApp* app = nullptr;

//void setScene(const std::string& _path, const std::string& _yaml) { sceneFile = _path; sceneYaml = _yaml; }

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
  double x, y;
  glfwGetCursorPos(window, &x, &y);
  double time = glfwGetTime();
  ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
  if (!ImGui::GetIO().WantCaptureMouse)
      app->onMouseButton(time, x, y, button, action, mods);
  else
      app->map->getPlatform().requestRender();  // necessary for proper update of combo boxes, etc
}

void cursorMoveCallback(GLFWwindow* window, double x, double y)
{
  int action = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1);
  double time = glfwGetTime();
  if (!ImGui::GetIO().WantCaptureMouse)
      app->onMouseMove(time, x, y, action == GLFW_PRESS);
}

void scrollCallback(GLFWwindow* window, double scrollx, double scrolly)
{
  double mouse_x, mouse_y;
  bool rotating = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
  bool shoving = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
  ImGui_ImplGlfw_ScrollCallback(window, scrollx, scrolly);
  glfwGetCursorPos(window, &mouse_x, &mouse_y);
  if (!ImGui::GetIO().WantCaptureMouse)
      app->onMouseWheel(mouse_x, mouse_y, scrollx, scrolly, rotating, shoving);
}


void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {

    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
    if (ImGui::GetIO().WantCaptureKeyboard) {
        return; // Imgui is handling this event.
    }
    Map* map = app->map;

    CameraPosition camera = map->getCameraPosition();

    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_A:
                app->load_async = !app->load_async;
                LOG("Toggle async load: %d", app->load_async);
                break;
            case GLFW_KEY_D:
                app->show_gui = !app->show_gui;
                break;
            case GLFW_KEY_BACKSPACE:
                app->recreate_context = true;
                break;
            case GLFW_KEY_R:
                app->loadSceneFile();
                break;
            case GLFW_KEY_EQUAL:
            case GLFW_KEY_MINUS:
                if(mods != GLFW_MOD_CONTROL)
                    break;
                [[fallthrough]];
            case GLFW_KEY_Z: {
                auto pos = map->getCameraPosition();
                pos.zoom += key == GLFW_KEY_MINUS ? -1.f : 1.f;
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
                if (app->pixel_scale == 1.0) {
                    app->pixel_scale = 2.0;
                } else if (app->pixel_scale == 2.0) {
                    app->pixel_scale = 0.75;
                } else {
                    app->pixel_scale = 1.0;
                }
                map->setPixelScale(app->pixel_scale*app->density);
                break;
            case GLFW_KEY_P:
                app->loadSceneFile(false, {SceneUpdate{"cameras", "{ main_camera: { type: perspective } }"}});
                break;
            case GLFW_KEY_I:
                app->loadSceneFile(false, {SceneUpdate{"cameras", "{ main_camera: { type: isometric } }"}});
                break;
            case GLFW_KEY_M:
                map->loadSceneYamlAsync("{ scene: { background: { color: red } } }", std::string(""));
                break;
            case GLFW_KEY_G:
                {
                    static bool geoJSON = false;
                    if (!geoJSON) {
                        app->loadSceneFile(false,
                                      { SceneUpdate{"sources.osm.type", "GeoJSON"},
                                        SceneUpdate{"sources.osm.url", "https://tile.mapzen.com/mapzen/vector/v1/all/{z}/{x}/{y}.json"}});
                    } else {
                        app->loadSceneFile(false,
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
            case GLFW_KEY_O:
                if((map->getPlatform().isOffline = !map->getPlatform().isOffline))
                  LOGW("Network access disabled!");
                else
                  LOGW("Network access enabled");
                break;
            default:
                break;
        }
    }
}

void dropCallback(GLFWwindow* window, int count, const char** paths)
{
    app->sceneFile = "file://" + std::string(paths[0]);
    app->sceneYaml.clear();
    app->loadSceneFile();
}

void framebufferResizeCallback(GLFWwindow* window, int fWidth, int fHeight)
{
    int wWidth = 0, wHeight = 0;
    glfwGetWindowSize(main_window, &wWidth, &wHeight);
    app->onResize(wWidth, wHeight, fWidth, fHeight);
}

void parseArgs(int argc, char* argv[]) {
    // Load file from command line, if given.
    int argi = 0;
    while (++argi < argc) {
        if (strcmp(argv[argi - 1], "-f") == 0) {
            app->sceneFile = std::string(argv[argi]);
            LOG("File from command line: %s\n", argv[argi]);
            break;
        }
        if (strcmp(argv[argi - 1], "-s") == 0) {

            if (argi+1 < argc) {
                app->sceneYaml = std::string(argv[argi]);
                app->sceneFile = std::string(argv[argi+1]);
                LOG("Yaml from command line: %s, resource path: %s\n",
                    app->sceneYaml.c_str(), app->sceneFile.c_str());
            } else {
                LOG("-s options requires YAML string and resource path");
                exit(1);
            }
            break;
        }
    }
}

void create(std::unique_ptr<Platform> p, int argc, char* argv[])
{
    if (!glfwInit()) {
        assert(false);
        return;
    }

    std::string apiKey;
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

    // Build a version string for the window title.
    char versionString[256] = { 0 };
    std::snprintf(versionString, sizeof(versionString), "Tangram ES %d.%d.%d " BUILD_NUM_STRING,
        TANGRAM_VERSION_MAJOR, TANGRAM_VERSION_MINOR, TANGRAM_VERSION_PATCH);

    // Create a windowed mode window and its OpenGL context
    glfwWindowHint(GLFW_SAMPLES, 2);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    if (!main_window) {
        main_window = glfwCreateWindow(1024, 768, versionString, NULL, NULL);
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

    // Setup ImGui
    const char* glsl_version = "#version 120";
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplOpenGL3_Init(glsl_version);
    ImGui_ImplGlfw_InitForOpenGL(main_window, false);

    MapsApp::baseDir = "/home/mwhite/maps/";
    MapsApp::apiKey = apiKey;
    app = new MapsApp(std::move(p));

    int fWidth = 0, fHeight = 0;
    glfwGetFramebufferSize(main_window, &fWidth, &fHeight);
    framebufferResizeCallback(main_window, fWidth, fHeight);

    // process args
    app->sceneFile = "scenes/scene.yaml";
    parseArgs(argc, argv);

    // Resolve the input path against the current directory.
    Url baseUrl("file:///");
    char pathBuffer[PATH_MAX] = {0};
    if (getcwd(pathBuffer, PATH_MAX) != nullptr) {
        baseUrl = baseUrl.resolve(Url(std::string(pathBuffer) + "/"));
    }
    LOG("Base URL: %s", baseUrl.string().c_str());
    Url sceneUrl = baseUrl.resolve(Url(app->sceneFile));
    app->sceneFile = sceneUrl.string();
}

void run()
{
    app->loadSceneFile(false);
    //double lastTime = glfwGetTime();
    // Loop until the user closes the window
    while (!glfwWindowShouldClose(main_window)) {

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        const bool wireframe = app->wireframe_mode;
        if(wireframe) {
          glPolygonMode(GL_FRONT, GL_LINE);
          glPolygonMode(GL_BACK, GL_LINE);
        }

        //bool focused = glfwGetWindowAttrib(main_window, GLFW_FOCUSED) != 0;
        //int w, h, display_w, display_h;
        //double current_time = glfwGetTime();
        //glfwGetWindowSize(main_window, &w, &h);
        //glfwGetFramebufferSize(main_window, &display_w, &display_h);
        double time = glfwGetTime();
        app->drawFrame(time);

        if(wireframe) {
          glPolygonMode(GL_FRONT, GL_FILL);
          glPolygonMode(GL_BACK, GL_FILL);
        }

        if(app->show_gui)
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Swap front and back buffers
        glfwSwapBuffers(main_window);

        // Poll for and process events
        if (app->needsRender()) {
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
    if (app) {
        delete app;
        app = nullptr;
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    if (main_window) {
        glfwDestroyWindow(main_window);
        main_window = nullptr;
    }
    glfwTerminate();
}

} // namespace GlfwApp

} // namespace Tangram
