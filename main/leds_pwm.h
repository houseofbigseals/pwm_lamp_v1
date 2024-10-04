#ifndef MYLEDSPWMLIB
#define MYLEDSPWMLIB

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"
#include "esp_err.h"



// ledc params for our 4 channels
#define LEDC_LS_TIMER          LEDC_TIMER_1
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_LS_CH0_GPIO       (10)
#define LEDC_LS_CH0_CHANNEL    LEDC_CHANNEL_0
#define LEDC_LS_CH1_GPIO       (11)
#define LEDC_LS_CH1_CHANNEL    LEDC_CHANNEL_1
#define LEDC_LS_CH2_GPIO       (12)
#define LEDC_LS_CH2_CHANNEL    LEDC_CHANNEL_2
#define LEDC_LS_CH3_GPIO       (13)
#define LEDC_LS_CH3_CHANNEL    LEDC_CHANNEL_3

#define LEDC_TEST_CH_NUM       (4)
#define MAX_PWM_DUTY 8192   /// because LEDC_TIMER_13_BIT

// function declarations
void init_led_pwm();
uint8_t set_channel_pwm(uint8_t channel, uint8_t percent);


// global array with channel configs
const ledc_channel_config_t global_ledc_channel[LEDC_TEST_CH_NUM ] = 
{
    {
        .channel    = LEDC_LS_CH0_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH0_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    },
    {
        .channel    = LEDC_LS_CH1_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH1_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    },

    {
        .channel    = LEDC_LS_CH2_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH2_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    },
    {
        .channel    = LEDC_LS_CH3_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH3_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    },
};



// func to init pwm timer and configs
void init_led_pwm()
{

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
        .freq_hz = 4000,                      // frequency of PWM signal
        .speed_mode = LEDC_LS_MODE,           // timer mode
        .timer_num = LEDC_LS_TIMER,            // timer index
        .clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
    };
    // Set configuration of timer0 for high speed channels
    ledc_timer_config(&ledc_timer);

    /*
    global_ledc_channel[0] = (ledc_channel_config_t){
        .channel    = LEDC_LS_CH0_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH0_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    };
    
    global_ledc_channel[1] = (ledc_channel_config_t){
        .channel    = LEDC_LS_CH1_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH1_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    };

    global_ledc_channel[2] = (ledc_channel_config_t){
        .channel    = LEDC_LS_CH2_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH2_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    };

    global_ledc_channel[3] = (ledc_channel_config_t){
        .channel    = LEDC_LS_CH3_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH3_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    };*/
    //ESP_ERROR_CHECK(ledc_channel_config(global_ledc_channel));
    for (int ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
        ESP_ERROR_CHECK(ledc_channel_config(&global_ledc_channel[ch]));
    }
    //return ledc_channel;
    //ledc_fade_func_install();
}



// Define error codes as an enum
typedef enum {
    PWM_OK = 0,
    ERR_INVALID_CHANNEL_NUM = 100,
    ERR_INVALID_DUTY_VALUE = 200,
    ERR_UNKNOWN_ERROR = 500
} pwm_error_code;

// Function to get error message by code using switch-case
const char* get_pwm_error_message(pwm_error_code code) {
    switch (code) {
        case PWM_OK:
            return "PWM_OPERATION_SUCCESS\n";
        case ERR_INVALID_CHANNEL_NUM:
            return "ERR_INVALID_CHANNEL_NUM\n";
        case ERR_INVALID_DUTY_VALUE:
            return "ERR_INVALID_DUTY_VALUE\n";
        case ERR_UNKNOWN_ERROR:
            return "ERR_UNKNOWN_ERROR\n";
        default:
            return "Error code not found\n";
    }
}


// func to change pwm value of selected value
// channels - from 0 to 3
// values - from 0 to 100
uint8_t set_channel_pwm(uint8_t channel, uint8_t percent)
{
    uint32_t max_val = 8190;  // 8192 because of 13 bit pwm
    //uint32_t res_pwm = 0;
    if (channel >= LEDC_TEST_CH_NUM)
    {
        ESP_LOGE("ledc", "Channel num must be from 0 to 3");
        return ERR_INVALID_CHANNEL_NUM;  // code for incorrect_channel_num
    }
    if (percent > 100)
    {
        ESP_LOGE("ledc", "PWM duty must be from 0 to 100");
        return ERR_INVALID_DUTY_VALUE;
    }
    else
    {
        uint32_t res_pwm = (uint32_t)max_val*percent/100;

        //ESP_LOGI("ledc", "Requested update - PWM channel: %d, duty: %lu", channel, res_pwm);
        //uint32_t max_duty = ledc_get_max_duty(global_ledc_channel[channel].speed_mode, global_ledc_channel[channel].channel);
        //ESP_LOGI("ledc", "max_duty: %lu", max_duty);
        ESP_ERROR_CHECK(ledc_set_duty(global_ledc_channel[channel].speed_mode, global_ledc_channel[channel].channel, res_pwm));
        // Update duty to apply the new value
        ESP_ERROR_CHECK(ledc_update_duty(global_ledc_channel[channel].speed_mode, global_ledc_channel[channel].channel));
        //global_ledc_channel[channel].duty
        // FUCK!
        //ESP_ERROR_CHECK(ledc_set_duty_and_update(global_ledc_channel[channel].speed_mode, global_ledc_channel[channel].channel, res_pwm, 0));
        vTaskDelay( pdMS_TO_TICKS(10) );
        uint32_t real_duty = ledc_get_duty(global_ledc_channel[channel].speed_mode, global_ledc_channel[channel].channel);
        ESP_LOGI("ledc", "Set new pwm duty: channel - %d, value - %lu", channel, real_duty);

        return PWM_OK;
    }
}

#endif