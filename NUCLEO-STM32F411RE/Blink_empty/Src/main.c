//#include <stdint.h>
//
//// Базовые адреса периферии
//#define RCC_BASE      0x40023800U
//#define GPIOA_BASE    0x40020000U
//#define GPIOC_BASE    0x40020800U
//
//// Регистр тактирования (включаем питание портов А и С)
//#define RCC_AHB1ENR   (*(volatile uint32_t *)(RCC_BASE + 0x30))
//
//// Регистры управления светодиодом (PA5)
//#define GPIOA_MODER   (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
//#define GPIOA_ODR     (*(volatile uint32_t *)(GPIOA_BASE + 0x14))
//
//// Регистры управления кнопкой (PC13)
//#define GPIOC_MODER   (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
//#define GPIOC_IDR     (*(volatile uint32_t *)(GPIOC_BASE + 0x10)) // Регистр ВХОДНЫХ данных
//
//int main(void) {
//    // 1. Включаем тактирование порта GPIOA (0-й бит) и порта GPIOC (2-й бит)
//    RCC_AHB1ENR |= (1U << 0) | (1U << 2);
//
//    // 2. Настраиваем светодиод PA5 на Выход (GPIO Output)
//    GPIOA_MODER &= ~(3U << (5 * 2));
//    GPIOA_MODER |= (1U << (5 * 2));
//
//    // 3. Настраиваем кнопку PC13 на Вход (GPIO Input)
//    // По умолчанию после сброса все ножки стоят в режиме ввода, но для надежности очищаем биты (00)
//    GPIOC_MODER &= ~(3U << (13 * 2));
//
//    while(1) {
//        // 4. Опрашиваем состояние кнопки через регистр входных данных IDR
//        // Проверяем 13-й бит. Если кнопка НАЖАТА, этот бит станет равен 0
//        if ((GPIOC_IDR & (1U << 13)) == 0) {
//
//            // Кнопка нажата -> включаем светодиод (устанавливаем 5-й бит в 1)
//            GPIOA_ODR |= (1U << 5);
//
//        } else {
//
//            // Кнопка отпущена -> выключаем светодиод (сбрасываем 5-й бит в 0)
//            GPIOA_ODR &= ~(1U << 5);
//
//        }
//    }
//}


#include <stdint.h>

// Базовые адреса периферии
#define RCC_BASE      0x40023800U
#define GPIOA_BASE    0x40020000U
#define GPIOC_BASE    0x40020800U

// Регистр тактирования (включаем питание портов А и С)
#define RCC_AHB1ENR   (*(volatile uint32_t *)(RCC_BASE + 0x30))

// Регистры управления светодиодом (PA5)
#define GPIOA_MODER   (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_ODR     (*(volatile uint32_t *)(GPIOA_BASE + 0x14))

// Регистры управления кнопкой (PC13)
#define GPIOC_MODER   (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_IDR     (*(volatile uint32_t *)(GPIOC_BASE + 0x10))

// Функция задержки для борьбы с дребезгом контактов
void delay_ms(volatile uint32_t count) {
    while(count--) {
        __asm("nop");
    }
}

int main(void) {
    // Переменная для хранения флага: нажали ли мы кнопку в прошлом цикле
    uint8_t button_was_pressed = 0;

    // 1. Включаем тактирование порта GPIOA (0-й бит) и порта GPIOC (2-й бит)
    RCC_AHB1ENR |= (1U << 0) | (1U << 2);

    // 2. Настраиваем светодиод PA5 на Выход (GPIO Output)
    GPIOA_MODER &= ~(3U << (5 * 2));
    GPIOA_MODER |= (1U << (5 * 2));

    // 3. Настраиваем кнопку PC13 на Вход (GPIO Input)
    GPIOC_MODER &= ~(3U << (13 * 2));

    while(1) {
        // Проверяем: кнопка нажата физически? (в IDR записан 0)
        if ((GPIOC_IDR & (1U << 13)) == 0) {

            // Если в прошлом цикле кнопка ЕЩЕ НЕ БЫЛА нажата, значит, это момент клика!
            if (!button_was_pressed) {

                // Инвертируем состояние светодиода (если горел — гаснет, если не горел — загорается)
                GPIOA_ODR ^= (1U << 5);

                // Запоминаем, что кнопка сейчас зажата, чтобы не переключать диод по кругу
                button_was_pressed = 1;

                // Небольшая задержка, чтобы пропустить дребезг контактов при нажатии
                delay_ms(40000);
            }

        } else {
            // Если физически кнопка отпущена (в IDR вернулась 1)
            if (button_was_pressed) {

                // Сбрасываем флаг, теперь мы готовы к следующему нажатию
                button_was_pressed = 0;

                // Небольшая задержка, чтобы пропустить дребезг контактов при отпускании
                delay_ms(40000);
            }
        }
    }
}
