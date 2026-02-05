#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_http_server.h"
#include "esp_spiffs.h"

#include "driver/gpio.h"
#include "led_strip.h"

#define AP_SSID "Poofer-AP"
#define AP_PASS "FlameoHotMan"
#define AP_MAX_CONN 4

#define WS_URI "/ws"
#define MAX_HOLD_MS 3000
#define MIN_HOLD_MS 250
#define SOLENOID_KICK_MS 50
#define SOLENOID_HOLD_LEVEL 5

#define STATUS_LED_INDEX 0
#define SOLENOID_PIXEL_INDEX 1

#define GPIO_NEOPIXEL GPIO_NUM_4

#define TAG "poofer"

typedef enum {
    STATE_BOOT = 0,
    STATE_READY,
    STATE_FIRING,
    STATE_ERROR,
} system_state_t;

typedef struct {
    system_state_t state;
    bool press_active;
    bool press_ignore_until_release;
    bool release_pending;
    int64_t press_start_us;
    uint32_t last_hold_ms;
    int64_t last_ws_rx_us;
    uint8_t solenoid_level;
} runtime_state_t;

static led_strip_handle_t strip = NULL;
static httpd_handle_t httpd = NULL;
static int ws_client_fd = -1;
static runtime_state_t runtime = {
    .state = STATE_BOOT,
    .press_active = false,
    .press_ignore_until_release = false,
    .release_pending = false,
    .press_start_us = 0,
    .last_hold_ms = MIN_HOLD_MS,
    .last_ws_rx_us = 0,
    .solenoid_level = 0,
};
static SemaphoreHandle_t state_lock;
static esp_timer_handle_t max_hold_timer;
static esp_timer_handle_t min_hold_timer;
static esp_timer_handle_t solenoid_kick_timer;

static void set_pixels(uint8_t status_r, uint8_t status_g, uint8_t status_b, uint8_t sol_r,
                       uint8_t sol_g, uint8_t sol_b) {
    if (!strip) {
        return;
    }
    led_strip_set_pixel(strip, STATUS_LED_INDEX, status_r, status_g, status_b);
    led_strip_set_pixel(strip, SOLENOID_PIXEL_INDEX, sol_r, sol_g, sol_b);
    led_strip_refresh(strip);
}

static void set_status_pixel(uint8_t status_r, uint8_t status_g, uint8_t status_b) {
    if (!strip) {
        return;
    }
    led_strip_set_pixel(strip, STATUS_LED_INDEX, status_r, status_g, status_b);
    led_strip_refresh(strip);
}

static void set_solenoid_level_locked(uint8_t level) {
    runtime.solenoid_level = level;
    if (!strip) {
        return;
    }
    led_strip_set_pixel(strip, SOLENOID_PIXEL_INDEX, 0, level, 0);
    led_strip_refresh(strip);
}

static void update_status_led_locked(void) {
    switch (runtime.state) {
    case STATE_BOOT:
        set_status_pixel(0, 0, 32); // dim blue
        break;
    case STATE_READY:
        set_status_pixel(0, 64, 0); // green
        break;
    case STATE_FIRING:
        set_status_pixel(64, 16, 0); // orange
        break;
    case STATE_ERROR:
    default:
        set_status_pixel(64, 0, 0); // red
        break;
    }
}

static uint32_t clamp_hold_ms(uint32_t hold_ms) {
    if (hold_ms < MIN_HOLD_MS) {
        return MIN_HOLD_MS;
    }
    if (hold_ms > MAX_HOLD_MS) {
        return MAX_HOLD_MS;
    }
    return hold_ms;
}

static uint32_t current_elapsed_ms_locked(void) {
    if (!runtime.press_active) {
        return 0;
    }
    int64_t now = esp_timer_get_time();
    int64_t diff = now - runtime.press_start_us;
    if (diff < 0) {
        return 0;
    }
    return (uint32_t)(diff / 1000);
}

static void send_state_async(void) {
    if (!httpd || ws_client_fd < 0) {
        return;
    }

    char payload[192];
    bool ready = false;
    bool firing = false;
    bool error = false;
    uint32_t elapsed = 0;
    uint32_t last_hold = 0;

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        ready = (runtime.state == STATE_READY || runtime.state == STATE_FIRING);
        firing = (runtime.state == STATE_FIRING);
        error = (runtime.state == STATE_ERROR);
        elapsed = current_elapsed_ms_locked();
        last_hold = runtime.last_hold_ms;
        xSemaphoreGive(state_lock);
    }

    int len = snprintf(payload, sizeof(payload),
                       "{\"ready\":%s,\"firing\":%s,\"error\":%s,\"elapsed_ms\":%" PRIu32
                       ",\"last_hold_ms\":%" PRIu32 "}",
                       ready ? "true" : "false", firing ? "true" : "false",
                       error ? "true" : "false", elapsed, last_hold);
    if (len <= 0 || len >= (int)sizeof(payload)) {
        return;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)payload,
        .len = (size_t)len,
    };
    httpd_ws_send_frame_async(httpd, ws_client_fd, &frame);
}

static void stop_firing_locked(system_state_t next_state) {
    runtime.press_active = false;
    runtime.release_pending = false;
    runtime.state = next_state;
    set_solenoid_level_locked(0);
    update_status_led_locked();
}

static void start_firing_locked(void) {
    runtime.state = STATE_FIRING;
    runtime.press_active = true;
    runtime.release_pending = false;
    runtime.press_start_us = esp_timer_get_time();
    update_status_led_locked();
    set_solenoid_level_locked(255);
}

static void max_hold_timer_cb(void* arg) {
    (void)arg;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (runtime.press_active) {
        runtime.last_hold_ms = MAX_HOLD_MS;
        runtime.press_active = false;
        runtime.press_ignore_until_release = true;
        runtime.state = STATE_READY;
        set_solenoid_level_locked(0);
        update_status_led_locked();
    }

    xSemaphoreGive(state_lock);
    send_state_async();
}

static void min_hold_timer_cb(void* arg) {
    (void)arg;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (runtime.press_active && runtime.release_pending) {
        stop_firing_locked(STATE_READY);
    }

    xSemaphoreGive(state_lock);
    send_state_async();
}

static void solenoid_kick_timer_cb(void* arg) {
    (void)arg;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (runtime.state == STATE_FIRING) {
        set_solenoid_level_locked(SOLENOID_HOLD_LEVEL);
    }

    xSemaphoreGive(state_lock);
}

static void handle_press_down(void) {
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (runtime.state == STATE_ERROR || runtime.press_active ||
        runtime.press_ignore_until_release) {
        xSemaphoreGive(state_lock);
        return;
    }

    start_firing_locked();

    esp_timer_stop(max_hold_timer);
    esp_timer_start_once(max_hold_timer, (uint64_t)MAX_HOLD_MS * 1000ULL);

    esp_timer_stop(solenoid_kick_timer);
    esp_timer_start_once(solenoid_kick_timer, (uint64_t)SOLENOID_KICK_MS * 1000ULL);

    xSemaphoreGive(state_lock);
    send_state_async();
}

static void handle_press_up(void) {
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (runtime.press_ignore_until_release) {
        runtime.press_ignore_until_release = false;
    }

    if (!runtime.press_active) {
        xSemaphoreGive(state_lock);
        return;
    }

    int64_t now = esp_timer_get_time();
    uint32_t held_ms = 0;
    if (now > runtime.press_start_us) {
        held_ms = (uint32_t)((now - runtime.press_start_us) / 1000);
    }

    runtime.last_hold_ms = clamp_hold_ms(held_ms);

    if (held_ms < MIN_HOLD_MS) {
        runtime.release_pending = true;
        esp_timer_stop(min_hold_timer);
        esp_timer_start_once(min_hold_timer, (uint64_t)(MIN_HOLD_MS - held_ms) * 1000ULL);
        xSemaphoreGive(state_lock);
        send_state_async();
        return;
    }

    stop_firing_locked(STATE_READY);

    esp_timer_stop(max_hold_timer);
    esp_timer_stop(min_hold_timer);

    xSemaphoreGive(state_lock);
    send_state_async();
}

static void handle_ws_message(const char* msg) {
    if (!msg) {
        return;
    }

    int64_t now = esp_timer_get_time();
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        runtime.last_ws_rx_us = now;
        xSemaphoreGive(state_lock);
    }

    if (strcmp(msg, "DOWN") == 0) {
        handle_press_down();
    } else if (strcmp(msg, "UP") == 0) {
        handle_press_up();
    } else if (strcmp(msg, "PING") == 0) {
        send_state_async();
    }
}

static esp_err_t ws_handler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        ws_client_fd = httpd_req_to_sockfd(req);
        send_state_async();
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK || frame.len == 0) {
        return err;
    }

    char* buf = calloc(1, frame.len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    frame.payload = (uint8_t*)buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err == ESP_OK) {
        handle_ws_message(buf);
    }

    free(buf);
    return err;
}

static esp_err_t send_file(httpd_req_t* req, const char* path, const char* content_type) {
    FILE* file = fopen(path, "r");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);

    char buffer[512];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
            fclose(file);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }
    fclose(file);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t* req) {
    return send_file(req, "/spiffs/index.html", "text/html");
}

static esp_err_t wifi_get_handler(httpd_req_t* req) {
    return send_file(req, "/spiffs/wifi.html", "text/html");
}

static void url_decode(char* dst, const char* src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
            (isxdigit((int)a) && isxdigit((int)b))) {
            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a -= 'A' - 10;
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b -= 'A' - 10;
            else
                b -= '0';
            *dst++ = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void parse_form_value(const char* body, const char* key, char* out, size_t out_len) {
    if (!body || !key || !out || out_len == 0) {
        return;
    }

    const char* start = strstr(body, key);
    if (!start) {
        return;
    }
    start += strlen(key);
    if (*start != '=') {
        return;
    }
    start++;
    const char* end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= out_len) {
        len = out_len - 1;
    }

    char temp[128];
    if (len >= sizeof(temp)) {
        len = sizeof(temp) - 1;
    }
    memcpy(temp, start, len);
    temp[len] = '\0';
    url_decode(out, temp);
}

static void wifi_store_credentials(const char* ssid, const char* pass) {
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    if (ssid && ssid[0]) {
        nvs_set_str(nvs, "ssid", ssid);
    }
    if (pass) {
        nvs_set_str(nvs, "pass", pass);
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

static bool wifi_load_credentials(char* ssid_out, size_t ssid_len, char* pass_out,
                                  size_t pass_len) {
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t ssid_size = ssid_len;
    size_t pass_size = pass_len;
    esp_err_t ssid_err = nvs_get_str(nvs, "ssid", ssid_out, &ssid_size);
    esp_err_t pass_err = nvs_get_str(nvs, "pass", pass_out, &pass_size);
    nvs_close(nvs);
    return (ssid_err == ESP_OK && ssid_out[0] != '\0') && (pass_err == ESP_OK);
}

static void wifi_connect_sta(void) {
    char ssid[33] = {0};
    char pass[65] = {0};
    if (!wifi_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "No STA credentials stored");
        return;
    }

    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char*)sta_config.sta.password, pass, sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_connect();
}

static esp_err_t wifi_post_handler(httpd_req_t* req) {
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content");
        return ESP_FAIL;
    }

    char* buf = calloc(1, total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, buf, total_len);
    if (received <= 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv fail");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};
    parse_form_value(buf, "ssid", ssid, sizeof(ssid));
    parse_form_value(buf, "pass", pass, sizeof(pass));

    free(buf);

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    wifi_store_credentials(ssid, pass);
    wifi_connect_sta();

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(
        req, "<html><body><h2>Saved. Reconnecting...</h2><a href=\"/\">Back</a></body></html>");
    return ESP_OK;
}

static void start_mdns(void) {
    mdns_init();
    mdns_hostname_set("poofer");
    mdns_instance_name_set("Poofer Controller");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                               void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_connect_sta();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        start_mdns();
        if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (runtime.state == STATE_BOOT) {
                runtime.state = STATE_READY;
                update_status_led_locked();
            }
            xSemaphoreGive(state_lock);
        }
    }
}

static void wifi_init_ap_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
                                        NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
                                        NULL);

    wifi_config_t ap_config = {0};
    strncpy((char*)ap_config.ap.ssid, AP_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(AP_SSID);
    strncpy((char*)ap_config.ap.password, AP_PASS, sizeof(ap_config.ap.password));
    ap_config.ap.max_connection = AP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen(AP_PASS) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    wifi_connect_sta();
}

static httpd_handle_t start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        return NULL;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &index_uri);

    httpd_uri_t wifi_get_uri = {
        .uri = "/wifi",
        .method = HTTP_GET,
        .handler = wifi_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &wifi_get_uri);

    httpd_uri_t wifi_post_uri = {
        .uri = "/wifi",
        .method = HTTP_POST,
        .handler = wifi_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &wifi_post_uri);

    httpd_uri_t ws_uri = {
        .uri = WS_URI,
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_uri);

    return server;
}

static void mount_spiffs(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_vfs_spiffs_register(&conf);
}

static void status_task(void* arg) {
    (void)arg;
    while (true) {
        bool should_send = false;
        bool should_stop = false;
        if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            int64_t now = esp_timer_get_time();
            if (runtime.press_active) {
                int64_t elapsed = now - runtime.press_start_us;
                if (elapsed >= (int64_t)MAX_HOLD_MS * 1000LL) {
                    runtime.last_hold_ms = MAX_HOLD_MS;
                    runtime.press_active = false;
                    runtime.press_ignore_until_release = true;
                    runtime.state = STATE_READY;
                    update_status_led_locked();
                    should_send = true;
                }
            }

            if (runtime.press_active && runtime.last_ws_rx_us != 0) {
                if ((now - runtime.last_ws_rx_us) > 2000000) {
                    stop_firing_locked(STATE_READY);
                    should_stop = true;
                }
            }
            xSemaphoreGive(state_lock);
        }

        if (should_send || should_stop) {
            send_state_async();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void init_led_strip(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_NEOPIXEL,
        .max_leds = 2,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
    update_status_led_locked();
    set_solenoid_level_locked(0);
}

void app_main(void) {
    nvs_flash_init();

    state_lock = xSemaphoreCreateMutex();
    if (!state_lock) {
        return;
    }

    init_led_strip();

    const esp_timer_create_args_t timer_args = {
        .callback = &max_hold_timer_cb,
        .name = "max_hold",
    };
    esp_timer_create(&timer_args, &max_hold_timer);

    const esp_timer_create_args_t min_hold_args = {
        .callback = &min_hold_timer_cb,
        .name = "min_hold",
    };
    esp_timer_create(&min_hold_args, &min_hold_timer);

    const esp_timer_create_args_t kick_args = {
        .callback = &solenoid_kick_timer_cb,
        .name = "sol_kick",
    };
    esp_timer_create(&kick_args, &solenoid_kick_timer);

    mount_spiffs();
    wifi_init_ap_sta();

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        runtime.state = STATE_READY;
        update_status_led_locked();
        xSemaphoreGive(state_lock);
    }

    httpd = start_http_server();

    xTaskCreate(status_task, "status_task", 4096, NULL, 5, NULL);
}
