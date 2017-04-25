#ifndef __VKDF_FRAMEBUFFER_H__
#define __VKDF_FRAMEBUFFER_H__

VkFramebuffer
vkdf_create_framebuffer(VkdfContext *ctx,
                        VkRenderPass render_pass,
                        VkImageView image,
                        uint32_t width,
                        uint32_t height,
                        VkdfImage *depth_image);
VkFramebuffer *
vkdf_create_framebuffers_for_swap_chain(VkdfContext *ctx,
                                        VkRenderPass render_pass,
                                        VkdfImage *depth_image);

#endif
