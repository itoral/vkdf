#include "vkdf.hpp"

VkdfMesh *
vkdf_mesh_new(VkPrimitiveTopology primitive)
{
   VkdfMesh *mesh = g_new0(VkdfMesh, 1);

   mesh->active = true;

   mesh->vertices = std::vector<glm::vec3>();
   mesh->normals = std::vector<glm::vec3>();
   mesh->uvs = std::vector<glm::vec2>();

   mesh->indices = std::vector<uint32_t>();

   mesh->material_idx = -1;

   mesh->primitive = primitive;

   return mesh;
}

VkdfMesh *
vkdf_cube_mesh_new(VkdfContext *ctx)
{
   static glm::vec3 vertices[] = {
      // Front
      glm::vec3(-1.0f, -1.0f,  1.0f),
      glm::vec3( 1.0f, -1.0f,  1.0f),
      glm::vec3(-1.0f,  1.0f,  1.0f),
      glm::vec3(-1.0f,  1.0f,  1.0f),
      glm::vec3( 1.0f, -1.0f,  1.0f),
      glm::vec3( 1.0f,  1.0f,  1.0f),

      // Back
      glm::vec3(-1.0f, -1.0f, -1.0f),
      glm::vec3(-1.0f,  1.0f, -1.0f),
      glm::vec3( 1.0f, -1.0f, -1.0f),
      glm::vec3( 1.0f, -1.0f, -1.0f),
      glm::vec3(-1.0f,  1.0f, -1.0f),
      glm::vec3( 1.0f,  1.0f, -1.0f),

      // Left
      glm::vec3(-1.0f, -1.0f, -1.0f),
      glm::vec3(-1.0f, -1.0f,  1.0f),
      glm::vec3(-1.0f,  1.0f, -1.0f),
      glm::vec3(-1.0f,  1.0f, -1.0f),
      glm::vec3(-1.0f, -1.0f,  1.0f),
      glm::vec3(-1.0f,  1.0f,  1.0f),

      // Right
      glm::vec3( 1.0f, -1.0f,  1.0f),
      glm::vec3( 1.0f, -1.0f, -1.0f),
      glm::vec3( 1.0f,  1.0f,  1.0f),
      glm::vec3( 1.0f,  1.0f,  1.0f),
      glm::vec3( 1.0f, -1.0f, -1.0f),
      glm::vec3( 1.0f,  1.0f, -1.0f),

      // Top
      glm::vec3(-1.0f,  1.0f,  1.0f),
      glm::vec3( 1.0f,  1.0f,  1.0f),
      glm::vec3(-1.0f,  1.0f, -1.0f),
      glm::vec3(-1.0f,  1.0f, -1.0f),
      glm::vec3( 1.0f,  1.0f,  1.0f),
      glm::vec3( 1.0f,  1.0f, -1.0f),

      // Bottom
      glm::vec3(-1.0f, -1.0f,  1.0f),
      glm::vec3(-1.0f, -1.0f, -1.0f),
      glm::vec3( 1.0f, -1.0f,  1.0f),
      glm::vec3( 1.0f, -1.0f,  1.0f),
      glm::vec3(-1.0f, -1.0f, -1.0f),
      glm::vec3( 1.0f, -1.0f, -1.0f),
   };

   static glm::vec3 face_normals[] = {
      glm::vec3( 0.0f,  0.0f,  1.0f), // Front
      glm::vec3( 0.0f,  0.0f, -1.0f), // Back
      glm::vec3(-1.0f,  0.0f,  0.0f), // Left
      glm::vec3( 1.0f,  0.0f,  0.0f), // Right
      glm::vec3( 0.0f,  1.0f,  0.0f), // Top
      glm::vec3( 0.0f, -1.0f,  0.0f), // Bottom
   };

   VkdfMesh *mesh = vkdf_mesh_new(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
   for (uint32_t i = 0; i < 36; i++) {
      mesh->vertices.push_back(vertices[i]);
      mesh->normals.push_back(face_normals[i / 6]);
   }

   vkdf_mesh_compute_size(mesh);

   return mesh;
}

VkdfMesh *
vkdf_tile_mesh_new(VkdfContext *ctx)
{
   static glm::vec3 vertices[] = {
      glm::vec3(-1.0f,  0.0f,  1.0f),
      glm::vec3( 1.0f,  0.0f,  1.0f),
      glm::vec3(-1.0f,  0.0f, -1.0f),
      glm::vec3(-1.0f,  0.0f, -1.0f),
      glm::vec3( 1.0f,  0.0f,  1.0f),
      glm::vec3( 1.0f,  0.0f, -1.0f),
   };

   VkdfMesh *mesh = vkdf_mesh_new(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
   for (uint32_t i = 0; i < 6; i++) {
      mesh->vertices.push_back(vertices[i]);
      mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
   }

   vkdf_mesh_compute_size(mesh);

   return mesh;
}

VkdfMesh *
vkdf_2d_tile_mesh_new(VkdfContext *ctx)
{
   static glm::vec3 vertices[] = {
      glm::vec3(-1.0f, -1.0f, 0.0f),
      glm::vec3( 1.0f, -1.0f, 0.0f),
      glm::vec3(-1.0f,  1.0f, 0.0f),
      glm::vec3( 1.0f,  1.0f, 0.0f),
   };

   static glm::vec2 uvs[] = {
      glm::vec2(0.0f, 1.0f),
      glm::vec2(1.0f, 1.0f),
      glm::vec2(0.0f, 0.0f),
      glm::vec2(1.0f, 0.0f),
   };

   VkdfMesh *mesh = vkdf_mesh_new(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
   for (uint32_t i = 0; i < 4; i++) {
      mesh->vertices.push_back(vertices[i]);
      mesh->uvs.push_back(uvs[i]);
   }

   vkdf_mesh_compute_size(mesh);

   return mesh;
}

void
vkdf_mesh_free(VkdfContext *ctx, VkdfMesh *mesh)
{
   mesh->vertices.clear();
   std::vector<glm::vec3>(mesh->vertices).swap(mesh->vertices);

   mesh->normals.clear();
   std::vector<glm::vec3>(mesh->normals).swap(mesh->normals);

   mesh->tangents.clear();
   std::vector<glm::vec3>(mesh->tangents).swap(mesh->tangents);

   mesh->bitangents.clear();
   std::vector<glm::vec3>(mesh->bitangents).swap(mesh->bitangents);

   mesh->uvs.clear();
   std::vector<glm::vec2>(mesh->uvs).swap(mesh->uvs);

   mesh->indices.clear();
   std::vector<uint32_t>(mesh->indices).swap(mesh->indices);

   if (mesh->vertex_buf.buf) {
      vkDestroyBuffer(ctx->device, mesh->vertex_buf.buf, NULL);
      vkFreeMemory(ctx->device, mesh->vertex_buf.mem, NULL);
   }

   if (mesh->index_buf.buf) {
      vkDestroyBuffer(ctx->device, mesh->index_buf.buf, NULL);
      vkFreeMemory(ctx->device, mesh->index_buf.mem, NULL);
   }

   g_free(mesh);
}

static inline uint32_t
get_vertex_data_stride(VkdfMesh *mesh)
{
   uint32_t has_vertices = MIN2(mesh->vertices.size(), 1);
   uint32_t has_normals = MIN2(mesh->normals.size(), 1);
   uint32_t has_tangents = MIN2(mesh->tangents.size(), 1) * has_normals;
   uint32_t has_bitangents = MIN2(mesh->bitangents.size(), 1) * has_normals;
   uint32_t has_uv = MIN2(mesh->uvs.size(), 1);
   uint32_t has_material = mesh->material_idx == -1 ? 0 : 1;

   assert(has_vertices);

   return has_vertices   * sizeof(glm::vec3) +
          has_normals    * sizeof(glm::vec3) +
          has_tangents   * sizeof(glm::vec3) +
          has_bitangents * sizeof(glm::vec3) +
          has_uv         * sizeof(glm::vec2) +
          has_material   * sizeof(int32_t);
}

uint32_t
vkdf_mesh_get_vertex_data_stride(VkdfMesh *mesh)
{
   return get_vertex_data_stride(mesh);
}

static inline VkDeviceSize
get_vertex_data_size(VkdfMesh *mesh)
{
   uint32_t vertex_count = mesh->vertices.size();
   uint32_t normal_count = mesh->normals.size();
   uint32_t tangent_count = mesh->tangents.size() * MIN2(normal_count, 1);
   uint32_t bitangent_count = mesh->bitangents.size() * MIN2(normal_count, 1);
   uint32_t uv_count = mesh->uvs.size();
   uint32_t material_count = (mesh->material_idx == -1 ? 0 : 1) * vertex_count;

   assert(vertex_count > 0 &&
          (vertex_count == normal_count || normal_count == 0) &&
          (normal_count == tangent_count || tangent_count == 0) &&
          (normal_count == bitangent_count || bitangent_count == 0) &&
          (tangent_count == bitangent_count) &&
          (vertex_count == uv_count || uv_count == 0));

   return vertex_count    * sizeof(glm::vec3) + // pos
          normal_count    * sizeof(glm::vec3) + // normal
          tangent_count   * sizeof(glm::vec3) + // tangent
          bitangent_count * sizeof(glm::vec3) + // bitangent
          uv_count        * sizeof(glm::vec2) + // uv
          material_count  * sizeof(int32_t);    // material
}

VkDeviceSize
vkdf_mesh_get_vertex_data_size(VkdfMesh *mesh)
{
   return get_vertex_data_size(mesh);
}

/**
 * Allocates a device buffer and populates it with vertex data from the
 * mesh in interleaved fashion.
 */
void
vkdf_mesh_fill_vertex_buffer(VkdfContext *ctx, VkdfMesh *mesh)
{
   // Interleaved per-vertex attributes (position, normal, uv, material)
   if (mesh->vertex_buf.buf != 0)
      return;

   VkDeviceSize vertex_data_size = get_vertex_data_size(mesh);

   mesh->vertex_buf =
      vkdf_create_buffer(ctx,
                         0,
                         vertex_data_size,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   bool has_normals = mesh->normals.size() > 0;
   bool has_tangents = has_normals && mesh->tangents.size() > 0;
   bool has_bitangents = has_normals && mesh->bitangents.size() > 0;
   bool has_uv = mesh->uvs.size() > 0;
   bool has_material = mesh->material_idx != -1;

   uint8_t *map;
   vkdf_memory_map(ctx, mesh->vertex_buf.mem,
                   0, vertex_data_size, (void **) &map);

   for (uint32_t i = 0; i < mesh->vertices.size(); i++) {
      uint32_t elem_size = sizeof(mesh->vertices[0]);
      memcpy(map, &mesh->vertices[i], elem_size);
      map += elem_size;

      if (has_normals) {
         elem_size = sizeof(mesh->normals[0]);
         memcpy(map, &mesh->normals[i], elem_size);
         map += elem_size;

         if (has_tangents) {
            assert(has_bitangents);

            elem_size = sizeof(mesh->tangents[0]);
            memcpy(map, &mesh->tangents[i], elem_size);
            map += elem_size;

            elem_size = sizeof(mesh->bitangents[0]);
            memcpy(map, &mesh->bitangents[i], elem_size);
            map += elem_size;
         }
      }

      if (has_uv) {
         elem_size = sizeof(mesh->uvs[0]);
         memcpy(map, &mesh->uvs[i], elem_size);
         map += elem_size;
      }

      if (has_material) {
         elem_size = sizeof(mesh->material_idx);
         memcpy(map, &mesh->material_idx, elem_size);
         map += elem_size;
      }
   }

   vkdf_memory_unmap(ctx, mesh->vertex_buf.mem, mesh->vertex_buf.mem_props,
                     0, vertex_data_size);
}

static inline VkDeviceSize
get_index_data_size(VkdfMesh *mesh)
{
   return mesh->indices.size() * sizeof(uint32_t);
}

VkDeviceSize
vkdf_mesh_get_index_data_size(VkdfMesh *mesh)
{
   return get_index_data_size(mesh);
}

/**
 * Allocates a device buffer and populates it with index data from the mesh
 */
void
vkdf_mesh_fill_index_buffer(VkdfContext *ctx, VkdfMesh *mesh)
{
   if (mesh->index_buf.buf != 0)
      return;

   VkDeviceSize index_data_size = get_index_data_size(mesh);
   if (index_data_size == 0)
      return;

   mesh->index_buf =
      vkdf_create_buffer(ctx,
                         0,
                         index_data_size,
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint8_t *map;
   vkdf_memory_map(ctx, mesh->index_buf.mem,
                   0, index_data_size, (void **) &map);

   memcpy(map, &mesh->indices[0], index_data_size);

   vkdf_memory_unmap(ctx, mesh->index_buf.mem, mesh->index_buf.mem_props,
                     0, index_data_size);
}

void
vkdf_mesh_compute_size(VkdfMesh *mesh)
{
   mesh->size.min = glm::vec3(999999999.0f, 999999999.0f, 999999999.0f);
   mesh->size.max = glm::vec3(-999999999.0f, -999999999.0f, -999999999.0f);

   for (uint32_t i = 0; i < mesh->vertices.size(); i++) {
      glm::vec3 *v = &mesh->vertices[i];

      if (v->x < mesh->size.min.x)
         mesh->size.min.x = v->x;
      else if (v->x > mesh->size.max.x)
         mesh->size.max.x = v->x;

      if (v->y < mesh->size.min.y)
         mesh->size.min.y = v->y;
      else if (v->y > mesh->size.max.y)
         mesh->size.max.y = v->y;

      if (v->z < mesh->size.min.z)
         mesh->size.min.z = v->z;
      else if (v->z > mesh->size.max.z)
         mesh->size.max.z = v->z;
   }

   mesh->size.w = mesh->size.max.x - mesh->size.min.x;
   mesh->size.h = mesh->size.max.y - mesh->size.min.y;
   mesh->size.d = mesh->size.max.z - mesh->size.min.z;

   mesh->pos.x = (mesh->size.max.x + mesh->size.min.x) / 2.0f;
   mesh->pos.y = (mesh->size.max.y + mesh->size.min.y) / 2.0f;
   mesh->pos.z = (mesh->size.max.z + mesh->size.min.z) / 2.0f;
}
