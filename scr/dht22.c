/** Copyright 2019 Achim Pieters | StudioPieters®

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NON INFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Build upon: ESP-HomeKit - MIT License - Copyright (c) 2017 Maxim Kulkin
 **/

#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <esplibs/libmain.h>
#include <queue.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>

#include <dht/dht.h>
#include <toggle.h>
#include "ota-api.h"

const int status_tampered_gpio = 14;

#define LED_INBUILT_GPIO 2  // this is the onboard LED used to show on/off only
#define SENSOR_PIN 4
bool led_on = false;
bool active = false;
int state;

void led_write(bool on) {
        gpio_write(LED_INBUILT_GPIO, on ? 0 : 1);
}

void led_init() {
        gpio_enable(LED_INBUILT_GPIO, GPIO_OUTPUT);
        led_write(led_on);
}

void sensor_identify_task(void *_args) {
        for (int i=0; i<3; i++) {
                for (int j=0; j<2; j++) {
                        led_write(true);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        led_write(false);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                vTaskDelay(250 / portTICK_PERIOD_MS);
        }
        led_write(led_on);
        vTaskDelete(NULL);
}

void sensor_identify(homekit_value_t _value) {
        printf("Sensor identify\n");
        xTaskCreate(sensor_identify_task, "Sensor identify", 128, NULL, 2, NULL);
}

homekit_characteristic_t ota_trigger = API_OTA_TRIGGER;
homekit_characteristic_t temperature = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 0);
homekit_characteristic_t humidity = HOMEKIT_CHARACTERISTIC_(CURRENT_RELATIVE_HUMIDITY, 0);
homekit_characteristic_t status_active = HOMEKIT_CHARACTERISTIC_(STATUS_ACTIVE, 0);
homekit_characteristic_t status_fault = HOMEKIT_CHARACTERISTIC_(STATUS_FAULT, 0);
homekit_characteristic_t status_tampered = HOMEKIT_CHARACTERISTIC_(STATUS_TAMPERED, 0);
homekit_characteristic_t status_low_battery = HOMEKIT_CHARACTERISTIC_(STATUS_LOW_BATTERY, 0);

void temperature_sensor_task(void *_args) {
        gpio_set_pullup(SENSOR_PIN, false, false);

        float humidity_value, temperature_value;
        while (1) {
                bool success = dht_read_float_data(
                        DHT_TYPE_DHT22, SENSOR_PIN,
                        &humidity_value, &temperature_value
                        );
                if (success) {
                        temperature.value.float_value = temperature_value;
                        humidity.value.float_value = humidity_value;
                        bool active = true;
                        homekit_characteristic_notify(&temperature, HOMEKIT_FLOAT(temperature_value));
                        homekit_characteristic_notify(&humidity, HOMEKIT_FLOAT(humidity_value));
                        homekit_characteristic_notify(&status_active, HOMEKIT_BOOL(active));
                } else {
                        printf("Couldnt read data from sensor\n");
                        bool state = true;
                        homekit_characteristic_notify(&status_fault, HOMEKIT_UINT8 (state));
                }
                vTaskDelay(3000 / portTICK_PERIOD_MS);
        }
}

void temperature_sensor_init() {
        xTaskCreate(temperature_sensor_task, "Temperatore Sensor", 256, NULL, 2, NULL);
}

void status_tampered_callback(bool high, void *context) {
        status_tampered.value = HOMEKIT_UINT8(high ? 1 : 0); // switch from 1:0 to 0:1 to inverse signal.
        homekit_characteristic_notify(&status_tampered, status_tampered.value);
}


int low_battery_value;
float battery_value;

void battery_low_task(void *_args) {
        while (1) {
                battery_value = sdk_system_adc_read();
                printf ("ADC voltage is %.3f\n", 1.0 / 1024 * sdk_system_adc_read()* 4.2);
                vTaskDelay(3000 / portTICK_PERIOD_MS);
                if (1.0 / 1024 * battery_value * 4.2 < 3.900) {
                        printf ("Battery value is low.\n");
                        low_battery_value = 1;
                }
                else{
                        printf ("Battery value is not low.\n");
                }
                homekit_characteristic_notify(&status_low_battery, HOMEKIT_UINT8(low_battery_value));
                vTaskDelay(3000 / portTICK_PERIOD_MS);
        }
}
void battery_low_init() {
        xTaskCreate(battery_low_task, "Battery Low", 256, NULL, 2, NULL);
}

homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_sensor, .services=(homekit_service_t*[]){
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                        HOMEKIT_CHARACTERISTIC(NAME, "Temperature Sensor"),
                        HOMEKIT_CHARACTERISTIC(MANUFACTURER, "StudioPieters®"),
                        HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "C39LDDQZFFD"),
                        HOMEKIT_CHARACTERISTIC(MODEL, "HKSP1SE/T"),
                        HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.0.2"),
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, sensor_identify),
                        NULL
                }),
                HOMEKIT_SERVICE(TEMPERATURE_SENSOR, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "Temperature Sensor"),
                        &temperature,
                        NULL
                }),
                HOMEKIT_SERVICE(HUMIDITY_SENSOR, .characteristics=(homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "Humidity Sensor"),
                        &humidity,
                        &status_active,
                        &status_fault,
                        &status_tampered,
                        &status_low_battery,
                        &ota_trigger,
                        NULL
                }),
                NULL
        }),
        NULL
};

homekit_server_config_t config = {
        .accessories = accessories,
        .password = "156-45-568",
        .setupId="1TH8",
};

void on_wifi_ready() {
}

void user_init(void) {
        uart_set_baud(0, 115200);
        homekit_server_init(&config);

        led_init();
        temperature_sensor_init();
        battery_low_init();

        if (toggle_create(status_tampered_gpio, status_tampered_callback, NULL)) {
                printf("Tampered with sensor\n");
        }
}
