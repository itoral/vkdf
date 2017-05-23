#include "vkdf.hpp"

#include <stdlib.h>
#include <time.h>

// ----------------------------------------------------------------------------
// Renders a simple scene with a number of moving positional lights
// ----------------------------------------------------------------------------

#define WIN_WIDTH     800
#define WIN_HEIGHT    600
#define FULLSCREEN    false

#define SCENE_NEAR    0.1f
#define SCENE_FAR     1000.0f

#define ROOM_WIDTH    20
#define ROOM_DEPTH    20

#define TILE_WIDTH    2.0f
#define TILE_DEPTH    2.0f

// WARNING: this must match the size of the array in the vertex shader
#define NUM_LIGHTS    4

typedef struct {
   VkdfObject *obj;
   glm::vec4 color;
} SceneCube;

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
   VkdfBuffer M_ubo;

   // UBO for lights
   VkdfBuffer Light_ubo;

   // Descriptor sets for UBO bindings
   VkDescriptorSetLayout MVP_set_layout;
   VkDescriptorSet MVP_descriptor_set;
   VkDescriptorSetLayout Light_set_layout;
   VkDescriptorSet Light_descriptor_set;

   // View/Projection matrices
   glm::mat4 view;
   glm::mat4 projection;

   // Objects
   VkdfMesh *cube_mesh;
   SceneCube cubes[ROOM_WIDTH * ROOM_DEPTH];

   // Vertex buffer with colors for each cube
   VkdfBuffer cube_color_buf;

   // Lights
   VkdfLight lights[NUM_LIGHTS];

   // Camera
   VkdfCamera *camera;
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
create_and_fill_cube_colors_buffer(VkdfContext *ctx, SceneResources *res)
{
   glm::vec4 color_data[ROOM_WIDTH * ROOM_DEPTH];

   for (uint32_t x = 0; x < ROOM_WIDTH; x++) {
      for (uint32_t z = 0; z < ROOM_DEPTH; z++) {
         color_data[x * ROOM_DEPTH + z] = res->cubes[x * ROOM_DEPTH + z].color;
      }
   }

   res->cube_color_buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flag
                         sizeof(color_data),                   // size
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,    // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type

   vkdf_buffer_map_and_fill(ctx, res->cube_color_buf, 0, sizeof(color_data),
                            color_data);
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

   // Vertex buffer: position, normal
   const VkdfMesh *mesh = res->cubes[0].  obj->model->meshes[0];

   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->cmd_bufs[index],
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &mesh->vertex_buf.buf,   // Buffers
                          offsets);                // Offsets


   // Vertex buffer: color
   vkCmdBindVertexBuffers(res->cmd_bufs[index],
                          1,                            // Start Binding
                          1,                            // Binding Count
                          &res->cube_color_buf.buf,     // Buffers
                          offsets);                     // Offsets

   // Bind static MVP descriptor set once
   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                        // First decriptor set
                           1,                        // Descriptor set count
                           &res->MVP_descriptor_set, // Descriptor sets
                           0,                        // Dynamic offset count
                           NULL);                    // Dynamic offsets

   // Bind static Light descriptor set once
   vkCmdBindDescriptorSets(res->cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           1,                          // First decriptor set
                           1,                          // Descriptor set count
                           &res->Light_descriptor_set, // Descriptor sets
                           0,                          // Dynamic offset count
                           NULL);                      // Dynamic offsets

   // Draw
   vkCmdDraw(res->cmd_bufs[index],
             mesh->vertices.size(),                // vertex count
             ROOM_WIDTH * ROOM_DEPTH,              // instance count
             0,                                    // first vertex
             0);                                   // first instance

   vkCmdEndRenderPass(res->cmd_bufs[index]);
}

static VkPipelineLayout
create_pipeline_layout(VkdfContext *ctx, SceneResources *res)
{
   VkDescriptorSetLayout layouts[2] = {
      res->MVP_set_layout,
      res->Light_set_layout
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
   VkResult result = vkCreatePipelineLayout(ctx->device,
                                            &pipeline_layout_info,
                                            NULL,
                                            &pipeline_layout);
   if (result != VK_SUCCESS)
      vkdf_fatal("Failed to create pipeline layout");

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
   VkResult res = vkAllocateDescriptorSets(ctx->device, alloc_info, &set);

   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to allocate descriptor set");

   return set;
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

static VkdfImage
create_depth_image(VkdfContext *ctx)
{
   return vkdf_create_image(ctx,
                            ctx->width,
                            ctx->height,
                            1,
                            VK_IMAGE_TYPE_2D,
                            VK_FORMAT_D32_SFLOAT,
                            0,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT,
                            VK_IMAGE_VIEW_TYPE_2D);
}

static void
init_meshes(VkdfContext *ctx, SceneResources *res)
{
   res->cube_mesh = vkdf_cube_mesh_new(ctx);
   vkdf_mesh_fill_vertex_buffer(ctx, res->cube_mesh);
}

static void
init_objects(VkdfContext *ctx, SceneResources *res)
{
   // Create all the cubes
   for (uint32_t x = 0; x < ROOM_WIDTH; x++) {
      for (uint32_t z = 0; z < ROOM_DEPTH; z++) {
         uint32_t idx = x * ROOM_DEPTH + z;

         float tx = (-ROOM_WIDTH * TILE_WIDTH + TILE_WIDTH) / 2.0f +
                    TILE_WIDTH * x;
         float tz = (-ROOM_DEPTH * TILE_DEPTH + TILE_DEPTH) / 2.0f +
                    TILE_DEPTH * z;
         glm::vec3 pos = glm::vec3(tx, 0.0f, tz);

         res->cubes[idx].obj = vkdf_object_new_from_mesh(pos, res->cube_mesh);
         vkdf_object_set_scale(res->cubes[idx].obj,
                              glm::vec3(TILE_WIDTH / 2.0f,
                                        0.5f,
                                        TILE_DEPTH / 2.0f));
         res->cubes[idx].color.r = (random() % 101) / 100.0f;
         res->cubes[idx].color.g = (random() % 101) / 100.0f;
         res->cubes[idx].color.b = (random() % 101) / 100.0f;
         res->cubes[idx].color.a = 1.0f;

         // Scale some of them up
         if (random() % 5 == 1) {
            uint32_t idx = x * ROOM_DEPTH + z;
            vkdf_object_set_scale(res->cubes[idx].obj,
                                  glm::vec3(res->cubes[idx].obj->scale.x,
                                            4.0f,
                                            res->cubes[idx].obj->scale.z));
         }
      }
   }
}

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

   // Vertex attribute binding 1: color
   vi_binding[1].binding = 1;
   vi_binding[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
   vi_binding[1].stride = sizeof(glm::vec4);

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

   // binding 1, location 2: color
   vi_attribs[2].binding = 1;
   vi_attribs[2].location = 2;
   vi_attribs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
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
                                   VK_CULL_MODE_BACK_BIT,
                                   res->vs_module,
                                   res->fs_module);
}

static void
init_light_sources(VkdfContext *ctx, SceneResources *res)
{
   assert(NUM_LIGHTS >= 4);

   res->lights[0].origin = glm::vec4(0.0f, 2.0f, 0.0f, 1.0f);
   res->lights[0].diffuse = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
   res->lights[0].ambient = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
   res->lights[0].specular = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
   res->lights[0].attenuation = glm::vec4(5.0f, 3.0f, 2.0f, 0.0f);

   res->lights[1].origin = glm::vec4(8.0f, 2.0f, -10.0f, 1.0f);
   res->lights[1].diffuse = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
   res->lights[1].ambient = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
   res->lights[1].specular = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
   res->lights[1].attenuation = glm::vec4(5.0f, 3.0f, 2.0f, 0.0f);

   res->lights[2].origin = glm::vec4(-5.0f, 2.0f, 7.0f, 1.0f);
   res->lights[2].diffuse = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
   res->lights[2].ambient = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
   res->lights[2].specular = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
   res->lights[2].attenuation = glm::vec4(5.0f, 3.0f, 2.0f, 0.0f);

   res->lights[3].origin = glm::vec4(-12.0f, 2.0f, -3.0f, 1.0f);
   res->lights[3].diffuse = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
   res->lights[3].ambient = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
   res->lights[3].specular = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
   res->lights[3].attenuation = glm::vec4(5.0f, 3.0f, 2.0f, 0.0f);
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

   // Fill vertex buffer with cube colors
   create_and_fill_cube_colors_buffer(ctx, res);

   // Setup lights
   init_light_sources(ctx, res);

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
   res->M_ubo = create_ubo(ctx, ROOM_WIDTH * ROOM_DEPTH * sizeof(glm::mat4),
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   // Create UBO for lights
   res->Light_ubo = create_ubo(ctx, NUM_LIGHTS * sizeof(VkdfLight),
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   vkdf_buffer_map_and_fill(ctx, res->Light_ubo,
                            0, NUM_LIGHTS * sizeof(VkdfLight),
                            res->lights);

   // Create depth image
   res->depth_image = create_depth_image(ctx);

   // Shaders
   res->vs_module = vkdf_create_shader_module(ctx, "shader.vert.spv");
   res->fs_module = vkdf_create_shader_module(ctx, "shader.frag.spv");

   // Render pass
   res->render_pass = create_render_pass(ctx, res);

   // Framebuffers
   res->framebuffers =
      vkdf_create_framebuffers_for_swap_chain(ctx, res->render_pass,
                                              &res->depth_image);

   // Descriptor pool
   res->ubo_pool =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3);

   // Descriptor set for UBO
   res->MVP_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 2,
                                            VK_SHADER_STAGE_VERTEX_BIT, false);

   res->MVP_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->MVP_set_layout);

   res->Light_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 1,
                                            VK_SHADER_STAGE_FRAGMENT_BIT, false);

   res->Light_descriptor_set =
      create_descriptor_set(ctx, res->ubo_pool, res->Light_set_layout);

   // Map View and Projection UBOs to set 0, binding 0
   VkDeviceSize VP_offset = 0;
   VkDeviceSize VP_size = 2 * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->MVP_descriptor_set,
                                     res->VP_ubo.buf,
                                     0, 1, &VP_offset, &VP_size, false);

   // Map Model UBO to set 0, binding 1
   VkDeviceSize M_offset = 0;
   VkDeviceSize M_size = ROOM_WIDTH * ROOM_DEPTH * sizeof(glm::mat4);
   vkdf_descriptor_set_buffer_update(ctx, res->MVP_descriptor_set,
                                     res->M_ubo.buf,
                                     1, 1, &M_offset, &M_size, false);

   // Map Lights UBO to set 1, binding 0
   VkDeviceSize Light_offset = 0;
   VkDeviceSize Light_size = NUM_LIGHTS * sizeof(VkdfLight);
   vkdf_descriptor_set_buffer_update(ctx, res->Light_descriptor_set,
                                     res->Light_ubo.buf,
                                     0, 1, &Light_offset, &Light_size, false);

   // Pipeline
   res->pipeline_layout = create_pipeline_layout(ctx, res);

   res->pipeline = create_pipeline(ctx, res, true);

   // Command pool
   res->cmd_pool = vkdf_create_gfx_command_pool(ctx, 0);

   // Command buffers
   create_command_buffers(ctx, res);
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
scene_update(VkdfContext *ctx, void *data)
{
   static bool initialized = false;

   SceneResources *res = (SceneResources *) data;

   // Only need to copy Model matrices once
   if (!initialized) {
      uint8_t *map;
      VkDeviceSize buf_size = VK_WHOLE_SIZE;
      vkMapMemory(ctx->device, res->M_ubo.mem, 0, buf_size, 0, (void**) &map);

      for (uint32_t i = 0; i < ROOM_WIDTH * ROOM_DEPTH; i++) {
         VkdfObject *obj = res->cubes[i].obj;
         glm::mat4 Model = vkdf_object_get_model_matrix(obj);
         memcpy(map, &Model[0][0], sizeof(glm::mat4));
         map += sizeof(glm::mat4);
      }

      VkMappedMemoryRange range;
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.pNext = NULL;
      range.memory = res->M_ubo.mem;
      range.offset = 0;
      range.size = buf_size;
      vkFlushMappedMemoryRanges(ctx->device, 1, &range);

      vkUnmapMemory(ctx->device, res->M_ubo.mem);
   }

   // Move ligths around every frame
   {
      static float light_x_dir[NUM_LIGHTS] = { 1.0f,  1.0f, -1.0f, -1.0 };
      static float light_z_dir[NUM_LIGHTS] = { 1.0f, -1.0f, 1.0f, -1.0 };
      assert(NUM_LIGHTS == 4);

      uint8_t *map;
      VkDeviceSize buf_size = VK_WHOLE_SIZE;
      vkMapMemory(ctx->device, res->Light_ubo.mem, 0, buf_size, 0, (void**) &map);

      for (uint32_t i = 0; i < NUM_LIGHTS; i++) {
         res->lights[i].origin.x += light_x_dir[i] * 0.2f;
         res->lights[i].origin.z += light_z_dir[i] * 0.1f;
      }
      memcpy(map, res->lights, NUM_LIGHTS * sizeof(VkdfLight));

      VkMappedMemoryRange range;
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.pNext = NULL;
      range.memory = res->Light_ubo.mem;
      range.offset = 0;
      range.size = buf_size;
      vkFlushMappedMemoryRanges(ctx->device, 1, &range);

      vkUnmapMemory(ctx->device, res->Light_ubo.mem);

      for (uint32_t i = 0; i < NUM_LIGHTS; i++) {
         if (fabs(res->lights[i].origin.z) > (ROOM_DEPTH / 2.0f) * TILE_DEPTH)
            light_z_dir[i] *= -1;
         if (fabs(res->lights[i].origin.x) > (ROOM_WIDTH / 2.0f) * TILE_WIDTH)
            light_x_dir[i] *= -1;
      }
   }

   // Update camera view
   {
      update_camera(ctx->window, res->camera);
      res->view = vkdf_camera_get_view_matrix(res->camera);

      uint8_t *map;
      VkDeviceSize buf_size = sizeof(glm::mat4);
      vkMapMemory(ctx->device, res->VP_ubo.mem, 0, buf_size, 0, (void**) &map);

      memcpy(map, &res->view[0][0], buf_size);

      VkMappedMemoryRange range;
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.pNext = NULL;
      range.memory = res->VP_ubo.mem;
      range.offset = 0;
      range.size = buf_size;
      vkFlushMappedMemoryRanges(ctx->device, 1, &range);

      vkUnmapMemory(ctx->device, res->VP_ubo.mem);
   }

   initialized = true;
}

static void
scene_render(VkdfContext *ctx, void *data)
{
   SceneResources *res = (SceneResources *) data;

   VkPipelineStageFlags pipeline_stages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

   vkdf_command_buffer_execute(ctx,
                               res->cmd_bufs[ctx->swap_chain_index],
                               &pipeline_stages,
                               1, &ctx->acquired_sem[ctx->swap_chain_index],
                               1, &ctx->draw_sem[ctx->swap_chain_index]);
}

static void
destroy_pipeline_resources(VkdfContext *ctx, SceneResources *res,
                           bool full_destroy)
{
   vkDestroyPipeline(ctx->device, res->pipeline, NULL);
   if (full_destroy) {
      vkDestroyPipelineCache(ctx->device, res->pipeline_cache, NULL);
      vkDestroyPipelineLayout(ctx->device, res->pipeline_layout, NULL);
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
}

static void
destroy_command_buffer_resources(VkdfContext *ctx, SceneResources *res)
{
   vkFreeCommandBuffers(ctx->device,
                        res->cmd_pool,
                        ctx->swap_chain_length,
                        res->cmd_bufs);
}

static void
destroy_descriptor_resources(VkdfContext *ctx, SceneResources *res)
{
   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->MVP_descriptor_set);
   vkFreeDescriptorSets(ctx->device,
                        res->ubo_pool, 1, &res->Light_descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->MVP_set_layout, NULL);
   vkDestroyDescriptorSetLayout(ctx->device, res->Light_set_layout, NULL);
   vkDestroyDescriptorPool(ctx->device, res->ubo_pool, NULL);
}

static void
destroy_ubo_resources(VkdfContext *ctx, SceneResources *res)
{
   vkDestroyBuffer(ctx->device, res->VP_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->VP_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->M_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->M_ubo.mem, NULL);

   vkDestroyBuffer(ctx->device, res->Light_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->Light_ubo.mem, NULL);
}

void
cleanup_resources(VkdfContext *ctx, SceneResources *res)
{
   vkdf_camera_free(res->camera);
   for (uint32_t i = 0; i < ROOM_WIDTH * ROOM_DEPTH; i++)
      vkdf_object_free(res->cubes[i].obj);
   vkdf_mesh_free(ctx, res->cube_mesh);
   vkdf_destroy_buffer(ctx, &res->cube_color_buf);
   destroy_pipeline_resources(ctx, res, true);
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   destroy_descriptor_resources(ctx, res);
   destroy_ubo_resources(ctx, res);
   destroy_framebuffer_resources(ctx, res);
   vkdf_destroy_image(ctx, &res->depth_image);
   destroy_shader_resources(ctx, res);
   destroy_command_buffer_resources(ctx, res);
   vkDestroyCommandPool(ctx->device, res->cmd_pool, NULL);
}

static void
before_rebuild_swap_chain_cb(VkdfContext *ctx, void *user_data)
{
   SceneResources *res = (SceneResources *) user_data;
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   destroy_pipeline_resources(ctx, res, false);
   destroy_framebuffer_resources(ctx, res);
   vkdf_destroy_image(ctx, &res->depth_image);
   destroy_command_buffer_resources(ctx, res);
}

static void
after_rebuild_swap_chain_cb(VkdfContext *ctx, void *user_data)
{
   SceneResources *res = (SceneResources *) user_data;
   res->render_pass = create_render_pass(ctx, res);
   res->depth_image = create_depth_image(ctx);
   res->framebuffers =
      vkdf_create_framebuffers_for_swap_chain(ctx, res->render_pass,
                                              &res->depth_image);
   res->pipeline = create_pipeline(ctx, res, false);
   create_command_buffers(ctx, res);
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

   vkdf_init(&ctx, WIN_WIDTH, WIN_HEIGHT, FULLSCREEN, true, ENABLE_DEBUG);

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
