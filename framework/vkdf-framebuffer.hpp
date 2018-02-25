#ifndef __VKDF_FRAMEBUFFER_H__
#define __VKDF_FRAMEBUFFER_H__

#include "vkdf-deps.hpp"
#include "vkdf-init.hpp"
#include "vkdf-image.hpp"

VkFramebuffer
vkdf_create_framebuffer(VkdfContext *ctx,
                        VkRenderPass render_pass,
                        VkImageView image,
                        uint32_t width,
                        uint32_t height,
                        uint32_t num_extra_attachments,
                        VkdfImage *extra_attachments);
VkFramebuffer *
vkdf_create_framebuffers_for_swap_chain(VkdfContext *ctx,
                                        VkRenderPass render_pass,
                                        uint32_t num_extra_attachments,
                                        VkdfImage *extra_attachments);

#endif
