#include "vkdf-model.hpp"
#include "vkdf-memory.hpp"

VkdfModel *
vkdf_model_new()
{
   VkdfModel *model = g_new0(VkdfModel, 1);
   model->meshes = std::vector<VkdfMesh *>();
   return model;
}

static VkdfModel *
create_model_with_mesh(VkdfContext *ctx, VkdfMesh *mesh)
{
   mesh->material_idx = -1;
   VkdfModel *model = vkdf_model_new();
   vkdf_model_add_mesh(model, mesh);
   return model;
}

VkdfModel *
vkdf_cube_model_new(VkdfContext *ctx, bool include_uvs)
{
   VkdfMesh *mesh = vkdf_cube_mesh_new(ctx, include_uvs);
   return create_model_with_mesh(ctx, mesh);
}

VkdfModel *
vkdf_sphere_model_new(VkdfContext *ctx)
{
   const char *vkdf_path = getenv("VKDF_HOME");
   char *path = g_strdup_printf("%s/data/models/sphere.obj", vkdf_path);
   VkdfModel *model = vkdf_model_load(path);
   model->materials.clear();
   g_free(path);
   return model;
}

VkdfModel *
vkdf_cone_model_new(VkdfContext *ctx)
{
   const char *vkdf_path = getenv("VKDF_HOME");
   char *path = g_strdup_printf("%s/data/models/cone.obj", vkdf_path);
   VkdfModel *model = vkdf_model_load(path);
   model->materials.clear();
   g_free(path);
   return model;
}

static VkdfMesh *
process_mesh(const aiScene *scene, const aiMesh *mesh)
{
   // FIXME: for now we only support triangle lists for loaded models
   assert(mesh->mPrimitiveTypes == aiPrimitiveType_TRIANGLE);
   VkdfMesh *_mesh = vkdf_mesh_new(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

   bool has_tangent = mesh->mTangents != NULL;
   bool has_bitangent = mesh->mBitangents != NULL;
   assert(has_tangent == has_bitangent);

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

         if (has_tangent) {
            glm::vec3 tangent;
            tangent.x = mesh->mTangents[i].x;
            tangent.y = mesh->mTangents[i].y;
            tangent.z = mesh->mTangents[i].z;

            glm::vec3 bitangent;
            bitangent.x = mesh->mBitangents[i].x;
            bitangent.y = mesh->mBitangents[i].y;
            bitangent.z = mesh->mBitangents[i].z;

            // Make sure our tangents and bitangents are oriented consistently
            // for all meshes
            if (glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f)
               tangent = tangent * -1.0f;

            _mesh->tangents.push_back(tangent);
            _mesh->bitangents.push_back(bitangent);
         }

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

   vkdf_mesh_compute_box(_mesh);

   return _mesh;
}

static void
process_node(VkdfModel *model, const aiScene *scene, const aiNode *node)
{
   for (uint32_t i = 0; i < node->mNumMeshes; i++) {
      aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
      model->meshes.push_back(process_mesh(scene, mesh));

      // Sanity check: all or no meshes have tangents
      assert(i == 0 ||
             ((model->meshes[i]->tangents.size() > 0 ) ==
              (model->meshes[i - 1]->tangents.size() > 0)));

      // Sanity check: The number of tangents and bitangents must match
      assert(model->meshes[i]->tangents.size() ==
             model->meshes[i]->bitangents.size());

      // Sanity check: if we have tangents and bitangents, then we must
      //               have as many as normals
      assert(model->meshes[i]->tangents.size() == 0 ||
             (model->meshes[i]->tangents.size() ==
              model->meshes[i]->normals.size()));
   }

   for (uint32_t i = 0; i < node->mNumChildren; i++)
      process_node(model, scene, node->mChildren[i]);
}

static char *
fixup_path_str(const char *str)
{
   char *new_str = g_strdup(str);
   char *iter = new_str;
   while (*iter) {
      if (*iter == '\\')
         *iter = '/';
      iter++;
   }

   return new_str;
}

static void
process_material(aiMaterial *material,
                 VkdfMaterial *solid_material,
                 VkdfTexMaterial *tex_material,
                 const char *file)
{
   // Solid materials
   aiColor4D diffuse(0.0f, 0.0f, 0.0f, 0.0f);
   aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse);
   memcpy(&solid_material->diffuse, &diffuse, sizeof(diffuse));

   aiColor4D ambient(0.0f, 0.0f, 0.0f, 0.0f);
   aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, &diffuse);
   memcpy(&solid_material->ambient, &ambient, sizeof(ambient));

   aiColor4D specular(0.0f, 0.0f, 0.0f, 0.0f);
   aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &specular);
   memcpy(&solid_material->specular, &specular, sizeof(specular));

   aiColor4D shininess(0.0f, 0.0f, 0.0f, 0.0f);
   aiGetMaterialColor(material, AI_MATKEY_SHININESS, &shininess);
   memcpy(&solid_material->shininess, &shininess.r, sizeof(float));

   // Texture materials
   aiString path;
   uint32_t count;

   memset(tex_material, 0, sizeof(VkdfTexMaterial));

   count = aiGetMaterialTextureCount(material, aiTextureType_NONE);
   if (count > 0)
      vkdf_info("model: %s: ignoring %u textures of type NONE\n", file, count);

   count = aiGetMaterialTextureCount(material, aiTextureType_DIFFUSE);
   if (count > 0) {
      if (count > 1) {
         vkdf_info("model: %s: %u DIFFUSE textures, using only one.\n",
                   file, count);
      }
      aiGetMaterialTexture(material, aiTextureType_DIFFUSE, 0, &path);
      tex_material->diffuse_path = fixup_path_str(path.C_Str());
   }
   solid_material->diffuse_tex_count = count;

   count = aiGetMaterialTextureCount(material, aiTextureType_AMBIENT);
   if (count > 0) {
      vkdf_info("model: %s: ignoring %u textures of type AMBIENT\n",
                file, count);
   }

   count = aiGetMaterialTextureCount(material, aiTextureType_SPECULAR);
   if (count > 0) {
      if (count > 1) {
         vkdf_info("model: %s: %u SPECULAR textures, using only one.\n",
                   file, count);
      }
      aiGetMaterialTexture(material, aiTextureType_SPECULAR, 0, &path);
      tex_material->specular_path = fixup_path_str(path.C_Str());
   }
   solid_material->specular_tex_count = count;

   count = aiGetMaterialTextureCount(material, aiTextureType_SHININESS);
   if (count > 0) {
      vkdf_info("model: %s: ignoring %u textures of type SHININESS\n",
                file, count);
   }

   count = aiGetMaterialTextureCount(material, aiTextureType_EMISSIVE);
   if (count > 0) {
      vkdf_info("model: %s: ignoring %u textures of type EMISSIVE\n",
                file, count);
   }

   count = aiGetMaterialTextureCount(material, aiTextureType_NORMALS);
   if (count > 0) {
      if (count > 1) {
         vkdf_info("model: %s: %u NORMAL textures, using only one.\n",
                   file, count);
      }
      aiGetMaterialTexture(material, aiTextureType_NORMALS, 0, &path);
      tex_material->normal_path = fixup_path_str(path.C_Str());
   }
   solid_material->normal_tex_count = count;

   count = aiGetMaterialTextureCount(material, aiTextureType_OPACITY);
   if (count > 0) {
      if (count > 1) {
         vkdf_info("model: %s: %u OPACITY textures, using only one.\n",
                   file, count);
      }
      aiGetMaterialTexture(material, aiTextureType_OPACITY, 0, &path);
      tex_material->opacity_path = fixup_path_str(path.C_Str());
   }
   solid_material->opacity_tex_count = count;

   count = aiGetMaterialTextureCount(material, aiTextureType_LIGHTMAP);
   if (count > 0) {
      vkdf_info("model: %s: ignoring %u textures of type LIGHTMAP\n",
                file, count);
   }

   count = aiGetMaterialTextureCount(material, aiTextureType_HEIGHT);
   if (count > 0) {
      vkdf_info("model: %s: ignoring %u textures of type HEIGHT\n",
                file, count);
   }

   count = aiGetMaterialTextureCount(material, aiTextureType_DISPLACEMENT);
   if (count > 0) {
      vkdf_info("model: %s: ignoring %u textures of type DISPLACEMENT\n",
                file, count);
   }

   count = aiGetMaterialTextureCount(material, aiTextureType_REFLECTION);
   if (count > 0) {
      vkdf_info("model: %s: ignoring %u textures of type REFLECTION\n",
                file, count);
   }

   count = aiGetMaterialTextureCount(material, aiTextureType_UNKNOWN);
   if (count > 0) {
      vkdf_info("model: %s: ignoring %u textures of type UNKNOWN\n",
                file, count);
   }
}

static VkdfModel *
create_model_from_scene(const aiScene *scene, const char *file)
{
   VkdfModel *model = vkdf_model_new();

   // Load materials
   for (uint32_t i = 0; i < scene->mNumMaterials; i++) {
      VkdfMaterial solid_material;
      VkdfTexMaterial tex_material;

      aiMaterial *material = scene->mMaterials[i];
      process_material(material, &solid_material, &tex_material, file);
      model->materials.push_back(solid_material);
      model->tex_materials.push_back(tex_material);
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

   VkdfModel *model = create_model_from_scene(scene, file);

   aiReleaseImport(scene);

   vkdf_model_compute_box(model);

   return model;
}

void
vkdf_model_free(VkdfContext *ctx, VkdfModel *model,
                bool free_material_resources)
{
   for (uint32_t i = 0; i < model->meshes.size(); i++)
      vkdf_mesh_free(ctx, model->meshes[i]);

   model->meshes.clear();
   std::vector<VkdfMesh *>(model->meshes).swap(model->meshes);

   model->collision_meshes.clear();
   std::vector<uint32_t>(model->collision_meshes).swap(model->collision_meshes);

   model->materials.clear();
   std::vector<VkdfMaterial>(model->materials).swap(model->materials);

   for (uint32_t i = 0; i < model->tex_materials.size(); i++) {
      g_free(model->tex_materials[i].diffuse_path);
      g_free(model->tex_materials[i].specular_path);
      g_free(model->tex_materials[i].normal_path);
      g_free(model->tex_materials[i].opacity_path);
      if (free_material_resources) {
         if (model->tex_materials[i].diffuse.image)
            vkdf_destroy_image(ctx, &model->tex_materials[i].diffuse);
         if (model->tex_materials[i].specular.image)
            vkdf_destroy_image(ctx, &model->tex_materials[i].specular);
         if (model->tex_materials[i].normal.image)
            vkdf_destroy_image(ctx, &model->tex_materials[i].normal);
         if (model->tex_materials[i].opacity.image)
            vkdf_destroy_image(ctx, &model->tex_materials[i].opacity);
      }
   }
   model->tex_materials.clear();
   std::vector<VkdfTexMaterial>(model->tex_materials).swap(model->tex_materials);

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
   vkdf_memory_map(ctx, model->vertex_buf.mem,
                   0, vertex_data_size, (void **) &map);

   // Interleaved per-vertex attributes (position, normal, uv, material)
   VkDeviceSize byte_offset = 0;
   for (uint32_t m = 0; m < model->meshes.size(); m++) {
      VkdfMesh *mesh = model->meshes[m];
      bool has_normals = mesh->normals.size() > 0;
      bool has_uv = mesh->uvs.size() > 0;
      bool has_material = mesh->material_idx != -1;

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

         if (has_material) {
            elem_size = sizeof(mesh->material_idx);
            memcpy(map + byte_offset, &mesh->material_idx, elem_size);
            byte_offset += elem_size;
         }
      }
   }

   vkdf_memory_unmap(ctx, model->vertex_buf.mem,
                     model->vertex_buf.mem_props, 0, vertex_data_size);
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
   vkdf_memory_map(ctx, model->index_buf.mem,
                   0, index_data_size, (void **) &map);

   VkDeviceSize byte_offset = 0;
   for (uint32_t m = 0; m < model->meshes.size(); m++) {
      VkdfMesh *mesh = model->meshes[m];
      VkDeviceSize mesh_index_data_size = vkdf_mesh_get_index_data_size(mesh);

      model->index_buf_offsets.push_back(byte_offset);

      memcpy(map + byte_offset, &mesh->indices[0], mesh_index_data_size);
      byte_offset += mesh_index_data_size;
   }

   vkdf_memory_unmap(ctx, model->index_buf.mem, model->index_buf.mem_props,
                     0, index_data_size);
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

void
vkdf_model_compute_box(VkdfModel *model)
{
   glm::vec3 min = glm::vec3(999999999.0f, 999999999.0f, 999999999.0f);
   glm::vec3 max = glm::vec3(-999999999.0f, -999999999.0f, -999999999.0f);

   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      VkdfMesh *mesh = model->meshes[i];
      const VkdfBox *m_box = vkdf_mesh_get_box(mesh);
      if (m_box->w == 0.0f && m_box->h == 0.0f && m_box->d == 0.0f)
         vkdf_mesh_compute_box(mesh);

      glm::vec3 m_min = glm::vec3(m_box->center.x - m_box->w,
                                  m_box->center.y - m_box->h,
                                  m_box->center.z - m_box->d);

      glm::vec3 m_max = glm::vec3(m_box->center.x + m_box->w,
                                  m_box->center.y + m_box->h,
                                  m_box->center.z + m_box->d);

      if (m_min.x < min.x)
         min.x = m_min.x;
      if (m_max.x > max.x)
         max.x = m_max.x;

      if (m_min.y < min.y)
         min.y = m_min.y;
      if (m_max.y > max.y)
         max.y = m_max.y;

      if (m_min.z < min.z)
         min.z = m_min.z;
      if (m_max.z > max.z)
         max.z = m_max.z;
   }

   model->box.center.x = (max.x + min.x) / 2.0f;
   model->box.center.y = (max.y + min.y) / 2.0f;
   model->box.center.z = (max.z + min.z) / 2.0f;
   model->box.w = (max.x - min.x) / 2.0f;
   model->box.h = (max.y - min.y) / 2.0f;
   model->box.d = (max.z - min.z) / 2.0f;
}

void
vkdf_model_load_textures(VkdfContext *ctx,
                         VkCommandPool pool,
                         VkdfModel *model,
                         bool color_is_srgb)
{
   for (uint32_t i = 0; i < model->materials.size(); i++) {
      VkdfMaterial *mat = &model->materials[i];
      VkdfTexMaterial *tex = &model->tex_materials[i];

      if (mat->diffuse_tex_count > 0) {
         assert(tex->diffuse_path);
         if (!vkdf_load_image_from_file(ctx, pool,
                                        tex->diffuse_path, &tex->diffuse,
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                        color_is_srgb)) {
            mat->diffuse_tex_count = 0;
         }
      }

      if (mat->specular_tex_count > 0) {
         assert(tex->specular_path);
         if (!vkdf_load_image_from_file(ctx, pool,
                                        tex->specular_path, &tex->specular,
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                        color_is_srgb)) {
            mat->specular_tex_count = 0;
         }
      }

      if (mat->normal_tex_count > 0) {
         assert(tex->normal_path);
         if (!vkdf_load_image_from_file(ctx, pool,
                                        tex->normal_path, &tex->normal,
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                        false)) {
            mat->normal_tex_count = 0;
         }
      }

      if (mat->opacity_tex_count > 0) {
         assert(tex->opacity_path);
         if (!vkdf_load_image_from_file(ctx, pool,
                                        tex->opacity_path, &tex->opacity,
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                        false)) {
            mat->opacity_tex_count = 0;
         }
      }
   }
}

