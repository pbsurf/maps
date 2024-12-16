#include "scene/importer.h"

#include "log.h"
#include "platform.h"
#include "util/asyncWorker.h"
#include "util/yamlUtil.h"
#include "util/zipArchive.h"

#include <algorithm>
#include <atomic>
#include <cassert>

using YAML::Node;
using YAML::NodeType;

namespace Tangram {

Importer::Importer() {}
Importer::~Importer() {}

YAML::Node Importer::loadSceneData(Platform& _platform, const Url& _sceneUrl, const std::string& _sceneYaml) {

    Url nextUrlToImport;
    std::vector<UrlRequestHandle> urlRequests;
    unsigned int activeDownloads = 0;  // protected by m_sceneMutex

    if (!_sceneYaml.empty()) {
        // Load scene from yaml string.
        addSceneYaml(_sceneUrl, _sceneYaml.data(), _sceneYaml.length());
    } else {
        // Load scene from yaml file.
        m_sceneQueue.push_back(_sceneUrl);
    }

    // we no longer wait for every callback to run (so activeDownloads == 0) when canceled - we only expect
    //  all callbacks to be called or removed by Platform before Importer is destroyed
    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_sceneMutex);
            m_sceneCond.wait(lock, [&](){
              return !m_sceneQueue.empty() || activeDownloads == 0 || m_canceled;
            });

            if (m_sceneQueue.empty() || m_canceled) { break; }

            nextUrlToImport = m_sceneQueue.back();
            m_sceneQueue.pop_back();

            // Mark Url as going-to-be-imported to prevent duplicate work.
            m_sceneNodes.emplace(nextUrlToImport, SceneNode{});
            activeDownloads++;
        }

        // unlock m_sceneMutex before starting request because callback could be sync or async
        auto cb = [&, nextUrlToImport](UrlResponse&& response) {
            if (m_canceled) { return; }
            std::unique_lock<std::mutex> _lock(m_sceneMutex);
            if (response.error) {
                LOGE("Unable to retrieve '%s': %s", nextUrlToImport.string().c_str(),
                     response.error);
            } else {
                addSceneData(nextUrlToImport, std::move(response.content));
            }
            activeDownloads--;
            m_sceneCond.notify_one();
        };

        if (nextUrlToImport.scheme() == "zip") {
            readFromZip(nextUrlToImport, cb);
        } else {
            urlRequests.push_back(_platform.startUrlRequest(nextUrlToImport, cb));
        }
    }

    if (m_canceled) {
        // clear all callbacks before captures go out of scope!
        for (auto& req : urlRequests) { _platform.cancelUrlRequest(req); }
        m_zipWorker.reset();
        return YAML::Node();
    }

    LOGD("Processing scene import Stack:");
    std::unordered_set<Url> imported;
    YAML::Node root;
    importScenesRecursive(root, _sceneUrl, imported);

    // After merging all scenes, resolve texture nodes as named textures or URLs.
    const Node& textures = root["textures"];
    for (auto& sceneNode : m_sceneNodes) {
        auto sceneUrl = sceneNode.first;
        if (isZipArchiveUrl(sceneUrl)) {
            sceneUrl = getBaseUrlForZipArchive(sceneUrl);
        }
        for (auto* url : sceneNode.second.pendingUrlNodes) {
            // If the node does not contain a named texture in the final scene, treat it as a URL relative to the scene
            // file where it was originally encountered.
            if (!textures[url->Scalar()]) {
                *const_cast<YAML::Node*>(url) = sceneUrl.resolve(Url(url->Scalar())).string();
            }
        }
    }

    m_sceneNodes.clear();

    return root;
}

void Importer::cancelLoading() {  //Platform& _platform) {
    std::unique_lock<std::mutex> lock(m_sceneMutex);
    m_canceled = true;
    m_sceneCond.notify_all();
}

void Importer::addSceneData(const Url& sceneUrl, std::vector<char>&& sceneData) {
    LOGD("Process: '%s'", sceneUrl.string().c_str());

    if (!isZipArchiveUrl(sceneUrl)) {
        addSceneYaml(sceneUrl, sceneData.data(), sceneData.size());
        return;
    }

    // We're loading a scene from a zip archive
    // First, create an archive from the data.
    auto zipArchive = std::make_shared<ZipArchive>();
    zipArchive->loadFromMemory(std::move(sceneData));

    // Find the "base" scene file in the archive entries.
    for (const auto& entry : zipArchive->entries()) {
        auto ext = Url::getPathExtension(entry.path);
        // The "base" scene file must have extension "yaml" or "yml" and be
        // at the root directory of the archive (i.e. no '/' in path).
        if ((ext == "yaml" || ext == "yml") && entry.path.find('/') == std::string::npos) {
            // Found the base, now extract the contents to the scene string.
            std::vector<char> yaml;
            yaml.resize(entry.uncompressedSize);

            zipArchive->decompressEntry(&entry, &yaml[0]);

            addSceneYaml(sceneUrl, yaml.data(), yaml.size());
            break;
        }
    }

    m_zipArchives.emplace(sceneUrl, zipArchive);
}

UrlRequestHandle Importer::readFromZip(const Url& url, UrlCallback callback) {

    if (!m_zipWorker) {
        m_zipWorker = std::make_unique<AsyncWorker>();
        //m_zipWorker->waitForCompletion();
    }

    m_zipWorker->enqueue([=](){
        UrlResponse response;
        // URL for a file in a zip archive, get the encoded source URL.
        auto source = Importer::getArchiveUrlForZipEntry(url);
        // Search for the source URL in our archive map.
        auto it = m_zipArchives.find(source);
        if (it != m_zipArchives.end()) {
            auto& archive = it->second;
            // Found the archive! Now create a response for the request.
            auto zipEntryPath = url.path().substr(1);
            auto entry = archive->findEntry(zipEntryPath);
            if (entry) {
                response.content.resize(entry->uncompressedSize);
                bool success = archive->decompressEntry(entry, response.content.data());
                if (!success) {
                    response.error = "Unable to decompress zip archive file.";
                }
            } else {
                response.error = "Did not find zip archive entry.";
            }
        } else {
            response.error = "Could not find zip archive.";
        }
        callback(std::move(response));
    });
    return 0;
}

void Importer::addSceneYaml(const Url& sceneUrl, const char* sceneYaml, size_t length) {

    auto& sceneNode = m_sceneNodes[sceneUrl];

    sceneNode.yaml = YAML::Load(sceneYaml, length);

    if (!sceneNode.yaml.IsMap()) {
        LOGE("Scene is not a valid YAML map: %s", sceneUrl.string().c_str());
        return;
    }

    sceneNode.imports = getResolvedImportUrls(sceneNode.yaml, sceneUrl);

    sceneNode.pendingUrlNodes = getTextureUrlNodes(sceneNode.yaml);

    // Remove 'import' values so they don't get merged.
    sceneNode.yaml.remove("import");

    for (const auto& url : sceneNode.imports) {
        // Check if this scene URL has been (or is going to be) imported already
        if (m_sceneNodes.find(url) == m_sceneNodes.end()) {
            m_sceneQueue.push_back(url);
        }
    }
}

std::vector<Url> Importer::getResolvedImportUrls(const Node& sceneNode, const Url& baseUrl) {

    std::vector<Url> sceneUrls;

    auto base = baseUrl;
    if (isZipArchiveUrl(baseUrl)) {
        base = getBaseUrlForZipArchive(baseUrl);
    }

    if (sceneNode.IsMap()) {
        if (const Node& import = sceneNode["import"]) {
            if (import.IsScalar()) {
                sceneUrls.push_back(base.resolve(Url(import.Scalar())));
            } else if (import.IsSequence()) {
                for (const auto &path : import) {
                    if (path.IsScalar()) {
                        sceneUrls.push_back(base.resolve(Url(path.Scalar())));
                    }
                }
            }
        }
    }

    return sceneUrls;
}

void Importer::importScenesRecursive(Node& root, const Url& sceneUrl, std::unordered_set<Url>& imported) {

    LOGD("Starting importing Scene: %s", sceneUrl.string().c_str());

    // Insert self to handle self-imports cycles
    imported.insert(sceneUrl);

    auto& sceneNode = m_sceneNodes[sceneUrl];

    // If an import URL is already in the imported set that means it is imported by a "parent" scene file to this one.
    // The parent import will assign the same values, so we can safely skip importing it here. This saves some work and
    // also prevents import cycles.
    //
    // It is important that we don't merge the same YAML node more than once. YAML node assignment is by reference, so
    // merging mutates the original input nodes.
    auto it = std::remove_if(sceneNode.imports.begin(), sceneNode.imports.end(),
                             [&](auto& i){ return imported.find(i) != imported.end(); });

    if (it != sceneNode.imports.end()) {
        LOGD("Skipping redundant import(s)");
        sceneNode.imports.erase(it, sceneNode.imports.end());
    }

    imported.insert(sceneNode.imports.begin(), sceneNode.imports.end());

    for (const auto& url : sceneNode.imports) {
        importScenesRecursive(root, url, imported);
    }

    // don't overwrite root with empty node from missing file!
    if (sceneNode.yaml) {
      YamlUtil::mergeMapFields(root, std::move(sceneNode.yaml));
    }

    resolveSceneUrls(root, sceneUrl);
}

bool Importer::isZipArchiveUrl(const Url& url) {
    return Url::getPathExtension(url.path()) == "zip";
}

Url Importer::getBaseUrlForZipArchive(const Url& archiveUrl) {
    auto encodedSourceUrl = Url::escapeReservedCharacters(archiveUrl.string());
    auto baseUrl = Url("zip://" + encodedSourceUrl);
    return baseUrl;
}

Url Importer::getArchiveUrlForZipEntry(const Url& zipEntryUrl) {
    auto encodedSourceUrl = zipEntryUrl.netLocation();
    auto source = Url(Url::unEscapeReservedCharacters(encodedSourceUrl));
    return source;
}

static bool nodeIsPotentialUrl(const Node& node) {
    // Check that the node is scalar and not null.
    if (!node || !node.IsScalar()) { return false; }

    // Check that the node does not contain a 'global' reference.
    if (node.Scalar().compare(0, 7, "global.") == 0) { return false; }

    // check for function
    if (node.Scalar().compare(0, 8, "function") == 0) { return false; }

    return true;
}

static bool nodeIsPotentialTextureUrl(const Node& node) {

    if (!nodeIsPotentialUrl(node)) { return false; }

    // Check that the node is not a number or a boolean.
    bool booleanValue = false;
    double numberValue = 0.;
    if (YamlUtil::getBool(node, booleanValue)) { return false; }  //YAML::convert<bool>::decode
    if (YamlUtil::getDouble(node, numberValue)) { return false; }  //YAML::convert<double>::decode

    return true;
}

std::vector<const YAML::Node*> Importer::getTextureUrlNodes(const Node& root) {

    std::vector<const YAML::Node*> nodes;

    if (const Node& styles = root["styles"]) {

        for (auto entry : styles.pairs()) {

            const Node& style = entry.second;
            if (!style.IsMap()) { continue; }

            //style->texture
            if (const Node& texture = style["texture"]) {
                if (nodeIsPotentialTextureUrl(texture)) {
                    nodes.push_back(&texture);
                }
            }

            //style->material->texture
            if (const Node& material = style["material"]) {
                if (!material.IsMap()) { continue; }
                for (auto& prop : {"emission", "ambient", "diffuse", "specular", "normal"}) {
                    if (const Node& propNode = material[prop]) {
                        if (!propNode.IsMap()) { continue; }
                        if (const Node& matTexture = propNode["texture"]) {
                            if (nodeIsPotentialTextureUrl(matTexture)) {
                                nodes.push_back(&matTexture);
                            }
                        }
                    }
                }
            }

            //style->shader->uniforms->texture
            if (const Node& shaders = style["shaders"]) {
                if (!shaders.IsMap()) { continue; }
                if (const Node& uniforms = shaders["uniforms"]) {
                    for (auto uniformEntry : uniforms.pairs()) {
                        const Node& uniformValue = uniformEntry.second;
                        if (nodeIsPotentialTextureUrl(uniformValue)) {
                            nodes.push_back(&uniformValue);
                        } else if (uniformValue.IsSequence()) {
                            for (const Node& u : uniformValue) {
                                if (nodeIsPotentialTextureUrl(u)) {
                                    nodes.push_back(&u);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return nodes;
}

void Importer::resolveSceneUrls(Node& root, const Url& baseUrl) {

    auto base = baseUrl;
    if (isZipArchiveUrl(baseUrl)) {
        base = getBaseUrlForZipArchive(baseUrl);
    }

    // Resolve global texture URLs.

    if (Node& textures = root["textures"]) {
        for (auto texture : textures.pairs()) {
            if (Node& textureUrlNode = texture.second["url"]) {
                if (nodeIsPotentialUrl(textureUrlNode)) {
                    textureUrlNode = base.resolve(Url(textureUrlNode.Scalar())).string();
                }
            }
        }
    }

    // Resolve data source URLs.

    if (Node& sources = root["sources"]) {
        for (auto source : sources.pairs()) {
            if (!source.second.IsMap()) { continue; }
            if (Node& sourceUrl = source.second["url"]) {
                if (nodeIsPotentialUrl(sourceUrl)) {
                    sourceUrl = base.resolve(Url(sourceUrl.Scalar())).string();
                }
            }
        }
    }

    // Resolve font URLs.

    if (Node& fonts = root["fonts"]) {
        if (fonts.IsMap()) {
            for (const auto& font : fonts.pairs()) {
                if (font.second.IsMap()) {
                    auto& urlNode = font.second["url"];
                    if (nodeIsPotentialUrl(urlNode)) {
                        urlNode = base.resolve(Url(urlNode.Scalar())).string();
                    }
                } else if (font.second.IsSequence()) {
                    for (auto& fontNode : font.second) {
                        auto& urlNode = fontNode["url"];
                        if (nodeIsPotentialUrl(urlNode)) {
                            urlNode = base.resolve(Url(urlNode.Scalar())).string();
                        }
                    }
                }
            }
        }
    }
}

}
