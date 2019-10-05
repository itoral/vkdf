#include "vkdf-image.hpp"
#include "vkdf-util.hpp"
#include "vkdf-buffer.hpp"
#include "vkdf-cmd-buffer.hpp"
#include "vkdf-memory.hpp"
#include "vkdf-barrier.hpp"

VkImage
create_image(VkdfContext *ctx,
             uint32_t width,
             uint32_t height,
             uint32_t num_layers,
             uint32_t num_levels,
             VkImageType image_type,
             VkFormat format,
             VkFormatFeatureFlags format_flags,
             VkImageUsageFlags usage_flags,
             bool is_cube)
{
   assert(!is_cube || num_layers == 6);

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
   image_info.arrayLayers = num_layers;
   image_info.samples = VK_SAMPLE_COUNT_1_BIT;
   image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   image_info.usage = usage_flags;
   image_info.queueFamilyIndexCount = 0;
   image_info.pQueueFamilyIndices = NULL;
   image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   image_info.flags = is_cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
;

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
                  uint32_t num_layers,
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
   view_info.subresourceRange.layerCount = num_layers;
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

    uint32_t num_layers = 1;

   image.format = format;

   image.image = create_image(ctx,
                              width, height,
                              num_layers, num_levels,
                              image_type, format,
                              format_flags,
                              usage_flags,
                              false);

   bind_image_memory(ctx, image.image, mem_props, &image.mem);

   image.view = create_image_view(ctx,
                                  VK_IMAGE_VIEW_TYPE_2D,
                                  image.image, format,
                                  aspect_flags,
                                  num_layers, num_levels,
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
vkdf_image_set_layout(VkCommandBuffer cmd_buf,
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
                       uint32_t num_layers,
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

   return total_bytes * num_layers;
}

/**
 * Generates mipmaps by blitting to mip-level N from mip-level N-1 using
 * linear filtering.
 */
static void
gen_mipmaps_linear_blit(VkImage image,
                        uint32_t layer,
                        uint32_t num_levels,
                        struct _MipmapInfo *mip_levels,
                        VkCommandBuffer cmd_buf)
{
   /* Transition mip-levels 1..N to transfer destination */
   VkImageSubresourceRange mip_1N =
       vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                           1, num_levels - 1, layer, 1);

   VkImageMemoryBarrier barrier_layout_mip_1N =
       vkdf_create_image_barrier(0,
                                 VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 image, mip_1N);

   vkCmdPipelineBarrier(cmd_buf,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0,
                        0, NULL,
                        0, NULL,
                        1, &barrier_layout_mip_1N);

   for (uint32_t i = 1; i < num_levels; i++) {
      VkImageSubresourceLayers src_subresource =
         vkdf_create_image_subresource_layers(VK_IMAGE_ASPECT_COLOR_BIT,
                                              i - 1, layer, 1);

      VkImageSubresourceLayers dst_subresource =
         vkdf_create_image_subresource_layers(VK_IMAGE_ASPECT_COLOR_BIT,
                                              i, layer, 1);

      VkImageBlit region =
         vkdf_create_image_blit_region(src_subresource,
                                       glm::uvec3(0, 0, 0),
                                       glm::uvec3(mip_levels[i - 1].w,
                                                  mip_levels[i - 1].h,
                                                  1),
                                       dst_subresource,
                                       glm::uvec3(0, 0, 0),
                                       glm::uvec3(mip_levels[i].w,
                                                  mip_levels[i].h,
                                                  1));

      VkImageSubresourceRange prev_mip =
         vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                             i - 1, 1, layer, 1);

      vkdf_image_set_layout(cmd_buf, image, prev_mip,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);

      vkCmdBlitImage(cmd_buf,
                     image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     1, &region, VK_FILTER_LINEAR);
   }

   VkImageSubresourceRange mip_0Nm1 =
       vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                           0, num_levels - 1, layer, 1);
   VkImageMemoryBarrier dst_barrier_0Nm1 =
      vkdf_create_image_barrier(VK_ACCESS_TRANSFER_READ_BIT,
                                VK_ACCESS_SHADER_READ_BIT,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                image, mip_0Nm1);

   VkImageSubresourceRange mip_N =
       vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                           num_levels - 1, 1, layer, 1);
   VkImageMemoryBarrier dst_barrier_N =
      vkdf_create_image_barrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                image, mip_N);

   VkImageMemoryBarrier barriers[2] = {
      dst_barrier_0Nm1,
      dst_barrier_N,
   };
   vkCmdPipelineBarrier(cmd_buf,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0,
                        0, NULL,
                        0, NULL,
                        2, barriers);
}

static VkFormatFeatureFlags
get_format_feature_flags_from_usage(VkImageUsageFlags usage)
{
   VkFormatFeatureFlags flags = 0;

   uint64_t usage_mask = (uint64_t) usage;
   while (usage_mask != 0) {
      uint64_t usage_flag = 1 << (ffsll(usage_mask) - 1);
      switch (usage_flag) {
         case VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:
            flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
            break;
         case VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:
            flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
            break;
         case VK_IMAGE_USAGE_TRANSFER_SRC_BIT:
            flags |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT_KHR;
            break;
         case VK_IMAGE_USAGE_TRANSFER_DST_BIT:
            flags |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT_KHR;
            break;
         case VK_IMAGE_USAGE_SAMPLED_BIT:
            flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            break;
         default:
            vkdf_fatal("image: unhandled image usage flag");
            break;
      }
      usage_mask &= ~usage_flag;
   }

   return flags;
}

static void
create_image_from_data(VkdfContext *ctx,
                       VkCommandPool pool,
                       VkdfImage *image,
                       uint32_t width,
                       uint32_t height,
                       uint32_t num_layers,
                       bool is_cube,
                       VkFormat format,
                       uint32_t bpp,
                       const VkComponentSwizzle *swz,
                       VkImageUsageFlags usage,
                       bool gen_mipmaps,
                       const void **pixel_data)
{
   assert(!is_cube || num_layers == 6);

   uint32_t num_levels;
   struct _MipmapInfo *mip_levels;
   VkDeviceSize gpu_image_bytes =
      compute_gpu_image_size(width, height, num_layers, bpp,
                             gen_mipmaps, &num_levels, &mip_levels);

   if (num_levels < 2)
      gen_mipmaps = false;

   // Create the image
   image->format = format;

   usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   if (gen_mipmaps)
      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

   VkFormatFeatureFlags format_flags =
      get_format_feature_flags_from_usage(usage);

   /* FIXME: mipmaps require blitting, so if is not supported we should
    *        probably do the following:
    *
    * 1. Copy the pixel data to an image in the original format. Let's call this
    *   imageA.
    *
    * 2. Create another image with a RGBA or sRGBA format where blitting
    *    and color attachment support is mandated by the spec. Let's call this
    *    imageB
    *
    * 2. Copy mip-level 0 from imageA to imageB. To do this we can't use
    *    a normal image copy, since the pixel sizes are probably different,
    *    instead we will have to use a shader so imageA only requires the
    *    SAMPLED feature.
    *
    * 3. Finally, we generate bitmaps for imageB and use imageB as result.
    */
   if (gen_mipmaps) {
      VkFormatProperties props;
      vkGetPhysicalDeviceFormatProperties(ctx->phy_device, format, &props);
      VkFormatFeatureFlags blit_flags = (VkFormatFeatureFlags)
         VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
      if ((props.optimalTilingFeatures & blit_flags) != blit_flags) {
         vkdf_error("image: blitting is not supported for format %d, "
                    "mipmap generation might not be correct.", format);
      }
   }

   image->image = create_image(ctx, width, height, num_layers, num_levels,
                               VK_IMAGE_TYPE_2D,
                               image->format,
                               format_flags,
                               usage, is_cube);

   bind_image_memory(ctx,
                     image->image,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &image->mem);

   image->view = create_image_view(ctx,
                                   is_cube ? VK_IMAGE_VIEW_TYPE_CUBE :
                                             VK_IMAGE_VIEW_TYPE_2D,
                                   image->image,
                                   image->format,
                                   VK_IMAGE_ASPECT_COLOR_BIT,
                                   num_layers,
                                   num_levels,
                                   swz[0], swz[1], swz[2], swz[3]);

   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,
                         mip_levels[0].bytes,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   for (uint32_t i = 0; i < num_layers; i++) {
      // Upload pixel data to a host-visible staging buffer
      uint8_t *data;
      vkdf_memory_map(ctx, buf.mem, 0, VK_WHOLE_SIZE, (void **)&data);
      memcpy(data, pixel_data[i], mip_levels[0].bytes);
      vkdf_memory_unmap(ctx, buf.mem, buf.mem_props, 0, VK_WHOLE_SIZE);

      // Copy data from staging buffer to mip level 0
      VkBufferImageCopy region = {};
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = 0;
      region.imageSubresource.baseArrayLayer = i;
      region.imageSubresource.layerCount = 1;
      region.imageExtent.width = width;
      region.imageExtent.height = height;
      region.imageExtent.depth = 1;
      region.bufferOffset = 0;


      VkImageSubresourceRange mip_0 =
         vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT,
                                             0, 1, i, 1);

      VkImageMemoryBarrier barrier_layout_mip_0 =
         vkdf_create_image_barrier(0,
                                   VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   image->image,
                                   mip_0);

      VkCommandBuffer cmd_buf;
      vkdf_create_command_buffer(ctx, pool,
                                 VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                 1, &cmd_buf);

      vkdf_command_buffer_begin(cmd_buf,
                                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

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


      if (!gen_mipmaps) {
         vkdf_image_set_layout(cmd_buf, image->image, mip_0,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      } else {
         gen_mipmaps_linear_blit(image->image, i, num_levels, mip_levels, cmd_buf);
      }

      vkdf_command_buffer_end(cmd_buf);
      vkdf_command_buffer_execute_sync(ctx, cmd_buf, 0);
      vkFreeCommandBuffers(ctx->device, pool, 1, &cmd_buf);
   }


   vkdf_destroy_buffer(ctx, &buf);
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

static void
compute_image_parameters_from_surface(SDL_Surface *surf,
                                      VkFormat *format,
                                      uint32_t *bpp,
                                      bool *is_srgb,
                                      VkComponentSwizzle swz[4])
{
   // Get pixel size and format
   *bpp = compute_bpp_from_sdl_surface(surf);

   // If this image is not at least RGB, it is unlikely that it represents
   // color data. It is probably a specular intensity texture, in which
   // case it should not be sRGB encoded.
   //
   // FIXME: At least with Intel/Mesa if we attempt to blit to sRGB images
   // (which we do for mipmaps) with less than 3 components we get GPU hangs.
   // Notice that the time of this writing, the Intel/Mesa driver doesn't
   // really support blitting to RGB either (only RGBA), but so far we seem
   // to be able to do away with it just fine.
   if (*bpp < 24)
      *is_srgb = false;

   *format = guess_format_from_bpp(*bpp, *is_srgb);
   assert(*format != VK_FORMAT_UNDEFINED);

   uint32_t has_component_masks =
      surf->format->Rmask || surf->format->Gmask ||
      surf->format->Bmask || surf->format->Amask;

   // Get pixel swizzle
   if (has_component_masks) {
      swz[0] = compute_component_swizzle_from_mask(surf->format->Rmask, false);
      swz[1] = compute_component_swizzle_from_mask(surf->format->Gmask, false);
      swz[2] = compute_component_swizzle_from_mask(surf->format->Bmask, false);
      swz[3] = compute_component_swizzle_from_mask(surf->format->Amask, true);
   } else {
      guess_swizzle_from_format(*format, swz);
   }
}

bool
vkdf_load_image_from_file(VkdfContext *ctx,
                          VkCommandPool pool,
                          const char *path,
                          VkdfImage *image,
                          VkImageUsageFlags usage,
                          bool is_srgb,
                          bool gen_mipmaps,
                          SDL_Surface **out_surf)
{
   memset(image, 0, sizeof(VkdfImage));

   // Load image data from file and put pixel data in a GPU buffer
   SDL_Surface *surf = IMG_Load(path);
   if (!surf) {
      vkdf_error("image: failed to load '%s'", path);
      return false;
   }

   VkFormat format;
   uint32_t bpp;
   VkComponentSwizzle swz[4];
   compute_image_parameters_from_surface(surf, &format, &bpp, &is_srgb, swz);

   // Create and initialize image
   create_image_from_data(ctx, pool,
                          image, surf->w, surf->h, 1, false,
                          format, bpp, swz,
                          usage, gen_mipmaps,
                          (const void **) &surf->pixels);

   if (out_surf)
      *out_surf = surf;
   else
      SDL_FreeSurface(surf);

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
                            VkImageUsageFlags usage,
                            VkdfImage *image)
{
   memset(image, 0, sizeof(VkdfImage));

   uint32_t bpp = get_bpp_for_format(format);

   VkComponentSwizzle swz[4];
   guess_swizzle_from_format(format, swz);

   create_image_from_data(ctx, pool,
                          image, width, height, 1, false,
                          format, bpp, swz,
                          usage,
                          gen_mipmaps,
                          &pixel_data);
}

bool
vkdf_load_cube_image_from_files(VkdfContext *ctx,
                                VkCommandPool pool,
                                const char *path[6],
                                VkdfImage *image,
                                VkImageUsageFlags usage,
                                bool is_srgb)
{
   memset(image, 0, sizeof(VkdfImage));

   SDL_Surface *surfs[6];
   for (uint32_t i = 0; i < 6; i++) {
      // Load image data from file and put pixel data in a GPU buffer
      surfs[i] = IMG_Load(path[i]);
      if (!surfs[i]) {
         vkdf_error("image: failed to load '%s'", path[i]);
         return false;
      }
   }

   // We assume that all images have a matching format
   SDL_Surface *surf = surfs[0];

   VkFormat format;
   uint32_t bpp;
   VkComponentSwizzle swz[4];
   compute_image_parameters_from_surface(surf, &format, &bpp, &is_srgb, swz);

   // Create and initialize image
   const void *pixels[6] = {
      surfs[0]->pixels,
      surfs[1]->pixels,
      surfs[2]->pixels,
      surfs[3]->pixels,
      surfs[4]->pixels,
      surfs[5]->pixels,
   };
   create_image_from_data(ctx, pool,
                          image, surf->w, surf->h, 6, true,
                          format, bpp, swz,
                          usage, false, pixels);

   return true;
}
