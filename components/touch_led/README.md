# Touch LED Component

This component provides touch-responsive LED control using WS2812 addressable LEDs integrated with capacitive touch sensors.

## Features

- **WS2812 LED Strip Control**: Full color control with brightness adjustment
- **Touch Detection**: Capacitive touch sensor integration with event callbacks
- **LED Animations**: Built-in animation effects (rainbow, breathe, fade, pulse, wave, sparkle)
- **Event System**: Touch event detection (press, release, long press, double tap)
- **Thread-Safe**: Multi-task safe with mutex protection
- **Configurable**: Flexible GPIO and parameter configuration

## Hardware Requirements

- ESP32/ESP32-S2/ESP32-S3/ESP32-C3/ESP32-C6/ESP32-H2
- WS2812/WS2812B LED strip
- Capacitive touch sensor (optional)

## Pin Configuration

| Function | Default GPIO | Configurable |
|----------|--------------|---------------|
| LED Data | GPIO_NUM_8   | Yes          |
| Touch    | GPIO_NUM_4   | Yes          |

## Usage Example

```c
#include "touch_led.h"

void app_main(void)
{
    // Configuration
    touch_led_config_t config = {
        .led_gpio = GPIO_NUM_8,
        .touch_gpio = GPIO_NUM_4,
        .led_count = 16,
        .max_brightness = 200,
        .touch_threshold = 1000,
        .touch_invert = false
    };

    // Initialize
    esp_err_t ret = touch_led_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE("app", "Failed to initialize touch LED: %s", esp_err_to_name(ret));
        return;
    }

    // Set callback for touch events
    touch_led_register_callback(touch_event_handler);

    // Set all LEDs to blue
    touch_led_set_all_color(TOUCH_LED_COLOR_BLUE);
    touch_led_update();

    // Start rainbow animation
    touch_led_start_animation(TOUCH_LED_ANIM_RAINBOW, 100, 
                             TOUCH_LED_COLOR_RED, TOUCH_LED_COLOR_BLUE);
}

void touch_event_handler(touch_event_t event, uint32_t duration)
{
    switch (event) {
        case TOUCH_EVENT_PRESS:
            ESP_LOGI("touch", "Touch pressed");
            touch_led_set_all_color(TOUCH_LED_COLOR_GREEN);
            touch_led_update();
            break;
            
        case TOUCH_EVENT_RELEASE:
            ESP_LOGI("touch", "Touch released after %lu ms", duration);
            touch_led_set_all_color(TOUCH_LED_COLOR_BLUE);
            touch_led_update();
            break;
            
        case TOUCH_EVENT_LONG_PRESS:
            ESP_LOGI("touch", "Long press detected (%lu ms)", duration);
            touch_led_start_animation(TOUCH_LED_ANIM_RAINBOW, 150,
                                     TOUCH_LED_COLOR_RED, TOUCH_LED_COLOR_BLUE);
            break;
            
        default:
            break;
    }
}
```

## Console Commands

The touch LED component provides comprehensive console commands for real-time control and configuration:

### Status and Information Commands

```bash
led touch status                  # Show complete LED status and configuration
led touch help                    # Display detailed command reference
```

### LED Color Control Commands

```bash
# Set color (single LED only)
led touch set <color>             # color: red|green|blue|white|yellow|cyan|magenta|orange|purple|off|RRGGBB
led touch set red                 # Set LED to red
led touch set FF6600              # Set LED to orange using RGB hex

# Brightness and clearing
led touch brightness <level>      # Set brightness (0-255)
led touch brightness 128          # Set brightness to 50%
led touch clear                   # Turn off LED
```

### Animation Control Commands

```bash
# Start animations
led touch animation start <mode> [speed] [color1] [color2]
led touch animation start rainbow 150   # Fast rainbow animation
led touch animation start breathe 50 green  # Slow green breathing
led touch animation start fade 100 red blue # Red to blue fade animation

# Stop animation
led touch animation stop          # Stop current animation

# Available animation modes:
# - fade: Fade in/out effect
# - rainbow: Cycling rainbow colors  
# - breathe: Breathing effect with specified color
# - pulse: Quick pulse effect
# - wave: Wave propagation effect
# - sparkle: Random sparkle effect
```

### Touch Sensor Control Commands

```bash
led touch sensor enable           # Enable touch detection
led touch sensor disable          # Disable touch detection
led touch sensor threshold <value> # Set touch sensitivity (0-4095)
led touch sensor threshold 800    # Set medium sensitivity
```

### Command Examples and Use Cases

**Basic LED Control:**
```bash
led touch status                  # Check current status
led touch set white               # Set LED to white
led touch brightness 200          # Increase brightness
led touch set off                 # Turn off LED
```

**Animation Control:**
```bash
led touch animation start rainbow 100   # Start rainbow at medium speed
led touch animation start breathe 30 blue  # Gentle blue breathing
led touch animation stop          # Stop current animation
led touch animation start sparkle 200 white  # Fast white sparkle effect
```

**Touch Sensor Adjustment:**
```bash
led touch sensor threshold 500    # High sensitivity (lower threshold)
led touch sensor threshold 1500   # Low sensitivity (higher threshold)
led touch sensor disable          # Disable touch during testing
led touch sensor enable           # Re-enable touch detection
```

**Advanced Color Control:**
```bash
led touch set FF0080              # Set LED to hot pink (RGB hex)
led touch set cyan                # Set LED to cyan
led touch brightness 150          # Adjust overall brightness
```

## API Reference

### Initialization Functions

- `touch_led_init()` - Initialize the touch LED system
- `touch_led_deinit()` - Deinitialize and cleanup resources

### LED Control Functions

- `touch_led_set_color()` - Set color for specific LED
- `touch_led_set_all_color()` - Set color for all LEDs
- `touch_led_set_brightness()` - Set brightness level (0-255)
- `touch_led_clear()` - Clear all LEDs
- `touch_led_update()` - Commit changes to LED strip

### Animation Functions

- `touch_led_start_animation()` - Start LED animation
- `touch_led_stop_animation()` - Stop current animation

### Touch Functions

- `touch_led_register_callback()` - Register touch event callback
- `touch_led_is_touched()` - Get current touch state
- `touch_led_get_touch_value()` - Get raw touch sensor value
- `touch_led_set_touch_enable()` - Enable/disable touch detection
- `touch_led_set_touch_threshold()` - Set touch detection threshold

### Status Functions

- `touch_led_get_status()` - Get LED strip status and statistics

## Animation Modes

| Mode | Description |
|------|-------------|
| `TOUCH_LED_ANIM_NONE` | No animation |
| `TOUCH_LED_ANIM_FADE` | Fade in/out effect |
| `TOUCH_LED_ANIM_RAINBOW` | Rainbow color cycle |
| `TOUCH_LED_ANIM_BREATHE` | Breathing effect |
| `TOUCH_LED_ANIM_PULSE` | Pulse effect |
| `TOUCH_LED_ANIM_WAVE` | Wave effect |
| `TOUCH_LED_ANIM_SPARKLE` | Sparkle effect |

## Predefined Colors

The component provides several predefined colors:

- `TOUCH_LED_COLOR_RED`
- `TOUCH_LED_COLOR_GREEN`
- `TOUCH_LED_COLOR_BLUE`
- `TOUCH_LED_COLOR_WHITE`
- `TOUCH_LED_COLOR_YELLOW`
- `TOUCH_LED_COLOR_CYAN`
- `TOUCH_LED_COLOR_MAGENTA`
- `TOUCH_LED_COLOR_ORANGE`
- `TOUCH_LED_COLOR_PURPLE`
- `TOUCH_LED_COLOR_OFF`

## Dependencies

This component requires the following ESP-IDF components:

- `driver` - GPIO and touch pad drivers
- `esp_timer` - High-resolution timer
- `freertos` - Real-time operating system
- `log` - Logging system
- `led_strip` - WS2812 LED strip driver (managed component)

## Configuration

The component can be configured through the `touch_led_config_t` structure:

```c
typedef struct {
    gpio_num_t led_gpio;           // GPIO pin for WS2812 LED data line
    gpio_num_t touch_gpio;         // GPIO pin for touch sensor
    uint16_t led_count;            // Number of LEDs in the strip
    uint32_t max_brightness;       // Maximum brightness (0-255)
    uint32_t touch_threshold;      // Touch detection threshold
    bool touch_invert;             // Invert touch logic (true for active low)
} touch_led_config_t;
```

## Thread Safety

All functions are thread-safe and can be called from multiple tasks simultaneously. Internal synchronization is handled using FreeRTOS mutexes.

## Performance Notes

- Animation task runs at configurable speed (1-255)
- Touch detection samples at 20Hz
- LED updates are optimized for minimal CPU usage
- Memory usage: ~2KB RAM + (led_count * 3) bytes for pixel buffer

## Troubleshooting

### Common Issues

1. **LEDs not lighting up**
   - Check GPIO pin configuration
   - Verify power supply (WS2812 requires 5V)
   - Ensure data line connection

2. **Touch not working**
   - Check touch GPIO pin
   - Adjust touch threshold value
   - Verify touch sensor wiring

3. **Erratic behavior**
   - Check power supply stability
   - Ensure proper grounding
   - Add decoupling capacitors

### Debug Logging

Enable debug logging by setting log level:

```c
esp_log_level_set("touch_led", ESP_LOG_DEBUG);
```

## License

Part of the robOS embedded operating system project.