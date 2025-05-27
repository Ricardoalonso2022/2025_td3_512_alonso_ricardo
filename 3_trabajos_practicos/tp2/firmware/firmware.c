#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define QUEUE_LENGTH 5
#define QUEUE_ITEM_SIZE sizeof(uint16_t)

//Variable de la cola de lectura

QueueHandle_t temp_queue;


// Tarea: lee del ADC canal 4 (temperatura interna)

void adc_read_task(void *pvParameters) {
    while (1) {

        // Canal 4 es sensor de temperatura
        adc_select_input(4); 
        uint16_t result = adc_read();

        // Enviar el valor a la cola        
        xQueueSend(temp_queue, &result, portMAX_DELAY);

        // Leer cada segundo
        vTaskDelay(pdMS_TO_TICKS(1000));  
    }
} 

// Tarea que recibe el dato de la cola y lo imprime
void print_task(void *pvParameters) {
    uint16_t value;
    while (1) {
        if (xQueueReceive(temp_queue, &value, portMAX_DELAY) == pdPASS) {

            // Convertir a voltaje
            float voltage = value * 3.3f / (1 << 12);
            float temperature = 27.0f - (voltage - 0.706f) / 0.001721f;

            // Imprime
            printf("ADC Raw: %d, Temperature: %.2f °C\n", value, temperature);
        }
    }
}

int main()

{
    stdio_init_all();

    // Inicializo el ADC
    adc_init ();
    adc_set_temp_sensor_enabled(true);
    adc_select_input (4);

    // Creo la cola
    temp_queue= xQueueCreate(5, sizeof(uint16_t));

 // Crear tareas
    xTaskCreate(adc_read_task, "ADC Reader", 256, NULL, 1, NULL);
    xTaskCreate(print_task, "Printer", 256, NULL, 1, NULL);

// Iniciar planificador

    vTaskStartScheduler();


    while (1);
}

