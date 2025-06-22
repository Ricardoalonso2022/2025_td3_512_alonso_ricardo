#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "helper.h"

#define ENTRADA_GPIO 15
#define SALIDA_GPIO 16

void pwm_user_init(uint32_t gpio, uint32_t freq);
SemaphoreHandle_t contador_semaforo;

void polling_task(void *params) {
    bool estado_anterior = 0;
    bool estado_actual;

    while (1) {
        estado_actual = gpio_get(ENTRADA_GPIO);
        if (estado_anterior && estado_actual) {  // Detect flanco de subida
            xSemaphoreGive(contador_semaforo); // Incrementa el semáforo
        }
        estado_anterior = estado_actual;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void frequency_counter_task(void *params) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Espera 1 segundo

        UBaseType_t count = 0;
        while (xSemaphoreTake(contador_semaforo, 0) == pdTRUE) {
            count++;
        }

        printf("Frecuencia: %lu Hz\n", count);
        // Aquí puedes mostrar el valor en un LCD/I2C si lo deseas
    }
}

void pwm_user_init(uint32_t gpio, uint32_t freq) {
    // Asigna función de PWM
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    // Configura frecuencia de PWM e inicializa
    uint32_t slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice, frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000.0);
    pwm_set_wrap(slice, 1000000 / freq);
    pwm_set_gpio_level(gpio, 500000 /freq);
    pwm_set_enabled(slice, true); 
} 

int main() {
    stdio_init_all();
    gpio_init(ENTRADA_GPIO);
    gpio_set_dir(ENTRADA_GPIO, GPIO_IN);

    pwm_user_init(SALIDA_GPIO, 22222);

    contador_semaforo = xSemaphoreCreateCounting(100000, 0); // hasta 100K pulsos por segundo

    xTaskCreate(polling_task, "Poll", 256, NULL, 2, NULL);
    xTaskCreate(frequency_counter_task, "Freq", 256, NULL, 1, NULL);

    vTaskStartScheduler();
    while (1); // Nunca llega aquí
}