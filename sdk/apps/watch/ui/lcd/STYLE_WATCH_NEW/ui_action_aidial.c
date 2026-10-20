#include "app_config.h"
#include "media/includes.h"
#include "JL_rcsp_protocol.h"
#include "ui/ui_api.h"
#include "ui/ui.h"
#include "ui/ui_pic_index.h"
#include "ui/ui_style.h"
#include "app_task.h"
#include "system/timer.h"
#include "app_main.h"
#include "init.h"
#include "ai_interaction/ai_audio.h"
#include "ai_interaction/ai_interaction.h"
#include "btstack/avctp_user.h"
#include "ui/ui_number.h"
#include "smartbox_rcsp_manage.h"
#include "ui/ui_text.h"
#include "ui_expand/ui_scrollview.h"
#include "JL_rcsp_protocol.h"
#if TCFG_AI_INTERACTION_ENABLE

#if 0//手动开打印  0---开打印，1---不开打印
#define ui_log  //
#else
#define ui_log  printf
#endif

static u8 aidial_core = 0; //0--未连接 1--进入 2--对话 3--文本显示 4--思考 5--安装表盘
#define STYLE_NAME JL
int cuntdown_flag = 60; //60s倒计时
volatile static int dial_button = 0; //开始录音的按钮控件取反标志位 0----开始录音  1----停止录音
static bool ai_dial_transfer_flag = false; //由于文件传输结束后会重新进入该页面，此标志位用于文件传输后管理显示的页面
extern u8 get_rcsp_connect_status(void);
extern u8 ui_auto_shut_down_disable(void);
extern void ui_auto_shut_down_enable(void);
extern int ai_interaction_rec_start(void);
extern int ai_interaction_rec_stop(void);
static void reflash_txt_handler(void);
static void reflash_getdialStyle_handler(void);
static void AI_DIAL_WAVE_handler();
static void show_aidial(void);
static void change_error(void);
static u8 aidial_show_flag;//用于标志是否下发完毕，1---下发完毕  0---没下发
extern volatile u8 *ai_dial_txt1;
//static void mem_settxt(void);
volatile u8 *aidial_txt;
#define aidial_bgp_path  "storage/virfat_flash/C/AITHUMB"
static u16 DIAL_TIMER_CHECK;//定时器，用于在全局监测app连接状态
static u16 DIAL_TIMER;//定时器，用于在全局监测app连接状态
/****************************************************************************************/
//								RCSP-AI表盘												//
/****************************************************************************************/
#define AI_DIAL_MSG 0X00


#define EXIST_AI_DIAL  0X00
#define ENTER_AI_DIAL  0X01


u8 _rcsp_ai_dial_flag;
enum {
    _flag_dial_style,
    _flag_dial_notice,
    _flag_pic_generate,
    _flag_re_record,
    _flag_train_success,
    _flag_dial_install,
    _flag_dial_re_gennerate
};
static u8 switch_cmd_send(u8 flag_type)
{
    switch (flag_type) {
    case 0:
        _rcsp_ai_dial_flag = _flag_dial_style;
        break;
    case  1:
        _rcsp_ai_dial_flag = _flag_dial_notice;
        break;
    case 2:
        _rcsp_ai_dial_flag = _flag_pic_generate;
        break;
    case 3:
        _rcsp_ai_dial_flag = _flag_re_record;
        break;
    case 4:
        _rcsp_ai_dial_flag = _flag_train_success;
        break;
    case 5:
        _rcsp_ai_dial_flag = _flag_dial_install;
        break;
    case 6:
        _rcsp_ai_dial_flag = _flag_dial_re_gennerate;
        break;
    }
    return _rcsp_ai_dial_flag;
}

#pragma pack(1)
struct _AI_RESPONCE_MSG {
    u8 ai_dial_data_a;
};
#pragma pack()

void put_aidial(void *priv, u8 OpCode, u8 OpCode_SN, u8 *data, u16 len)
{
    struct _AI_RESPONCE_MSG ai_responce_msg;
    ai_responce_msg.ai_dial_data_a = 0X00;
    u8 op = data[1];
    if (op == 0X00) {
        u8 *temp = (u8 *)(data + 2);
        size_t bylen = strlen((u8 *)temp);
        size_t hlen = bylen / sizeof(u8);
        *(temp + hlen - 1) = '\0';
        put_buf(temp, hlen - 1);
        if (aidial_txt) {
            free(aidial_txt);
            aidial_txt = NULL;
        }
        aidial_txt = strdup(temp);
        ui_log("ZEZEZEstring:%s\n", aidial_txt);
        put_buf(aidial_txt, hlen);
        JL_CMD_response_send(OpCode, JL_PRO_STATUS_SUCCESS, OpCode_SN, (u8 *)&ai_responce_msg, sizeof(struct _AI_RESPONCE_MSG));
    }
    if (op == 0X04) {
        ui_log("FOCUS!!!\n");
        JL_CMD_response_send(OpCode, JL_PRO_STATUS_SUCCESS, OpCode_SN, (u8 *)&ai_responce_msg, sizeof(struct _AI_RESPONCE_MSG));
        UI_MSG_POST("show_aidial");
    }
}
#pragma pack(1)
struct _AI_DIAL_MSG {
    u8 ai_dial_status;
    u8 ai_dial_flag;
    u8 ai_dial_param;
};
#pragma pack()


#pragma pack(1)
struct _AI_DIAL_SEN {
    u8 a;
    u8 b;
};
#pragma pack()


static void send_dial_cmd(u8 status, u8 flag_type, u8 state)
{
    struct _AI_DIAL_MSG ai_dial_send;
    if (state == ENTER_AI_DIAL || state == EXIST_AI_DIAL) {
        ai_dial_send.ai_dial_status = status;
        ai_dial_send.ai_dial_flag = switch_cmd_send(flag_type);
        ai_dial_send.ai_dial_param = state;
    }
    JL_CMD_send(JL_OPCODE_AI_DIAL, (u8 *)&ai_dial_send, sizeof(struct _AI_DIAL_MSG), JL_NEED_RESPOND);
}

static void send_dialWithoutState_cmd(u8 status, u8 flag_type)
{
    struct _AI_DIAL_SEN ai_dial_sen;
    u8 temp = switch_cmd_send(flag_type);
    ai_dial_sen.a = status;
    ai_dial_sen.b = temp;
    JL_CMD_send(JL_OPCODE_AI_DIAL, (u8 *)&ai_dial_sen, sizeof(struct _AI_DIAL_SEN), JL_NEED_RESPOND);
}

/****************************************************************************************************/
//						            		UI-公共接口												//
/********************************************** *****************************************************/

/****************************************************************************************************/
//									框架
//              |========================|	           |===========================|
//	RCSP------ENTER------VOICE-------VOICETXT--------THINK---(file_transfer)----JPGSHOW------INSTALL
//			    |==================================================================|
//
/****************************************************************************************************/
static const struct uimsg_handl ui_msg_cmd_handler[] = {
    {"SEND_TXT", reflash_txt_handler},
    {"show_aidial", show_aidial},
    {"AI_ERROR", change_error},
    {NULL, NULL},
};

static void page_switch(u8 ui_temp)
{
    struct element *ctr = ui_core_get_element_by_id(AI_DIAL);
    if (ctr == NULL) {
        return;
    }
    if ((get_rcsp_connect_status()) && (get_call_status() != BT_CALL_ACTIVE)) {
        aidial_core = ui_temp;
    } else {
        aidial_core = 0;
    }
    struct element *elm;
    list_for_each_child_element(elm, (struct element *)ctr) {
        switch (elm->id) {
        case AI_DIAL_LINK:
            if (!aidial_core) {
                elm->css.invisible = 0;

            } else {
                elm->css.invisible = 1;
            }
            break;
        case AI_ENTER_DIAL:
            if (aidial_core == 1) {
                elm->css.invisible = 0;
            } else {
                elm->css.invisible = 1;
            }
            break;
        case AI_DIALOGUE:
            if (aidial_core == 2) {
                elm->css.invisible = 0;
            } else {
                elm->css.invisible = 1;
            }
            break;
        case AI_DIAL_GENERATE:
            if (aidial_core == 3) {
                elm->css.invisible = 0;
            } else {
                elm->css.invisible = 1;
            }
            break;
        case AI_DIAL_INSTALL:
            if (aidial_core == 4) {
                elm->css.invisible = 0;
            } else {
                elm->css.invisible = 1;
            }
            break;
        case AI_DIAL_INSTALLING:
            if (aidial_core == 5) {
                elm->css.invisible = 0;
            } else {
                elm->css.invisible = 1;
            }
            break;
        }
        ui_core_redraw(ctr);
    }
}
static void page_enter_dial()
{
    page_switch(1);
}
static void page_ai_dialogue()
{
    page_switch(2);
}
static void page_generate()
{
    ui_register_msg_handler(ID_WINDOW_AI_DIAL, ui_msg_cmd_handler);//注册消息交互的回调
    page_switch(3);
}
static void page_show()
{
    page_switch(4);
}
static void page_installing()
{
    page_switch(5);
}
static void show_aidial(void)
{
    ui_log("aidial_show_aidial!!!\n");
    page_show();
}
static void change_error(void)
{
    extern u8  aidial_flag;
    ui_log("aidial_error!!!\n");
    ui_log("aidial_flag:%d\n", aidial_flag);
    struct element *ctr = ui_core_get_element_by_id(AI_DIAL_GENERATE);
    struct element *elm;
    list_for_each_child_element(elm, (struct element *)ctr)
    switch (elm->id) {
    case AI_DIAL_CONFIRM:
    case BASEFORM_1229:
        elm->css.invisible = 0;
        break;
    }
    ui_core_redraw(ctr);
}
static void reflash_getdialStyle_handler(void)
{
    ui_log(">>>>>>>>>>>>>>>>DIAL_TIMER_STYLE>>>>>>\n");
    if (aidial_txt) {
        ui_text_set_textu_by_id(AI_DIAL_STYLE, aidial_txt, strlen(aidial_txt), FONT_DEFAULT);
    }
}
static void check_rcsp_status()
{

    if (!get_rcsp_connect_status()) {
        UI_HIDE_CURR_WINDOW();
        UI_SHOW_WINDOW(PAGE_7);
    }
}
static void AI_DIAL_WAVE_handler()
{
    ui_log(">>>>>>>>>>>>>>>>DIAL_TIMER_WAVE>>>>>>\n");
    static int i = 0;
    i++;
    ui_pic_show_image_by_id(AI_DIAL_WAVE, i);
    if (i == 10) {
        i = 1;
    }
    if (i % 5 == 0) {
        cuntdown_flag--;
        if (cuntdown_flag == 0) {
            cuntdown_flag = 60;
        } else if (dial_button == 0) {
            cuntdown_flag = 60;
        }
        struct unumber num;
        num.type = TYPE_NUM;
        num.numbs = 1;
        num.number[0] = cuntdown_flag;
        ui_number_update_by_id(AI_DIAL_TIME, &num);
    }
    return;
}

static void reflash_txt_handler(void)
{
    printf("ai_dial_txt1!!!:%s\n", ai_dial_txt1);
    ui_text_set_textu_by_id(AI_DIAL_TXT, ai_dial_txt1, strlen(ai_dial_txt1), FONT_DEFAULT);
}

static void delete_dial_file()
{
    FILE *file = NULL;
    file = fopen(aidial_bgp_path, "r");
    if (file) {
        ui_log("file exist!!!\n");
        fdelete(file);
        file = NULL;
    } else {
        ui_log("file not exist!!!\n");
        return;
    }
}

static void creat_AI_timer()
{
    if (!DIAL_TIMER) {
        DIAL_TIMER = sys_timer_add(NULL, AI_DIAL_WAVE_handler, 200);
    }
}
static void release_AI_TIMER()
{
    if (DIAL_TIMER) {
        sys_timer_del(DIAL_TIMER);
        DIAL_TIMER = NULL;
    }
}

//****************************************************************************************/
//								图层-AI表盘												//
/****************************************************************************************/
static int AI_DIAL_onchange(void *ctr, enum element_change_event e, void *arg)
{
    struct window *window = (struct window *)ctr;
    switch (e) {
    case ON_CHANGE_INIT:
        break;
    case ON_CHANGE_FIRST_SHOW:
        dial_button = 0;
        if ((get_rcsp_connect_status()) && ((get_call_status() != BT_CALL_ACTIVE))) {
            aidial_core = 1;
            if (aidial_show_flag) {
                aidial_core = 4;
            }
            if (!aidial_show_flag) {
                aidial_core = 1;
            }

        } else {
            aidial_core = 0;
        }
        struct element *elm;
        list_for_each_child_element(elm, (struct element *)ctr) {
            switch (elm->id) {
            case AI_DIAL_LINK:
                if (!aidial_core) {
                    elm->css.invisible = 0;
                } else {
                    elm->css.invisible = 1;
                }
                break;
            case AI_ENTER_DIAL:
                if (aidial_core == 1) {
                    elm->css.invisible = 0;
                } else {
                    elm->css.invisible = 1;
                }
                break;
            case AI_DIALOGUE:
                if (aidial_core == 2) {
                    elm->css.invisible = 0;
                } else {
                    elm->css.invisible = 1;
                }
                break;
            case AI_DIAL_GENERATE:
                if (aidial_core == 3) {
                    elm->css.invisible = 0;
                } else {
                    elm->css.invisible = 1;
                }
                break;
            case AI_DIAL_INSTALL:
                if (aidial_core == 4) {
                    elm->css.invisible = 0;
                } else {
                    elm->css.invisible = 1;
                }
                break;
            case AI_DIAL_INSTALLING:
                if (aidial_core == 5) {
                    elm->css.invisible = 0;
                } else {
                    elm->css.invisible = 1;
                }
                break;
            }
        }
        break;
    case ON_CHANGE_RELEASE:
        ai_interaction_rec_stop();
        break;
    }
    return false;
}
REGISTER_UI_EVENT_HANDLER(AI_DIAL)
.onchange = AI_DIAL_onchange,
 .onkey = NULL,
  .ontouch = NULL,
};
//****************************************************************************************/
//								layout0-AI表盘											//
/****************************************************************************************/
static void change_text()
{
    ui_text_show_index_by_id(AI_DIAL_STATUS, 0);
}
static void change_phone_txt()
{
    ui_text_show_index_by_id(AI_DIAL_STATUS, 1);
}
static int AI_status_onchange(void *ctr, enum element_change_event e, void *arg)
{
    struct ui_text *text = (struct ui_text *)ctr;
    switch (e) {
    case ON_CHANGE_FIRST_SHOW:
        if (!get_rcsp_connect_status()) {
            ui_set_call(change_text, 0);
        }
        if (get_call_status() == BT_CALL_ACTIVE) {
            ui_set_call(change_phone_txt, 0);
        }
        break;
    case ON_CHANGE_RELEASE:
        break;
    default:
        break;
    }
    return 0;
}
REGISTER_UI_EVENT_HANDLER(AI_DIAL_LINK)
.onchange = AI_status_onchange,
 .onkey = NULL,
  .ontouch = NULL,
};


//****************************************************************************************/
//								layout1-AI表盘											//
/****************************************************************************************/

static int AI_ENTER_onchange(void *ctr, enum element_change_event e, void *arg)
{
    struct ui_text *text = (struct ui_text *)ctr;
    switch (e) {
    case ON_CHANGE_INIT:
        ui_set_call(reflash_getdialStyle_handler, 0); //进入页面显示一次当前表盘风格
        break;
    case ON_CHANGE_RELEASE:
        break;
    default:
        break;
    }
    return 0;
}
REGISTER_UI_EVENT_HANDLER(AI_ENTER_DIAL)
.onchange = AI_ENTER_onchange,
 .onkey = NULL,
  .ontouch = NULL,
};
static int AI_ENTER_ontouch(void *ctr, struct element_touch_event *e)
{
    switch (e->event) {
    case ELM_EVENT_TOUCH_UP:
        send_dial_cmd(AI_DIAL_MSG, 1, ENTER_AI_DIAL);
        page_ai_dialogue();//进入录音
        break;
    default :
        break;
    }
    return false;
}
REGISTER_UI_EVENT_HANDLER(AI_DIAL_ENTER)
.onchange = NULL,
 .onkey = NULL,
  .ontouch = AI_ENTER_ontouch,
};
//****************************************************************************************/
//								layout2-AI表盘											//
/****************************************************************************************/
static int AI_dialogue_onchange(void *ctr, enum element_change_event e, void *arg)
{
    struct element *elm = (struct element *)ctr;
    switch (e) {
    case ON_CHANGE_INIT:
        cuntdown_flag = 60;
        ui_auto_shut_down_disable();
        break;
    case ON_CHANGE_RELEASE:
        ui_auto_shut_down_enable();
        break;
    }
    return 0;
}
static int AI_dialogue_ontouch(void *ctr, struct element_touch_event *e)
{
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        if (!dial_button) {
            creat_AI_timer();
            ai_interaction_rec_start();
            dial_button = 1;
            ui_log(">>>>>>>>>>>>>>>>>开始录音>>>>>>>>>>>>>>>>>>>>\n");
        } else if (dial_button) {
            release_AI_TIMER();
            ui_hide(AI_DIAL_GENERATE);
            ui_show(AI_DIAL_GENERATE);
            cuntdown_flag = 60;
            delete_dial_file();//录音结束把上一个表盘文件删除
            ai_interaction_rec_stop();
            ui_log(">>>>>>>>>>>>>>>>>结束录音>>>>>>>>>>>>>>>>>>>>\n");
            dial_button = 0;
            page_generate();
        }
        ui_text_show_index_by_id(AI_DIAL_STR, dial_button);
        break;
    default :
        break;
    }
    return true;
}

REGISTER_UI_EVENT_HANDLER(AI_DIALOGUE)
.onchange = AI_dialogue_onchange,
 .onkey = NULL,
  .ontouch = NULL,
};
REGISTER_UI_EVENT_HANDLER(AI_DIAL_VOICE)
.onchange = NULL,
 .onkey = NULL,
  .ontouch = AI_dialogue_ontouch,
};
//****************************************************************************************/
//								layout3-AI表盘											//
/****************************************************************************************/
static int AI_DIAL_confirm_ontouch(void *ctr, struct element_touch_event *e)
{
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        ai_dial_transfer_flag = true;
        aidial_show_flag = 1;
        send_dialWithoutState_cmd(AI_DIAL_MSG, 2);
        page_installing();
        break;
    default :
        break;
    }
    return true;
}

static int AI_DIAL_return_ontouch(void *ctr, struct element_touch_event *e)
{
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        cuntdown_flag = 60;
        send_dialWithoutState_cmd(AI_DIAL_MSG, 3);
        page_enter_dial();
        break;
    default :
        break;
    }
    return true;
}

static int hid_button_txt_onchange(void *ctr, enum element_change_event e, void *arg)
{
    struct element *elm = (struct element *)ctr;
    switch (e) {
    case ON_CHANGE_FIRST_SHOW:
        //把按钮控件隐藏起来
        struct element *ctr = ui_core_get_element_by_id(AI_DIAL_GENERATE);
        list_for_each_child_element(elm, (struct element *)ctr)
        switch (elm->id) {
        case AI_DIAL_CONFIRM:
        case BASEFORM_1229:
            elm->css.invisible = 1;
            break;
        }
        break;
    case ON_CHANGE_RELEASE:
        break;
    }
    return 0;
}
REGISTER_UI_EVENT_HANDLER(AI_DIAL_GENERATE)
.onchange = hid_button_txt_onchange,
 .onkey = NULL,
  .ontouch = NULL,
};
REGISTER_UI_EVENT_HANDLER(AI_DIAL_CONFIRM)
.onchange = NULL,
 .onkey = NULL,
  .ontouch = AI_DIAL_confirm_ontouch,
};
REGISTER_UI_EVENT_HANDLER(AI_DIAL_RETURN)
.onchange = NULL,
 .onkey = NULL,
  .ontouch = AI_DIAL_return_ontouch,
};
//****************************************************************************************/
//								layout4-AI表盘											//
/****************************************************************************************/
static int AI_DIAL_INSTALL_ontouch(void *ctr, struct element_touch_event *e)
{
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        ai_dial_transfer_flag = true;
        aidial_show_flag = 0;
        send_dialWithoutState_cmd(AI_DIAL_MSG, 5);
        page_installing();
        break;
    default :
        break;
    }
    return true;
}

static int AI_DIAL_regenerate_ontouch(void *ctr, struct element_touch_event *e)
{
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        page_installing();
        ai_dial_transfer_flag = true;
        send_dialWithoutState_cmd(AI_DIAL_MSG, 6);
        break;
    default :
        break;
    }
    return true;
}

//****************************************************************************************/
//								layout5-AI表盘											//
/****************************************************************************************/
static int AI_DIAL_installing_onchange(void *ctr, enum element_change_event e, void *arg)
{
    switch (e) {
    case ON_CHANGE_FIRST_SHOW:
        break;
    case ON_CHANGE_RELEASE:
        break;
    }
    return 0;
}
static int AI_DIAL_installing_ontouch(void *ctr, struct element_touch_event *e)
{
    switch (e->event) {
    case ELM_EVENT_TOUCH_MOVE:
        break;
    default :
        break;
    }
    return true;
}

REGISTER_UI_EVENT_HANDLER(AI_DIAL_INSTALLING)
.onchange = AI_DIAL_installing_onchange,
 .onkey = NULL,
  .ontouch = AI_DIAL_installing_ontouch,
};
//****************************************************************************************/
//								page_show-AI表盘										//
/****************************************************************************************/
struct aidial_photo_info {
    RESFILE *file;
    struct flash_file_info file_info;
};
static struct aidial_photo_info ai_photo = {0};
static int AI_DIAL_PICII_onchange(void *ctr, enum element_change_event e, void *arg)
{
    struct draw_context *dc = (struct draw_context *)arg;
    struct ui_pic *pic = (struct ui_pic *)ctr;
    switch (e) {
    case ON_CHANGE_INIT:
        ai_photo.file = res_fopen(aidial_bgp_path, "r");
        if (!ai_photo.file) {
            return FALSE;
        }
        ui_res_flash_info_get(&ai_photo.file_info, aidial_bgp_path, "res");
        break;
    case ON_CHANGE_SHOW:
        if (ai_photo.file) {
            pic->elm.css.background_image = 1;
            dc->preview.file = ai_photo.file;
            dc->preview.file_info = &ai_photo.file_info;
            dc->preview.id = 1;
            dc->preview.page = 0;
            break;
        }
        break;
    case ON_CHANGE_RELEASE:
        if (ai_photo.file) {
            res_fclose(ai_photo.file);
            ui_res_flash_info_free(&ai_photo.file_info, "res");
            ai_photo.file = NULL;
        } else {
            break;
        }
        break;
    }
    return 0;
}

static int AI_DIAL_recall_ontouch(void *ctr, struct element_touch_event *e)
{
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        aidial_show_flag = 0;
        send_dialWithoutState_cmd(AI_DIAL_MSG, 3);
        page_ai_dialogue();
        break;
    default :
        break;
    }
    return true;
}

REGISTER_UI_EVENT_HANDLER(AI_DIAL_PIC)
.onchange = AI_DIAL_PICII_onchange,
 .onkey = NULL,
  .ontouch = NULL,
};

REGISTER_UI_EVENT_HANDLER(AI_DIAL_INS)
.onchange = NULL,
 .onkey = NULL,
  .ontouch = AI_DIAL_INSTALL_ontouch,
};
REGISTER_UI_EVENT_HANDLER(AI_DIAL_RECALL)
.onchange = NULL,
 .onkey = NULL,
  .ontouch = AI_DIAL_recall_ontouch,
};

REGISTER_UI_EVENT_HANDLER(AI_DIAL_RET)
.onchange = NULL,
 .onkey = NULL,
  .ontouch = AI_DIAL_regenerate_ontouch,
};

//****************************************************************************************/
//								AI表盘---页面管理										//
/****************************************************************************************/
static int AI_page_onchange(void *ctr, enum element_change_event e, void *arg)
{
    struct element *elm = (struct element *)ctr;
    switch (e) {
    case ON_CHANGE_INIT:
        send_dial_cmd(AI_DIAL_MSG, 1, ENTER_AI_DIAL);
        if (!DIAL_TIMER_CHECK) {
            DIAL_TIMER_CHECK = sys_timer_add(NULL, check_rcsp_status, 3000);
        }
        ai_dial_transfer_flag = false;
        ui_register_msg_handler(ID_WINDOW_AI_DIAL, ui_msg_cmd_handler);//注册消息交互的回调
        break;
    case ON_CHANGE_RELEASE:
        release_AI_TIMER();
        send_dial_cmd(AI_DIAL_MSG, 1, EXIST_AI_DIAL);
        /*
        if(!ai_dial_transfer_flag)
        {
        	send_dial_cmd(AI_DIAL_MSG, 1, EXIST_AI_DIAL);
        }
        */
        if (DIAL_TIMER_CHECK) {
            sys_timer_del(DIAL_TIMER_CHECK);
            DIAL_TIMER_CHECK = NULL;
        }
        ai_interaction_rec_stop();
        break;
    }
    return 0;
}
REGISTER_UI_EVENT_HANDLER(ID_WINDOW_AI_DIAL)
.onchange = AI_page_onchange,
 .onkey = NULL,
  .ontouch = NULL,
};
#endif
