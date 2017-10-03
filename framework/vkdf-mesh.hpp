#ifndef __VKDF_MESH_H__
#define __VKDF_MESH_H__

#include <vector>

typedef struct {
   bool active;

   std::vector<glm::vec3> vertices;
   std::vector<glm::vec3> normals;
   std::vector<glm::vec2> uvs;
   std::vector<uint32_t> indices;

   int32_t material_idx;
   
   VkdfBuffer vertex_buf;
   VkdfBuffer index_buf;

   VkPrimitiveTopology primitive;

   struct {
      float w;
      float h;
      float d;
      glm::vec3 min;
      glm::vec3 max;
   } size;
} VkdfMesh;

VkdfMesh *
vkdf_mesh_new(VkPrimitiveTopology primitive);

VkdfMesh *
vkdf_cube_mesh_new(VkdfContext *ctx);

VkdfMesh *
vkdf_tile_mesh_new(VkdfContext *ctx);

VkdfMesh *
vkdf_2d_tile_mesh_new(VkdfContext *ctx);

void
vkdf_mesh_free(VkdfContext *ctx, VkdfMesh *mesh);

inline void
vkdf_mesh_add_solid_vertex(VkdfMesh *mesh,
                           const glm::vec3 &pos,
                           const glm::vec3 &normal)
{
   mesh->vertices.push_back(pos);
   mesh->normals.push_back(normal);
}

inline void
vkdf_mesh_add_textured_vertex(VkdfMesh *mesh,
                              const glm::vec3 &pos,
                              const glm::vec3 &normal,
                              const glm::vec2 &uv)
{
   mesh->vertices.push_back(pos);
   mesh->normals.push_back(normal);
   mesh->uvs.push_back(uv);
}

inline VkPrimitiveTopology
vkdf_mesh_get_primitive(VkdfMesh *mesh)
{
   return mesh->primitive;
}

VkDeviceSize
vkdf_mesh_get_vertex_data_size(VkdfMesh *mesh);

uint32_t
vkdf_mesh_get_vertex_data_stride(VkdfMesh *mesh);

void
vkdf_mesh_fill_vertex_buffer(VkdfContext *ctx, VkdfMesh *mesh);

VkDeviceSize
vkdf_mesh_get_index_data_size(VkdfMesh *mesh);

void
vkdf_mesh_fill_index_buffer(VkdfContext *ctx, VkdfMesh *mesh);

void
vkdf_mesh_compute_size(VkdfMesh *mesh);

#endif

