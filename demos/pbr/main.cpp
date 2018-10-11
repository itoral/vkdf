/**
 * This demo is mostly a port to Vulkan of the OpenGL PBR demo developed
 * by Joey de Vries that is available at http://learnopengl.com, without
 * the specular IBL component.
 *
 * The free textures used by this demo (under ./data) are the same used
 * in the OpenGL demo and were downloaded from:
 *
 * - PBR Material textures:
 * https://freepbr.com/materials/rusted-iron-pbr-metal-material-alt
 *
 * - HDR environment map:
 * http://www.hdrlabs.com/sibl/archive.html
 *
 * For those familiar with the original tutorial, one slightly tricky difference
 * that is worth remarking is that our scene framework expects the rendering
 * output to be linear and will do conversion to sRGB for display as it blits
 * to the presentation image, whereas the original OpenGL vesion implemented
 * sRGB conversion in the PBR shader itself.
 */

#include "vkdf.hpp"

#include <stdlib.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

const uint32_t WIN_WIDTH    = 800;
const uint32_t WIN_HEIGHT   = 600;

const int32_t NUM_OBJECTS_X = 8;
const int32_t NUM_OBJECTS_Y = 8;
const int32_t NUM_OBJECTS   = NUM_OBJECTS_X * NUM_OBJECTS_Y;

const int32_t NUM_LIGHTS    = 4;

enum {
   TEX_ALBEDO = 0,
   TEX_NORMAL,
   TEX_ROUGHNESS,
   TEX_METALLIC,
   TEX_LDR_ENV_MAP,
   TEX_HDR_ENV_MAP,
   TEX_HDR_IRRADIANCE_MAP,
   TEX_LAST
};

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer *cmd_bufs;
   VkRenderPass render_pass;
   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkShaderModule vs_module;
   VkShaderModule fs_module;
   VkFramebuffer *framebuffers;
   VkdfImage depth_image;

   // Descriptor pools
   VkDescriptorPool ubo_pool;
   VkDescriptorPool sampler_pool;

   // UBOs for View/Projection and Model matrices
   VkdfBuffer VP_ubo;
   VkdfBuffer M_ubo;
   VkdfBuffer light_ubo;

   // Descriptor sets
   VkDescriptorSetLayout ubo_set_layout;
   VkDescriptorSet ubo_set;
   VkDescriptorSetLayout tex_set_layout;
   VkDescriptorSet tex_set;
   VkDescriptorSetLayout irradiance_tex_set_layout;
   VkDescriptorSet irradiance_tex_set;

   // Cubemap rendering resources
   VkPipelineLayout cubemap_pipeline_layout;
   VkPipeline cubemap_pipeline;
   VkDescriptorSetLayout cubemap_tex_set_layout;
   VkDescriptorSet cubemap_tex_set;
   VkShaderModule cubemap_vs_module;
   VkShaderModule cubemap_fs_module;

   // View/Projection matrices
   glm::mat4 view;
   glm::mat4 projection;

   // Objects
   VkdfObject *objs[NUM_OBJECTS];
   VkdfBuffer instance_buf;
   struct {
      VkdfModel *sphere;
      VkdfModel *cube;
   } model;

   // Lights
   VkdfLight *lights[NUM_LIGHTS];

   // Camera
   VkdfCamera *camera;

   // Textures
   VkSampler sampler;
   VkSampler cubemap_sampler;
   VkdfImage textures[TEX_LAST];

   // Select solid or textured objects
   bool enable_texture_mode;

   // Enable or disable Image Based Lighting (IBL)
   bool enable_ibl_mode;
} DemoResources;

static VkdfBuffer
create_ubo(VkdfContext *ctx, uint32_t size, uint32_t mem_props)
{
   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flags
                         size,                                 // size
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,   // usage
                         mem_props);                           // memory props
   return buf;
}

static VkRenderPass
create_render_pass(VkdfContext *ctx, DemoResources *res)
{
   VkAttachmentDescription attachments[2];

   // Single color attachment
   attachments[0].format = ctx->surface_format.format;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   attachments[0].flags = 0;

   // Depth attachment
   attachments[1].format = res->depth_image.format;
   attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   attachments[1].flags = 0;

   // Color and depth attachment references
   VkAttachmentReference color_reference;
   color_reference.attachment = 0;
   color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkAttachmentReference depth_reference;
   depth_reference.attachment = 1;
   depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   // Subpass for rendering to color and depth attachments
   VkSubpassDescription subpass;
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.flags = 0;
   subpass.inputAttachmentCount = 0;
   subpass.pInputAttachments = NULL;
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &color_reference;
   subpass.pResolveAttachments = NULL;
   subpass.pDepthStencilAttachment = &depth_reference;
   subpass.preserveAttachmentCount = 0;
   subpass.pPreserveAttachments = NULL;

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 2;
   rp_info.pAttachments = attachments;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = &subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass;
   VkResult result =
      vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass);
   if (result != VK_SUCCESS)
      vkdf_fatal("Failed to create render pass");

   return render_pass;
}

static void
render_pass_commands(VkdfContext *ctx, DemoResources *res, uint32_t index)
{
   VkClearValue clear_values[2];
   vkdf_color_clear_set(&clear_values[0], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
   vkdf_depth_stencil_clear_set(&clear_values[1], 1.0f, 0);

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->render_pass;
   rp_begin.framebuffer = res->framebuffers[index];
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = ctx->width;
   rp_begin.renderArea.extent.height = ctx->height;
   rp_begin.clearValueCount = 2;
   rp_begin.pClearValues = clear_values;

   vkCmdBeginRenderPass(res->cmd_bufs[index],
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Viewport and Scissor
   VkViewport viewport;
   viewport.height = ctx->height;
   viewport.width = ctx->width;
   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;
   viewport.x = 0;
   viewport.y = 0;
   vkCmdSetViewport(res->cmd_bufs[index], 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = ctx->width;
   scissor.extent.height = ctx->height;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(res->cmd_bufs[index], 0, 1, &scissor);

   // ====================== Render objects ========================

   // Pipeline
   vkCmdBindPipeline(res->cmd_bufs[index], VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipeline);

   // Bind descriptorsets
   VkDescriptorSet sets[] = {
      res->ubo_set,              // Always used
      res->tex_set,              // Only used with in texture mode
      res->irradiance_tex_set    // Only used with IBL mode
   };

   uint32_t num_descr_sets =
      res->enable_texture_mode && res->enable_ibl_mode ? 3 :  // tex + ibl
      res->enable_texture_mode ? 2 :                          // tex - ibl
      !res->enable_texture_mode && res->enable_ibl_mode ? 3:  // solid + ibl
      1;                                                      // solid - ibl

   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                            // First decriptor set
                           num_descr_sets,               // Descr set count
                           sets,                         // Descriptor sets
                           0,                            // Dynamic offset count
                           NULL);                        // Dynamic offsets

   // Render NUM_OBJECTS instances of each mesh of the model
   // We have a single vertex buffer for all per-vertex data with data for
   // all the meshes, the same for per-instance attributes and for the index
   // data, so we always bind the same buffers but update the offsets depending
   // on the mesh we are rendering.
   VkdfModel *model = res->model.sphere;
   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      // Per-vertex attributes for this mesh
      vkCmdBindVertexBuffers(res->cmd_bufs[index],
                             0,                                // Start Binding
                             1,                                // Binding Count
                             &model->vertex_buf.buf,           // Buffers
                             &model->vertex_buf_offsets[i]);   // Offsets

      // Per-instance attributes for this mesh: we have a buffer with the
      // material index for each instance of each mesh, ordered by mesh.
      VkDeviceSize instance_buf_offset = i * NUM_OBJECTS * sizeof(uint32_t);
      vkCmdBindVertexBuffers(res->cmd_bufs[index],
                             1,                                // Start Binding
                             1,                                // Binding Count
                             &res->instance_buf.buf,           // Buffers
                             &instance_buf_offset);            // Offsets

      // Index buffer for this mesh (taken from model)
      vkCmdBindIndexBuffer(res->cmd_bufs[index],
                           model->index_buf.buf,               // Buffer
                           model->index_buf_offsets[i],        // Offset
                           VK_INDEX_TYPE_UINT32);              // Index type

      // Draw NUM_OBJECTS instances of this mesh
      vkCmdDrawIndexed(res->cmd_bufs[index],
                       model->meshes[i]->indices.size(),       // Index count
                       NUM_OBJECTS,                            // Instance count
                       0,                                      // First index
                       0,                                      // Vertex offset
                       0);                                     // First instance
   }

   // ====================== Render cubemap ========================

   // Pipeline
   vkCmdBindPipeline(res->cmd_bufs[index], VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->cubemap_pipeline);

   // Bind descriptorsets
   VkDescriptorSet cubemap_sets[] = {
      res->ubo_set,
      res->cubemap_tex_set,
   };

   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->cubemap_pipeline_layout,
                           0,                            // First decriptor set
                           2,                            // Descr set count
                           cubemap_sets,                 // Descriptor sets
                           0,                            // Dynamic offset count
                           NULL);                        // Dynamic offsets

   const VkDeviceSize offsets[1] = { 0 };
   model = res->model.cube;
   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      // Per-vertex attributes for this mesh
      vkCmdBindVertexBuffers(res->cmd_bufs[index],
                             0,                                 // Start Binding
                             1,                                 // Binding Count
                             &model->meshes[i]->vertex_buf.buf, // Buffers
                             offsets);                          // Offsets

      vkdf_mesh_draw(model->meshes[i], res->cmd_bufs[index], 1, 0);
   }

   vkCmdEndRenderPass(res->cmd_bufs[index]);
}

static void
create_pipeline_layouts(VkdfContext *ctx, DemoResources *res)
{
   // ~~~~~~~~~~~~~~~~ Pipeline layout regular rendering  ~~~~~~~~~~~~~~~~~~~

   // ======== UBOs ========

   res->ubo_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 3,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                            VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   res->ubo_set =
      vkdf_descriptor_set_create(ctx, res->ubo_pool, res->ubo_set_layout);

   // Binding 0: View / Projection matrices
   VkDeviceSize VP_offset = 0;
   VkDeviceSize VP_size = 2 * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->ubo_set,
                                     res->VP_ubo.buf,
                                     0, 1, &VP_offset, &VP_size, false, true);

   // Binding 1: Model matrices
   VkDeviceSize M_offset = 0;
   VkDeviceSize M_size = NUM_OBJECTS * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->ubo_set,
                                     res->M_ubo.buf,
                                     1, 1, &M_offset, &M_size, false, true);

   // Binding 2: Lights
   VkDeviceSize light_offset = 0;
   VkDeviceSize light_size = sizeof(VkdfLight) * NUM_LIGHTS;
   vkdf_descriptor_set_buffer_update(ctx, res->ubo_set,
                                     res->light_ubo.buf,
                                     2, 1, &light_offset, &light_size,
                                     false, true);

   // ======== Material textures ========

   res->sampler =
         vkdf_create_sampler(ctx,
                             VK_SAMPLER_ADDRESS_MODE_REPEAT,
                             VK_FILTER_LINEAR,
                             VK_SAMPLER_MIPMAP_MODE_LINEAR,
                             16.0f);

   res->tex_set_layout =
      vkdf_create_sampler_descriptor_set_layout(ctx, 0, 4,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->tex_set =
      vkdf_descriptor_set_create(ctx, res->sampler_pool, res->tex_set_layout);

   // Binding 0: Albedo
   vkdf_descriptor_set_sampler_update(ctx, res->tex_set,
                                      res->sampler,
                                      res->textures[TEX_ALBEDO].view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   // Binding 1: Normal
   vkdf_descriptor_set_sampler_update(ctx, res->tex_set,
                                      res->sampler,
                                      res->textures[TEX_NORMAL].view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      1, 1);

   // Binding 2: Roughness
   vkdf_descriptor_set_sampler_update(ctx, res->tex_set,
                                      res->sampler,
                                      res->textures[TEX_ROUGHNESS].view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      2, 1);

   // Binding 3: Metallic
   vkdf_descriptor_set_sampler_update(ctx, res->tex_set,
                                      res->sampler,
                                      res->textures[TEX_METALLIC].view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      3, 1);

   // ======== Irradiance map ========

   res->cubemap_sampler =
         vkdf_create_sampler(ctx,
                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                             VK_FILTER_LINEAR,
                             VK_SAMPLER_MIPMAP_MODE_NEAREST,
                             0.0f);

   res->irradiance_tex_set_layout =
      vkdf_create_sampler_descriptor_set_layout(ctx, 0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->irradiance_tex_set =
      vkdf_descriptor_set_create(ctx, res->sampler_pool,
                                 res->irradiance_tex_set_layout);

   // Binding 0: Irradiance map
   vkdf_descriptor_set_sampler_update(ctx, res->irradiance_tex_set,
                                      res->cubemap_sampler,
                                      res->textures[TEX_HDR_IRRADIANCE_MAP].view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   // ========= Pipeline layout =========

   VkDescriptorSetLayout set_layouts[] = {
      res->ubo_set_layout,
      res->tex_set_layout,
      res->irradiance_tex_set_layout
   };

   uint32_t num_descr_set_layouts =
      res->enable_texture_mode && res->enable_ibl_mode ? 3 :  // tex + ibl
      res->enable_texture_mode ? 2 :                          // tex - ibl
      !res->enable_texture_mode && res->enable_ibl_mode ? 3:  // solid + ibl
      1;                                                      // solid - ibl

   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = num_descr_set_layouts;
   pipeline_layout_info.pSetLayouts = set_layouts;
   pipeline_layout_info.flags = 0;

   VkResult result = vkCreatePipelineLayout(ctx->device,
                                            &pipeline_layout_info,
                                            NULL,
                                            &res->pipeline_layout);
   if (result != VK_SUCCESS)
      vkdf_fatal("Failed to create pipeline layout");


   // ~~~~~~~~~~~~~~~~ Pipeline layout cubemap rendering  ~~~~~~~~~~~~~~~~~~~

   // ======== Set 0: UBOs ========

   /* We reuse the UBO layout from the regular pipeline, we only require its
    * 1st binding.
    */

   // ======== Set 1: Textures ========

   res->cubemap_tex_set_layout =
      vkdf_create_sampler_descriptor_set_layout(ctx, 0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->cubemap_tex_set =
      vkdf_descriptor_set_create(ctx, res->sampler_pool,
                                 res->cubemap_tex_set_layout);

   // Binding 0: equirectangular environment map
   vkdf_descriptor_set_sampler_update(ctx, res->cubemap_tex_set,
                                      res->cubemap_sampler,
                                      res->textures[TEX_LDR_ENV_MAP].view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   // ========= Pipeline layout =========

   VkDescriptorSetLayout cubemap_set_layouts[] = {
      res->ubo_set_layout,
      res->cubemap_tex_set_layout
   };

   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 2;
   pipeline_layout_info.pSetLayouts = cubemap_set_layouts;
   pipeline_layout_info.flags = 0;

   result = vkCreatePipelineLayout(ctx->device,
                                   &pipeline_layout_info,
                                   NULL,
                                   &res->cubemap_pipeline_layout);
   if (result != VK_SUCCESS)
      vkdf_fatal("Failed to create cubemap pipeline layout");
}

static void
create_pipelines(VkdfContext *ctx, DemoResources *res)
{
   /* ================ Default pipeline =================== */

   VkVertexInputBindingDescription vi_bindings[1];
   VkVertexInputAttributeDescription vi_attribs[3];

   // Vertex attribute binding 0: position, normal, UV
   uint32_t stride =
      vkdf_mesh_get_vertex_data_stride(res->model.sphere->meshes[0]);
   vkdf_vertex_binding_set(&vi_bindings[0],
                           0, VK_VERTEX_INPUT_RATE_VERTEX, stride);

   /* binding 0, location 0: position
    * binding 0, location 1: normal
    * binding 0, location 2: UV
    */
   vkdf_vertex_attrib_set(&vi_attribs[0], 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
   vkdf_vertex_attrib_set(&vi_attribs[1], 0, 1, VK_FORMAT_R32G32B32_SFLOAT, 12);
   vkdf_vertex_attrib_set(&vi_attribs[2], 0, 2, VK_FORMAT_R32G32_SFLOAT, 24);

   // We assume all meshes in the model use the same primitive type
   VkdfMesh *mesh = res->model.sphere->meshes[0];
   VkPrimitiveTopology primitive = vkdf_mesh_get_primitive(mesh);
   res->pipeline = vkdf_create_gfx_pipeline(ctx, NULL,
                                            1, vi_bindings,
                                            res->enable_texture_mode ? 3 : 2,
                                            vi_attribs,
                                            true,
                                            VK_COMPARE_OP_LESS,
                                            res->render_pass,
                                            res->pipeline_layout,
                                            primitive,
                                            VK_CULL_MODE_BACK_BIT,
                                            1,
                                            res->vs_module,
                                            res->fs_module);

   /* ================ Cubemap pipeline =================== */

   VkVertexInputBindingDescription vi_bindings_cm[1];
   VkVertexInputAttributeDescription vi_attribs_cm[1];

   // Vertex attribute binding 0: position
   stride = vkdf_mesh_get_vertex_data_stride(res->model.cube->meshes[0]);
   vkdf_vertex_binding_set(&vi_bindings_cm[0],
                           0, VK_VERTEX_INPUT_RATE_VERTEX, stride);

   /* binding 0, location 0: position */
   vkdf_vertex_attrib_set(&vi_attribs_cm[0], 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);

   // We assume all meshes in the model use the same primitive type
   mesh = res->model.cube->meshes[0];
   primitive = vkdf_mesh_get_primitive(mesh);
   res->cubemap_pipeline =
      vkdf_create_gfx_pipeline(ctx, NULL,
                               1, vi_bindings_cm,
                               1, vi_attribs_cm,
                               true,
                               VK_COMPARE_OP_LESS_OR_EQUAL, // NDZ z = 1.0 for all cubemap fragments
                               res->render_pass,
                               res->cubemap_pipeline_layout,
                               primitive,
                               VK_CULL_MODE_FRONT_BIT,
                               1,
                               res->cubemap_vs_module,
                               res->cubemap_fs_module);
}

static void
init_matrices(DemoResources *res)
{
   VkdfCamera *camera = vkdf_camera_new(0.0f, 0.0f, 20.0f,    // Position
                                        0.0f, 0.0f, 1.0f,     // View dir
                                        45.0f, 0.1f, 100.0f,  // Projection
                                        (float) WIN_WIDTH / WIN_HEIGHT);
   vkdf_camera_look_at(camera, 0.0f, 0.0f, 0.0f);
   res->camera = camera;

   const glm::mat4 *proj = vkdf_camera_get_projection_ptr(res->camera);
   res->projection = *proj;

   res->view = vkdf_camera_get_view_matrix(res->camera);
}

static void
init_models(VkdfContext *ctx, DemoResources *res)
{
   res->model.sphere =
      vkdf_model_load("./../../data/models/sphere.obj", true, false);

   // Create per-vertex and index buffers for this model. Make it so we have
   // a single buffer for the entire model that packs data from all meshes
   // (instead of having a different vertex/index buffer per mesh). This way,
   // when we render the meshes we do not have to bind a different buffer
   // for each one, instead we simply update the byte offset into the buffer
   // where the mesh's data is stored.
   vkdf_model_fill_vertex_buffers(ctx, res->model.sphere, false);

   res->model.cube =
      vkdf_model_load("./../../data/models/cube.obj", true, false);

   vkdf_model_fill_vertex_buffers(ctx, res->model.cube, true);
}

static void
init_objects(VkdfContext *ctx, DemoResources *res)
{
   VkdfModel *model = res->model.sphere;

   // Create objects
   for (int32_t x = 0; x < NUM_OBJECTS_X; x++) {
      float pos_x = -2.0f * (NUM_OBJECTS_X / 2.0f) + x * 2.0f + 1.0f;
      for (int32_t y = 0; y < NUM_OBJECTS_Y; y++) {
         int32_t idx = y * NUM_OBJECTS_X + x;
         float pos_y = -2.0f * (NUM_OBJECTS_Y / 2.0f) + y * 2.0f + 1.0f;

         res->objs[idx] =
            vkdf_object_new(glm::vec3(pos_x, pos_y, 0.0f), model);
         vkdf_object_set_scale(res->objs[idx], glm::vec3(0.5f, 0.5f, 0.5f));
      }
   }

   // Prepare per-instance vertex buffer with material indices for each
   // mesh instance. The first NUM_OBJECTS indices belong to the materials
   // for each instance of the first mesh, then we have the next NUM_OBJECTS
   // material indices for the second mesh, etc
   VkDeviceSize instance_data_size =
      sizeof(uint32_t) * model->meshes.size() * NUM_OBJECTS;
   res->instance_buf =
      vkdf_create_buffer(ctx,
                         0,
                         instance_data_size,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint32_t *map;
   vkdf_memory_map(ctx, res->instance_buf.mem, 0, instance_data_size,
                   (void **) &map);

   for (uint32_t j = 0; j < model->meshes.size(); j++) {
      assert(model->meshes[j]->material_idx >= 0 &&
             model->meshes[j]->material_idx < (int32_t) model->materials.size());
      for (uint32_t i = 0; i < NUM_OBJECTS; i++) {
         *map = model->meshes[j]->material_idx;
         map++;
      }
   }

   vkdf_memory_unmap(ctx, res->instance_buf.mem, res->instance_buf.mem_props,
                     0, instance_data_size);
}

static void
fill_model_ubo(VkdfContext *ctx, DemoResources *res)
{
   uint8_t *map;
   vkdf_memory_map(ctx, res->M_ubo.mem, 0, VK_WHOLE_SIZE, (void**) &map);

   for (uint32_t i = 0; i < NUM_OBJECTS; i++) {
      VkdfObject *obj = res->objs[i];
      glm::mat4 Model = vkdf_object_get_model_matrix(obj);
      memcpy(map, &Model[0][0], sizeof(glm::mat4));
      map += sizeof(glm::mat4);
   }

   vkdf_memory_unmap(ctx, res->M_ubo.mem, res->M_ubo.mem_props,
                     0, VK_WHOLE_SIZE);
}

static void
init_lights(VkdfContext *ctx, DemoResources *res)
{
   for (int32_t i = 0; i < NUM_LIGHTS; i++) {
      float pos_x = -10.0f + (i / 2)  * 20.0f;
      float pos_y = -10.0f + (i % 2)  * 20.0f;
      res->lights[i] =
         vkdf_light_new_positional(glm::vec4(pos_x, pos_y, 10.0f, 0.0f),
                                   glm::vec4(250.0f, 250.0f, 250.0f, 1.0f),
                                   glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                   glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                   glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
   }
}

static bool
load_hdr_environment_image(VkdfContext *ctx,
                           VkCommandPool cmd_pool,
                           const char *path,
                           VkdfImage *image)
{
   int width, height, components;
   void *data = (void *) stbi_loadf(path, &width, &height, &components, 0);
   if (!data)
      return false;

   vkdf_create_image_from_data(ctx,
                               cmd_pool,
                               width, height,
                               VK_FORMAT_R32G32B32_SFLOAT,
                               false, data,
                               VK_IMAGE_USAGE_SAMPLED_BIT,
                               image);

   stbi_image_free(data);

   return true;
}

static void
init_textures(VkdfContext *ctx, DemoResources *res)
{
   if (!vkdf_load_image_from_file(ctx,
                                  res->cmd_pool,
                                  "./data/albedo.png",
                                  &res->textures[TEX_ALBEDO],
                                  VK_IMAGE_USAGE_SAMPLED_BIT, true)) {
      vkdf_fatal("Failed to load texture image\n");
   }

   if (!vkdf_load_image_from_file(ctx,
                                  res->cmd_pool,
                                  "./data/normal.png",
                                  &res->textures[TEX_NORMAL],
                                  VK_IMAGE_USAGE_SAMPLED_BIT, false)) {
      vkdf_fatal("Failed to load texture image\n");
   }

   if (!vkdf_load_image_from_file(ctx,
                                  res->cmd_pool,
                                  "./data/roughness.png",
                                  &res->textures[TEX_ROUGHNESS],
                                  VK_IMAGE_USAGE_SAMPLED_BIT, false)) {
      vkdf_fatal("Failed to load texture image\n");
   }

   if (!vkdf_load_image_from_file(ctx,
                                  res->cmd_pool,
                                  "./data/metallic.png",
                                  &res->textures[TEX_METALLIC],
                                  VK_IMAGE_USAGE_SAMPLED_BIT, false)) {
      vkdf_fatal("Failed to load texture image\n");
   }

   if (!vkdf_load_image_from_file(ctx,
                                  res->cmd_pool,
                                  "./data/newport_loft.jpg",
                                  &res->textures[TEX_LDR_ENV_MAP],
                                  VK_IMAGE_USAGE_SAMPLED_BIT, true)) {
      vkdf_fatal("Failed to load texture image\n");
   }

   if (!load_hdr_environment_image(ctx,
                                   res->cmd_pool,
                                   "./data/newport_loft.hdr",
                                   &res->textures[TEX_HDR_ENV_MAP])) {
      vkdf_fatal("Failed to load HDR environment image\n");
   }

   if (!load_hdr_environment_image(ctx,
                                   res->cmd_pool,
                                   "./data/newport_loft_irradiance.hdr",
                                   &res->textures[TEX_HDR_IRRADIANCE_MAP])) {
      vkdf_fatal("Failed to load HDR environment image\n");
   }
}

static void
init_resources(VkdfContext *ctx,
               bool enable_texture_mode,
               bool enable_ibl_mode,
               DemoResources *res)
{
   memset(res, 0, sizeof(DemoResources));

   res->enable_texture_mode = enable_texture_mode;
   res->enable_ibl_mode = enable_ibl_mode;

   // Compute View, Projection and Cliip matrices
   init_matrices(res);

   // Load models
   init_models(ctx, res);

   // Create the object and its mesh
   init_objects(ctx, res);

   // Create light sources
   init_lights(ctx, res);

   // Create UBO for View and Projection matrices
   res->VP_ubo = create_ubo(ctx, 2 * sizeof(glm::mat4),
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   vkdf_buffer_map_and_fill(ctx, res->VP_ubo,
                            0, sizeof(glm::mat4),
                            &res->view[0][0]);

   vkdf_buffer_map_and_fill(ctx, res->VP_ubo,
                            sizeof(glm::mat4), sizeof(glm::mat4),
                            &res->projection[0][0]);

   // Create UBO for Model matrix
   res->M_ubo = create_ubo(ctx, NUM_OBJECTS * sizeof(glm::mat4),
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   fill_model_ubo(ctx, res);

   // Create UBO for lights

   // The code below assumes a packed 16-byte aligned array of lights to
   // comply with std140 rules
   assert(sizeof(VkdfLight) % 16 == 0);

   uint32_t lights_size = sizeof(VkdfLight) * NUM_LIGHTS;
   res->light_ubo = create_ubo(ctx, lights_size,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   VkDeviceSize offset = 0;
   for (uint32_t i = 0; i < NUM_LIGHTS; i++) {
      vkdf_buffer_map_and_fill(ctx, res->light_ubo,
                               offset, sizeof(VkdfLight), res->lights[i]);
      offset += sizeof(VkdfLight);
   }

   // Create depth image
   res->depth_image =
      vkdf_create_image(ctx,
                        ctx->width,
                        ctx->height,
                        1,
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_D16_UNORM,
                        0,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);

   // Shaders
   if (res->enable_texture_mode) {
      res->vs_module = vkdf_create_shader_module(ctx, "shader-tex.vert.spv");
      if (res->enable_ibl_mode)
         res->fs_module = vkdf_create_shader_module(ctx, "shader-ibl-tex.frag.spv");
      else
         res->fs_module = vkdf_create_shader_module(ctx, "shader-tex.frag.spv");
   } else {
      res->vs_module = vkdf_create_shader_module(ctx, "shader.vert.spv");
      if (res->enable_ibl_mode)
         res->fs_module = vkdf_create_shader_module(ctx, "shader-ibl.frag.spv");
      else
         res->fs_module = vkdf_create_shader_module(ctx, "shader.frag.spv");
   }

   res->cubemap_vs_module =
      vkdf_create_shader_module(ctx, "shader-ibl-cubemap.vert.spv");
   res->cubemap_fs_module =
      vkdf_create_shader_module(ctx, "shader-ibl-cubemap.frag.spv");

   // Render pass
   res->render_pass = create_render_pass(ctx, res);

   // Framebuffers
   res->framebuffers =
      vkdf_create_framebuffers_for_swap_chain(ctx, res->render_pass,
                                              1, &res->depth_image);

   // Command pool
   res->cmd_pool = vkdf_create_gfx_command_pool(ctx, 0);

   // Descriptor pool
   res->ubo_pool =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16);

   res->sampler_pool =
      vkdf_create_descriptor_pool(ctx,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  256);

   // Load textures
   init_textures(ctx, res);

   // Pipelines
   create_pipeline_layouts(ctx, res);
   create_pipelines(ctx, res);

   // Command buffers
   res->cmd_bufs = g_new(VkCommandBuffer, ctx->swap_chain_length);
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              ctx->swap_chain_length,
                              res->cmd_bufs);

   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      vkdf_command_buffer_begin(res->cmd_bufs[i],
                                VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
      render_pass_commands(ctx, res, i);
      vkdf_command_buffer_end(res->cmd_bufs[i]);
   }
}

static void
update_camera(VkdfContext *ctx, VkdfCamera *cam)
{
   const float mov_speed = 0.15f;
   const float rot_speed = 1.0f;

   float base_speed = 1.0f;

   /* Rotation */
   if (vkdf_platform_key_is_pressed(&ctx->platform, VKDF_KEY_LEFT))
      vkdf_camera_rotate(cam, 0.0f, base_speed * rot_speed, 0.0f);
   else if (vkdf_platform_key_is_pressed(&ctx->platform, VKDF_KEY_RIGHT))
      vkdf_camera_rotate(cam, 0.0f, -base_speed * rot_speed, 0.0f);

   if (vkdf_platform_key_is_pressed(&ctx->platform, VKDF_KEY_PAGE_UP))
      vkdf_camera_rotate(cam, base_speed * rot_speed, 0.0f, 0.0f);
   else if (vkdf_platform_key_is_pressed(&ctx->platform, VKDF_KEY_PAGE_DOWN))
      vkdf_camera_rotate(cam, -base_speed * rot_speed, 0.0f, 0.0f);

   /* Stepping */
   float step_speed = base_speed;
   if (vkdf_platform_key_is_pressed(&ctx->platform, VKDF_KEY_UP))
      step_speed *= mov_speed;
   else if (vkdf_platform_key_is_pressed(&ctx->platform, VKDF_KEY_DOWN))
      step_speed *= -mov_speed;
   else
      return; /* Not stepping */

   vkdf_camera_step(cam, step_speed, 1, 1, 1);
}

static void
scene_update(VkdfContext *ctx, void *data)
{
   DemoResources *res = (DemoResources *) data;

   update_camera(ctx, res->camera);
   res->view = vkdf_camera_get_view_matrix(res->camera);
   vkdf_buffer_map_and_fill(ctx, res->VP_ubo,
                            0, sizeof(glm::mat4), &res->view[0][0]);
}

static void
scene_render(VkdfContext *ctx, void *data)
{
   DemoResources *res = (DemoResources *) data;

   VkPipelineStageFlags pipeline_stages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

   vkdf_command_buffer_execute(ctx,
                               res->cmd_bufs[ctx->swap_chain_index],
                               &pipeline_stages,
                               1, &ctx->acquired_sem[ctx->swap_chain_index],
                               1, &ctx->draw_sem[ctx->swap_chain_index]);
}

static void
destroy_pipeline_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyPipeline(ctx->device, res->pipeline, NULL);
   vkDestroyPipelineLayout(ctx->device, res->pipeline_layout, NULL);

   vkDestroyPipeline(ctx->device, res->cubemap_pipeline, NULL);
   vkDestroyPipelineLayout(ctx->device, res->cubemap_pipeline_layout, NULL);
}

static void
destroy_framebuffer_resources(VkdfContext *ctx, DemoResources *res)
{
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++)
      vkDestroyFramebuffer(ctx->device, res->framebuffers[i], NULL);
   g_free(res->framebuffers);
}

static void
destroy_shader_resources(VkdfContext *ctx, DemoResources *res)
{
  vkDestroyShaderModule(ctx->device, res->vs_module, NULL);
  vkDestroyShaderModule(ctx->device, res->fs_module, NULL);

  vkDestroyShaderModule(ctx->device, res->cubemap_vs_module, NULL);
  vkDestroyShaderModule(ctx->device, res->cubemap_fs_module, NULL);
}

static void
destroy_command_buffer_resources(VkdfContext *ctx, DemoResources *res)
{
   vkFreeCommandBuffers(ctx->device,
                        res->cmd_pool,
                        ctx->swap_chain_length,
                        res->cmd_bufs);
   vkDestroyCommandPool(ctx->device, res->cmd_pool, NULL);
}

static void
destroy_descriptor_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroySampler(ctx->device, res->sampler, NULL);
   vkDestroySampler(ctx->device, res->cubemap_sampler, NULL);

   for (uint32_t i = 0; i < TEX_LAST; i++)
      vkdf_destroy_image(ctx, &res->textures[i]);

   vkFreeDescriptorSets(ctx->device, res->ubo_pool, 1, &res->ubo_set);
   vkFreeDescriptorSets(ctx->device, res->sampler_pool, 1, &res->tex_set);
   vkFreeDescriptorSets(ctx->device, res->sampler_pool, 1, &res->irradiance_tex_set);
   vkFreeDescriptorSets(ctx->device, res->sampler_pool, 1, &res->cubemap_tex_set);

   vkDestroyDescriptorSetLayout(ctx->device, res->ubo_set_layout, NULL);
   vkDestroyDescriptorSetLayout(ctx->device, res->tex_set_layout, NULL);
   vkDestroyDescriptorSetLayout(ctx->device, res->irradiance_tex_set_layout, NULL);
   vkDestroyDescriptorSetLayout(ctx->device, res->cubemap_tex_set_layout, NULL);

   vkDestroyDescriptorPool(ctx->device, res->ubo_pool, NULL);
   vkDestroyDescriptorPool(ctx->device, res->sampler_pool, NULL);
}

static void
destroy_ubo_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyBuffer(ctx->device, res->light_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->light_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->VP_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->VP_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->M_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->M_ubo.mem, NULL);
}

void
cleanup_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyBuffer(ctx->device, res->instance_buf.buf, NULL);
   vkFreeMemory(ctx->device, res->instance_buf.mem, NULL);
   for (uint32_t i = 0; i < NUM_OBJECTS; i++)
      vkdf_object_free(res->objs[i]);

   vkdf_model_free(ctx, res->model.sphere);
   vkdf_model_free(ctx, res->model.cube);

   destroy_pipeline_resources(ctx, res);
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   destroy_descriptor_resources(ctx, res);
   destroy_ubo_resources(ctx, res);
   vkdf_destroy_image(ctx, &res->depth_image);
   destroy_framebuffer_resources(ctx, res);
   destroy_shader_resources(ctx, res);
   destroy_command_buffer_resources(ctx, res);
}

static void
process_cmd_line(int argc, char **argv,
                 bool *enable_texture_mode, bool *enable_ibl_mode)
{
   if (argc != 2 && argc != 3) {
      printf("Usage: ./pbr [-s, -t] [--ibl-off]\n");
      exit(1);
   }

   *enable_ibl_mode = true;

   for (int32_t i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "-s")) {
         *enable_texture_mode = false;
      } else if (!strcmp(argv[i], "-t")) {
         *enable_texture_mode = true;
      } else if (!strcmp(argv[i], "-")) {
         *enable_texture_mode = true;
      } else if (!strcmp(argv[i], "--ibl-off")) {
         *enable_ibl_mode = false;
      } else {
         printf("Usage: ./pbr [-s, -t] [--ibl-off]\n");
         exit(1);
      }
   }
}

int
main(int argc, char *argv[])
{
   VkdfContext ctx;
   DemoResources resources;

   bool enable_texture_mode;
   bool enable_ibl_mode;
   process_cmd_line(argc, argv, &enable_texture_mode, &enable_ibl_mode);

   srandom(time(NULL));

   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, false, false, ENABLE_DEBUG);
   init_resources(&ctx, enable_texture_mode, enable_ibl_mode, &resources);

   vkdf_event_loop_run(&ctx, scene_update, scene_render, &resources);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
