# Programowanie Bare-Metal i Rejestry Sprzętowe z FreeRTOS na ESP32

## 📋 Spis treści
- [Wymagania sprzętowe](#wymagania-sprzętowe)
- [Implementacja](#implementacja)
- [Teoria i koncepcje](#teoria-i-koncepcje)
- [Rejestry GPIO](#rejestry-gpio)
- [Volatile keyword](#volatile-keyword)
- [Porównanie metod](#porównanie-metod)

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
```

---

## Teoria i koncepcje

### Programowanie Bare-Metal

**Bare-Metal** (dosł. "goły metal") to programowanie bezpośrednio na sprzęcie, bez lub z minimalną warstwą abstrakcji systemowej.

#### Charakterystyka
- Bezpośredni dostęp do rejestrów sprzętowych
- Pełna kontrola nad działaniem mikrokontrolera
- Maksymalna wydajność i minimalne opóźnienia
- Wymaga głębokiej znajomości architektury sprzętu

#### Zastosowania
- Systemy o krytycznym czasie reakcji
- Optymalizacja wydajności
- Zaawansowana diagnostyka i debugowanie
- Niestandardowe funkcje sprzętowe

---

## Rejestry GPIO

### Typy rejestrów w ESP32

| Rejestr | Adres (przykładowy) | Funkcja | Zastosowanie |
| :--- | :--- | :--- | :--- |
| **GPIO_OUT_W1TS_REG** | 0x3FF44000 | Write 1 to Set | Ustawienie pinów na HIGH (logiczna 1) |
| **GPIO_OUT_W1TC_REG** | 0x3FF44004 | Write 1 to Clear | Wyczyszczenie pinów na LOW (logiczna 0) |
| **GPIO_ENABLE_W1TS_REG** | 0x3FF44020 | Enable Write 1 to Set | Włączenie pinu jako OUTPUT |
| **GPIO_ENABLE_W1TC_REG** | 0x3FF44024 | Enable Write 1 to Clear | Wyłączenie pinu jako OUTPUT |

### Operacje na rejestrach

#### Makra dostępu

```c
#define REG_WRITE(reg, val)  (*(volatile uint32_t *)(reg)) = (uint32_t)(val)
#define REG_READ(reg)        (*(volatile uint32_t *)(reg))
```

**Wyjaśnienie:**
1. `(reg)` - adres rejestru (liczba całkowita)
2. `(volatile uint32_t *)` - rzutowanie na wskaźnik do 32-bitowej liczby całkowitej
3. `volatile` - informuje kompilator o możliwości zmian sprzętowych
4. `*` - dereferencja wskaźnika (dostęp do wartości)

#### Manipulacja bitami

```c
#define PIN_BIT(num) (1 << (num))
```

- Tworzy maskę bitową dla danego pinu
- `PIN_BIT(2)` = `0b00000100` = `0x04`
- Umożliwia ustawienie/wyczyszczenie konkretnego bitu

### Metoda Write-1-to-Set/Clear

#### Zalety
- **Atomowość:** Operacja wykonywana w jednym cyklu procesora
- **Bezpieczeństwo:** Nie wymaga odczytu-modyfikacji-zapisu (Read-Modify-Write)
- **Brak race conditions:** Inne piny nie są narażone na przypadkowe zmiany
- **Szybkość:** Bezpośrednie ustawienie/wyczyszczenie bez dodatkowej logiki

#### Przykład użycia

```c
// Włączenie LED (GPIO 2)
REG_WRITE(GPIO_OUT_W1TS_REG, PIN_BIT(2));  // Ustawia bit 2 na 1

// Wyłączenie LED (GPIO 2)
REG_WRITE(GPIO_OUT_W1TC_REG, PIN_BIT(2));  // Czyści bit 2 na 0
```

---

## Volatile keyword

### Czym jest volatile?

`volatile` to kwalifikator typu w C/C++, który informuje kompilator, że wartość zmiennej może ulec zmianie **w sposób nieprzewidywalny dla kompilatora**.

### Kiedy używać volatile?

1. **Zmienne modyfikowane przez ISR (przerwania)**
   ```c
   volatile uint32_t isr_flag = 0;
   ```

2. **Rejestry sprzętowe mapowane w pamięci**
   ```c
   #define REG_WRITE(reg, val)  (*(volatile uint32_t *)(reg)) = (uint32_t)(val)
   ```

3. **Zmienne współdzielone między wątkami** (choć w RTOS preferowane są mechanizmy synchronizacji)

### Co robi volatile?

#### Bez volatile (niebezpieczne!)

```c
uint32_t flag = 0;

// Kompilator może zoptymalizować:
if (flag == 0) {
    // Kod...
}
if (flag == 0) {  // Kompilator: "flag nie zmieniło się, pomiń sprawdzenie"
    // Ten kod może być pominięty!
}
```

#### Z volatile (bezpieczne)

```c
volatile uint32_t flag = 0;

// Kompilator zawsze odczyta wartość z pamięci:
if (flag == 0) {
    // Kod...
}
if (flag == 0) {  // Kompilator: "Muszę sprawdzić ponownie, bo może się zmienić"
    // Kod zawsze zostanie sprawdzony
}
```

### Przykład w ISR

```c
volatile uint32_t isr_flag = 0;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    isr_flag = 1;  // Zmiana w przerwaniu
}

void task(void) {
    while (1) {
        if (isr_flag == 1) {  // Zawsze odczyta aktualną wartość
            isr_flag = 0;
            // Obsługa zdarzenia
        }
    }
}
```

### Ograniczenia volatile

⚠️ **Volatile NIE zapewnia:**
- **Atomowości** operacji wielobajtowych
- **Synchronizacji** między wątkami
- **Porządku operacji** (memory ordering)

✅ **W RTOS preferuj:**
- Kolejki (`xQueue`)
- Semafory (`xSemaphore`)
- Mutexy (`xMutex`)
- Event Groups (`xEventGroup`)

---

## Porównanie metod

### 1. Biblioteki HAL vs. Rejestry Bare-Metal

| Aspekt | Biblioteki HAL (np. ESP-IDF) | Rejestry Bare-Metal |
| :--- | :--- | :--- |
| **Łatwość użycia** | ⭐⭐⭐⭐⭐ Wysoka | ⭐⭐ Niska |
| **Przenośność** | ⭐⭐⭐⭐⭐ Kod działa na różnych MCU | ⭐ Specyficzne dla danego MCU |
| **Wydajność** | ⭐⭐⭐⭐ Bardzo dobra | ⭐⭐⭐⭐⭐ Maksymalna |
| **Czytelność** | ⭐⭐⭐⭐⭐ Czytelny API | ⭐⭐ Wymaga komentarzy |
| **Kontrola** | ⭐⭐⭐ Ograniczona abstrakcją | ⭐⭐⭐⭐⭐ Pełna kontrola |
| **Debugowanie** | ⭐⭐⭐⭐ Łatwe | ⭐⭐ Trudne |

### 2. Volatile Flag vs. Queue

| Aspekt | Volatile Flag | FreeRTOS Queue |
| :--- | :--- | :--- |
| **Złożoność** | ⭐⭐ Prosta | ⭐⭐⭐⭐ Wyższa |
| **Bezpieczeństwo** | ⭐⭐ Wymaga ostrożności | ⭐⭐⭐⭐⭐ Thread-safe |
| **Dane** | Tylko flaga (tak/nie) | Dowolne struktury danych |
| **Synchronizacja** | ⭐ Brak | ⭐⭐⭐⭐⭐ Wbudowana |
| **Buforowanie** | ❌ Jedno zdarzenie | ✅ Kolejka zdarzeń |
| **Zalecenie RTOS** | Tylko w prostych przypadkach | ✅ Preferowana metoda |

### 3. Przykład porównania kodu

#### Metoda HAL (prosta)
```c
gpio_set_level(LED_GPIO, 1);  // Włącz LED
```

#### Metoda Bare-Metal (kontrola)
```c
REG_WRITE(GPIO_OUT_W1TS_REG, PIN_BIT(LED_GPIO));  // Włącz LED
```

---

## Cykl działania

### Sekwencja obsługi w przykładzie Bare-Metal

1. **Inicjalizacja sprzętu:**
   - Konfiguracja rejestrów ENABLE dla GPIO 2 (LED)
   - Ustawienie początkowego stanu LED przez rejestr CLEAR
   - Konfiguracja GPIO 5 (przycisk) przez bibliotekę

2. **Utworzenie mechanizmów RTOS:**
   - Utworzenie kolejki (`gpio_evt_queue`)
   - Uruchomienie zadania (`button_event_task`)

3. **Wciśnięcie przycisku:**
   - Przerwanie na GPIO 5 (zbocze opadające)
   - Wywołanie `gpio_isr_handler`

4. **W ISR:**
   - Ustawienie volatile flagi: `isr_flag = 1`
   - Wysłanie numeru GPIO do kolejki: `xQueueSendFromISR`

5. **W Task:**
   - Odbiór z kolejki (lub timeout 500ms)
   - Sprawdzenie volatile flagi
   - **Zmiana stanu LED przez rejestry:**
     - Jeśli OFF → Zapis do `GPIO_OUT_W1TS_REG` (włączenie)
     - Jeśli ON → Zapis do `GPIO_OUT_W1TC_REG` (wyłączenie)

6. **Kontynuacja:**
   - Task powraca do oczekiwania na kolejne zdarzenie
   - Cykl się powtarza

---

## 🔑 Kluczowe wnioski

### Programowanie Bare-Metal
- ✅ Maksymalna kontrola i wydajność
- ✅ Możliwość implementacji niestandardowych funkcji
- ⚠️ Wymaga głębokiej znajomości sprzętu
- ⚠️ Kod specyficzny dla danego MCU

### Volatile keyword
- ✅ Niezbędne dla zmiennych modyfikowanych przez ISR
- ✅ Wymagane dla dostępu do rejestrów sprzętowych
- ⚠️ Nie zastępuje mechanizmów synchronizacji RTOS
- ⚠️ Nie gwarantuje atomowości

### Rejestry W1TS/W1TC
- ✅ Atomowe operacje na pojedynczych bitach
- ✅ Bezpieczne w środowisku wielowątkowym
- ✅ Szybsze od Read-Modify-Write
- ✅ Eliminują race conditions

### Najlepsze praktyki
- **W aplikacjach:** Używaj bibliotek HAL (ESP-IDF)
- **W optymalizacjach:** Rozważ dostęp do rejestrów
- **W RTOS:** Preferuj kolejki zamiast volatile flag
- **W ISR:** Minimalna logika + przekazanie do Task

