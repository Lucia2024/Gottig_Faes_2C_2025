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
#include "switch.h"
/*==================[macros and definitions]=================================*/
#define ALTURA_TANQUE_CM   17      // altura total del tanque (cm)
#define REFRESH_PERIOD_US  100000   // 100 ms
#define UART_BAUDRATE      115200
#define CONTROL_PERIOD_US  500000 //500ms
#define NIVEL_MIN_CM      11
#define NIVEL_MAX_CM      13

/*==================[internal data definition]===============================*/
/**
 * @brief Handler de la tarea de medición del nivel de agua.
 */
TaskHandle_t medir_task_handle = NULL;

/**
 * @brief Handler de la tarea que controla el estado de la bomba según el nivel.
 */
TaskHandle_t control_task_handle = NULL;

/**
 * @brief Handler de la tarea de control manual mediante pulsadores.
 */
TaskHandle_t control_manual_task_handle = NULL;

/**
 * @brief Última medición del nivel de agua, expresada en centímetros.
 */
uint8_t nivel_agua_cm = 0;  

/**
 * @brief Estructura que contiene los tres estados posibles del tanque.
 */
typedef enum {
    NIVEL_BAJO,
    NIVEL_ESTABLE,
    TANQUE_LLENO
} estado_tanque_t;

/**
 * @brief Estado actual del tanque de agua (bajo, estable o lleno).
 */
volatile estado_tanque_t estado_tanque = TANQUE_LLENO;  

/**
 * @brief Bandera que indica si el control manual está activo.
 * 
 * Si es true, el control automático de la bomba se deshabilita y la bomba se maneja manualmente.
 */
volatile bool control_manual_activo = false;

/**
 * @brief Bandera de control manual de la válvula de desagote.
 */
volatile bool control_valvula_manual = false;
/*==================[internal functions declaration]=========================*/
void TimerNivelHandler(void *param);
void MedirNivelTask(void *pvParameter);
void TimerControlHandler(void *param);
void ControlNivelTask(void *pvParameter);

/*==================[internal functions definition]==========================*/

/**
 * @brief Callback del timer que notifica a la tarea de medición del nivel.
 * @param param Puntero a parámetros (no usado).
 */
void TimerNivelHandler(void *param) {
    vTaskNotifyGiveFromISR(medir_task_handle, pdFALSE);
}

/**
 * @brief Callback del timer que activa la tarea de control automático de nivel.
 * @param param Puntero a parámetros (no usado).
 */
void TimerControlHandler(void *param) {
    vTaskNotifyGiveFromISR(control_task_handle, pdFALSE);
}

/**
 * @brief Tarea que controla la bomba según el nivel de agua medido y lo envía por UART.
 * @param pvParameter Parámetro de tarea (no usado).
 */
void ControlNivelTask(void *pvParameter) {
    bool bomba_activada = false;
    
    while (true) {
        /* Esperar notificación del timer */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Si está activo el control manual, salteamos el control automático
        if (control_manual_activo) {
            continue;
        }

        if (nivel_agua_cm < NIVEL_MIN_CM) {
            // Activar bomba (rele y LED)
            GPIOOff(GPIO_6);   // ejemplo: relé en GPIO_6
            //LedOn(LED_1);      // LED indica bomba activa
            estado_tanque = NIVEL_BAJO;

        } else if (nivel_agua_cm > NIVEL_MAX_CM) {
            // Apagar bomba (rele y LED)
            GPIOOn(GPIO_6);
            //LedOff(LED_1);
            estado_tanque = TANQUE_LLENO;

        } else {
            estado_tanque = NIVEL_ESTABLE;
        }

        UartSendString(UART_PC, "Estado: ");

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

        vTaskDelay(pdMS_TO_TICKS(50)); // pequeña pausa
    }
}

/**
 * @brief Tarea encargada de leer el sensor ultrasónico, actualizar el nivel y enviarlo por UART, LCD y serial-plotter.
 * @param pvParameter Parámetro de tarea (no usado).
 */
void MedirNivelTask(void *pvParameter) {
    uint16_t distancia_cm;
    uint16_t nivel_cm;

    while (true) {
        /* Espera la notificación del timer */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Leer distancia del sensor (cm) */
        distancia_cm = HcSr04ReadDistanceInCentimeters();

        /* Calcular nivel (altura - distancia) */
        if (distancia_cm >= ALTURA_TANQUE_CM)
            nivel_cm = 0;
        else
            nivel_cm = ALTURA_TANQUE_CM - distancia_cm;

        /* Guardar el valor global */
        nivel_agua_cm = nivel_cm;
        /* Enviar por UART */
        UartSendString(UART_PC, "Nivel de agua: ");
        UartSendString(UART_PC, (char *)UartItoa((uint32_t)nivel_agua_cm, 10));
        UartSendString(UART_PC, " cm\r\n");
        
        //Muestra el nivel en el display LCD
        LcdItsE0803Write(nivel_agua_cm);

        // ---------- Envío para el Serial Plotter ----------
        char mensaje[30];
        char *valor_str = (char *)UartItoa((uint32_t)nivel_agua_cm, 10);
        strcpy(mensaje, ">brightness:");
        strcat(mensaje, valor_str);
        strcat(mensaje, "\r\n");
        UartSendString(UART_PC, mensaje);



    }
}

/**
 * @brief Tarea que permite el control manual mediante pulsadores físicos.
 * Verifica si el pulsador está presionado, y si lo está, detiene la bomba y muestra un mensaje por UART.
 * @param pvParameter Parámetro de tarea (no usado).
 */
void ControlManualTask(void *pvParameter) {

while (1) {
    int8_t estado_switch = SwitchesRead();

    /* Si el usuario presiona el SWITCH_1, alterna entre modo manual y automático */
    if (estado_switch == SWITCH_1) {
        if (control_manual_activo) {
            // Si ya estaba en modo manual → volver a automático
            LedOff(LED_1);
            control_manual_activo = false;
            UartSendString(UART_PC, "\r\n[CONTROL MANUAL] Desactivado, vuelve a control automático\r\n");

        } else {
            // Si estaba en automático → pasar a manual y apagar bomba
            GPIOOn(GPIO_6);   // apagar bomba
            LedOn(LED_1);
            control_manual_activo = true;
            UartSendString(UART_PC, "\r\n[CONTROL MANUAL] Activado: bomba detenida manualmente\r\n");
        }
    }

   /* SWITCH_2: alterna modo manual de válvula */
    else if (estado_switch == SWITCH_2) {
        if (control_valvula_manual) {
            // Estaba activada manualmente → apagar válvula
            control_valvula_manual = false;
            GPIOOn(GPIO_7);   // cerrar válvula
            LedOff(LED_2);
            UartSendString(UART_PC, "\r\n[CONTROL MANUAL] Válvula cerrada\r\n");
        } else {
            // Estaba apagada → activar manualmente
            control_valvula_manual = true;
            GPIOOff(GPIO_7);  // abrir válvula
            LedOn(LED_2);
            UartSendString(UART_PC, "\r\n[CONTROL MANUAL] Válvula en desagote manual\r\n");
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // pequeño retardo para evitar rebotes
}
}


/*==================[external functions definition]==========================*/
/**
 * @brief Función principal del sistema.
 * 
 * Inicializa todos los periféricos y dispositivos (sensor ultrasónico, display LCD, GPIOs, UART, LEDs y switches),
 * configura los timers y crea las tareas del sistema:
 * 
 * - @c MedirNivelTask: mide el nivel de agua y actualiza la pantalla LCD.
 * - @c ControlNivelTask: controla la bomba según los umbrales configurados.
 * - @c ControlManualTask: permite la intervención manual del usuario.
 * 
 * Finalmente, se inician los timers para comenzar el funcionamiento periódico.
 */
void app_main(void) {
    LedsInit();
    HcSr04Init(GPIO_3, GPIO_2); 
    LcdItsE0803Init();
    SwitchesInit();
    //BOMBA
    GPIOInit(GPIO_6, GPIO_OUTPUT);
    GPIOOn(GPIO_6);
    //VALVULA
    GPIOInit(GPIO_7, GPIO_OUTPUT);
    GPIOOn(GPIO_7);

    /* Configuración dela uart */
    serial_config_t uart_cfg = {
        .port      = UART_PC,
        .baud_rate = UART_BAUDRATE,
        .func_p    = NULL,     // sin interrupciones de RX por ahora
        .param_p   = NULL
    };
    UartInit(&uart_cfg);

    /* Configuración del timer para medicion: 100 ms */
    timer_config_t timer_nivel = {
        .timer   = TIMER_A,
        .period  = REFRESH_PERIOD_US,
        .func_p  = TimerNivelHandler,
        .param_p = NULL
    };
    TimerInit(&timer_nivel);

    /* Configurar timer para control de nivel (cada 0,5 s) */
    timer_config_t timer_control = {
        .timer   = TIMER_C,
        .period  = CONTROL_PERIOD_US,
        .func_p  = TimerControlHandler,
        .param_p = NULL
    };
    TimerInit(&timer_control);


   /* Crear tareas */
    xTaskCreate(&MedirNivelTask, "NivelTask", 512, NULL, 5, &medir_task_handle);
    xTaskCreate(&ControlNivelTask, "ControlTask", 512, NULL, 5, &control_task_handle);
    xTaskCreate(&ControlManualTask, "ControlManualTask", 512, NULL, 5, &control_manual_task_handle);

   
    /* Iniciar timer */
    TimerStart(timer_nivel.timer);
    TimerStart(timer_control.timer);
}

/*==================[end of file]============================================*/