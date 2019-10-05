#include "vkdf-skybox.hpp"

VkdfSkyBox *
vkdf_skybox_new(VkdfContext *ctx,
                VkCommandPool pool,
                glm::vec3 position,
                float scale,
                const char *img_path[6])
{
   VkdfSkyBox *skybox = g_new0(VkdfSkyBox, 1);
   skybox->pos = position;
   skybox->scale = scale;
   skybox->dirty_model_matrix = true;

   if (!vkdf_load_cube_image_from_files(ctx, pool,
                                        img_path,
                                        &skybox->image,
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                        true)) {
      g_free(skybox);
      return NULL;
   }

   return skybox;
}

glm::mat4
vkdf_skybox_compute_model_matrix(VkdfSkyBox *skybox)
{
   if (skybox->dirty_model_matrix) {
      skybox->model_matrix = glm::mat4(1.0f);
      skybox->model_matrix = glm::translate(skybox->model_matrix,
                                            skybox->pos);
      skybox->model_matrix = glm::scale(skybox->model_matrix,
                                        glm::vec3(skybox->scale));
      skybox->dirty_model_matrix = false;
   }
   return skybox->model_matrix;;
}

void
vkdf_skybox_free(VkdfContext *ctx, VkdfSkyBox *sb, bool free_image)
{
   if (free_image)
      vkdf_destroy_image(ctx, &sb->image);
   g_free(sb);
}

