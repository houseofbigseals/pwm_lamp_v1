#ifndef MYRELAYHANDLER
#define MYRELAYHANDLER

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <esp_log.h>

#define GPIO_OUTPUT_PIN GPIO_NUM_17 
#define DEFAULT_WAITING_TIME_MS  (36 * 60 * 60 * 1000)  // 32 hours in milliseconds
#define DEFAULT_ACTIVE_TIME_MS   (6 * 1000) // (4 * 1000)  
//static const char *TAG = "relay";

typedef struct {
    uint32_t waiting_time_ms;
    uint32_t active_time_ms;
} gpio_toggle_config_t;

// queue for time params for relay toggle task
QueueHandle_t gpio_toggle_queue;

// FreeRTOS task to toggle the GPIO pin
void gpio_toggle_task(void *pvParameter)
{

    gpio_toggle_config_t config;
    config.waiting_time_ms = DEFAULT_WAITING_TIME_MS;
    config.active_time_ms = DEFAULT_ACTIVE_TIME_MS;

    while (1)
    {
        // Check if there is a new configuration in the queue
        if (xQueueReceive(gpio_toggle_queue, &config, 0) == pdTRUE)
        {
            ESP_LOGI("relay", "New timing configuration received: waiting_time = %lu ms, active_time = %lu ms\n", config.waiting_time_ms, config.active_time_ms);
        }

        // Wait for the configured waiting time
        vTaskDelay(config.waiting_time_ms / portTICK_PERIOD_MS);

        // Set the GPIO pin on
        gpio_set_level(GPIO_OUTPUT_PIN, 0);

        // Wait for the configured active time
        vTaskDelay(config.active_time_ms / portTICK_PERIOD_MS);

        // Set the GPIO pin off
        gpio_set_level(GPIO_OUTPUT_PIN, 1);
    }
}


#endif