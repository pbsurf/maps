#include "mapsources.h"
#include "mapsapp.h"
#include "util.h"
#include "imgui.h"
#include "imgui_stl.h"


// Source selection

class SourceBuilder
{
public:
  const YAML::Node& sources;
  std::vector<std::string> imports;
  std::vector<SceneUpdate> updates;
  int order = 0;

  std::vector<std::string> layerkeys;

  SourceBuilder(const YAML::Node& s) : sources(s) {}

  void addLayer(const std::string& key);
  std::string getSceneYaml(const std::string& baseUrl);
};

void SourceBuilder::addLayer(const std::string& key)  //, const YAML::Node& src)
{
  YAML::Node src = sources[key];
  if(src["type"].Scalar() == "Multi") {
    for (const auto& layer : src["layers"]) {
      std::string layerkey = layer["source"].Scalar();
      addLayer(layerkey);  //, sources[layerkey]);
    }
  }
  else if(src["type"].Scalar() == "Raster") {
    layerkeys.push_back(key);
    std::string rasterN = fstring("raster-%d", order);
    for (const auto& attr : src) {
      if(attr.first.Scalar() != "title")
        updates.emplace_back("+sources." + rasterN + "." + attr.first.Scalar(), yamlToStr(attr.second));
    }
    // if cache file is not explicitly specified, use key since it is guaranteed to be unique
    if(!src["cache"] || src["cache"].Scalar() != "false")
      updates.emplace_back("+sources." + rasterN + ".cache", key);
    // separate style is required for each overlay layer; overlay layers are always drawn over opaque layers
    //  text and points are drawn as overlays w/ blend_order -1, so use blend_order < -1 to place rasters
    //  under vector map text
    if(order > 0)
      updates.emplace_back("+styles." + rasterN, fstring("{base: raster, blend: overlay, blend_order: %d}", order-10));
    updates.emplace_back("+layers." + rasterN + ".data.source", rasterN);
    // order is ignored (and may not be required) for raster styles
    updates.emplace_back("+layers." + rasterN + ".draw.group-0.style", order > 0 ? rasterN : "raster");
    updates.emplace_back("+layers." + rasterN + ".draw.group-0.order", std::to_string(order++));
  }
  else if(src["type"].Scalar() == "Vector") {  // vector map
    imports.push_back(src["url"].Scalar());
    layerkeys.push_back(key);
    ++order;  //order = 9001;  // subsequent rasters should be drawn on top of the vector map
  }
  else if(src["type"].Scalar() == "Update") {
    layerkeys.push_back(key);
  }
  else {
    LOGE("Invalid map source type %s for %s", src["type"].Scalar().c_str(), key.c_str());
    return;
  }

  for(const auto& update : src["updates"]) {
    updates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
  }
}

std::string SourceBuilder::getSceneYaml(const std::string& baseUrl)
{
  // main.cpp prepends file://<cwd>/ to sceneFile!
  // we'll probably want to skip curl for reading from filesystem in scene/importer.cpp - see tests/src/mockPlatform.cpp
  // or maybe add a Url getParent() method to Url class
  if(imports.empty())
    return "global:\n\nsources:\n\nlayers:\n";

  std::string importstr;
  for(auto& url : imports)
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  return "import:\n" + importstr;  //+ "\nglobal:\n\nsources:\n\nstyles:\n\nlayers:\n";
}

// auto it = mapSources.begin();  std::advance(it, currSrcIdx[ii]-1); builder.addLayer(it->first.Scalar(), it->second);

MapsSources::MapsSources(MapsApp* _app, const std::string& sourcesFile) : MapsComponent(_app)
{
  // have to use Url request to access assets on Android
  auto cb = [&, sourcesFile](UrlResponse&& response) {
    if(response.error)
      LOGE("Unable to load '%s': %s", sourcesFile.c_str(), response.error);
    else {
      try {
        std::lock_guard<std::mutex> lock(sourcesMutex);
        YAML::Node oldsources = Clone(mapSources);
        mapSources = YAML::Load(response.content.data(), response.content.size());
        for(auto& node : oldsources)
          mapSources[node.first.Scalar()] = node.second;
        sourcesLoaded = true;
      } catch (std::exception& e) {
        LOGE("Error parsing '%s': %s", sourcesFile.c_str(), e.what());
      }
    }
  };

  app->platform->startUrlRequest(Url(sourcesFile), cb);

  std::size_t sep = sourcesFile.find_last_of("/\\");
  if(sep != std::string::npos)
    baseUrl = sourcesFile.substr(0, sep+1);  //"file://" +
}

void MapsSources::addSource(const std::string& key, YAML::Node srcnode)
{
  std::lock_guard<std::mutex> lock(sourcesMutex);
  mapSources[key] = srcnode;
  //for(auto& k : layerkeys) -- TODO: if modified layer is in use, reload
}

void MapsSources::showGUI()
{
  static constexpr int MAX_SOURCES = 8;
  static int currIdx = 0;
  static std::vector<int> currSrcIdx(MAX_SOURCES, 0);
  static int nSources = 1;
  static std::string newSrcTitle;

  if (!ImGui::CollapsingHeader("Sources", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  if(!sourcesLoaded) {
    ImGui::TextWrapped("Loading mapsources.yaml...");
    return;
  }

  try {

  std::vector<std::string> titles = {"None"};
  std::vector<std::string> keys = {""};
  for (const auto& src : mapSources) {
    titles.push_back(src.second["title"].Scalar());
    keys.push_back(src.first.Scalar());
  }

  std::vector<const char*> ctitles;
  for(const auto& s : titles)
    ctitles.push_back(s.c_str());

  int reload = 0;
  if(ImGui::Combo("Source", &currIdx, ctitles.data(), ctitles.size()))
    reload = 1;  // selected source changed - reload scene

  if(currIdx > 0 && mapSources[keys[currIdx]]["type"].Scalar() == "Multi") {
    ImGui::SameLine();
    if (ImGui::Button("Remove"))
      mapSources.remove(keys[currIdx]);
  }
  ImGui::Separator();
  for(int ii = 0; ii < nSources; ++ii) {
    if(ImGui::Combo(fstring("Layer %d", ii+1).c_str(), &currSrcIdx[ii], ctitles.data(), ctitles.size()))
      reload = 2;  // layer changed - reload scene
  }

  if (nSources > 1) {
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
      --nSources;
      if(currSrcIdx[nSources] > 0)
        reload = 2;
    }
  }
  if (nSources < MAX_SOURCES && ImGui::Button("Add Layer")) {
    currSrcIdx[nSources] = 0;
    ++nSources;
  }

  if(reload) {
    SourceBuilder builder(mapSources);
    if(reload == 1)
      builder.addLayer(keys[currIdx]);
    else {
      for(int ii = 0; ii < nSources; ++ii) {
        if(currSrcIdx[ii] > 0)
          builder.addLayer(keys[currSrcIdx[ii]]);
      }
    }

    if(!builder.imports.empty() || !builder.updates.empty()) {
      app->sceneYaml = builder.getSceneYaml(baseUrl);
      app->sceneFile = baseUrl + "__GUI_SOURCES__";
      app->loadSceneFile(false, builder.updates);
    }

    if(reload == 1 && builder.layerkeys.size() > 1)
      newSrcTitle = titles[currIdx];

    nSources = std::max(int(builder.layerkeys.size()), nSources);
    for(size_t ii = 0; ii < builder.layerkeys.size(); ++ii) {
      for(int jj = 0; jj < keys.size(); ++jj) {
        if(builder.layerkeys[ii] == keys[jj]) {
          currSrcIdx[ii] = jj;
          break;  // next layer
        }
      }
    }
    for(int ii = builder.layerkeys.size(); ii < nSources; ++ii)
      currSrcIdx[ii] = 0;
  }

  if(nSources > 1) {
    ImGui::InputText("Name", &newSrcTitle, ImGuiInputTextFlags_EnterReturnsTrue);
    //ent = ImGui::Button("Save") || ent;
    if(ImGui::Button("Save") && !newSrcTitle.empty()) {
      std::stringstream fs;  //fs(sourcesFile, std::fstream::app | std::fstream::binary);

      // find available name
      std::string savekey;
      if(currIdx > 0 && newSrcTitle == titles[currIdx] && mapSources[keys[currIdx]]["type"].Scalar() == "Multi")
        savekey = keys[currIdx];
      else {
        int ii = mapSources.size();
        while(ii < INT_MAX && mapSources[fstring("custom-%d", ii)]) ++ii;
        savekey = fstring("custom-%d", ii);
        currIdx = keys.size();  // new source will be added at end of list
      }
      //YAML::Node node = mapSources[savekey] = YAML::Node(YAML::NodeType::Map);  node["type"] = "Multi";
      fs << "type: Multi\n";
      fs << "title: " << newSrcTitle << "\n";
      fs << "layers:\n";
      for(int ii = 0; ii < nSources; ++ii) {
        if(currSrcIdx[ii] > 0)
          fs << "  - source: " << keys[currSrcIdx[ii]] << "\n";
      }
      newSrcTitle.clear();
      mapSources[savekey] = YAML::Load(fs.str());
      // we'd set a flag here to save mapsources.yaml on exit
    }
  }

  } catch (std::exception& e) {
    ImGui::TextWrapped("Error parsing mapsources.yaml: %s", e.what());
  }
}
