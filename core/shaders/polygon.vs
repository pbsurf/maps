#pragma tangram: extensions

#ifdef GL_ES
precision highp float;
#endif

#pragma tangram: defines

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normal_matrix;
uniform vec4 u_tile_origin;
uniform vec3 u_map_position;
uniform vec2 u_resolution;
uniform float u_time;
uniform float u_meters_per_pixel;
uniform float u_device_pixel_ratio;
uniform float u_proxy_depth;

#pragma tangram: uniforms

#ifdef TANGRAM_RASTER_STYLE
    uniform float u_order;
    attribute vec2 a_position;
#else
    attribute vec4 a_position;
    attribute vec4 a_color;
    attribute vec3 a_normal;

    varying vec4 v_color;
#endif

#ifdef TANGRAM_USE_TEX_COORDS
    attribute vec2 a_texcoord;
    varying vec2 v_texcoord;
#endif

#ifdef TANGRAM_FEATURE_SELECTION
    // Make sure lighting is a no-op for feature selection pass
    #undef TANGRAM_LIGHTING_VERTEX

    attribute vec4 a_selection_color;
    varying vec4 v_selection_color;
#endif

varying vec4 v_world_position;
varying vec4 v_position;
varying vec3 v_normal;

#ifdef TANGRAM_LIGHTING_VERTEX
    varying vec4 v_lighting;
#endif

#define UNPACK_POSITION(x) (x / 8192.0)

vec4 worldPosition() {
    return v_world_position;
}

vec3 worldNormal() {
    #ifdef TANGRAM_RASTER_STYLE
        return vec3(0.0, 0.0, 1.0);
    #else
        return a_normal;
    #endif
}

vec4 modelPositionBaseZoom() {
    #ifdef TANGRAM_RASTER_STYLE
        return vec4(UNPACK_POSITION(a_position.xy), 0.0, 1.0);
    #else
        return vec4(UNPACK_POSITION(a_position.xyz), 1.0);
    #endif
}

vec4 modelPosition() {
    return vec4(modelPositionBaseZoom().xyz * exp2(u_tile_origin.z - u_tile_origin.w), 1.0);
}

#ifdef TANGRAM_MODEL_POSITION_BASE_ZOOM_VARYING
    varying vec4 v_modelpos_base_zoom;
#endif

#pragma tangram: material
#pragma tangram: lighting
#pragma tangram: raster
#pragma tangram: global

void main() {

    vec4 position = modelPositionBaseZoom();

    #ifdef TANGRAM_FEATURE_SELECTION
        v_selection_color = a_selection_color;
        // Skip non-selectable meshes
        if (v_selection_color == vec4(0.0)) {
            gl_Position = vec4(0.0);
            return;
        }
    #else
        // Initialize globals
        #pragma tangram: setup
    #endif

    // assign here to allow blocks to modify
    float proxy = u_proxy_depth;
    float depth_shift = 0.0;

    #ifdef TANGRAM_RASTER_STYLE
        float layer = u_order;
    #else
        float layer = a_position.w;
        v_color = a_color;
    #endif

    #ifdef TANGRAM_USE_TEX_COORDS
        v_texcoord = a_texcoord;
    #endif

    #ifdef TANGRAM_MODEL_POSITION_BASE_ZOOM_VARYING
        v_modelpos_base_zoom = modelPositionBaseZoom();
    #endif

    v_normal = normalize(u_normal_matrix * worldNormal());

    // Transform position into meters relative to map center
    position = u_model * position;

    // World coordinates for 3d procedural textures
    vec4 local_origin = vec4(u_map_position.xy, 0., 0.);
    #ifdef TANGRAM_WORLD_POSITION_WRAP
        local_origin = mod(local_origin, TANGRAM_WORLD_POSITION_WRAP);
    #endif
    v_world_position = position + local_origin;

    // Modify position before lighting and camera projection
    #pragma tangram: position

    // Set position varying to the camera-space vertex position
    v_position = u_view * position;

    #if defined(TANGRAM_LIGHTING_VERTEX)
        // Modify normal before lighting
        vec3 normal = v_normal;
        #pragma tangram: normal

        v_lighting = calculateLighting(v_position.xyz, normal, vec4(1.));
    #endif

    gl_Position = u_proj * v_position;

    // Proxy tiles are placed deeper in the depth buffer than non-proxy tiles
    gl_Position.z += (proxy - layer) * (TANGRAM_DEPTH_DELTA * gl_Position.w + depth_shift);
}
