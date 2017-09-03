#include "vkdf.hpp"

VkdfImage
vkdf_create_image(VkdfContext *ctx,
                  uint32_t width,
                  uint32_t height,
                  uint32_t num_levels,
                  VkImageType image_type,
                  VkFormat format,
                  VkFormatFeatureFlags format_flags,
                  VkImageUsageFlags usage_flags,
                  uint32_t mem_props,
                  VkImageAspectFlags aspect_flags,
                  VkImageViewType image_view_type)
{
   VkdfImage image;

   image.format = format;

   // Create image
   VkFormatProperties props;
   vkGetPhysicalDeviceFormatProperties(ctx->phy_device, format, &props);
   if ((props.optimalTilingFeatures & format_flags) != format_flags)
      vkdf_fatal("Can't create image: unsupported format features");

   VkImageCreateInfo image_info = {};
   image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   image_info.pNext = NULL;
   image_info.imageType = image_type;
   image_info.format = format;
   image_info.extent.width = width;
   image_info.extent.height = height;
   image_info.extent.depth = 1;
   image_info.mipLevels = num_levels;
   image_info.arrayLayers = 1;
   image_info.samples = VK_SAMPLE_COUNT_1_BIT;
   image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   image_info.usage = usage_flags;
   image_info.queueFamilyIndexCount = 0;
   image_info.pQueueFamilyIndices = NULL;
   image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   image_info.flags = 0;

   VK_CHECK(vkCreateImage(ctx->device, &image_info, NULL, &image.image));

   // Allocate and bind memory for the image
   VkMemoryRequirements mem_reqs;
   vkGetImageMemoryRequirements(ctx->device, image.image, &mem_reqs);

   VkMemoryAllocateInfo mem_alloc = {};
   mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mem_alloc.pNext = NULL;
   mem_alloc.allocationSize = mem_reqs.size;
   bool result =
      vkdf_memory_type_from_properties(ctx,
                                       mem_reqs.memoryTypeBits,
                                       mem_props,
                                       &mem_alloc.memoryTypeIndex);
   assert(result);
   VK_CHECK(vkAllocateMemory(ctx->device, &mem_alloc, NULL, &image.mem));
   VK_CHECK(vkBindImageMemory(ctx->device, image.image, image.mem, 0));

   // Create image view
   VkImageViewCreateInfo view_info = {};
   view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   view_info.pNext = NULL;
   view_info.image = image.image;
   view_info.format = format;
   view_info.components.r = VK_COMPONENT_SWIZZLE_R;
   view_info.components.g = VK_COMPONENT_SWIZZLE_G;
   view_info.components.b = VK_COMPONENT_SWIZZLE_B;
   view_info.components.a = VK_COMPONENT_SWIZZLE_A;
   view_info.subresourceRange.aspectMask = aspect_flags;
   view_info.subresourceRange.baseMipLevel = 0;
   view_info.subresourceRange.levelCount = num_levels;
   view_info.subresourceRange.baseArrayLayer = 0;
   view_info.subresourceRange.layerCount = 1;
   view_info.viewType = image_view_type;
   view_info.flags = 0;

   VK_CHECK(vkCreateImageView(ctx->device, &view_info, NULL, &image.view));

   return image;
}

void
vkdf_destroy_image(VkdfContext *ctx, VkdfImage *image)
{
   vkDestroyImageView(ctx->device, image->view, NULL);
   vkDestroyImage(ctx->device, image->image, NULL);
   vkFreeMemory(ctx->device, image->mem, NULL);
}

VkImageSubresourceRange
vkdf_create_image_subresource_range(VkImageAspectFlags aspect,
                                    uint32_t base_level,
                                    uint32_t level_count,
                                    uint32_t base_layer,
                                    uint32_t layer_count)
{
   VkImageSubresourceRange subresource_range = { };
   subresource_range.aspectMask = aspect;
   subresource_range.baseMipLevel = base_level;
   subresource_range.levelCount = level_count;
   subresource_range.baseArrayLayer = base_layer;
   subresource_range.layerCount = layer_count;
   return subresource_range;
}

VkImageSubresourceLayers
vkdf_create_image_subresource_layers(VkImageAspectFlags aspect,
                                     uint32_t level,
                                     uint32_t base_layer,
                                     uint32_t layer_count)
{
   VkImageSubresourceLayers subresource_layers = { };
   subresource_layers.aspectMask = aspect;
   subresource_layers.mipLevel = level;
   subresource_layers.baseArrayLayer = base_layer;
   subresource_layers.layerCount = layer_count;
   return subresource_layers;
}

VkImageCopy
vkdf_create_image_copy_region(VkImageSubresourceLayers src_subresource_layers,
                              uint32_t src_offset_x,
                              uint32_t src_offset_y,
                              uint32_t src_offset_z,
                              VkImageSubresourceLayers dst_subresource_layers,
                              uint32_t dst_offset_x,
                              uint32_t dst_offset_y,
                              uint32_t dst_offset_z,
                              uint32_t width,
                              uint32_t height,
                              uint32_t depth)
{
   VkImageCopy region = {};

   region.srcSubresource = src_subresource_layers;
   region.srcOffset.x = src_offset_x;
   region.srcOffset.y = src_offset_y;
   region.srcOffset.z = src_offset_z;

   region.dstSubresource = dst_subresource_layers;
   region.dstOffset.x = dst_offset_x;
   region.dstOffset.y = dst_offset_y;
   region.dstOffset.z = dst_offset_z;

   region.extent.width = width;
   region.extent.height = height;
   region.extent.depth = depth;

   return region;
}

void
vkdf_image_set_layout(VkdfContext *ctx,
                      VkCommandBuffer cmd_buf,
                      VkImage image,
                      VkImageSubresourceRange subresource_range,
                      VkImageLayout old_layout,
                      VkImageLayout new_layout,
                      VkPipelineStageFlags src_stage_mask,
                      VkPipelineStageFlags dst_stage_mask)
{
   VkAccessFlags src_access_mask = 0;
   VkAccessFlags dst_access_mask = 0;

   if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
      src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

   if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
      src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;

   if (old_layout == VK_IMAGE_LAYOUT_PREINITIALIZED)
      src_access_mask = VK_ACCESS_HOST_WRITE_BIT;

   if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
      dst_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;

   if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
      dst_access_mask = VK_ACCESS_TRANSFER_READ_BIT;

   if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      dst_access_mask = VK_ACCESS_SHADER_READ_BIT;

   if (new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
      dst_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

   if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
      dst_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

   VkImageMemoryBarrier barrier =
      vkdf_create_image_barrier(src_access_mask,
                                dst_access_mask,
                                old_layout,
                                new_layout,
                                image,
                                subresource_range);

   vkCmdPipelineBarrier(cmd_buf,
                        src_stage_mask,
                        dst_stage_mask,
                        0,
                        0, NULL,
                        0, NULL,
                        1, &barrier);
}

