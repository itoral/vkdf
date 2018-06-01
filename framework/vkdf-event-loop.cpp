#include "vkdf-event-loop.hpp"
#include "vkdf-init-priv.hpp"
#include "vkdf-util.hpp"
#include "vkdf-error.hpp"
#include "vkdf-cmd-buffer.hpp"
#include "vkdf-platform.hpp"

#define VKDF_LOG_FPS_ENABLE 1

static uint64_t _frames = 0;
static double _frame_start_time = 0.0;
static double _last_frame_time = 0.0;

#if VKDF_LOG_FPS_ENABLE
static double _total_time = 0.0;
static double _frame_min_time = 1000000000.0;
static double _frame_max_time = 0.0;
#endif

static inline void
frame_start(VkdfContext *ctx)
{
   _frame_start_time = vkdf_platform_get_time();
}

static inline void
frame_end(VkdfContext *ctx)
{
   _frames++;

   /* Compute frame time */
   double frame_end_time = vkdf_platform_get_time();
   _last_frame_time = frame_end_time - _frame_start_time;

   /* If we have a FPS target set and we are early for it we wait until
    * our frame budget is over before rendering the next frame.
    */
   if (ctx->fps_target > 0.0) {
      double remaining = MAX2(0, ctx->frame_time_budget - _last_frame_time);
      if (remaining > 0.0) {
         long  nanos = trunc(remaining * 1000000000.0);
         struct timespec req = { 0, nanos };
         nanosleep(&req, NULL);
         _last_frame_time = ctx->frame_time_budget;
      }
   }

#if VKDF_LOG_FPS_ENABLE
   _total_time += _last_frame_time;

   if (_last_frame_time > _frame_max_time)
      _frame_max_time = _last_frame_time;
   else if (_last_frame_time < _frame_min_time)
      _frame_min_time = _last_frame_time;

   if (_frames == 60) {
      vkdf_info("fps: %.2f, avg: %.4f min=%.4f, max = %.4f\n",
                _frames / _total_time, _total_time / _frames,
                _frame_min_time, _frame_max_time);
      _frames = 0;
      _total_time = 0.0;
      _frame_min_time = 1000000;
      _frame_max_time = 0.0;
   }
#endif
}

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

   vkdf_platform_get_window_size(&ctx->platform, &width, &height);

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
                    vkdf_event_loop_update_func update_func,
                    vkdf_event_loop_render_func render_func,
                    void *data)
{
   do {
      frame_start(ctx);

      update_func(ctx, data);

      acquire_next_image(ctx);
      render_func(ctx, data);

      present_image(ctx);

      vkdf_platform_poll_events(&ctx->platform);

      frame_end(ctx);
   } while (!vkdf_platform_should_quit(&ctx->platform));

   vkDeviceWaitIdle(ctx->device);
}

/**
 * Applications doing offscreen rendering will call this function right after
 * they are done rendering to the offscreen image in their render_func() hook.
 *
 * The function receives the list of copy command buffers for all swapchain
 * images and selects the correct one to use based on the current swap chain
 * index.
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
