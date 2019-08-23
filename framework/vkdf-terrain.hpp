#ifndef __VKDF_TERRAIN_H__
#define __VKDF_TERRAIN_H__

#include "vkdf-object.hpp"

typedef struct {
   VkdfObject *obj;
   uint32_t num_verts_x;
   uint32_t num_verts_z;
   bool initialized;
} VkdfTerrain;

/**
 * Returns normalized terrain height in range [-1, 1] at normalized
 * coords (x, z) in range [-1, 1].
 *
 * This function is called from the terrain constructor during the terrain
 * initialization.
 */
typedef float (*VkdfTerrainHeightFunc)(VkdfTerrain *t, float x, float z, void *data);

VkdfTerrain *
vkdf_terrain_new(VkdfContext *ctx, uint32_t num_verts_x, uint32_t num_verts_z,
                 VkdfTerrainHeightFunc hf, void *hf_data);

void
vkdf_terrain_free(VkdfContext *ctx, VkdfTerrain *t, bool free_obj);

/**
 * A VkdfTerrainHeightFunc that reads height from a heightmap texture.
 * 'data' must be a pointer to an SDL_Surface with an 8-bit ubyte heightmap
 * image.
 */
float
vkdf_terrain_height_from_height_map(VkdfTerrain *t,
                                    float x, float z, void *data);

#endif
