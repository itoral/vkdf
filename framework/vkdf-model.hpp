#ifndef __VKDF_MODEL_H__
#define __VKDF_MODEL_H__

typedef struct {
   glm::vec4 diffuse;
   glm::vec4 ambient;
   glm::vec4 specular;
   float shininess;
   float padding[3]; // So the size if 16-byte aligned
} VkdfMaterial;

typedef struct {
   std::vector<VkdfMesh *> meshes;
   std::vector<VkdfMaterial> materials;

   // A single vertex buffer packing vertex data for all meshes, where
   // vertex data for mesh 'm' starts at byte offset 'vertex_buf_offsets[m]'
   VkdfBuffer vertex_buf;
   std::vector<VkDeviceSize> vertex_buf_offsets;

   // A single index buffer packing index data for all meshes, where
   // index data for mesh 'm' starts at byte offset 'index_buf_offsets[m]'
   VkdfBuffer index_buf;
   std::vector<VkDeviceSize> index_buf_offsets;
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

void
vkdf_model_fill_vertex_buffers(VkdfContext *ctx,
                               VkdfModel *model,
                               bool per_mesh);

#endif
