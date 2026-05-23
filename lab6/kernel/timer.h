#ifndef TIMER_H
#define TIMER_H

#define TIMEBASE_FREQ 24000000UL

typedef void (*timer_callback_t)(void *);

void timer_init(void);
void timer_enable_interrupt(void);
int timer_add(timer_callback_t callback, void *arg, int sec);
void timer_handle_interrupt(void);
unsigned long timer_now_sec(void);
int timer_usleep(unsigned int usec);

#endif

