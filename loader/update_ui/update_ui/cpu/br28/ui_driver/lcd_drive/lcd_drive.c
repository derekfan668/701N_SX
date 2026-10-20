#include "app_config.h"


#include "ui/lcd_spi/lcd_drive.h"
#include "includes.h"
#include "ui/ui_api.h"
#include "generic/jiffies.h"
//#include "system/timer.h"
//#include "asm/spi.h"
#include "clock_cfg.h"
//#include "asm/mcpwm.h"
//#include "ui/res_config.h"
#include "asm/imd.h"
#include "asm/imb.h"
//#include "ui/ui_sys_param.h"


#define lcd_debug printf


static u8  backlight_status = 0;
static u8  lcd_sleep_in     = 0;
static volatile u8 is_lcd_busy = 0;
static struct lcd_platform_data *lcd_dat = NULL;
//struct pwm_platform_data lcd_pwm_p_data;

#define __lcd ((struct lcd_drive *)&lcd_drive)
#define __this ((struct imd_param *)lcd_drive.param)

extern __attribute__((weak)) unsigned RAM1_SIZE ;
extern void *malloc_vlt(size_t size);
extern void free_vlt(void *pv);
void lcd_drv_cmd_list(u8 *cmd_list, int cmd_cnt);

void *lcd_qspi_st77903_get_info();
int lcd_qspi_st77903_mode();
void lcd_spi_draw_st77903(u8 *pixel, int lcd_w, int lcd_h, int lcd_stride, int back_porch, int front_porch);
void lcd_qspi_st77903_refresh_mode_set(int mode);
int lcd_qspi_st77903_refresh_mode_get();

//void dma_memcpy(void *dest, void *src, u32 len);
//void dma_memcpy_wait_idle(void);

#define dma_memcpy memcpy
#define dma_memcpy_wait_idle()

#if defined(TCFG_PSRAM_DEV_ENABLE) && TCFG_PSRAM_DEV_ENABLE
#define PHY_ADDR(addr) psram_cache2nocache_addr(addr)
#else
#define PHY_ADDR(addr) addr
#endif
__attribute__((noinline)) u32 check_ram1_size(void)
{
	//return &RAM1_SIZE;
	return 0;
}

// EN 控制
void lcd_en_ctrl(u8 val)
{
	if (lcd_dat->pin_en == NO_CONFIG_PORT) {
		return;
	}
	gpio_set_die(lcd_dat->pin_en, 1);
	gpio_direction_output(lcd_dat->pin_en, val);
}

// BL 控制
void lcd_bl_ctrl(u8 val)
{
	if (lcd_dat->pin_bl == NO_CONFIG_PORT) {
		return;
	}
	gpio_set_die(lcd_dat->pin_bl, 1);
	gpio_direction_output(lcd_dat->pin_bl, !!val);
}



// CS 控制
static void spi_cs_ctrl(u8 val)
{
	if (lcd_dat->pin_cs == NO_CONFIG_PORT) {
		return;
	}
	gpio_set_die(lcd_dat->pin_cs, 1);
	gpio_direction_output(lcd_dat->pin_cs, val);
}

// DC 控制
static void spi_dc_ctrl(u8 val)
{
	if (lcd_dat->pin_dc == NO_CONFIG_PORT) {
		return;
	}
	gpio_set_die(lcd_dat->pin_dc, 1);
	gpio_direction_output(lcd_dat->pin_dc, val);
}

// TE 控制
static int spi_te_stat()
{
	if (lcd_dat->pin_te == NO_CONFIG_PORT) {
		return -1;
	}
	gpio_set_pull_up(lcd_dat->pin_te, 1);
	gpio_set_pull_down(lcd_dat->pin_te, 0);
	gpio_set_die(lcd_dat->pin_te, 1);
	gpio_direction_input(lcd_dat->pin_te);

	return gpio_read(lcd_dat->pin_te);
}

/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD DEVICE RESET
 *
 * Description: LCD 设备复位
 *
 * Arguments  : none
 *
 * Returns    : none
 *
 * Notes      : 1、判断是否在屏驱有重新定义LCD复位函数，
 *                  是使用屏驱上定义的LCD复位函数，
 *                  否使用GPIO控制板级配置的LCD复位IO
 *********************************************************************************************************
 */

static void lcd_reset()
{
	printf("__lcd : 0x%x, __lcd->reset : 0x%x\n", __lcd, __lcd->reset);

	if (__lcd->reset) {
		__lcd->reset();
	} else {
		gpio_set_die(lcd_dat->pin_reset, 1);
		gpio_direction_output(lcd_dat->pin_reset, 1);
		//os_time_dly(10);
		delay(1000);
		gpio_direction_output(lcd_dat->pin_reset, 0);
		//os_time_dly(10);
		delay(1000);
		gpio_direction_output(lcd_dat->pin_reset, 1);
		//os_time_dly(10);
		delay(1000);
	}
}



/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD BACKLIGHT CONTROL
 *
 * Description: LCD 背光控制
 *
 * Arguments  : on LCD 背光开关标志，0为关，其它值为开
 *
 * Returns    : none
 *
 * Notes      : 1、判断是否在屏驱有重新定义背光控制函数，
 *                  是使用屏驱上定义的背光控制函数，
 *                  否使用GPIO控制板级配置的背光IO
 *
 *              2、配置背光状态标志，打开为true，关闭为false
 *********************************************************************************************************
 */

static void lcd_mcpwm_init()
{
#if (TCFG_BACKLIGHT_PWM_MODE == 2)
	extern void mcpwm_init(struct pwm_platform_data * arg);
	lcd_pwm_p_data.pwm_aligned_mode = pwm_edge_aligned;         //边沿对齐
	lcd_pwm_p_data.pwm_ch_num = pwm_ch0;                        //通道
	lcd_pwm_p_data.frequency = 10000;                           //Hz
	lcd_pwm_p_data.duty = 10000;                                //占空比
	lcd_pwm_p_data.h_pin = lcd_dat->pin_bl;                     //任意引脚
	lcd_pwm_p_data.l_pin = -1;                                  //任意引脚,不需要就填-1
	lcd_pwm_p_data.complementary_en = 1;                        //两个引脚的波形, 0: 同步,  1: 互补，互补波形的占空比体现在H引脚上
	mcpwm_init(&lcd_pwm_p_data);
#endif
}

int lcd_backlight_ctrl_base(u8 percent)
{
	/* r_printf("bl:%d \n", percent); */
	if (__lcd->backlight_ctrl) {
		__lcd->backlight_ctrl(percent);
	} else if (lcd_dat->pin_bl != -1) {
#if (TCFG_BACKLIGHT_PWM_MODE == 0)
		if (percent > 0) {
			gpio_direction_output(lcd_dat->pin_bl, 1);
		} else {
			gpio_direction_output(lcd_dat->pin_bl, 0);
		}
#elif (TCFG_BACKLIGHT_PWM_MODE == 1)
		//注意：duty不能大于prd，并且prd和duty是非标准非线性的，建议用示波器看着来调
		extern int pwm_led_output_clk(u8 gpio, u8 prd, u8 duty);
		if (percent) {
			u32 light_level = (percent + 9) / 10;
			if (light_level > 10) {
				printf("lcd pwm full\n");
				light_level = 10;
			}
			pwm_led_output_clk(lcd_dat->pin_bl, 10, light_level);
		} else {
			pwm_led_output_clk(lcd_dat->pin_bl, 10, 0);
		}
#elif (TCFG_BACKLIGHT_PWM_MODE == 2)
		if (percent) {
			u32 pwm_duty = percent * 100;
			if (pwm_duty > 10000) {
				pwm_duty = 10000;
				printf("lcd pwm full\n");
			}
			mcpwm_set_duty(lcd_pwm_p_data.pwm_ch_num, pwm_duty);
		} else {
			mcpwm_set_duty(lcd_pwm_p_data.pwm_ch_num, 0);
		}
#endif
	} else {
		return -1;
	}

	return 0;
}

int lcd_drv_backlight_ctrl(u8 percent)
{
	int ret = lcd_backlight_ctrl_base(percent);
	if (ret < 0) {
		backlight_status = 0;
	} else {
		backlight_status = percent;
	}
	return ret;
}

int lcd_drv_power_ctrl(u8 on)
{
	printf("__lcd : 0x%x, lcd->power_ctrl : 0x%x\n", __lcd, __lcd->power_ctrl);
	if (__lcd->power_ctrl) {
		__lcd->power_ctrl(on);
	}
	return 0;
}


struct lcd_platform_data *lcd_get_platform_data()
{
	return lcd_dat;
}

static int find_begin(u8 *begin, u8 *end, int pos)
{
	int i;
	u8 *p = &begin[pos];
	while ((p + 3) < end) {
		if ((p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) == BEGIN_FLAG) {
			return (&p[4] - begin);
		}
		p++;
	}

	return -1;
}


static int find_end(u8 *begin, u8 *end, int pos)
{
	u8 *p = &begin[pos];
	while ((p + 3) < end) {
		if ((p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) == END_FLAG) {
			return (&p[0] - begin);
		}
		p++;
	}

	return -1;
}


#define LCD_INIT_DEBUG  1
void lcd_drv_cmd_list(u8 *cmd_list, int cmd_cnt)
{
	int i;
	int k;
	int cnt;
	u16 *p16;
	u8 *p8;

#if LCD_INIT_DEBUG
	u8 *temp = NULL;
	u16 temp_len = 5 * 64;
	u16 len;
	temp = (u8 *)malloc(temp_len);
	printf("temp : 0x%x\n", temp);
#endif

	for (i = 0; i < cmd_cnt;) {
		p16 = (u16 *)&cmd_list[i];
		int begin = find_begin(cmd_list, &cmd_list[cmd_cnt], i);
		if ((begin != -1)) {
			int end = find_end(cmd_list, &cmd_list[cmd_cnt], begin);
			if (end != -1) {
				//printf("begin : %d, end:%d\n", begin, end);
				p8 = (u8 *)&cmd_list[begin];
				u8 *param = &p8[1];
				u8 addr = p8[0];
				u8 cnt = end - begin - 1;
				if (((p8[0] << 24) | (p8[1] << 16) | (p8[2] << 8) | p8[3]) == REGFLAG_DELAY_FLAG) {
					//os_time_dly(p8[4] / 10);
					delay_2ms((p8[4] + 1) / 2);
					printf("delay %d ms\n", p8[4]);
				}  else {
#if LCD_INIT_DEBUG
					len = sprintf((char *)temp, "send : 0x%02x(%d), ", addr, cnt);
					for (k = 0; k < cnt; k++) {
						len += sprintf((char *)&temp[len], "0x%02x, ", param[k]);
						if (len > (temp_len - 10)) {
							len += sprintf((char *)&temp[len], "...");
							break;
						}
					}
					len += sprintf((char *)&temp[len], "\n");
					printf("cmd:%s", temp);
					delay_2ms(1);
#endif
					lcd_write_cmd(addr, param, cnt);
				}

				i = end + 4;
			}
		}
	}
#if LCD_INIT_DEBUG
	free(temp);
#endif

}


struct lcd_priv {
    //OS_SEM lcd_sem;
    int refresh_mode;
    int lcd_draw_stage;
    int lcd_w;
    int lcd_h;
    int lcd_stride;
    int back_porch;
    int front_porch;
    int back_porch_cnt;
    int front_porch_cnt;
    volatile int draw_status;
    u8 *pixel;
    int h;
    u8 *p;
};

void lcd_wait_te()
{
#if 1
    struct lcd_priv *priv = (struct lcd_priv *)lcd_qspi_st77903_get_info();
    u32 wait_timeout = 0;

    wait_timeout = jiffies + msecs_to_jiffies(500);
    while (priv->draw_status != 1) { //wait low level
        if (time_after(jiffies, wait_timeout)) {
            printf("wait te timeout.\n");
            extern void wdt_clear(void);
            wdt_clear();
            wait_timeout = jiffies + msecs_to_jiffies(500);
            break;
        }
    }

#else
    u32 wait_timeout = 0;
    if (spi_te_stat() == -1) {
        return;
    }

    wait_timeout = jiffies + msecs_to_jiffies(500);
    while (spi_te_stat()) { //wait low level
        if (time_after(jiffies, wait_timeout)) {
            printf("wait te timeout.\n");
            extern void wdt_clear(void);
            wdt_clear();
            wait_timeout = jiffies + msecs_to_jiffies(500);
            break;
        }
    }

    wait_timeout = jiffies + msecs_to_jiffies(500);
    while (!spi_te_stat()) {//wait high level
        if (time_after(jiffies, wait_timeout)) {
            printf("wait te timeout.\n");
            extern void wdt_clear(void);
            wdt_clear();
            wait_timeout = jiffies + msecs_to_jiffies(500);
            break;
        }
    }
#endif
}


static const u8 line_begin_cmd[] ALIGNED(4) = {
    0x11, 0x01, 0x11, 0x10, 0x00, 0x00, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


void lcd_data_copy(u8 type, struct rect *rect, u8 *lcd_buf, int stride, int left, int top, int width, int height, int wait)
{
    int h;
    u8 *p;
    ASSERT(__this);
    ASSERT(__lcd->lcd_buf);

    int bytes_per_pixel = (__this->in_format == FORMAT_RGB888) ? 3 : 2;
    int lcd_stride;

    if ((0 == lcd_qspi_st77903_refresh_mode_get()) || (2 == lcd_qspi_st77903_refresh_mode_get())) {
        lcd_stride = (__this->lcd_width * bytes_per_pixel + 3) / 4 * 4;
    } else {
        lcd_stride = ((__this->lcd_width + 8) * bytes_per_pixel + 3) / 4 * 4;
    }

    if (wait == 1) {
        if ((__lcd->lcd_buf_num == 2) && (type == 2)) {
            dma_memcpy(PHY_ADDR(__lcd->lcd_dbuf[!__lcd->lcd_dbuf_index]), PHY_ADDR(__lcd->lcd_dbuf[__lcd->lcd_dbuf_index]), __lcd->lcd_buf_size / __lcd->lcd_buf_num);
        }
    }

    p = __lcd->lcd_dbuf[!__lcd->lcd_dbuf_index];

    if ((left == 0) && (width == __this->lcd_width)) {
        if ((0 == lcd_qspi_st77903_refresh_mode_get()) || (2 == lcd_qspi_st77903_refresh_mode_get())) {
            dma_memcpy(PHY_ADDR(&p[top * lcd_stride]), lcd_buf, stride * height);
        } else {
            for (h = 0; h < height; h++) {
                dma_memcpy(PHY_ADDR(&p[16 + (top + h)*lcd_stride]), &lcd_buf[h * stride], width * bytes_per_pixel);
            }
        }
    } else {
        for (h = 0; h < height; h++) {
            if ((0 == lcd_qspi_st77903_refresh_mode_get()) || (2 == lcd_qspi_st77903_refresh_mode_get())) {
                dma_memcpy(PHY_ADDR(&p[(top + h)*lcd_stride + left * bytes_per_pixel]), &lcd_buf[h * stride], width * bytes_per_pixel);
            } else {
                dma_memcpy(PHY_ADDR(&p[16 + (top + h)*lcd_stride + left * bytes_per_pixel]), &lcd_buf[h * stride], width * bytes_per_pixel);
            }
        }
    }

    if (wait == 2) {
        if (__lcd->lcd_buf_num == 2) {
            __lcd->lcd_dbuf_index = !__lcd->lcd_dbuf_index;
        }
    }
}

void lcd_data_copy_wait()
{
    dma_memcpy_wait_idle();
}

static void lcd_buf_malloc()
{
    int lcd_w;
    int lcd_h;
    int lcd_stride;
    int h;
    int i;

    if ((0 == lcd_qspi_st77903_refresh_mode_get()) || (2 == lcd_qspi_st77903_refresh_mode_get())) {
        lcd_w = __this->lcd_width;
        lcd_h = __this->lcd_height;
        lcd_stride = (lcd_w * ((__this->in_format == FORMAT_RGB888) ? 3 : 2) + 3) / 4 * 4;
    } else {
        lcd_w = __this->lcd_width + 8;
        lcd_h = __this->lcd_height;
        lcd_stride = (lcd_w * ((__this->in_format == FORMAT_RGB888) ? 3 : 2) + 3) / 4 * 4;
    }

    if (!__lcd->lcd_buf) {
#if defined(TCFG_PSRAM_DEV_ENABLE) && TCFG_PSRAM_DEV_ENABLE
        __lcd->lcd_buf_num = 2;
        __lcd->lcd_buf_size = lcd_stride * lcd_h * __lcd->lcd_buf_num;
        __lcd->lcd_buf = (u8 *)malloc_psram(__lcd->lcd_buf_size);
        __lcd->lcd_dbuf[0] = __lcd->lcd_buf;
        __lcd->lcd_dbuf[1] = &__lcd->lcd_buf[lcd_stride * lcd_h];
        __lcd->lcd_dbuf_index = 0;
        printf("psram lcd_buf:0x%x, nocache buf:0x%x\n", __lcd->lcd_buf, psram_cache2nocache_addr(__lcd->lcd_buf));
#else
        __lcd->lcd_buf_num = 1;
        __lcd->lcd_buf_size = lcd_stride * lcd_h;
        __lcd->lcd_buf = (u8 *)malloc(__lcd->lcd_buf_size);
        __lcd->lcd_dbuf[0] = __lcd->lcd_buf;
        __lcd->lcd_dbuf[1] = __lcd->lcd_buf;
        __lcd->lcd_dbuf_index = 0;
#endif // defined

    }
    ASSERT(__lcd->lcd_buf);

    memset(__lcd->lcd_buf, 0x00, __lcd->lcd_buf_size);
#if defined(TCFG_PSRAM_DEV_ENABLE) && TCFG_PSRAM_DEV_ENABLE
    psram_flush_invaild_cache(__lcd->lcd_buf, __lcd->lcd_buf_size);
#endif // defined


    if (1 == lcd_qspi_st77903_refresh_mode_get()) {
        for (i = 0; i < __lcd->lcd_buf_num; i++) {
            u8 *p = __lcd->lcd_dbuf[i];

            for (h = 0; h < lcd_h; h++) {
                dma_memcpy(PHY_ADDR(&p[h * lcd_stride]), line_begin_cmd, sizeof(line_begin_cmd));
            }

        }
        dma_memcpy_wait_idle();
    }
}

static void lcd_buf_free()
{
    if (__lcd->lcd_buf) {
#if defined(TCFG_PSRAM_DEV_ENABLE) && TCFG_PSRAM_DEV_ENABLE
        free_psram(__lcd->lcd_buf);
#else
        free_vlt(__lcd->lcd_buf);
#endif // defined
        __lcd->lcd_buf = NULL;
        __lcd->lcd_dbuf[0] = NULL;
        __lcd->lcd_dbuf[1] = NULL;
    }
}

#if 0
static void lcd_task(void *arg)
{
    int lcd_w;
    int lcd_h;
    int lcd_stride;
    OS_SEM *lcd_sem = (OS_SEM *)lcd_qspi_st77903_get_info();

    ASSERT(__this);

    if ((0 == lcd_qspi_st77903_refresh_mode_get()) || (2 == lcd_qspi_st77903_refresh_mode_get())) {
        lcd_w = __this->lcd_width;
        lcd_h = __this->lcd_height;
        lcd_stride = (lcd_w * ((__this->in_format == FORMAT_RGB888) ? 3 : 2) + 3) / 4 * 4;
    } else {
        lcd_w = __this->lcd_width + 8;
        lcd_h = __this->lcd_height;
        lcd_stride = (lcd_w * ((__this->in_format == FORMAT_RGB888) ? 3 : 2) + 3) / 4 * 4;
    }

    __lcd->lcd_enter = 1;
    __lcd->lcd_exit = 0;

    os_sem_create(lcd_sem, 1);

    int cnt = 0;

    while (1) {
        os_sem_pend(lcd_sem, 0);

        if (__lcd->lcd_enter) {
            lcd_buf_malloc();
            __lcd->lcd_enter = 0;
        }

        if (__lcd->lcd_exit) {
            lcd_buf_free();
            __lcd->lcd_exit = 0;
            continue;
        }

        if (__lcd->lcd_buf) {
            lcd_spi_draw_st77903(__lcd->lcd_dbuf[__lcd->lcd_dbuf_index], lcd_w, lcd_h, lcd_stride, 18, 18);
        }
    }
}
#endif

#if defined(TCFG_PSRAM_DEV_ENABLE) && TCFG_PSRAM_DEV_ENABLE
void lcd_draw_hori_slide_test()//左右滑动
{
    int lcd_w;
    int lcd_h;
    int lcd_stride;
    u8 bytes_per_pixel;
    u8 *p;
    int w, h;

    lcd_w = __this->lcd_width;
    lcd_h = __this->lcd_height;
    bytes_per_pixel = (__this->in_format == FORMAT_RGB888) ? 3 : 2;
    lcd_stride = (lcd_w * 2 * bytes_per_pixel + 3) / 4 * 4;

    u32 lcd_buf_size = lcd_stride * lcd_h;
    u8 *lcd_buf = (u8 *)malloc_psram(lcd_buf_size);
    ASSERT(lcd_buf);

    for (h = 0; h < lcd_h; h++) {
        for (w = 0; w < lcd_w * 2; w++) {
            if (w < lcd_w) {
                lcd_buf[h * lcd_stride + 2 * w] = 0xf8;
                lcd_buf[h * lcd_stride + 2 * w + 1] = 0x00;
            } else {
                lcd_buf[h * lcd_stride + 2 * w] = 0x00;
                lcd_buf[h * lcd_stride + 2 * w + 1] = 0x1f;
            }
        }
    }
    psram_flush_invaild_cache(lcd_buf, lcd_buf_size);

    //向左滑屏
    for (w = 0; w < lcd_w; w++) {
        lcd_draw_area_stride(lcd_buf, 0, 0, lcd_w, lcd_h, lcd_stride, w, 0, true);
        wdt_clear();
    }
    //向右滑屏
    for (w = lcd_w - 1; w >= 0; w--) {
        lcd_draw_area_stride(lcd_buf, 0, 0, lcd_w, lcd_h, lcd_stride, w, 0, true);
        wdt_clear();
    }

    lcd_wait();//dma非阻塞方式推屏, 退出需要待屏幕推完

    free_psram(lcd_buf);
}


void lcd_draw_vert_slide_test()//上下滑动
{
    int lcd_w;
    int lcd_h;
    int lcd_stride;
    u8 bytes_per_pixel;
    u8 *p;
    int w, h;

    lcd_w = __this->lcd_width;
    lcd_h = __this->lcd_height;
    bytes_per_pixel = (__this->in_format == FORMAT_RGB888) ? 3 : 2;
    lcd_stride = (lcd_w * bytes_per_pixel + 3) / 4 * 4;

    u32 lcd_buf_size = lcd_stride * lcd_h * 2;
    u8 *lcd_buf = (u8 *)malloc_psram(lcd_buf_size);
    ASSERT(lcd_buf);

    for (h = 0; h < lcd_h * 2; h++) {
        for (w = 0; w < lcd_w; w++) {
            if (h < lcd_h) {
                lcd_buf[h * lcd_stride + 2 * w] = 0xf8;
                lcd_buf[h * lcd_stride + 2 * w + 1] = 0x00;
            } else {
                lcd_buf[h * lcd_stride + 2 * w] = 0x00;
                lcd_buf[h * lcd_stride + 2 * w + 1] = 0x1f;
            }
        }
    }
    psram_flush_invaild_cache(lcd_buf, lcd_buf_size);

    //向下滑屏
    for (h = 0; h < lcd_h; h++) {
        lcd_draw_area_stride(lcd_buf, 0, 0, lcd_w, lcd_h, lcd_stride, 0, h, true);
        wdt_clear();
    }
    //向上滑屏
    for (h = lcd_h - 1; h >= 0; h--) {
        lcd_draw_area_stride(lcd_buf, 0, 0, lcd_w, lcd_h, lcd_stride, 0, h, true);
        wdt_clear();
    }

    lcd_wait();//dma非阻塞方式推屏, 退出需要待屏幕推完

    free_psram(lcd_buf);
}
#endif

/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD DEVICE INIT
 *
 * Description: LCD 设备初始化
 *
 * Arguments  : *p 板级配置的 LCD SPI 信息
 *
 * Returns    : 0 初始化成功
 * 				-1 初始化失败
 *
 * Notes      : 1、判断是否在板级文件配置SPI，是继续，否进入断言，
 *
 *              2、配置SPI可操作IO给IMD操作
 *
 *              3、LCD设备复位
 *
 *              4、SPI模块初始化，IMD模块初始化
 *********************************************************************************************************
 */

int lcd_drv_init(void *p)
{
	lcd_debug("lcd_drv_init ...\n");
	int err = 0;
	struct ui_devices_cfg *cfg = (struct ui_devices_cfg *)p;
	lcd_dat = (struct lcd_platform_data *)cfg->private_data;

	ASSERT(lcd_dat, "Error! spi io not config");
	lcd_debug("spi pin rest:%d, cs:%d, dc:%d, en:%d\n", \
	          lcd_dat->pin_reset, lcd_dat->pin_cs, lcd_dat->pin_dc, lcd_dat->pin_en);


	/* 如果有使能IO，设置使能IO输出高电平 */
	if (lcd_dat->pin_en != -1) {
		gpio_direction_output(lcd_dat->pin_en, 1);
	}
	/* 把 CS、DC IO 的控制配置到IMD */
	lcd_set_ctrl_pin_func(spi_dc_ctrl, spi_cs_ctrl, spi_te_stat);

    if (__lcd->row_addr_align && __lcd->column_addr_align) {
        lcd_set_align(__lcd->row_addr_align, __lcd->column_addr_align);
    } else {
        lcd_set_align(1, 1);
    }
	__this->pap.wr_sel = lcd_dat->mcu_pins.pin_wr;
	__this->pap.rd_sel = lcd_dat->mcu_pins.pin_rd;
	__this->rgb.hsync_sel = lcd_dat->rgb_pins.pin_hsync;
	__this->rgb.vsync_sel = lcd_dat->rgb_pins.pin_vsync;
	__this->rgb.den_sel = lcd_dat->rgb_pins.pin_den;
	lcd_reset(); /* lcd复位 */
	/* 初始化imd和硬件SPI等 */
	lcd_init(__this);
	/* SPI发送屏幕初始化代码 */
	lcd_drv_cmd_list(__lcd->lcd_cmd, __lcd->cmd_cnt);
	/* y_printf("RAM1_SIZE:%d \n", &RAM1_SIZE); */
    if (lcd_qspi_st77903_mode()) {
        //lcd_qspi_st77903_refresh_mode_set(0);
        lcd_qspi_st77903_refresh_mode_set(1);
        //lcd_qspi_st77903_refresh_mode_set(2);
        #if 0
        int err = task_create(lcd_task, NULL, "lcd");
        ASSERT(!err);
		#else
		lcd_buf_malloc();
		int lcd_w;
		int lcd_h;
		int lcd_stride;

		ASSERT(__this);

		if ((0 == lcd_qspi_st77903_refresh_mode_get()) || (2 == lcd_qspi_st77903_refresh_mode_get())) {
			lcd_w = __this->lcd_width;
			lcd_h = __this->lcd_height;
			lcd_stride = (lcd_w * ((__this->in_format == FORMAT_RGB888) ? 3 : 2) + 3) / 4 * 4;
		} else {
			lcd_w = __this->lcd_width + 8;
			lcd_h = __this->lcd_height;
			lcd_stride = (lcd_w * ((__this->in_format == FORMAT_RGB888) ? 3 : 2) + 3) / 4 * 4;
		}

		if (__lcd->lcd_buf) {
            lcd_spi_draw_st77903(__lcd->lcd_dbuf[__lcd->lcd_dbuf_index], lcd_w, lcd_h, lcd_stride, 8, 8);
        }
        #endif
    }
	if (check_ram1_size()) {
		/* y_printf("%s,%d \n", __func__, __LINE__); */
		//imb_set_memory_func(malloc_vlt, free_vlt);
	}
	lcd_mcpwm_init();
	return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       GET LCD DEVICE INFO
 *
 * Description: 获取 LCD 设备信息
 *
 * Arguments  : *info LCD 设备信息缓存结构体，根据结构体内容赋值即可
 *
 * Returns    : 0 获取成功
 *              -1 获取失败
 *
 * Notes      : 1、根据参数结构体的内容，将LCD对应信息赋值给结构体元素
 *********************************************************************************************************
 */

static int lcd_drv_get_screen_info(struct lcd_info *info)
{
	/* imb的宽高 */
	info->width = __this->in_width;
	info->height = __this->in_height;

	/* imb的输出格式 */
	info->color_format = __this->in_format;//OUTPUT_FORMAT_RGB565;
	if (info->color_format == OUTPUT_FORMAT_RGB565) {
		info->stride = (info->width * 2 + 3) / 4 * 4;
	} else if (info->color_format == OUTPUT_FORMAT_RGB888) {
		info->stride = (info->width * 3 + 3) / 4 * 4;
	}

	/* 屏幕类型 */
	info->interface = __this->lcd_type;

	/* 对齐 */
	info->col_align = __lcd->column_addr_align;
	info->row_align = __lcd->row_addr_align;
    if (!info->col_align) {
        info->col_align = 1;
    }
    if (!info->row_align) {
        info->row_align = 1;
    }

	/* 背光状态 */
	info->bl_status = !!backlight_status;
	info->buf_num = __this->buffer_num;

	ASSERT(info->col_align, " = 0, lcd driver column address align error, default value is 1");
	ASSERT(info->row_align, " = 0, lcd driver row address align error, default value is 1");

	return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       MALLOC DISPLAY BUFFER
 *
 * Description: 申请 LCD 显存 buffer
 *
 * Arguments  : **buf 保存显存buffer指针
 *              *size 保存显存buffer大小
 *
 * Returns    : 0 成功
 *              -1 失败
 *
 * Notes      : 1、根据LCD驱动中配置的显存大小和数量申请显存BUFFER
 *
 *              2、将显存buffer指针赋值给参数**buf，显存buffer大小赋值给参数*size
 *
 *              注意：buffer默认是lock状态，此时不能推屏，需由UI框架获取并写入数据后才能推屏
 *********************************************************************************************************
 */

static int lcd_drv_buffer_malloc(u8 **buf, u32 *size)
{
	/* int buf_size = ((__this->lcd_width * __this->lcd_height * 2) + 3) / 4 * 4; */
	int buf_size = (__this->buffer_size + 3) / 4 * 4;   // 把buffer大小做四字节对齐

	if (check_ram1_size() == 0) {
		*buf = (u8 *)malloc(buf_size * __this->buffer_num);
	} else {
		*buf = (u8 *)malloc(buf_size * __this->buffer_num);
	}

	/* *buf = (u8 *)malloc(__this->buffer_size * __this->buffer_num); */

	if (!buf) {
		// 如果buffer申请失败
		*buf = NULL;
		*size = 0;
		return -1;
	}

	/* *size = __this->buffer_size; */
	*size = buf_size * __this->buffer_num;

	return 0;
}



/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       FREE DISPLAY BUFFER
 *
 * Description: 释放 LCD 显存 buffer
 *
 * Arguments  : *buf 显存buffer指针
 *
 * Returns    : 0 成功
 *              -1 失败
 *
 * Notes      : 1、使用memory API 释放显存buffer
 *********************************************************************************************************
 */

static int lcd_drv_buffer_free(u8 *buf)
{
	if (buf) {
		if (check_ram1_size() == 0) {
			free(buf);
		} else {
			free(buf);
		}
		buf = NULL;
	}
	return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD DRAW BUFFER
 *
 * Description: 把显存 buf 推送到屏幕
 *
 * Arguments  : *buf 显存buffer指针
 *              len 显存buffer的数据量
 *              wait 是否等待
 *
 * Returns    : 0 成功
 *              -1 失败
 *
 * Notes      : 1、使用 IMD 模块将显存buffer推给屏幕
 *********************************************************************************************************
 */

static int lcd_drv_draw(u8 *buf, u32 len, u8 wait)
{
	if (lcd_qspi_st77903_mode()) {
		return -1;
	}

	lcd_draw(LCD_DATA_MODE, (u32)buf);
	return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD DRAW PAGE
 *
 * Description: 将UI页面buffer推给屏幕
 *
 * Arguments  : *buf 页面缓存buffer指针
 *
 * Returns    : 0 成功
 *              -1 失败
 *
 * Notes      :
 *********************************************************************************************************
 */

static int lcd_draw_page(u8 *buf, u8 page_star, u8 page_len)
{
	// 绘制页面功能暂时没有
	return 0;
}



/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       GET LCD BACKLIGHT STATUS
 *
 * Description: 获取 LCD 背光状态
 *
 * Arguments  : none
 *
 * Returns    : 0 背光熄灭
 *              1 背光点亮
 *
 * Notes      :
 *********************************************************************************************************
 */

int lcd_backlight_status()
{
	return !!backlight_status;
}

/*
 *********************************************************************************************************
 *                                       GET LCD BACKLIGHT STATUS
 *
 * Description: 获取 LCD sleep状态
 *
 * Arguments  : none
 *
 * Returns    : 0 sleep out
 *              1 sleep in
 *
 * Notes      :
 *********************************************************************************************************
 */

int lcd_sleep_status()
{
	return lcd_sleep_in;
}

// 触摸屏sleep处理
__attribute__((weak)) void ctp_enter_sleep(void)
{
}
__attribute__((weak)) void ctp_exit_sleep(void)
{
}

static void lcd_st77903_stop(void)
{
    #if 0
    if (lcd_qspi_st77903_mode()) {
        __lcd->lcd_exit = 1;
        while (__lcd->lcd_exit) {
            os_time_dly(1);
        }
    }
    #endif
}

static void lcd_st77903_start(void)
{
    #if 0
    if (lcd_qspi_st77903_mode()) {
        __lcd->lcd_enter = 1;
        OS_SEM *lcd_sem = (OS_SEM *)lcd_qspi_st77903_get_info();
        os_sem_post(lcd_sem);
        while (__lcd->lcd_enter) {
            os_time_dly(1);
        }
    }
    #endif
}
/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD SLEEP CONTROL
 *
 * Description: LCD 休眠控制
 *
 * Arguments  : enter 是否进入休眠，true 进入休眠，false 退出休眠
 *
 * Returns    : 0 成功
 *              -1 失败
 *
 * Notes      : 1、判断 LCD 是否正在使用，是等待使用结束，否进入下一步
 *
 *              2、enter是否进入休眠，是使用LCD休眠函数进入休眠状态，否使用LCD退出休眠函数退出休眠状态
 *
 *              3、lcd_sleep_in 记录LCD的休眠状态
 *********************************************************************************************************
 */




int lcd_sleep_ctrl(u8 enter)
{
	if ((!!enter) == lcd_sleep_in) {
		return -1;
	}
	while (is_lcd_busy);
	is_lcd_busy = 0x11;

	if (enter) {
		lcd_backlight_ctrl_base(0);
#if TCFG_TP_SLEEP_EN
		ctp_enter_sleep();
#endif /* #if TCFG_TP_SLEEP_EN */
        lcd_st77903_stop();
		if (__lcd->entersleep) {
			__lcd->entersleep();
			lcd_sleep_in = true;
		}
		clock_remove_set(DEC_UI_CLK);
	} else {
		clock_add_set(DEC_UI_CLK);
#if TCFG_TP_SLEEP_EN
		ctp_exit_sleep();
#endif /* #if TCFG_TP_SLEEP_EN */
		if (__lcd->exitsleep) {
			__lcd->exitsleep();
			lcd_sleep_in = false;
		}
        lcd_st77903_start();
	}

	is_lcd_busy = 0;
	return 0;
}

void lcd_bl_open()
{
	lcd_mcpwm_init();
	lcd_backlight_ctrl_base(backlight_status);
}




/*********************************************************************************************************
 *                                       LCD POWER CONTROL
 *
 * Description: LCD 关机控制,关机使用
 *
 *
 * Returns    : 0 成功
 *              -1 失败
 *
 *********************************************************************************************************/




int lcd_poweroff()
{


	if (lcd_sleep_in) {
		return -1;
	}

	if (is_lcd_busy) {
		return -1;
	}

	is_lcd_busy = 0x11;
	lcd_backlight_ctrl_base(0);
#if TCFG_TP_SLEEP_EN
	ctp_enter_sleep();
#endif /* #if TCFG_TP_SLEEP_EN */
	if (__lcd->entersleep) {
		__lcd->entersleep();
		lcd_sleep_in = true;
	}

	lcd_en_ctrl(0);
	is_lcd_busy = 0;
	return 0;
}

/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       GET LCD DRIVE HANDLER
 *
 * Description: 获取LCD驱动句柄
 *
 * Arguments  : none
 *
 * Returns    : struct lcd_interface* LCD驱动接口句柄
 *
 * Notes      : 1、从LCD接口列表中找到LCD接口句柄并返回
 *********************************************************************************************************
 */

struct lcd_interface *lcd_get_hdl()
{
	struct lcd_interface *p;

	ASSERT(lcd_interface_begin != lcd_interface_end, "don't find lcd interface!");
	for (p = lcd_interface_begin; p < lcd_interface_end; p++) {
		return p;
	}
	return NULL;
}


extern void imd_set_buf_size(int width, int height, int stride);
static void lcd_drv_set_draw_area(u16 xs, u16 xe, u16 ys, u16 ye)
{
#if 0
	u16 width = xe - xs + 1;
	u16 height = ye - ys  + 1;
	u16 stride  = (width * 2 + 3) / 4 * 4;
	imd_set_buf_size(width, height, stride);
#endif
	if (lcd_qspi_st77903_mode()) {
		return;
	}

	lcd_set_draw_area(xs, xe, ys, ye);
}


static void lcd_drv_clear_screen(u32 color)
{
	if (lcd_qspi_st77903_mode()) {
		return;
	}

	lcd_full_clear(color);
}

REGISTER_LCD_INTERFACE(lcd) = {
	.init               = lcd_drv_init,
	.draw               = lcd_drv_draw,
	.get_screen_info    = lcd_drv_get_screen_info,
	.buffer_malloc      = lcd_drv_buffer_malloc,
	.buffer_free        = lcd_drv_buffer_free,
	.backlight_ctrl     = lcd_drv_backlight_ctrl,
	.power_ctrl         = lcd_drv_power_ctrl,
	.set_draw_area      = lcd_drv_set_draw_area,
	.clear_screen       = lcd_drv_clear_screen,
};


static u8 lcd_idle_query(void)
{
	return !is_lcd_busy;
}

//REGISTER_LP_TARGET(lcd_lp_target) = {
//    .name = "lcd",
//    .is_idle = lcd_idle_query,
//};




