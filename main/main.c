/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_wireguard.h"
//#include "sync_time.h"
//#include "http_server_s.h"
//#include "relay_handler.h"
#include "rom/ets_sys.h"
//#include "dht11_handler.h"
#include "esp_sntp.h"


//static const char *TAG = "flower_keeper";
static wireguard_config_t wg_config = ESP_WIREGUARD_CONFIG_DEFAULT();


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "protocol_examples_utils.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cJSON.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include "nvs_flash.h"
//#include "esp_eth.h"
#include "mdns.h"
#include "leds_pwm.h"
#include "onewire_bus_impl_rmt.h"
#include "ds18b20.h"
    
    
#define EXAMPLE_ONEWIRE_BUS_GPIO    14
#define EXAMPLE_ONEWIRE_MAX_DS18B20 2
#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)

cJSON *response_json;

const char* onewire_tag = "onewire";
const char* led_tag = "leds";
static const char *TAG = "pwm_lamp";
uint8_t led1 = GPIO_NUM_21;   // default debug led on pwm led lamp board

// i-wire global variables
// install 1-wire bus
onewire_bus_handle_t bus = NULL;
onewire_bus_config_t bus_config = {
    .bus_gpio_num = EXAMPLE_ONEWIRE_BUS_GPIO,
};

int ds18b20_device_num = 0;
ds18b20_device_handle_t ds18b20s[EXAMPLE_ONEWIRE_MAX_DS18B20];
ds18b20_device_handle_t pcb_sensor;  // 96092426B912FF28
ds18b20_device_handle_t lamp_sensor;
onewire_device_iter_handle_t iter = NULL;
onewire_device_t next_onewire_device;

typedef struct device_state{
    int ch0_pwm;
    int ch1_pwm;
    int ch2_pwm;
    int ch3_pwm;
    float pcb_temp;
    float lamp_temp;
    int64_t uptime;

}device_state;

// Global device state
device_state global_device_state;

// Global mutex for thread safety
SemaphoreHandle_t device_state_mutex;



// Function to create cJSON object from device_state struct with thread safety
cJSON *create_json_from_device_state_safe() {
    // Try to take the mutex before accessing the shared data
    if (xSemaphoreTake(device_state_mutex, portMAX_DELAY) == pdTRUE) {
        // Create the root cJSON object
        cJSON *json = cJSON_CreateObject();
        if (json == NULL) {
            // Release the mutex before returning
            xSemaphoreGive(device_state_mutex);
            return NULL;
        }
        
        // Add fields from the device_state struct to the cJSON object
        cJSON_AddNumberToObject(json, "ch0_pwm", global_device_state.ch0_pwm);
        cJSON_AddNumberToObject(json, "ch1_pwm", global_device_state.ch1_pwm);
        cJSON_AddNumberToObject(json, "ch2_pwm", global_device_state.ch2_pwm);
        cJSON_AddNumberToObject(json, "ch3_pwm", global_device_state.ch3_pwm);
        cJSON_AddNumberToObject(json, "pcb_temp", global_device_state.pcb_temp);
        cJSON_AddNumberToObject(json, "lamp_temp", global_device_state.lamp_temp);
        cJSON_AddNumberToObject(json, "uptime", (double)global_device_state.uptime); // Cast to double for int64_t
        
        // Release the mutex after accessing the shared data
        xSemaphoreGive(device_state_mutex);
        
        // Return the created cJSON object
        return json;
    } else {
        // Failed to take the mutex (unlikely if portMAX_DELAY is used)
        return NULL;
    }
}


void init_ds18b20()
{
    // https://esp-idf-lib.readthedocs.io/en/latest/groups/onewire.html
    // https://esp-idf-lib.readthedocs.io/en/latest/groups/ds18x20.html

    // example fully stolen from https://components.espressif.com/components/espressif/ds18b20/versions/0.1.1
    // but to work we need to include 
    // #include "onewire_bus_impl_rmt.h"
    // #include "ds18b20.h"

    bool built_in_found = false;

    onewire_bus_rmt_config_t rmt_config = {
    .max_rx_bytes = 10, // 1byte ROM command + 8byte ROM number + 1byte device command
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));
    esp_err_t search_result = ESP_OK;

    // create 1-wire device iterator, which is used for device search
    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    ESP_LOGI(onewire_tag, "Device iterator created, start searching...");
    do {
        search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (search_result == ESP_OK) { // found a new device, let's check if we can upgrade it to a DS18B20
            ds18b20_config_t ds_cfg = {};
            // check if the device is a DS18B20, if so, return the ds18b20 handle
            if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &ds18b20s[ds18b20_device_num]) == ESP_OK) {
                ESP_LOGI(onewire_tag, "Found a DS18B20[%d], address: %016llX", ds18b20_device_num, next_onewire_device.address);
                ds18b20_device_num++;

            } else {
                ESP_LOGI(onewire_tag, "Found an unknown device, address: %016llX", next_onewire_device.address);
            }
        }
    } while (search_result != ESP_ERR_NOT_FOUND);
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    ESP_LOGI(onewire_tag, "Searching done, %d DS18B20 device(s) found", ds18b20_device_num);

    // Now you have the DS18B20 sensor handle, you can use it to read the temperature
    /*if (CONFIG_PCB_SENSOR_ADDR == "")
    {
        pcb_sensor = ds18b20s[0];
    }
                    if ((!buil_in_found)&&(CONFIG_PCB_SENSOR_ADDR == "unknown"))    // REMOVE IT ON CODE FOR PRODUCTION PCB
                {
                    // it means that it is test run to check addr of built-in sensor address
                    uint64_t builtin_addr = 

                }
    */
}

// to get data from 0 device
float read_pcb_temp()
{
    float temperature = 0;
    ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(ds18b20s[0]));
    ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20s[0], &temperature));
    ESP_LOGI(TAG, "temperature read from built-in DS18B20: %.2fC", temperature);
    return temperature;
}

// just to show data from all sensors to uart
void test_read_ds18b20_data()
{
    float temperature = 0;
    for (int i = 0; i < ds18b20_device_num; i ++) {
        ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(ds18b20s[i]));
        ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20s[i], &temperature));
        ESP_LOGI(TAG, "temperature read from DS18B20[%d]: %.2fC", i, temperature);
    }
}


// Initialize mDNS service
void start_mdns_service(void) {
    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_init());

    // Set the hostname for the device
    const char* name = CONFIG_MY_MDNS_NAME;
    ESP_ERROR_CHECK(mdns_hostname_set(name));
    ESP_LOGI("mDNS", "mDNS hostname set to: %s.local", name);

    // Set an optional instance name
    ESP_ERROR_CHECK(mdns_instance_name_set(name));

    // Advertise a service over mDNS (HTTP service on port 80)
    ESP_ERROR_CHECK(mdns_service_add(name, "_http", "_tcp", 80, NULL, 0));
    ESP_LOGI("mDNS", "mDNS service added: http://%s.local/hello", name);
}

/*
// Function to create the JSON response
static char* create_json_response(struct dht11_reading reading)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "status", reading.status);
    cJSON_AddNumberToObject(root, "temperature", reading.temperature);
    cJSON_AddNumberToObject(root, "humidity", reading.humidity);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    return json_str;
}
*/

/* An HTTP GET handler */
static esp_err_t info_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    //const char* resp_str = "Hello World!" ; //(const char*) req->user_ctx;
    //httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    // calculate current time
    //int64_t current_time = esp_timer_get_time()/1000000;  // time from last boot in microseconds -> seconds

    // update temperature from sensors
    // for now we cannot read data from second sensor
    if (xSemaphoreTake(device_state_mutex, portMAX_DELAY) == pdTRUE) {
        global_device_state.uptime = esp_timer_get_time()/1000000; // Cast to double for int64_t
        global_device_state.pcb_temp = read_pcb_temp();  
        // Release the mutex after accessing the shared data
        xSemaphoreGive(device_state_mutex);
    }
    
    // Create JSON from device state safely
    response_json = create_json_from_device_state_safe();

    // Convert cJSON object to string and print it
    if (response_json != NULL) {
        char *json_string = cJSON_Print(response_json);
        cJSON_Delete(response_json);
        //ESP_LOGI(TAG,"current device state was requested:\n %s\n", json_string);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));

        // Cleanup
        free(json_string);

    } else {
        ESP_LOGI(TAG,"Failed to create JSON!\n");
    }
    
    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

static const httpd_uri_t info = {
    .uri       = "/info",
    .method    = HTTP_GET,
    .handler   = info_get_handler,
    .user_ctx  = NULL 
};




static void handle_leds(int led_num_value, int state_value)
{
    if (led_num_value == 1)
    {
        gpio_set_level(led1, state_value);
        ESP_LOGI(led_tag, "Setting led %d in state %d", led_num_value, state_value);
    }
    /*else if (led_num_value == 2)
    {
        gpio_set_level(led2, state_value);
        ESP_LOGI(led_tag, "Setting led %d in state %d", led_num_value, state_value);
    }*/
    else
    {
        ESP_LOGI(led_tag, "No such led!");
    }
}


// device can be controlled via POST request like that
// curl -X POST http://esp32_pwm_lamp_0.local/pwm -H 'Content-Type: application/json' -d '{"channel":0,"duty":100}'


static esp_err_t pwm_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret = 0;
    int remaining = req->content_len;

    // Read the data from the request
    while (remaining > 0) {
        // This will copy the request body into the buffer
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                // Retry receiving if timeout occurred
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }

    // Null-terminate the buffer
    buf[ret] = '\0';
    ESP_LOGI(TAG, "Received: %s", buf);

    // Parse the JSON data
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        char* resp = "RESULT: ERR_INCORRECT_JSON\n";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }

    // Extract the "led_num" value
    cJSON *channel = cJSON_GetObjectItem(json, "channel");
    if (!cJSON_IsNumber(channel)) {
        ESP_LOGE(TAG, "Invalid channel");
        char* resp = "RESULT: ERR_INVALID_CHANNEL_NUM\n";
        httpd_resp_send(req, resp, strlen(resp));
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Extract the "state" value
    cJSON *duty = cJSON_GetObjectItem(json, "duty");
    if (!cJSON_IsNumber(duty)) {
        ESP_LOGE(TAG, "Invalid duty");
        char* resp = "RESULT: ERR_INVALID_DUTY_VALUE\n";
        httpd_resp_send(req, resp, strlen(resp));
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Get the values
    int channel_value = channel->valueint;
    int duty_value = duty->valueint;

    ESP_LOGI(TAG, "Requested update - PWM channel: %d, duty: %d", channel_value, duty_value);

    // Work with the values - for now just here
    // in future - using queue
    
    char resp[60];
    uint err_code = set_channel_pwm(channel_value, duty_value);
    if (err_code == PWM_OK)
    {
        uint32_t  real_duty = ledc_get_duty(global_ledc_channel[channel_value].speed_mode, global_ledc_channel[channel_value].channel)*100/MAX_PWM_DUTY  + 1;
        // update global state struct
        if (xSemaphoreTake(device_state_mutex, portMAX_DELAY) == pdTRUE) 
        {
            global_device_state.uptime = esp_timer_get_time()/1000000; // mks to s

            switch(channel_value)
            {
            case 0:
                global_device_state.ch0_pwm = real_duty;
                break;
            case 1:
                global_device_state.ch1_pwm = real_duty;
                break;
            case 2:
                global_device_state.ch2_pwm = real_duty;
                break;
            case 3:
                global_device_state.ch3_pwm = real_duty;
                break;
            }
            // Release the mutex after accessing the shared data
            xSemaphoreGive(device_state_mutex);
        }

        snprintf(resp, 60, "RESULT: %s; Set channel %d to %lu %% pwm\n", get_pwm_error_message(err_code), channel_value, real_duty);
        httpd_resp_send(req, resp, strlen(resp));

    }
    else
    {   //do we ever get here? 
        snprintf(resp, 60, "RESULT: %s;", get_pwm_error_message(err_code));
        httpd_resp_send(req, resp, strlen(resp));
    }

    // Clean up
    cJSON_Delete(json);

    return ESP_OK;
}


static const httpd_uri_t pwm = {
    .uri       = "/pwm",
    .method    = HTTP_POST,
    .handler   = pwm_post_handler,
    .user_ctx  = NULL
};


// curl -X POST http://esp32_relay_1.local/reset -H 'Content-Type: text/html' -d 'force_reset'
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret = 0;
    int remaining = req->content_len;

    // Read the data from the request
    while (remaining > 0) {
        // This will copy the request body into the buffer
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                // Retry receiving if timeout occurred
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }

    // Null-terminate the buffer
    buf[ret] = '\0';
    ESP_LOGI(TAG, "Received: %s", buf);

    // check if it is real desire to reset
    const char* reset_command = "force_reset";
    if (strcmp(buf, reset_command) == 0)
    {
        ESP_LOGI(TAG, "User have sent force reset command, so rebooting");
        char* resp = "User have sent force reset command, so rebooting\n";
        httpd_resp_send(req, resp, strlen(resp));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        // make reset
        esp_restart();
        return ESP_OK;
    }
    else
    {
        ESP_LOGI(TAG, "User have sent incorrect reset command, so no rebooting");
        char* resp = "User have sent incorrect reset command, so no rebooting\n";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }
}

static const httpd_uri_t reset = {
    .uri       = "/reset",
    .method    = HTTP_POST,
    .handler   = reset_post_handler,
    .user_ctx  = NULL
};


static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &info);
        //httpd_register_uri_handler(server, &echo);
        httpd_register_uri_handler(server, &reset);
        httpd_register_uri_handler(server, &pwm);
        #if CONFIG_EXAMPLE_BASIC_AUTH
        httpd_register_basic_auth(server);
        #endif
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

#if !CONFIG_IDF_TARGET_LINUX
static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}
#endif // !CONFIG_IDF_TARGET_LINUX








void app_main(void)
{
    // init ledc pwm timers and configs
    init_led_pwm();
    

    // search and init ds18b20 devices
    init_ds18b20();
    test_read_ds18b20_data();

    // fill device_state with default values
    global_device_state.ch0_pwm = 0;
    global_device_state.ch1_pwm = 0;
    global_device_state.ch2_pwm = 0;
    global_device_state.ch3_pwm = 0;
    global_device_state.pcb_temp = 0;
    global_device_state.lamp_temp = 0;
    global_device_state.uptime = esp_timer_get_time()/1000000; // mks to seconds

    // Create the mutex before using it
    device_state_mutex = xSemaphoreCreateMutex();

    // Create the queue to hold relay timing configurations
    //gpio_toggle_queue = xQueueCreate(1, sizeof(gpio_toggle_config_t));
    //dht11_queue = xQueueCreate(1, sizeof(struct dht11_reading));
    // init dht11
    //DHT11_init(GPIO_NUM_4);
    // Create the DHT11 reading task
    //xTaskCreatePinnedToCore(&dht11_task, "dht11_task", 2048, NULL, tskIDLE_PRIORITY, NULL, 1);

    /*if (dht11_queue == NULL)
    {
        ESP_LOGI(TAG, "Failed to create dht11 queue\n");
    }*/

    // Create the relay toggle task
    //xTaskCreatePinnedToCore(&gpio_toggle_task, "gpio_toggle_task", 2048, NULL, tskIDLE_PRIORITY, NULL, 1);

    esp_err_t err;
    //time_t now;
    //struct tm timeinfo;
    //char strftime_buf[64];
    wireguard_ctx_t ctx = {0};

    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // start mdns server on all default network interfaces
    // https://docs.espressif.com/projects/esp-protocols/mdns/docs/latest/en/index.html
    start_mdns_service();

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());


    // if we need some work with netif we can do
    //esp_netif_t* default_netif_copy = esp_netif_get_default_netif();

    vTaskDelay( pdMS_TO_TICKS(10) );
    ESP_LOGI(TAG, "1");

    // turn on led 1 to indicate device activity
    gpio_set_direction(led1, GPIO_MODE_OUTPUT);
    gpio_set_level(led1, 1);

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    // very important time sync with sntp  ???
    // https://github.com/trombik/esp_wireguard/issues/29#issuecomment-1331375949
    // https://docs.espressif.com/projects/esp-idf/en/v4.4.3/esp32/api-reference/system/system_time.html#sntp-time-synchronization
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    // NECESSARITY OF USING SNTP SYNC IS VERY UNCLEAR
    // I REALLY HATE THIS LIB

    /* Start the server for the first time */
    server = start_webserver();


    // init wireguard
    ESP_LOGI(TAG, "Initializing WireGuard.");
    wg_config.private_key = CONFIG_WG_PRIVATE_KEY;
    wg_config.listen_port = CONFIG_WG_LOCAL_PORT;
    wg_config.public_key = CONFIG_WG_PEER_PUBLIC_KEY;
    if (strcmp(CONFIG_WG_PRESHARED_KEY, "") != 0) {
        wg_config.preshared_key = CONFIG_WG_PRESHARED_KEY;
    } else {
        wg_config.preshared_key = NULL;
    }
    wg_config.allowed_ip = CONFIG_WG_LOCAL_IP_ADDRESS;
    wg_config.allowed_ip_mask = CONFIG_WG_LOCAL_IP_NETMASK;
    wg_config.endpoint = CONFIG_WG_PEER_ADDRESS;
    wg_config.port = CONFIG_WG_PEER_PORT;
    wg_config.persistent_keepalive = CONFIG_WG_PERSISTENT_KEEP_ALIVE;

    ESP_ERROR_CHECK(esp_wireguard_init(&wg_config, &ctx));

    ESP_LOGI(TAG, "Connecting to the peer.");

    // https://github.com/trombik/esp_wireguard/issues/33
    /*
    TODO:
    So if we activate CONFIG_ESP_NETIF_BRIDGE_EN or CONFIG_LWIP_PPP_SUPPORT in idf.py menuconfig, it will fix the problem.
    For a test, I tried activating "Enable PPP support (new/experimental)" (Name: LWIP_PPP_SUPPORT), and it works.
    So in short, activate CONFIG_LWIP_PPP_SUPPORT in your project configuration in ESP-IDF-v5, recompile your project, and the wireguard tunnel will work as intended.
    */
    ESP_ERROR_CHECK(esp_wireguard_connect(&ctx));

    /*
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wireguard_setup: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Halting due to error");
        while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }*/

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        err = esp_wireguardif_peer_is_up(&ctx);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Peer is up");
            break;
        } else {
            ESP_LOGI(TAG, "Peer is down");
        }
    }

    ESP_LOGI(TAG, "Peer is up");
    esp_wireguard_set_default(&ctx);



    while (server) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
