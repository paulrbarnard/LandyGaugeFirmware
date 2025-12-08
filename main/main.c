#include "ST77916.h"
#include "PCF85063.h"
#include "QMI8658.h"
#include "SD_MMC.h"
#include "Wireless.h"
#include "TCA9554PWR.h"
#include "LVGL_Example.h"
#include "BAT_Driver.h"
#include "PWR_Key.h"
#include "PCM5101.h"
#include "MIC_Speech.h"
#include "CST820.h"
#include "clock.h"
#include "wifi_ntp.h"

void Driver_Loop(void *parameter)
{
    Wireless_Init();
    while(1)
    {
        QMI8658_Loop();
        PCF85063_Loop();
        BAT_Get_Volts();
        PWR_Loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}
void Driver_Init(void)
{
    PWR_Init();
    BAT_Init();
    I2C_Init();
    EXIO_Init();                    // Example Initialize EXIO
    Flash_Searching();
    PCF85063_Init();
    QMI8658_Init();
    xTaskCreatePinnedToCore(
        Driver_Loop, 
        "Other Driver task",
        4096, 
        NULL, 
        3, 
        NULL, 
        0);
}

void app_main(void)
{
    Driver_Init();

    SD_Init();
    Touch_Init();  // Initialize touch BEFORE display to avoid GPIO conflict
    LCD_Init();
    Audio_Init();
    // MIC_Speech_init();  // Wake word: "Hi ESP" - TODO: Fix model partition format
    // Play_Music("/sdcard","AAA.mp3");
    LVGL_Init();   // returns the screen object

// /********************* Clock Display *********************/
    // Initialize and display clock with current RTC time (before WiFi/NTP sync)
    clock_init();
    
    // Initialize WiFi and sync time from NTP in background
    if (wifi_ntp_init()) {
        ESP_LOGI("MAIN", "WiFi connected");
        if (wifi_ntp_sync_time()) {
            ESP_LOGI("MAIN", "Time synchronized from NTP");
        }
    }
    // Simulated_Touch_Init();  // Disabled - using real CST820 touch now
    // lv_demo_widgets();
    // lv_demo_keypad_encoder();
    // lv_demo_benchmark();
    // lv_demo_stress();
    // lv_demo_music();

    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
}






