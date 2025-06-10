/* ac_msg_queue.h - AC Control Message Queue Module */

#ifndef _AC_MSG_QUEUE_H_
#define _AC_MSG_QUEUE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* ==================== 消息队列配置 ==================== */
#define AC_MSG_QUEUE_SIZE 16  /* 消息队列最大长度 */

/* ==================== 消息队列数据结构 ==================== */

/* BLE Mesh消息类型 */
typedef enum {
    AC_MSG_TYPE_SET_POWER,
    AC_MSG_TYPE_GET_POWER,
    AC_MSG_TYPE_SET_TEMPERATURE,
    AC_MSG_TYPE_GET_TEMPERATURE,
    AC_MSG_TYPE_SET_MODE,
    AC_MSG_TYPE_GET_MODE,
    AC_MSG_TYPE_SET_FAN_SPEED,
    AC_MSG_TYPE_GET_FAN_SPEED,
} ac_msg_type_t;

/* 消息队列项结构 */
typedef struct {
    ac_msg_type_t msg_type;     /* 消息类型 */
    uint16_t server_addr;       /* 目标服务器地址 */
    uint8_t value;              /* 消息值（对于GET类型消息无效） */
    uint32_t timestamp;         /* 消息时间戳 */
} ac_msg_queue_item_t;

/* 消息发送状态 */
typedef enum {
    AC_SEND_STATE_IDLE,         /* 空闲状态，可以发送消息 */
    AC_SEND_STATE_SENDING,      /* 正在发送消息 */
    AC_SEND_STATE_WAITING_ACK,  /* 等待响应 */
} ac_send_state_t;

/* 消息发送回调函数类型 */
typedef esp_err_t (*ac_msg_send_callback_t)(const ac_msg_queue_item_t *msg);

/* 消息发送完成回调函数类型 */
typedef void (*ac_msg_send_complete_callback_t)(const ac_msg_queue_item_t *msg, esp_err_t result);

/* ==================== 核心API接口 ==================== */

/**
 * @brief 初始化消息队列
 * 
 * @param send_cb 消息发送回调函数
 * @param complete_cb 消息发送完成回调函数（可为NULL）
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_msg_queue_init(ac_msg_send_callback_t send_cb, ac_msg_send_complete_callback_t complete_cb);

/**
 * @brief 向队列添加消息（核心功能）
 * 
 * @param msg_type 消息类型
 * @param server_addr 目标服务器地址
 * @param value 消息值
 * @return esp_err_t ESP_OK成功，ESP_ERR_NO_MEM队列满，其他值失败
 */
esp_err_t ac_msg_queue_enqueue(ac_msg_type_t msg_type, uint16_t server_addr, uint8_t value);

/**
 * @brief 通知消息发送完成（由BLE Mesh回调调用）
 * 
 * @param success 发送是否成功
 */
void ac_msg_queue_send_complete(bool success);

/**
 * @brief 通知消息发送超时（由BLE Mesh回调调用）
 */
void ac_msg_queue_send_timeout(void);

/**
 * @brief 获取队列状态信息
 * 
 * @return uint8_t 队列中的消息数量
 */
uint8_t ac_msg_queue_get_size(void);

/**
 * @brief 获取当前发送状态
 * 
 * @return ac_send_state_t 当前发送状态
 */
ac_send_state_t ac_msg_queue_get_send_state(void);

/**
 * @brief 清空消息队列（用于错误恢复）
 */
void ac_msg_queue_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* _AC_MSG_QUEUE_H_ */ 