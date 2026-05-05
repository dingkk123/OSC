#include "timer.h"
#include "uart.h"
#include "interrupt.h"

#define SBI_EXT_SET_TIMER 0x0

#define SIE_STIE (1UL << 5) //Supervisor Timer Interrupt Enable bit in SIE register, see riscv-privileged-1.10 section 3.1.2


#define MAX_TIMERS 16
#define TIMER_DISABLED_DELTA (TIMEBASE_FREQ * 3600UL)
//#define TIMEBASE_FREQ 10000000UL //time counterwould increase 10,000,000 every second 

struct sbiret {
    long error;
    long value;
};

struct timer_event {
    int active;
    unsigned long expire_time;
    timer_callback_t callback;
    void *arg;
};

static struct timer_event timers[MAX_TIMERS];
static unsigned long current_time_sec = 0;

static struct sbiret sbi_ecall(int ext,
                               int fid,
                               unsigned long arg0,
                               unsigned long arg1,
                               unsigned long arg2,
                               unsigned long arg3,
                               unsigned long arg4,
                               unsigned long arg5) {
    struct sbiret ret;

    register unsigned long a0 asm("a0") = arg0;
    register unsigned long a1 asm("a1") = arg1;
    register unsigned long a2 asm("a2") = arg2;
    register unsigned long a3 asm("a3") = arg3;
    register unsigned long a4 asm("a4") = arg4;
    register unsigned long a5 asm("a5") = arg5;
    register unsigned long a6 asm("a6") = fid;
    register unsigned long a7 asm("a7") = ext;

    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");

    ret.error = a0;
    ret.value = a1;
    return ret;
}

//to get the current increasing click
static unsigned long read_time(void) {
    unsigned long t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

static void sbi_set_timer(unsigned long stime_value) {
    sbi_ecall(SBI_EXT_SET_TIMER, 0, stime_value, 0, 0, 0, 0, 0);
}

static void disable_timer_interrupt_pending(void) {
    sbi_set_timer(read_time() + TIMER_DISABLED_DELTA);
}


static int has_active_timer(void) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].active)
            return 1;
    }

    return 0;
}

static unsigned long get_next_expire_time(void) {
    unsigned long next = 0;

    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!timers[i].active) //如果這個timer slot是inactive的，就跳過
            continue;

        if (next == 0 || timers[i].expire_time < next) //找到最早會expire的active timer，把他的expire time當作next
            next = timers[i].expire_time;
    }

    return next;
}

static void program_next_timer(void) {
    unsigned long next = get_next_expire_time(); //找到下一個要expire的timer的expire time
    unsigned long now_tick;
    unsigned long target_tick;

    if (next == 0)
        return;

    now_tick = read_time();

    if (next <= current_time_sec) //如果下一個要expire的timer的expire time已經過了，那就把target_tick設為現在的tick加一，讓timer interrupt可以盡快發生
        target_tick = now_tick + 1;
    else
        target_tick = now_tick + (next - current_time_sec) * TIMEBASE_FREQ;

    sbi_set_timer(target_tick); //set timer to let timer interrupt happen when tick reaches target_tick
}

//to initialize the timer, set all timers to inactive, and set the current_time_sec to the current time in second
void timer_init(void) {
    current_time_sec = read_time() / TIMEBASE_FREQ;

    for (int i = 0; i < MAX_TIMERS; i++) {
        timers[i].active = 0;
        timers[i].callback = 0;
        timers[i].arg = 0;
        timers[i].expire_time = 0;
    }
}

void timer_enable_interrupt(void) {
    asm volatile("csrs sie, %0" :: "r"(SIE_STIE)); //csrs, csr set
    asm volatile("csrs sstatus, %0" :: "r"(SSTATUS_SIE));//csr set global interrupt enable (可保護共享變數的臨界區)
}

//to add a timer, find an inactive timer slot, set it to active 
int timer_add(timer_callback_t callback, void *arg, int sec) {
    int slot = -1;
    unsigned long flags;

    if (sec < 0)//防呆秒數不會是負的
        return -1;

    flags = irq_save(); //進入共享變數前先把interupt關掉

    for (int i = 0; i < MAX_TIMERS; i++) { //找一個inactive的timer slot，來用他
        if (!timers[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) { //代表大家都是active，沒得用
        irq_restore(flags);
        return -1;
    }

    current_time_sec = read_time() / TIMEBASE_FREQ;

    timers[slot].active = 1;
    timers[slot].expire_time = current_time_sec + (unsigned long)sec;
    timers[slot].callback = callback; //callback = boot_time_callback or timeout_callback
    timers[slot].arg = arg;

    program_next_timer(); //新增一個之後 要檢查有沒有更早會expire的timer，或是這個新的變最早過期的，那要去把sbi設定的interrupt時間點改一下

    irq_restore(flags);
    return 0;
}

//to handle the timer interrupt, check which timer expires, call the callback function, and set the next timer
void timer_handle_interrupt(void) {
    current_time_sec = read_time() / TIMEBASE_FREQ;// to get the cur time (in second) when timer interrupt happens

    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].active && timers[i].expire_time <= current_time_sec) {
            timer_callback_t callback = timers[i].callback;
            void *arg = timers[i].arg;

            timers[i].active = 0;
            timers[i].callback = 0;
            timers[i].arg = 0;

            if (callback) //如果callback不是null，就call他的function，傳入arg
                callback(arg);
        }
    }

    if (has_active_timer()) {
        program_next_timer(); //要去檢查有沒有下一個timer，如果有就去設定讓timer interrupt在下一個timer expire的時候發生
    } else {
        disable_timer_interrupt_pending();
    }

}

unsigned long timer_now_sec(void) {
    return read_time() / TIMEBASE_FREQ;
}
