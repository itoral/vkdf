#include "vkdf.hpp"

static void
binary_sem_set(VkdfBinarySemaphore *sem, uint32_t value)
{
   assert(value == 0 || value == 1);
   pthread_mutex_init(&sem->mutex, NULL);
   pthread_cond_init(&sem->cond, NULL);
   sem->value = value;
}

static void
binary_sem_wait(VkdfBinarySemaphore *sem)
{
   pthread_mutex_lock(&sem->mutex);
   while (sem->value != 1)
      pthread_cond_wait(&sem->cond, &sem->mutex);
   sem->value = 0;
   pthread_mutex_unlock(&sem->mutex);
}

static void
binary_sem_post(VkdfBinarySemaphore *sem)
{
   pthread_mutex_lock(&sem->mutex);
   sem->value = 1;
   pthread_cond_signal(&sem->cond);
   pthread_mutex_unlock(&sem->mutex);
}

static void
binary_sem_post_all(VkdfBinarySemaphore *sem)
{
   pthread_mutex_lock(&sem->mutex);
   sem->value = 1;
   pthread_cond_broadcast(&sem->cond);
   pthread_mutex_unlock(&sem->mutex);
}

static VkdfThreadJob *
queue_pull(VkdfThreadQueue *queue)
{
   VkdfThreadJob *job = NULL;

   pthread_mutex_lock(&queue->mutex);

   if (queue->jobs != NULL) {
      GList *head = queue->jobs;
      job = (VkdfThreadJob *) head->data;
      queue->jobs = g_list_delete_link(queue->jobs, head);
      if (queue->jobs != NULL)
         binary_sem_post(&queue->has_jobs);
   }

   pthread_mutex_unlock(&queue->mutex);

   return job;
}

static void
queue_push(VkdfThreadQueue *queue, VkdfThreadJob *job)
{
   pthread_mutex_lock(&queue->mutex);
   queue->jobs = g_list_prepend(queue->jobs, job);
   binary_sem_post(&queue->has_jobs);
   pthread_mutex_unlock(&queue->mutex);
}

static void *
thread_run(void *data)
{
   VkdfThread *thread = (VkdfThread *) data;

   VkdfThreadPool *pool = (VkdfThreadPool *) thread->pool;

   pthread_mutex_lock(&pool->thread_count_mutex);
   pool->num_alive++;
   pthread_mutex_unlock(&pool->thread_count_mutex);

   while (pool->active) {
      binary_sem_wait(&pool->queue.has_jobs);

      if (!pool->active)
         continue;

      pthread_mutex_lock(&pool->thread_count_mutex);
      pool->num_working++;
      pthread_mutex_unlock(&pool->thread_count_mutex);

      VkdfThreadJob *job = queue_pull(&pool->queue);
      if (job) {
         job->function(job->arg);
         free(job);
      }

      pthread_mutex_lock(&pool->thread_count_mutex);
      pool->num_working--;
      if (pool->num_working == 0)
         pthread_cond_signal(&pool->all_idle);
      pthread_mutex_unlock(&pool->thread_count_mutex);
   }

   pthread_mutex_lock(&pool->thread_count_mutex);
   pool->num_alive--;
   pthread_mutex_unlock(&pool->thread_count_mutex);

   return NULL;
}

static void
queue_init(VkdfThreadQueue *queue)
{
   memset(queue, 0, sizeof(VkdfThreadQueue));
   pthread_mutex_init(&queue->mutex, NULL);
   binary_sem_set(&queue->has_jobs, 0);
}

static void
queue_free(VkdfThreadQueue *queue)
{
   while (queue->jobs) {
      VkdfThreadJob *job = queue_pull(queue);
      g_free(job);
   }

   binary_sem_set(&queue->has_jobs, 0);
}

static void
thread_init(VkdfThreadPool *pool, VkdfThread *thread, uint32_t id)
{
   thread->pool = (struct _VkdfThreadPool *) pool;
   thread->id = id;

   pthread_create(&thread->pthread, NULL, thread_run, thread);
   pthread_detach(thread->pthread);
}

static void
threads_init(VkdfThreadPool *pool, uint32_t num_threads)
{
   pool->num_threads = num_threads;
   pool->threads = g_new0(VkdfThread, pool->num_threads);
   for (uint32_t i = 0; i < num_threads; i++)
      thread_init(pool, &pool->threads[i], i);
   while (pool->num_alive != pool->num_threads) {}
}

VkdfThreadPool *
vkdf_thread_pool_new(uint32_t num_threads)
{
   VkdfThreadPool *pool = g_new0(VkdfThreadPool, 1);

   pool->active = true;

   queue_init(&pool->queue);
   threads_init(pool, num_threads);

   return pool;
}

void
vkdf_thread_pool_add_job(VkdfThreadPool *pool,
                         VkdfThreadJobFunction func,
                         void *arg)
{
   VkdfThreadJob *job = g_new(VkdfThreadJob, 1);
   job->function = func;
   job->arg = arg;
   queue_push(&pool->queue, job);
}

void
vkdf_thread_pool_wait(VkdfThreadPool *pool)
{
   pthread_mutex_lock(&pool->thread_count_mutex);
   while (pool->queue.jobs || pool->num_working)
      pthread_cond_wait(&pool->all_idle, &pool->thread_count_mutex);
   pthread_mutex_unlock(&pool->thread_count_mutex);
}

void
vkdf_thread_pool_free(VkdfThreadPool *pool)
{
   pool->active = false;

   const struct timespec wait_time = { 0, 1000};
   while (pool->num_alive) {
      binary_sem_post_all(&pool->queue.has_jobs);
      nanosleep(&wait_time, NULL);
   }

   queue_free(&pool->queue);

   g_free(pool->threads);

   g_free(pool);
}
