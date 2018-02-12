#include "vkdf.hpp"

VkImage
create_image(VkdfContext *ctx,
             uint32_t width,
             uint32_t height,
             uint32_t num_levels,
             VkImageType image_type,
             VkFormat format,
             VkFormatFeatureFlags format_flags,
             VkImageUsageFlags usage_flags)
{
   VkImage image;

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

   VK_CHECK(vkCreateImage(ctx->device, &image_info, NULL, &image));

   return image;
}

static void
bind_image_memory(VkdfContext *ctx,
                  VkImage image,
                  uint32_t mem_props,
                  VkDeviceMemory *mem)
{
   VkMemoryRequirements mem_reqs;
   vkGetImageMemoryRequirements(ctx->device, image, &mem_reqs);

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
   VK_CHECK(vkAllocateMemory(ctx->device, &mem_alloc, NULL, mem));
   VK_CHECK(vkBindImageMemory(ctx->device, image, *mem, 0));
}

static VkImageView
create_image_view(VkdfContext *ctx,
                  VkImageViewType view_type,
                  VkImage image,
                  VkFormat format,
                  VkImageAspectFlags aspect_flags,
                  uint32_t num_levels,
                  VkComponentSwizzle swz_r,
                  VkComponentSwizzle swz_g,
                  VkComponentSwizzle swz_b,
                  VkComponentSwizzle swz_a)
{

   VkImageViewCreateInfo view_info = {};
   view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   view_info.pNext = NULL;
   view_info.image = image;
   view_info.format = format;
   view_info.components.r = swz_r;
   view_info.components.g = swz_g;
   view_info.components.b = swz_b;
   view_info.components.a = swz_a;
   view_info.subresourceRange.aspectMask = aspect_flags;
   view_info.subresourceRange.baseMipLevel = 0;
   view_info.subresourceRange.levelCount = num_levels;
   view_info.subresourceRange.baseArrayLayer = 0;
   view_info.subresourceRange.layerCount = 1;
   view_info.viewType = view_type;
   view_info.flags = 0;

   VkImageView view;
   VK_CHECK(vkCreateImageView(ctx->device, &view_info, NULL, &view));
   return view;
}

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

   image.image = create_image(ctx,
                              width, height, num_levels,
                              image_type, format,
                              format_flags,
                              usage_flags);

   bind_image_memory(ctx, image.image, mem_props, &image.mem);

   image.view = create_image_view(ctx,
                                  VK_IMAGE_VIEW_TYPE_2D,
                                  image.image, format,
                                  aspect_flags, num_levels,
                                  VK_COMPONENT_SWIZZLE_R,
                                  VK_COMPONENT_SWIZZLE_G,
                                  VK_COMPONENT_SWIZZLE_B,
                                  VK_COMPONENT_SWIZZLE_A);

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

VkImageBlit
vkdf_create_image_blit_region(VkImageSubresourceLayers src_subresource_layers,
                              glm::uvec3 src_offset,
                              glm::uvec3 src_size,
                              VkImageSubresourceLayers dst_subresource_layers,
                              glm::uvec3 dst_offset,
                              glm::uvec3 dst_size)
{
   VkImageBlit region = {};

   region.srcSubresource = src_subresource_layers;
   region.srcOffsets[0].x = src_offset.x;
   region.srcOffsets[0].y = src_offset.y;
   region.srcOffsets[0].z = src_offset.z;
   region.srcOffsets[1].x = src_offset.x + src_size.x;
   region.srcOffsets[1].y = src_offset.y + src_size.y;
   region.srcOffsets[1].z = src_offset.z + src_size.z;

   region.dstSubresource = dst_subresource_layers;
   region.dstOffsets[0].x = dst_offset.x;
   region.dstOffsets[0].y = dst_offset.y;
   region.dstOffsets[0].z = dst_offset.z;
   region.dstOffsets[1].x = dst_offset.x + dst_size.x;
   region.dstOffsets[1].y = dst_offset.y + dst_size.y;
   region.dstOffsets[1].z = dst_offset.z + dst_size.z;

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

struct _MipmapInfo {
   uint32_t w, h;
   VkDeviceSize bytes;
};

VkComponentSwizzle
compute_component_swizzle_from_mask(uint32_t mask, bool is_alpha)
{
   // FIXME: we only support 8-bit color components for now
   switch (mask) {
      case 0x00000000:
         return is_alpha ? VK_COMPONENT_SWIZZLE_ONE :
                           VK_COMPONENT_SWIZZLE_ZERO;
      case 0x000000FF:
         return VK_COMPONENT_SWIZZLE_R;
      case 0x0000FF00:
         return VK_COMPONENT_SWIZZLE_G;
      case 0x00FF0000:
         return VK_COMPONENT_SWIZZLE_B;
      case 0xFF000000:
         return VK_COMPONENT_SWIZZLE_A;
      default:
         assert(!"unsupported color mask");
         return VK_COMPONENT_SWIZZLE_ZERO;
   }
}

static inline uint32_t
compute_bpp_from_sdl_surface(SDL_Surface *surf)
{
   /* Warning: Don't trust the SDL format info, some times it is bogus */
   assert(surf->pitch % surf->w == 0);
   return 8 * (surf->pitch / surf->w);
}

static VkDeviceSize
compute_gpu_image_size(uint32_t width,
                       uint32_t height,
                       uint32_t bpp,
                       bool with_mipmaps,
                       uint32_t *num_levels,
                       struct _MipmapInfo **mip_levels)
{
   if (with_mipmaps)
      *num_levels = 1 + ((uint32_t) floorf(log2f(MAX2(width, height))));
   else
      *num_levels = 1;
   *mip_levels = g_new(struct _MipmapInfo, *num_levels);

   VkDeviceSize total_bytes = 0;
   uint32_t size_x = width;
   uint32_t size_y = height;
   for (uint32_t i = 0; i < *num_levels; i++) {
      struct _MipmapInfo *info = &(*mip_levels)[i];
      info->bytes = size_x * size_y * bpp / 8;
      total_bytes += info->bytes;
      info->w = size_x;
      info->h = size_y;

      size_x = MAX2(size_x / 2, 1);
      size_y = MAX2(size_y / 2, 1);
   }

   return total_bytes;
}

static void
create_image_from_data(VkdfContext *ctx,
                       VkCommandPool pool,
                       VkdfImage *image,
                       uint32_t width,
                       uint32_t height,
                       VkFormat format,
                       uint32_t bpp,
                       const VkComponentSwizzle *swz,
                       uint32_t gen_mipmaps,
                       const void *pixel_data)
{
   uint32_t num_levels;
   struct _MipmapInfo *mip_levels;
   VkDeviceSize gpu_image_bytes =
      compute_gpu_image_size(width, height, bpp,
                             gen_mipmaps, &num_levels, &mip_levels);

   // Upload pixel data to a host-visible staging buffer
   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,
                         gpu_image_bytes,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   uint8_t *data;
   vkdf_memory_map(ctx, buf.mem, 0, VK_WHOLE_SIZE, (void **)&data);
   memcpy(data, pixel_data, mip_levels[0].bytes);
   vkdf_memory_unmap(ctx, buf.mem, buf.mem_props, 0, VK_WHOLE_SIZE);

   // Create the image
   image->format = format;

   image->image = create_image(ctx, width, height, num_levels,
                               VK_IMAGE_TYPE_2D,
                               image->format,
                               VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
                               VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT);

   bind_image_memory(ctx,
                     image->image,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &image->mem);

   image->view = create_image_view(ctx,
                                   VK_IMAGE_VIEW_TYPE_2D,
                                   image->image,
                                   image->format,
                                   VK_IMAGE_ASPECT_COLOR_BIT,
                                   num_levels,
                                   swz[0], swz[1], swz[2], swz[3]);

   // Copy data from staging buffer to mip level 0
   VkBufferImageCopy region = {};
   region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   region.imageSubresource.mipLevel = 0;
   region.imageSubresource.baseArrayLayer = 0;
   region.imageSubresource.layerCount = 1;
   region.imageExtent.width = width;
   region.imageExtent.height = height;
   region.imageExtent.depth = 1;
   region.bufferOffset = 0;

   VkCommandBuffer cmd_buf;
   vkdf_create_command_buffer(ctx, pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &cmd_buf);

   vkdf_command_buffer_begin(cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

   VkImageSubresourceRange mip_0 =
      vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                          0, 1, 0, 1);

   VkImageMemoryBarrier barrier_layout_mip_0 =
      vkdf_create_image_barrier(0,
                                VK_ACCESS_TRANSFER_WRITE_BIT,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                image->image,
                                mip_0);

   vkCmdPipelineBarrier(cmd_buf,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0,
                        0, NULL,
                        0, NULL,
                        1, &barrier_layout_mip_0);

   vkCmdCopyBufferToImage(cmd_buf, buf.buf, image->image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1, &region);


   if (num_levels == 1) {
      // If we only need one level, we are done
      barrier_layout_mip_0 =
         vkdf_create_image_barrier(VK_ACCESS_TRANSFER_READ_BIT,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   image->image,
                                   mip_0);

      vkCmdPipelineBarrier(cmd_buf,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0,
                           0, NULL,
                           0, NULL,
                           1, &barrier_layout_mip_0);
   } else {
      // Blit level 0 to mip levels [1..num_levels]
      barrier_layout_mip_0 =
         vkdf_create_image_barrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_ACCESS_TRANSFER_READ_BIT,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   image->image,
                                   mip_0);

      VkImageSubresourceRange mip_1N =
         vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                             1, num_levels - 1, 0, 1);

      VkImageMemoryBarrier barrier_layout_mip_1N;
      barrier_layout_mip_1N =
         vkdf_create_image_barrier(0,
                                   VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   image->image,
                                   mip_1N);

      VkImageMemoryBarrier barriers[2] = {
         barrier_layout_mip_0,
         barrier_layout_mip_1N
      };

      vkCmdPipelineBarrier(cmd_buf,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0, NULL,
                           0, NULL,
                           2, barriers);

      VkImageBlit blit_region;
      blit_region.srcSubresource =
         vkdf_create_image_subresource_layers(VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1);

      blit_region.srcOffsets[0].x = 0;
      blit_region.srcOffsets[0].y = 0;
      blit_region.srcOffsets[0].z = 0;

      blit_region.srcOffsets[1].x = mip_levels[0].w;
      blit_region.srcOffsets[1].y = mip_levels[0].h;
      blit_region.srcOffsets[1].z = 1;

      for (uint32_t i = 1; i < num_levels; i++) {
         blit_region.dstSubresource =
            vkdf_create_image_subresource_layers(VK_IMAGE_ASPECT_COLOR_BIT,
                                                 i, 0, 1);
         blit_region.dstOffsets[0].x = 0;
         blit_region.dstOffsets[0].y = 0;
         blit_region.dstOffsets[0].z = 0;

         blit_region.dstOffsets[1].x = mip_levels[i].w;
         blit_region.dstOffsets[1].y = mip_levels[i].h;
         blit_region.dstOffsets[1].z = 1;

         vkCmdBlitImage(cmd_buf,
                        image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &blit_region,
                        VK_FILTER_NEAREST);
      }

      barrier_layout_mip_0 =
         vkdf_create_image_barrier(VK_ACCESS_TRANSFER_READ_BIT,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   image->image,
                                   mip_0);

      barrier_layout_mip_1N =
         vkdf_create_image_barrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   image->image,
                                   mip_1N);

      barriers[0] = barrier_layout_mip_0;
      barriers[1] = barrier_layout_mip_1N;
      vkCmdPipelineBarrier(cmd_buf,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0,
                           0, NULL,
                           0, NULL,
                           2, barriers);
   }

   vkdf_command_buffer_end(cmd_buf);
   vkdf_command_buffer_execute_sync(ctx, cmd_buf, 0);

   vkdf_destroy_buffer(ctx, &buf);
   vkFreeCommandBuffers(ctx->device, pool, 1, &cmd_buf);
   g_free(mip_levels);
}

static VkFormat
guess_format_from_bpp(uint32_t bpp, bool is_srgb)
{
   switch (bpp) {
      case 32:
         return is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
      case 24:
         return is_srgb ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM;
      case 16:
         return is_srgb ? VK_FORMAT_R8G8_SRGB : VK_FORMAT_R8G8_UNORM;
      case 8:
         return is_srgb ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM;
      default:
         vkdf_error("Unsupported image format (bpp = %u)", bpp);
         return VK_FORMAT_UNDEFINED;
   }
}

static uint32_t
get_bpp_for_format(VkFormat format)
{
   // FIXME: support more formats
   switch (format) {
      /* RGBA */
      case VK_FORMAT_R32G32B32A32_SFLOAT:
         return 128;
      case VK_FORMAT_R16G16B16A16_SFLOAT:
         return 64;
      case VK_FORMAT_R8G8B8A8_UNORM:
      case VK_FORMAT_R8G8B8A8_SRGB:
         return 32;

      /* RGB */
      case VK_FORMAT_R32G32B32_SFLOAT:
         return 96;
      case VK_FORMAT_R16G16B16_SFLOAT:
         return 48;
      case VK_FORMAT_R8G8B8_UNORM:
      case VK_FORMAT_R8G8B8_SRGB:
         return 24;

      /* RG */
      case VK_FORMAT_R32G32_SFLOAT:
         return 64;
      case VK_FORMAT_R16G16_SFLOAT:
         return 32;
      case VK_FORMAT_R8G8_UNORM:
      case VK_FORMAT_R8G8_SRGB:
         return 16;

      /* R */
      case VK_FORMAT_R32_SFLOAT:
         return 32;
      case VK_FORMAT_R16_SFLOAT:
         return 16;
      case VK_FORMAT_R8_UNORM:
      case VK_FORMAT_R8_SRGB:
         return 8;

      default:
         vkdf_error("Unsupported image format (%u)", format);
         return 32;
   }
}

static void
guess_swizzle_from_format(VkFormat format, VkComponentSwizzle *swz)
{
   // FIXME: support more formats
   switch (format) {
      /* RGBA */
      case VK_FORMAT_R32G32B32A32_SFLOAT:
      case VK_FORMAT_R16G16B16A16_SFLOAT:
      case VK_FORMAT_R8G8B8A8_UNORM:
      case VK_FORMAT_R8G8B8A8_SRGB:
         swz[0] = VK_COMPONENT_SWIZZLE_R;
         swz[1] = VK_COMPONENT_SWIZZLE_G;
         swz[2] = VK_COMPONENT_SWIZZLE_B;
         swz[3] = VK_COMPONENT_SWIZZLE_A;
         break;

      /* RGB */
      case VK_FORMAT_R32G32B32_SFLOAT:
      case VK_FORMAT_R16G16B16_SFLOAT:
      case VK_FORMAT_R8G8B8_UNORM:
      case VK_FORMAT_R8G8B8_SRGB:
         swz[0] = VK_COMPONENT_SWIZZLE_R;
         swz[1] = VK_COMPONENT_SWIZZLE_G;
         swz[2] = VK_COMPONENT_SWIZZLE_B;
         swz[3] = VK_COMPONENT_SWIZZLE_ONE;
         break;

      /* RG */
      case VK_FORMAT_R32G32_SFLOAT:
      case VK_FORMAT_R16G16_SFLOAT:
      case VK_FORMAT_R8G8_UNORM:
      case VK_FORMAT_R8G8_SRGB:
         swz[0] = VK_COMPONENT_SWIZZLE_R;
         swz[1] = VK_COMPONENT_SWIZZLE_G;
         swz[2] = VK_COMPONENT_SWIZZLE_ZERO;
         swz[3] = VK_COMPONENT_SWIZZLE_ONE;
         break;

      /* R */
      case VK_FORMAT_R32_SFLOAT:  /* Assume it represents intensity */
      case VK_FORMAT_R16_SFLOAT:
      case VK_FORMAT_R8_UNORM:
      case VK_FORMAT_R8_SRGB:
         swz[0] = VK_COMPONENT_SWIZZLE_R;
         swz[1] = VK_COMPONENT_SWIZZLE_R;
         swz[2] = VK_COMPONENT_SWIZZLE_R;
         swz[3] = VK_COMPONENT_SWIZZLE_ONE;
         break;

      default:
         vkdf_error("Unsupported image format (%u)", format);
         swz[0] = VK_COMPONENT_SWIZZLE_R;
         swz[1] = VK_COMPONENT_SWIZZLE_G;
         swz[2] = VK_COMPONENT_SWIZZLE_B;
         swz[3] = VK_COMPONENT_SWIZZLE_A;
         break;
   }
}

bool
vkdf_load_image_from_file(VkdfContext *ctx,
                          VkCommandPool pool,
                          const char *path,
                          VkdfImage *image,
                          bool is_srgb)
{
   memset(image, 0, sizeof(VkdfImage));

   // Load image data from file and put pixel data in a GPU buffer
   SDL_Surface *surf = IMG_Load(path);
   if (!surf) {
      vkdf_error("image: failed to load '%s'", path);
      return false;
   }

   // Get pixel size and format
   uint32_t bpp = compute_bpp_from_sdl_surface(surf);
   VkFormat format = guess_format_from_bpp(bpp, is_srgb);
   assert(format != VK_FORMAT_UNDEFINED);

   uint32_t has_component_masks =
      surf->format->Rmask || surf->format->Gmask ||
      surf->format->Bmask || surf->format->Amask;

   // Get pixel swizzle
   VkComponentSwizzle swz[4];
   if (has_component_masks) {
      swz[0] = compute_component_swizzle_from_mask(surf->format->Rmask, false);
      swz[1] = compute_component_swizzle_from_mask(surf->format->Gmask, false);
      swz[2] = compute_component_swizzle_from_mask(surf->format->Bmask, false);
      swz[3] = compute_component_swizzle_from_mask(surf->format->Amask, true);
   } else {
      guess_swizzle_from_format(format, swz);
   }

   // Create and initialize image
   create_image_from_data(ctx, pool,
                          image, surf->w, surf->h,
                          format, bpp, swz,
                          true, surf->pixels);

   return true;
}

void
vkdf_create_image_from_data(VkdfContext *ctx,
                            VkCommandPool pool,
                            uint32_t width,
                            uint32_t height,
                            VkFormat format,
                            bool gen_mipmaps,
                            const void *pixel_data,
                            VkdfImage *image)
{
   memset(image, 0, sizeof(VkdfImage));

   uint32_t bpp = get_bpp_for_format(format);

   VkComponentSwizzle swz[4];
   guess_swizzle_from_format(format, swz);

   create_image_from_data(ctx, pool,
                          image, width, height,
                          format, bpp, swz,
                          gen_mipmaps,
                          pixel_data);
}
