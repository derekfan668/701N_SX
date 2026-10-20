
#ifndef __MASS_DATA_H__
#define __MASS_DATA_H__

#include "typedef.h"
#include "app_config.h"

#define MASS_DATA_TYPE_ORIGINAL			0 // 原始数据
#define MASS_DATA_TYPE_ALIYUN			1 // 阿里云
#define MASS_DATA_TYPE_RCSP_DATA		2 // RCSP
#define MASS_DATA_TYPE_AI_TEXT			3 // AI
#define MASS_DATA_TYPE_TTS				4 // TTS
#define MASS_DATA_TYPE_MAX				5

// 注册收数回调
void mass_data_recv_register(u8 type, void *cb_priv, void (*cb)(void *priv, u8 *data, int len));
// 非阻塞式发送
int mass_data_asyn_send(u8 type, u8 *data, u16 len, void *evt_cb_priv, void (*evt_cb)(void *priv, int event));
// 阻塞式发送 // 不能在app_core和蓝牙相关任务中调用
int mass_data_blocking_send(u8 type, u8 *data, u16 len);

#endif//__MASS_DATA_H__

