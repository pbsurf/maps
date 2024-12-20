#include "catch.hpp"

#include "mockPlatform.h"
#include "scene/importer.h"
#include "scene/scene.h"

#include <iostream>
#include <vector>

using namespace Tangram;
using namespace YAML;


struct ImportMockPlatform : public MockPlatform {
    ImportMockPlatform() {
        putMockUrlContents(Url("/root/a.yaml"), R"END(
            import: b.yaml
            value: a
            has_a: true
        )END");

        putMockUrlContents(Url("/root/b.yaml"), R"END(
            value: b
            has_b: true
        )END");

        putMockUrlContents(Url("/root/c.yaml"), R"END(
            import: [a.yaml, b.yaml]
            value: c
            has_c: true
        )END");

        putMockUrlContents(Url("/root/cycle_simple.yaml"), R"END(
            import: cycle_simple.yaml
            value: cyclic
        )END");

        putMockUrlContents(Url("/root/cycle_tricky.yaml"), R"END(
            import: imports/cycle_tricky.yaml
            has_cycle_tricky: true
        )END");

        putMockUrlContents(Url("/root/imports/cycle_tricky.yaml"), R"END(
            import: ../cycle_tricky.yaml
            has_imports_cycle_tricky: true
        )END");

        putMockUrlContents(Url("/root/urls.yaml"), R"END(
            import: imports/urls.yaml
            fonts: { fontA: { url: https://host/font.woff } }
            sources: { sourceA: { url: 'https://host/tiles/{z}/{y}/{x}.mvt' } }
            textures:
                tex1: { url: "path/to/texture.png" }
                tex2: { url: "../up_a_directory.png" }
            styles:
                styleA:
                    texture: "path/to/texture.png"
                    shaders:
                        uniforms:
                            u_tex1: "/at_root.png"
                            u_tex2: ["path/to/texture.png", tex2]
                            u_tex3: tex3
                            u_bool: true
                            u_float: 0.25
        )END");

        putMockUrlContents(Url("/root/imports/urls.yaml"), R"END(
            fonts: { fontB: [ { url: fonts/0.ttf }, { url: fonts/1.ttf } ] }
            sources: { sourceB: { url: "tiles/{z}/{y}/{x}.mvt" } }
            textures:
                tex3: { url: "in_imports.png" }
                tex4: { url: "../not_in_imports.png" }
                tex5: { url: "/at_root.png" }
            styles:
                styleB:
                    texture: "in_imports.png"
                    shaders:
                        uniforms:
                            u_tex1: "in_imports.png"
                            u_tex2: tex2
        )END");

        putMockUrlContents(Url("/root/globals.yaml"), R"END(
            fonts: { aFont: { url: global.fontUrl } }
            sources: { aSource: { url: global.sourceUrl } }
            textures: { aTexture: { url: global.textureUrl } }
            styles: { aStyle: { texture: global.textureUrl, shaders: { uniforms: { aUniform: global.textureUrl } } } }
        )END");
    }
};

TEST_CASE("Imported scenes are merged with the parent scene", "[import][core]") {
    ImportMockPlatform platform;
    Importer importer;
    auto root = importer.loadSceneData(platform, Url("/root/a.yaml"));

    CHECK(root["value"].Scalar() == "a");
    CHECK(root["has_a"].Scalar() == "true");
    CHECK(root["has_b"].Scalar() == "true");
}

TEST_CASE("Nested imports are merged recursively", "[import][core]") {
    ImportMockPlatform platform;
    Importer importer;
    auto root = importer.loadSceneData(platform, Url("/root/c.yaml"));

    CHECK(root["value"].Scalar() == "c");
    CHECK(root["has_a"].Scalar() == "true");
    CHECK(root["has_b"].Scalar() == "true");
    CHECK(root["has_c"].Scalar() == "true");
}

TEST_CASE("Imports that would start a cycle are ignored", "[import][core]") {
    ImportMockPlatform platform;
    Importer importer;
    // If import cycles aren't checked for and stopped, this call won't return.
    auto root = importer.loadSceneData(platform, Url("/root/cycle_simple.yaml"));

    // Check that the scene values were applied.
    CHECK(root["value"].Scalar() == "cyclic");
}

TEST_CASE("Tricky import cycles are ignored", "[import][core]") {
    ImportMockPlatform platform;
    Importer importer;

    // The nested import should resolve to the same path as the original file,
    // and so the importer should break the cycle.
    auto root = importer.loadSceneData(platform, Url("/root/cycle_tricky.yaml"));

    // Check that the imported scene values were merged.
    CHECK(root["has_cycle_tricky"].Scalar() == "true");
    CHECK(root["has_imports_cycle_tricky"].Scalar() == "true");
}

TEST_CASE("Scene URLs are resolved against their parent during import", "[import][core]") {
    ImportMockPlatform platform;
    Importer importer;
    auto root = importer.loadSceneData(platform, Url("/root/urls.yaml"));

    // Check that global texture URLs are resolved correctly.

    const auto& textures = root["textures"];

    CHECK(textures["tex1"]["url"].Scalar() == "/root/path/to/texture.png");
    CHECK(textures["tex2"]["url"].Scalar() == "/up_a_directory.png");
    CHECK(textures["tex3"]["url"].Scalar() == "/root/imports/in_imports.png");
    CHECK(textures["tex4"]["url"].Scalar() == "/root/not_in_imports.png");
    CHECK(textures["tex5"]["url"].Scalar() == "/at_root.png");

    // Check that "inline" texture URLs are resolved correctly.

    const auto& styleA = root["styles"]["styleA"];

    CHECK(styleA["texture"].Scalar() == "/root/path/to/texture.png");

    const auto& uniformsA = styleA["shaders"]["uniforms"];

    CHECK(uniformsA["u_tex1"].Scalar() == "/at_root.png");
    CHECK(uniformsA["u_tex2"][0].Scalar() == "/root/path/to/texture.png");
    CHECK(uniformsA["u_tex2"][1].Scalar() == "tex2");
    CHECK(uniformsA["u_bool"].Scalar() == "true");
    CHECK(uniformsA["u_float"].Scalar() == "0.25");
    CHECK(uniformsA["u_tex3"].Scalar() == "tex3");

    const auto& styleB = root["styles"]["styleB"];

    CHECK(styleB["texture"].Scalar() == "/root/imports/in_imports.png");

    const auto& uniformsB = styleB["shaders"]["uniforms"];

    CHECK(uniformsB["u_tex1"].Scalar() == "/root/imports/in_imports.png");

    // Use global textures from importing scene
    CHECK(uniformsB["u_tex2"].Scalar() == "tex2");

    // Check that data source URLs are resolved correctly.

    CHECK(root["sources"]["sourceA"]["url"].Scalar() == "https://host/tiles/{z}/{y}/{x}.mvt");
    CHECK(root["sources"]["sourceB"]["url"].Scalar() == "/root/imports/tiles/{z}/{y}/{x}.mvt");

    // Check that font URLs are resolved correctly.

    CHECK(root["fonts"]["fontA"]["url"].Scalar() == "https://host/font.woff");
    CHECK(root["fonts"]["fontB"][0]["url"].Scalar() == "/root/imports/fonts/0.ttf");
    CHECK(root["fonts"]["fontB"][1]["url"].Scalar() == "/root/imports/fonts/1.ttf");

    // We don't explicitly check that import URLs are resolved correctly because if they were not,
    // the scenes wouldn't be loaded and merged; i.e. we already test it implicitly.
}

TEST_CASE("References to globals are not treated like URLs during importing", "[import][core]") {
    ImportMockPlatform platform;
    Importer importer;
    auto root = importer.loadSceneData(platform, Url("/root/globals.yaml"));

    // Check that font global references are preserved.
    CHECK(root["fonts"]["aFont"]["url"].Scalar() == "global.fontUrl");

    // Check that data source global references are preserved.
    CHECK(root["sources"]["aSource"]["url"].Scalar() == "global.sourceUrl");

    // Check that texture global references are preserved.
    CHECK(root["textures"]["aTexture"]["url"].Scalar() == "global.textureUrl");
    CHECK(root["styles"]["aStyle"]["texture"].Scalar() == "global.textureUrl");
    CHECK(root["styles"]["aStyle"]["shaders"]["uniforms"]["aUniform"].Scalar() == "global.textureUrl");
}

TEST_CASE("Map overwrites sequence", "[import][core]") {
    ImportMockPlatform platform;
    platform.putMockUrlContents(Url("/base.yaml"), R"END(
        import: [roads.yaml, roads-labels.yaml]
    )END");

    platform.putMockUrlContents(Url("/roads.yaml"), R"END(
            filter:
                - kind: highway
                - $zoom: { min: 8 }
    )END");

    platform.putMockUrlContents(Url("/roads-labels.yaml"), R"END(
                filter: { kind: highway }
    )END");

    Importer importer;
    auto root = importer.loadSceneData(platform, Url("/base.yaml"));

    CHECK(root["filter"].IsMap());
    CHECK(root["filter"].size() == 1);
    CHECK(root["filter"]["kind"].Scalar() == "highway");
}

TEST_CASE("Sequence overwrites map", "[import][core]") {
    MockPlatform platform;
    platform.putMockUrlContents(Url("/base.yaml"), R"END(
        import: [map.yaml, sequence.yaml]
    )END");
    platform.putMockUrlContents(Url("/map.yaml"), R"END(
            a: { b: c }
    )END");

    platform.putMockUrlContents(Url("/sequence.yaml"), R"END(
            a: [ b, c]
    )END");

    Importer importer;
    auto root = importer.loadSceneData(platform, Url("/base.yaml"));

    CHECK(root["a"].IsSequence());
    CHECK(root["a"].size() == 2);
}

TEST_CASE("Scalar and null overwrite correctly", "[import][core]") {
    MockPlatform platform;
    platform.putMockUrlContents(Url("/base.yaml"), R"END(
        import: [scalar.yaml, null.yaml]
        scalar_at_end: scalar
        null_at_end: null
    )END");
    platform.putMockUrlContents(Url("/scalar.yaml"), R"END(
            null_at_end: scalar
    )END");

    platform.putMockUrlContents(Url("/null.yaml"), R"END(
            scalar_at_end: null
    )END");

    Importer importer;
    auto root = importer.loadSceneData(platform, Url("/base.yaml"));

    CHECK(root["scalar_at_end"].Scalar() == "scalar");
    CHECK(root["null_at_end"].IsNull());
}

TEST_CASE("Scene load from source string", "[import][core]") {
    MockPlatform platform;
    platform.putMockUrlContents(Url("/resource_root/scalar.yaml"), R"END(
            null_at_end: scalar
    )END");
    platform.putMockUrlContents(Url("/resource_root/null.yaml"), R"END(
            scalar_at_end: null
    )END");

    std::string base_yaml = R"END(
        import: [scalar.yaml, null.yaml]
        scalar_at_end: scalar
        null_at_end: null
    )END";

    Importer importer;
    auto root = importer.loadSceneData(platform, Url("/resource_root/"), base_yaml);

    CHECK(root["scalar_at_end"].Scalar() == "scalar");
    CHECK(root["null_at_end"].IsNull());
}

TEST_CASE("Scenes imported more than once are not mutated", "[import][core]") {
    MockPlatform platform;
    platform.putMockUrlContents(Url("/duplicate_imports_a.yaml"), R"END(
        key: value_a
    )END");

    platform.putMockUrlContents(Url("/duplicate_imports_b.yaml"), R"END(
        import: duplicate_imports_a.yaml
        key: value_b
    )END");

    platform.putMockUrlContents(Url("/duplicate_imports.yaml"), R"END(
        import: [duplicate_imports_b.yaml, duplicate_imports_a.yaml]
    )END");


    Importer importer;
    auto root = importer.loadSceneData(platform, Url("/duplicate_imports.yaml"));

    CHECK(root["key"].Scalar() == "value_a");
}
