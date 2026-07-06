#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define LED_GPIO 8
#define SERVO_GPIO 2
#define UART_PORT UART_NUM_0
#define UART_BUFFER_SIZE 256
#define WORD_BUFFER_SIZE 64

#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2500
#define SERVO_PWM_FREQ_HZ 50
#define SERVO_PWM_RESOLUTION LEDC_TIMER_14_BIT
#define SERVO_PWM_MAX_DUTY ((1 << 14) - 1)

static const char *TAG = "binary_blink";

static const uint8_t ONE_RED = 0;
static const uint8_t ONE_GREEN = 45;
static const uint8_t ONE_BLUE = 0;

static const uint8_t ZERO_RED = 0;
static const uint8_t ZERO_GREEN = 0;
static const uint8_t ZERO_BLUE = 35;

static const TickType_t BIT_ON_TICKS = pdMS_TO_TICKS(220);
static const TickType_t BIT_OFF_TICKS = pdMS_TO_TICKS(140);
static const TickType_t LETTER_GAP_TICKS = pdMS_TO_TICKS(650);
static const TickType_t WORD_GAP_TICKS = pdMS_TO_TICKS(1800);
static const TickType_t SERVO_STEP_DELAY_TICKS = pdMS_TO_TICKS(800);

static char word[WORD_BUFFER_SIZE] = "Patch";
static char input[WORD_BUFFER_SIZE];
static size_t input_len;
static led_strip_handle_t led_strip;

static uint32_t servo_angle_to_duty(uint8_t angle)
{
    uint32_t pulse_us = SERVO_MIN_PULSE_US +
        ((uint32_t)angle * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US)) / 180;

    return (pulse_us * SERVO_PWM_MAX_DUTY) / (1000000 / SERVO_PWM_FREQ_HZ);
}

static void set_servo_angle(uint8_t angle)
{
    uint32_t duty = servo_angle_to_duty(angle);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    ESP_LOGI(TAG, "Servo angle: %u", angle);
}

static void servo_task(void *arg)
{
    const uint8_t positions[] = {0, 90, 180};
    size_t index = 0;

    while (true) {
        set_servo_angle(positions[index]);
        index = (index + 1) % (sizeof(positions) / sizeof(positions[0]));
        vTaskDelay(SERVO_STEP_DELAY_TICKS);
    }
}

static void set_led(uint8_t red, uint8_t green, uint8_t blue)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, red, green, blue));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void led_off(void)
{
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}

static void blink_bit(bool bit_value)
{
    if (bit_value) {
        set_led(ONE_RED, ONE_GREEN, ONE_BLUE);
    } else {
        set_led(ZERO_RED, ZERO_GREEN, ZERO_BLUE);
    }

    vTaskDelay(BIT_ON_TICKS);
    led_off();
    vTaskDelay(BIT_OFF_TICKS);
}

static void poll_serial_word(void)
{
    uint8_t byte;

    while (uart_read_bytes(UART_PORT, &byte, 1, 0) == 1) {
        if (byte == '\r') {
            continue;
        }

        if (byte == '\n') {
            if (input_len > 0) {
                input[input_len] = '\0';
                strncpy(word, input, sizeof(word) - 1);
                word[sizeof(word) - 1] = '\0';
                input_len = 0;

                ESP_LOGI(TAG, "New word: %s", word);
            }
            continue;
        }

        if (input_len < sizeof(input) - 1) {
            input[input_len++] = (char)byte;
        }
    }
}

static void blink_character(char character)
{
    uint8_t ascii = (uint8_t)character;

    printf("%c = ", character);

    for (int bit = 7; bit >= 0; bit--) {
        bool bit_value = ((ascii >> bit) & 0x01) != 0;
        putchar(bit_value ? '1' : '0');
        fflush(stdout);
        blink_bit(bit_value);
    }

    putchar('\n');
    vTaskDelay(LETTER_GAP_TICKS);
}

static void blink_word(void)
{
    ESP_LOGI(TAG, "Blinking word: %s", word);

    size_t length = strlen(word);
    for (size_t index = 0; index < length; index++) {
        blink_character(word[index]);
        poll_serial_word();
    }

    vTaskDelay(WORD_GAP_TICKS);
}

static void configure_led(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_off();
}

static void configure_uart(void)
{
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUFFER_SIZE, 0, 0, NULL, 0));
}

static void configure_servo(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = SERVO_PWM_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = SERVO_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ledc_channel_config_t channel_config = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = servo_angle_to_duty(90),
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

void app_main(void)
{
    configure_led();
    configure_uart();
    configure_servo();

    ESP_LOGI(TAG, "ESP32-C6 binary word blinker");
    ESP_LOGI(TAG, "1 bits blink green, 0 bits blink blue.");
    ESP_LOGI(TAG, "Servo signal on GPIO %d.", SERVO_GPIO);
    ESP_LOGI(TAG, "Type a word in the PlatformIO serial monitor and press Enter.");

    xTaskCreate(servo_task, "servo_task", 2048, NULL, 5, NULL);

    while (true) {
        poll_serial_word();
        blink_word();
    }
}
