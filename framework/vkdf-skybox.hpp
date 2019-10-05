#ifndef __VKDF_SKYBOX_H__
#define __VKDF_SKYBOX_H__

#include "vkdf-image.hpp"

typedef struct {
   glm::vec3 pos;
   float scale;
   VkdfImage image;

   glm::mat4 model_matrix;
   bool dirty_model_matrix;
} VkdfSkyBox;

VkdfSkyBox *
vkdf_skybox_new(VkdfContext *ctx,
                VkCommandPool pool,
                glm::vec3 position,
                float scale,
                const char *img_path[6]);

inline void
vkdf_skybox_set_position(VkdfSkyBox *skybox, glm::vec3 position)
{
   skybox->pos = position;
   skybox->dirty_model_matrix = true;
}

glm::mat4
vkdf_skybox_compute_model_matrix(VkdfSkyBox *skybox);

void
vkdf_skybox_free(VkdfContext *ctx, VkdfSkyBox *sb, bool free_image);

#endif
