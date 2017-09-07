#ifndef __VKDF_RENDERPASS_H__
#define __VKDF_RENDERPASS_H__

VkRenderPass
vkdf_renderpass_simple_new(VkdfContext *ctx,
                           VkFormat color_format,
                           VkAttachmentLoadOp color_load,
                           VkAttachmentStoreOp color_store,
                           VkImageLayout color_initial_layout,
                           VkImageLayout color_final_layout,
                           VkFormat depth_format,
                           VkAttachmentLoadOp depth_load,
                           VkAttachmentStoreOp depth_store,
                           VkImageLayout depth_initial_layout,
                           VkImageLayout depth_final_layout);

inline VkRenderPassBeginInfo
vkdf_renderpass_begin_new(VkRenderPass renderpass,
                          VkFramebuffer framebuffer,
                          uint32_t offset_x, uint32_t offset_y,
                          uint32_t width, uint32_t height,
                          uint32_t num_clear_values, VkClearValue *clear_values)
{
   VkRenderPassBeginInfo rp_begin;

   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = renderpass;
   rp_begin.framebuffer = framebuffer;
   rp_begin.renderArea.offset.x = offset_x;
   rp_begin.renderArea.offset.y = offset_y;
   rp_begin.renderArea.extent.width = width;
   rp_begin.renderArea.extent.height = height;
   rp_begin.clearValueCount = num_clear_values;
   rp_begin.pClearValues = clear_values;

   return rp_begin;
}

#endif
