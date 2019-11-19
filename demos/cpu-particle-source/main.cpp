#include "vkdf.hpp"

// ----------------------------------------------------------------------------
// Renders a particle source.
// ----------------------------------------------------------------------------

#define MAX_PARTICLES 1000

// Subclass VkdfCpuParticleData to add a material index and a particle
// age indicator in range 0-1.
typedef struct {
   VkdfCpuParticle base;
   uint32_t material_idx;
   float age;
} ParticleData;

// Per-particle shader data.
typedef struct {
   glm::mat4 model;
   uint32_t material_idx;
   float age;
   uint32_t padding[2];
} ParticleShaderData;

// Structure of the UBO contianing shader data for all particles.
typedef struct {
   glm::mat4 ViewProjection;
   ParticleShaderData data[MAX_PARTICLES];
} ParticleCollectionShaderData;

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer *cmd_bufs;
   VkRenderPass render_pass;
   VkFramebuffer *framebuffers;
   VkDescriptorPool descriptor_pool;
   VkdfImage depth_image;
   VkFence frame_fence;

   VkdfCpuParticleSource *ps;
   VkdfCpuParticleSourceRenderer *psr;
   VkShaderModule psr_vs_module;
   VkShaderModule psr_fs_module;
   VkPipeline psr_pipeline;
   VkPipelineLayout psr_pipeline_layout;
   VkDescriptorSet psr_descriptor_set;
   VkDescriptorSetLayout psr_descriptor_set_layout;
   VkdfBuffer psr_ubo;

   glm::mat4 clip;
   glm::mat4 view;
   glm::mat4 projection;
   glm::mat4 mvp;
} DemoResources;

static void
spawn_particles(VkdfCpuParticleSource *ps, uint32_t max_particles, void *data)
{
   /* Max of 6 particles per cycle */
   for (uint32_t i = 0; i < MIN2(max_particles, 6); i++) {
      VkdfCpuParticle *p = vkdf_cpu_particle_source_spawn_particle(ps);
      ParticleData *pd = (ParticleData *) p;
      pd->material_idx = ps->num_particles % 5;
      pd->age = 0.0f;
   }
}

static void
update_particle(VkdfCpuParticleSource *ps, VkdfCpuParticle *p, void *data)
{
     // Update position and speed
     p->pos += p->dir * p->speed;
     p->speed = MAX2(p->speed - ps->friction, 0.0f);

     // Compute particle age in [0, 1]
     ParticleData *pd = (ParticleData *) p;
     pd->age = (ps->particle_life - p->life) / ((float) ps->particle_life);

     // Simulate gravity by increasing vertical speed with age
     p->pos.y -= 0.04f * pd->age;
}

static VkdfBuffer
create_ubo(VkdfContext *ctx, VkDeviceSize size)
{
   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flags
                         size,                                 // size
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,   // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type
   return buf;
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

   vkdf_cpu_particle_source_renderer_render(res->psr, res->cmd_bufs[index]);

   vkCmdEndRenderPass(res->cmd_bufs[index]);
}

static VkPipelineLayout
create_pipeline_layout(VkdfContext *ctx, VkDescriptorSetLayout set_layout)
{
   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 1;
   pipeline_layout_info.pSetLayouts = &set_layout;
   pipeline_layout_info.flags = 0;

   VkPipelineLayout pipeline_layout;
   VkResult res = vkCreatePipelineLayout(ctx->device,
                                         &pipeline_layout_info,
                                         NULL,
                                         &pipeline_layout);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create pipeline layout");

   return pipeline_layout;
}

static void
init_matrices(DemoResources *res)
{
   res->clip = glm::mat4(1.0f,  0.0f, 0.0f, 0.0f,
                         0.0f, -1.0f, 0.0f, 0.0f,
                         0.0f,  0.0f, 0.5f, 0.0f,
                         0.0f,  0.0f, 0.5f, 1.0f);

   res->projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);

   res->view = glm::lookAt(glm::vec3( 0,  0, -5),  // Camera position
                           glm::vec3( 0,  0,  0),  // Looking at origin
                           glm::vec3( 0,  1,  0)); // Head is up
}

static void
init_particle_source(VkdfContext *ctx, DemoResources *res)
{
   VkdfBox source_box;
   source_box.center = glm::vec3(0.0f, -0.5f, 0.0f);
   source_box.w = 0.025f;
   source_box.h = 0.025f;
   source_box.d = 0.025f;

   glm::vec3 dir = glm::vec3(0.0f, 1.0f, 0.0f);
   vkdf_vec3_normalize(&dir);
   float dir_noise = 0.5f;

   float speed = 0.05f;
   float speed_noise = 0.005f;
   float friction = speed * 0.01f; // Speed drops to 0 after 100 cycles

   uint32_t particle_life = 200; // This is in units of cycles
   float particle_life_noise = particle_life * 0.3f;

   uint32_t max_particles = MAX_PARTICLES;

   res->ps =
      vkdf_cpu_particle_source_new(source_box,
                                   dir, dir_noise,
                                   speed, speed_noise, friction,
                                   particle_life, particle_life_noise,
                                   max_particles,
                                   sizeof(ParticleData));

   vkdf_cpu_particle_source_set_callbacks(res->ps,
                                          spawn_particles,
                                          update_particle,
                                          NULL);

   res->psr_descriptor_set_layout =
      vkdf_create_ubo_descriptor_set_layout(ctx, 0, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT, false);

   res->psr_descriptor_set =
      vkdf_descriptor_set_create(ctx, res->descriptor_pool,
                                 res->psr_descriptor_set_layout);

   VkDeviceSize ubo_offset = 0;
   VkDeviceSize ubo_size = sizeof(ParticleCollectionShaderData);
   res->psr_ubo = create_ubo(ctx, ubo_size);
   vkdf_descriptor_set_buffer_update(ctx, res->psr_descriptor_set,
                                     res->psr_ubo.buf, 0, 1,
                                     &ubo_offset, &ubo_size,
                                     false, true);

   res->psr_pipeline_layout =
      create_pipeline_layout(ctx, res->psr_descriptor_set_layout);

   res->psr_vs_module = vkdf_create_shader_module(ctx, "particle.vert.spv");
   res->psr_fs_module = vkdf_create_shader_module(ctx, "particle.frag.spv");

   res->psr_pipeline =
      vkdf_create_gfx_pipeline(ctx,
                               NULL,
                               0, NULL,
                               0, NULL,
                               true, VK_COMPARE_OP_LESS,
                               res->render_pass,
                               res->psr_pipeline_layout,
                               VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
                               VK_CULL_MODE_NONE,
                               1,
                               res->psr_vs_module, res->psr_fs_module);

   res->psr =
      vkdf_cpu_particle_source_renderer_new(res->ps, NULL,
                                            res->psr_pipeline,
                                            res->psr_pipeline_layout,
                                            1, &res->psr_descriptor_set);
}

static void
init_resources(VkdfContext *ctx, DemoResources *res)
{
   memset(res, 0, sizeof(DemoResources));

   // Compute View, Projection and Clip matrices
   init_matrices(res);

   // Depth image
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

   // Render pass
   res->render_pass =
      vkdf_renderpass_simple_new(ctx,
                                 ctx->surface_format.format,
                                 VK_ATTACHMENT_LOAD_OP_CLEAR,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                 res->depth_image.format,
                                 VK_ATTACHMENT_LOAD_OP_CLEAR,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

   // Framebuffers
   res->framebuffers =
      vkdf_create_framebuffers_for_swap_chain(ctx, res->render_pass,
                                              1, &res->depth_image);

   // Descriptor pool
   res->descriptor_pool =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);

   // Particle source
   init_particle_source(ctx, res);

   // Command pool
   res->cmd_pool =
      vkdf_create_gfx_command_pool(ctx,
                                   VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

   // Command buffers
   res->cmd_bufs = g_new(VkCommandBuffer, ctx->swap_chain_length);
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              ctx->swap_chain_length,
                              res->cmd_bufs);

   res->frame_fence = vkdf_create_fence(ctx);
}

static void
scene_update(VkdfContext *ctx, void *data)
{
   DemoResources *res = (DemoResources *) data;

   // Move particle source
   static float ps_dir = 1.0f;
   res->ps->origin.center.x += 0.01f * ps_dir;
   if (fabs(res->ps->origin.center.x) >= 1.0f)
      ps_dir *= -1.0f;

   // Update particles
   vkdf_cpu_particle_source_update(res->ps);

   // Prepare particle UBO for rendering
   unsigned char *map;
   VkDeviceSize ubo_size = sizeof(ParticleCollectionShaderData);
   vkdf_memory_map(ctx, res->psr_ubo.mem, 0, ubo_size, (void **) &map);
   glm::mat4 psr_vp = res->clip * res->projection * res->view;
   memcpy(map, &psr_vp, sizeof(glm::mat4));
   map += sizeof(glm::mat4);
   GList *iter = res->ps->particles;
   while (iter) {
      VkdfCpuParticle *particle = (VkdfCpuParticle *) iter->data;
      ParticleData *particle_data = (ParticleData *) particle;
      ParticleShaderData *shader_data = (ParticleShaderData *) map;
      shader_data->model = glm::mat4(1.0f);
      shader_data->model = glm::translate(shader_data->model, particle->pos);
      shader_data->material_idx = particle_data->material_idx;
      shader_data->age = particle_data->age;
      iter = g_list_next(iter);
      map += sizeof(ParticleShaderData);
   }
   vkdf_memory_unmap(ctx, res->psr_ubo.mem, res->psr_ubo.mem_props, 0, ubo_size);
}

static void
scene_render(VkdfContext *ctx, void *data)
{
   DemoResources *res = (DemoResources *) data;

   // The number of particles being renderer changes dynamically so we need
   // to record the command buffer every frame
   VkPipelineStageFlags pipeline_stages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

   vkdf_command_buffer_begin(res->cmd_bufs[ctx->swap_chain_index],
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
   render_pass_commands(ctx, res, ctx->swap_chain_index);
   vkdf_command_buffer_end(res->cmd_bufs[ctx->swap_chain_index]);

   vkdf_command_buffer_execute_with_fence(
      ctx,
      res->cmd_bufs[ctx->swap_chain_index],
      &pipeline_stages,
      1, &ctx->acquired_sem[ctx->swap_chain_index],
      1, &ctx->draw_sem[ctx->swap_chain_index],
      res->frame_fence);

   // Wait for rendering to complete. Not the most efficient implementation
   // but good enough for the purposes of this demo.
   VkResult status;
   do {
      status = vkWaitForFences(ctx->device,
                               1, &res->frame_fence,
                               true, 1000ull);
   } while (status == VK_NOT_READY || status == VK_TIMEOUT);
   vkResetFences(ctx->device, 1, &res->frame_fence);
}

static void
destroy_framebuffer_resources(VkdfContext *ctx, DemoResources *res)
{
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++)
      vkDestroyFramebuffer(ctx->device, res->framebuffers[i], NULL);
   g_free(res->framebuffers);
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
destroy_particle_source_resources(VkdfContext *ctx, DemoResources *res)
{
   vkdf_cpu_particle_source_free(res->ps);
   vkdf_cpu_particle_source_renderer_free(res->psr);

   vkDestroyPipeline(ctx->device, res->psr_pipeline, NULL);
   vkDestroyPipelineLayout(ctx->device, res->psr_pipeline_layout, NULL);

   vkFreeDescriptorSets(ctx->device, res->descriptor_pool,
                        1, &res->psr_descriptor_set);
   vkDestroyDescriptorSetLayout(ctx->device, res->psr_descriptor_set_layout, NULL);

   vkDestroyBuffer(ctx->device, res->psr_ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->psr_ubo.mem, NULL);

  vkDestroyShaderModule(ctx->device, res->psr_vs_module, NULL);
  vkDestroyShaderModule(ctx->device, res->psr_fs_module, NULL);
}

void
cleanup_resources(VkdfContext *ctx, DemoResources *res)
{
   destroy_particle_source_resources(ctx, res);
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   vkdf_destroy_image(ctx, &res->depth_image);
   destroy_framebuffer_resources(ctx, res);
   destroy_command_buffer_resources(ctx, res);
   vkDestroyDescriptorPool(ctx->device, res->descriptor_pool, NULL);
   vkDestroyFence(ctx->device, res->frame_fence, NULL);
}

int
main()
{
   VkdfContext ctx;
   DemoResources resources;

   vkdf_init(&ctx, 800, 600, false, false, ENABLE_DEBUG);
   init_resources(&ctx, &resources);

   vkdf_event_loop_run(&ctx, scene_update, scene_render, &resources);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
