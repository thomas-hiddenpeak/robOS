/**
 * @file color_console.c
 * @brief Color Correction Console Commands Implementation
 */

#include "color_console.h"
#include "color_correction.h"
#include "console_core.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "color_console";

// Forward declarations
static esp_err_t color_cmd_handler(int argc, char **argv);
static void print_color_status(void);
static void print_color_help(void);

/**
 * @brief Main color command handler
 */
static esp_err_t color_cmd_handler(int argc, char **argv)
{
    if (argc < 2) {
        print_color_help();
        return ESP_OK;
    }

    const char *subcmd = argv[1];

    // color enable
    if (strcmp(subcmd, "enable") == 0) {
        esp_err_t ret = color_correction_set_enabled(true);
        if (ret == ESP_OK) {
            printf("Color correction enabled\n");
        } else {
            printf("Failed to enable color correction: %s\n", esp_err_to_name(ret));
            return ret;
        }
    }
    // color disable
    else if (strcmp(subcmd, "disable") == 0) {
        esp_err_t ret = color_correction_set_enabled(false);
        if (ret == ESP_OK) {
            printf("Color correction disabled\n");
        } else {
            printf("Failed to disable color correction: %s\n", esp_err_to_name(ret));
            return ret;
        }
    }
    // color status
    else if (strcmp(subcmd, "status") == 0) {
        print_color_status();
    }
    // color whitepoint <r> <g> <b> [enable/disable]
    else if (strcmp(subcmd, "whitepoint") == 0) {
        if (argc < 5) {
            printf("Usage: color whitepoint <r> <g> <b> [enable|disable]\n");
            printf("  r, g, b: Scale factors (0.0-2.0)\n");
            printf("  Default is 'enable' if not specified\n");
            return ESP_ERR_INVALID_ARG;
        }

        float r = atof(argv[2]);
        float g = atof(argv[3]);
        float b = atof(argv[4]);
        bool enable = true;

        if (argc >= 6) {
            if (strcmp(argv[5], "disable") == 0) {
                enable = false;
            } else if (strcmp(argv[5], "enable") == 0) {
                enable = true;
            } else {
                printf("Invalid enable/disable option: %s\n", argv[5]);
                return ESP_ERR_INVALID_ARG;
            }
        }

        esp_err_t ret = color_correction_set_white_point(enable, r, g, b);
        if (ret == ESP_OK) {
            printf("White point set to R:%.2f G:%.2f B:%.2f (%s)\n", 
                   r, g, b, enable ? "enabled" : "disabled");
        } else {
            printf("Failed to set white point: %s\n", esp_err_to_name(ret));
            return ret;
        }
    }
    // color gamma <value> [enable/disable]
    else if (strcmp(subcmd, "gamma") == 0) {
        if (argc < 3) {
            printf("Usage: color gamma <value> [enable|disable]\n");
            printf("  value: Gamma value (0.1-4.0, typical: 2.2)\n");
            printf("  Default is 'enable' if not specified\n");
            return ESP_ERR_INVALID_ARG;
        }

        float gamma = atof(argv[2]);
        bool enable = true;

        if (argc >= 4) {
            if (strcmp(argv[3], "disable") == 0) {
                enable = false;
            } else if (strcmp(argv[3], "enable") == 0) {
                enable = true;
            } else {
                printf("Invalid enable/disable option: %s\n", argv[3]);
                return ESP_ERR_INVALID_ARG;
            }
        }

        esp_err_t ret = color_correction_set_gamma(enable, gamma);
        if (ret == ESP_OK) {
            printf("Gamma correction set to %.2f (%s)\n", 
                   gamma, enable ? "enabled" : "disabled");
        } else {
            printf("Failed to set gamma correction: %s\n", esp_err_to_name(ret));
            return ret;
        }
    }
    // color brightness <factor> [enable/disable]
    else if (strcmp(subcmd, "brightness") == 0) {
        if (argc < 3) {
            printf("Usage: color brightness <factor> [enable|disable]\n");
            printf("  factor: Brightness factor (0.0-2.0, 1.0=no change)\n");
            printf("  Default is 'enable' if not specified\n");
            return ESP_ERR_INVALID_ARG;
        }

        float factor = atof(argv[2]);
        bool enable = true;

        if (argc >= 4) {
            if (strcmp(argv[3], "disable") == 0) {
                enable = false;
            } else if (strcmp(argv[3], "enable") == 0) {
                enable = true;
            } else {
                printf("Invalid enable/disable option: %s\n", argv[3]);
                return ESP_ERR_INVALID_ARG;
            }
        }

        esp_err_t ret = color_correction_set_brightness(enable, factor);
        if (ret == ESP_OK) {
            printf("Brightness enhancement set to %.2f (%s)\n", 
                   factor, enable ? "enabled" : "disabled");
        } else {
            printf("Failed to set brightness enhancement: %s\n", esp_err_to_name(ret));
            return ret;
        }
    }
    // color saturation <factor> [enable/disable]
    else if (strcmp(subcmd, "saturation") == 0) {
        if (argc < 3) {
            printf("Usage: color saturation <factor> [enable|disable]\n");
            printf("  factor: Saturation factor (0.0-2.0, 1.0=no change)\n");
            printf("  Default is 'enable' if not specified\n");
            return ESP_ERR_INVALID_ARG;
        }

        float factor = atof(argv[2]);
        bool enable = true;

        if (argc >= 4) {
            if (strcmp(argv[3], "disable") == 0) {
                enable = false;
            } else if (strcmp(argv[3], "enable") == 0) {
                enable = true;
            } else {
                printf("Invalid enable/disable option: %s\n", argv[3]);
                return ESP_ERR_INVALID_ARG;
            }
        }

        esp_err_t ret = color_correction_set_saturation(enable, factor);
        if (ret == ESP_OK) {
            printf("Saturation enhancement set to %.2f (%s)\n", 
                   factor, enable ? "enabled" : "disabled");
        } else {
            printf("Failed to set saturation enhancement: %s\n", esp_err_to_name(ret));
            return ret;
        }
    }
    // color reset
    else if (strcmp(subcmd, "reset") == 0) {
        color_correction_config_t default_config;
        esp_err_t ret = color_correction_get_default_config(&default_config);
        if (ret == ESP_OK) {
            ret = color_correction_set_config(&default_config);
            if (ret == ESP_OK) {
                printf("Color correction reset to default settings\n");
            } else {
                printf("Failed to reset color correction: %s\n", esp_err_to_name(ret));
                return ret;
            }
        } else {
            printf("Failed to get default configuration: %s\n", esp_err_to_name(ret));
            return ret;
        }
    }
    // color save
    else if (strcmp(subcmd, "save") == 0) {
        printf("Color correction settings are automatically saved to NVS\n");
    }
    // color export <filename>
    else if (strcmp(subcmd, "export") == 0) {
        if (argc < 3) {
            printf("Usage: color export <filename>\n");
            printf("  filename: Path to save configuration (e.g., /sdcard/color_config.json)\n");
            return ESP_ERR_INVALID_ARG;
        }

        const char *filename = argv[2];
        esp_err_t ret = color_correction_export_config(filename);
        if (ret == ESP_OK) {
            printf("Color correction configuration exported to: %s\n", filename);
        } else {
            printf("Failed to export configuration: %s\n", esp_err_to_name(ret));
            return ret;
        }
    }
    // color import <filename>
    else if (strcmp(subcmd, "import") == 0) {
        if (argc < 3) {
            printf("Usage: color import <filename>\n");
            printf("  filename: Path to configuration file (e.g., /sdcard/color_config.json)\n");
            return ESP_ERR_INVALID_ARG;
        }

        const char *filename = argv[2];
        esp_err_t ret = color_correction_import_config(filename);
        if (ret == ESP_OK) {
            printf("Color correction configuration imported from: %s\n", filename);
        } else {
            printf("Failed to import configuration: %s\n", esp_err_to_name(ret));
            return ret;
        }
    }
    // Unknown subcommand
    else {
        printf("Unknown subcommand: %s\n", subcmd);
        print_color_help();
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * @brief Print current color correction status
 */
static void print_color_status(void)
{
    color_correction_config_t config;
    esp_err_t ret = color_correction_get_config(&config);
    if (ret != ESP_OK) {
        printf("Failed to get color correction configuration: %s\n", esp_err_to_name(ret));
        return;
    }

    printf("Color Correction Status:\n");
    printf("  Overall: %s\n", config.enabled ? "Enabled" : "Disabled");
    
    printf("  White Point Correction: %s\n", config.white_point.enabled ? "Enabled" : "Disabled");
    if (config.white_point.enabled) {
        printf("    R: %.2f, G: %.2f, B: %.2f\n", 
               config.white_point.red_scale, 
               config.white_point.green_scale, 
               config.white_point.blue_scale);
    }
    
    printf("  Gamma Correction: %s\n", config.gamma.enabled ? "Enabled" : "Disabled");
    if (config.gamma.enabled) {
        printf("    Gamma: %.2f\n", config.gamma.gamma);
    }
    
    printf("  Brightness Enhancement: %s\n", config.brightness.enabled ? "Enabled" : "Disabled");
    if (config.brightness.enabled) {
        printf("    Factor: %.2f\n", config.brightness.factor);
    }
    
    printf("  Saturation Enhancement: %s\n", config.saturation.enabled ? "Enabled" : "Disabled");
    if (config.saturation.enabled) {
        printf("    Factor: %.2f\n", config.saturation.factor);
    }
}

/**
 * @brief Print color correction help
 */
static void print_color_help(void)
{
    printf("Color Correction Commands:\n");
    printf("  color enable                     - Enable color correction\n");
    printf("  color disable                    - Disable color correction\n");
    printf("  color status                     - Show current settings\n");
    printf("  color whitepoint <r> <g> <b>     - Set white point correction\n");
    printf("  color gamma <value>              - Set gamma correction\n");
    printf("  color brightness <factor>        - Set brightness enhancement\n");
    printf("  color saturation <factor>        - Set saturation enhancement\n");
    printf("  color reset                      - Reset to default settings\n");
    printf("  color save                       - Save settings to NVS (auto)\n");
    printf("  color export <filename>          - Export config to SD card\n");
    printf("  color import <filename>          - Import config from SD card\n");
    printf("\nParameters:\n");
    printf("  r, g, b: Scale factors (0.0-2.0, default: 1.0)\n");
    printf("  gamma: Gamma value (0.1-4.0, typical: 2.2)\n");
    printf("  factor: Enhancement factor (0.0-2.0, 1.0=no change)\n");
    printf("\nExamples:\n");
    printf("  color enable\n");
    printf("  color whitepoint 0.9 1.0 1.1\n");
    printf("  color gamma 2.2\n");
    printf("  color brightness 1.2\n");
    printf("  color saturation 1.1\n");
    printf("  color export /sdcard/my_config.json\n");
    printf("  color import /sdcard/my_config.json\n");
}

esp_err_t color_correction_register_console_commands(void)
{
    console_cmd_t color_cmd = {
        .command = "color",
        .help = "Color correction control and configuration",
        .hint = "color <subcmd> [args...]",
        .func = color_cmd_handler,
        .min_args = 0,
        .max_args = 0  // Unlimited
    };

    esp_err_t ret = console_register_command(&color_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register color command: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Color correction console commands registered");
    return ESP_OK;
}