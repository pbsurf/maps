/* jshint esnext:true */

// Mapbox-GL style to Tangram scene converter - based on https://gitlab.com/stevage/mapbox2tangram
// edit this file as needed for style being converted

var mb2mz = require('./mb2mz');
var toyaml = require('./toyaml');
//let yaml = require('js-yaml');
//var schemaCross = require('./schemaCross');

function processStyle(filename, fixup, outname='out') {
    function copy(o) { return JSON.parse(JSON.stringify(o)); }
    //let style =schemaCross.convertSchema('mapbox', 'mapzen', require(filename));
    let style = require(filename);

    // first copy avoids messing up source object. second strips out undefined values.
    let output = copy(mb2mz.toTangram(copy(style), { globalColors: true }));

    // apply fixups to output
    fixup(output)

    // write yaml
    let yamlout = toyaml.dump(output, {extraLines: 2, flowLevel: 8,
        alwaysFlow: ['size', 'width', 'dash', 'buffer', 'offset', 'placement', 'alpha', 'data']});
    //yaml.safeDump(output, {"flowLevel": 5})
    require('fs').writeFileSync(`./${outname}.yaml`, yamlout);
    console.log(`Wrote ./${outname}.yaml`);
    //require('fs').writeFileSync('./compare/in.json', JSON.stringify(style,undefined,4));
}

function fix_osm_bright(obj) {
    for (var key in obj) {
        if(key == "text_source") {
            if(obj[key] == "name:latin")
                obj[key] = "global.latin_name";
            else if(obj[key] == "name:latin\nname:nonlatin")
                obj[key] = "global.names_two_lines";
            else if(obj[key] == "name:latin name:nonlatin")
                obj[key] = "global.names_one_line";
        } else if(key == "family" && obj[key].length > 0) {
          if(obj[key][0].startsWith("Noto Sans"))
              obj[key] = "global.font_sans";
          if(obj[key][0].endsWith("Italic"))
              obj["style"] = "italic";
          if(obj[key][0].endsWith("Bold"))
              obj["weight"] = "bold";

        } else if(key == "sprite") {
              obj["texture"] = "osm-bright";

        } else if (obj[key] !== null && typeof(obj[key])=="object") {
            fixup(obj[key]);
        }
    }

    obj.textures = { "osm-bright": { url: "osm-bright.svg", density: 2 } };
    obj.global.latin_name = "function() { return feature['name:latin'] || feature.name_en || feature.name; }";
    obj.global.names_two_lines =
        "function() { const nl = feature['name:nonlatin']; return global.latin_name() + (nl ? '\n' + nl : ''); }";
    obj.global.names_one_line =
        "function() { const nl = feature['name:nonlatin']; return global.latin_name() + (nl ? ' ' + nl : ''); }";
}

//processStyle('../../osm-bright-gl-style/style.json', fix_osm_bright, 'osm-bright');

function fix_colorful(obj) { }

processStyle('./colorful.json', fix_colorful, 'sb-colorful');
