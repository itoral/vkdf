#include "vkdf.hpp"
// ----------------------------------------------------------------------------
// Basic SSBO example: no framebuffer or attachment. The output is the SSBO
//  content (checking that the content is corrent).
//
// It uses just one vertex shader. It stores on the SSBO the vertex input attribute.
//
// There is a more "pure SSBO" alternative: the SSBO as input and output (and even
// move to use a compute shader). But this demo should be a valuable just SSBO
// demo too.

#define DEFAULT_NUM_VERTICES 3
#define DEFAULT_NUM_COMPONENTS 2

//binding at shader.vert for the ssbo
#define SSBO_BINDING 3
float LSB = 0.001231;

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer render_cmd_buf;
   VkdfBuffer vertex_buf;
   float *vertex_data;
   VkRenderPass render_pass;
   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkShaderModule vs_module;

   VkdfBuffer ssbo;
   VkDescriptorPool descriptor_pool_ssbo;
   VkDescriptorSet descriptor_set_ssbo;
   VkDescriptorSetLayout set_layout_ssbo;

   int num_vertices;
   int num_components;
   int num_elements;
   int vertex_size;
   int vbo_size;

   int num_ssbo_components; //In case there are padding
   int num_ssbo_elements; //dito
   int ssbo_size;

} DemoResources;

static float*
create_vertex_data(VkdfContext *ctx, DemoResources *res)
{
   float *vertex_data;

   vertex_data = (float*) malloc(res->vbo_size);
   for (int i = 0; i < res->num_elements; i++)
      vertex_data[i] = i + 1 + LSB;

   return vertex_data;
}

static VkdfBuffer
create_vertex_buffer(VkdfContext *ctx, DemoResources *res)
{
   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flag
                         res->vbo_size,                  // size
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,    // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type


   vkdf_buffer_map_and_fill(ctx, buf, 0, res->vbo_size, res->vertex_data);

   return buf;
}

static VkdfBuffer
create_ssbo(VkdfContext *ctx, DemoResources *res)
{
   float initial_values[res->num_ssbo_elements];

   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flags
                         res->ssbo_size,                     // size
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,   // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type

   // we set some initial values just to confirm if it is properly updated
   for (int i = 0; i < res->num_ssbo_elements; i++) {
      initial_values[i] = 666.0f;
   }
   vkdf_buffer_map_and_fill(ctx, buf, 0, res->ssbo_size, &initial_values);

   return buf;
}

static VkRenderPass
create_render_pass(VkdfContext *ctx)
{
   // Single subpass

   VkSubpassDescription subpass;
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.flags = 0;
   subpass.inputAttachmentCount = 0;
   subpass.pInputAttachments = NULL;
   subpass.colorAttachmentCount = 0;
   subpass.pColorAttachments = NULL;
   subpass.pResolveAttachments = NULL;
   subpass.pDepthStencilAttachment = NULL;
   subpass.preserveAttachmentCount = 0;
   subpass.pPreserveAttachments = NULL;

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 0;
   rp_info.pAttachments = NULL;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = &subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass;
   VkResult res =
      vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create render pass");

   return render_pass;
}

static void
render_pass_commands(VkdfContext *ctx, DemoResources *res)
{
   VkClearValue clear_values[1];
   clear_values[0].color.float32[0] = 0.0f;
   clear_values[0].color.float32[1] = 0.0f;
   clear_values[0].color.float32[2] = 1.0f;
   clear_values[0].color.float32[3] = 1.0f;

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->render_pass;
   rp_begin.framebuffer = NULL;
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = ctx->width;
   rp_begin.renderArea.extent.height = ctx->height;
   rp_begin.clearValueCount = 1;
   rp_begin.pClearValues = clear_values;

   vkCmdBeginRenderPass(res->render_cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Pipeline
   vkCmdBindPipeline(res->render_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipeline);

   // Descriptor set
   vkCmdBindDescriptorSets(res->render_cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                      // First decriptor set
                           1,                      // Descriptor set count
                           &res->descriptor_set_ssbo,   // Descriptor sets
                           0,                      // Dynamic offset count
                           NULL);                  // Dynamic offsets

   // Vertex buffer
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->render_cmd_buf,
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &res->vertex_buf.buf,    // Buffers
                          offsets);                // Offsets

   // Draw
   vkCmdDraw(res->render_cmd_buf,
             res->num_vertices,           // vertex count
             res->num_vertices,           // instance count == vertex count (POINTS TOPOLOGY)
             0,                    // first vertex
             0);                   // first instance

   vkCmdEndRenderPass(res->render_cmd_buf);
}

static VkPipelineLayout
create_pipeline_layout(VkdfContext *ctx,
                       VkDescriptorSetLayout set_layout_ssbo)
{
   VkPipelineLayoutCreateInfo pipeline_layout_info;
   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 1;
   pipeline_layout_info.pSetLayouts = &set_layout_ssbo;
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

VkFormat
format_from_num_components(int num_components)
{
   assert(num_components <= 4);
   assert(num_components >= 1);

   switch (num_components) {
   case 1: return VK_FORMAT_R32_SFLOAT;
   case 2: return VK_FORMAT_R32G32_SFLOAT;
   case 3: return VK_FORMAT_R32G32B32_SFLOAT;
   case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
   default:
      //unreachable
      return VK_FORMAT_R32G32B32A32_SFLOAT;
   }
}

static void
init_resources(VkdfContext *ctx,
               DemoResources *res,
               int num_vertices,
               int num_components)
{
   char filename[100];

   assert(num_vertices > 0);
   assert(num_components >= 1 && num_components <= 4);

   memset(res, 0, sizeof(DemoResources));

   // Fill useful component/element sizes, some derived
   res->num_vertices = num_vertices;
   res->num_components = num_components;
   res->num_elements = num_vertices * num_components;
   res->vbo_size = res->num_elements * sizeof(float);
   res->vertex_size = num_components * sizeof(float);

   res->num_ssbo_components = res->num_components == 3 ? 4 : res->num_components;
   res->num_ssbo_elements = res->num_ssbo_components * num_vertices;
   res->ssbo_size = res->num_ssbo_elements * sizeof(float);

   // Vertex buffer
   res->vertex_data = create_vertex_data(ctx, res);
   res->vertex_buf = create_vertex_buffer(ctx, res);

   // SSBO
   res->ssbo = create_ssbo(ctx, res);

   // Shaders
   snprintf(filename, 100, "shader_%i.vert.spv", res->num_components);
   res->vs_module = vkdf_create_shader_module(ctx, filename);

   // Render pass
   res->render_pass = create_render_pass(ctx);

   // Descriptor pool
   res->descriptor_pool_ssbo =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);

   // Descriptor set (bound to SSBO)
   res->set_layout_ssbo =
      vkdf_create_buffer_descriptor_set_layout(ctx, SSBO_BINDING, 1,
                                               VK_SHADER_STAGE_VERTEX_BIT,
                                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

   res->descriptor_set_ssbo =
      create_descriptor_set(ctx, res->descriptor_pool_ssbo, res->set_layout_ssbo);

   VkDeviceSize ssbo_offset = 0;
   VkDeviceSize ssbo_size = res->ssbo_size;
   vkdf_descriptor_set_buffer_update(ctx, res->descriptor_set_ssbo, res->ssbo.buf,
                                     SSBO_BINDING, 1, &ssbo_offset, &ssbo_size,
                                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

   // Pipeline
   res->pipeline_layout =
      create_pipeline_layout(ctx, res->set_layout_ssbo);

   VkVertexInputBindingDescription vi_binding;
   vi_binding.binding = 0;
   vi_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding.stride = res->vertex_size;

   VkVertexInputAttributeDescription vi_attribs[1];
   vi_attribs[0].binding = 0;
   vi_attribs[0].location = 0;
   vi_attribs[0].format = format_from_num_components(res->num_components);
   vi_attribs[0].offset = 0;

   res->pipeline = vkdf_create_basic_pipeline(ctx,
                                              NULL,
                                              1, &vi_binding,
                                              1, vi_attribs,
                                              res->render_pass,
                                              res->pipeline_layout,
                                              VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
                                              res->vs_module,
                                              VK_SHADER_STAGE_VERTEX_BIT);

   // Command pool
   res->cmd_pool = vkdf_create_gfx_command_pool(ctx, 0);

   // Command buffers
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1,
                              &res->render_cmd_buf);

   vkdf_command_buffer_begin(res->render_cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
   render_pass_commands(ctx, res);
   vkdf_command_buffer_end(res->render_cmd_buf);
}

static void
print_component(int c)
{
   assert(c < 4);
   assert(c >= 0);

   switch (c) {
   case 0: fprintf(stdout, "x: ");
      break;
   case 1: fprintf(stdout, "y: ");
      break;
   case 2: fprintf(stdout, "z: ");
      break;
   case 3: fprintf(stdout, "w: ");
      break;
   default:
      //unreachable
      break;
   }
}

static const
char *float_to_hex(float f)
{
        union {
                float f;
                unsigned i;
        } b;

        b.f = f;
        char *s = (char *) malloc(100);
        sprintf(s, "0x%08X", b.i);
        return s;
}

//fetch ssbo, compare with expected, and print content
static void
check_ssbo_values(VkdfContext *ctx, DemoResources *res)
{
   float feedback[res->num_ssbo_elements];
   int index = 0;
   int index_ssbo = 0;
   bool result = true;

   vkdf_buffer_map_and_get(ctx,
                           res->ssbo, 0, res->ssbo_size, feedback);

   for (int vertex = 0; vertex < res->num_vertices; vertex++) {
      fprintf(stdout, "Sample%i:", vertex + 1);
      if (res->num_components > 1)
         fprintf(stdout, "\n*********************************\n");

      for (int c = 0; c < res->num_components; c++) {
         if (res->num_components > 1) print_component(c);
         fprintf(stdout, "Original = %.14g[%s] Fetched = %.14g[%s]",
                 res->vertex_data[index], float_to_hex(res->vertex_data[index]),
                 feedback[index_ssbo], float_to_hex(feedback[index_ssbo]));

         if (res->vertex_data[index] == feedback[index_ssbo]) {
            fprintf(stdout, "\tequal\n");
         } else {
            fprintf(stdout, "\tWRONG\n");
            result = false;
         }

         index++;
         index_ssbo++;
      }
      //vec3 needs a vec4 alignment
      if (res->num_components == 3)
         index_ssbo++;

      fprintf(stdout, "\n");
   }

   if (result)
      fprintf(stdout, "Correct: all values equal.\n");
   else
      fprintf(stdout, "WRONG: at least one value different.\n");
}

static void
scene_render(VkdfContext *ctx, void *data)
{
   DemoResources *res = (DemoResources *) data;
   VkPipelineStageFlags pipeline_stages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

   vkdf_command_buffer_execute(ctx,
                               res->render_cmd_buf,
                               &pipeline_stages,
                               0, NULL,
                               0, NULL);

   check_ssbo_values(ctx, res);
}

static void
destroy_pipeline_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyPipeline(ctx->device, res->pipeline, NULL);
   vkDestroyPipelineLayout(ctx->device, res->pipeline_layout, NULL);
}

static void
destroy_shader_resources(VkdfContext *ctx, DemoResources *res)
{
  vkDestroyShaderModule(ctx->device, res->vs_module, NULL);
}

static void
destroy_descriptor_resources(VkdfContext *ctx, DemoResources *res)
{
   vkFreeDescriptorSets(ctx->device,
                        res->descriptor_pool_ssbo, 1, &res->descriptor_set_ssbo);
   vkDestroyDescriptorSetLayout(ctx->device, res->set_layout_ssbo, NULL);
   vkDestroyDescriptorPool(ctx->device, res->descriptor_pool_ssbo, NULL);
}

static void
destroy_ssbo_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyBuffer(ctx->device, res->ssbo.buf, NULL);
   vkFreeMemory(ctx->device, res->ssbo.mem, NULL);
}

void
cleanup_resources(VkdfContext *ctx, DemoResources *res)
{
   destroy_pipeline_resources(ctx, res);
   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);
   vkdf_destroy_buffer(ctx, &res->vertex_buf);
   free(res->vertex_data);
   destroy_descriptor_resources(ctx, res);
   destroy_ssbo_resources(ctx, res);
   destroy_shader_resources(ctx, res);
}

int
main(int argc, char *argv[])
{
   VkdfContext ctx;
   DemoResources resources;
   int num_vertices = DEFAULT_NUM_VERTICES;
   int num_components = DEFAULT_NUM_COMPONENTS;

   if (argc > 3) {
      fprintf(stdout, "Usage: ./ssbo [num_components] [num_samples]\n");
      fprintf(stdout, "\tnum_components needs to be on the range [1..4]\n");
      fprintf(stdout, "\tnum_samples needs to be on the range [1..20]\n");
      fprintf(stdout, "\tWrong values will be defaulted\n");
   }
   if (argc > 1) {
      num_components = atoi(argv[1]);
      if (num_components < 1 || num_components > 4)
         num_components = DEFAULT_NUM_COMPONENTS;
   }
   if (argc > 2) {
      num_vertices = atoi(argv[2]);
      if (num_vertices < 1 || num_vertices > 20)
         num_vertices = DEFAULT_NUM_VERTICES;
   }
   fprintf(stdout, "Running ssbo test with params (num_components, num_samples) = (%i, %i)\n",
           num_components, num_vertices);

   //Although we don't need a full window initialization, it is easier to just
   //use the vanilla _init
   vkdf_init(&ctx, 20, 20, false, false, ENABLE_DEBUG);
   init_resources(&ctx, &resources, num_vertices, num_components);

   scene_render(&ctx, &resources);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
