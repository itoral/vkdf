#ifndef __VKDF_MODEL_H__
#define __VKDF_MODEL_H__

#include "vkdf-deps.hpp"
#include "vkdf-init.hpp"
#include "vkdf-box.hpp"
#include "vkdf-mesh.hpp"
#include "vkdf-image.hpp"

typedef struct {
   glm::vec4 diffuse;
   glm::vec4 ambient;
   glm::vec4 specular;
   float shininess;
   uint32_t diffuse_tex_count;
   uint32_t normal_tex_count;
   uint32_t specular_tex_count;
   uint32_t opacity_tex_count;
   float reflectiveness;
   float roughness;
   uint32_t padding[1]; // So the size if 16-byte aligned
} VkdfMaterial;

typedef struct {
   char *diffuse_path;
   VkdfImage diffuse;

   char *specular_path;
   VkdfImage specular;

   char *normal_path;
   VkdfImage normal;

   char *opacity_path;
   VkdfImage opacity;
} VkdfTexMaterial;

typedef struct {
   std::vector<VkdfMesh *> meshes;
   std::vector<VkdfMaterial> materials;
   std::vector<VkdfTexMaterial> tex_materials;

   // A single vertex buffer packing vertex data for all meshes, where
   // vertex data for mesh 'm' starts at byte offset 'vertex_buf_offsets[m]'
   VkdfBuffer vertex_buf;
   std::vector<VkDeviceSize> vertex_buf_offsets;

   // A single index buffer packing index data for all meshes, where
   // index data for mesh 'm' starts at byte offset 'index_buf_offsets[m]'
   VkdfBuffer index_buf;
   std::vector<VkDeviceSize> index_buf_offsets;

   // Bounding box (in model-space coordinates)
   VkdfBox box;
} VkdfModel;

VkdfModel *
vkdf_model_load(const char *file);

VkdfModel *
vkdf_model_new();

void
vkdf_model_free(VkdfContext *ctx, VkdfModel *model);

inline void
vkdf_model_add_mesh(VkdfModel *model, VkdfMesh *mesh)
{
   model->meshes.push_back(mesh);
}

inline void
vkdf_model_add_material(VkdfModel *model, VkdfMaterial *material)
{
   model->materials.push_back(*material);
}

void
vkdf_model_fill_vertex_buffers(VkdfContext *ctx,
                               VkdfModel *model,
                               bool per_mesh);

void
vkdf_model_compute_box(VkdfModel *model);

void
vkdf_model_load_textures(VkdfContext *ctx,
                         VkCommandPool pool,
                         VkdfModel *model,
                         bool color_is_srgb);

#endif
