#include "stm32f4xx.h" // Подключаем официальный заголовочный файл вашей серии

void delay_ms(volatile uint32_t count) {
	while (count--)
		__asm("nop");
}

// Инициализация портов через структуры CMSIS
void init_peripherals(void) {
	// Включаем тактирование GPIOA и GPIOC через регистр AHB1ENR периферии RCC
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;

	// Настройка PA5 на Выход (GPIO Output)
	GPIOA->MODER &= ~GPIO_MODER_MODER5;      // Очищаем биты настройки PA5
	GPIOA->MODER |= GPIO_MODER_MODER5_0;    // Устанавливаем режим "Выход" (01)

	// Настройка PC13 на Вход (GPIO Input)
	GPIOC->MODER &= ~GPIO_MODER_MODER13;    // Режим "Вход" задается нулями (00)
}

// Проверка нажатия кнопки с защитой от дребезга
uint8_t is_button_pressed(void) {
	// Проверяем 13-й бит в регистре входных данных (IDR)
	if ((GPIOC->IDR & GPIO_IDR_IDR_13) == 0) {
		delay_ms(40000);
		return (GPIOC->IDR & GPIO_IDR_IDR_13) == 0;
	}
	return 0;
}

// Переключение состояния светодиода
void toggle_led(void) {
	// Инвертируем 5-й бит в регистре выходных данных (ODR)
	GPIOA->ODR ^= GPIO_ODR_ODR_5;
}

int main(void) {
	uint8_t btn_was_pressed = 0;

	init_peripherals();

	while (1) {
		uint8_t btn_now = is_button_pressed();

		// Ловим момент клика
		if (btn_now && !btn_was_pressed) {
			toggle_led();
		}

		btn_was_pressed = btn_now;
	}
}
