#ifndef MOCK_RTTHREAD_H
#define MOCK_RTTHREAD_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ALIGN(n) __attribute__((aligned(n)))
#define RT_ALIGN_SIZE 4
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
typedef uint64_t rt_uint64_t;
typedef intptr_t rt_base_t;
typedef int rt_err_t;
typedef int rt_bool_t;
typedef size_t rt_size_t;
typedef int32_t rt_off_t;
typedef uint32_t rt_tick_t;
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_NULL NULL
#define RT_EOK 0
#define RT_EIO 8
#define RT_EINVAL 10
#define RT_EBUSY 7
#define RT_ENOSYS 6
#define RT_ETIMEOUT 2
#define RT_WAITING_FOREVER (-1)
#define RT_IPC_FLAG_PRIO 1
#define INIT_COMPONENT_EXPORT(fn)
struct rt_mutex {int held;};
struct rt_semaphore {int count;};
struct rt_thread {int unused;};
rt_err_t rt_mutex_init(struct rt_mutex*, const char*, uint8_t);
rt_err_t rt_mutex_take(struct rt_mutex*, int);
rt_err_t rt_mutex_release(struct rt_mutex*);
rt_err_t rt_sem_init(struct rt_semaphore*,const char*,unsigned,uint8_t);
rt_err_t rt_sem_take(struct rt_semaphore*, int);
rt_err_t rt_sem_release(struct rt_semaphore*);
rt_tick_t rt_tick_get(void);
rt_tick_t rt_tick_from_millisecond(int32_t ms);
rt_err_t rt_thread_init(struct rt_thread*,const char*,void(*)(void*),void*,void*,unsigned,uint8_t,unsigned);
rt_err_t rt_thread_startup(struct rt_thread*);
void rt_thread_mdelay(int32_t ms);
#endif
