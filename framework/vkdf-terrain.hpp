#ifndef __VKDF_TERRAIN_H__
#define __VKDF_TERRAIN_H__

#include "vkdf-object.hpp"
#include "vkdf-box.hpp"

typedef struct _VkdfTerrain VkdfTerrain;

/**
 * Returns normalized terrain height in range [-1, 1] at vertex coordinates
 * (x, z) in range ([0, num_verts_x - 1], [0, num_verts_z - 1].
 *
 * This function is called from the terrain constructor during the terrain
 * initialization.
 */
typedef float (*VkdfTerrainHeightFunc)(VkdfTerrain *t, uint32_t x, uint32_t z, void *data);

struct _VkdfTerrain {
   VkdfObject *obj;
   uint32_t num_verts_x;
   uint32_t num_verts_z;
   float uv_scale_x;
   float uv_scale_z;
   VkdfTerrainHeightFunc hf;
   void *hf_data;
   float max_height;
   bool initialized;
};

VkdfTerrain *
vkdf_terrain_new(VkdfContext *ctx,
                 uint32_t num_verts_x, uint32_t num_verts_z,
                 float uv_scale_x, float uv_scale_z,
                 VkdfTerrainHeightFunc hf, void *hf_data);

void
vkdf_terrain_free(VkdfContext *ctx, VkdfTerrain *t,
                  bool free_obj, bool free_materials);

/**
 * A VkdfTerrainHeightFunc that reads height from a heightmap texture.
 * 'data' must be a pointer to an SDL_Surface with an 8-bit ubyte heightmap
 * image.
 */
float
vkdf_terrain_height_from_height_map(VkdfTerrain *t,
                                    uint32_t x, uint32_t z, void *data);

float
vkdf_terrain_get_height_at(VkdfTerrain *t, float x, float z);

bool
vkdf_terrain_check_collision(VkdfTerrain *t,
                             VkdfBox *box,
                             float *collision_height);

#endif
