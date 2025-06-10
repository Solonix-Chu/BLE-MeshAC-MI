/* ac_msg_queue.c - AC Control Message Queue Module Implementation */

#include "ac_msg_queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

#define TAG "AC_MSG_QUEUE"

/* ==================== 内部数据结构 ==================== */
static struct {
    ac_msg_queue_item_t queue[AC_MSG_QUEUE_SIZE];  /* 消息队列 */
    uint8_t head;                                   /* 队列头指针 */
    uint8_t tail;                                   /* 队列尾指针 */
    uint8_t count;                                  /* 队列中消息数量 */
    ac_send_state_t send_state;                     /* 发送状态 */
    ac_msg_queue_item_t current_msg;                /* 当前正在发送的消息 */
    ac_msg_send_callback_t send_cb;                 /* 消息发送回调 */
    ac_msg_send_complete_callback_t complete_cb;    /* 发送完成回调 */
    bool initialized;                               /* 初始化标志 */
} msg_queue_ctx = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .send_state = AC_SEND_STATE_IDLE,
    .send_cb = NULL,
    .complete_cb = NULL,
    .initialized = false,
};

/* ==================== 内部函数声明 ==================== */
static uint32_t _get_current_timestamp(void);
static esp_err_t _dequeue_message(ac_msg_queue_item_t *msg);
static void _process_next_message(void);

/* ==================== 内部函数实现 ==================== */

/* 获取当前时间戳 */
static uint32_t _get_current_timestamp(void) {
    return (uint32_t)(esp_timer_get_time() / 1000); // Convert to milliseconds
}

/* 从队列取出消息 */
static esp_err_t _dequeue_message(ac_msg_queue_item_t *msg) {
    if (msg_queue_ctx.count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (msg) {
        *msg = msg_queue_ctx.queue[msg_queue_ctx.head];
    }
    
    msg_queue_ctx.head = (msg_queue_ctx.head + 1) % AC_MSG_QUEUE_SIZE;
    msg_queue_ctx.count--;
    
    ESP_LOGD(TAG, "Dequeued message, remaining queue size: %d", msg_queue_ctx.count);
    return ESP_OK;
}

/* 处理队列中的下一个消息 */
static void _process_next_message(void) {
    if (msg_queue_ctx.send_state != AC_SEND_STATE_IDLE) {
        ESP_LOGD(TAG, "Send state not idle (%d), cannot process next message", msg_queue_ctx.send_state);
        return;
    }
    
    if (msg_queue_ctx.count == 0) {
        ESP_LOGD(TAG, "Message queue empty, nothing to process");
        return;
    }
    
    esp_err_t err = _dequeue_message(&msg_queue_ctx.current_msg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to dequeue message: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGD(TAG, "Processing message type %d to 0x%04x from queue", 
             msg_queue_ctx.current_msg.msg_type, msg_queue_ctx.current_msg.server_addr);
    
    msg_queue_ctx.send_state = AC_SEND_STATE_SENDING;
    
    // 调用发送回调函数
    if (msg_queue_ctx.send_cb) {
        err = msg_queue_ctx.send_cb(&msg_queue_ctx.current_msg);
        
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send message type %d to 0x%04x: %s", 
                    msg_queue_ctx.current_msg.msg_type, msg_queue_ctx.current_msg.server_addr, esp_err_to_name(err));
            
            msg_queue_ctx.send_state = AC_SEND_STATE_IDLE;
            
            // 调用完成回调
            if (msg_queue_ctx.complete_cb) {
                msg_queue_ctx.complete_cb(&msg_queue_ctx.current_msg, err);
            }
            
            // 对于BLE Mesh栈繁忙的情况，将消息重新放回队列头部进行重试
            if (err == ESP_ERR_TIMEOUT) {
                ESP_LOGI(TAG, "Re-queueing message due to BLE Mesh busy, queue size: %d", msg_queue_ctx.count);
                
                // 将消息放回队列头部
                msg_queue_ctx.head = (msg_queue_ctx.head == 0) ? (AC_MSG_QUEUE_SIZE - 1) : (msg_queue_ctx.head - 1);
                msg_queue_ctx.queue[msg_queue_ctx.head] = msg_queue_ctx.current_msg;
                msg_queue_ctx.count++;
                
                ESP_LOGD(TAG, "Message re-queued, will retry after short delay");
                return;
            } else {
                // 对于其他错误，丢弃消息并继续处理下一个
                ESP_LOGE(TAG, "Dropping message due to non-recoverable error: %s", esp_err_to_name(err));
                // 立即尝试处理下一个消息
                _process_next_message();
            }
        } else {
            ESP_LOGD(TAG, "Message sent successfully, waiting for completion callback");
            // 发送成功，等待外部调用 ac_msg_queue_send_complete
        }
    } else {
        ESP_LOGE(TAG, "Send callback not set!");
        msg_queue_ctx.send_state = AC_SEND_STATE_IDLE;
    }
}

/* ==================== 公共API实现 ==================== */

esp_err_t ac_msg_queue_init(ac_msg_send_callback_t send_cb, ac_msg_send_complete_callback_t complete_cb) {
    if (send_cb == NULL) {
        ESP_LOGE(TAG, "Send callback cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 重置队列状态
    msg_queue_ctx.head = 0;
    msg_queue_ctx.tail = 0;
    msg_queue_ctx.count = 0;
    msg_queue_ctx.send_state = AC_SEND_STATE_IDLE;
    msg_queue_ctx.send_cb = send_cb;
    msg_queue_ctx.complete_cb = complete_cb;
    msg_queue_ctx.initialized = true;
    
    // 清空队列和当前消息
    memset(msg_queue_ctx.queue, 0, sizeof(msg_queue_ctx.queue));
    memset(&msg_queue_ctx.current_msg, 0, sizeof(msg_queue_ctx.current_msg));
    
    ESP_LOGI(TAG, "Message queue initialized successfully");
    return ESP_OK;
}

esp_err_t ac_msg_queue_enqueue(ac_msg_type_t msg_type, uint16_t server_addr, uint8_t value) {
    if (!msg_queue_ctx.initialized) {
        ESP_LOGE(TAG, "Message queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (msg_queue_ctx.count >= AC_MSG_QUEUE_SIZE) {
        ESP_LOGW(TAG, "Message queue full, dropping message type %d to 0x%04x", msg_type, server_addr);
        return ESP_ERR_NO_MEM;
    }
    
    bool queue_was_empty = (msg_queue_ctx.count == 0);
    
    msg_queue_ctx.queue[msg_queue_ctx.tail].msg_type = msg_type;
    msg_queue_ctx.queue[msg_queue_ctx.tail].server_addr = server_addr;
    msg_queue_ctx.queue[msg_queue_ctx.tail].value = value;
    msg_queue_ctx.queue[msg_queue_ctx.tail].timestamp = _get_current_timestamp();
    
    msg_queue_ctx.tail = (msg_queue_ctx.tail + 1) % AC_MSG_QUEUE_SIZE;
    msg_queue_ctx.count++;
    
    ESP_LOGD(TAG, "Enqueued message type %d to 0x%04x, queue size: %d", msg_type, server_addr, msg_queue_ctx.count);
    
    // 只有当队列之前为空且当前没有消息在发送时，才立即处理
    if (queue_was_empty && msg_queue_ctx.send_state == AC_SEND_STATE_IDLE) {
        ESP_LOGD(TAG, "Queue was empty, processing message immediately");
        _process_next_message();
    } else {
        ESP_LOGD(TAG, "Message queued, waiting for current transmission to complete");
    }
    
    return ESP_OK;
}

void ac_msg_queue_send_complete(bool success) {
    if (msg_queue_ctx.send_state != AC_SEND_STATE_SENDING) {
        ESP_LOGW(TAG, "Received send complete but not in sending state (%d)", msg_queue_ctx.send_state);
        return;
    }
    
    msg_queue_ctx.send_state = AC_SEND_STATE_IDLE;
    
    // 调用完成回调
    if (msg_queue_ctx.complete_cb) {
        esp_err_t result = success ? ESP_OK : ESP_FAIL;
        msg_queue_ctx.complete_cb(&msg_queue_ctx.current_msg, result);
    }
    
    ESP_LOGD(TAG, "Message send completed (%s), processing next message in queue", 
             success ? "success" : "failed");
    
    // 处理下一个消息
    _process_next_message();
}

void ac_msg_queue_send_timeout(void) {
    if (msg_queue_ctx.send_state != AC_SEND_STATE_SENDING) {
        ESP_LOGW(TAG, "Received send timeout but not in sending state (%d)", msg_queue_ctx.send_state);
        return;
    }
    
    msg_queue_ctx.send_state = AC_SEND_STATE_IDLE;
    
    // 调用完成回调，传递超时错误
    if (msg_queue_ctx.complete_cb) {
        msg_queue_ctx.complete_cb(&msg_queue_ctx.current_msg, ESP_ERR_TIMEOUT);
    }
    
    ESP_LOGD(TAG, "Message send timeout, processing next message in queue");
    
    // 处理队列中的下一个消息
    _process_next_message();
}

ac_send_state_t ac_msg_queue_get_send_state(void) {
    return msg_queue_ctx.send_state;
}

uint8_t ac_msg_queue_get_size(void) {
    return msg_queue_ctx.count;
}

void ac_msg_queue_clear(void) {
    msg_queue_ctx.head = 0;
    msg_queue_ctx.tail = 0;
    msg_queue_ctx.count = 0;
    msg_queue_ctx.send_state = AC_SEND_STATE_IDLE;
    ESP_LOGI(TAG, "Message queue cleared");
}

/* 精简版本 - 移除了不必要的API函数，保持接口简洁高效 */ 