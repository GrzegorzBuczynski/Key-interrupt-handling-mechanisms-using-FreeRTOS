#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Definicje pinów
#define BUTTON_GPIO   GPIO_NUM_5
#define LED_GPIO      GPIO_NUM_2

// Uchwyt do kolejki, używanej do przekazywania danych z ISR do Tasku
static xQueueHandle gpio_evt_queue = NULL;

// Tag do logów
static const char *TAG = "INTERRUPT_EXAMPLE";

// =========================================================
// 1. Procedura Obsługi Przerwania (ISR)
// =========================================================

/**
 * @brief Funkcja obsługi przerwania GPIO (Interrupt Service Routine - ISR).
 * * Ta funkcja jest wywoływana natychmiast po wystąpieniu zdarzenia na pinie GPIO_5.
 * Wymaga atrybutu IRAM_ATTR, aby zapewnić szybkie wykonanie z wewnętrznej pamięci RAM.
 */
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    // Odczytanie numeru pinu, który wywołał przerwanie (argument przekazany przy podłączeniu)
    uint32_t gpio_num = (uint32_t) arg;

    // Przekazanie numeru pinu do kolejki. 
    // Używamy wersji FromISR, ponieważ jesteśmy w kontekście przerwania.
    // Ostatni argument (NULL) jest używany do sprawdzania, czy obudzono zadanie o wyższym priorytecie.
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL); 
}

// =========================================================
// 2. Zadanie FreeRTOS (Task) do Obsługi Logiki
// =========================================================

/**
 * @brief Zadanie FreeRTOS, które czeka na zdarzenia z kolejki i wykonuje logikę.
 * * To zadanie może wykonywać operacje trwające długo (np. logowanie, komunikacja sieciowa).
 */
static void button_event_task(void* arg)
{
    uint32_t io_num;
    int led_state = 0;

    for(;;) {
        // Czekaj na dane z kolejki. portMAX_DELAY oznacza, że zadanie będzie spać 
        // (nie zużywa CPU) dopóki nie otrzyma danych.
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            
            ESP_LOGI(TAG, "Zdarzenie! Przycisk na GPIO %lu wciśnięty.", io_num);
            
            // Logika aplikacji: Odwrócenie stanu diody LED
            led_state = !led_state;
            gpio_set_level(LED_GPIO, led_state);
        }
    }
}

// =========================================================
// 3. Funkcja Główna Programu
// =========================================================

void app_main(void)
{
    // 1. UTWORZENIE KOLEJKI: Pojemność 10 elementów, każdy element o rozmiarze uint32_t (4 bajty)
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // 2. UTWORZENIE TASKU: Uruchomienie zadania, które będzie przetwarzać zdarzenia z kolejki
    xTaskCreate(button_event_task, "button_task", 2048, NULL, 10, NULL);

    // 3. KONFIGURACJA PINU WEJŚCIOWEGO (PRZYCISK)
    gpio_config_t io_conf_in = {};
    io_conf_in.intr_type = GPIO_INTR_NEGEDGE;       // Przerwanie na zboczu opadającym (włączenie)
    io_conf_in.mode = GPIO_MODE_INPUT;              // Pin jako wejście
    io_conf_in.pin_bit_mask = (1ULL << BUTTON_GPIO); // Maska pinu
    io_conf_in.pull_up_en = 1;                      // Włącz rezystor Pull-Up (stan normalny: Wysoki)
    gpio_config(&io_conf_in);

    // 4. KONFIGURACJA PINU WYJŚCIOWEGO (LED)
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    // 5. INSTALACJA I PODŁĄCZENIE PRZERWANIA
    // Instalacja ogólnego serwisu przerwań GPIO
    gpio_install_isr_service(0); 

    // Podłączenie konkretnej funkcji ISR do konkretnego pinu
    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, (void*) BUTTON_GPIO);

    ESP_LOGI(TAG, "System gotowy. Naciskaj przycisk na GPIO %d.", BUTTON_GPIO);

    // app_main teraz po prostu czeka, podczas gdy FreeRTOS zarządza Taskiem i ISR.
}