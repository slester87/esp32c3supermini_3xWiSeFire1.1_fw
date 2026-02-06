#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* -----------------------------------------------------------------------
 * Safety timing constants
 *
 * Multiple independent mechanisms prevent runaway fire:
 *
 * 1. MIN_HOLD_MS  -- Minimum solenoid activation. Prevents rapid on/off
 *    cycling that could damage valves. If the user releases early the
 *    solenoid stays open until this time elapses.
 *
 * 2. MAX_HOLD_MS  -- Maximum solenoid activation. Solenoid auto-closes
 *    after this limit regardless of user input. Sets ignore_until_release
 *    to prevent re-firing until the user lifts their finger.
 *
 * 3. SOLENOID_KICK_MS -- Full-power pulse before reducing to hold level.
 *    (Currently a no-op; see SOLENOID_HOLD_LEVEL below.)
 *
 * 4. WebSocket watchdog (2 s, in status_task) -- If no message arrives
 *    for 2 seconds all channels stop and state becomes DISCONNECTED.
 *
 * 5. status_task backup max-hold -- Polls every 200 ms as defense-in-
 *    depth behind the per-channel max_hold_timers.
 * ----------------------------------------------------------------------- */
#define MAX_HOLD_MS 3000
#define MIN_HOLD_MS 250
#define SOLENOID_KICK_MS 50

/* WiSeFire 1.1 uses the pixel value as an on/off gate, not PWM current
 * control. 255 matches the initial kick level, making the kick timer a
 * no-op. The timer infrastructure is retained for future hardware that
 * supports PWM hold-off. */
#define SOLENOID_HOLD_LEVEL 255

#define STATUS_LED_INDEX 0
#define SOLENOID_PIXEL_INDEX 1
#define FIRING_PIXEL_INDEX 2

#define GPIO_NEOPIXEL GPIO_NUM_4

/* -----------------------------------------------------------------------
 * Multi-channel solenoid control
 *
 * Three solenoid channels are driven via a single WS2812 pixel:
 *   Pixel 1 color: R = channel 0, G = channel 1, B = channel 2
 *   Value 255 = solenoid open, 0 = solenoid closed.
 *
 * Channels are addressed by bitmask (bit 0 = ch0, bit 1 = ch1, etc.).
 * WebSocket protocol: "DOWN:<mask>" where mask is 1-7, "UP" releases all.
 * Each channel has independent max_hold, min_hold, and kick timers.
 * ----------------------------------------------------------------------- */
#define NUM_CHANNELS 3

#define TAG "poofer"

typedef enum {
    STATE_BOOT = 0,
    STATE_READY,
    STATE_FIRING,
    STATE_DISCONNECTED,
    STATE_ERROR, /* Reserved. No code path currently transitions here. */
} system_state_t;

typedef struct {
    bool active;
    bool ignore_until_release;
    bool release_pending;
    uint8_t level;
} channel_state_t;

typedef struct {
    system_state_t state;
    channel_state_t channels[NUM_CHANNELS];
    uint8_t active_mask;
    int64_t press_start_us; /* Shared across channels. See start_channels_locked(). */
    uint32_t last_hold_ms;
    int64_t last_ws_rx_us;
    bool ws_connected;
    uint8_t status_r;
    uint8_t status_g;
    uint8_t status_b;
} runtime_state_t;

static led_strip_handle_t strip = NULL;
static httpd_handle_t httpd = NULL;
static int ws_client_fd = -1;
static runtime_state_t runtime = {
    .state = STATE_BOOT,
    .channels = {{0}},
    .active_mask = 0,
    .press_start_us = 0,
    .last_hold_ms = MIN_HOLD_MS,
    .last_ws_rx_us = 0,
    .ws_connected = false,
    .status_r = 0,
    .status_g = 0,
    .status_b = 0,
};
static SemaphoreHandle_t state_lock;
static esp_timer_handle_t max_hold_timers[NUM_CHANNELS];
static esp_timer_handle_t min_hold_timers[NUM_CHANNELS];
static esp_timer_handle_t solenoid_kick_timers[NUM_CHANNELS];

static void refresh_pixels_locked(void) {
    if (!strip) {
        return;
    }
    led_strip_set_pixel(strip, STATUS_LED_INDEX, runtime.status_r, runtime.status_g,
                        runtime.status_b);
    led_strip_set_pixel(strip, SOLENOID_PIXEL_INDEX, runtime.channels[0].level,
                        runtime.channels[1].level, runtime.channels[2].level);
    led_strip_set_pixel(strip, FIRING_PIXEL_INDEX, runtime.channels[0].level,
                        runtime.channels[1].level, runtime.channels[2].level);
    led_strip_refresh(strip);
}

static void set_channel_level_locked(int ch, uint8_t level) {
    runtime.channels[ch].level = level;
    refresh_pixels_locked();
}

static void update_status_led_locked(void) {
    switch (runtime.state) {
    case STATE_BOOT:
        runtime.status_r = 122;
        runtime.status_g = 138;
        runtime.status_b = 160; // idle/muted (#7a8aa0)
        break;
    case STATE_READY:
        runtime.status_r = 29;
        runtime.status_g = 185;
        runtime.status_b = 84; // ready green (#1db954)
        break;
    case STATE_FIRING:
        runtime.status_r = 255;
        runtime.status_g = 138;
        runtime.status_b = 0; // firing orange (#ff8a00)
        break;
    case STATE_DISCONNECTED:
        runtime.status_r = 0;
        runtime.status_g = 0;
        runtime.status_b = 255; // disconnected blue (#0000ff)
        break;
    case STATE_ERROR:
    default:
        runtime.status_r = 230;
        runtime.status_g = 57;
        runtime.status_b = 70; // error red (#e63946)
        break;
    }
    refresh_pixels_locked();
}

static void recalc_active_mask_locked(void) {
    uint8_t mask = 0;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (runtime.channels[i].active) {
            mask |= (uint8_t)(1 << i);
        }
    }
    runtime.active_mask = mask;
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
    if (runtime.active_mask == 0) {
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

    char payload[256];
    bool ready = false;
    bool firing[NUM_CHANNELS] = {false};
    bool error = false;
    bool connected = false;
    uint32_t elapsed = 0;
    uint32_t last_hold = 0;

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        ready = (runtime.state == STATE_READY || runtime.state == STATE_FIRING);
        for (int i = 0; i < NUM_CHANNELS; i++) {
            firing[i] = runtime.channels[i].active;
        }
        error = (runtime.state == STATE_ERROR);
        connected = runtime.ws_connected;
        elapsed = current_elapsed_ms_locked();
        last_hold = runtime.last_hold_ms;
        xSemaphoreGive(state_lock);
    }

    const char* t = "true";
    const char* f = "false";
    int len = snprintf(payload, sizeof(payload),
                       "{\"ready\":%s,\"firing\":[%s,%s,%s],"
                       "\"error\":%s,\"connected\":%s,"
                       "\"elapsed_ms\":%" PRIu32 ",\"last_hold_ms\":%" PRIu32 "}",
                       ready ? t : f, firing[0] ? t : f, firing[1] ? t : f, firing[2] ? t : f,
                       error ? t : f, connected ? t : f, elapsed, last_hold);
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

static void stop_channel_locked(int ch) {
    runtime.channels[ch].active = false;
    runtime.channels[ch].release_pending = false;
    runtime.channels[ch].level = 0;
    esp_timer_stop(solenoid_kick_timers[ch]);
    recalc_active_mask_locked();
}

static void stop_all_channels_locked(system_state_t next_state) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        runtime.channels[i].active = false;
        runtime.channels[i].release_pending = false;
        runtime.channels[i].level = 0;
    }
    runtime.active_mask = 0;
    runtime.state = next_state;
    update_status_led_locked();
}

static void start_channels_locked(uint8_t mask) {
    runtime.state = STATE_FIRING;
    /* Shared timestamp -- if a custom client sends DOWN:1 then DOWN:2, the
     * second call overwrites this. Per-channel max_hold_timers are independent
     * and fire correctly regardless. The UI sends a single DOWN per gesture. */
    runtime.press_start_us = esp_timer_get_time();
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (mask & (1 << i)) {
            runtime.channels[i].active = true;
            runtime.channels[i].release_pending = false;
            runtime.channels[i].level = 255;
        }
    }
    recalc_active_mask_locked();
    update_status_led_locked();
}

static void max_hold_timer_cb(void* arg) {
    int ch = (int)(intptr_t)arg;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (runtime.channels[ch].active) {
        runtime.channels[ch].ignore_until_release = true;
        stop_channel_locked(ch);
        refresh_pixels_locked();

        if (runtime.active_mask == 0) {
            runtime.last_hold_ms = MAX_HOLD_MS;
            runtime.state = STATE_READY;
            update_status_led_locked();
        }
    }

    xSemaphoreGive(state_lock);
    send_state_async();
}

static void min_hold_timer_cb(void* arg) {
    int ch = (int)(intptr_t)arg;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (runtime.channels[ch].active && runtime.channels[ch].release_pending) {
        stop_channel_locked(ch);
        refresh_pixels_locked();

        if (runtime.active_mask == 0) {
            runtime.state = STATE_READY;
            update_status_led_locked();
        }
    }

    xSemaphoreGive(state_lock);
    send_state_async();
}

/* With SOLENOID_HOLD_LEVEL == 255 this callback is a no-op (level stays
 * at the initial kick value). See comment at SOLENOID_HOLD_LEVEL. */
static void solenoid_kick_timer_cb(void* arg) {
    int ch = (int)(intptr_t)arg;
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (runtime.channels[ch].active) {
        set_channel_level_locked(ch, SOLENOID_HOLD_LEVEL);
    }

    xSemaphoreGive(state_lock);
}

static void handle_press_down_mask(uint8_t mask) {
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (runtime.state == STATE_ERROR) {
        xSemaphoreGive(state_lock);
        return;
    }

    uint8_t effective = 0;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (!(mask & (1 << i))) {
            continue;
        }
        if (runtime.channels[i].active || runtime.channels[i].ignore_until_release) {
            continue;
        }
        effective |= (uint8_t)(1 << i);
    }

    if (effective == 0) {
        xSemaphoreGive(state_lock);
        return;
    }

    start_channels_locked(effective);

    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (!(effective & (1 << i))) {
            continue;
        }
        esp_timer_stop(max_hold_timers[i]);
        esp_timer_start_once(max_hold_timers[i], (uint64_t)MAX_HOLD_MS * 1000ULL);

        esp_timer_stop(solenoid_kick_timers[i]);
        esp_timer_start_once(solenoid_kick_timers[i], (uint64_t)SOLENOID_KICK_MS * 1000ULL);
    }

    xSemaphoreGive(state_lock);
    send_state_async();
}

static void handle_press_up(void) {
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    for (int i = 0; i < NUM_CHANNELS; i++) {
        runtime.channels[i].ignore_until_release = false;
    }

    if (runtime.active_mask == 0) {
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
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (runtime.channels[i].active) {
                runtime.channels[i].release_pending = true;
                esp_timer_stop(min_hold_timers[i]);
                esp_timer_start_once(min_hold_timers[i],
                                     (uint64_t)(MIN_HOLD_MS - held_ms) * 1000ULL);
            }
        }
        xSemaphoreGive(state_lock);
        send_state_async();
        return;
    }

    for (int i = 0; i < NUM_CHANNELS; i++) {
        esp_timer_stop(max_hold_timers[i]);
        esp_timer_stop(min_hold_timers[i]);
    }

    stop_all_channels_locked(STATE_READY);

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
        runtime.ws_connected = true;
        if (runtime.state == STATE_DISCONNECTED) {
            runtime.state = STATE_READY;
            update_status_led_locked();
        }
        xSemaphoreGive(state_lock);
    }

    if (strncmp(msg, "DOWN:", 5) == 0) {
        if (msg[5] >= '1' && msg[5] <= '7' && msg[6] == '\0') {
            handle_press_down_mask((uint8_t)(msg[5] - '0'));
        }
    } else if (strcmp(msg, "DOWN") == 0) {
        handle_press_down_mask(7);
    } else if (strcmp(msg, "UP") == 0) {
        handle_press_up();
    } else if (strcmp(msg, "PING") == 0) {
        send_state_async();
    }
}

static esp_err_t ws_handler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        ws_client_fd = httpd_req_to_sockfd(req);
        if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            runtime.ws_connected = true;
            runtime.last_ws_rx_us = esp_timer_get_time();
            if (runtime.state == STATE_DISCONNECTED) {
                runtime.state = STATE_READY;
                update_status_led_locked();
            }
            xSemaphoreGive(state_lock);
        }
        send_state_async();
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK || frame.len == 0) {
        return err;
    }

    /* Reject oversized frames. Valid messages are at most 6 bytes
     * (e.g. "DOWN:7"). Cap prevents heap exhaustion from crafted
     * frames on the open AP network. */
    if (frame.len > 64) {
        return ESP_ERR_INVALID_ARG;
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
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);

    char buffer[512];
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            if (httpd_resp_send_chunk(req, buffer, (size_t)n) != ESP_OK) {
                close(fd);
                httpd_resp_sendstr_chunk(req, NULL);
                return ESP_FAIL;
            }
            continue;
        }
        if (n < 0) {
            close(fd);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
        break;
    }
    close(fd);
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

/* Simple form parser. Uses strstr so a key like "pass" can match inside
 * another field name (e.g. "xpass"). Acceptable: callers only use "ssid"
 * and "pass" with the browser form from wifi.html. */
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
                runtime.state = STATE_DISCONNECTED;
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
        if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            int64_t now = esp_timer_get_time();

            /* Defense-in-depth: backup max-hold check. The primary
             * max_hold_timer_cb fires per-channel via esp_timer. This
             * polling check catches any case where the timer callback
             * failed to acquire the lock or was not started. */
            if (runtime.active_mask != 0) {
                int64_t elapsed = now - runtime.press_start_us;
                if (elapsed >= (int64_t)MAX_HOLD_MS * 1000LL) {
                    for (int i = 0; i < NUM_CHANNELS; i++) {
                        if (runtime.channels[i].active) {
                            runtime.channels[i].ignore_until_release = true;
                            stop_channel_locked(i);
                        }
                    }
                    runtime.last_hold_ms = MAX_HOLD_MS;
                    if (runtime.active_mask == 0) {
                        runtime.state = STATE_READY;
                        update_status_led_locked();
                    }
                    refresh_pixels_locked();
                    should_send = true;
                }
            }

            if (runtime.active_mask != 0 && runtime.last_ws_rx_us != 0) {
                if ((now - runtime.last_ws_rx_us) > 2000000) {
                    for (int i = 0; i < NUM_CHANNELS; i++) {
                        esp_timer_stop(max_hold_timers[i]);
                        esp_timer_stop(min_hold_timers[i]);
                        esp_timer_stop(solenoid_kick_timers[i]);
                    }
                    stop_all_channels_locked(STATE_DISCONNECTED);
                    runtime.ws_connected = false;
                    should_send = true;
                }
            }
            if (runtime.ws_connected && runtime.last_ws_rx_us != 0 &&
                (now - runtime.last_ws_rx_us) > 2000000) {
                runtime.ws_connected = false;
                if (runtime.state != STATE_ERROR && runtime.state != STATE_FIRING) {
                    runtime.state = STATE_DISCONNECTED;
                    update_status_led_locked();
                }
                should_send = true;
            }
            xSemaphoreGive(state_lock);
        }

        if (should_send) {
            send_state_async();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void init_led_strip(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_NEOPIXEL,
        .max_leds = 3,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
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
    refresh_pixels_locked();
}

void app_main(void) {
    nvs_flash_init();

    state_lock = xSemaphoreCreateMutex();
    if (!state_lock) {
        return;
    }

    init_led_strip();

    static const char* max_names[NUM_CHANNELS] = {"ch0_max", "ch1_max", "ch2_max"};
    static const char* min_names[NUM_CHANNELS] = {"ch0_min", "ch1_min", "ch2_min"};
    static const char* kick_names[NUM_CHANNELS] = {"ch0_kick", "ch1_kick", "ch2_kick"};
    for (int i = 0; i < NUM_CHANNELS; i++) {
        const esp_timer_create_args_t max_args = {
            .callback = &max_hold_timer_cb,
            .arg = (void*)(intptr_t)i,
            .name = max_names[i],
        };
        esp_timer_create(&max_args, &max_hold_timers[i]);

        const esp_timer_create_args_t min_args = {
            .callback = &min_hold_timer_cb,
            .arg = (void*)(intptr_t)i,
            .name = min_names[i],
        };
        esp_timer_create(&min_args, &min_hold_timers[i]);

        const esp_timer_create_args_t kick_args = {
            .callback = &solenoid_kick_timer_cb,
            .arg = (void*)(intptr_t)i,
            .name = kick_names[i],
        };
        esp_timer_create(&kick_args, &solenoid_kick_timers[i]);
    }

    mount_spiffs();
    wifi_init_ap_sta();

    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        runtime.state = STATE_DISCONNECTED;
        update_status_led_locked();
        xSemaphoreGive(state_lock);
    }

    httpd = start_http_server();

    xTaskCreate(status_task, "status_task", 4096, NULL, 5, NULL);
}
