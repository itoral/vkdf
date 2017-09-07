#include "vkdf.hpp"

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
                           VkImageLayout depth_final_layout)
{
   VkAttachmentDescription atts[2];

   uint32_t idx = 0;
   int32_t color_idx = -1;
   int32_t depth_idx = -1;

   // Color attachment
   if (color_format != VK_FORMAT_UNDEFINED) {
      atts[idx].format = color_format;
      atts[idx].samples = VK_SAMPLE_COUNT_1_BIT;
      atts[idx].loadOp = color_load;
      atts[idx].storeOp = color_store;
      atts[idx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      atts[idx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      atts[idx].initialLayout = color_initial_layout;
      atts[idx].finalLayout = color_final_layout;
      atts[idx].flags = 0;

      color_idx = idx++;
   }

   // Depth attachment
   if (depth_format != VK_FORMAT_UNDEFINED) {
      atts[idx].format = depth_format;
      atts[idx].samples = VK_SAMPLE_COUNT_1_BIT;
      atts[idx].loadOp = depth_load;
      atts[idx].storeOp = depth_store;
      atts[idx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      atts[idx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      atts[idx].initialLayout = depth_initial_layout;
      atts[idx].finalLayout = depth_final_layout;
      atts[idx].flags = 0;

      depth_idx = idx++;
   }

   // Attachment references from subpasses
   VkAttachmentReference color_ref;
   color_ref.attachment = color_idx;
   color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkAttachmentReference depth_ref;
   depth_ref.attachment = depth_idx;
   depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

   // Single subpass
   VkSubpassDescription subpass[1];
   subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass[0].flags = 0;
   subpass[0].inputAttachmentCount = 0;
   subpass[0].pInputAttachments = NULL;
   subpass[0].colorAttachmentCount = color_idx >= 0 ? 1 : 0;
   subpass[0].pColorAttachments = color_idx >= 0 ? &color_ref : NULL;
   subpass[0].pResolveAttachments = NULL;
   subpass[0].pDepthStencilAttachment = depth_idx >= 0 ? &depth_ref : NULL;
   subpass[0].preserveAttachmentCount = 0;
   subpass[0].pPreserveAttachments = NULL;

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = idx;
   rp_info.pAttachments = atts;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass;
   VK_CHECK(vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass));

   return render_pass;
}
