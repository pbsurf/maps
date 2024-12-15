#include "scene/styleMixer.h"

#include "style/style.h"
#include "util/topologicalSort.h"

#include <algorithm>
#include <set>
#include "gaml/src/yaml.h"

namespace Tangram {

std::vector<std::string> StyleMixer::getStylesToMix(const Node& _style) {

    std::vector<std::string> names;

    // 'base' style is the first item to mix.
    if (const Node& base = _style["base"]) {
        if (base.IsScalar()) { names.push_back(base.Scalar()); }
    }

    // 'mix' styles are mixed next, in order of declaration.
    if (const Node& mix = _style["mix"]) {
        if (mix.IsScalar()) {
            names.push_back(mix.Scalar());
        } else if (mix.IsSequence()) {
            for (const auto& m : mix) {
                if (m.IsScalar()) { names.push_back(m.Scalar()); }
            }
        }
    }

    return names;
}

std::vector<std::string> StyleMixer::getMixingOrder(const Node& _styles) {

    // Input must be a map of names to style configuration nodes.
    if (!_styles.IsMap()) {
        return {};
    }

    // Dependencies are pairs of strings that form a DAG.
    // If style 'a' mixes style 'b', the dependency would be {'b', 'a'}.
    std::vector<std::pair<std::string, std::string>> dependencies;

    for (auto entry : _styles.pairs()) {
        const auto& name = entry.first;
        const auto& config = entry.second;
        for (const auto& mix : getStylesToMix(config)) {
            dependencies.push_back({ mix, name.Scalar() });
        }
    }

    return topologicalSort(dependencies);
}

void StyleMixer::mixStyleNodes(Node& _styles) {

    // First determine the order of nodes to evaluate.
    auto styleNamesSorted = getMixingOrder(_styles);

    for (const auto& name : styleNamesSorted) {

        auto& style = _styles[name];

        if (!style || !style.IsMap()) {
            // Something's wrong here, try the next one!
            continue;
        }

        // For each style to evaluate, get the list of styles that need to be mixed with this one.
        auto stylesToMix = getStylesToMix(style);

        Mixins mixins;
        for (const auto& styleNameToMix : stylesToMix) {

            // Skip mixing built-in styles.
            const auto& builtIn = Style::builtInStyleNames();
            if (std::find(builtIn.begin(), builtIn.end(), styleNameToMix) != builtIn.end()) {
                continue;
            }

            mixins.push_back(&_styles[styleNameToMix]);
        }

        applyStyleMixins(style, mixins);
    }
}

void StyleMixer::applyStyleMixins(Node& _style, const Mixins& _mixins) {

    // Merge boolean flags as a disjunction.
    mergeBooleanFieldAsDisjunction("animated", _style, _mixins);
    mergeBooleanFieldAsDisjunction("texcoords", _style, _mixins);

    // Merge scalar fields with newer values taking precedence.
    mergeFieldTakingLast("base", _style, _mixins);
    mergeFieldTakingLast("lighting", _style, _mixins);
    mergeFieldTakingLast("texture", _style, _mixins);
    mergeFieldTakingLast("blend", _style, _mixins);
    mergeFieldTakingLast("blend_order", _style, _mixins);
    mergeFieldTakingLast("raster", _style, _mixins);

    // Merge map fields with newer values taking precedence.
    mergeMapFieldTakingLast("material", _style, _mixins);
    mergeMapFieldTakingLast("draw", _style, _mixins);

    // Produce a list of all 'mixins' with shader nodes and merge those separately.
    Mixins shaderMixins;
    for (const auto& mixin : _mixins) {
        if (const auto& shaders = (*mixin)["shaders"]) {
            shaderMixins.push_back(&shaders);
        }
    }
    applyShaderMixins(_style["shaders"], shaderMixins);
}

void StyleMixer::applyShaderMixins(Node& _shaders, const Mixins& _mixins) {

    // Merge maps fields with newer values taking precedence.
    mergeMapFieldTakingLast("defines", _shaders, _mixins);
    mergeMapFieldTakingLast("uniforms", _shaders, _mixins);

    // Merge "extensions" as a non-repeating set.
    {
        std::set<std::string> set;
        // Clear this node in case something was already there.
        Node& output = _shaders["extensions_mixed"] = Node();
        for (const Node* mixin : _mixins) {
            const Node& extensions = (*mixin)["extensions_mixed"];
            for (const auto& e : extensions) {
                set.insert(e.Scalar());
            }
        }
        Node& extensions = _shaders["extensions"];
        if (extensions.IsScalar()) {
            set.insert(extensions.Scalar());
        } else if (extensions.IsSequence()) {
            for (const auto& e : extensions) {
                set.insert(e.Scalar());
            }
        }
        for (const auto& extension : set) {
            output.push_back(extension);
        }
    }

    // Merge "blocks" into a list of strings for each key.
    {
        // Clear this node in case something was already there.
        Node& output = _shaders["blocks_mixed"] = Node();
        for (const Node* mixin : _mixins) {
            const Node& blocks = (*mixin)["blocks_mixed"];
            for (auto entry : blocks.pairs()) {
                Node& list = output[entry.first.Scalar()];
                for (const auto& block : entry.second) {
                    // If the list already contains an exact reference to the same node,
                    // don't add it again.
                    auto listit = list.begin();
                    while (listit != list.end() && *listit != block.Scalar()) { ++listit; }
                    if (listit == list.end()) {
                        list.push_back(block.clone());
                    }
                }
            }
        }
        for (auto entry : _shaders["blocks"].pairs()) {
            output[entry.first.Scalar()].push_back(entry.second.Scalar());
        }
    }
}

void StyleMixer::mergeBooleanFieldAsDisjunction(const std::string& key, Node& target, const Mixins& sources) {

    Node& current = target[key];
    if (current && current.as<bool>(false)) {
        // Target field is already true, we can stop here.
        return;
    }

    for (const Node* source : sources) {
        const auto& value = (*source)[key];
        if (value && value.as<bool>(false)) {
            target[key] = true;
            return;
        }
    }
}

void StyleMixer::mergeFieldTakingLast(const std::string& key, Node& target, const Mixins& sources) {

    if (target[key]) {
        // Target already has a value, we can stop here.
        return;
    }

    for (auto it = sources.rbegin(); it != sources.rend(); ++it) {
        const auto& value = (**it)[key];
        if (value) {
            target[key] = value.clone();
            return;
        }
    }
}

void StyleMixer::mergeMapFieldTakingLast(const std::string& key, Node& target, const Mixins& sources) {

    Node& map = target[key];
    if (map && !map.IsMap()) { return; }

    for (auto it = sources.rbegin(); it != sources.rend(); ++it) {
        const Node& source = (**it)[key];
        if (!source || !source.IsMap()) {
            continue;
        }

        for (auto entry : source.pairs()) {
            const auto& subkey = entry.first.Scalar();
            if (!map[subkey]) {
                map[subkey] = entry.second.clone();
            }
        }
    }

}

}
