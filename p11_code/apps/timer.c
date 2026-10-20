#include "includes.h"
#include "timer.h"
#include "gp_timer_api.h"
#include "lp_timer_api.h"
#include "app_config.h"
#include "usr_timer.h"

volatile u32 cur_time_tick = 0;
#if TCFG_VCHR02_DEV_ENABLE
volatile u32 vchr02_cur_time_tick = 0;//set 20k
#endif
/*  */
/* #pragma bss_seg(".timer_bss") */
/* #pragma const_seg(".timer_const") */
/* #pragma code_seg(".timer_code") */
/*  */

u32 get_cur_sys_tick(void)
{
    return cur_time_tick;
}


static void timer_isr_fun(void *priv)
{
    struct loop_detect_handler *detect;
    list_for_each_loop_detect(detect) {
        if ((cur_time_tick %  detect->time) == 0) {
            if (detect->fun) {
                detect->fun(detect->priv);
            }
        }
    }
    cur_time_tick += 40;
#if TCFG_VCHR02_DEV_ENABLE
    vchr02_cur_time_tick++;
#endif
}

void timer_tick_init()
{
#if 0
    lp_timer_init(P11_LPTMR3, 40 * 1000);
    /* gp_timer_init(P11_GPTMR1, 10950 * 40); //10ms */
    /* gp_timer_register_irq(P11_GPTMR1, timer0_isr_fun); */
    lp_timer_register_irq(P11_LPTMR3, timer_isr_fun);
#else

    usr_timer_add(NULL, timer_isr_fun, 40, 0);

#endif
}

