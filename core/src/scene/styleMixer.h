#pragma once

#include <memory>
#include <string>
#include <vector>

namespace YAML {
    class Node;
}

namespace Tangram {

class StyleMixer {

public:

    using Node = YAML::Node;
    using Mixins = std::vector<const Node*>;

    // Get the sequence of style names that are designated to be mixed into the
    // input style node by its 'base' and 'mix' fields.
    std::vector<std::string> getStylesToMix(const Node& _style);

    // Get a sequence of style names ordered such that if style 'a' mixes style
    // 'b', 'b' will always precede 'a' in the sequence.
    std::vector<std::string> getMixingOrder(const Node& _styles);

    // Apply mixing to all styles in the input map with modifications in-place
    // unless otherwise noted.
    void mixStyleNodes(Node& _styles);

    // Apply the given list of 'mixin' styles to the first style.
    void applyStyleMixins(Node& _style, const Mixins& _mixins);

    // Apply the given list of 'mixin' style shader nodes to the first style
    // shader node. Note that 'blocks' and 'extensions' are merged into new
    // output fields called 'blocks_merged' and 'extensions_merged'.
    void applyShaderMixins(Node& _shaders, const Mixins& _mixins);

private:

    void mergeBooleanFieldAsDisjunction(const std::string& _key, Node& _target, const Mixins& _sources);

    void mergeFieldTakingLast(const std::string& _key, Node& _target, const Mixins& _sources);

    void mergeMapFieldTakingLast(const std::string& _key, Node& _target, const Mixins& _sources);

};

}
