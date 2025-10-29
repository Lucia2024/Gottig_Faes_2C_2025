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
#include "lcditse0803.h"
#include <string.h>
/*==================[macros and definitions]=================================*/
#define ALTURA_TANQUE_CM   17.0        // altura total del tanque (cm)
#define REFRESH_PERIOD_US  1000000      // 1 s
#define LCD_REFRESH_US     1000000 
#define UART_BAUDRATE      115200
#define CONTROL_PERIOD_US  1000000 
#define NIVEL_MIN_CM      10.0
#define NIVEL_MAX_CM      14.0

/*==================[internal data definition]===============================*/
TaskHandle_t medir_task_handle = NULL;
TaskHandle_t lcd_task_handle = NULL;
TaskHandle_t control_task_handle = NULL;
float nivel_agua_cm = 0.0;   // última medición del nivel (cm)
/*--- Nuevo: estado del tanque ---*/
typedef enum {
    NIVEL_BAJO,
    NIVEL_ESTABLE,
    TANQUE_LLENO
} estado_tanque_t;

estado_tanque_t estado_tanque = TANQUE_LLENO;  // estado inicial

/*==================[internal functions declaration]=========================*/
void TimerNivelHandler(void *param);
void TimerLCDHandler(void *param);
void TimerNivelHandler(void *param);
void MedirNivelTask(void *pvParameter);
void TimerControlHandler(void *param);
void ControlNivelTask(void *pvParameter);
/*==================[internal functions definition]==========================*/

/**
 * @brief Callback del timer: despierta la tarea cada 1 s
 */
void TimerNivelHandler(void *param) {
    vTaskNotifyGiveFromISR(medir_task_handle, pdFALSE);
}
/**
 * @brief Callback del timer: despierta la tarea de LCD cada 1 s
 */
void TimerLCDHandler(void *param) {
    vTaskNotifyGiveFromISR(lcd_task_handle, pdFALSE);
}

void TimerControlHandler(void *param) {
    vTaskNotifyGiveFromISR(control_task_handle, pdFALSE);
}

void ControlNivelTask(void *pvParameter) {
    bool bomba_activada = false;
    
    while (true) {
        /* Esperar notificación del timer */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (nivel_agua_cm < NIVEL_MIN_CM) {
            // Activar bomba (rele y LED)
            GPIOOn(GPIO_18);   // ejemplo: relé en GPIO_18
            //LedOn(LED_1);      // LED indica bomba activa
            bomba_activada = true;
            estado_tanque = NIVEL_BAJO;

        } else if (nivel_agua_cm > NIVEL_MAX_CM) {
            // Apagar bomba (rele y LED)
            GPIOOff(GPIO_18);
            //LedOff(LED_1);
            bomba_activada = false;
            estado_tanque = TANQUE_LLENO;

        } else {
            estado_tanque = NIVEL_ESTABLE;
        }

        UartSendString(UART_PC, "Estado:\r\n");

         switch (estado_tanque) {
            case NIVEL_BAJO:
                UartSendString(UART_PC, "Nivel bajo, activando bomba\r\n");
                break;
            case NIVEL_ESTABLE:
                UartSendString(UART_PC, "Nivel estable\r\n");
                break;
            case TANQUE_LLENO:
                UartSendString(UART_PC, "Tanque lleno, desactivando bomba\r\n");
                break;
        }
    }
}

/**
 * @brief Tarea que mide el nivel de agua y lo envía por UART
 */
void MedirNivelTask(void *pvParameter) {
    uint16_t distancia_cm;
    float nivel_cm;

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
        /* Enviar por UART */
        UartSendString(UART_PC, "Nivel de agua: ");
        UartSendString(UART_PC, (char *)UartItoa((uint32_t)nivel_agua_cm, 10));
        UartSendString(UART_PC, " cm\r\n");
    }
}
/**
 * @brief Tarea que muestra el nivel en el display LCD cada 1 s
 */
void MostrarLCDTask(void *pvParameter) {
    while (true) {
        /* Espera notificación del timer */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t nivel_entero = (uint8_t)nivel_agua_cm;
        LcdItsE0803Write(nivel_entero);
        /* Opcional para mostrar porcentaje, aca podriamos usar una 
        tecla o mostrar directamente en porcentaje ya que es mas intuitivo*/
        // uint8_t porcentaje = (nivel_agua_cm / ALTURA_TANQUE_CM) * 100;
        // LcdItsE0803Write(porcentaje);
    }
}

/*==================[external functions definition]==========================*/
void app_main(void) {
    //LedsInit();
    HcSr04Init(GPIO_3, GPIO_2); 
    LcdItsE0803Init();   

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

    /* Configuración del timer de LCD */
    timer_config_t timer_lcd = {
        .timer   = TIMER_B,
        .period  = LCD_REFRESH_US,
        .func_p  = TimerLCDHandler,
        .param_p = NULL
    };
    TimerInit(&timer_lcd);

      /* Configurar timer para control de nivel (cada 0,5 s) */
    timer_config_t timer_control = {
        .timer   = TIMER_C,
        .period  = CONTROL_PERIOD_US,
        .func_p  = TimerControlHandler,
        .param_p = NULL
    };
    TimerInit(&timer_control);

    /* Crear tarea */
    xTaskCreate(&MedirNivelTask, "NivelTask", 512, NULL, 5, &medir_task_handle);
    xTaskCreate(&MostrarLCDTask, "LCDTask", 512, NULL, 4, &lcd_task_handle);
    xTaskCreate(&ControlNivelTask, "ControlTask", 512, NULL, 4, &control_task_handle);


    /* Iniciar timer */
    TimerStart(timer_nivel.timer);
    TimerStart(timer_lcd.timer);
    TimerStart(timer_control.timer);
}

/*==================[end of file]============================================*/

