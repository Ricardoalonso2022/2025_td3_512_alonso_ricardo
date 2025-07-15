// Librerias propias del micro
#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
// Librerias de FreRtos
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
// Librerias del LCD y del sensor
#include "bmp280.h"
#include "lcd.h"

// Defino los pines del I2C
#define I2C_PORT       i2c0     // Puerto principal del I2C
#define I2C_SDA_PIN    4        // Pin de datos del I2C
#define I2C_SCL_PIN    5        // Pin de clock del I2C
#define LCD_ADDR       0x27     // Direccion del I2C
#define I2C_FREQ       100000   // Frecuencia del I2C

// Defino los pines GPIO del MICROSWITCH y el LED
#define BUTTON_PIN     15      // GPIO entrada con interrupción
#define LED_PWM_PIN    16      // GPIO salida PWM para LED

// Defino constantes de seteo para la temperatura
#define SETPOINT       25.0f   // Setpoint fijo en °C
#define MAX_ERROR      50.0f   // Máximo error considerado para PWM

// Defino el valor del PWM
#define PWM_WRAP 1000          // PWM va de 0-1000

// Estructura de variables de presion y temperatura
typedef struct {
    float temperature;         // Variable float de la temperatura
    float pressure;            // Variable float de la presión
} sensor_data_t;               // Tipo de datos para declarar variable de estructura

// Variables de la cola y del semáforo
QueueHandle_t queue_sensor_data;     // Variable de la cola
SemaphoreHandle_t i2c_mutex;         // Variable del semaforo Mutex para el sensor
SemaphoreHandle_t sem_button;        // Variable del semaforo binario para el microswitch 

// Variable global de modo pantalla (0 o 1)
volatile int screen_mode = 0;        // Variable para seleccion de pantallas

// Variables globales para debounce 
TickType_t last_button_time = 0;                            // Variable que cuenta el tiempo de la ultima vez que se presiono el boton
const TickType_t debounce_delay = pdMS_TO_TICKS(200);       // Variable que se la carga el valor de 200 ticks para comparar tiempo antirrebote

// ISR del botón, o sea por interrupcion
void button_isr(uint gpio, uint32_t events) {                          // Funcion de interrupcion para lectura del pulsador
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;                     // Variable asociada al semaforo
    xSemaphoreGiveFromISR(sem_button, &xHigherPriorityTaskWoken);      // Se cede el semaforo para operar luego de apretar el boton
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);                      // Realiza el cambio de contexto freertos ya tiende la interuupcion
}

// Inicialización de PWM 
void init_pwm() {
    gpio_set_function(LED_PWM_PIN, GPIO_FUNC_PWM);            // Defino el pin como salida PWM
    uint slice = pwm_gpio_to_slice_num(LED_PWM_PIN);          // Ciclo de actividad para el PWM
    pwm_set_wrap(slice, PWM_WRAP);                            // Resolucion de PWM o valor maximo  
    pwm_set_chan_level(slice, PWM_CHAN_A, 0);                 // Duty inicial, comienza apagado
    pwm_set_enabled(slice, true);                             // Habilito PWM
}

// Inicialización del botón con interrupción y pull-up 
void init_button() {
    gpio_init(BUTTON_PIN);                    // Defino el pin del boton
    gpio_set_dir(BUTTON_PIN, GPIO_IN);        // Defino el pin del boton como entrada
    gpio_pull_up(BUTTON_PIN);                 // Usar resistencia interna de pull up
    gpio_set_irq_enabled_with_callback(BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &button_isr);  // Defino que es por interrupcion
}

// Tarea para el sensor BMP280
void vTaskSensor(void *pvParameters) {
    sensor_data_t data;                        // Variable del tipo estructura para los datos del senso
    int32_t raw_temp = 0, raw_pres = 0;        // Variable de tipo entera para los datos en crudo del sensor
    struct bmp280_calib_param calib;           // Variable de tipo estructura para la calibración del sensor

    // Carga los parámetros de fábrica del sensor
    bmp280_get_calib_params(&calib);

    while (1) {
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {                           // Tomar Mutex con TimeOut_ticks de 100 ms
            bmp280_read_raw(&raw_temp, &raw_pres);                                               // Lectura directa del sensor
            data.temperature = bmp280_convert_temp(raw_temp, &calib);                            // Compensación temperatura
            data.pressure = bmp280_convert_pressure(raw_pres, raw_temp, &calib) / 1000.0f;       // Compensación de presión
            xSemaphoreGive(i2c_mutex);                                                           // Entrega el semáforo
            xQueueSend(queue_sensor_data, &data, 0);                                             // Envia a la cola para el LCD
        }

        vTaskDelay(pdMS_TO_TICKS(1000));                                                         // Esperar 1 segundo entre lecturas
    }
}

// Tarea del LCD y control PWM
void vTaskLCD(void *pvParameters) {
    sensor_data_t data;                   // Variable de tipo estructura de datos del sensor
    char line1[17], line2[17];            // Vectores o buffers para carga del display
    float error, error_abs;               // Variables de tipo float para los errores
    uint slice = pwm_gpio_to_slice_num(LED_PWM_PIN);   // Funcion de porcion de PWM para encender el LED

    while (1) {
        if (xQueueReceive(queue_sensor_data, &data, portMAX_DELAY) == pdTRUE) {    // Cola que recibe el dato de lo que mide el sensor
            error = SETPOINT - data.temperature;    // Compara el error con el valor seteado
            error_abs = fabsf(error);               // Transforma el error en error absoluto

            if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {                       // Pregunta si el semaforo se tomo
                if (screen_mode == 0) {                                                          // Variable para selecion de pantalla del display
                    snprintf(line1, sizeof(line1), "Temp: %.1f %cC", data.temperature, '\xDF');  // Imprime la primer linea del display temperatura
                    snprintf(line2, sizeof(line2), "Pres: %.1f kPa", data.pressure);             // Imprime la segunda linea del display presion
                } else {                                                                         // Si se presiono el pulsador entra aca
                    snprintf(line1, sizeof(line1), "Set: %.1f %cC", SETPOINT, '\xDF');           // Imprime en primer linea el valor del setpoint
                    snprintf(line2, sizeof(line2), "Err: %.1f %cC", error, '\xDF');              // Imprime en la segunda linea el valor del error
                }

                lcd_set_cursor(0, 0);         // Posiciono el cursor en la primera linea
                lcd_string(line1);            // Escribo en la primera linea
                lcd_set_cursor(1, 0);         // Posiciono el cursor en la segunda linea
                lcd_string(line2);            // Escribo en la segunda linea
                xSemaphoreGive(i2c_mutex);
            }

            // PWM inverso proporcional al error absoluto
            uint16_t duty = 0;                       // Variable del manejo del PWM

            if (error_abs < 0.01f) {                 // Si el error absoluto es menor que cierto valor
                duty = 0;                            // Apagar LED si el error es despreciable
            } else if (error_abs < MAX_ERROR) {      // Si el erro absoluto es menor al maximo error
                uint16_t duty = (uint16_t)((1.0f - (error_abs / MAX_ERROR)) * PWM_WRAP);  // Formula que me da el valor del PWM en base al error
            } else {
                duty = 0;                            // Apagar LED si el error es despreciable
            }

            pwm_set_chan_level(slice, PWM_CHAN_A, duty);     // Funcion que compara valores del PWM
        }

        // Verificar pulsador (modo de pantalla)
        if (xSemaphoreTake(sem_button, 0) == pdTRUE) {          // Se fija si se apreto el pulsador, toma el semaforo
            TickType_t now = xTaskGetTickCount();               // Hace un conteo de ticks para un antirrebote
            if ((now - last_button_time) > debounce_delay) {    // Si la diferencia ente el conteo de ticks y el tiempo del pulsador es mas de 200 
            screen_mode = !screen_mode;                         // Elpulsador es valido e invierte la pantalla del LCD
            last_button_time = now;                             // Coloca el valor de cantidad de ticks leidos en la varaible del boton.
            }
        }
    }
}

// Inicialización general de hardware 
void init_hardware() {
    stdio_init_all();                                 // Inicializo todo lo referente al microcontrolador

    // I2C
    i2c_init(I2C_PORT, I2C_FREQ);                     // Inicializo el I2C por puerto y frecuencia de trabajo
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);    // habilito por I2C el pin de datos
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);    // Habilito por I2C el pin de clock
    gpio_pull_up(I2C_SDA_PIN);                        // Habilito el pull up para datos
    gpio_pull_up(I2C_SCL_PIN);                        // Habilito el pull up para el clock

    bmp280_init(I2C_PORT);                            // Inicializo el sensor puerto, la direccion esta en el .h
    lcd_init(I2C_PORT, LCD_ADDR);                     // Inicializo el LCD puerto y direccion

    init_pwm();               // Inicializo el PWM
    init_button();            // Inicializo el pulsador por interrupcion
}

   //Función principal main
int main() {
    init_hardware();          // Inicializo todo 

    // Creacion de recursos FREERTOS
    i2c_mutex = xSemaphoreCreateMutex();                              // Variable para manejo del semaforo mutex
    sem_button = xSemaphoreCreateBinary();                            // Variable para manejo del semaforo binario
    queue_sensor_data = xQueueCreate(4, sizeof(sensor_data_t));       // Variable para manejo de la cola

    // Crear tareas
    xTaskCreate(vTaskSensor, "Sensor", configMINIMAL_STACK_SIZE + 100, NULL, 2, NULL);    // Tarea para manejo del sensor
    xTaskCreate(vTaskLCD, "LCD", configMINIMAL_STACK_SIZE + 200, NULL, 2, NULL);          // Tarea paramanejo del LCD

    vTaskStartScheduler();   // Toma el control el scheduler

    while (1);               // Nunca debería llegar aquí
}
