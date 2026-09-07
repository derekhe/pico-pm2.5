#include "pm25/lcd.hpp"

#include "pm25/canvas.hpp"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

namespace pm25 {
namespace {

constexpr uint kPinDc = 8;
constexpr uint kPinCs = 9;
constexpr uint kPinSck = 10;
constexpr uint kPinMosi = 11;
constexpr uint kPinReset = 12;
constexpr uint kPinBacklight = 13;

void set_output(uint pin, bool initial) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, initial);
}

}  // namespace

void Lcd::command(std::uint8_t command_byte, const std::uint8_t* data, std::uint8_t length) {
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(kPinDc, false);
    gpio_put(kPinCs, false);
    spi_write_blocking(spi1, &command_byte, 1);
    if (length > 0) {
        gpio_put(kPinDc, true);
        spi_write_blocking(spi1, data, length);
    }
    while (spi_is_busy(spi1)) {
        tight_loop_contents();
    }
    gpio_put(kPinCs, true);
}

bool Lcd::init() {
    spi_init(spi1, 40'000'000);
    gpio_set_function(kPinSck, GPIO_FUNC_SPI);
    gpio_set_function(kPinMosi, GPIO_FUNC_SPI);
    set_output(kPinCs, true);
    set_output(kPinDc, true);
    set_output(kPinReset, true);

    gpio_set_function(kPinBacklight, GPIO_FUNC_PWM);
    pwm_slice_ = pwm_gpio_to_slice_num(kPinBacklight);
    pwm_channel_ = pwm_gpio_to_channel(kPinBacklight);
    pwm_config pwm_config_value = pwm_get_default_config();
    pwm_config_set_clkdiv(&pwm_config_value, 50.0F);
    pwm_config_set_wrap(&pwm_config_value, 99);
    pwm_init(pwm_slice_, &pwm_config_value, true);
    set_backlight(80);

    gpio_put(kPinReset, true);
    sleep_ms(20);
    gpio_put(kPinReset, false);
    sleep_ms(20);
    gpio_put(kPinReset, true);
    sleep_ms(120);

    // ST7789VW register setup for the Waveshare Pico-LCD-1.3 panel.
    const std::uint8_t color_mode[] = {0x05};
    command(0x3a, color_mode, sizeof(color_mode));
    const std::uint8_t madctl[] = {0x70};  // USB-right landscape orientation, RGB order.
    command(0x36, madctl, sizeof(madctl));
    const std::uint8_t porch[] = {0x0c, 0x0c, 0x00, 0x33, 0x33};
    command(0xb2, porch, sizeof(porch));
    const std::uint8_t gate[] = {0x35};
    command(0xb7, gate, sizeof(gate));
    const std::uint8_t vcom[] = {0x19};
    command(0xbb, vcom, sizeof(vcom));
    const std::uint8_t lcm[] = {0x2c};
    command(0xc0, lcm, sizeof(lcm));
    const std::uint8_t vdv_enable[] = {0x01};
    command(0xc2, vdv_enable, sizeof(vdv_enable));
    const std::uint8_t vrh[] = {0x12};
    command(0xc3, vrh, sizeof(vrh));
    const std::uint8_t vdv[] = {0x20};
    command(0xc4, vdv, sizeof(vdv));
    const std::uint8_t frame_rate[] = {0x0f};
    command(0xc6, frame_rate, sizeof(frame_rate));
    const std::uint8_t power[] = {0xa4, 0xa1};
    command(0xd0, power, sizeof(power));
    const std::uint8_t positive_gamma[] = {
        0xd0, 0x04, 0x0d, 0x11, 0x13, 0x2b, 0x3f, 0x54, 0x4c, 0x18, 0x0d, 0x0b, 0x1f, 0x23};
    command(0xe0, positive_gamma, sizeof(positive_gamma));
    const std::uint8_t negative_gamma[] = {
        0xd0, 0x04, 0x0c, 0x11, 0x13, 0x2c, 0x3f, 0x44, 0x51, 0x2f, 0x1f, 0x1f, 0x20, 0x23};
    command(0xe1, negative_gamma, sizeof(negative_gamma));
    command(0x21);  // Display inversion on.
    command(0x11);  // Sleep out.
    sleep_ms(120);
    command(0x29);  // Display on.

    dma_channel_ = dma_claim_unused_channel(false);
    return dma_channel_ >= 0;
}

void Lcd::set_full_window() {
    const std::uint8_t columns[] = {0x00, 0x00, 0x00, 0xef};
    const std::uint8_t rows[] = {0x00, 0x00, 0x00, 0xef};
    command(0x2a, columns, sizeof(columns));
    command(0x2b, rows, sizeof(rows));
    command(0x2c);
}

void Lcd::present(const std::uint16_t* framebuffer) {
    set_full_window();
    gpio_put(kPinDc, true);
    gpio_put(kPinCs, false);
    spi_set_format(spi1, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    if (dma_channel_ >= 0) {
        dma_channel_config config = dma_channel_get_default_config(dma_channel_);
        channel_config_set_transfer_data_size(&config, DMA_SIZE_16);
        channel_config_set_read_increment(&config, true);
        channel_config_set_write_increment(&config, false);
        channel_config_set_dreq(&config, spi_get_dreq(spi1, true));
        dma_channel_configure(dma_channel_, &config, &spi_get_hw(spi1)->dr, framebuffer,
                              kDisplayWidth * kDisplayHeight, true);
        dma_channel_wait_for_finish_blocking(dma_channel_);
    } else {
        spi_write16_blocking(spi1, framebuffer, kDisplayWidth * kDisplayHeight);
    }
    while (spi_is_busy(spi1)) {
        tight_loop_contents();
    }
    gpio_put(kPinCs, true);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

void Lcd::set_backlight(std::uint8_t percent) {
    percent = percent > 100 ? 100 : percent;
    pwm_set_chan_level(pwm_slice_, pwm_channel_, percent == 0 ? 0 : percent - 1);
}

}  // namespace pm25
