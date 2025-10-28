/*! @mainpage Proyecto Final Integrador: "Control automatico del nivel de agua"
 *
 * @section Descripcion general:
 *
 * El sistema mide periódicamente la distancia entre la tapa del tanque y la superficie del
 * agua mediante un sensor ultrasónico. A partir de esa distancia se calcula el nivel del 
 * líquido, si el nivel desciende por debajo de un umbral establecido, el microcontrolador 
 * lo detecta y activa una bomba hasta que se supere nuevamente el umbral , evitando que se
 * rebalse. También posee un botón de reset/manual que permite parar la bomba en casos de 
 * emergencia.
 * El sistema tambien posee comunicacion con el usuario, para que el mismo tenga acceso y 
 * conocimiento del estado del mismo.
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 22/10/2025 | Document creation		                         |
 *
 * @author Lucia Faes (luchifaess@gmail.com)
 * @author Valentina Gottig (valentinagottig@gmail.com)
 * 
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "timer_mcu.h"
#include "hc_sr04.h"
#include "uart_mcu.h"
#include "gpio_mcu.h"
#include "led.h"
/*==================[macros and definitions]=================================*/
#define ALTURA_TANQUE_CM   17.0        // altura total del tanque (cm)
#define REFRESH_PERIOD_US  1000000      // 1 s
#define UART_BAUDRATE      115200

/*==================[internal data definition]===============================*/
TaskHandle_t medir_task_handle = NULL;
float nivel_agua_cm = 0.0f;   // última medición del nivel (cm)

/*==================[internal functions declaration]=========================*/
void TimerNivelHandler(void *param);
void MedirNivelTask(void *pvParameter);

/*==================[internal functions definition]==========================*/

/**
 * @brief Callback del timer: despierta la tarea cada 1 s
 */
void TimerNivelHandler(void *param) {
    vTaskNotifyGiveFromISR(medir_task_handle, pdFALSE);
}

/**
 * @brief Tarea que mide el nivel de agua y lo envía por UART
 */
void MedirNivelTask(void *pvParameter) {
    uint16_t distancia_cm;
    float nivel_cm;
    float umbral = 13.0;
    float nivel_cm_min = umbral- 2.0;
    float nivel_cm_max = umbral + 2.0;

    while (true) {
        /* Espera la notificación del timer */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Leer distancia del sensor (cm) */
        distancia_cm = HcSr04ReadDistanceInCentimeters();

        /* Calcular nivel (altura - distancia) */
        if (distancia_cm >= ALTURA_TANQUE_CM)
            nivel_cm = 0.0;
        else
            nivel_cm = ALTURA_TANQUE_CM - (float)distancia_cm;

        /* Guardar el valor global */
        nivel_agua_cm = nivel_cm;

        if (nivel_agua_cm<nivel_cm_min){
            LedOn(LED_1);}
        else if (nivel_agua_cm>nivel_cm_max){
            LedOn(LED_2);
        }
        /* Enviar por UART */
        UartSendString(UART_PC, "Nivel de agua: ");
        UartSendString(UART_PC, (char *)UartItoa((uint32_t)nivel_agua_cm, 10));
        UartSendString(UART_PC, " cm\r\n");
    }
}

/*==================[external functions definition]==========================*/
void app_main(void) {
    LedsInit();
    /* Inicialización de periféricos */
    HcSr04Init(GPIO_3, GPIO_2);   // ajustar pines según conexión
    serial_config_t uart_cfg = {
        .port      = UART_PC,
        .baud_rate = UART_BAUDRATE,
        .func_p    = NULL,     // sin interrupciones de RX por ahora
        .param_p   = NULL
    };
    UartInit(&uart_cfg);

    /* Configuración del timer: 1 s */
    timer_config_t timer_nivel = {
        .timer   = TIMER_A,
        .period  = REFRESH_PERIOD_US,
        .func_p  = TimerNivelHandler,
        .param_p = NULL
    };
    TimerInit(&timer_nivel);

    /* Crear tarea */
    xTaskCreate(&MedirNivelTask, "NivelTask", 512, NULL, 5, &medir_task_handle);

    /* Iniciar timer */
    TimerStart(timer_nivel.timer);
}

/*==================[end of file]============================================*/

