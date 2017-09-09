#include "vkdf.hpp"
#include "vkdf-init-priv.hpp"

#if VKDF_LOG_FPS_ENABLE
static uint64_t _frames = 0;
static double _frame_start_time = 0.0;
static double _last_frame_time = 0.0;
static double _total_time = 0.0;

static inline void
frame_start()
{
   _frame_start_time = glfwGetTime();
}

static inline void
frame_end()
{
   double frame_end_time = glfwGetTime();
   _last_frame_time = frame_end_time - _frame_start_time;
   _total_time += _last_frame_time;

   _frames++;
   if (_frames == 60) {
      vkdf_info("FPS: %.2f\n", _frames / _total_time);
      _frames = 0;
      _total_time = 0.0;
   }
}
#endif

void
vkdf_rebuild_swap_chain(VkdfContext *ctx)
{
   int32_t width, height;

   if (!ctx->before_rebuild_swap_chain_cb ||
       !ctx->before_rebuild_swap_chain_cb) {
      vkdf_error("Swap chain needs to be resized but no swap chain "
                 "rebuild callbacks have been provided.");
      return;
   }

   glfwGetWindowSize(ctx->window, &width, &height);

   vkDeviceWaitIdle(ctx->device);

   ctx->before_rebuild_swap_chain_cb(ctx, ctx->rebuild_swap_chain_cb_data);

   ctx->width = width;
   ctx->height = height;

   _init_swap_chain(ctx);

   ctx->after_rebuild_swap_chain_cb(ctx, ctx->rebuild_swap_chain_cb_data);
}

static void
acquire_next_image(VkdfContext *ctx)
{
   VkResult res;
   bool image_acquired = false;

   // We only get an updated swap_chain_index after calling
   // vkAcquireNextImageKHR, but we need it before the call to select a valid
   // semaphore, pre-increment it here for that purpose. We initialized
   // swap_chain_index to swap_chain_length - 1, so the first time this
   // is ever called, it will give us index 0.
   uint32_t sem_index = (ctx->swap_chain_index + 1) % ctx->swap_chain_length;
   do {
      res = vkAcquireNextImageKHR(ctx->device,
                                  ctx->swap_chain,
                                  UINT64_MAX,
                                  ctx->acquired_sem[sem_index],
                                  NULL,
                                  &ctx->swap_chain_index);

      if (res != VK_SUCCESS &&
          res != VK_ERROR_OUT_OF_DATE_KHR &&
          res != VK_SUBOPTIMAL_KHR) {
         vkdf_fatal("Failed to acquire image from swap chain");
      } else if (res == VK_ERROR_OUT_OF_DATE_KHR) {
         vkdf_rebuild_swap_chain(ctx);
         sem_index = 0;
      } else {
         image_acquired = true;
      }
   } while (!image_acquired);
}

static void
present_image(VkdfContext *ctx)
{
   VkPresentInfoKHR present;
   present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
   present.pNext = NULL;
   present.swapchainCount = 1;
   present.pSwapchains = &ctx->swap_chain;
   present.pImageIndices = &ctx->swap_chain_index;
   present.pWaitSemaphores = &ctx->draw_sem[ctx->swap_chain_index];
   present.waitSemaphoreCount = 1;
   present.pResults = NULL;

   VK_CHECK(vkQueuePresentKHR(ctx->pst_queue, &present));
}

void
vkdf_event_loop_run(VkdfContext *ctx,
                    bool offscreen_rendering,
                    vkdf_event_loop_update_func update_func,
                    vkdf_event_loop_render_func render_func,
                    void *data)
{
   do {
#if VKDF_LOG_FPS_ENABLE
      frame_start();
#endif

      update_func(ctx, data);

      if (offscreen_rendering) {
         /**
          * For offscreen rendering, the application is responsible for
          * triggering swapchain acquisition by calling vkdf_copy_to_swapchain()
          * at the end of render_func(), this way we can acquire right before
          * presenting.
          */
         render_func(ctx, data);
      } else {
         acquire_next_image(ctx);
         render_func(ctx, data);
      }

      present_image(ctx);

      glfwPollEvents();

#if VKDF_LOG_FPS_ENABLE
      frame_end();
#endif
   } while (glfwGetKey(ctx->window, GLFW_KEY_ESCAPE) != GLFW_PRESS &&
            glfwWindowShouldClose(ctx->window) == 0);

   vkDeviceWaitIdle(ctx->device);
}

/**
 * For applications doing offscreen rendering, we want to postpone swapchain
 * acquisition until we have finished rendering to the offscreen buffer. Such
 * applications should call this function right after they are done rendering
 * to the offscreen image in their render_func() hook.
 *
 * The function receives the list of copy command buffers for all swapchain
 * images and selects the correct one to use after aquiring the next image
 * from the swapchain.
 *
 * The fence is used so that clients can know when presentation has been
 * completed.
 */
void
vkdf_copy_to_swapchain(VkdfContext *ctx,
                       VkCommandBuffer *copy_cmd_bufs,
                       VkPipelineStageFlags wait_stage,
                       VkSemaphore wait_sem,
                       VkFence fence)
{
   acquire_next_image(ctx);

   VkSemaphore wait_sems[2] = {
      wait_sem,
      ctx->acquired_sem[ctx->swap_chain_index]
   };

   VkPipelineStageFlags wait_stages[2] = {
      wait_stage,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
   };

   vkdf_command_buffer_execute_with_fence(
      ctx,
      copy_cmd_bufs[ctx->swap_chain_index],
      wait_stages,
      2, wait_sems,
      1, &ctx->draw_sem[ctx->swap_chain_index],
      fence);
}
