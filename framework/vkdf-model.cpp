#include "vkdf.hpp"

VkdfModel *
vkdf_model_new()
{
   VkdfModel *model = g_new0(VkdfModel, 1);
   model->meshes = std::vector<VkdfMesh *>();
   return model;
}

static VkdfMesh *
process_mesh(const aiScene *scene, const aiMesh *mesh)
{
   VkdfMesh *_mesh = vkdf_mesh_new();

   // Vertex data
   for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
         glm::vec3 vertex;
         vertex.x = mesh->mVertices[i].x;
         vertex.y = mesh->mVertices[i].y;
         vertex.z = mesh->mVertices[i].z;
         _mesh->vertices.push_back(vertex);

         glm::vec3 normal;
         normal.x = mesh->mNormals[i].x;
         normal.y = mesh->mNormals[i].y;
         normal.z = mesh->mNormals[i].z;
         _mesh->normals.push_back(normal);

         if (mesh->mTextureCoords[0]) {
             glm::vec2 uv;
             uv.x = mesh->mTextureCoords[0][i].x;
             uv.y = mesh->mTextureCoords[0][i].y;
            _mesh->uvs.push_back(uv);
         }
   }

   // Index data
   for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
      aiFace *face = &mesh->mFaces[i];
      for (uint32_t j = 0; j < face->mNumIndices; j++)
         _mesh->indices.push_back(face->mIndices[j]);
   }

   // Material data
   _mesh->material_idx = mesh->mMaterialIndex;

   return _mesh;
}

static void
process_node(VkdfModel *model, const aiScene *scene, const aiNode *node)
{
   for (uint32_t i = 0; i < node->mNumMeshes; i++) {
      aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
      model->meshes.push_back(process_mesh(scene, mesh));
   }

   for (uint32_t i = 0; i < node->mNumChildren; i++)
      process_node(model, scene, node->mChildren[i]);
}

static VkdfMaterial
process_material(aiMaterial *material)
{
   // FIXME: only supports solid materials for now
   VkdfMaterial _material;

   aiColor4D diffuse(0.0f, 0.0f, 0.0f, 0.0f);
   aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse);
   memcpy(&_material.diffuse, &diffuse, sizeof(diffuse));

   aiColor4D ambient(0.0f, 0.0f, 0.0f, 0.0f);
   aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, &diffuse);
   memcpy(&_material.ambient, &ambient, sizeof(ambient));

   aiColor4D specular(0.0f, 0.0f, 0.0f, 0.0f);
   aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &specular);
   memcpy(&_material.specular, &specular, sizeof(specular));

   aiColor4D shininess(0.0f, 0.0f, 0.0f, 0.0f);
   aiGetMaterialColor(material, AI_MATKEY_SHININESS, &shininess);
   memcpy(&_material.shininess, &shininess.r, sizeof(float));

   return _material;
}

static VkdfModel *
create_model_from_scene(const aiScene *scene)
{
   VkdfModel *model = vkdf_model_new();

   // Load materials
   for (uint32_t i = 0; i < scene->mNumMaterials; i++) {
      aiMaterial *material = scene->mMaterials[i];
      model->materials.push_back(process_material(material));
   }

   // Load meshes
   process_node(model, scene, scene->mRootNode);

   return model;
}

VkdfModel *
vkdf_model_load(const char *file)
{
   uint32_t flags = aiProcess_CalcTangentSpace |
                    aiProcess_Triangulate |
                    aiProcess_JoinIdenticalVertices |
                    aiProcess_SplitLargeMeshes |
                    aiProcess_OptimizeMeshes |
                    aiProcess_TransformUVCoords |
                    aiProcess_GenNormals |
                    aiProcess_SortByPType;

   const aiScene *scene = aiImportFile(file, flags);
   if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
      vkdf_fatal("Assimp failed to load model at '%s'. Error: %s.",
                 file, aiGetErrorString());

   VkdfModel *model = create_model_from_scene(scene);

   aiReleaseImport(scene);

   return model;
}

void
vkdf_model_free(VkdfContext *ctx, VkdfModel *model)
{
   for (uint32_t i = 0; i < model->meshes.size(); i++)
      vkdf_mesh_free(ctx, model->meshes[i]);

   model->meshes.clear();
   std::vector<VkdfMesh *>(model->meshes).swap(model->meshes);

   model->materials.clear();
   std::vector<VkdfMaterial>(model->materials).swap(model->materials);

   if (model->vertex_buf.buf) {
      vkDestroyBuffer(ctx->device, model->vertex_buf.buf, NULL);
      vkFreeMemory(ctx->device, model->vertex_buf.mem, NULL);
   }

   if (model->index_buf.buf) {
      vkDestroyBuffer(ctx->device, model->index_buf.buf, NULL);
      vkFreeMemory(ctx->device, model->index_buf.mem, NULL);
   }

   g_free(model);
}

static void
model_fill_vertex_buffer(VkdfContext *ctx, VkdfModel *model)
{
   assert(model->meshes.size() > 0);

   if (model->vertex_buf.buf != 0)
      return;

   VkDeviceSize vertex_data_size = 0;
   for (uint32_t m = 0; m < model->meshes.size(); m++)
      vertex_data_size += vkdf_mesh_get_vertex_data_size(model->meshes[m]);

   assert(vertex_data_size > 0);

   model->vertex_buf =
      vkdf_create_buffer(ctx,
                         0,
                         vertex_data_size,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint8_t *map;
   VK_CHECK(vkMapMemory(ctx->device, model->vertex_buf.mem,
                        0, vertex_data_size, 0, (void **) &map));

   // Interleaved per-vertex attributes (position, normal, uv)
   VkDeviceSize byte_offset = 0;
   for (uint32_t m = 0; m < model->meshes.size(); m++) {
      VkdfMesh *mesh = model->meshes[m];
      bool has_normals = mesh->normals.size() > 0;
      bool has_uv = mesh->uvs.size() > 0;

      model->vertex_buf_offsets.push_back(byte_offset);

      for (uint32_t i = 0; i < mesh->vertices.size(); i++) {
         uint32_t elem_size = sizeof(mesh->vertices[0]);
         memcpy(map + byte_offset, &mesh->vertices[i], elem_size);
         byte_offset += elem_size;

         if (has_normals) {
            elem_size = sizeof(mesh->normals[0]);
            memcpy(map + byte_offset, &mesh->normals[i], elem_size);
            byte_offset += elem_size;
         }

         if (has_uv) {
            elem_size = sizeof(mesh->uvs[0]);
            memcpy(map + byte_offset, &mesh->uvs[i], elem_size);
            byte_offset += elem_size;
         }
      }
   }

   VkMappedMemoryRange range;
   range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
   range.pNext = NULL;
   range.memory = model->vertex_buf.mem;
   range.offset = 0;
   range.size = vertex_data_size;
   VK_CHECK(vkFlushMappedMemoryRanges(ctx->device, 1, &range));

   vkUnmapMemory(ctx->device, model->vertex_buf.mem);
}

static void
model_fill_index_buffer(VkdfContext *ctx, VkdfModel *model)
{
   assert(model->meshes.size() > 0);

   if (model->index_buf.buf != 0)
      return;

   VkDeviceSize index_data_size = 0;
   for (uint32_t m = 0; m < model->meshes.size(); m++)
      index_data_size += vkdf_mesh_get_index_data_size(model->meshes[m]);

   assert(index_data_size > 0);

   model->index_buf =
      vkdf_create_buffer(ctx,
                         0,
                         index_data_size,
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint8_t *map;
   VK_CHECK(vkMapMemory(ctx->device, model->index_buf.mem,
                        0, index_data_size, 0, (void **) &map));

   VkDeviceSize byte_offset = 0;
   for (uint32_t m = 0; m < model->meshes.size(); m++) {
      VkdfMesh *mesh = model->meshes[m];
      VkDeviceSize mesh_index_data_size = vkdf_mesh_get_index_data_size(mesh);

      model->index_buf_offsets.push_back(byte_offset);

      memcpy(map + byte_offset, &mesh->indices[0], mesh_index_data_size);
      byte_offset += mesh_index_data_size;
   }

   VkMappedMemoryRange range;
   range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
   range.pNext = NULL;
   range.memory = model->index_buf.mem;
   range.offset = 0;
   range.size = index_data_size;
   VK_CHECK(vkFlushMappedMemoryRanges(ctx->device, 1, &range));

   vkUnmapMemory(ctx->device, model->index_buf.mem);
}

/**
 * Creates vertex buffers and populates them with vertex data from all the
 * meshes in the model. If 'per_mesh' is TRUE, then each mesh will have
 * its own vertex/index buffer (in model->mesh->vertex/index_buf), otherwise,
 * there is a single vertex/index buffer owned by the model
 * (model->vertex/index_buf)itself that packs vertex and index data for all
 * meshes.
 */
void
vkdf_model_fill_vertex_buffers(VkdfContext *ctx,
                               VkdfModel *model,
                               bool per_mesh)
{
   if (per_mesh) {
      for (uint32_t i = 0; i < model->meshes.size(); i++) {
         vkdf_mesh_fill_vertex_buffer(ctx, model->meshes[i]);
         vkdf_mesh_fill_index_buffer(ctx, model->meshes[i]);
      }
   } else {
      model_fill_vertex_buffer(ctx, model);
      model_fill_index_buffer(ctx, model);
   }
}

