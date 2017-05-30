#ifndef __VKDF_PIPELINE_H__
#define __VKDF_PIPELINE_H__

VkPipeline
vkdf_create_basic_pipeline(VkdfContext *ctx,
                           VkPipelineCache *pipeline_cache,
                           uint32_t num_vi_bindings,
                           VkVertexInputBindingDescription *vi_bindings,
                           uint32_t num_vi_attribs,
                           VkVertexInputAttributeDescription *vi_attribs,
                           VkRenderPass render_pass,
                           VkPipelineLayout pipeline_layout,
                           VkPrimitiveTopology primitive,
                           VkShaderModule module,
                           VkShaderStageFlagBits stage);

VkPipeline
vkdf_create_gfx_pipeline(VkdfContext *ctx,
                         VkPipelineCache *cache,
                         uint32_t num_vi_bindings,
                         VkVertexInputBindingDescription *vi_bindings,
                         uint32_t num_vi_attribs,
                         VkVertexInputAttributeDescription *vi_attribs,
                         bool enable_depth_test,
                         VkRenderPass render_pass,
                         VkPipelineLayout pipeline_layout,
                         VkPrimitiveTopology primitive,
                         VkCullModeFlagBits cull_mode,
                         VkShaderModule vs_module,
                         VkShaderModule fs_module);

static inline void
vkdf_pipeline_fill_shader_stage_info(VkPipelineShaderStageCreateInfo *info,
                                     VkShaderStageFlagBits stage,
                                     VkShaderModule module)
{
   info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   info->pNext = NULL;
   info->pSpecializationInfo = NULL;
   info->flags = 0;
   info->stage = stage;
   info->pName = "main";
   info->module = module;
}

#endif
