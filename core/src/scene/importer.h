#pragma once

#include "platform.h"

#include "gaml/src/yaml.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Tangram {

class AsyncWorker;
class SceneOptions;
class ZipArchive;
class Url;

class Importer {
public:

    using Node = YAML::Node;

    Importer();
    ~Importer();

    // Loads the main scene with deep merging dependent imported scenes.
    YAML::Node loadSceneData(Platform& platform, const Url& sceneUrl, const std::string& sceneYaml = "");

    void cancelLoading();

    static bool isZipArchiveUrl(const Url& url);

    static Url getBaseUrlForZipArchive(const Url& archiveUrl);

    static Url getArchiveUrlForZipEntry(const Url& zipEntryUrl);

    // Traverses the nodes contained in the given root scene node and for all
    // nodes that represent URLs, replaces the contents with that URL resolved
    // against the given base URL.
    static void resolveSceneUrls(Node& root, const Url& base);

    static std::vector<const YAML::Node*> getTextureUrlNodes(const Node& root);

    // Start an asynchronous request for the scene resource at the given URL.
    // In addition to the URL types supported by the platform instance, this
    // also supports a custom ZIP URL scheme. ZIP URLs are of the form:
    //   zip://path/to/file.txt#http://host.com/some/archive.zip
    // The fragment (#http...) of the URL is the location of the archive and the
    // relative portion of the URL (path/...) is the path of the target file
    // within the archive (this allows relative path operations on URLs to work
    // as expected within zip archives). This function expects that all required
    // zip archives will be added to the scene with addZipArchive before being
    // requested.
    UrlRequestHandle readFromZip(const Url& url, UrlCallback callback);

protected:

    // Process and store data for an imported scene from a vector of bytes.
    void addSceneData(const Url& sceneUrl, std::vector<char>&& sceneContent);

    // Process and store data for an imported scene from a string of YAML.
    void addSceneYaml(const Url& sceneUrl, const char* sceneYaml, size_t length);

    // Get the sequence of scene names that are designated to be imported into the
    // input scene node by its 'import' fields.
    std::vector<Url> getResolvedImportUrls(const Node& sceneNode, const Url& base);

    // loads all the imported scenes and the master scene and returns a unified YAML root node.
    void importScenesRecursive(Node& root, const Url& sceneUrl, std::unordered_set<Url>& imported);

    // Scene files must be parsed into YAML nodes to find further imports.
    // The parsed scenes are stored in a map with their URLs to be merged once
    // all imports are found and parsed.
    struct SceneNode {
        YAML::Node yaml{};
        std::vector<Url> imports;
        std::vector<const YAML::Node*> pendingUrlNodes;
    };
    std::unordered_map<Url, SceneNode> m_sceneNodes = {};

    std::vector<Url> m_sceneQueue = {};

    std::atomic<bool> m_canceled{false};
    std::mutex m_sceneMutex;
    std::condition_variable m_sceneCond;

    // Container for any zip archives needed for the scene. For each entry, the
    // key is the original URL from which the zip archive was retrieved and the
    // value is a ZipArchive initialized with the compressed archive data.
    std::unordered_map<Url, std::shared_ptr<ZipArchive>> m_zipArchives;
    std::unique_ptr<AsyncWorker> m_zipWorker;
};

}
