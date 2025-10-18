#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdint.h> 

// =========================================================
// 1. DEKLARACJA I DEFINICJA REJESTRÓW (Bare Metal Simulation)
// =========================================================

// Adresy bazowe i offsety są uproszczone/edukacyjne. W realnym ESP-IDF są one 
// definiowane w plikach nagłówkowych producenta (np. soc/gpio_reg.h)

// Adres bazowy dla rejestrów GPIO Output (uproszczone dla GPIO 2 - LED)
#define GPIO_OUT_W1TS_REG    0x3FF44000 
#define GPIO_OUT_W1TC_REG    0x3FF44004 
#define GPIO_ENABLE_W1TS_REG 0x3FF44020
#define GPIO_ENABLE_W1TC_REG 0x3FF44024

// Używamy makr, które rzutują stały adres na wskaźnik do volatile uint32_t
// Słowo kluczowe volatile jest krytyczne!

/**
 * @brief Volatile jest niezbędne!
 * Informuje kompilator, że wartość pod tym adresem może zostać zmieniona 
 * przez sprzęt (np. przez kontroler DMA, peryferium, lub przez ISR)
 * w dowolnym momencie, poza kontrolą kompilatora.
 * Zapobiega to optymalizacji, która mogłaby pominąć odczyt/zapis do rejestru.
 */
#define REG_WRITE(reg, val)  (*(volatile uint32_t *)(reg)) = (uint32_t)(val)
#define REG_READ(reg)        (*(volatile uint32_t *)(reg))

// Uproszczone bity kontrolne dla pinów (np. dla GPIO 2 i 5)
#define PIN_BIT(num) (1 << (num))
#define BUTTON_GPIO  5
#define LED_GPIO     2

// =========================================================
// 2. Obsługa przerwania (ISR) i Kolejki (jak wcześniej)
// =========================================================

// Uchwyt do kolejki i tag do logów
static xQueueHandle gpio_evt_queue = NULL;
static const char *TAG = "BARE_METAL_EXAMPLE";

/**
 * @brief Zmienna volatile globalna używana do sygnalizowania zdarzenia w prostszych systemach 
 * (używana tutaj tylko dla demonstracji, zwykle używa się kolejek/semaforów w RTOS).
 */
volatile uint32_t isr_flag = 0;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    
    // Używamy volatile flagi do sygnalizacji (alternatywa dla kolejki w prostszych przypadkach)
    // Szybka zmiana volatile jest bezpieczna, ale operacje na niej muszą być atomiczne.
    isr_flag = 1; 

    // Wysłanie do kolejki (preferowany sposób w RTOS)
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL); 
}

// =========================================================
// 3. Zadanie FreeRTOS (Task)
// =========================================================

static void button_event_task(void* arg)
{
    uint32_t io_num;
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, pdMS_TO_TICKS(500))) { // Czekaj na zdarzenie z kolejki lub timeout 500ms
            
            ESP_LOGI(TAG, "TASK: Przycisk na GPIO %lu wciśnięty. Zmieniam stan LED (rejestry).", io_num);
            
            // Logika aplikacji: Odwrócenie stanu diody LED
            // Zwykle rejestr GPIOx_OUT odczytuje się z innego adresu, ale upraszczamy,
            // używając rejestrów SET i CLEAR do bezpiecznej zmiany.
            
            // Przykładowa (bezpieczna) zmiana stanu LED: Użycie rejestrów SET/CLEAR
            static int led_state = 0;
            if (led_state == 0) {
                // Ustaw (Set) bit dla LED_GPIO na 1 (Włączenie LED)
                REG_WRITE(GPIO_OUT_W1TS_REG, PIN_BIT(LED_GPIO)); 
                led_state = 1;
            } else {
                // Czyść (Clear) bit dla LED_GPIO na 0 (Wyłączenie LED)
                REG_WRITE(GPIO_OUT_W1TC_REG, PIN_BIT(LED_GPIO)); 
                led_state = 0;
            }
        }
        
        // Przykład użycia volatile flagi (alternatywne sprawdzenie)
        if (isr_flag == 1) {
            ESP_LOGW(TAG, "TASK: Zmieniona flaga volatile!");
            isr_flag = 0;
        }
    }
}

// =========================================================
// 4. Funkcja Główna Programu i Konfiguracja
// =========================================================

void app_main(void)
{
    // 1. UTWORZENIE KOLEJKI i TASKU (jak wcześniej)
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(button_event_task, "button_task", 2048, NULL, 10, NULL);
    
    // 2. KONFIGURACJA PINU WYJŚCIOWEGO (LED) - UŻYCIE REJESTRÓW
    
    // Ustawienie GPIO 2 jako OUTPUT: Manipulacja rejestrem ENABLE
    // Ustawienie bitu 2 w rejestrze ENABLE_W1TS (Write 1 to Set)
    REG_WRITE(GPIO_ENABLE_W1TS_REG, PIN_BIT(LED_GPIO)); 
    
    // Inicjalnie wyłącz LED: Użycie rejestru CLEAR
    REG_WRITE(GPIO_OUT_W1TC_REG, PIN_BIT(LED_GPIO)); 

    // 3. KONFIGURACJA PINU WEJŚCIOWEGO (PRZYCISK) - UŻYCIE STANDARDOWYCH BIBLIOTEK
    // Konfiguracja przerwań jest złożona i zintegrowana, dlatego dla uproszczenia
    // ten fragment często pozostawia się na poziomie bibliotek (ESP-IDF).
    gpio_config_t io_conf_in = {};
    io_conf_in.intr_type = GPIO_INTR_NEGEDGE;       
    io_conf_in.mode = GPIO_MODE_INPUT;              
    io_conf_in.pin_bit_mask = (1ULL << BUTTON_GPIO); 
    io_conf_in.pull_up_en = 1;                      
    gpio_config(&io_conf_in);

    // 4. INSTALACJA PRZERWANIA (jak wcześniej)
    gpio_install_isr_service(0); 
    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, (void*) BUTTON_GPIO);

    ESP_LOGI(TAG, "Bare Metal gotowy. Kontroluję LED na GPIO %d przez rejestry.", LED_GPIO);
}