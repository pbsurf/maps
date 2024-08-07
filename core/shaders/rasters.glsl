#if defined(TANGRAM_FRAGMENT_SHADER) || defined(TANGRAM_VERTEX_RASTERS)

#if !defined(TANGRAM_RASTER_PRECISION) || !defined(GL_ES)
#undef TANGRAM_RASTER_PRECISION
#define TANGRAM_RASTER_PRECISION
#endif

uniform TANGRAM_RASTER_PRECISION sampler2D u_rasters[TANGRAM_NUM_RASTER_SOURCES];
uniform vec2 u_raster_sizes[TANGRAM_NUM_RASTER_SOURCES];
uniform vec3 u_raster_offsets[TANGRAM_NUM_RASTER_SOURCES];

#define adjustRasterUV(raster_index, uv) ((uv) * u_raster_offsets[raster_index].z + u_raster_offsets[raster_index].xy)

#define currentRasterUV(raster_index) (adjustRasterUV(raster_index, v_modelpos_base_zoom.xy))

#define currentRasterPixel(raster_index) (currentRasterUV(raster_index) * rasterPixelSize(raster_index))

#define sampleRasterAtPixel(raster_index, pixel) (texture2D(u_rasters[raster_index], (pixel) / rasterPixelSize(raster_index)))

#define sampleRaster(raster_index) (texture2D(u_rasters[raster_index], currentRasterUV(raster_index)))

#define rasterPixelSize(raster_index) (u_raster_sizes[raster_index])

#endif
