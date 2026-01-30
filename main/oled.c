#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "u8g2.h"
#include "driver/gpio.h"
#include "oled_port.h"


static const char *TAG = "sh110x_oled";

// ====== USER CONFIG ======
#define I2C_PORT_NUM        0
#define I2C_SDA_GPIO        8
#define I2C_SCL_GPIO        9
#define I2C_FREQ_HZ         400000

#define OLED_I2C_ADDR       0x3C   // try 0x3D if not found
#define OLED_RESET_GPIO     10     // set to -1 if not connected
// =========================

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_oled_dev = NULL;

// u8g2 expects 8-bit address for some APIs, but our I2C driver uses 7-bit.
// We'll keep 7-bit here and shift inside callbacks if needed.
static uint8_t s_oled_addr_7bit = OLED_I2C_ADDR;

static void i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,  // ok for short wires; external pullups still recommended
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_oled_addr_7bit,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_oled_dev));
    ESP_LOGI(TAG, "I2C initialized: SDA=%d SCL=%d @%d Hz, OLED addr=0x%02X",
             I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_FREQ_HZ, OLED_I2C_ADDR);
}

static void reset_gpio_init_if_needed(void)
{
    if (OLED_RESET_GPIO < 0) return;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << OLED_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // pulse reset
    gpio_set_level(OLED_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(OLED_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ---- u8g2 GPIO+Delay callback ----
uint8_t u8x8_gpio_and_delay_esp32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;

    switch (msg) {
        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(pdMS_TO_TICKS(arg_int));
            break;

        case U8X8_MSG_GPIO_RESET:
            if (OLED_RESET_GPIO >= 0) {
                // arg_int: 0 = assert reset, 1 = deassert reset
                gpio_set_level(OLED_RESET_GPIO, arg_int);
            }
            break;

        // u8g2 may ask for these; we don’t need them for I2C
        case U8X8_MSG_GPIO_I2C_CLOCK:
        case U8X8_MSG_GPIO_I2C_DATA:
        default:
            break;
    }
    return 1;
}

// ---- u8g2 I2C byte callback using ESP-IDF i2c_master ----
// u8g2 uses a small state machine: START, SEND bytes, END.
uint8_t u8x8_byte_esp32_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    static uint8_t buffer[256];
    static size_t buf_len = 0;

    (void)u8x8;

    switch (msg) {
        case U8X8_MSG_BYTE_INIT:
            // Initialize I2C if not already initialized
            if (s_i2c_bus == NULL) {
                i2c_init();
                reset_gpio_init_if_needed();
            }
            buf_len = 0;
            break;

        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_len = 0;
            break;

        case U8X8_MSG_BYTE_SEND: {
            uint8_t *data = (uint8_t *)arg_ptr;
            if (buf_len + (size_t)arg_int > sizeof(buffer)) {
                ESP_LOGE(TAG, "u8g2 I2C buffer overflow");
                return 0;
            }
            for (int i = 0; i < arg_int; i++) buffer[buf_len++] = data[i];
        } break;

        case U8X8_MSG_BYTE_END_TRANSFER: {
            // Write the accumulated bytes in one transaction.
            // u8g2 includes the control byte (0x00 for commands / 0x40 for data) in the stream,
            // so we just send as-is.
            esp_err_t err = i2c_master_transmit(s_oled_dev, buffer, buf_len, -1);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "I2C transmit failed: %s", esp_err_to_name(err));
                return 0;
            }
        } break;

        case U8X8_MSG_BYTE_SET_DC:
            // Not used on I2C (control byte handles this)
            break;

        default:
            break;
    }

    return 1;
}

void oled_task(void *arg)
{
    i2c_init();
    reset_gpio_init_if_needed();

    u8g2_t u8g2;

    // SH1106 128x64 I2C constructor (full framebuffer)
    // If your panel is SH1107 or differs, change constructor accordingly.
    u8g2_Setup_sh1106_i2c_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        u8x8_byte_esp32_hw_i2c,
        u8x8_gpio_and_delay_esp32
    );

    // Tell u8g2 the I2C address in 8-bit form (left-shifted)
    // u8g2 expects address << 1
    u8g2_SetI2CAddress(&u8g2, (OLED_I2C_ADDR << 1));

    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_6x12_tf);
    u8g2_DrawStr(&u8g2, 0, 14, "ESP32-S3 + SH110X");
    u8g2_DrawStr(&u8g2, 0, 30, "I2C OLED 128x64");
    u8g2_SendBuffer(&u8g2);
    int counter = 0;
    while (1) {
        char line[32];
        snprintf(line, sizeof(line), "Counter: %d", counter++);

        u8g2_ClearBuffer(&u8g2);
        u8g2_SetFont(&u8g2, u8g2_font_6x12_tf);
        u8g2_DrawStr(&u8g2, 0, 14, "Running...");
        u8g2_DrawStr(&u8g2, 0, 30, line);
        u8g2_SendBuffer(&u8g2);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void start_oled_task(void)
{
    xTaskCreate(oled_task, "OLED_task", 4096, NULL, 5, NULL);
}
/*
#include "ui_menu.h"
#include "esp_log.h"

static const char *TAG = "UI";

static void act_settings(void *ctx) { ESP_LOGI(TAG, "Settings"); }
static void act_led(void *ctx)      { ESP_LOGI(TAG, "LED Toggle"); }
static void act_about(void *ctx)    { ESP_LOGI(TAG, "About"); }
static void act_reset(void *ctx)    { ESP_LOGI(TAG, "Reset"); }

static const ui_menu_item_t main_items[] = {
    // glyphs are Unicode:
    // ⚙ U+2699, 💡 U+1F4A1, ℹ U+2139, ↻ U+21BB
    { "Settings", 0x2699, act_settings },
    { "LED",      0x1F4A1, act_led      },
    { "About",    0x2139, act_about    },
    { "Reset",    0x21BB, act_reset    },
    { "Extra 1",  0x2605, NULL         }, // ★ U+2605
    { "Extra 2",  0x25CF, NULL         }, // ● U+25CF
};

static ui_menu_t g_menu;

void ui_setup(u8g2_t *u8)
{
    ui_menu_init(&g_menu, u8, "MAIN MENU", main_items, sizeof(main_items)/sizeof(main_items[0]), NULL);
    ui_menu_draw(&g_menu);
}

// Call this from your keypad handler (queue receive)
void ui_handle_key_event(int keycode)
{
    // Map your keypad codes to UI keys
    switch (keycode) {
        case 1: ui_menu_on_key(&g_menu, UI_KEY_UP); break;
        case 2: ui_menu_on_key(&g_menu, UI_KEY_DOWN); break;
        case 3: ui_menu_on_key(&g_menu, UI_KEY_OK); break;
        case 4: ui_menu_on_key(&g_menu, UI_KEY_BACK); break;
        default: return;
    }

    ui_menu_draw(&g_menu); // or set a flag and draw in a display task
}

*/