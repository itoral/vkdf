#include "vkdf.hpp"

const float WIN_WIDTH  = 1024.0f;
const float WIN_HEIGHT = 728.0f;

const bool SHOW_SPONZA_FLAG_MESH = false;
const uint32_t SPONZA_FLAG_MESH_IDX = 4;

const bool SHOW_DEBUG_TILE = false;
const bool ENABLE_CLIPPING = true;

enum {
   DIFFUSE_TEX_BINDING  = 0,
   NORMAL_TEX_BINDING   = 1,
   SPECULAR_TEX_BINDING = 2,
   OPACITY_TEX_BINDING  = 3,
};

struct PCBData {
   uint8_t proj[sizeof(glm::mat4)];
};

typedef struct {
   VkdfContext *ctx;

   VkdfScene *scene;

   VkdfCamera *camera;

   struct {
      VkDescriptorPool static_ubo_pool;
      VkDescriptorPool sampler_pool;
   } descriptor_pool;

   VkCommandPool cmd_pool;

   struct {
      struct {
         VkDescriptorSetLayout camera_view_layout;
         VkDescriptorSet camera_view_set;
         VkDescriptorSetLayout obj_layout;
         VkDescriptorSet obj_set;
         VkDescriptorSetLayout obj_tex_layout;
         VkDescriptorSetLayout obj_tex_opacity_layout;
         VkDescriptorSet obj_tex_set[32];
         VkDescriptorSetLayout light_layout;
         VkDescriptorSet light_set;
         VkDescriptorSetLayout shadow_map_sampler_layout;
         VkDescriptorSet shadow_map_sampler_set;
      } descr;

      struct {
         VkPipelineLayout base;
         VkPipelineLayout opacity;
      } layout;

      VkPipeline sponza;
      VkPipeline sponza_opacity;
   } pipelines;

   struct {
      struct {
         VkdfBuffer buf;
         VkDeviceSize size;
      } camera_view;
   } ubos;

   struct {
      struct {
         VkShaderModule vs;
         VkShaderModule fs;
         VkShaderModule fs_opacity;
      } obj;
   } shaders;

   VkdfMesh *tile_mesh;
   VkdfModel *sponza_model;
   VkdfObject *sponza_obj;
   VkdfBox sponza_mesh_boxes[400];
   bool sponza_mesh_visible[400];

   VkSampler sampler;

   struct {
      VkdfImage image;
      VkSampler sampler;
      struct {
         VkShaderModule vs;
         VkShaderModule fs;
      } shaders;
      struct {
         VkDescriptorSetLayout sampler_set_layout;
         VkDescriptorSet sampler_set;
         VkPipelineLayout layout;
         VkPipeline pipeline;
      } pipeline;
      VkRenderPass renderpass;
      VkFramebuffer framebuffer;
      VkCommandBuffer cmd_buf;
   } debug;

} SceneResources;

static void
postprocess_draw(VkdfContext *ctx,
                 VkSemaphore scene_draw_sem,
                 VkSemaphore postprocess_draw_sem,
                 void *data);

static inline VkdfBuffer
create_ubo(VkdfContext *ctx, uint32_t size, uint32_t usage, uint32_t mem_props)
{
   usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
   VkdfBuffer buf = vkdf_create_buffer(ctx, 0, size, usage, mem_props);
   return buf;
}

static VkDescriptorSet
create_descriptor_set(VkdfContext *ctx,
                      VkDescriptorPool pool,
                      VkDescriptorSetLayout layout)
{
   VkDescriptorSet set;
   VkDescriptorSetAllocateInfo alloc_info[1];
   alloc_info[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   alloc_info[0].pNext = NULL;
   alloc_info[0].descriptorPool = pool;
   alloc_info[0].descriptorSetCount = 1;
   alloc_info[0].pSetLayouts = &layout;
   VK_CHECK(vkAllocateDescriptorSets(ctx->device, alloc_info, &set));

   return set;
}

static void
init_ubos(SceneResources *res)
{
   // Camera view matrix
   res->ubos.camera_view.size = sizeof(glm::mat4);
   res->ubos.camera_view.buf = create_ubo(res->ctx,
                                          res->ubos.camera_view.size,
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
}

void
compute_sponza_mesh_boxes(SceneResources *res)
{
   VkdfObject *obj = res->sponza_obj;
   VkdfModel *model = res->sponza_model;

   // FIXME: we should put this in a helper in the framework
   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      VkdfBox box;
      box.center = obj->pos + vkdf_mesh_get_center_pos(model->meshes[i]) * obj->scale;
      box.w = vkdf_mesh_get_width(model->meshes[i]) * obj->scale.x / 2.0f;
      box.h = vkdf_mesh_get_height(model->meshes[i]) * obj->scale.y / 2.0f;
      box.d = vkdf_mesh_get_depth(model->meshes[i]) * obj->scale.z / 2.0f;

      if (obj->rot.x != 0.0f || obj->rot.y != 0.0f || obj->rot.z != 0.0f) {
         glm::mat4 Model(1.0f);
         Model = glm::translate(Model, box.center);
         // FIXME: use quaternion
         if (obj->rot.x)
            Model = glm::rotate(Model, DEG_TO_RAD(obj->rot.x), glm::vec3(1, 0, 0));
         if (obj->rot.y)
            Model = glm::rotate(Model, DEG_TO_RAD(obj->rot.y), glm::vec3(0, 1, 0));
         if (obj->rot.z)
            Model = glm::rotate(Model, DEG_TO_RAD(obj->rot.z), glm::vec3(0, 0, 1));
         Model = glm::translate(Model, -box.center);
         vkdf_box_transform(&box, &Model);
      }

      res->sponza_mesh_boxes[i] = box;
   }
}

static inline uint32_t
frustum_contains_box(VkdfBox *box,
                     VkdfBox *frustum_box,
                     VkdfPlane *frustum_planes)
{
   if (!vkdf_box_collision(box, frustum_box))
      return OUTSIDE;

   return vkdf_box_is_in_frustum(box, frustum_planes);
}

void
update_visible_sponza_meshes(SceneResources *res)
{
   VkdfCamera *camera = vkdf_scene_get_camera(res->scene);
   if (!vkdf_camera_is_dirty(camera))
      return;

   VkdfBox *cam_box = vkdf_camera_get_frustum_box(camera);
   VkdfPlane *cam_planes = vkdf_camera_get_frustum_planes(camera);

   VkdfModel *model = res->sponza_model;
   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      VkdfBox *box = &res->sponza_mesh_boxes[i];
      res->sponza_mesh_visible[i] =
         frustum_contains_box(box, cam_box, cam_planes) != OUTSIDE;
   }
}

static bool
record_update_resources_command(VkdfContext *ctx,
                                VkCommandBuffer cmd_buf,
                                void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Update camera view matrix
   VkdfCamera *camera = vkdf_scene_get_camera(res->scene);
   if (!vkdf_camera_is_dirty(camera))
      return false;

   glm::mat4 view = vkdf_camera_get_view_matrix(res->camera);
   vkCmdUpdateBuffer(cmd_buf,
                     res->ubos.camera_view.buf.buf,
                     0, sizeof(glm::mat4),
                     &view[0][0]);

   return true;
}

static void
record_instanced_draw(VkCommandBuffer cmd_buf,
                      VkPipeline pipeline,
                      VkPipeline pipeline_opacity,
                      VkdfModel *model,
                      bool *mesh_visible,
                      uint32_t count,
                      uint32_t first_instance,
                      VkPipelineLayout pipeline_layout,
                      VkPipelineLayout pipeline_opacity_layout,
                      VkDescriptorSet *obj_tex_set)
{
   VkPipeline bound_pipeline = 0;

   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      VkdfMesh *mesh = model->meshes[i];

      if (mesh->active == false)
         continue;

      if (mesh_visible[i] == false)
         continue;

      bool has_opacity =
         model->materials[mesh->material_idx].opacity_tex_count > 0;

      VkPipelineLayout required_pipeline_layout =
         has_opacity ? pipeline_opacity_layout : pipeline_layout;

      // We need to have a valid sampler even if the material for this mesh
      // doesn't use textures because we have a single shader that handles both
      // solid-only and solid+texture materials.
      VkDescriptorSet tex_set = obj_tex_set[mesh->material_idx];
      assert(tex_set);

      // Bind descriptor set with texture samplers for this material
      vkCmdBindDescriptorSets(cmd_buf,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              required_pipeline_layout,
                              4,                        // First decriptor set
                              1,                        // Descriptor set count
                              &tex_set,                 // Descriptor sets
                              0,                        // Dynamic offset count
                              NULL);                    // Dynamic offsets

      const VkDeviceSize offsets[1] = { 0 };
      vkCmdBindVertexBuffers(cmd_buf,
                             0,                         // Start Binding
                             1,                         // Binding Count
                             &mesh->vertex_buf.buf,     // Buffers
                             offsets);                  // Offsets


      // Bind pipeline
      VkPipeline required_pipeline = has_opacity ? pipeline_opacity : pipeline;
      if (bound_pipeline != required_pipeline) {
         vkCmdBindPipeline(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           required_pipeline);
         bound_pipeline = required_pipeline;
      }

      if (mesh->index_buf.buf == 0) {
         vkCmdDraw(cmd_buf,
                   mesh->vertices.size(),               // vertex count
                   count,                               // instance count
                   0,                                   // first vertex
                   first_instance);                     // first instance
      } else {
         vkCmdBindIndexBuffer(cmd_buf,
                              mesh->index_buf.buf,      // Buffer
                              0,                        // Offset
                              VK_INDEX_TYPE_UINT32);    // Index type

         vkCmdDrawIndexed(cmd_buf,
                          mesh->indices.size(),         // index count
                          count,                        // instance count
                          0,                            // first index
                          0,                            // first vertex
                          first_instance);              // first instance
      }
   }
}

static void
record_scene_commands(VkdfContext *ctx, VkCommandBuffer cmd_buf,
                      GHashTable *sets, bool is_dynamic, void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Push constants: camera projection matrix
   struct PCBData pcb_data;
   glm::mat4 *proj = vkdf_camera_get_projection_ptr(res->scene->camera);
   memcpy(&pcb_data.proj, &(*proj)[0][0], sizeof(pcb_data.proj));

   vkCmdPushConstants(cmd_buf,
                      res->pipelines.layout.base,
                      VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(pcb_data), &pcb_data);

   // Bind descriptor sets for the camera view matrix and the scene static
   // object UBO data
   VkDescriptorSet descriptor_sets[] = {
      res->pipelines.descr.camera_view_set,
      res->pipelines.descr.obj_set,
      res->pipelines.descr.light_set,
      res->pipelines.descr.shadow_map_sampler_set,
   };

   vkCmdBindDescriptorSets(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipelines.layout.base,
                           0,                        // First decriptor set
                           4,                        // Descriptor set count
                           descriptor_sets,          // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // Render objects
   char *set_id;
   VkdfSceneSetInfo *set_info;
   GHashTableIter iter;
   g_hash_table_iter_init(&iter, sets);
   while (g_hash_table_iter_next(&iter, (void **)&set_id, (void **)&set_info)) {
      if (!set_info || set_info->count == 0)
         continue;

      if (!strcmp(set_id, "sponza")) {
         if (ENABLE_CLIPPING)
            update_visible_sponza_meshes(res);
         record_instanced_draw(cmd_buf,
                               res->pipelines.sponza,
                               res->pipelines.sponza_opacity,
                               res->sponza_model,
                               res->sponza_mesh_visible,
                               set_info->count, set_info->start_index,
                               res->pipelines.layout.base,
                               res->pipelines.layout.opacity,
                               res->pipelines.descr.obj_tex_set);
         continue;
      }

      assert(!"unkown object category");
   }
}

static void
init_scene(SceneResources *res)
{
   VkdfContext *ctx = res->ctx;

   res->camera = vkdf_camera_new(-20.0f, 3.0f, -1.0f,
                                 0.0f, 180.0f, 0.0f);
   // FIXME: we should pass the projection in the constructor
   vkdf_camera_set_projection(res->camera,
                              45.0f, 0.1f, 500.0f, WIN_WIDTH / WIN_HEIGHT);
   vkdf_camera_look_at(res->camera, 10.0f, 5.0f, 0.0f);

   glm::vec3 scene_origin = glm::vec3(-100.0f, -100.0f, -100.0f);
   glm::vec3 scene_size = glm::vec3(200.0f, 200.0f, 200.0f);
   glm::vec3 tile_size = glm::vec3(200.0f, 200.0f, 200.0f);
   uint32_t cache_size = 0;
   res->scene = vkdf_scene_new(ctx,
                               res->camera,
                               scene_origin, scene_size, tile_size, 1,
                               cache_size, 1);

   vkdf_scene_set_scene_callbacks(res->scene,
                                  record_update_resources_command,
                                  record_scene_commands,
                                  SHOW_DEBUG_TILE ? postprocess_draw : NULL,
                                  res);

   VkClearValue color_clear;
   color_clear.color.float32[0] = 0.2f;
   color_clear.color.float32[1] = 0.4f;
   color_clear.color.float32[2] = 0.8f;
   color_clear.color.float32[3] = 1.0f;

   VkClearValue depth_clear;
   depth_clear.depthStencil.depth = 1.0f;
   depth_clear.depthStencil.stencil = 0;

   vkdf_scene_set_clear_values(res->scene, &color_clear, &depth_clear);

   glm::vec4 direction = glm::vec4(1.0f, -4.5f, -1.25f, 0.0f);
   glm::vec4 diffuse = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
   glm::vec4 ambient = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
   glm::vec4 specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

   VkdfLight *light =
      vkdf_light_new_directional(direction, diffuse, ambient, specular);

   vkdf_light_enable_shadows(light, true);

   VkdfSceneShadowSpec shadow_spec;
   shadow_spec.shadow_map_near = 5.0f;
   shadow_spec.shadow_map_far = 110.0f;
   shadow_spec.shadow_map_size = 4096;
   shadow_spec.depth_bias_const_factor = 1.0f;
   shadow_spec.depth_bias_slope_factor = 2.0f;
   shadow_spec.pfc_kernel_size = 2;

   vkdf_scene_add_light(res->scene, light, &shadow_spec);

   // Select source image for debug output
   res->debug.image = res->scene->lights[0]->shadow.shadow_map;

}

static void
create_sponza_texture_descriptor_sets(SceneResources *res)
{
   res->sampler =
         vkdf_create_sampler(res->ctx,
                             VK_SAMPLER_ADDRESS_MODE_REPEAT,
                             VK_FILTER_LINEAR,
                             VK_SAMPLER_MIPMAP_MODE_LINEAR);

   VkdfModel *model = res->sponza_model;
   assert(model->tex_materials.size() == model->materials.size());

   for (uint32_t i = 0; i < model->materials.size(); i++) {
      VkdfMaterial *m = &model->materials[i];

      VkdfTexMaterial *tm = &model->tex_materials[i];

      // We have a single shader that handles both solid+texture materials
      // and also solid-only materials. This means the shader always has
      // sampler bindings and these need to be valid even if the material
      // for the mesh we're rendering doesn't have any actual textures
      // so just bind the texture from a textured material
      if (m->opacity_tex_count == 0) {
         res->pipelines.descr.obj_tex_set[i] =
            create_descriptor_set(res->ctx,
                                  res->descriptor_pool.sampler_pool,
                                  res->pipelines.descr.obj_tex_layout);
      } else {
         res->pipelines.descr.obj_tex_set[i] =
            create_descriptor_set(res->ctx,
                                  res->descriptor_pool.sampler_pool,
                                  res->pipelines.descr.obj_tex_opacity_layout);
      }

      if (m->diffuse_tex_count > 0) {
         assert(tm->diffuse.view);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sampler,
                                            tm->diffuse.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            DIFFUSE_TEX_BINDING, 1);
      } else {
         vkdf_info("Material %u doesn't have a diffuse texture\n", i);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sampler,
                                            model->tex_materials[16].diffuse.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            DIFFUSE_TEX_BINDING, 1);
      }

      if (m->specular_tex_count > 0) {
         assert(tm->specular.view);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sampler,
                                            tm->specular.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            SPECULAR_TEX_BINDING, 1);
      } else {
         vkdf_info("Material %u doesn't have a specular texture\n", i);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sampler,
                                            model->tex_materials[1].normal.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            SPECULAR_TEX_BINDING, 1);
      }

      if (m->normal_tex_count > 0) {
         assert(tm->normal.view);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sampler,
                                            tm->normal.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            NORMAL_TEX_BINDING, 1);
      } else {
         vkdf_info("Material %u doesn't have a normal texture\n", i);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sampler,
                                            model->tex_materials[1].normal.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            NORMAL_TEX_BINDING, 1);
      }

      if (m->opacity_tex_count > 0) {
         assert(tm->opacity.view);
         vkdf_descriptor_set_sampler_update(res->ctx,
                                            res->pipelines.descr.obj_tex_set[i],
                                            res->sampler,
                                            tm->opacity.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            OPACITY_TEX_BINDING, 1);
      }
   }
}

static void
init_pipeline_descriptors(SceneResources *res)
{
   if (res->pipelines.layout.base)
      return;

   // Setup pipeline push constant range
   VkPushConstantRange pcb_range;
   pcb_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
   pcb_range.offset = 0;
   pcb_range.size = sizeof(PCBData);

   VkPushConstantRange pcb_ranges[] = {
      pcb_range,
   };

   // Setup pipeline layout
   res->pipelines.descr.camera_view_layout =
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT,
                                            false);

   res->pipelines.descr.obj_layout =
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   res->pipelines.descr.obj_tex_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx,
                                                0, 3,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->pipelines.descr.obj_tex_opacity_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx,
                                                0, 4,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->pipelines.descr.light_layout =
      vkdf_create_ubo_descriptor_set_layout(res->ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   res->pipelines.descr.shadow_map_sampler_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx, 0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   /* Base pipeline layout */
   VkDescriptorSetLayout layouts[] = {
      res->pipelines.descr.camera_view_layout,
      res->pipelines.descr.obj_layout,
      res->pipelines.descr.light_layout,
      res->pipelines.descr.shadow_map_sampler_layout,
      res->pipelines.descr.obj_tex_layout,
   };

   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 1;
   pipeline_layout_info.pPushConstantRanges = pcb_ranges;
   pipeline_layout_info.setLayoutCount = 5;
   pipeline_layout_info.pSetLayouts = layouts;
   pipeline_layout_info.flags = 0;

   VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                   &pipeline_layout_info,
                                   NULL,
                                   &res->pipelines.layout.base));

   /* Pipeline layout with opacity texture */
   layouts[4] = res->pipelines.descr.obj_tex_opacity_layout;
   VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                   &pipeline_layout_info,
                                   NULL,
                                   &res->pipelines.layout.opacity));

   // Camera view matrix descriptor set
   res->pipelines.descr.camera_view_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.camera_view_layout);

   VkDeviceSize ubo_offset = 0;
   VkDeviceSize ubo_size = res->ubos.camera_view.size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.camera_view_set,
                                     res->ubos.camera_view.buf.buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   // Scene objects descriptor sets
   res->pipelines.descr.obj_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.obj_layout);

   VkdfBuffer *obj_ubo = vkdf_scene_get_dynamic_object_ubo(res->scene);
   VkDeviceSize obj_ubo_size = vkdf_scene_get_dynamic_object_ubo_size(res->scene);
   ubo_offset = 0;
   ubo_size = obj_ubo_size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.obj_set,
                                     obj_ubo->buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   VkdfBuffer *material_ubo = vkdf_scene_get_dynamic_material_ubo(res->scene);
   VkDeviceSize material_ubo_size =
      vkdf_scene_get_dynamic_material_ubo_size(res->scene);
   ubo_offset = 0;
   ubo_size = material_ubo_size;
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.obj_set,
                                     material_ubo->buf,
                                     1, 1, &ubo_offset, &ubo_size, false, true);

   // Descriptor sets with textures for the sponza model (one set per mesh)
   create_sponza_texture_descriptor_sets(res);

   // Light & shadow map descriptions descriptor set
   res->pipelines.descr.light_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.static_ubo_pool,
                            res->pipelines.descr.light_layout);

   VkdfBuffer *light_ubo = vkdf_scene_get_light_ubo(res->scene);
   vkdf_scene_get_light_ubo_range(res->scene, &ubo_offset, &ubo_size);
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.light_set,
                                     light_ubo->buf,
                                     0, 1, &ubo_offset, &ubo_size, false, true);

   vkdf_scene_get_shadow_map_ubo_range(res->scene, &ubo_offset, &ubo_size);
   vkdf_descriptor_set_buffer_update(res->ctx,
                                     res->pipelines.descr.light_set,
                                     light_ubo->buf,
                                     1, 1, &ubo_offset, &ubo_size, false, true);

   // Shadow map sampler descriptor set
   res->pipelines.descr.shadow_map_sampler_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.sampler_pool,
                            res->pipelines.descr.shadow_map_sampler_layout);

   VkSampler shadow_map_sampler =
         vkdf_scene_light_get_shadow_map_sampler(res->scene, 0);

   VkdfImage *shadow_map_image =
         vkdf_scene_light_get_shadow_map_image(res->scene, 0);

   vkdf_descriptor_set_sampler_update(res->ctx,
                                      res->pipelines.descr.shadow_map_sampler_set,
                                      shadow_map_sampler,
                                      shadow_map_image->view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);
}

static void
init_sponza_pipelines(SceneResources *res)
{
   VkVertexInputBindingDescription vi_bindings[1];
   VkVertexInputAttributeDescription vi_attribs[6];

   // Vertex attribute binding 0: position, normal, material
   vi_bindings[0].binding = 0;
   vi_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_bindings[0].stride = vkdf_mesh_get_vertex_data_stride(res->sponza_model->meshes[0]);

   // binding 0, location 0: position
   vi_attribs[0].binding = 0;
   vi_attribs[0].location = 0;
   vi_attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[0].offset = 0;

   // binding 0, location 1: normal
   vi_attribs[1].binding = 0;
   vi_attribs[1].location = 1;
   vi_attribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[1].offset = 12;

   // binding 0, location 2: tangent
   vi_attribs[2].binding = 0;
   vi_attribs[2].location = 2;
   vi_attribs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[2].offset = 24;

   // binding 0, location 3: bitangent
   vi_attribs[3].binding = 0;
   vi_attribs[3].location = 3;
   vi_attribs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[3].offset = 36;

   // binding 0, location 4: uv
   vi_attribs[4].binding = 0;
   vi_attribs[4].location = 4;
   vi_attribs[4].format = VK_FORMAT_R32G32_SFLOAT;
   vi_attribs[4].offset = 48;

   // binding 0, location 5: material idx
   vi_attribs[5].binding = 0;
   vi_attribs[5].location = 5;
   vi_attribs[5].format = VK_FORMAT_R32_UINT;
   vi_attribs[5].offset = 56;

   VkRenderPass renderpass = vkdf_scene_get_static_render_pass(res->scene);

   res->pipelines.sponza =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_bindings,
                               6,
                               vi_attribs,
                               true,
                               renderpass,
                               res->pipelines.layout.base,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               res->shaders.obj.vs,
                               res->shaders.obj.fs);

   res->pipelines.sponza_opacity =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_bindings,
                               6,
                               vi_attribs,
                               true,
                               renderpass,
                               res->pipelines.layout.opacity,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                               VK_CULL_MODE_BACK_BIT,
                               res->shaders.obj.vs,
                               res->shaders.obj.fs_opacity);
}

static void
init_cmd_bufs(SceneResources *res)
{
   if (!res->cmd_pool)
      res->cmd_pool = vkdf_create_gfx_command_pool(res->ctx, 0);
}

static void
init_shaders(SceneResources *res)
{
   res->shaders.obj.vs = vkdf_create_shader_module(res->ctx, "obj.vert.spv");
   res->shaders.obj.fs = vkdf_create_shader_module(res->ctx, "obj.frag.spv");
   res->shaders.obj.fs_opacity =
      vkdf_create_shader_module(res->ctx, "obj_opacity.frag.spv");

   res->debug.shaders.vs =
      vkdf_create_shader_module(res->ctx, "debug-tile.vert.spv");
   res->debug.shaders.fs =
      vkdf_create_shader_module(res->ctx, "debug-tile.frag.spv");
}

static inline void
init_pipelines(SceneResources *res)
{
   init_pipeline_descriptors(res);
   init_sponza_pipelines(res);
}

static void
init_meshes(SceneResources *res)
{
   // Sponza model
   res->sponza_model = vkdf_model_load("./sponza.obj");
   vkdf_model_fill_vertex_buffers(res->ctx, res->sponza_model, true);
   vkdf_model_load_textures(res->ctx, res->cmd_pool, res->sponza_model);

   if (SHOW_SPONZA_FLAG_MESH == false)
      res->sponza_model->meshes[SPONZA_FLAG_MESH_IDX]->active = false;

   // Make all meshes visible by default
   memset(res->sponza_mesh_visible, 1, sizeof(res->sponza_mesh_visible));

   // 2D tile mesh, used for debug display
   res->tile_mesh = vkdf_2d_tile_mesh_new(res->ctx);
   vkdf_mesh_fill_vertex_buffer(res->ctx, res->tile_mesh);
}

static void
init_objects(SceneResources *res)
{
   glm::vec3 pos;
   VkdfObject *obj;

   pos = glm::vec3(0.0f, 0.0f, 0.0f);
   obj = vkdf_object_new_from_model(pos, res->sponza_model);
   vkdf_object_set_scale(obj, glm::vec3(0.02f, 0.02f, 0.02f));
   vkdf_object_set_material_idx_base(obj, 0);
   vkdf_object_set_lighting_behavior(obj, true, true);
   vkdf_object_set_dynamic(obj, true);
   vkdf_scene_add_object(res->scene, "sponza", obj);

   vkdf_scene_prepare(res->scene);

   res->sponza_obj = obj;
   compute_sponza_mesh_boxes(res);
}

static void
init_descriptor_pools(SceneResources *res)
{
   res->descriptor_pool.static_ubo_pool =
      vkdf_create_descriptor_pool(res->ctx,
                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8);

   res->descriptor_pool.sampler_pool =
      vkdf_create_descriptor_pool(res->ctx,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  256);
}

static void
create_debug_tile_pipeline(SceneResources *res)
{
   // Pipeline layout: debug sampler descriptor set
   res->debug.pipeline.sampler_set_layout =
      vkdf_create_sampler_descriptor_set_layout(res->ctx,
                                                0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->debug.pipeline.sampler_set =
      create_descriptor_set(res->ctx,
                            res->descriptor_pool.sampler_pool,
                            res->debug.pipeline.sampler_set_layout);

   res->debug.sampler =
         vkdf_create_sampler(res->ctx,
                             VK_SAMPLER_ADDRESS_MODE_REPEAT,
                             VK_FILTER_LINEAR,
                             VK_SAMPLER_MIPMAP_MODE_LINEAR);

   vkdf_descriptor_set_sampler_update(res->ctx,
                                      res->debug.pipeline.sampler_set,
                                      res->debug.sampler,
                                      res->debug.image.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   VkDescriptorSetLayout layouts[1] = {
      res->debug.pipeline.sampler_set_layout
   };

   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 1;
   pipeline_layout_info.pSetLayouts = layouts;
   pipeline_layout_info.flags = 0;

   VK_CHECK(vkCreatePipelineLayout(res->ctx->device,
                                   &pipeline_layout_info,
                                   NULL,
                                   &res->debug.pipeline.layout));

   // Pipeline
   VkVertexInputBindingDescription vi_binding[1];
   VkVertexInputAttributeDescription vi_attribs[2];

   // Vertex attribute binding 0: position, uv
   vi_binding[0].binding = 0;
   vi_binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding[0].stride = vkdf_mesh_get_vertex_data_stride(res->tile_mesh);

   // binding 0, location 0: position
   vi_attribs[0].binding = 0;
   vi_attribs[0].location = 0;
   vi_attribs[0].format = VK_FORMAT_R32G32_SFLOAT;
   vi_attribs[0].offset = 0;

   // binding 0, location 1: uv
   vi_attribs[1].binding = 0;
   vi_attribs[1].location = 1;
   vi_attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
   vi_attribs[1].offset = 12;

   res->debug.pipeline.pipeline =
      vkdf_create_gfx_pipeline(res->ctx,
                               NULL,
                               1,
                               vi_binding,
                               2,
                               vi_attribs,
                               false,
                               res->debug.renderpass,
                               res->debug.pipeline.layout,
                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                               VK_CULL_MODE_BACK_BIT,
                               res->debug.shaders.vs,
                               res->debug.shaders.fs);
}

static void
record_debug_tile_cmd_buf(SceneResources *res)
{
   const VkdfMesh *mesh = res->tile_mesh;

   vkdf_create_command_buffer(res->ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &res->debug.cmd_buf);

   vkdf_command_buffer_begin(res->debug.cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->debug.renderpass;
   rp_begin.framebuffer = res->debug.framebuffer;
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = res->ctx->width;
   rp_begin.renderArea.extent.height = res->ctx->height;
   rp_begin.clearValueCount = 0;
   rp_begin.pClearValues = NULL;

   vkCmdBeginRenderPass(res->debug.cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Viewport and Scissor
   uint32_t width = res->ctx->width * 0.5;
   uint32_t height = res->ctx->height * 0.5;

   VkViewport viewport;
   viewport.width = width;
   viewport.height = height;
   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;
   viewport.x = 0;
   viewport.y = 0;
   vkCmdSetViewport(res->debug.cmd_buf, 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = width;
   scissor.extent.height = height;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(res->debug.cmd_buf, 0, 1, &scissor);

   // Pipeline
   vkCmdBindPipeline(res->debug.cmd_buf,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->debug.pipeline.pipeline);

   // Vertex buffer: position, uv
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->debug.cmd_buf,
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &mesh->vertex_buf.buf,   // Buffers
                          offsets);                // Offsets

   // Descriptors
   vkCmdBindDescriptorSets(res->debug.cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->debug.pipeline.layout,
                           0,                                // First decriptor set
                           1,                                // Descriptor set count
                           &res->debug.pipeline.sampler_set, // Descriptor sets
                           0,                                // Dynamic offset count
                           NULL);                            // Dynamic offsets

   // Draw
   vkCmdDraw(res->debug.cmd_buf,
             mesh->vertices.size(),                // vertex count
             1,                                    // instance count
             0,                                    // first vertex
             0);                                   // first instance

   vkCmdEndRenderPass(res->debug.cmd_buf);

   vkdf_command_buffer_end(res->debug.cmd_buf);
}

static VkRenderPass
create_debug_tile_renderpass(SceneResources *res)
{
   VkAttachmentDescription attachments[1];

   attachments[0].format = res->ctx->surface_format;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   attachments[0].flags = 0;

   VkAttachmentReference color_ref;
   color_ref.attachment = 0;
   color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkSubpassDescription subpass[1];
   subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass[0].flags = 0;
   subpass[0].inputAttachmentCount = 0;
   subpass[0].pInputAttachments = NULL;
   subpass[0].colorAttachmentCount = 1;
   subpass[0].pColorAttachments = &color_ref;
   subpass[0].pResolveAttachments = NULL;
   subpass[0].pDepthStencilAttachment = NULL;
   subpass[0].preserveAttachmentCount = 0;
   subpass[0].pPreserveAttachments = NULL;

   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 1;
   rp_info.pAttachments = attachments;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass renderpass;
   VK_CHECK(vkCreateRenderPass(res->ctx->device, &rp_info, NULL, &renderpass));

   return renderpass;
}

static void
init_debug_tile_resources(SceneResources *res)
{
   res->debug.renderpass =
      create_debug_tile_renderpass(res);

   VkdfImage *color_image = vkdf_scene_get_color_render_target(res->scene);
   res->debug.framebuffer =
      vkdf_create_framebuffer(res->ctx,
                              res->debug.renderpass,
                              color_image->view,
                              res->ctx->width, res->ctx->height,
                              0, NULL);

   create_debug_tile_pipeline(res);

   record_debug_tile_cmd_buf(res);
}


static void
init_resources(VkdfContext *ctx, SceneResources *res)
{
   memset(res, 0, sizeof(SceneResources));

   res->ctx = ctx;

   init_descriptor_pools(res);
   init_cmd_bufs(res);
   init_scene(res);
   init_meshes(res);
   init_objects(res);
   init_ubos(res);
   init_shaders(res);
   init_pipelines(res);
   init_debug_tile_resources(res);
}

static void
update_camera(SceneResources *res)
{
   const float mov_speed = 0.15f;
   const float rot_speed = 1.0f;

   VkdfCamera *cam = vkdf_scene_get_camera(res->scene);
   GLFWwindow *window = res->ctx->window;

   float base_speed = 1.0f;

   // Rotation
   if (glfwGetKey(window, GLFW_KEY_LEFT) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, 0.0f, base_speed * rot_speed, 0.0f);
   else if (glfwGetKey(window, GLFW_KEY_RIGHT) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, 0.0f, -base_speed * rot_speed, 0.0f);

   if (glfwGetKey(window, GLFW_KEY_PAGE_UP) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, base_speed * rot_speed, 0.0f, 0.0f);
   else if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, -base_speed * rot_speed, 0.0f, 0.0f);

   // Stepping
   if (glfwGetKey(window, GLFW_KEY_UP) != GLFW_RELEASE) {
      float step_speed = base_speed * mov_speed;
      vkdf_camera_step(cam, step_speed, 1, 1, 1);
   } else if (glfwGetKey(window, GLFW_KEY_DOWN) != GLFW_RELEASE) {
      float step_speed = -base_speed * mov_speed;
      vkdf_camera_step(cam, step_speed, 1, 1, 1);
   }
}

static void
update_objects(SceneResources *res)
{
}

static void
scene_update(VkdfContext *ctx, void *data)
{
   SceneResources *res = (SceneResources *) data;
   update_camera(res); // FIXME: this should be a callback called from the scene
   update_objects(res);
   vkdf_scene_update_cmd_bufs(res->scene);
   vkdf_camera_set_dirty(res->camera, false); // FIXME: this should be done by the scene
}

static void
postprocess_draw(VkdfContext *ctx,
                 VkSemaphore scene_draw_sem,
                 VkSemaphore postprocess_draw_sem,
                 void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Render debug tile
   VkPipelineStageFlags debug_tile_wait_stages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   vkdf_command_buffer_execute(ctx,
                               res->debug.cmd_buf,
                               &debug_tile_wait_stages,
                               1, &scene_draw_sem,
                               1, &postprocess_draw_sem);
}

static void
scene_render(VkdfContext *ctx, void *data)
{
   SceneResources *res = (SceneResources *) data;
   vkdf_scene_draw(res->scene);
}

static void
destroy_models(SceneResources *res)
{
   vkdf_mesh_free(res->ctx, res->tile_mesh);
   vkdf_model_free(res->ctx, res->sponza_model);
}

static void
destroy_cmd_bufs(SceneResources *res)
{
   vkDestroyCommandPool(res->ctx->device, res->cmd_pool, NULL);
}

static void
destroy_pipelines(SceneResources *res)
{
   vkDestroyPipeline(res->ctx->device, res->pipelines.sponza, NULL);
   vkDestroyPipelineLayout(res->ctx->device, res->pipelines.layout.base, NULL);

   vkDestroyPipeline(res->ctx->device, res->pipelines.sponza_opacity, NULL);
   vkDestroyPipelineLayout(res->ctx->device, res->pipelines.layout.opacity, NULL);

   /* Set 0 */
   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.camera_view_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.camera_view_layout, NULL);

   /* Set 1 */
   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.obj_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.obj_layout, NULL);

   /* Set 2 */
   for (uint32_t i = 0; i < res->sponza_model->tex_materials.size(); i++) {
      if (res->pipelines.descr.obj_tex_set[i]) {
         vkFreeDescriptorSets(res->ctx->device,
                              res->descriptor_pool.sampler_pool,
                              1, &res->pipelines.descr.obj_tex_set[i]);
      }
   }
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.obj_tex_layout, NULL);

   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.obj_tex_opacity_layout, NULL);

   /* Set 3 */
   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.static_ubo_pool,
                        1, &res->pipelines.descr.light_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.light_layout, NULL);

   /* Set 4 */
   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.sampler_pool,
                        1, &res->pipelines.descr.shadow_map_sampler_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->pipelines.descr.shadow_map_sampler_layout,
                                NULL);

   vkDestroyDescriptorPool(res->ctx->device,
                           res->descriptor_pool.static_ubo_pool, NULL);
   vkDestroyDescriptorPool(res->ctx->device,
                           res->descriptor_pool.sampler_pool, NULL);
}

static void
destroy_shader_modules(SceneResources *res)
{
   vkDestroyShaderModule(res->ctx->device, res->shaders.obj.vs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.obj.fs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->shaders.obj.fs_opacity, NULL);
}

static void
destroy_ubos(SceneResources *res)
{
   vkDestroyBuffer(res->ctx->device, res->ubos.camera_view.buf.buf, NULL);
   vkFreeMemory(res->ctx->device, res->ubos.camera_view.buf.mem, NULL);
}

static void
destroy_debug_tile_resources(SceneResources *res)
{
   vkDestroyShaderModule(res->ctx->device, res->debug.shaders.vs, NULL);
   vkDestroyShaderModule(res->ctx->device, res->debug.shaders.fs, NULL);

   vkDestroyRenderPass(res->ctx->device, res->debug.renderpass, NULL);


   vkDestroyPipelineLayout(res->ctx->device, res->debug.pipeline.layout, NULL);
   vkDestroyPipeline(res->ctx->device, res->debug.pipeline.pipeline, NULL);

   vkFreeDescriptorSets(res->ctx->device,
                        res->descriptor_pool.sampler_pool,
                        1, &res->debug.pipeline.sampler_set);
   vkDestroyDescriptorSetLayout(res->ctx->device,
                                res->debug.pipeline.sampler_set_layout, NULL);

   vkDestroyFramebuffer(res->ctx->device, res->debug.framebuffer, NULL);
}

static void
destroy_samplers(SceneResources *res)
{
   vkDestroySampler(res->ctx->device, res->debug.sampler, NULL);
   vkDestroySampler(res->ctx->device, res->sampler, NULL);
}

void
cleanup_resources(SceneResources *res)
{
   destroy_samplers(res);
   vkdf_scene_free(res->scene);
   destroy_debug_tile_resources(res);
   destroy_models(res);
   destroy_cmd_bufs(res);
   destroy_shader_modules(res);
   destroy_pipelines(res);
   destroy_ubos(res);

   vkdf_camera_free(res->camera);
}

int
main()
{
   VkdfContext ctx;
   SceneResources resources;

   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, false, false, false);
   init_resources(&ctx, &resources);

   vkdf_event_loop_run(&ctx, true, scene_update, scene_render, &resources);

   cleanup_resources(&resources);
   vkdf_cleanup(&ctx);

   return 0;
}
