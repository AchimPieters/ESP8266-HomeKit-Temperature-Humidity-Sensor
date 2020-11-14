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
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>

#include <dht/dht.h>

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

homekit_characteristic_t temperature = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 0);
homekit_characteristic_t humidity = HOMEKIT_CHARACTERISTIC_(CURRENT_RELATIVE_HUMIDITY, 0);
homekit_characteristic_t status_active = HOMEKIT_CHARACTERISTIC_(STATUS_ACTIVE, 0);
homekit_characteristic_t status_fault = HOMEKIT_CHARACTERISTIC_(STATUS_FAULT, 0);


void temperature_sensor_task(void *_args) {
        gpio_set_pullup(SENSOR_PIN, false, false);

        float humidity_value, temperature_value;
        while (1) {
                bool success = dht_read_float_data(
                        DHT_TYPE_DHT1, SENSOR_PIN,
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

homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_sensor, .services=(homekit_service_t*[]){
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                        HOMEKIT_CHARACTERISTIC(NAME, "Humidity Sensor"),
                        HOMEKIT_CHARACTERISTIC(MANUFACTURER, "StudioPieters®"),
                        HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "C39LDDQZFFD"),
                        HOMEKIT_CHARACTERISTIC(MODEL, "HKSP1SE/H"),
                        HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.0.1"),
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
                        NULL

                }),
                NULL
        }),
        NULL
};

homekit_server_config_t config = {
        .accessories = accessories,
        .password = "123-45-678",
        .setupId="1QJ8",
};

void on_wifi_ready() {
}

void user_init(void) {
        uart_set_baud(0, 115200);
        homekit_server_init(&config);

        led_init();
        temperature_sensor_init();

}
