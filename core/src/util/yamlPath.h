// Helper Functions for parsing YAML nodes
// NOTE: To be used in multiple YAML parsing modules once SceneLoader aptly modularized

#pragma once

#include <vector>
#include "gaml/src/yaml.h"

namespace Tangram {

// A YamlPath encodes the location of a node in a yaml document in a string,
// e.g. "lorem.ipsum#0" identifies root["lorem"]["ipsum"][0]
struct YamlPath {
    YamlPath();
    YamlPath(const std::string& path);
    YamlPath add(int index);
    YamlPath add(const std::string& key);
    // Follow this path from a root node and return pointer to Node if it exists or nullptr if not
    YAML::Node* get(YAML::Node& root);
    const YAML::Node* get(const YAML::Node& root) { return get(const_cast<YAML::Node&>(root)); }
    std::string codedPath;
};

struct YamlPathBuffer {

    struct PathElement {
        size_t index;
        const std::string* key;
        PathElement(size_t index, const std::string* key) : index(index), key(key) {}
    };

    std::vector<PathElement> m_path;

    void pushMap(const std::string* _p) { m_path.emplace_back(0, _p);}
    void pushSequence() { m_path.emplace_back(0, nullptr); }
    void increment() { m_path.back().index++; }
    void pop() { m_path.pop_back(); }
    YamlPath toYamlPath();
};

}
