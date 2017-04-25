#ifndef __VKDF_PIPELINE_H__
#define __VKDF_PIPELINE_H__

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

#endif
