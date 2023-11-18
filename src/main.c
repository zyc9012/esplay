#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "nofrendo.h"
#include "ugui.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "esp_app_format.h"

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

int num_menu = 6;
char menu_text[6][20] = {"WiFi AP *", "Volume", "Brightness", "Upscaler", "Scale Alg", "Quit"};
char scaling_text[3][20] = {"Native", "Normal", "Stretch"};
char scaling_alg_text[3][20] = {"Nearest Neighbor", "Bilinier Intrp.", "Box Filtered"};
int32_t wifi_en = 0;
int32_t volume = 25;
int32_t bright = 50;
int32_t scaling = SCALE_FIT;
int32_t scale_alg = NEAREST_NEIGHBOR;

char *osd_getromdata()
{
    printf("Initialized. ROM@%p\n", ROM_DATA);
    return (char *)ROM_DATA;
}

static void showOptionPage(int selected)
{
    ui_clear_screen();
    /* Header */
    UG_FillFrame(0, 0, 320 - 1, 16 - 1, C_BLUE);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLUE);
    char *msg = "Device Options";
    UG_PutString((320 / 2) - (strlen(msg) * 9 / 2), 2, msg);
    /* End Header */

    /* Footer */
    UG_FillFrame(0, 240 - 16 - 1, 320 - 1, 240 - 1, C_BLUE);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLUE);
    msg = "     Browse      Change       ";
    UG_PutString((320 / 2) - (strlen(msg) * 9 / 2), 240 - 15, msg);

    UG_FillRoundFrame(15, 240 - 15 - 1, 15 + (5 * 9) + 8, 237, 7, C_WHITE);
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_PutString(20, 240 - 15, "Up/Dn");

    UG_FillRoundFrame(140, 240 - 15 - 1, 140 + (3 * 9) + 8, 237, 7, C_WHITE);
    UG_PutString(145, 240 - 15, "< >");
    /* End Footer */

    UG_FillFrame(0, 16, 320 - 1, 240 - 20, C_BLACK);

    UG_SetForecolor(C_RED);
    UG_SetBackcolor(C_BLACK);
    UG_PutString(0, 240 - 30, "* restart required");
    esp_app_desc_t *desc = esp_ota_get_app_description();
    char idfVer[512];
    sprintf(idfVer, "IDF %s", desc->idf_ver);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLACK);
    UG_PutString(0, 240 - 72, desc->project_name);
    UG_PutString(0, 240 - 58, desc->version);
    UG_PutString(0, 240 - 44, idfVer);

    for (int i = 0; i < num_menu; i++)
    {
        short top = 18 + i * 15 + 8;
        if (i == selected)
            UG_SetForecolor(C_YELLOW);
        else
            UG_SetForecolor(C_WHITE);

        UG_PutString(0, top, menu_text[i]);

        // show value on right side
        switch (i)
        {
        case 0:
            if (i == selected)
                ui_display_switch(307, top, wifi_en, C_YELLOW, C_BLUE, C_GRAY);
            else
                ui_display_switch(307, top, wifi_en, C_WHITE, C_BLUE, C_GRAY);
            break;
        case 1:
            if (i == selected)
                ui_display_seekbar((320 - 103), top + 4, 100, (volume * 100) / 100, C_YELLOW, C_RED);
            else
                ui_display_seekbar((320 - 103), top + 4, 100, (volume * 100) / 100, C_WHITE, C_RED);
            break;
        case 2:
            if (i == selected)
                ui_display_seekbar((320 - 103), top + 4, 100, (bright * 100) / 100, C_YELLOW, C_RED);
            else
                ui_display_seekbar((320 - 103), top + 4, 100, (bright * 100) / 100, C_WHITE, C_RED);
            break;
        case 3:
            UG_PutString(319 - (strlen(scaling_text[scaling]) * 9), top, scaling_text[scaling]);
            break;

        case 4:
            UG_PutString(319 - (strlen(scaling_alg_text[scale_alg]) * 9), top, scaling_alg_text[scale_alg]);
            break;

        default:
            break;
        }
    }
    ui_flush();
}

static int showOption()
{
    int32_t wifi_state = wifi_en;
    int selected = 0;
    showOptionPage(selected);

    input_gamepad_state prevKey;
    gamepad_read(&prevKey);
    while (true)
    {
        input_gamepad_state key;
        gamepad_read(&key);
        if (!prevKey.values[GAMEPAD_INPUT_DOWN] && key.values[GAMEPAD_INPUT_DOWN])
        {
            ++selected;
            if (selected > num_menu - 1)
                selected = 0;
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_UP] && key.values[GAMEPAD_INPUT_UP])
        {
            --selected;
            if (selected < 0)
                selected = num_menu - 1;
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_LEFT] && key.values[GAMEPAD_INPUT_LEFT])
        {
            switch (selected)
            {
            case 0:
                wifi_en = ! wifi_en;
                settings_save(SettingWifi, (int32_t)wifi_en);
                break;
            case 1:
                volume -= 5;
                if (volume < 0)
                    volume = 0;
                settings_save(SettingAudioVolume, (int32_t)volume);
                break;
            case 2:
                bright -= 5;
                if (bright < 1)
                    bright = 1;
                set_display_brightness(bright);
                settings_save(SettingBacklight, (int32_t)bright);
                break;
            case 3:
                scaling--;
                if (scaling < 0)
                    scaling = 2;
                settings_save(SettingScaleMode, (int32_t)scaling);
                break;
            case 4:
                scale_alg--;
                if (scale_alg < 0)
                    scale_alg = 1;
                settings_save(SettingAlg, (int32_t)scale_alg);
                break;

            default:
                break;
            }
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_RIGHT] && key.values[GAMEPAD_INPUT_RIGHT])
        {
            switch (selected)
            {
            case 0:
                wifi_en = !wifi_en;
                settings_save(SettingWifi, (int32_t)wifi_en);
                break;
            case 1:
                volume += 5;
                if (volume > 100)
                    volume = 100;
                settings_save(SettingAudioVolume, (int32_t)volume);
                break;
            case 2:
                bright += 5;
                if (bright > 100)
                    bright = 100;
                set_display_brightness(bright);
                settings_save(SettingBacklight, (int32_t)bright);
                break;
            case 3:
                scaling++;
                if (scaling > 2)
                    scaling = 0;
                settings_save(SettingScaleMode, (int32_t)scaling);
                break;
            case 4:
                scale_alg++;
                if (scale_alg > 1)
                    scale_alg = 0;
                settings_save(SettingAlg, (int32_t)scale_alg);
                break;

            default:
                break;
            }
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_A] && key.values[GAMEPAD_INPUT_A])
            if (selected == 5)
            {
                vTaskDelay(10);
                break;
            }

        prevKey = key;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (wifi_en != wifi_state)
        return 1;

    return 0;
}

static int load_game(char* romPath)
{
    audio_init(32000);

    display_clear(0);
    // display_show_hourglass();

    gpio_reset_pin(GPIO_NUM_0); // To make sdcard work

    size_t fileSize = sdcard_get_filesize(romPath);
    ROM_DATA = malloc(fileSize);
    size_t fileSizeRead = sdcard_copy_file_to_memory(romPath, ROM_DATA);
    printf("app_main: fileSize=%d\n", fileSizeRead);
    if (fileSize == 0)
    {
        settings_save_str(SettingRomPath, "");
        abort();
    }

    esp_err_t r = sdcard_close();
    if (r != ESP_OK)
    {
        settings_save_str(SettingRomPath, "");
        abort();
    }

    printf("NoFrendo start!\n");

    char* fileName = system_util_GetFileName(romPath);
    if (!fileName)
        abort();

    // free(romPath);

    char *args[1] = {fileName};
    nofrendo_main(1, args);

    printf("NoFrendo died.\n");
    asm("break.n 1");
}

int entry(void)
{
    printf("nesemu (%s-%s).\n", "1.0.0", "0");

    settings_init();

    esplay_system_init();

    esp_err_t ret;

    int startHeap = esp_get_free_heap_size();
    printf("A HEAP:0x%x\n", startHeap);

    // Joystick.
    gamepad_init();

    // display
    display_init();
    display_prepare();

    // display brightness
    int brightness;
    settings_load(SettingBacklight, &brightness);
    set_display_brightness(brightness);

    // load alghoritm
    settings_load(SettingAlg, &scaleAlg);

    // battery
    battery_level_init();

    // copy from SD card
    esp_err_t r = sdcard_open(SD_BASE_PATH);
    if (r != ESP_OK)
    {
        abort();
    }

    ui_init();

    UG_FontSelect(&FONT_8X12);
    if(settings_load(SettingWifi, &wifi_en) != 0)
        settings_save(SettingWifi, (int32_t)wifi_en);
    if(settings_load(SettingAudioVolume, &volume) != 0)
        settings_save(SettingAudioVolume, (int32_t)volume);
    if(settings_load(SettingBacklight, &bright) != 0)
        settings_save(SettingBacklight, (int32_t)bright);
    if(settings_load(SettingScaleMode, &scaling) != 0)
        settings_save(SettingScaleMode, (int32_t)scaling);
    if(settings_load(SettingAlg, &scale_alg) != 0)
        settings_save(SettingAlg, (int32_t)scale_alg);

    while (1)
    {
        char *filename = ui_file_chooser("/sd/nes", "nes", 0, "Nintendo", showOption);
        if (filename)
        {
            settings_save_str(SettingRomPath, filename);
            ui_clear_screen();
            ui_flush();
            // display_show_hourglass();
            ui_deinit();
            printf("loading %s\n", filename);
            load_game(filename);
        }
    }

    return 0;
}