#ifndef __VKDF_MODEL_H__
#define __VKDF_MODEL_H__

#include "vkdf-deps.hpp"
#include "vkdf-init.hpp"
#include "vkdf-box.hpp"
#include "vkdf-mesh.hpp"
#include "vkdf-image.hpp"

/* WARNING: changes to this struct need to be applied to lighting.glsl too */
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
   float emission;
   uint32_t padding[0]; // So the size if 16-byte aligned
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

   bool materials_dirty;

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

   // If this is TRUE, then collision against this model is only tested
   // against the meshes indexed in this array
   bool use_collision_meshes;
   std::vector<uint32_t> collision_meshes;
} VkdfModel;

VkdfModel *
vkdf_model_load(const char *file,
                bool load_uvs = true,
                bool load_tangents = true);

VkdfModel *
vkdf_model_new();

VkdfModel *
vkdf_cube_model_new(VkdfContext *ctx,
                    bool include_uvs = false,
                    bool include_tangents = false);

VkdfModel *
vkdf_sphere_model_new(VkdfContext *ctx);

VkdfModel *
vkdf_cone_model_new(VkdfContext *ctx);

void
vkdf_model_free(VkdfContext *ctx, VkdfModel *model,
                bool free_material_resources = true);

inline void
vkdf_model_add_mesh(VkdfModel *model, VkdfMesh *mesh)
{
   model->meshes.push_back(mesh);
}

inline void
vkdf_model_add_material(VkdfModel *model, VkdfMaterial *material)
{
   assert(material->shininess >= 1.0f);
   model->materials.push_back(*material);
   model->materials_dirty = true;
}

inline void
vkdf_model_add_texture_material(VkdfModel *model,
                                VkdfMaterial *material,
                                VkdfTexMaterial *tex_material)
{
   vkdf_model_add_material(model, material);
   model->tex_materials.push_back(*tex_material);
}

void
vkdf_model_fill_vertex_buffers(VkdfContext *ctx,
                               VkdfModel *model,
                               bool per_mesh);

void
vkdf_model_compute_box(VkdfModel *model);

inline void
vkdf_model_add_collison_mesh(VkdfModel *model, uint32_t mesh_idx)
{
   assert(mesh_idx < model->meshes.size());
   model->use_collision_meshes = true;
   model->collision_meshes.push_back(mesh_idx);
}

inline bool
vkdf_model_uses_collison_meshes(VkdfModel *model)
{
   return model->use_collision_meshes;
}

void
vkdf_model_load_textures(VkdfContext *ctx,
                         VkCommandPool pool,
                         VkdfModel *model,
                         bool color_is_srgb);

#endif
