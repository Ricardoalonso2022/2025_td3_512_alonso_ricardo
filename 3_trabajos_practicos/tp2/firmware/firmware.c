#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Variable de la cola
QueueHandle_t adcQueue;

// ISR del ADC
void __isr adc_irq_handler() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    while (adc_fifo_get_level()) {
        uint16_t adc_cola = adc_fifo_get();
        xQueueSendFromISR(adcQueue, &adc_cola, &xHigherPriorityTaskWoken);
    }

    adc_run(false); // detener conversión
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Tarea para leer temperatura desde la cola
void temperature_task(void *params) {
    uint16_t adc_cola;
    while (1) {
        if (xQueueReceive(adcQueue, &adc_cola, portMAX_DELAY) == pdTRUE) {
            float voltage = adc_cola * 3.3f / (1 << 12); // 12 bits
            float temperature = 27 - (voltage - 0.706f) / 0.001721f;
            printf("Temperatura: %.2f °C\n", temperature);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Tarea para iniciar conversiones ADC periódicamente
void adc_trigger_task(void *params) {
    while (1) {
        adc_run(true); // inicia conversión
        vTaskDelay(pdMS_TO_TICKS(1000)); // esperar antes de siguiente conversión
    }
}

int main() {
    stdio_init_all();

    // Inicializar ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    adc_fifo_setup(
        true,    // habilitar FIFO
        true,    // habilitar solicitud IRQ
        1,       // solicitud IRQ cuando FIFO tiene al menos 1 muestra
        false,
        false
    );

    irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_irq_handler);
    irq_set_enabled(ADC_IRQ_FIFO, true);
    adc_irq_set_enabled(true);

    // Crear cola
    adcQueue = xQueueCreate(10, sizeof(uint16_t));

    if (adcQueue == NULL) {
        printf("No se pudo crear la cola.\n");
        while (true);
    }

    // Crear tareas
    xTaskCreate(temperature_task, "TemperatureTask", 256, NULL, 1, NULL);
    xTaskCreate(adc_trigger_task, "ADCTriggerTask", 256, NULL, 1, NULL);

    // Iniciar scheduler
    vTaskStartScheduler();

    while (1); // Nunca se llega aquí
}
