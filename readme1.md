# Obsługa Przerwań za pomocą FreeRTOS Queues na ESP32

## 📋 Spis treści
- [Wymagania sprzętowe](#wymagania-sprzętowe)
- [Implementacja](#implementacja)
- [Teoria i koncepcje](#teoria-i-koncepcje)
- [Cykl działania](#cykl-działania)

---

## Wymagania sprzętowe

### Konfiguracja pinów
- **Przycisk:** GPIO_5 (Input z Pull-Up)
- **Dioda LED:** GPIO_2 (Output)
- **Platforma:** ESP32 / ESP32-S3
- **Środowisko:** ESP-IDF

---

## Implementacja

### Kompletny kod przykładu

```c
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
```

---

## Teoria i koncepcje

### Cel mechanizmu ISR + Queue

W systemach czasu rzeczywistego (RTOS) priorytetem jest **determinizm** (przewidywalność) i **szybkość reakcji**.

#### Problem
Funkcje obsługi przerwań (**ISR**) muszą być wykonane w **mikrosekundach**. Jeśli ISR wykonuje skomplikowaną logikę (np. alokację pamięci, komunikację Wi-Fi, długą pętlę), blokuje to procesor, uniemożliwiając obsługę innych, potencjalnie krytycznych, przerwań.

#### Rozwiązanie
Rozdzielenie obowiązków:
- **ISR (Sprzęt):** Wykonuje minimalną pracę — odbiera sygnał i natychmiast **przekazuje wiadomość** o zdarzeniu.
- **Task (Oprogramowanie):** Czeka na wiadomość i wykonuje **całą logikę** (która może trwać milisekundy lub dłużej) w bezpiecznym kontekście zadania.

### Kluczowe komponenty FreeRTOS

| Komponent | Funkcja w Przykładzie | Kontekst Użycia |
| :--- | :--- | :--- |
| **ISR (`gpio_isr_handler`)** | Wychwytuje zdarzenie (wciśnięcie przycisku) i używa `xQueueSendFromISR` do przekazania numeru pinu do kolejki. | **Kontekst Przerwania** (bardzo restrykcyjny, szybki). |
| **Kolejka (`gpio_evt_queue`)** | Mechanizm komunikacji (bufor FIFO). Umożliwia **asynchroniczną komunikację** między ISR a Taskiem. | **Współdzielony** (ISR wysyła, Task odbiera). |
| **Task (`button_event_task`)** | Zadanie (wątek) RTOS. Używa `xQueueReceive` do czekania na dane. Gdy dane nadejdą, **Task budzi się** i wykonuje długą, bezpieczną logikę. | **Kontekst Tasku** (normalny, bezpieczny). |

### Funkcje bezpieczne dla przerwań

FreeRTOS ściśle rozróżnia funkcje:

1. **`xQueueSend()` / `xQueueReceive()`:** 
   - Standardowe funkcje
   - Mogą wywoływać przełączanie Tasków
   - Używane tylko **w kontekście Tasków**

2. **`xQueueSendFromISR()` / `xQueueReceiveFromISR()`:** 
   - Specjalne funkcje
   - **Zoptymalizowane do pracy w ISR**
   - Nie wywołują przełączania kontekstu w trakcie działania
   - Flagują potrzebę ewentualnego przełączenia **po powrocie z przerwania**

### Atrybut IRAM_ATTR

- Zapewnia umieszczenie funkcji ISR w wewnętrznej pamięci RAM
- Gwarantuje **szybki dostęp** i wykonanie
- Niezbędny dla stabilności i wydajności przerwań

---

## Cykl działania

### Sekwencja obsługi wciśnięcia przycisku

1. **Inicjalizacja:** 
   - Zadanie `button_event_task` startuje
   - Blokuje się na instrukcji `xQueueReceive(..., portMAX_DELAY)`
   - Procesor nie marnuje cykli na to zadanie

2. **Akcja:** 
   - Użytkownik naciska przycisk
   - Następuje zbocze opadające na GPIO_5

3. **Wstrzymanie:** 
   - Procesor wstrzymuje bieżącą pracę (Task)
   - Skacze do `gpio_isr_handler`

4. **Szybkie przekazanie:** 
   - ISR wywołuje `xQueueSendFromISR`
   - Umieszcza numer `5` w kolejce

5. **Obudzenie:** 
   - Jądro RTOS zauważa, że wysłanie danych odblokowało Task
   - `button_event_task` staje się gotowy do wykonania

6. **Powrót i przełączenie:** 
   - ISR kończy działanie
   - Jądro RTOS **przełącza kontekst** do Tasku `button_event_task`

7. **Wykonanie logiki:** 
   - Task budzi się
   - Funkcja `xQueueReceive` zwraca `true`
   - Wykonywana jest bezpieczna operacja `gpio_set_level(LED_GPIO, led_state)`
   - Stan diody LED ulega odwróceniu

---

## 🔑 Kluczowe wnioski

- **Minimalna logika w ISR:** Tylko przekazanie danych do kolejki
- **Asynchroniczna komunikacja:** Queue jako bufor między ISR a Task
- **Bezpieczeństwo kontekstu:** Używanie funkcji `FromISR` w przerwaniach
- **Efektywność energetyczna:** Task śpi do czasu otrzymania zdarzenia
- **Skalowalność:** Łatwe dodanie obsługi wielu przycisków/źródeł przerwań
