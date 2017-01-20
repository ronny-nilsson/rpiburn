
#ifndef MISC_H
#define MISC_H

#include <stdint.h>
#include <time.h>
#include <sys/time.h>


//-------------------------------------------------------------
#ifndef likely
#define likely(x)       __builtin_expect(!!(x), 1)								// gcc branch extensions	
#define unlikely(x)     __builtin_expect(!!(x), 0)
#endif


//-------------------------------------------------------------
void timer_set(struct timespec* const t, const int32_t ms_forw);
int32_t _timer_remaining(const struct timespec* const t);
int32_t timer_remaining(const struct timespec* const t);
short timer_timeout(const struct timespec* const t);
void timer_cancel(struct timespec* const t);
int64_t diffntime(struct timespec *t1, struct timespec *t2);
int update_current_time(void);
void maxSleep(const int ms);

#endif // MISC_H

