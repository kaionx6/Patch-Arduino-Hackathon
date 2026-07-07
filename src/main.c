#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "assert.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define MOTOR_ENA_GPIO 2
#define MOTOR_IN1_GPIO 3
#define MOTOR_IN2_GPIO 0
#define MOTOR_IN3_GPIO 1
#define MOTOR_IN4_GPIO 6
#define MOTOR_ENB_GPIO 7

#define LCD_RS_GPIO 10
#define LCD_E_GPIO 11
#define LCD_D4_GPIO 4
#define LCD_D5_GPIO 5
#define LCD_D6_GPIO 15
#define LCD_D7_GPIO 23

#define MATRIX_DIN_GPIO 18
#define MATRIX_CLK_GPIO 19
#define MATRIX_CS_GPIO 20

#define MOTOR_PWM_FREQ_HZ 1000
#define MOTOR_PWM_RESOLUTION LEDC_TIMER_10_BIT
#define MOTOR_PWM_MAX_DUTY ((1 << 10) - 1)

#define BLE_DEVICE_NAME "LoopyBot"

static const char *TAG = "l298n_motors";

static const uint8_t MATRIX_CLOCKWISE_D_MOUTH[8] = {
    0b01111110,
    0b01000010,
    0b01000010,
    0b01000010,
    0b01000010,
    0b00100100,
    0b00011000,
    0b00000000,
};

static const uint8_t MATRIX_SMALL_FROWN[8] = {
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b00111100,
    0b01000010,
    0b10000001,
    0b00000000,
};

static const uint8_t MATRIX_ANGRY_MOUTH[8] = {
    0b00000000,
    0b10000001,
    0b01000010,
    0b00100100,
    0b00011000,
    0b00100100,
    0b01000010,
    0b00000000,
};

static const uint8_t MATRIX_FLAT_MOUTH[8] = {
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b11111111,
    0b00000000,
    0b00000000,
    0b00000000,
};

typedef enum {
    MOOD_HAPPY,
    MOOD_SAD,
    MOOD_ANGRY,
    MOOD_MOODY,
    MOOD_JOYFUL,
} robot_mood_t;

static volatile robot_mood_t current_mood = MOOD_HAPPY;
static uint8_t ble_own_addr_type;

static const ble_uuid128_t MOOD_SERVICE_UUID =
    BLE_UUID128_INIT(0x6c, 0x6f, 0x6f, 0x70, 0x79, 0x2d, 0x62, 0x6f,
                     0x74, 0x2d, 0x6d, 0x6f, 0x6f, 0x64, 0x00, 0x01);

static const ble_uuid128_t MOOD_CHARACTERISTIC_UUID =
    BLE_UUID128_INIT(0x6c, 0x6f, 0x6f, 0x70, 0x79, 0x2d, 0x62, 0x6f,
                     0x74, 0x2d, 0x6d, 0x6f, 0x6f, 0x64, 0x00, 0x02);

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static int mood_characteristic_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg
);

static const struct ble_gatt_svc_def ble_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &MOOD_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &MOOD_CHARACTERISTIC_UUID.u,
                .access_cb = mood_characteristic_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {0},
        },
    },
    {0},
};

static void matrix_clock_pulse(void)
{
    ESP_ERROR_CHECK(gpio_set_level(MATRIX_CLK_GPIO, 1));
    esp_rom_delay_us(1);
    ESP_ERROR_CHECK(gpio_set_level(MATRIX_CLK_GPIO, 0));
    esp_rom_delay_us(1);
}

static void matrix_write_byte(uint8_t value)
{
    for (int bit = 7; bit >= 0; bit--) {
        ESP_ERROR_CHECK(gpio_set_level(MATRIX_DIN_GPIO, (value >> bit) & 0x01));
        matrix_clock_pulse();
    }
}

static void matrix_write_register(uint8_t address, uint8_t value)
{
    ESP_ERROR_CHECK(gpio_set_level(MATRIX_CS_GPIO, 0));
    matrix_write_byte(address);
    matrix_write_byte(value);
    ESP_ERROR_CHECK(gpio_set_level(MATRIX_CS_GPIO, 1));
}

static void matrix_clear(void)
{
    for (uint8_t row = 1; row <= 8; row++) {
        matrix_write_register(row, 0x00);
    }
}

static uint8_t matrix_reverse_bits(uint8_t value)
{
    value = ((value & 0xF0) >> 4) | ((value & 0x0F) << 4);
    value = ((value & 0xCC) >> 2) | ((value & 0x33) << 2);
    value = ((value & 0xAA) >> 1) | ((value & 0x55) << 1);
    return value;
}

static void matrix_draw(const uint8_t rows[8])
{
    for (uint8_t row = 0; row < 8; row++) {
        matrix_write_register(row + 1, matrix_reverse_bits(rows[7 - row]));
    }
}

static const char *mood_to_string(robot_mood_t mood)
{
    switch (mood) {
        case MOOD_HAPPY:
            return "happy";
        case MOOD_SAD:
            return "sad";
        case MOOD_ANGRY:
            return "angry";
        case MOOD_MOODY:
            return "moody";
        case MOOD_JOYFUL:
            return "joyful";
        default:
            return "unknown";
    }
}

static bool mood_from_string(const char *text, robot_mood_t *mood)
{
    if (strcmp(text, "happy") == 0) {
        *mood = MOOD_HAPPY;
    } else if (strcmp(text, "sad") == 0) {
        *mood = MOOD_SAD;
    } else if (strcmp(text, "angry") == 0) {
        *mood = MOOD_ANGRY;
    } else if (strcmp(text, "moody") == 0) {
        *mood = MOOD_MOODY;
    } else if (strcmp(text, "joyful") == 0) {
        *mood = MOOD_JOYFUL;
    } else {
        return false;
    }

    return true;
}

static void configure_matrix(void)
{
    gpio_config_t matrix_gpio_config = {
        .pin_bit_mask =
            (1ULL << MATRIX_DIN_GPIO) |
            (1ULL << MATRIX_CLK_GPIO) |
            (1ULL << MATRIX_CS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&matrix_gpio_config));
    ESP_ERROR_CHECK(gpio_set_level(MATRIX_CS_GPIO, 1));
    ESP_ERROR_CHECK(gpio_set_level(MATRIX_CLK_GPIO, 0));

    matrix_write_register(0x0F, 0x00);
    matrix_write_register(0x0C, 0x01);
    matrix_write_register(0x09, 0x00);
    matrix_write_register(0x0A, 0x04);
    matrix_write_register(0x0B, 0x07);
    matrix_clear();
}

static void lcd_delay_us(uint32_t delay_us)
{
    esp_rom_delay_us(delay_us);
}

static void lcd_pulse_enable(void)
{
    ESP_ERROR_CHECK(gpio_set_level(LCD_E_GPIO, 0));
    lcd_delay_us(1);
    ESP_ERROR_CHECK(gpio_set_level(LCD_E_GPIO, 1));
    lcd_delay_us(1);
    ESP_ERROR_CHECK(gpio_set_level(LCD_E_GPIO, 0));
    lcd_delay_us(100);
}

static void lcd_write_nibble(uint8_t nibble)
{
    ESP_ERROR_CHECK(gpio_set_level(LCD_D4_GPIO, (nibble >> 0) & 0x01));
    ESP_ERROR_CHECK(gpio_set_level(LCD_D5_GPIO, (nibble >> 1) & 0x01));
    ESP_ERROR_CHECK(gpio_set_level(LCD_D6_GPIO, (nibble >> 2) & 0x01));
    ESP_ERROR_CHECK(gpio_set_level(LCD_D7_GPIO, (nibble >> 3) & 0x01));
    lcd_pulse_enable();
}

static void lcd_write_byte(uint8_t value, bool is_data)
{
    ESP_ERROR_CHECK(gpio_set_level(LCD_RS_GPIO, is_data));
    lcd_write_nibble(value >> 4);
    lcd_write_nibble(value & 0x0F);
}

static void lcd_command(uint8_t command)
{
    lcd_write_byte(command, false);

    if (command == 0x01 || command == 0x02) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void lcd_clear(void)
{
    lcd_command(0x01);
}

static void lcd_home(void)
{
    lcd_command(0x02);
}

static void lcd_data(uint8_t data)
{
    lcd_write_byte(data, true);
}

static void lcd_print(const char *text)
{
    while (*text != '\0') {
        lcd_data((uint8_t)*text);
        text++;
    }
}

static void lcd_print_line(const char *text)
{
    lcd_clear();
    lcd_home();
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_print(text);
}

static void update_face(robot_mood_t mood)
{
    switch (mood) {
        case MOOD_HAPPY:
            lcd_print_line("   U       U   ");
            matrix_draw(MATRIX_CLOCKWISE_D_MOUTH);
            break;
        case MOOD_SAD:
            lcd_print_line("   n       n   ");
            matrix_draw(MATRIX_SMALL_FROWN);
            break;
        case MOOD_ANGRY:
            lcd_print_line("   >       <   ");
            matrix_draw(MATRIX_ANGRY_MOUTH);
            break;
        case MOOD_MOODY:
            lcd_print_line("   -       -   ");
            matrix_draw(MATRIX_FLAT_MOUTH);
            break;
        case MOOD_JOYFUL:
            lcd_print_line("   ^       ^   ");
            matrix_draw(MATRIX_CLOCKWISE_D_MOUTH);
            break;
    }

    ESP_LOGI(TAG, "Face mood: %s", mood_to_string(mood));
}

static void configure_lcd(void)
{
    gpio_config_t lcd_gpio_config = {
        .pin_bit_mask =
            (1ULL << LCD_RS_GPIO) |
            (1ULL << LCD_E_GPIO) |
            (1ULL << LCD_D4_GPIO) |
            (1ULL << LCD_D5_GPIO) |
            (1ULL << LCD_D6_GPIO) |
            (1ULL << LCD_D7_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&lcd_gpio_config));

    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_ERROR_CHECK(gpio_set_level(LCD_RS_GPIO, 0));
    ESP_ERROR_CHECK(gpio_set_level(LCD_E_GPIO, 0));

    lcd_write_nibble(0x03);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x03);
    lcd_delay_us(150);
    lcd_write_nibble(0x03);
    lcd_delay_us(150);
    lcd_write_nibble(0x02);

    lcd_command(0x28);
    lcd_command(0x0C);
    lcd_command(0x06);
    lcd_command(0x01);
}

static uint32_t motor_percent_to_duty(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }

    return ((uint32_t)percent * MOTOR_PWM_MAX_DUTY) / 100;
}

static void set_motor_speed(uint8_t left_percent, uint8_t right_percent)
{
    ESP_ERROR_CHECK(ledc_set_duty(
        LEDC_LOW_SPEED_MODE,
        LEDC_CHANNEL_0,
        motor_percent_to_duty(left_percent)
    ));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));

    ESP_ERROR_CHECK(ledc_set_duty(
        LEDC_LOW_SPEED_MODE,
        LEDC_CHANNEL_1,
        motor_percent_to_duty(right_percent)
    ));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
}

static void set_left_motor(bool in1, bool in2)
{
    ESP_ERROR_CHECK(gpio_set_level(MOTOR_IN1_GPIO, in1));
    ESP_ERROR_CHECK(gpio_set_level(MOTOR_IN2_GPIO, in2));
}

static void set_right_motor(bool in3, bool in4)
{
    ESP_ERROR_CHECK(gpio_set_level(MOTOR_IN3_GPIO, in3));
    ESP_ERROR_CHECK(gpio_set_level(MOTOR_IN4_GPIO, in4));
}

static void motors_stop(void)
{
    set_motor_speed(0, 0);
    set_left_motor(false, false);
    set_right_motor(false, false);
    ESP_LOGI(TAG, "Motors stopped");
}

static void motors_forward(uint8_t speed_percent)
{
    set_left_motor(true, false);
    set_right_motor(true, false);
    set_motor_speed(speed_percent, speed_percent);
    ESP_LOGI(TAG, "Motors forward at %u%%", speed_percent);
}

static void motors_reverse(uint8_t speed_percent)
{
    set_left_motor(false, true);
    set_right_motor(false, true);
    set_motor_speed(speed_percent, speed_percent);
    ESP_LOGI(TAG, "Motors reverse at %u%%", speed_percent);
}

static void motors_spin_left(uint8_t speed_percent)
{
    set_left_motor(false, true);
    set_right_motor(true, false);
    set_motor_speed(speed_percent, speed_percent);
}

static void motors_spin_right(uint8_t speed_percent)
{
    set_left_motor(true, false);
    set_right_motor(false, true);
    set_motor_speed(speed_percent, speed_percent);
}

static void motors_forward_for(uint8_t speed_percent, TickType_t ticks)
{
    motors_forward(speed_percent);
    vTaskDelay(ticks);
}

static void motors_reverse_for(uint8_t speed_percent, TickType_t ticks)
{
    motors_reverse(speed_percent);
    vTaskDelay(ticks);
}

static void motors_spin_left_for(uint8_t speed_percent, TickType_t ticks)
{
    motors_spin_left(speed_percent);
    vTaskDelay(ticks);
}

static void motors_spin_right_for(uint8_t speed_percent, TickType_t ticks)
{
    motors_spin_right(speed_percent);
    vTaskDelay(ticks);
}

static void motors_stop_for(TickType_t ticks)
{
    motors_stop();
    vTaskDelay(ticks);
}

static void dance_for_mood(robot_mood_t mood)
{
    switch (mood) {
        case MOOD_HAPPY:
            motors_spin_left_for(65, pdMS_TO_TICKS(350));
            motors_spin_right_for(65, pdMS_TO_TICKS(350));
            motors_forward_for(60, pdMS_TO_TICKS(500));
            motors_stop_for(pdMS_TO_TICKS(200));
            break;

        case MOOD_SAD:
            motors_forward_for(30, pdMS_TO_TICKS(600));
            motors_stop_for(pdMS_TO_TICKS(500));
            motors_reverse_for(25, pdMS_TO_TICKS(500));
            motors_stop_for(pdMS_TO_TICKS(700));
            break;

        case MOOD_ANGRY:
            motors_forward_for(90, pdMS_TO_TICKS(250));
            motors_reverse_for(90, pdMS_TO_TICKS(250));
            motors_spin_left_for(95, pdMS_TO_TICKS(250));
            motors_spin_right_for(95, pdMS_TO_TICKS(250));
            motors_stop_for(pdMS_TO_TICKS(150));
            break;

        case MOOD_MOODY:
            motors_spin_left_for(35, pdMS_TO_TICKS(450));
            motors_stop_for(pdMS_TO_TICKS(450));
            motors_spin_right_for(35, pdMS_TO_TICKS(450));
            motors_stop_for(pdMS_TO_TICKS(900));
            break;

        case MOOD_JOYFUL:
            motors_forward_for(80, pdMS_TO_TICKS(300));
            motors_spin_left_for(80, pdMS_TO_TICKS(300));
            motors_forward_for(80, pdMS_TO_TICKS(300));
            motors_spin_right_for(80, pdMS_TO_TICKS(300));
            motors_stop_for(pdMS_TO_TICKS(150));
            break;
    }
}

static void configure_motors(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = MOTOR_PWM_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ledc_channel_config_t ena_config = {
        .gpio_num = MOTOR_ENA_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };

    ledc_channel_config_t enb_config = {
        .gpio_num = MOTOR_ENB_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };

    gpio_config_t motor_gpio_config = {
        .pin_bit_mask =
            (1ULL << MOTOR_IN1_GPIO) |
            (1ULL << MOTOR_IN2_GPIO) |
            (1ULL << MOTOR_IN3_GPIO) |
            (1ULL << MOTOR_IN4_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));
    ESP_ERROR_CHECK(ledc_channel_config(&ena_config));
    ESP_ERROR_CHECK(ledc_channel_config(&enb_config));
    ESP_ERROR_CHECK(gpio_config(&motor_gpio_config));
    motors_stop();
}

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name = ble_svc_gap_device_name();
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.uuids128 = &MOOD_SERVICE_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set BLE advertisement data: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(
        ble_own_addr_type,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        ble_gap_event,
        NULL
    );
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start BLE advertising: %d", rc);
    }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "BLE client connected");
            } else {
                ESP_LOGW(TAG, "BLE connection failed: %d", event->connect.status);
                ble_advertise();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE client disconnected");
            ble_advertise();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ble_advertise();
            return 0;

        default:
            return 0;
    }
}

static int mood_characteristic_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg
)
{
    char mood_text[16];
    uint16_t mood_len = 0;
    robot_mood_t requested_mood;
    int rc;

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            rc = os_mbuf_append(
                ctxt->om,
                mood_to_string(current_mood),
                strlen(mood_to_string(current_mood))
            );
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            rc = ble_hs_mbuf_to_flat(ctxt->om, mood_text, sizeof(mood_text) - 1, &mood_len);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }

            mood_text[mood_len] = '\0';
            if (!mood_from_string(mood_text, &requested_mood)) {
                ESP_LOGW(TAG, "Unknown BLE mood: %s", mood_text);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }

            current_mood = requested_mood;
            ESP_LOGI(TAG, "Received BLE mood: %s", mood_to_string(requested_mood));
            return 0;

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE reset: %d", reason);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &ble_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer BLE address type: %d", rc);
        return;
    }

    ble_advertise();
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void configure_bluetooth(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(ble_services);
    assert(rc == 0);

    rc = ble_gatts_add_svcs(ble_services);
    assert(rc == 0);

    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    assert(rc == 0);

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE ready. Advertised name: %s", BLE_DEVICE_NAME);
}

void app_main(void)
{
    configure_bluetooth();
    configure_lcd();
    configure_matrix();
    configure_motors();

    robot_mood_t shown_mood = current_mood;
    update_face(shown_mood);

    ESP_LOGI(TAG, "L298N motor test started");
    ESP_LOGI(TAG, "Send mood by writing text to BLE device %s", BLE_DEVICE_NAME);
    ESP_LOGI(TAG, "MAX7219 pins: DIN=%d CLK=%d CS=%d",
        MATRIX_DIN_GPIO,
        MATRIX_CLK_GPIO,
        MATRIX_CS_GPIO
    );
    ESP_LOGI(TAG, "ENA=%d IN1=%d IN2=%d IN3=%d IN4=%d ENB=%d",
        MOTOR_ENA_GPIO,
        MOTOR_IN1_GPIO,
        MOTOR_IN2_GPIO,
        MOTOR_IN3_GPIO,
        MOTOR_IN4_GPIO,
        MOTOR_ENB_GPIO
    );

    while (true) {
        robot_mood_t mood = current_mood;
        if (mood != shown_mood) {
            shown_mood = mood;
            update_face(shown_mood);
        }

        dance_for_mood(mood);
    }
}
