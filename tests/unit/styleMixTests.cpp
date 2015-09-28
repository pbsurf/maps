#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <iostream>
#include <vector>
#include <string>

#include "data/filters.h"
#include "yaml-cpp/yaml.h"
#include "scene/sceneLoader.h"
#include "scene/scene.h"

using namespace Tangram;
using YAML::Node;

TEST_CASE( "Style Mixing Test: Concrete Overwrite value check", "[mixing][core][yaml]") {
    SceneLoader sceneLoader;
    std::unordered_set<std::string> uniqueStyles;
    std::vector<Node> mix;
    Node mixNode;
    Node node = YAML::Load(R"END(
        StyleA:
            mix: StyleB
        StyleB:
            mix: StyleC
        StyleC:
            material: valueC
        )END");

    mix = sceneLoader.recursiveMixins("StyleA", node, uniqueStyles);
    mixNode = sceneLoader.mixStyle(mix);
    REQUIRE(mixNode["material"].as<std::string>() == "valueC");
}

TEST_CASE( "Style Mixing Test: Nested Style Mixin Nodes", "[mixing][core][yaml]") {
    SceneLoader sceneLoader;
    std::unordered_set<std::string> uniqueStyles;
    std::vector<Node> mix;
    Node mixNode;
    Node node = YAML::Load(R"END(
        styleA:
        styleB:
        styleC:
            mix: [styleA, styleB]
        styleD:
            mix: [styleC, styleA]
        styleE:
            mix: [styleA, styleB, styleF]
        styleF:
            mix: styleA
        )END");

    mix = sceneLoader.recursiveMixins("styleA", node, uniqueStyles);
    REQUIRE(mix.size() == 1);
    mixNode = sceneLoader.mixStyle(mix);
    node["styleA"] = mixNode;
    uniqueStyles.clear();

    mix = sceneLoader.recursiveMixins("styleB", node, uniqueStyles);
    REQUIRE(mix.size() == 1);
    mixNode = sceneLoader.mixStyle(mix);
    node["styleB"] = mixNode;
    uniqueStyles.clear();

    mix = sceneLoader.recursiveMixins("styleC", node, uniqueStyles);
    REQUIRE(mix.size() == 3);
    mixNode = sceneLoader.mixStyle(mix);
    node["styleC"] = mixNode;
    uniqueStyles.clear();

    mix = sceneLoader.recursiveMixins("styleD", node, uniqueStyles);
    REQUIRE(mix.size() == 3);
    mixNode = sceneLoader.mixStyle(mix);
    node["styleD"] = mixNode;
    uniqueStyles.clear();

    mix = sceneLoader.recursiveMixins("styleE", node, uniqueStyles);
    REQUIRE(mix.size() == 4);
    mixNode = sceneLoader.mixStyle(mix);
    node["styleE"] = mixNode;
    uniqueStyles.clear();

    mix = sceneLoader.recursiveMixins("styleF", node, uniqueStyles);
    REQUIRE(mix.size() == 2);
    mixNode = sceneLoader.mixStyle(mix);
    node["styleF"] = mixNode;
}

TEST_CASE( "Style Mixing Test: Shader Extensions Merging", "[mixing][core][yaml]") {
    SceneLoader sceneLoader;
    Node node = YAML::Load(R"END(
        Node1:
            shaders:
                extensions: extension1
        Node2:
            shaders:
                extensions: [extension1, extension2, extension3]
        Node3:
            shaders:
                extensions: extension3
        Node4:
            shaders:
                extensions: [extension4]
        )END");

    Node extNode;

    extNode = sceneLoader.shaderExtMerge( { node["Node1"] } );
    // extNode: [extension1]
    REQUIRE(extNode.size() == 1);
    REQUIRE(extNode[0].as<std::string>() == "extension1");

    extNode = sceneLoader.shaderExtMerge( { node["Node1"], node["Node2"], node["Node3"], node["Node4"] } );
    // extNode: [extension1, extension2, extension3, extension4]
    REQUIRE(extNode.size() == 4);
    REQUIRE(extNode[0].as<std::string>() == "extension1");
    REQUIRE(extNode[1].as<std::string>() == "extension2");
    REQUIRE(extNode[2].as<std::string>() == "extension3");
    REQUIRE(extNode[3].as<std::string>() == "extension4");

}

TEST_CASE( "Style Mixing Test: Shader Blocks Merging", "[mixing][core][yaml]") {
    SceneLoader sceneLoader;

    Node node = YAML::Load(R"END(
        Node1:
            shaders:
                blocks:
                    color: colorBlockA;
                    normal: normalBlockA;
        Node2:
            shaders:
                blocks:
                    color: colorBlockB;
                    position: posBlockB;
                    global: globalBlockB;
        Node3:
            shaders:
                blocks:
                    global: globalBlockC;
                    filter: filterBlockC;
        )END");

    Node shaderBlocksNode = sceneLoader.shaderBlockMerge( { node["Node1"] } );
    // shaderBlocksNode:
    //          color: colorBlockA;
    //          normal: normalBlockA;

    REQUIRE(shaderBlocksNode["color"].as<std::string>() == "colorBlockA;");
    REQUIRE(shaderBlocksNode["normal"].as<std::string>() == "normalBlockA;");
    REQUIRE(!shaderBlocksNode["global"]);

    shaderBlocksNode = sceneLoader.shaderBlockMerge( { node["Node1"], node["Node2"], node["Node3"] } );

    // shaderBlocksNode:
    //          color: colorBlockA;\ncolorBlockB;
    //          normal: normalBlockA;
    //          position: posBlockB;
    //          global: globalBlockB;\nglobalBlockC;
    //          filter: filterBlockC;

    REQUIRE(shaderBlocksNode["color"].as<std::string>() == "colorBlockA;\ncolorBlockB;");
    REQUIRE(shaderBlocksNode["normal"].as<std::string>() == "normalBlockA;");
    REQUIRE(shaderBlocksNode["position"].as<std::string>() == "posBlockB;");
    REQUIRE(shaderBlocksNode["global"].as<std::string>() == "globalBlockB;\nglobalBlockC;");
    REQUIRE(shaderBlocksNode["filter"].as<std::string>() == "filterBlockC;");
}

TEST_CASE( "Style Mixing Test: propMerge Tests (recursive overWrite properties)", "[mixing][core][yaml]") {
    SceneLoader sceneLoader;

    Node node = YAML::Load(R"END(
        Node1:
            prop1:
                subProp1:
                    tag1: value1
            prop2:
                subProp3:
                    tag2: value2
        Node2:
            prop1:
                subProp1:
                    tag3: value3
            prop2:
                subProp2: value_scalar
                subProp3: value_scalar2
        Node3:
            prop1:
                subProp1:
                    tag1: value1_3
            prop2:
                subProp2: [v1, v2]
                subProp3:
                    tag4: value4
        )END");

    // NodeMix:
    //      prop1:
    //          subProp1:
    //              tag1: value1_3
    //              tag3: value3
    //      prop2:
    //          subProp2: [v1, v2]
    //          subProp3:
    //              tag4: value4
    //              tag2: value2

    Node mixedNode;

    for (auto& property : {"prop1", "prop2"}) {
        mixedNode[property] = sceneLoader.propMerge(property, { node["Node1"], node["Node2"], node["Node3"] });
    }

    REQUIRE(mixedNode["prop1"].IsMap());
    REQUIRE(mixedNode["prop1"]["subProp1"].IsMap());
    REQUIRE(mixedNode["prop1"]["subProp1"]["tag1"].as<std::string>() ==  "value1_3");
    REQUIRE(mixedNode["prop1"]["subProp1"]["tag3"].as<std::string>() ==  "value3");
    REQUIRE(mixedNode["prop2"].IsMap());
    REQUIRE(mixedNode["prop2"]["subProp2"].IsSequence());
    REQUIRE(mixedNode["prop2"]["subProp2"].size() == 2);
    REQUIRE(mixedNode["prop2"]["subProp3"].IsMap());
    REQUIRE(mixedNode["prop2"]["subProp3"].size() == 2);
    REQUIRE(mixedNode["prop2"]["subProp3"]["tag4"].as<std::string>() == "value4");
    REQUIRE(mixedNode["prop2"]["subProp3"]["tag2"].as<std::string>() == "value2");
}

TEST_CASE( "Style Mixing Test: propMerge Tests (overWrite properties)", "[mixing][core][yaml]") {
    SceneLoader sceneLoader;

    Node node = YAML::Load(R"END(
        Node1:
            prop1: value1
        Node2:
            prop1: value1_2
            prop2: value2
        Node3:
            prop1: value1_3
        )END");

    // NodeMix:
    //      prop1: value1_3
    //      prop2: value2

    Node mixedNode;
    for (auto& property : {"prop1", "prop2"}) {
        mixedNode[property] = sceneLoader.propMerge(property, { node["Node1"], node["Node2"], node["Node3"] });
    }

    REQUIRE(mixedNode["prop1"].as<std::string>() == "value1_3");
    REQUIRE(mixedNode["prop2"].as<std::string>() == "value2");

    // Verify that the original node was not modified
    REQUIRE(node["Node1"]["prop1"].as<std::string>() == "value1");
    REQUIRE(node["Node2"]["prop1"].as<std::string>() == "value1_2");
    REQUIRE(node["Node2"]["prop2"].as<std::string>() == "value2");
    REQUIRE(node["Node3"]["prop1"].as<std::string>() == "value1_3");

}

TEST_CASE( "Style Mixing Test: propOr Tests (boolean properties)", "[mixing][core][yaml]") {

    SceneLoader sceneLoader;

    Node node = YAML::Load(R"END(
        Node1:
            prop1: false
        Node2:
            prop1: true
        Node3:
            prop1: false
        )END");

    // NodeMix:
    //      prop1: true

    Node mixedNode;
    for (auto& property : {"prop1"}) {
        mixedNode[property] = sceneLoader.propOr(property, { node["Node1"], node["Node2"], node["Node3"] });
    }

    REQUIRE(mixedNode["prop1"].as<bool>());

}

