// Definición de las librerias a usar
#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "hardware/i2c.h"
#include "bmp280.h"
#include "lcd.h"

// Definición de los pines I2C
#define I2C_PORT i2c0
#define SDA_PIN 4
#define SCL_PIN 5
// Tamaño de la memoria para las tareas
#define STACK_SIZE 512
// Frecuencia de comunicación de 100 khz
#define I2C_FREQ  100000   
// Dirección del sensor BMP280
#define ADDR_BMP280 0x76
// Dirección del LCD
#define LCD_ADDR 0x27

// Estructura para los datos
typedef struct {
    float temperature;      // Variable float de la temperatura
    float pressure;         // Variable float de la presión
} sensor_data_t;            // Tipo de datos para declarar variable de estructura

// Variables de la cola y del semáforo
QueueHandle_t sensorQueue;              // Variable de la cola
SemaphoreHandle_t i2cMutex;             // Variable del semáforo 

// Tarea para leer el BMP280
void bmp280_task(void *pvParameters) {
    sensor_data_t data;                         // Variable del tipo estructura para los datos del sensor
    int32_t raw_temp = 0, raw_pres = 0;         // Variable de tipo entera para los datos en crudo del sensor
    struct bmp280_calib_param calib;            // Variable de tipo estructura para la calibración del sensor

    // Carga los parámetros de fábrica del sensor
    bmp280_get_calib_params(&calib);  

    while (1) {
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {                       // Tomar Mutex con TimeOut_ticks de 100 ms
            bmp280_read_raw(&raw_temp, &raw_pres);                                          // Lectura directa del sensor
            data.temperature = bmp280_convert_temp(raw_temp, &calib);                       // Compensación temperatura
            data.pressure = bmp280_convert_pressure(raw_pres, raw_temp, &calib);            // Compensación de presión
            xSemaphoreGive(i2cMutex);                                                       // Entrega el semáforo

            xQueueSend(sensorQueue, &data, 0);                                              // Envia a la cola para el LCD
        }

        vTaskDelay(pdMS_TO_TICKS(1000));                                                     // Esperar 1 segundo entre lecturas
    }
  }

// Tarea para mostrar en LCD
void lcd_task(void *pvParameters) {
    sensor_data_t data;                             // Variable que toma los datos el swnsor
    char line1[17];                                 // Vector o buffer de la primera linea del display
    char line2[17];                                 // Vector o buffer de la segunda linea del display

    while (1) {
        if (xQueueReceive(sensorQueue, &data, portMAX_DELAY)) {                   // Pregunta a la cola si hay un dato recibido del sensor 
            if (xSemaphoreTake(i2cMutex, portMAX_DELAY)) {                        // Toma el semáforo  preguntando si recibió un dato
                
                snprintf(line1, 17, "Temp: %.2f %cC", data.temperature,'\xDF');   // Manda al display el dato de temp, con 2 digitos de resol.
                snprintf(line2, 17, "Pres: %.2f kPa", data.pressure / 1000.0f);   // Lo mismo para la presión, dividido 1000 para ser en Kpa
                lcd_set_cursor(0, 0);   // Ubica el cursor en la primera linea   
                lcd_string(line1);      // Imprime la primera linea
                lcd_set_cursor(1, 0);   // Ubica el cursor en la segunda linea
                lcd_string(line2);      // Imprime la segunda linea

                xSemaphoreGive(i2cMutex);  // Entrega el semáforo
            }
        }
    }
}

// Inicialización del I2C
void i2c_setup() {

    stdio_init_all();                             // Inicializa cosas del micro
    i2c_init(I2C_PORT,I2C_FREQ);                  // Inicializa el puerto y la frecuencia del I2C
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);    // Inicializa el pin de la salida GPIO de datos
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);    // Inicializa el pin de la salida GPIO de clock
    gpio_pull_up(SDA_PIN);                        // habilita la resistencia puul-up de datos
    gpio_pull_up(SCL_PIN);                        // habikita la resitencia pull-up de clock
    bmp280_init(I2C_PORT);                        // Inicializa el puerto que funciona el sensor
    lcd_init(I2C_PORT, LCD_ADDR);                 // Inicializa el puerto y la dirección de funcionamiento del display
    lcd_clear();                                  // Limpia el display 
}

// Función principal Main
int main() {
   
    i2c_setup();  // Función que inicializa el I2C del sensor y del display

    sensorQueue = xQueueCreate(5, sizeof(sensor_data_t));            // Creo la cola de datos
    i2cMutex = xSemaphoreCreateMutex();                              // Creo el semáforo Nutex

    xTaskCreate(bmp280_task, "BMP280", STACK_SIZE, NULL, 1, NULL);   // Creo la tarea del manejo del sensor con prioridad 1
    xTaskCreate(lcd_task, "LCD", STACK_SIZE, NULL, 1, NULL);         // Creo la tarea del manejo del Display con prioridad 1

    vTaskStartScheduler();  // Le doy en control al scneduler 
    while (1);              // Si todo está bien no debe llegar acá
}
