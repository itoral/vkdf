#ifndef __VKDF_PIPELINE_H__
#define __VKDF_PIPELINE_H__

#include "vkdf-deps.hpp"
#include "vkdf-init.hpp"

VkPipeline
vkdf_create_gfx_pipeline(VkdfContext *ctx,
                         VkPipelineCache *cache,
                         uint32_t num_vi_bindings,
                         VkVertexInputBindingDescription *vi_bindings,
                         uint32_t num_vi_attribs,
                         VkVertexInputAttributeDescription *vi_attribs,
                         bool enable_depth_test,
                         VkCompareOp depth_compare_op,
                         VkRenderPass render_pass,
                         VkPipelineLayout pipeline_layout,
                         VkPrimitiveTopology primitive,
                         VkCullModeFlagBits cull_mode,
                         uint32_t num_color_attachments,
                         VkShaderModule vs_module,
                         VkShaderModule fs_module);

VkPipeline
vkdf_create_gfx_pipeline(VkdfContext *ctx,
                         VkPipelineCache *cache,
                         uint32_t num_vi_bindings,
                         VkVertexInputBindingDescription *vi_bindings,
                         uint32_t num_vi_attribs,
                         VkVertexInputAttributeDescription *vi_attribs,
                         bool enable_depth_test,
                         VkCompareOp depth_compare_op,
                         VkRenderPass render_pass,
                         VkPipelineLayout pipeline_layout,
                         VkPrimitiveTopology primitive,
                         VkCullModeFlagBits cull_mode,
                         uint32_t num_color_attachments,
                         const VkPipelineShaderStageCreateInfo *vs_info,
                         const VkPipelineShaderStageCreateInfo *fs_info);

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

static inline void
vkdf_pipeline_fill_shader_stage_info(VkPipelineShaderStageCreateInfo *info,
                                     VkShaderStageFlagBits stage,
                                     VkShaderModule module,
                                     const VkSpecializationInfo *si)
{
   info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   info->pNext = NULL;
   info->pSpecializationInfo = si;
   info->flags = 0;
   info->stage = stage;
   info->pName = "main";
   info->module = module;
}

static inline void
vkdf_vertex_binding_set(VkVertexInputBindingDescription *desc,
                        uint32_t binding,
                        VkVertexInputRate input_rate,
                        uint32_t stride)
{
   desc->binding = binding;
   desc->inputRate = input_rate;
   desc->stride = stride;
}

static inline void
vkdf_vertex_attrib_set(VkVertexInputAttributeDescription *desc,
                       uint32_t binding,
                       uint32_t location,
                       VkFormat format,
                       uint32_t offset)
{
   desc->binding = binding;
   desc->location = location;
   desc->format = format;
   desc->offset = offset;
}

#endif
