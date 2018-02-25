#ifndef __VKDF_THREAD_POOL_H__
#define __VKDF_THREAD_POOL_H__

#include "vkdf-deps.hpp"

#include <pthread.h>

struct _VkdfThreadPool;
struct _VkdfThreadJob;

typedef void (*VkdfThreadJobFunction)(uint32_t, void *);

typedef struct {
   pthread_mutex_t mutex;
   pthread_cond_t cond;
   uint32_t value;
} VkdfBinarySemaphore;

typedef struct {
   VkdfThreadJobFunction function;
   void *arg;
} VkdfThreadJob;

typedef struct {
   pthread_mutex_t mutex;
   GList *jobs;
   VkdfBinarySemaphore has_jobs;
   uint32_t len;
} VkdfThreadQueue;

typedef struct {
   uint32_t id;
   pthread_t pthread;
   struct _VkdfThreadPool *pool;
} VkdfThread;

typedef struct {
   volatile bool active;
   VkdfThread *threads;
   uint32_t num_threads;
   uint32_t num_alive;
   uint32_t num_working;
   pthread_mutex_t thread_count_mutex;
   pthread_cond_t all_idle;
   VkdfThreadQueue queue;
} VkdfThreadPool;

VkdfThreadPool *
vkdf_thread_pool_new(uint32_t num_threads);

inline uint32_t
vkdf_thread_pool_get_num_threads(VkdfThreadPool *pool)
{
   return pool->num_threads;
}

void
vkdf_thread_pool_add_job(VkdfThreadPool *pool,
                         VkdfThreadJobFunction func, void *arg);

void
vkdf_thread_pool_wait(VkdfThreadPool *pool);

void
vkdf_thread_pool_free(VkdfThreadPool *pool);

#endif
