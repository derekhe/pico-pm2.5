#pragma once

#include <cstdint>

namespace pm25 {

class Lcd {
public:
    bool init();
    void present(const std::uint16_t* framebuffer);
    void set_backlight(std::uint8_t percent);

private:
    void command(std::uint8_t command_byte, const std::uint8_t* data = nullptr, std::uint8_t length = 0);
    void set_full_window();
    int dma_channel_ = -1;
    std::uint32_t pwm_slice_ = 0;
    std::uint32_t pwm_channel_ = 0;
};

}  // namespace pm25
