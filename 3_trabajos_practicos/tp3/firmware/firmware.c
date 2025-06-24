#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "helper.h"
#include "lcd.h"
#include "string.h"

// Pines y direcciones del LCD
#define I2C_PORT i2c0
#define LCD_I2C_ADDR 0x27
#define OUTPUT_GPIO 16
#define INPUT_GPIO 15
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5

//Defino la funcion que genera PWM
void pwm_user_init(uint32_t gpio, uint32_t freq);

// Semáforo de conteo
SemaphoreHandle_t xPulseSemaphore;

// Variable de conteo (solo la tarea debe leerla)
volatile uint32_t pulse_count = 0;

// ISR GPIO Funcion de Interrupcion
void gpio_callback(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (gpio == INPUT_GPIO && events & GPIO_IRQ_EDGE_RISE) {
        xSemaphoreGiveFromISR(xPulseSemaphore, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Tarea que cuenta el semáforo
void PulseCountTask(void *params) {
    while (1) {
        if (xSemaphoreTake(xPulseSemaphore, portMAX_DELAY) == pdTRUE) {
            pulse_count++;
        }
    }
}

// Tarea que muestra la frecuencia cada 1 segundo
void LCDDisplayTask(void *params) {
    char buffer[17];
    uint32_t count = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // Espera 1 segundo

        taskENTER_CRITICAL();
        count = pulse_count;
        pulse_count = 0;
        taskEXIT_CRITICAL();

        snprintf(buffer, sizeof(buffer), "Freq: %lu Hz", count);

        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_string("Contador Pulsos");
        lcd_set_cursor(1, 0);
        lcd_string(buffer);
    }
}
 
// Funcion que genera PWM

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

    // Inicializar I2C y LCD
    i2c_init(i2c0, 100000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    lcd_init(I2C_PORT, LCD_I2C_ADDR);
    lcd_clear();
    lcd_string("Inicializando...");

    // Inicializar GPIO de entrada
    gpio_init(INPUT_GPIO);
    gpio_set_dir(INPUT_GPIO, GPIO_IN);
    gpio_pull_down(INPUT_GPIO);
    gpio_set_irq_enabled_with_callback(INPUT_GPIO, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    // Funcion que genera PWM
    pwm_user_init(OUTPUT_GPIO, 9500);

    // Crear semáforo counting (capacidad máxima 100000, inicial 0)
    xPulseSemaphore = xSemaphoreCreateCounting(100000, 0);

    // Crear tareas
    xTaskCreate(PulseCountTask, "PulseCount", 256, NULL, 2, NULL);
    xTaskCreate(LCDDisplayTask, "LCDTask", 512, NULL, 1, NULL);

    // Iniciar FreeRTOS
    vTaskStartScheduler();

    // Nunca llega aquí
    while (1);
}