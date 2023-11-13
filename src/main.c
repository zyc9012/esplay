#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "nofrendo.h"
#include "esp_partition.h"
#include "esp_spiffs.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include <dirent.h>
#include <limits.h>

#include "settings.h"
#include "power.h"
#include "sdcard.h"
#include "display.h"
#include "gamepad.h"
#include "audio.h"

const char *SD_BASE_PATH = "/sd";
static char *ROM_DATA;

extern bool forceConsoleReset;
int32_t scaleAlg;

char *osd_getromdata()
{
    printf("Initialized. ROM@%p\n", ROM_DATA);
    return (char *)ROM_DATA;
}

static const char *TAG = "main";

int entry(void)
{
    printf("nesemu (%s-%s).\n", "1.0.0", "0");

    settings_init();

    esplay_system_init();

    esp_err_t ret;

    audio_init(32000);

    char *fileName;

    char *romName = settings_load_str(SettingRomPath);

    if (romName)
    {
        fileName = system_util_GetFileName(romName);
        if (!fileName)
            abort();

        free(romName);
    }
    else
    {
        fileName = "nesemu-show3.nes";
    }

    int startHeap = esp_get_free_heap_size();
    printf("A HEAP:0x%x\n", startHeap);

    // Joystick.
    gamepad_init();

    // display
    display_init();
    display_prepare();

    // display brightness
    // int brightness;
    // settings_load(SettingBacklight, &brightness);
    // set_display_brightness(brightness);

    // load alghoritm
    settings_load(SettingAlg, &scaleAlg);

    // battery
    battery_level_init();

    gpio_reset_pin(GPIO_NUM_0); // To make sdcard work

    // Load ROM
    char *romPath = "/sd/NES/Nekketsu! Street Basket - Ganbare Dunk Heroes (Japan).nes";
    // char *romPath = settings_load_str(SettingRomPath);

    printf("osd_getromdata: Reading from sdcard.\n");

    // copy from SD card
    esp_err_t r = sdcard_open(SD_BASE_PATH);
    if (r != ESP_OK)
    {
        abort();
    }

    display_clear(0);
    display_show_hourglass();

    size_t fileSize = sdcard_get_filesize(romPath);
    ROM_DATA = malloc(fileSize);
    size_t fileSizeRead = sdcard_copy_file_to_memory(romPath, ROM_DATA);
    printf("app_main: fileSize=%d\n", fileSizeRead);
    if (fileSize == 0)
    {
        abort();
    }

    r = sdcard_close();
    if (r != ESP_OK)
    {
        abort();
    }

    // free(romPath);

    printf("NoFrendo start!\n");

    char *args[1] = {fileName};
    nofrendo_main(1, args);

    printf("NoFrendo died.\n");
    asm("break.n 1");
    return 0;
}