#include "vkdf.hpp"

#include <stdlib.h>
#include <time.h>

// ----------------------------------------------------------------------------
// Renders a scene with a spotlight and shadows
// ----------------------------------------------------------------------------

// Window size
const uint32_t WIN_WIDTH  = 800;
const uint32_t WIN_HEIGHT = 600;
const bool FULLSCREEN     = false;

// Scene depth range
const float SCENE_NEAR =   0.1f;
const float SCENE_FAR  = 100.0f;

// Number of objects in the scene
const int32_t NUM_CUBES = 3;

// Number of floor tiles and their size
const int32_t ROOM_WIDTH = 20;
const int32_t ROOM_DEPTH = 20;
const int32_t TILE_WIDTH = 2.0f;
const int32_t TILE_DEPTH = 2.0f;
const int32_t NUM_TILES  = ROOM_WIDTH * ROOM_DEPTH;

// Depth range of the light. We want this to be as tightly packed as possible
const float LIGHT_NEAR =  0.1f;
const float LIGHT_FAR  = 50.0f;

// Shadow map resolution. Lowering this may cause more self-shadowing
// artifacts and require to increase depth bias factors
const uint32_t SHADOW_MAP_WIDTH  = 2048;
const uint32_t SHADOW_MAP_HEIGHT = 2048;

// Shadow map depth bias factors. Too large values can cause shadows to
// be dettached from the objects that cast them
const float SHADOW_MAP_DEPTH_BIAS_CONST = 4.0f;
const float SHADOW_MAP_DEPTH_BIAS_SLOPE = 1.8f;

// For debugging only (shows the shadow map texture on the top-left corner)
const bool SHOW_SHADOW_MAP_TILE = true;
const uint32_t SHADOW_MAP_TILE_WIDTH  = 200;
const uint32_t SHADOW_MAP_TILE_HEIGHT = 150;

// Enable light movement
const bool enable_dynamic_lights = true;

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer *cmd_bufs;
   VkRenderPass render_pass;
   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkPipelineCache pipeline_cache;
   VkShaderModule vs_module;
   VkShaderModule fs_module;
   VkFramebuffer *framebuffers;
   VkdfImage depth_image;

   // Pool for UBO descriptor
   VkDescriptorPool ubo_pool;

   // UBOs for View/Projection and Model matrices
   VkdfBuffer VP_ubo;
   VkdfBuffer M_cubes_ubo;
   VkdfBuffer M_tiles_ubo;

   // Descriptor sets for scene MVP UBO bindings
   VkDescriptorSetLayout MVP_set_layout;
   VkDescriptorSet MVP_cubes_descriptor_set;
   VkDescriptorSet MVP_tiles_descriptor_set;

   // Scene draw semaphore
   VkSemaphore *scene_draw_sem;

   // View/Projection matrices
   glm::mat4 view;
   glm::mat4 projection;

   // Objects (cubes and tiles)
   VkdfModel *cube_model;
   VkdfModel *tile_model;
   VkdfMesh *cube_mesh;
   VkdfMesh *tile_mesh;
   VkdfObject *cubes[NUM_CUBES];
   VkdfObject *tiles[NUM_TILES];

   // Vertex buffer with material indices for each object
   VkdfBuffer cube_material_buf;
   VkdfBuffer tile_material_buf;

   // Materials UBOs
   VkdfBuffer tile_materials_ubo;
   VkdfBuffer cube_materials_ubo;

   // Scene descriptors for materials
   VkDescriptorSetLayout Materials_set_layout;
   VkDescriptorSet tile_materials_descriptor_set;
   VkDescriptorSet cube_materials_descriptor_set;

   // Light source
   VkdfLight light;
   glm::mat4 light_projection;
   glm::mat4 light_view;

   // Light UBOs (Light description and View/Projection matrix)
   VkdfBuffer Light_ubo;
   VkdfBuffer Light_VP_ubo;

   // Scene descriptors for the light source
   VkDescriptorSetLayout Light_set_layout;
   VkDescriptorSet Light_descriptor_set;

   // Camera
   VkdfCamera *camera;

   // Shadow map
   VkdfImage shadow_map;

   // Shadow map rendering pipeline
   VkPipeline shadow_pipeline;
   VkPipelineCache shadow_pipeline_cache;
   VkShaderModule shadow_vs_module;
   VkPipelineLayout shadow_pipeline_layout;
   VkDescriptorSetLayout shadow_map_mvp_set_layout;
   VkDescriptorSet shadow_map_mvp_descriptor_set;

   // Shadow map render renderpass and command buffers
   VkRenderPass shadow_render_pass;
   VkCommandBuffer shadow_cmd_buf;
   VkFramebuffer shadow_framebuffer;
   VkSemaphore shadow_draw_sem;

   // Shadow map sampler
   VkDescriptorPool sampler_pool;
   VkSampler shadow_map_sampler;
   VkDescriptorSetLayout shadow_sampler_set_layout;
   VkDescriptorSet shadow_sampler_descriptor_set;

   // UI tile rendering resources (debugging only)
   VkdfMesh *ui_tile_mesh;
   glm::mat4 ui_tile_mvp;
   VkdfBuffer ui_tile_mvp_ubo;
   VkPipelineLayout ui_tile_pipeline_layout;
   VkPipeline ui_tile_pipeline;
   VkDescriptorSetLayout ui_tile_mvp_set_layout;
   VkDescriptorSet ui_tile_mvp_descriptor_set;
   VkShaderModule ui_tile_vs_module;
   VkShaderModule ui_tile_fs_module;
   VkRenderPass ui_tile_render_pass;
   VkCommandBuffer *ui_tile_cmd_bufs;
} SceneResources;

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

static void
create_and_fill_material_buffers(VkdfContext *ctx, SceneResources *res)
{
   res->tile_material_buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flag
                         NUM_TILES * sizeof(uint32_t),         // size
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,    // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type

   uint32_t tile_materials[NUM_TILES];
   for (uint32_t i = 0; i < NUM_TILES; i++)
      tile_materials[i] = res->tiles[i]->material_idx_base;

   vkdf_buffer_map_and_fill(ctx,
                            res->tile_material_buf,
                            0,
                            NUM_TILES * sizeof(uint32_t),
                            tile_materials);

   res->cube_material_buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flag
                         NUM_CUBES * sizeof(uint32_t),         // size
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,    // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type

   uint32_t cube_materials[NUM_CUBES];
   for (uint32_t i = 0; i < NUM_CUBES; i++)
      cube_materials[i] = res->cubes[i]->material_idx_base;

   vkdf_buffer_map_and_fill(ctx,
                            res->cube_material_buf,
                            0,
                            NUM_CUBES * sizeof(uint32_t),
                            cube_materials);
}

static void
init_ui_tile_mesh(VkdfContext *ctx, SceneResources *res)
{
   res->ui_tile_mesh = vkdf_2d_tile_mesh_new(ctx);
   vkdf_mesh_fill_vertex_buffer(ctx, res->ui_tile_mesh);
}

static VkRenderPass
create_render_pass(VkdfContext *ctx, SceneResources *res)
{
   VkAttachmentDescription attachments[2];

   // Single color attachment
   attachments[0].format = ctx->surface_format;
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

   // Attachment references from subpasses
   VkAttachmentReference color_ref;
   color_ref.attachment = 0;
   color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkAttachmentReference depth_ref;
   depth_ref.attachment = 1;
   depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   // Subpass 0: render color and depth output
   VkSubpassDescription subpass[1];
   subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass[0].flags = 0;
   subpass[0].inputAttachmentCount = 0;
   subpass[0].pInputAttachments = NULL;
   subpass[0].colorAttachmentCount = 1;
   subpass[0].pColorAttachments = &color_ref;
   subpass[0].pResolveAttachments = NULL;
   subpass[0].pDepthStencilAttachment = &depth_ref;
   subpass[0].preserveAttachmentCount = 0;
   subpass[0].pPreserveAttachments = NULL;

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 2;
   rp_info.pAttachments = attachments;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass;
   VK_CHECK(vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass));

   return render_pass;
}

static VkRenderPass
create_shadow_render_pass(VkdfContext *ctx, SceneResources *res)
{
   VkAttachmentDescription attachments[2];

   // Depth attachment (shadow map)
   attachments[0].format = res->depth_image.format;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   attachments[0].flags = 0;

   // Attachment references from subpasses
   VkAttachmentReference depth_ref;
   depth_ref.attachment = 0;
   depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   // Subpass 0: shadow map rendering
   VkSubpassDescription subpass[1];
   subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass[0].flags = 0;
   subpass[0].inputAttachmentCount = 0;
   subpass[0].pInputAttachments = NULL;
   subpass[0].colorAttachmentCount = 0;
   subpass[0].pColorAttachments = NULL;
   subpass[0].pResolveAttachments = NULL;
   subpass[0].pDepthStencilAttachment = &depth_ref;
   subpass[0].preserveAttachmentCount = 0;
   subpass[0].pPreserveAttachments = NULL;

   // Create render pass
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

   VkRenderPass render_pass;
   VK_CHECK(vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass));

   return render_pass;
}

static VkRenderPass
create_ui_tile_render_pass(VkdfContext *ctx, SceneResources *res)
{
   VkAttachmentDescription attachments[2];

   // Single color attachment
   attachments[0].format = ctx->surface_format;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   attachments[0].flags = 0;

   // Depth attachment (unused)
   attachments[1].format = res->depth_image.format;
   attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[1].finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[1].flags = 0;

   // Attachment references from subpasses
   VkAttachmentReference color_ref;
   color_ref.attachment = 0;
   color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   // Subpass 0: render tile
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

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 2;
   rp_info.pAttachments = attachments;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass;
   VK_CHECK(vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass));

   return render_pass;
}

static void
render_pass_commands(VkdfContext *ctx, SceneResources *res, uint32_t index)
{
   VkClearValue clear_values[2];
   clear_values[0].color.float32[0] = 0.0f;
   clear_values[0].color.float32[1] = 0.0f;
   clear_values[0].color.float32[2] = 0.0f;
   clear_values[0].color.float32[3] = 1.0f;
   clear_values[1].depthStencil.depth = 1.0f;
   clear_values[1].depthStencil.stencil = 0;

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

   const VkdfMesh *cube_mesh = res->cubes[0]->model->meshes[0];
   const VkdfMesh *tile_mesh = res->tiles[0]->model->meshes[0];

   // ------------------- Subpass 0: scene rendering ------------------- 

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

   // Pipeline
   vkCmdBindPipeline(res->cmd_bufs[index], VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipeline);

   // Bind static descriptor sets for tiles and cubes (light and shadow map)
   VkDescriptorSet descriptor_sets[] = {
      res->Light_descriptor_set,
      res->shadow_sampler_descriptor_set
   };
   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           1,                        // First decriptor set
                           2,                        // Descriptor set count
                           descriptor_sets,          // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // --- Render scene cubes

   // Bind descriptor sets with cube data (Model matrices and materials)
   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                        // First decriptor set
                           1,                        // Descriptor set count
                           &res->MVP_cubes_descriptor_set,          // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           3,                        // First decriptor set
                           1,                        // Descriptor set count
                           &res->cube_materials_descriptor_set, // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // Vertex buffer: position, normal
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->cmd_bufs[index],
                          0,                           // Start Binding
                          1,                           // Binding Count
                          &cube_mesh->vertex_buf.buf,  // Buffers
                          offsets);                    // Offsets


   // Vertex buffer: material indices
   vkCmdBindVertexBuffers(res->cmd_bufs[index],
                          1,                            // Start Binding
                          1,                            // Binding Count
                          &res->cube_material_buf.buf,  // Buffers
                          offsets);                     // Offsets


   // Draw
   vkCmdDraw(res->cmd_bufs[index],
             cube_mesh->vertices.size(),           // vertex count
             NUM_CUBES,                            // instance count
             0,                                    // first vertex
             0);                                   // first instance

   // --- Render scene tiles

   // Bind descriptor sets with tile data (Model matrices and materials)
   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                        // First decriptor set
                           1,                        // Descriptor set count
                           &res->MVP_tiles_descriptor_set,          // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           3,                        // First decriptor set
                           1,                        // Descriptor set count
                           &res->tile_materials_descriptor_set, // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // Vertex buffer: position, normal
   vkCmdBindVertexBuffers(res->cmd_bufs[index],
                          0,                           // Start Binding
                          1,                           // Binding Count
                          &tile_mesh->vertex_buf.buf,  // Buffers
                          offsets);                    // Offsets


   // Vertex buffer: material indices
   vkCmdBindVertexBuffers(res->cmd_bufs[index],
                          1,                            // Start Binding
                          1,                            // Binding Count
                          &res->tile_material_buf.buf,  // Buffers
                          offsets);                     // Offsets

   // Draw
   vkCmdDraw(res->cmd_bufs[index],
             tile_mesh->vertices.size(),           // vertex count
             NUM_TILES,                            // instance count
             0,                                    // first vertex
             0);                                   // first instance

   vkCmdEndRenderPass(res->cmd_bufs[index]);
}

static void
shadow_render_pass_commands(VkdfContext *ctx, SceneResources *res)
{
   VkClearValue clear_values[1];
   clear_values[0].depthStencil.depth = 1.0f;
   clear_values[0].depthStencil.stencil = 0;

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->shadow_render_pass;
   rp_begin.framebuffer = res->shadow_framebuffer;
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = SHADOW_MAP_WIDTH;
   rp_begin.renderArea.extent.height = SHADOW_MAP_HEIGHT;
   rp_begin.clearValueCount = 1;
   rp_begin.pClearValues = clear_values;

   vkCmdBeginRenderPass(res->shadow_cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   /* No need to render tiles to the shadow map */
   const VkdfMesh *mesh = res->cubes[0]->model->meshes[0];

   // ------------------- Subpass 0: scene rendering ------------------- 

   // Viewport and Scissor
   VkViewport viewport;
   viewport.height = SHADOW_MAP_HEIGHT;
   viewport.width = SHADOW_MAP_WIDTH;
   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;
   viewport.x = 0;
   viewport.y = 0;
   vkCmdSetViewport(res->shadow_cmd_buf, 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = SHADOW_MAP_WIDTH;
   scissor.extent.height = SHADOW_MAP_HEIGHT;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(res->shadow_cmd_buf, 0, 1, &scissor);

   // Pipeline
   vkCmdBindPipeline(res->shadow_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->shadow_pipeline);

   // Vertex buffer: position, normal (only position is used)
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->shadow_cmd_buf,
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &mesh->vertex_buf.buf,   // Buffers
                          offsets);                // Offsets

   // Bind static MVP descriptor set once
   vkCmdBindDescriptorSets(res->shadow_cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->shadow_pipeline_layout,
                           0,                        // First decriptor set
                           1,                        // Descriptor set count
                           &res->shadow_map_mvp_descriptor_set, // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // Draw
   vkCmdDraw(res->shadow_cmd_buf,
             mesh->vertices.size(),                // vertex count
             NUM_CUBES,                            // instance count
             0,                                    // first vertex
             0);                                   // first instance

   vkCmdEndRenderPass(res->shadow_cmd_buf);
}

static void
ui_tile_render_pass_commands(VkdfContext *ctx, SceneResources *res,
                             uint32_t index)
{
   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->ui_tile_render_pass;
   rp_begin.framebuffer = res->framebuffers[index];
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = SHADOW_MAP_TILE_WIDTH;
   rp_begin.renderArea.extent.height = SHADOW_MAP_TILE_HEIGHT;
   rp_begin.clearValueCount = 0;
   rp_begin.pClearValues = NULL;

   vkCmdBeginRenderPass(res->ui_tile_cmd_bufs[index],
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   const VkdfMesh *mesh = res->ui_tile_mesh;

   // ------------------- Subpass 0: tile rendering ------------------- 

   // Viewport and Scissor
   VkViewport viewport;
   viewport.height = SHADOW_MAP_TILE_HEIGHT;
   viewport.width = SHADOW_MAP_TILE_WIDTH;
   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;
   viewport.x = 0;
   viewport.y = 0;
   vkCmdSetViewport(res->ui_tile_cmd_bufs[index], 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = SHADOW_MAP_TILE_WIDTH;
   scissor.extent.height = SHADOW_MAP_TILE_HEIGHT;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(res->ui_tile_cmd_bufs[index], 0, 1, &scissor);

   // Pipeline
   vkCmdBindPipeline(res->ui_tile_cmd_bufs[index],
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->ui_tile_pipeline);

   // Vertex buffer: position, uv
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->ui_tile_cmd_bufs[index],
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &mesh->vertex_buf.buf,   // Buffers
                          offsets);                // Offsets

   // Bind static MVP descriptor set once
   vkCmdBindDescriptorSets(res->ui_tile_cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->ui_tile_pipeline_layout,
                           0,                        // First descriptor set
                           1,                        // Descriptor set count
                           &res->ui_tile_mvp_descriptor_set, // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // Bind shadow map sampler
   vkCmdBindDescriptorSets(res->ui_tile_cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->ui_tile_pipeline_layout,
                           1,                            // Second descriptor set
                           1,                            // Descriptor set count
                           &res->shadow_sampler_descriptor_set, // Descriptor sets
                           0,                            // Dynamic offset count
                           NULL);                        // Dynamic offsets

   // Draw
   vkCmdDraw(res->ui_tile_cmd_bufs[index],
             mesh->vertices.size(),                // vertex count
             1,                                    // instance count
             0,                                    // first vertex
             0);                                   // first instance

   vkCmdEndRenderPass(res->ui_tile_cmd_bufs[index]);
}


static VkPipelineLayout
create_pipeline_layout(VkdfContext *ctx, SceneResources *res)
{
   VkDescriptorSetLayout layouts[] = {
      res->MVP_set_layout,
      res->Light_set_layout,
      res->shadow_sampler_set_layout,
      res->Materials_set_layout
   };

   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 4;
   pipeline_layout_info.pSetLayouts = layouts;
   pipeline_layout_info.flags = 0;

   VkPipelineLayout pipeline_layout;
   VK_CHECK(vkCreatePipelineLayout(ctx->device,
                                   &pipeline_layout_info,
                                   NULL,
                                   &pipeline_layout));

   return pipeline_layout;
}

static VkPipelineLayout
create_shadow_pipeline_layout(VkdfContext *ctx, SceneResources *res)
{
   VkDescriptorSetLayout layouts[1] = {
      res->shadow_map_mvp_set_layout,
   };

   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 1;
   pipeline_layout_info.pSetLayouts = layouts;
   pipeline_layout_info.flags = 0;

   VkPipelineLayout pipeline_layout;
   VK_CHECK(vkCreatePipelineLayout(ctx->device,
                                   &pipeline_layout_info,
                                   NULL,
                                   &pipeline_layout));

   return pipeline_layout;
}

static VkPipelineLayout
create_ui_tile_pipeline_layout(VkdfContext *ctx, SceneResources *res)
{
   VkDescriptorSetLayout layouts[2] = {
      res->ui_tile_mvp_set_layout,
      res->shadow_sampler_set_layout,
   };

   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 2;
   pipeline_layout_info.pSetLayouts = layouts;
   pipeline_layout_info.flags = 0;

   VkPipelineLayout pipeline_layout;
   VK_CHECK(vkCreatePipelineLayout(ctx->device,
                                   &pipeline_layout_info,
                                   NULL,
                                   &pipeline_layout));

   return pipeline_layout;
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

static inline float
vec3_module(glm::vec3 p, int xaxis, int yaxis, int zaxis)
{
   return sqrtf(p.x * p.x * xaxis + p.y * p.y * yaxis + p.z * p.z * zaxis);
}

static void
init_matrices(SceneResources *res)
{
   glm::mat4 clip = glm::mat4(1.0f, 0.0f, 0.0f, 0.0f,
                              0.0f,-1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 0.5f, 0.0f,
                              0.0f, 0.0f, 0.5f, 1.0f);

   res->projection =  clip * glm::perspective(glm::radians(45.0f),
                                              (float) WIN_WIDTH / WIN_HEIGHT,
                                              SCENE_NEAR, SCENE_FAR);

   res->ui_tile_mvp = clip * glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f);

   res->light_projection = clip *
      glm::perspective(glm::radians(45.0f),
                       (float) SHADOW_MAP_WIDTH / SHADOW_MAP_HEIGHT,
                       LIGHT_NEAR, LIGHT_FAR);
}

static void inline
create_command_buffers(VkdfContext *ctx, SceneResources *res)
{
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

static void inline
create_shadow_command_buffers(VkdfContext *ctx, SceneResources *res)
{
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1,
                              &res->shadow_cmd_buf);

   vkdf_command_buffer_begin(res->shadow_cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
   shadow_render_pass_commands(ctx, res);
   vkdf_command_buffer_end(res->shadow_cmd_buf);
}

static void inline
create_ui_tile_command_buffers(VkdfContext *ctx, SceneResources *res)
{
   res->ui_tile_cmd_bufs = g_new(VkCommandBuffer, ctx->swap_chain_length);
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              ctx->swap_chain_length,
                              res->ui_tile_cmd_bufs);

   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      vkdf_command_buffer_begin(res->ui_tile_cmd_bufs[i],
                                VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
      ui_tile_render_pass_commands(ctx, res, i);
      vkdf_command_buffer_end(res->ui_tile_cmd_bufs[i]);
   }
}

static VkdfImage
create_depth_image(VkdfContext *ctx, uint32_t width, uint32_t height,
                   VkImageUsageFlagBits usage)
{
   return vkdf_create_image(ctx,
                            width,
                            height,
                            1,
                            VK_IMAGE_TYPE_2D,
                            VK_FORMAT_D32_SFLOAT,
                            0,
                            usage,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT,
                            VK_IMAGE_VIEW_TYPE_2D);
}

static void
init_meshes(VkdfContext *ctx, SceneResources *res)
{
   res->cube_mesh = vkdf_cube_mesh_new(ctx);
   vkdf_mesh_fill_vertex_buffer(ctx, res->cube_mesh);

   res->cube_model = vkdf_model_new();

   VkdfMaterial red;
   red.diffuse = glm::vec4(0.5f, 0.0f, 0.0f, 1.0f);
   red.ambient = glm::vec4(0.5f, 0.0f, 0.0f, 1.0f);
   red.specular = glm::vec4(1.0f, 0.75f, 0.75f, 1.0f);
   red.shininess = 48.0f;

   VkdfMaterial green;
   green.diffuse = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f);
   green.ambient = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f);
   green.specular = glm::vec4(0.75f, 1.0f, 0.75f, 1.0f);
   green.shininess = 48.0f;

   VkdfMaterial blue;
   blue.diffuse = glm::vec4(0.0f, 0.0f, 0.5f, 1.0f);
   blue.ambient = glm::vec4(0.0f, 0.0f, 0.5f, 1.0f);
   blue.specular = glm::vec4(0.75f, 0.75f, 1.0f, 1.0f);
   blue.shininess = 48.0f;

   vkdf_model_add_mesh(res->cube_model, res->cube_mesh);
   vkdf_model_add_material(res->cube_model, &red);
   vkdf_model_add_material(res->cube_model, &green);
   vkdf_model_add_material(res->cube_model, &blue);

   res->tile_mesh = vkdf_tile_mesh_new(ctx);
   vkdf_mesh_fill_vertex_buffer(ctx, res->tile_mesh);

   res->tile_model = vkdf_model_new();

   VkdfMaterial white;
   white.diffuse = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
   white.ambient = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
   white.specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
   white.shininess = 24.0f;

   VkdfMaterial black;
   black.diffuse = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
   black.ambient = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
   black.specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
   black.shininess = 24.0f;

   vkdf_model_add_mesh(res->tile_model, res->tile_mesh);
   vkdf_model_add_material(res->tile_model, &white);
   vkdf_model_add_material(res->tile_model, &black);
}

static void
init_objects(VkdfContext *ctx, SceneResources *res)
{
   // Create room tiles
   for (uint32_t x = 0; x < ROOM_WIDTH; x++) {
      uint32_t color_idx = x % 2;
      for (uint32_t z = 0; z < ROOM_DEPTH; z++) {
         uint32_t idx = x * ROOM_DEPTH + z;

         float tx = (-ROOM_WIDTH * TILE_WIDTH + TILE_WIDTH) / 2.0f +
                    TILE_WIDTH * x;
         float tz = (-ROOM_DEPTH * TILE_DEPTH + TILE_DEPTH) / 2.0f +
                    TILE_DEPTH * z;
         glm::vec3 pos = glm::vec3(tx, 0.0f, tz);

         res->tiles[idx] = vkdf_object_new_from_model(pos, res->tile_model);
         vkdf_object_set_material_idx_base(res->tiles[idx],
                                           (color_idx + z) % 2);
         vkdf_object_set_scale(res->tiles[idx],
                               glm::vec3(TILE_WIDTH / 2.0f,
                                         1.0f,
                                         TILE_DEPTH / 2.0f));
      }
   }

   // Create scene cubes
   uint32_t idx = 0;
   res->cubes[idx] =
      vkdf_object_new_from_model(glm::vec3(0.0f, 2.0f, 0.0f), res->cube_model);
   vkdf_object_set_material_idx_base(res->cubes[idx],  idx % 3);
   vkdf_object_set_scale(res->cubes[idx],
                         glm::vec3(1.0f, 3.0f, 1.0f));

   idx++;
   res->cubes[idx] =
      vkdf_object_new_from_model(glm::vec3(5.0f, 2.0f, -5.0f), res->cube_model);
   vkdf_object_set_material_idx_base(res->cubes[idx], idx % 3);
   vkdf_object_set_scale(res->cubes[idx],
                         glm::vec3(1.0f, 6.0f, 1.0f));
   res->cubes[idx]->rot = glm::vec3(-25.0f, 35.0f, 0.0f);

   idx++;
   res->cubes[idx] =
      vkdf_object_new_from_model(glm::vec3(-9.0f, 2.0f, -9.0f), res->cube_model);
   vkdf_object_set_material_idx_base(res->cubes[idx], idx % 3);
   vkdf_object_set_scale(res->cubes[idx],
                         glm::vec3(1.0f, 4.0f, 1.0f));
   res->cubes[idx]->rot = glm::vec3(0.0f, 0.0f, 30.0f);

   assert(++idx == NUM_CUBES);
}

static inline void
default_pipeline_input_assembly_state(VkPipelineInputAssemblyStateCreateInfo *ia)
{
   ia->sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   ia->pNext = NULL;
   ia->flags = 0;
   ia->primitiveRestartEnable = VK_FALSE;
   ia->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

static inline void
default_pipeline_viewport_state(VkPipelineViewportStateCreateInfo *vp)
{
   vp->sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   vp->pNext = NULL;
   vp->flags = 0;
   vp->viewportCount = 1;
   vp->scissorCount = 1;
   vp->pScissors = NULL;
   vp->pViewports = NULL;
}

static inline void
default_pipeline_rasterization_state(VkPipelineRasterizationStateCreateInfo *rs)
{
   rs->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
   rs->pNext = NULL;
   rs->flags = 0;
   rs->polygonMode = VK_POLYGON_MODE_FILL;
   rs->cullMode = VK_CULL_MODE_BACK_BIT;
   rs->frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
   rs->depthClampEnable = VK_FALSE;
   rs->rasterizerDiscardEnable = VK_FALSE;
   rs->depthBiasEnable = VK_FALSE;
   rs->depthBiasConstantFactor = 0;
   rs->depthBiasClamp = 0;
   rs->depthBiasSlopeFactor = 0;
   rs->lineWidth = 1.0f;
}

static inline void
default_pipeline_multisample_state(VkPipelineMultisampleStateCreateInfo *ms)
{
   ms->sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
   ms->pNext = NULL;
   ms->flags = 0;
   ms->pSampleMask = NULL;
   ms->rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
   ms->sampleShadingEnable = VK_FALSE;
   ms->alphaToCoverageEnable = VK_FALSE;
   ms->alphaToOneEnable = VK_FALSE;
   ms->minSampleShading = 0.0;
}

static inline void
default_pipeline_depth_stencil_state(VkPipelineDepthStencilStateCreateInfo *ds)
{
   ds->sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
   ds->pNext = NULL;
   ds->flags = 0;
   ds->depthTestEnable = VK_TRUE;
   ds->depthWriteEnable = VK_TRUE;
   ds->depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
   ds->depthBoundsTestEnable = VK_FALSE;
   ds->minDepthBounds = 0;
   ds->maxDepthBounds = 0;
   ds->stencilTestEnable = VK_FALSE;
   ds->back.failOp = VK_STENCIL_OP_KEEP;
   ds->back.passOp = VK_STENCIL_OP_KEEP;
   ds->back.compareOp = VK_COMPARE_OP_ALWAYS;
   ds->back.compareMask = 0;
   ds->back.reference = 0;
   ds->back.depthFailOp = VK_STENCIL_OP_KEEP;
   ds->back.writeMask = 0;
   ds->front = ds->back;
}

static inline void
default_pipeline_blend_state(VkPipelineColorBlendStateCreateInfo *cb)
{
   static VkPipelineColorBlendAttachmentState att_state[1];

   att_state[0].colorWriteMask = 0xf;
   att_state[0].blendEnable = VK_FALSE;
   att_state[0].alphaBlendOp = VK_BLEND_OP_ADD;
   att_state[0].colorBlendOp = VK_BLEND_OP_ADD;
   att_state[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
   att_state[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   att_state[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
   att_state[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

   cb->sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
   cb->flags = 0;
   cb->pNext = NULL;
   cb->attachmentCount = 0;
   cb->pAttachments = att_state;
   cb->logicOpEnable = VK_FALSE;
   cb->logicOp = VK_LOGIC_OP_COPY;
   cb->blendConstants[0] = 1.0f;
   cb->blendConstants[1] = 1.0f;
   cb->blendConstants[2] = 1.0f;
   cb->blendConstants[3] = 1.0f;
}

static inline void
default_pipeline_dynamic_state(VkPipelineDynamicStateCreateInfo *dsi)
{
   static VkDynamicState ds_enables[VK_DYNAMIC_STATE_RANGE_SIZE];

   int ds_count = 0;
   memset(ds_enables, 0, sizeof(ds_enables));
   ds_enables[ds_count++] = VK_DYNAMIC_STATE_SCISSOR;
   ds_enables[ds_count++] = VK_DYNAMIC_STATE_VIEWPORT;

   dsi->sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   dsi->pNext = NULL;
   dsi->flags = 0;
   dsi->pDynamicStates = ds_enables;
   dsi->dynamicStateCount = ds_count;
}

/* Pipeline used to render the final scene */
static inline VkPipeline
create_pipeline(VkdfContext *ctx, SceneResources *res, bool init_cache)
{
   if (init_cache) {
      VkPipelineCacheCreateInfo info;
      info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
      info.pNext = NULL;
      info.initialDataSize = 0;
      info.pInitialData = NULL;
      info.flags = 0;
      VK_CHECK(vkCreatePipelineCache(ctx->device, &info, NULL,
                                     &res->pipeline_cache));
   }

   VkVertexInputBindingDescription vi_binding[2];
   VkVertexInputAttributeDescription vi_attribs[3];

   // Vertex attribute binding 0: position, normal
   vi_binding[0].binding = 0;
   vi_binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding[0].stride = 2 * sizeof(glm::vec3);

   // Vertex attribute binding 1: material index
   vi_binding[1].binding = 1;
   vi_binding[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
   vi_binding[1].stride = sizeof(uint32_t);

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

   // binding 1, location 2: material index
   vi_attribs[2].binding = 1;
   vi_attribs[2].location = 2;
   vi_attribs[2].format = VK_FORMAT_R32_UINT;
   vi_attribs[2].offset = 0;

   return vkdf_create_gfx_pipeline(ctx,
                                   &res->pipeline_cache,
                                   2,
                                   vi_binding,
                                   3,
                                   vi_attribs,
                                   true,
                                   res->render_pass,
                                   res->pipeline_layout,
                                   VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                   VK_CULL_MODE_NONE,
                                   res->vs_module,
                                   res->fs_module);
}

/* Pipeline used to render the shadow map. Only requires a shader vertex
 * and makes use of DepthBias functionality in the rasterization state.
 */
static inline VkPipeline
create_shadow_pipeline(VkdfContext *ctx, SceneResources *res, bool init_cache)
{
   if (init_cache) {
      VkPipelineCacheCreateInfo info;
      info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
      info.pNext = NULL;
      info.initialDataSize = 0;
      info.pInitialData = NULL;
      info.flags = 0;
      VK_CHECK(vkCreatePipelineCache(ctx->device, &info, NULL,
                                     &res->shadow_pipeline_cache));
   }

   VkPipelineInputAssemblyStateCreateInfo ia;
   default_pipeline_input_assembly_state(&ia);

   VkPipelineViewportStateCreateInfo vp;
   default_pipeline_viewport_state(&vp);

   VkPipelineMultisampleStateCreateInfo ms;
   default_pipeline_multisample_state(&ms);

   VkPipelineDepthStencilStateCreateInfo ds;
   default_pipeline_depth_stencil_state(&ds);

   VkPipelineColorBlendStateCreateInfo cb;
   default_pipeline_blend_state(&cb);

   VkPipelineDynamicStateCreateInfo dsi;
   default_pipeline_dynamic_state(&dsi);

   // Rasterization (use depth bias to prevent self-shadowing artifacts)
   VkPipelineRasterizationStateCreateInfo rs;
   default_pipeline_rasterization_state(&rs);
   rs.depthBiasEnable = VK_TRUE;
   rs.depthBiasConstantFactor = SHADOW_MAP_DEPTH_BIAS_CONST;
   rs.depthBiasSlopeFactor = SHADOW_MAP_DEPTH_BIAS_SLOPE;
   rs.depthBiasClamp = 0.0f;

   // Vertex input
   VkVertexInputBindingDescription vi_binding[1];
   VkVertexInputAttributeDescription vi_attribs[1];

   // Vertex attribute binding 0, location 0: position
   vi_binding[0].binding = 0;
   vi_binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding[0].stride = 2 * sizeof(glm::vec3); // positon and normal are stored together

   vi_attribs[0].binding = 0;
   vi_attribs[0].location = 0;
   vi_attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
   vi_attribs[0].offset = 0;

   VkPipelineVertexInputStateCreateInfo vi;
   vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
   vi.pNext = NULL;
   vi.flags = 0;
   vi.vertexBindingDescriptionCount = 1;
   vi.pVertexBindingDescriptions = vi_binding;
   vi.vertexAttributeDescriptionCount = 1;
   vi.pVertexAttributeDescriptions = vi_attribs;

   // Shader stages
   VkPipelineShaderStageCreateInfo shader_stages[1];
   vkdf_pipeline_fill_shader_stage_info(&shader_stages[0],
                                        VK_SHADER_STAGE_VERTEX_BIT,
                                        res->shadow_vs_module);

   // Create pipeline
   VkPipeline pipeline;
   VkGraphicsPipelineCreateInfo pipeline_info;
   pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   pipeline_info.pNext = NULL;
   pipeline_info.layout = res->shadow_pipeline_layout;
   pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
   pipeline_info.basePipelineIndex = 0;
   pipeline_info.flags = 0;
   pipeline_info.pVertexInputState = &vi;
   pipeline_info.pInputAssemblyState = &ia;
   pipeline_info.pTessellationState = NULL;
   pipeline_info.pViewportState = &vp;
   pipeline_info.pRasterizationState = &rs;
   pipeline_info.pMultisampleState = &ms;
   pipeline_info.pDepthStencilState = &ds;
   pipeline_info.pColorBlendState = &cb;
   pipeline_info.pDynamicState = &dsi;
   pipeline_info.pStages = shader_stages;
   pipeline_info.stageCount = 1;
   pipeline_info.renderPass = res->shadow_render_pass;
   pipeline_info.subpass = 0;

   VK_CHECK(vkCreateGraphicsPipelines(ctx->device,
                                      res->shadow_pipeline_cache,
                                      1,
                                      &pipeline_info,
                                      NULL,
                                      &pipeline));

   return pipeline;
}

/* Pipeline used to render a 2D tile with the contents of the shadow map
 * (debugging only)
 */
static inline VkPipeline
create_ui_tile_pipeline(VkdfContext *ctx, SceneResources *res)
{
   // Vertex input
   VkVertexInputBindingDescription vi_binding[1];
   VkVertexInputAttributeDescription vi_attribs[2];

   // Vertex attribute binding 0: position, uv
   // Notice that mesh's positions are always a vec3
   vi_binding[0].binding = 0;
   vi_binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding[0].stride = sizeof(glm::vec3) + sizeof(glm::vec2);

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

   return vkdf_create_gfx_pipeline(ctx,
                                   NULL,
                                   1,
                                   vi_binding,
                                   2,
                                   vi_attribs,
                                   false,
                                   res->ui_tile_render_pass,
                                   res->ui_tile_pipeline_layout,
                                   VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                                   VK_CULL_MODE_BACK_BIT,
                                   res->ui_tile_vs_module,
                                   res->ui_tile_fs_module);
}

static void
init_light_sources(VkdfContext *ctx, SceneResources *res)
{
   res->light.origin = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
   res->light.diffuse = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
   res->light.ambient = glm::vec4(0.02f, 0.02f, 0.02f, 1.0f);
   res->light.specular = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
   res->light.attenuation = glm::vec4(0.1f, 0.05f, 0.01f, 0.0f);

   res->light.direction = -res->light.origin;
   vkdf_light_set_cutoff_angle(&res->light, DEG_TO_RAD(45.0f / 2.0f));
}

static VkdfCamera *
init_camera(VkdfContext *ctx)
{
   float cam_z = -ROOM_DEPTH / 2.0 * TILE_DEPTH - 10.0f;
   VkdfCamera *camera = vkdf_camera_new(0.0f, 10.0f, cam_z,   // Position
                                        0.0f, 0.0f, 1.0f);    // View dir
   vkdf_camera_look_at(camera, 0.0f, 0.0f, 0.0f);
   return camera;
}

static VkFramebuffer *
create_framebuffers(VkdfContext *ctx, SceneResources *res)
{
   VkdfImage extra_attachments[1] = {
      res->depth_image,
   };

   return vkdf_create_framebuffers_for_swap_chain(ctx, res->render_pass,
                                                  1, extra_attachments);
}

static VkFramebuffer
create_shadow_framebuffer(VkdfContext *ctx, SceneResources *res)
{
   VkFramebufferCreateInfo fb_info;
   fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fb_info.pNext = NULL;
   fb_info.renderPass = res->shadow_render_pass;
   fb_info.attachmentCount = 1;
   fb_info.pAttachments = &res->shadow_map.view;
   fb_info.width = SHADOW_MAP_WIDTH;
   fb_info.height = SHADOW_MAP_HEIGHT;
   fb_info.layers = 1;
   fb_info.flags = 0;

   VkFramebuffer fb;
   VK_CHECK(vkCreateFramebuffer(ctx->device, &fb_info, NULL, &fb));

   return fb;
}

static void
create_scene_semaphores(VkdfContext *ctx, SceneResources *res)
{
   res->scene_draw_sem = g_new0(VkSemaphore, ctx->swap_chain_length);
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++)
      res->scene_draw_sem[i] = vkdf_create_semaphore(ctx);
}

static void
setup_descriptor_sets(VkdfContext *ctx, SceneResources *res)
{
   // Descriptor sets for scene MVP UBOs. The layout contains 2 bindings:
   // - 0: Static View/Projection matrix.
   // - 1: Array of Model matrices
   //
   // Binding 0 is static, but for binding 1 we need two different UBOs
   // (cubes, tiles) so we create 2 descriptor set instances with this
   // layout. We could have also merged both model data into a single ubo
   // and use the dynamic uniform buffer descriptor type to bind a different
   // offset for cubes and tiles at draw time.
   //
   // We use these when rendering the scene.

   // Set layout
   res->MVP_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT, false);

   // Cubes descriptor set
   res->MVP_cubes_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->MVP_set_layout);

   VkDeviceSize VP_offset = 0;
   VkDeviceSize VP_size = 2 * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->MVP_cubes_descriptor_set,
                                     res->VP_ubo.buf,
                                     0, 1, &VP_offset, &VP_size, false);

   VkDeviceSize M_offset = 0;
   VkDeviceSize M_size = NUM_CUBES * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->MVP_cubes_descriptor_set,
                                     res->M_cubes_ubo.buf,
                                     1, 1, &M_offset, &M_size, false);

   // Tiles descriptor set
   res->MVP_tiles_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->MVP_set_layout);

   VP_offset = 0;
   VP_size = 2 * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->MVP_tiles_descriptor_set,
                                     res->VP_ubo.buf,
                                     0, 1, &VP_offset, &VP_size, false);
   M_offset = 0;
   M_size = NUM_TILES * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->MVP_tiles_descriptor_set,
                                     res->M_tiles_ubo.buf,
                                     1, 1, &M_offset, &M_size, false);

   // Descriptor sets for materials. We have two descriptors, one with
   // the tile materials and another with the cube materials.
   res->Materials_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 1,
                                            VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);
   res->tile_materials_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->Materials_set_layout);

   VkDeviceSize Mat_offset = 0;
   VkDeviceSize Mat_size = 2 * sizeof(VkdfMaterial);
   vkdf_descriptor_set_buffer_update(ctx, res->tile_materials_descriptor_set,
                                     res->tile_materials_ubo.buf,
                                     0, 1, &Mat_offset, &Mat_size, false);

   res->cube_materials_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->Materials_set_layout);

   Mat_offset = 0;
   Mat_size = NUM_CUBES * sizeof(VkdfMaterial);
   vkdf_descriptor_set_buffer_update(ctx, res->cube_materials_descriptor_set,
                                     res->cube_materials_ubo.buf,
                                     0, 1, &Mat_offset, &Mat_size, false);

   // Descriptor set for light data. We have 2 separate bindings.
   // The first binding contains the light description, the
   // second contains the View/Projection matrix of the light which we
   // need for rendering shadows in the scene.

   res->Light_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                            VK_SHADER_STAGE_FRAGMENT_BIT,
                                            false);

   res->Light_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->Light_set_layout);

   // Light description
   VkDeviceSize Light_offset = 0;
   VkDeviceSize Light_size = sizeof(VkdfLight);
   vkdf_descriptor_set_buffer_update(ctx, res->Light_descriptor_set,
                                     res->Light_ubo.buf,
                                     0, 1, &Light_offset, &Light_size, false);
   // Light View/Projection
   VkDeviceSize Light_VP_offset = 0;
   VkDeviceSize Light_VP_size = sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->Light_descriptor_set,
                                     res->Light_VP_ubo.buf,
                                     1, 1, &Light_VP_offset, &Light_VP_size,
                                     false);

   // Descriptor sets for shadow map rendering. For this we need a layout set
   // with 2 bindings
   // 0: View/Projection matrix of the light
   // 1: Array of model matrices of the objects rendered to the shadow map
   //    (we only need to render the cubes)
   res->shadow_map_mvp_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT, false);

   res->shadow_map_mvp_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->shadow_map_mvp_set_layout);

   VP_offset = 0;
   VP_size = sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->shadow_map_mvp_descriptor_set,
                                     res->Light_VP_ubo.buf,
                                     0, 1, &VP_offset, &VP_size, false);

   M_offset = 0;
   M_size = NUM_CUBES * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->shadow_map_mvp_descriptor_set,
                                     res->M_cubes_ubo.buf,
                                     1, 1, &M_offset, &M_size, false);

   // Descriptor set for shadow map sampling. A single binding with the
   // sampler object.
   //
   // We use this when sampling from the shadow map (during scene rendering
   // and the UI tile display of the shadow map)
   res->shadow_sampler_set_layout =
      vkdf_create_sampler_descriptor_set_layout(ctx,
                                                0, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT);

   res->shadow_sampler_descriptor_set =
      create_descriptor_set(ctx,
                            res->sampler_pool,
                            res->shadow_sampler_set_layout);
   vkdf_descriptor_set_sampler_update(ctx,
                                      res->shadow_sampler_descriptor_set,
                                      res->shadow_map_sampler,
                                      res->shadow_map.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      0, 1);

   // Descriptor sets for UI tiles. We need the combined MVP matrix used
   // to render the UI tile. We use this when rendering the UI tiles (debugging
   // only)
   res->ui_tile_mvp_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT, false);

   res->ui_tile_mvp_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->ui_tile_mvp_set_layout);

   VkDeviceSize ui_tile_mvp_offset = 0;
   VkDeviceSize ui_tile_mvp_size = sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->ui_tile_mvp_descriptor_set,
                                     res->ui_tile_mvp_ubo.buf,
                                     0, 1,
                                     &ui_tile_mvp_offset, &ui_tile_mvp_size,
                                     false);
}

static void
fill_model_matrices_ubos(VkdfContext *ctx, SceneResources *res)
{
   // Fill cubes
   glm::mat4 ModelCubes[NUM_CUBES];
   for (uint32_t i = 0; i < NUM_CUBES; i++)
      ModelCubes[i] = vkdf_object_get_model_matrix(res->cubes[i]);

   vkdf_buffer_map_and_fill(ctx,
                            res->M_cubes_ubo,
                            0,
                            NUM_CUBES * sizeof(glm::mat4),
                            &ModelCubes[0][0][0]);

   // Fill tiles
   glm::mat4 ModelTiles[NUM_TILES];
   for (uint32_t i = 0; i < NUM_TILES; i++)
      ModelTiles[i] = vkdf_object_get_model_matrix(res->tiles[i]);

   vkdf_buffer_map_and_fill(ctx,
                            res->M_tiles_ubo,
                            0,
                            NUM_TILES * sizeof(glm::mat4),
                            &ModelTiles[0][0][0]);
}

static void
fill_material_ubos(VkdfContext *ctx, SceneResources *res)
{
   vkdf_buffer_map_and_fill(ctx,
                            res->tile_materials_ubo,
                            0,
                            res->tile_model->materials.size() * sizeof(VkdfMaterial),
                            &res->tile_model->materials[0]);

   vkdf_buffer_map_and_fill(ctx,
                            res->cube_materials_ubo,
                            0,
                            res->cube_model->materials.size() * sizeof(VkdfMaterial),
                            &res->cube_model->materials[0]);
}

static void
init_resources(VkdfContext *ctx, SceneResources *res)
{
   memset(res, 0, sizeof(SceneResources));

   // Create camera
   res->camera = init_camera(ctx);

   // Compute View, Projection and Clip matrices
   init_matrices(res);

   // Load meshes
   init_meshes(ctx, res);

   // Create the object and its mesh
   init_objects(ctx, res);

   // Fill vertex buffers with material index data for scene cubes and tiles
   create_and_fill_material_buffers(ctx, res);

   // Setup lights
   init_light_sources(ctx, res);

   // Setup UI tile vertex buffer
   init_ui_tile_mesh(ctx, res);

   // Create UBO for scene View and Projection matrices
   res->VP_ubo = create_ubo(ctx, 2 * sizeof(glm::mat4),
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   vkdf_buffer_map_and_fill(ctx, res->VP_ubo,
                            0, sizeof(glm::mat4),
                            &res->view[0][0]);

   vkdf_buffer_map_and_fill(ctx, res->VP_ubo,
                            sizeof(glm::mat4), sizeof(glm::mat4),
                            &res->projection[0][0]);

   // Create UBOs for Model matrices (cubes and floor tiles)
   res->M_cubes_ubo = create_ubo(ctx, NUM_CUBES * sizeof(glm::mat4),
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   res->M_tiles_ubo = create_ubo(ctx, NUM_TILES * sizeof(glm::mat4),
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   fill_model_matrices_ubos(ctx, res);

   // Create UBOs for materials
   res->tile_materials_ubo = create_ubo(ctx, 2 * sizeof(VkdfMaterial),
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   res->cube_materials_ubo = create_ubo(ctx, NUM_CUBES * sizeof(VkdfMaterial),
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   fill_material_ubos(ctx, res);

   // Create UBO for light description
   res->Light_ubo = create_ubo(ctx, sizeof(VkdfLight),
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   vkdf_buffer_map_and_fill(ctx, res->Light_ubo,
                            0, sizeof(VkdfLight), &res->light);

   // Create UBO for light View/Projection matrix (we may update this every
   // frame so we fill the buffer at scene update time)
   res->Light_VP_ubo = create_ubo(ctx, sizeof(glm::mat4),
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   // Create UBO for UI tile MVP (used for debugging)
   res->ui_tile_mvp_ubo = create_ubo(ctx, sizeof(glm::mat4),
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   vkdf_buffer_map_and_fill(ctx, res->ui_tile_mvp_ubo,
                            0, sizeof(glm::mat4),
                            &res->ui_tile_mvp[0][0]);

   // Create depth images
   res->depth_image =
      create_depth_image(ctx, ctx->width, ctx->height,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

   // Create shadow map
   VkImageUsageFlagBits shadow_map_usage_flags = (VkImageUsageFlagBits)
      (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
       VK_IMAGE_USAGE_SAMPLED_BIT);

   res->shadow_map =
      create_depth_image(ctx, SHADOW_MAP_WIDTH, SHADOW_MAP_HEIGHT,
                         shadow_map_usage_flags);

   // Create shadow map sampler
   res->shadow_map_sampler =
      vkdf_create_sampler(ctx,
                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                          VK_FILTER_LINEAR,
                          VK_SAMPLER_MIPMAP_MODE_NEAREST);

   // Shaders for scene rendering
   res->vs_module = vkdf_create_shader_module(ctx, "shader.vert.spv");
   res->fs_module = vkdf_create_shader_module(ctx, "shader.frag.spv");

   // Shaders for shadow map rendering
   res->shadow_vs_module = vkdf_create_shader_module(ctx, "shadow.vert.spv");

   // Shaders for UI tile rendering (debugging only)
   res->ui_tile_vs_module = vkdf_create_shader_module(ctx, "ui-tile.vert.spv");
   res->ui_tile_fs_module = vkdf_create_shader_module(ctx, "ui-tile.frag.spv");

   // Render pass for scene, shadowmap and UI tile rendering
   res->render_pass = create_render_pass(ctx, res);
   res->shadow_render_pass = create_shadow_render_pass(ctx, res);
   res->ui_tile_render_pass = create_ui_tile_render_pass(ctx, res);

   // Framebuffer for scene rendering
   res->framebuffers = create_framebuffers(ctx, res);

   // Framebuffer for shadow map rendering
   res->shadow_framebuffer = create_shadow_framebuffer(ctx, res);

   // Descriptor pools
   res->ubo_pool =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16);

   res->sampler_pool =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  16);

   // Descriptor sets
   setup_descriptor_sets(ctx, res);

   // Pipelines for scene, shadow map and UI tile
   res->pipeline_layout = create_pipeline_layout(ctx, res);
   res->pipeline = create_pipeline(ctx, res, true);

   res->shadow_pipeline_layout = create_shadow_pipeline_layout(ctx, res);
   res->shadow_pipeline = create_shadow_pipeline(ctx, res, true);

   res->ui_tile_pipeline_layout = create_ui_tile_pipeline_layout(ctx, res);
   res->ui_tile_pipeline = create_ui_tile_pipeline(ctx, res);

   // Command pool
   res->cmd_pool = vkdf_create_gfx_command_pool(ctx, 0);

   // Command buffers for scene, shadowmap and UI tile
   create_command_buffers(ctx, res);
   create_shadow_command_buffers(ctx, res);
   create_ui_tile_command_buffers(ctx, res);

   // Semaphores
   create_scene_semaphores(ctx, res);
   res->shadow_draw_sem = vkdf_create_semaphore(ctx);
}

static void
update_camera(GLFWwindow *window, VkdfCamera *cam)
{
   const float mov_speed = 0.15f;
   const float rot_speed = 1.0f;

   float base_speed = 1.0f;

   /* Rotation */
   if (glfwGetKey(window, GLFW_KEY_LEFT) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, 0.0f, base_speed * rot_speed, 0.0f);
   else if (glfwGetKey(window, GLFW_KEY_RIGHT) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, 0.0f, -base_speed * rot_speed, 0.0f);

   if (glfwGetKey(window, GLFW_KEY_PAGE_UP) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, base_speed * rot_speed, 0.0f, 0.0f);
   else if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) != GLFW_RELEASE)
      vkdf_camera_rotate(cam, -base_speed * rot_speed, 0.0f, 0.0f);

   /* Stepping */
   float step_speed = base_speed;
   if (glfwGetKey(window, GLFW_KEY_UP) != GLFW_RELEASE)
      step_speed *= mov_speed;
   else if (glfwGetKey(window, GLFW_KEY_DOWN) != GLFW_RELEASE)
      step_speed *= -mov_speed;
   else
      return; /* Not stepping */

   vkdf_camera_step(cam, step_speed, 1, 1, 1);
}

static void
update_lights(SceneResources *res)
{
   static float rotY = 0.0f;

   glm::mat4 model(1.0f);
   model = glm::rotate(model, rotY, glm::vec3(0, 1, 0));
   res->light.origin = model * glm::vec4(-15.0f, 2.0f, -15.0f, 1.0f);
   res->light.direction = -res->light.origin;

   rotY += 0.01f;
}

static void
scene_update(VkdfContext *ctx, void *data)
{
   static bool initialized = false;

   SceneResources *res = (SceneResources *) data;

   // Animate lights
   if (!initialized || enable_dynamic_lights) {
      update_lights(res);

      // Light description
      vkdf_buffer_map_and_fill(ctx, res->Light_ubo,
                               0, sizeof(VkdfLight), &res->light);

      // Light View/Projection
      res->light_view =
         vkdf_compute_view_matrix(glm::vec3(res->light.origin),
                                  glm::vec3(0.0f, 0.0f, 0.0f));
      glm::mat4 vp = res->light_projection * res->light_view;

      vkdf_buffer_map_and_fill(ctx, res->Light_VP_ubo,
                               0, sizeof(glm::mat4), &vp[0][0]);
   }

   // Animate camera
   {
      update_camera(ctx->window, res->camera);
      res->view = vkdf_camera_get_view_matrix(res->camera);
      vkdf_buffer_map_and_fill(ctx, res->VP_ubo,
                               0, sizeof(glm::mat4), &res->view[0][0]);
   }

   initialized = true;
}

static void
scene_render(VkdfContext *ctx, void *data)
{
   SceneResources *res = (SceneResources *) data;

   // Render shadow map
   VkPipelineStageFlags shadow_map_wait_stages = 0;
   vkdf_command_buffer_execute(ctx,
                               res->shadow_cmd_buf,
                               &shadow_map_wait_stages,
                               0, NULL,
                               1, &res->shadow_draw_sem);

   // Render scene
   VkSemaphore scene_render_wait_sems[2] = {
      ctx->acquired_sem[ctx->swap_chain_index],
      res->shadow_draw_sem
   };

   VkPipelineStageFlags scene_wait_stages[2] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
   };

   VkSemaphore *scene_render_complete_sem =
      SHOW_SHADOW_MAP_TILE ? &res->scene_draw_sem[ctx->swap_chain_index] :
                             &ctx->draw_sem[ctx->swap_chain_index];
   vkdf_command_buffer_execute(ctx,
                               res->cmd_bufs[ctx->swap_chain_index],
                               scene_wait_stages,
                               2, scene_render_wait_sems,
                               1, scene_render_complete_sem);

   // Render UI tile
   if (SHOW_SHADOW_MAP_TILE) {
      VkPipelineStageFlags ui_tile_wait_stages =
         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

      vkdf_command_buffer_execute(ctx,
                                  res->ui_tile_cmd_bufs[ctx->swap_chain_index],
                                  &ui_tile_wait_stages,
                                  1, &res->scene_draw_sem[ctx->swap_chain_index],
                                  1, &ctx->draw_sem[ctx->swap_chain_index]);
   }
}

static void
destroy_pipeline_resources(VkdfContext *ctx, SceneResources *res,
                           bool full_destroy)
{
   vkDestroyPipeline(ctx->device, res->pipeline, NULL);
   vkDestroyPipeline(ctx->device, res->shadow_pipeline, NULL);
   vkDestroyPipeline(ctx->device, res->ui_tile_pipeline, NULL);
   if (full_destroy) {
      vkDestroyPipelineCache(ctx->device, res->pipeline_cache, NULL);
      vkDestroyPipelineLayout(ctx->device, res->pipeline_layout, NULL);
      vkDestroyPipelineCache(ctx->device, res->shadow_pipeline_cache, NULL);
      vkDestroyPipelineLayout(ctx->device, res->shadow_pipeline_layout, NULL);
      vkDestroyPipelineLayout(ctx->device, res->ui_tile_pipeline_layout, NULL);
   }
}

static void
destroy_framebuffer_resources(VkdfContext *ctx, SceneResources *res)
{
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++)
      vkDestroyFramebuffer(ctx->device, res->framebuffers[i], NULL);
   g_free(res->framebuffers);
}

static void
destroy_shader_resources(VkdfContext *ctx, SceneResources *res)
{
  vkDestroyShaderModule(ctx->device, res->vs_module, NULL);
  vkDestroyShaderModule(ctx->device, res->fs_module, NULL);
  vkDestroyShaderModule(ctx->device, res->shadow_vs_module, NULL);
  vkDestroyShaderModule(ctx->device, res->ui_tile_vs_module, NULL);
  vkDestroyShaderModule(ctx->device, res->ui_tile_fs_module, NULL);
}

static void
destroy_command_buffer_resources(VkdfContext *ctx, SceneResources *res)
{
   vkFreeCommandBuffers(ctx->device,
                        res->cmd_pool,
                        ctx->swap_chain_length,
                        res->cmd_bufs);

   vkFreeCommandBuffers(ctx->device,
                        res->cmd_pool,
                        ctx->swap_chain_length,
                        res->ui_tile_cmd_bufs);
}

static void
destroy_descriptor_resources(VkdfContext *ctx, SceneResources *res)
{
   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->MVP_cubes_descriptor_set);
   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->MVP_tiles_descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->MVP_set_layout, NULL);

   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->tile_materials_descriptor_set);
   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->cube_materials_descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->Materials_set_layout, NULL);

   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->Light_descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->Light_set_layout, NULL);

   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->shadow_map_mvp_descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->shadow_map_mvp_set_layout, NULL);

   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->ui_tile_mvp_descriptor_set);
   vkFreeDescriptorSets(ctx->device,
                        res->sampler_pool, 1, &res->shadow_sampler_descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->ui_tile_mvp_set_layout, NULL);
   vkDestroyDescriptorSetLayout(ctx->device, res->shadow_sampler_set_layout, NULL);

   vkDestroyDescriptorPool(ctx->device, res->ubo_pool, NULL);
   vkDestroyDescriptorPool(ctx->device, res->sampler_pool, NULL);
}

static void
destroy_ubo_resources(VkdfContext *ctx, SceneResources *res)
{
   vkDestroyBuffer(ctx->device, res->VP_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->VP_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->M_cubes_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->M_cubes_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->M_tiles_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->M_tiles_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->tile_materials_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->tile_materials_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->cube_materials_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->cube_materials_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->Light_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->Light_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->Light_VP_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->Light_VP_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->ui_tile_mvp_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->ui_tile_mvp_ubo.mem, NULL);
}

static void
destroy_scene_semaphores(VkdfContext *ctx, SceneResources *res)
{
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++)
      vkDestroySemaphore(ctx->device, res->scene_draw_sem[i], NULL);
   g_free(res->scene_draw_sem);
}

void
cleanup_resources(VkdfContext *ctx, SceneResources *res)
{
   vkdf_camera_free(res->camera);
   for (uint32_t i = 0; i < ROOM_WIDTH * ROOM_DEPTH; i++)
      vkdf_object_free(res->tiles[i]);
   for (uint32_t i = 0; i < NUM_CUBES; i++)
      vkdf_object_free(res->cubes[i]);
   vkdf_mesh_free(ctx, res->cube_mesh);
   vkdf_mesh_free(ctx, res->tile_mesh);
   vkdf_mesh_free(ctx, res->ui_tile_mesh);
   vkdf_destroy_buffer(ctx, &res->cube_material_buf);
   vkdf_destroy_buffer(ctx, &res->tile_material_buf);
   destroy_pipeline_resources(ctx, res, true);
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   vkDestroyRenderPass(ctx->device, res->shadow_render_pass, NULL);
   vkDestroyRenderPass(ctx->device, res->ui_tile_render_pass, NULL);
   destroy_descriptor_resources(ctx, res);
   destroy_ubo_resources(ctx, res);
   destroy_framebuffer_resources(ctx, res);
   vkdf_destroy_image(ctx, &res->depth_image);
   vkdf_destroy_image(ctx, &res->shadow_map);
   vkDestroySampler(ctx->device, res->shadow_map_sampler, NULL);
   vkDestroyFramebuffer(ctx->device, res->shadow_framebuffer, NULL);
   destroy_shader_resources(ctx, res);
   destroy_command_buffer_resources(ctx, res);
   vkDestroyCommandPool(ctx->device, res->cmd_pool, NULL);
   destroy_scene_semaphores(ctx, res);
   vkDestroySemaphore(ctx->device, res->shadow_draw_sem, NULL);
}

static void
before_rebuild_swap_chain_cb(VkdfContext *ctx, void *user_data)
{
   SceneResources *res = (SceneResources *) user_data;
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   vkDestroyRenderPass(ctx->device, res->ui_tile_render_pass, NULL);
   vkDestroyPipeline(ctx->device, res->pipeline, NULL);
   vkDestroyPipeline(ctx->device, res->ui_tile_pipeline, NULL);
   destroy_framebuffer_resources(ctx, res);
   vkdf_destroy_image(ctx, &res->depth_image);
   destroy_command_buffer_resources(ctx, res);
   destroy_scene_semaphores(ctx, res);
}

static void
after_rebuild_swap_chain_cb(VkdfContext *ctx, void *user_data)
{
   SceneResources *res = (SceneResources *) user_data;
   res->render_pass = create_render_pass(ctx, res);
   res->ui_tile_render_pass = create_ui_tile_render_pass(ctx, res);
   res->depth_image =
      create_depth_image(ctx, ctx->width, ctx->height,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
   res->framebuffers = create_framebuffers(ctx, res);
   res->pipeline = create_pipeline(ctx, res, false);
   res->ui_tile_pipeline = create_ui_tile_pipeline(ctx, res);
   create_command_buffers(ctx, res);
   create_ui_tile_command_buffers(ctx, res);
   create_scene_semaphores(ctx, res);
}

static void
window_resize_cb(GLFWwindow* window, int width, int height)
{
   if (width == 0 || height == 0)
      return;

   VkdfContext *ctx = (VkdfContext *) glfwGetWindowUserPointer(window);
   vkdf_rebuild_swap_chain(ctx);
}

int
main()
{
   VkdfContext ctx;
   SceneResources resources;

   srandom(time(NULL));

   // Disable validation, seems to intefere with depthBias behavior somehow...
   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, FULLSCREEN, true, false);

   vkdf_set_rebuild_swapchain_cbs(&ctx,
                                  before_rebuild_swap_chain_cb,
                                  after_rebuild_swap_chain_cb,
                                  &resources);

   glfwSetWindowSizeCallback(ctx.window, window_resize_cb);
   glfwSetWindowUserPointer(ctx.window, &ctx);

   init_resources(&ctx, &resources);

   vkdf_event_loop_run(&ctx, scene_update, scene_render, &resources);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
