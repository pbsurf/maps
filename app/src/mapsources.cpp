#include "mapsources.h"
#include "mapsapp.h"
#include "util.h"
#include "scene/scene.h"
#include "style/style.h"  // for making uniforms avail as GUI variables
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "mapwidgets.h"

#include "offlinemaps.h"

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
  if(!src) {
    LOGE("Invalid map source %s", key.c_str());
    return;
  }
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
  // we'll probably want to skip curl for reading from filesystem in scene/importer.cpp - see tests/src/mockPlatform.cpp
  // or maybe add a Url getParent() method to Url class
  std::string importstr;
  for(auto& url : imports)
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  for(auto& imp : MapsApp::config["common_imports"]) {
    std::string url = imp.Scalar();
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  }
  if(importstr.empty())
    return "global:\n\nsources:\n\nlayers:\n";
  return "import:\n" + importstr;  //+ "\nglobal:\n\nsources:\n\nstyles:\n\nlayers:\n";
}

// auto it = mapSources.begin();  std::advance(it, currSrcIdx[ii]-1); builder.addLayer(it->first.Scalar(), it->second);

MapsSources::MapsSources(MapsApp* _app) : MapsComponent(_app)  // const std::string& sourcesFile
{
  FSPath path = FSPath(app->configFile).parent();
  baseUrl = "file://" + path.path;

  FSPath srcfile = path.child(app->config["sources"].as<std::string>("mapsources.yaml"));
  try {
    mapSources = YAML::LoadFile(srcfile.c_str());
  } catch (...) {
    try {
      mapSources = YAML::LoadFile(srcfile.parent().childPath(srcfile.baseName() + ".default.yaml"));
    } catch (...) {
      LOGE("Unable to load map sources!");
      return;
    }
  }
  srcFile = srcfile.c_str();
}

// don't run this during offline map download!
int64_t MapsSources::shrinkCache(int64_t maxbytes)
{
  std::vector< std::unique_ptr<Tangram::MBTilesDataSource> > dbsources;
  std::vector< std::pair<int, int> > tiles;
  auto insertTile = [&](int timestamp, int size){ tiles.emplace_back(timestamp, size); };

  FSPath cachedir(app->baseDir, "cache");
  for(auto& file : lsDirectory(cachedir)) {
    FSPath cachefile = cachedir.child(file);
    if(cachefile.extension() != "mbtiles") continue;
    dbsources.push_back(std::make_unique<Tangram::MBTilesDataSource>(
        *app->platform, cachefile.baseName(), cachefile.path, "", true));
    dbsources.back()->getTileSizes(insertTile);
  }

  std::sort(tiles.rbegin(), tiles.rend());  // sort by timestamp, descending (newest to oldest)
  int64_t tot = 0;
  for(auto& x : tiles) {
    tot += x.second;
    if(tot > maxbytes) {
      for(auto& src : dbsources)
        src->deleteOldTiles(x.first);
      break;
    }
  }
  return tot;
}

void MapsSources::addSource(const std::string& key, YAML::Node srcnode)
{
  mapSources[key] = srcnode;
  mapSources[key]["__plugin"] = true;
  //for(auto& k : layerkeys) -- TODO: if modified layer is in use, reload
}

void MapsSources::saveSources()
{
  if(srcFile.empty()) return;
  YAML::Node sources = YAML::Node(YAML::NodeType::Map);
  for(auto& node : mapSources) {
    if(!node.second["__plugin"])
      sources[node.first] = node.second;
  }

  YAML::Emitter emitter;
  //emitter.SetStringFormat(YAML::DoubleQuoted);
  emitter << sources;
  FileStream fs(srcFile.c_str(), "wb");
  fs.write(emitter.c_str(), emitter.size());
}

void MapsSources::sourceModified()
{
  saveBtn->setEnabled(!titleEdit->text().empty());
}

// New GUI

constexpr int MAX_SOURCES = 8;

void MapsSources::rebuildSource(const std::string& srcname)
{
  SourceBuilder builder(mapSources);
  // support comma separated list of sources
  if(!srcname.empty()) {
    currLayers.clear();
    currUpdates.clear();
    auto splitsrc = splitStr<std::vector>(srcname, ",");
    if(splitsrc.size() > 1) {
      for(auto& src : splitsrc)
        currLayers.push_back(src);  //builder.addLayer(src);
    }
    else {
      auto src = mapSources[srcname];
      if(!src) return;
      if(src["type"].Scalar() == "Multi") {
        for(const auto& layer : src["layers"])
          currLayers.push_back(layer["source"].Scalar());
        for(const auto& update : src["updates"])
          currUpdates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
      }
      else
        currLayers.push_back(srcname);
    }
  }
  //else {
  //  for(size_t ii = 0; ii < layerCombos.size(); ++ii) {
  //    int idx = layerCombos[ii]->index();
  //    if(idx > 0)
  //      builder.addLayer(layerKeys[idx]);
  //  }
  //}

  builder.updates = currUpdates;
  for(auto& src : currLayers)
    builder.addLayer(src);

  if(!builder.imports.empty() || !builder.updates.empty()) {
    // we need this to be persistent for scene reloading (e.g., on scene variable change)
    app->sceneYaml = builder.getSceneYaml(baseUrl);
    app->sceneFile = baseUrl + "__GUI_SOURCES__";
    app->sceneUpdates = std::move(builder.updates);  //.clear();
    app->loadSceneFile();  //builder.getSceneYaml(baseUrl), builder.updates);
    sceneVarsLoaded = false;
    currSource = srcname;
    if(!srcname.empty()) {
      app->config["last_source"] = currSource;
      for(Widget* item : sourcesContent->select(".listitem"))
        static_cast<Button*>(item)->setChecked(item->node->getStringAttr("__sourcekey", "") == currSource);
    }
  }

  saveBtn->setEnabled(srcname.empty());  // for existing source, don't enable saveBtn until edited

  //bool multi = srcname.empty() || mapSources[srcname]["type"].Scalar() == "Multi";
  //size_t ii = 0;
  //for(; ii < builder.layerkeys.size(); ++ii) {
  //  for(size_t jj = 0; jj < layerKeys.size(); ++jj) {
  //    if(builder.layerkeys[ii] == layerKeys[jj]) {
  //      //currSrcIdx[ii] = jj;
  //      layerCombos[ii]->setIndex(jj);
  //      layerRows[ii]->setVisible(multi);
  //      break;  // next layer
  //    }
  //  }
  //}
  //
  //layerCombos[ii]->setIndex(0);
  //layerRows[ii]->setVisible(multi);
  //for(++ii; ii < layerRows.size(); ++ii) {
  //  layerCombos[ii]->setIndex(0);
  //  layerRows[ii]->setVisible(false);
  //}
}

std::string MapsSources::createSource(std::string savekey, const std::string& yamlStr)
{
  if(savekey.empty() || !mapSources[savekey]) {
    // find available name
    int ii = mapSources.size();
    while(ii < INT_MAX && mapSources[fstring("custom-%d", ii)]) ++ii;
    savekey = fstring("custom-%d", ii);

    mapSources[savekey] = YAML::Node(YAML::NodeType::Map);
    mapSources[savekey]["type"] = "Multi";
  }

  if(!yamlStr.empty()) {
    try {
      mapSources[savekey] = YAML::Load(yamlStr);
    } catch (...) {
      return "";
    }
  }
  else {
    YAML::Node node = mapSources[savekey];
    node["title"] = titleEdit->text();
    if(node["type"].Scalar() == "Multi") {
      YAML::Node layers = node["layers"] = YAML::Node(YAML::NodeType::Sequence);
      for(auto& src : currLayers)
        layers.push_back(YAML::Load("{source: " + src + "}"));
      //for(size_t ii = 0; ii < layerCombos.size(); ++ii) {
      //  int idx = layerCombos[ii]->index();
      //  if(idx > 0)
      //    layers.push_back(YAML::Load("{source: " + src + "}"));
      //}
    }
    YAML::Node updates = node["updates"] = YAML::Node(YAML::NodeType::Map);
    for(const SceneUpdate& upd : app->sceneUpdates) {
      // we only want updates from explicit scene var changes
      if(upd.path[0] != '+')
        updates[upd.path] = upd.value;
    }
  }

  saveSources();
  populateSources();
  rebuildSource(savekey);  // populateSources() resets the layer select boxes, need to restore!
  return savekey;
}

void MapsSources::populateSources()
{
  //app->gui->deleteContents(sourcesContent, ".listitem");
  sourcesDirty = false;
  sourcesContent->clear();

  std::vector<std::string> layerTitles = {"None"};
  std::vector<std::string> sourceTitles = {};
  layerKeys = {""};  // used for layerCombos
  sourceKeys = {};  // currently only used for quick menu
  for(const auto& src : mapSources) {
    std::string key = src.first.Scalar();
    bool isLayer = src.second["layer"].as<bool>(false);
    if(!isLayer && src.second["type"].Scalar() != "Update") {
      sourceKeys.push_back(key);
      sourceTitles.push_back(src.second["title"].Scalar());
    }
    if(src.second["type"].Scalar() != "Multi") {
      layerKeys.push_back(key);
      layerTitles.push_back(src.second["title"].Scalar());
    }

    Button* item = createListItem(MapsApp::uiIcon("layers"), src.second["title"].Scalar().c_str());
    item->node->setAttr("__sourcekey", key.c_str());
    Widget* container = item->selectFirst(".child-container");

    Button* editBtn = createToolbutton(MapsApp::uiIcon("edit"), "Show");

    if(isLayer) {
      Button* showBtn = createToolbutton(MapsApp::uiIcon("eye"), "Show");
      showBtn->onClicked = [=](){
        if(key == currSource) return;
        bool show = !showBtn->isChecked();
        showBtn->setChecked(show);
        if(show)
          currLayers.push_back(key);
        else
          std::remove(currLayers.begin(), currLayers.end(), key);
        rebuildSource();  //currLayers
      };
      container->addWidget(showBtn);
      item->onClicked = [=](){
        showBtn->setChecked(false);
        if(key != currSource)
          rebuildSource(key);
      };
      editBtn->onClicked = [=](){ populateSourceEdit(showBtn->isChecked() ? "" : key); };
    }
    else {
      item->onClicked = [=](){ if(key != currSource) rebuildSource(key); };
      editBtn->onClicked = [=](){ populateSourceEdit(key); };
    }

    Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowBtn->setMenu(overflowMenu);
    auto deleteSrcFn = [=](std::string res){
      if(res != "OK") return;
      mapSources.remove(key);
      saveSources();
      app->gui->deleteWidget(item);  //populateSources();
    };
    overflowMenu->addItem("Delete", [=](){
      std::vector<std::string> dependents;
      for (const auto& ssrc : mapSources) {
        for (const auto& layer : ssrc.second["layers"]) {
          if(layer["source"].Scalar() == key)
            dependents.push_back(ssrc.second["title"].Scalar());
        }
      }
      if(!dependents.empty())
        MapsApp::messageBox("Delete source", fstring("%s is used by other sources: %s. Delete anyway?",
            mapSources[key]["title"].Scalar().c_str(), joinStr(dependents, ", ").c_str()),
            {"OK", "Cancel"}, deleteSrcFn);
      else
        deleteSrcFn("OK");
    });

    container->addWidget(editBtn);
    container->addWidget(overflowBtn);
    sourcesContent->addItem(key, item);  //addWidget(item);
  }
  //for(SelectBox* combo : layerCombos)
  //  combo->addItems(layerTitles);
  if(!selectLayerDialog) {
    selectLayerDialog.reset(createSelectDialog("Choose Layer", MapsApp::uiIcon("layer")));
    selectLayerDialog->onSelected = [this](int idx){
      currLayers.push_back(layerKeys[idx]);
      rebuildSource();  //currLayers);
      populateSourceEdit("");
    };
  }
  selectLayerDialog->addItems(layerTitles);
}

void MapsSources::onMapEvent(MapEvent_t event)
{
  if(event != MAP_CHANGE)
    return;
  if(!sceneVarsLoaded && app->map->getScene()->isReady() && sourceEditPanel->isVisible())
    populateSceneVars();
}

void MapsSources::populateSceneVars()
{
  sceneVarsLoaded = true;
  app->gui->deleteContents(varsContent);

  YAML::Node vars = app->readSceneValue("global.gui_variables");
  for(const auto& var : vars) {
    std::string name = var["name"].as<std::string>("");
    std::string label = var["label"].as<std::string>("");
    std::string reload = var["reload"].as<std::string>("");
    std::string stylename = var["style"].as<std::string>("");
    if(!stylename.empty()) {
      // shader uniform
      auto& styles = app->map->getScene()->styles();
      for(auto& style : styles) {
        if(style->getName() == stylename) {
          for(auto& uniform : style->styleUniforms()) {
            if(uniform.first.name == name) {
              if(uniform.second.is<float>()) {
                auto spinBox = createTextSpinBox(uniform.second.get<float>(), 1, -INFINITY, INFINITY, "%.2f");
                spinBox->onValueChanged = [=, &uniform](real val){
                  std::string path = "styles." + stylename + ".shaders.uniforms." + name;
                  std::remove_if(app->sceneUpdates.begin(), app->sceneUpdates.end(),
                      [&](const SceneUpdate& s){ return s.path == path; });
                  app->sceneUpdates.push_back(SceneUpdate{path, std::to_string(val)});
                  uniform.second.set<float>(val);
                  app->platform->requestRender();
                  sourceModified();
                };
                varsContent->addWidget(createTitledRow(label.c_str(), spinBox));
              }
              else
                LOGE("Cannot set %s.%s: only float uniforms currently supported in gui_variables!", stylename.c_str(), name.c_str());
              return;
            }
          }
          break;
        }
      }
      LOGE("Cannot find style uniform %s.%s referenced in gui_variables!", stylename.c_str(), name.c_str());
    }
    else {
      // global variable, accessed in scene file by JS functions
      std::string value = app->readSceneValue("global." + name).as<std::string>("");
      auto checkbox = createCheckBox("", value == "true");
      checkbox->onToggled = [=](bool newval){
        std::string path = "global." + name;
        std::remove_if(app->sceneUpdates.begin(), app->sceneUpdates.end(),
            [&](const SceneUpdate& s){ return s.path == path; });
        app->sceneUpdates.push_back(SceneUpdate{path, newval ? "true" : "false"});
        sourceModified();
        if(reload == "false")  // ... so default to reloading
          app->map->updateGlobals({app->sceneUpdates.back()});
        else
          app->loadSceneFile();
      };
      varsContent->addWidget(createTitledRow(label.c_str(), checkbox));
    }
  }

  // load legend widgets
  app->gui->deleteContents(legendMenu->selectFirst(".child-container"));
  app->gui->deleteContents(app->legendContainer);
  YAML::Node legends = app->readSceneValue("global.__legend");
  for(const auto& legend : legends) {
    Widget* widget = new Widget(loadSVGFragment(legend.second["svg"].Scalar().c_str()));
    widget->setMargins(10, 0, 10, 0);
    widget->setVisible(false);
    app->legendContainer->addWidget(widget);

    Button* menuitem = createCheckBoxMenuItem(legend.second["title"].Scalar().c_str());
    legendMenu->addItem(legend.second["title"].Scalar().c_str(), [=](){
      widget->setVisible(!widget->isVisible());
      menuitem->setChecked(widget->isVisible());
    });
  }
  legendBtn->setVisible(app->legendContainer->containerNode()->firstChild() != NULL);
}

void MapsSources::populateSourceEdit(std::string key)
{
  if(currSource != key)
    rebuildSource(key);

  titleEdit->setText(mapSources[key]["title"].Scalar().c_str());
  app->showPanel(sourceEditPanel, true);
  //sourceEditPanel->selectFirst(".panel-title")->setText(mapSources[key]["title"].Scalar().c_str());

  for(auto& src : currLayers) {
    Button* item = createListItem(MapsApp::uiIcon("layers"), mapSources[src]["title"].Scalar().c_str());
    Widget* container = item->selectFirst(".child-container");

    //<use class="icon elevation-icon" width="18" height="18" xlink:href=":/ui-icons.svg#mountain"/>
    //widgetNode("#listitem-icon")
    //TextEdit* opacityEdit = createTextEdit(80);
    //container->addWidget(opacityEdit);
    //... updates.emplace_back("+layers." + rasterN + ".draw.group-0.alpha", <alpha value>);

    Button* discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Remove");
    discardBtn->onClicked = [=](){
      std::remove(currLayers.begin(), currLayers.end(), src);
      app->gui->deleteWidget(item);
      rebuildSource();  //tempLayers);
    };
    container->addWidget(discardBtn);
  }

  Button* item = createListItem(MapsApp::uiIcon("add"), "Add Layer...");
  item->onClicked = [=](){
    MapsApp::gui->showModal(selectLayerDialog.get(), MapsApp::gui->windows.front()->modalOrSelf());
  };

  if(app->map->getScene()->isReady())
    populateSceneVars();
}

void MapsSources::importSources(const std::string& src)
{
  std::string key;
  if(src.back() == '}') {
    key = createSource("", src);
  }
  else if(Tangram::NetworkDataSource::urlHasTilePattern(src)) {
    key = createSource("", fstring("{type: Raster, title: 'New Source', url: %s}", src.c_str()));
  }
  else {
    // source name conflicts: skip, replace, rename, or cancel? dialog on first conflict?
    app->platform->startUrlRequest(Url(src), [=](UrlResponse&& response){ MapsApp::runOnMainThread( [=](){
      if(response.error)
        MapsApp::messageBox("Import error", fstring("Unable to load '%s': %s", src.c_str(), response.error));
      else {
        try {
          YAML::Node newsources = YAML::Load(response.content.data(), response.content.size());
          for(auto& node : newsources)
            mapSources[node.first.Scalar()] = node.second;
        } catch (std::exception& e) {
          MapsApp::messageBox("Import error", fstring("Error parsing '%s': %s", src.c_str(), e.what()));
        }
      }
    } ); });
    return;
  }
  if(key.empty())
    MapsApp::messageBox("Import error", fstring("Unable to create source from '%s'", src.c_str()));
  else
    populateSourceEdit(key);  // so user can edit title
}

Button* MapsSources::createPanel()
{
  Toolbar* sourceTb = createToolbar();
  titleEdit = createTitledTextEdit("Title");
  titleEdit->node->setAttribute("box-anchor", "hfill");
  saveBtn = createToolbutton(MapsApp::uiIcon("save"), "Save Source");
  //discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Delete Source");
  sourceTb->addWidget(titleEdit);
  sourceTb->addWidget(saveBtn);

  Toolbar* importTb = createToolbar();
  TextEdit* importEdit = createTextEdit();
  Button* importAccept = createToolbutton(MapsApp::uiIcon("accept"), "Save");
  Button* importCancel = createToolbutton(MapsApp::uiIcon("cancel"), "Cancel");
  importTb->addWidget(importEdit);
  importTb->addWidget(importAccept);
  importTb->addWidget(importCancel);
  importCancel->onClicked = [=](){ importTb->setVisible(false); };

  // JSON (YAML flow), tile URL, or path/URL to file
  importAccept->onClicked = [=](){
    importSources(importEdit->text());
    importTb->setVisible(false);
  };

  Button* createBtn = createToolbutton(MapsApp::uiIcon("add"), "New Source");
  createBtn->onClicked = [=](){
    // ensure at least the first two layer selects are visible
    //layerRows[0]->setVisible(true);
    //layerRows[1]->setVisible(true);
    currSource = "";
    populateSourceEdit("");  // so user can edit title
  };

  saveBtn->onClicked = [=](){
    createSource(currSource);
    saveBtn->setEnabled(false);
  };

  // we should check for conflicting w/ title of other source here
  titleEdit->onChanged = [this](const char* s){ saveBtn->setEnabled(s[0]); };

  sourcesContent = new DragDropList;  //createColumn();

  Widget* srcEditContent = createColumn();
  layersContent = createColumn();
  layersContent->node->setAttribute("box-anchor", "hfill");
  varsContent = createColumn();
  varsContent->node->setAttribute("box-anchor", "hfill");
  srcEditContent->addWidget(varsContent);
  srcEditContent->addWidget(layersContent);
  //for(int ii = 1; ii <= MAX_SOURCES; ++ii) {
  //  layerCombos.push_back(createSelectBox(fstring("Layer %d", ii).c_str(), MapsApp::uiIcon("layers"), {}));
  //  layerCombos.back()->onChanged = [this](int){ rebuildSource(); };
  //  layerRows.push_back(createTitledRow(fstring("Layer %d", ii).c_str(), layerCombos.back()));
  //  layersContent->addWidget(layerRows.back());
  //}

  auto clearCacheFn = [this](std::string res){
    if(res == "OK") {
      shrinkCache(20'000'000);  // 20MB just to test shrinkCache code
      app->storageTotal = app->storageOffline;
    }
  };

  Widget* offlineBtn = app->mapsOffline->createPanel();

  legendBtn = createToolbutton(MapsApp::uiIcon("map-question"), "Legends");
  legendMenu = createMenu(Menu::VERT_LEFT);
  legendBtn->setMenu(legendMenu);
  legendBtn->setVisible(false);

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
  Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
  overflowBtn->setMenu(overflowMenu);
  overflowMenu->addItem("Import source", [=](){ importEdit->setText("");  importTb->setVisible(true); });
  overflowMenu->addItem("Clear cache", [=](){
    MapsApp::messageBox("Clear cache", "Delete all cached map data? This action cannot be undone.",
        {"OK", "Cancel"}, clearCacheFn);
  });
  overflowMenu->addItem("Restore default sources", [=](){
    FSPath path = FSPath(app->configFile).parent().child("mapsources.default.yaml");
    importSources(path.path);
  });

  auto sourcesHeader = app->createPanelHeader(MapsApp::uiIcon("layers"), "Map Source");
  sourcesHeader->addWidget(createStretch());
  sourcesHeader->addWidget(createBtn);
  sourcesHeader->addWidget(legendBtn);
  sourcesHeader->addWidget(offlineBtn);
  sourcesHeader->addWidget(overflowBtn);
  sourcesPanel = app->createMapPanel(sourcesHeader, NULL, sourcesContent);

  sourcesPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(sourcesDirty)
        populateSources();
    }
    return false;
  });


  auto editHeader = app->createPanelHeader(MapsApp::uiIcon("edit"), "Edit Source");
  sourceEditPanel = app->createMapPanel(editHeader, srcEditContent, sourceTb);

  // main toolbar button
  Menu* sourcesMenu = createMenu(Menu::VERT_LEFT);
  //sourcesMenu->autoClose = true;
  sourcesMenu->addHandler([this, sourcesMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(sourcesMenu->selectFirst(".child-container"));
      for(int ii = 0; ii < 10 && ii < sourceKeys.size(); ++ii) {
        std::string key = sourceKeys[ii];
        sourcesMenu->addItem(mapSources[key]["title"].Scalar().c_str(),
            [this, key](){ rebuildSource(key); });
      }
    }
    return false;
  });

  Button* sourcesBtn = app->createPanelButton(MapsApp::uiIcon("layers"), "Sources", sourcesPanel);
  sourcesBtn->setMenu(sourcesMenu);
  return sourcesBtn;
}
