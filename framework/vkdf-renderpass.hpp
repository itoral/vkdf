#ifndef __VKDF_RENDERPASS_H__
#define __VKDF_RENDERPASS_H__

VkRenderPass
vkdf_renderpass_simple_new(VkdfContext *ctx,
                           VkFormat color_format,
                           VkFormat depth_format);

#endif
