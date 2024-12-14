#include "util/yamlPath.h"

#include <cmath>

#define MAP_DELIM '.'
#define SEQ_DELIM '#'

namespace Tangram {

YamlPath YamlPathBuffer::toYamlPath() {
    size_t length = 0;

    for(auto& element : m_path) {
        if (element.key) {
            length += element.key->length()+1;
        } else {
            int c = 1;
            while (element.index >= pow(10, c)) { c += 1; }
            length += c + 1;
        }
    }
    std::string path;
    path.reserve(length);

    for(auto& element : m_path) {
        if (element.key) {
            if (!path.empty()) { path += '.'; }
            path += *element.key;
        } else {
            path += '#';
            path += std::to_string(element.index);
        }
    }
    return YamlPath(path);
}

YamlPath::YamlPath() {}

YamlPath::YamlPath(const std::string& path)
    : codedPath(path) {}

YamlPath YamlPath::add(int index) {
    return YamlPath(codedPath + SEQ_DELIM + std::to_string(index));
}

YamlPath YamlPath::add(const std::string& key) {
    if (codedPath.empty()) { return YamlPath(key); }
    return YamlPath(codedPath + MAP_DELIM + key);
}

YAML::Node* YamlPath::get(YAML::Node& root) {
    YAML::Node* proot = &root;
    size_t beginToken = 0, pathSize = codedPath.size();
    bool createPath = pathSize && codedPath[0] == '+';
    size_t endToken = createPath ? 1 : 0;
    auto delimiter = MAP_DELIM; // First token must be a map key.
    while (endToken < pathSize) {
        if (!proot->IsDefined()) {
            return nullptr; // A node before the end of the path was mising, quit!
        }
        beginToken = endToken;
        endToken = pathSize;
        endToken = std::min(endToken, codedPath.find(SEQ_DELIM, beginToken));
        endToken = std::min(endToken, codedPath.find(MAP_DELIM, beginToken));
        if (delimiter == SEQ_DELIM) {
            int index = std::stoi(&codedPath[beginToken]);
            if (proot->IsSequence()) {
                proot = &(*proot)[index];
            } else {
                return nullptr;
            }
        } else if (delimiter == MAP_DELIM) {
            auto key = codedPath.substr(beginToken, endToken - beginToken);
            if (createPath && !(*proot)[key])
                (*proot)[key] = YAML::Node();
            if (proot->IsMap()) {
                proot = &(*proot)[key];
            } else {
                return nullptr;
            }
        } else {
            return nullptr; // Path is malformed, return null node.
        }
        delimiter = codedPath[endToken]; // Get next character as the delimiter.
        ++endToken; // Move past the delimiter.
    }
    return proot;
}

}
