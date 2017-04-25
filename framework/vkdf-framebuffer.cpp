#include "vkdf.hpp"

VkFramebuffer
vkdf_create_framebuffer(VkdfContext *ctx,
                        VkRenderPass render_pass,
                        VkImageView image,
                        uint32_t width,
                        uint32_t height,
                        VkdfImage *depth_image)
{
   VkImageView attachments[2];
   attachments[0] = image;
   attachments[1] = depth_image ? depth_image->view : NULL;


   VkFramebufferCreateInfo fb_info;
   fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fb_info.pNext = NULL;
   fb_info.renderPass = render_pass;
   fb_info.attachmentCount = depth_image ? 2 : 1;
   fb_info.pAttachments = attachments;
   fb_info.width = width;
   fb_info.height = height;
   fb_info.layers = 1;
   fb_info.flags = 0;

   VkFramebuffer fb;
   VK_CHECK(vkCreateFramebuffer(ctx->device, &fb_info, NULL, &fb));

   return fb;
}

VkFramebuffer *
vkdf_create_framebuffers_for_swap_chain(VkdfContext *ctx,
                                        VkRenderPass render_pass,
                                        VkdfImage *depth_image)
{
   VkFramebuffer *fbs = g_new(VkFramebuffer, ctx->swap_chain_length);
   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      fbs[i] = vkdf_create_framebuffer(ctx,
                                       render_pass,
                                       ctx->swap_chain_images[i].view,
                                       ctx->width,
                                       ctx->height,
                                       depth_image);
   }

   return fbs;
}

