#include "stm32f4xx.h"
#include <stdio.h>
#include <stdlib.h>

// Константы проекта
#define DEADZONE 30	// Мертвая зона

// Глобальные переменные для хранения результатов
int32_t x_percent = 0;     // Итоговый процент X (-100...100)
int32_t y_percent = 0;     // Итоговый процент Y (-100...100)
uint8_t btn_state = 0;     // Кнопка
char tx_buf[100];          // Буфер для текста (задали четкий размер)
//char tx_buf[64];

// Прототипы функций
void SetUp_Peripherals(void);
uint32_t Read_ADC_Channel(uint8_t channel);
void UART2_Send_String(char *str);

int main(void) {
	// 1. Инициализируем всю периферию (Тактирование, GPIO, АЦП, Таймер, UART)
	SetUp_Peripherals();

	// ОТКЛЮЧАЕМ БУФЕРИЗАЦИЮ СТАНДАРТНОГО ВЫВОДА (Добавьте эту строчку!)
	setvbuf(stdout, NULL, _IONBF, 0);

	while (1) {
		// 2. Поочередно опрашиваем аналоговые каналы джойстика
		int32_t raw_x = (int32_t) Read_ADC_Channel(0) - 2048; // Ось X (Пин PA0)
		int32_t raw_y = (int32_t) Read_ADC_Channel(1) - 2048; // Ось Y (Пин PA1)
		int32_t max_range = 2048 - DEADZONE;

		// 3. Расчет процентов оси X с фильтрацией мертвой зоны
		if (raw_x > -DEADZONE && raw_x < DEADZONE)
			x_percent = 0;
		else if (raw_x >= DEADZONE)
			x_percent = ((raw_x - DEADZONE) * 100) / max_range;
		else
			x_percent = ((raw_x + DEADZONE) * 100) / max_range;

		// 4. Расчет процентов оси Y с фильтрацией мертвой зоны
		if (raw_y > -DEADZONE && raw_y < DEADZONE)
			y_percent = 0;
		else if (raw_y >= DEADZONE)
			y_percent = ((raw_y - DEADZONE) * 100) / max_range;
		else
			y_percent = ((raw_y + DEADZONE) * 100) / max_range;

		// Жесткое ограничение лимитов [-100, 100]
		if (x_percent > 100)
			x_percent = 100;
		if (x_percent < -100)
			x_percent = -100;
		if (y_percent > 100)
			y_percent = 100;
		if (y_percent < -100)
			y_percent = -100;

		// 5. Опрос кнопки джойстика (Пин PA10)
		// Если в регистре IDR 10-й бит равен 0 — кнопка зажата (замыкание на GND)
		if ((GPIOA->IDR & (1 << 10)) == 0) {
			btn_state = 1;
			GPIOA->ODR |= (1 << 5); // Включаем встроенный зеленый светодиод LD2 (PA5 = 1)
		} else {
			btn_state = 0;
			GPIOA->ODR &= ~(1 << 5); // Выключаем встроенный светодиод LD2 (PA5 = 0)
		}

		// 6. Управление ШИМ-яркостью красного светодиода (Пин PC7)
		// Переводим проценты оси X в шаги ШИМ от 0 до 999
		uint32_t pwm_x = (abs(x_percent) * 999) / 100;
		TIM3->CCR2 = pwm_x; // Записываем значение напрямую в регистр сравнения Канала 2

		// 7. Формирование строки и отправка отчета на ПК через UART2
		sprintf(tx_buf, "X: %4ld%% | Y: %4ld%% | Button: %d\r\n", x_percent,
				y_percent, btn_state);
		UART2_Send_String(tx_buf);

		// Программная задержка (примерно 100-150 мс в зависимости от частоты процессора)
		for (volatile uint32_t i = 0; i < 300000; i++)
			;
	}
}


// === Функция инициализации всей периферии на уровне регистров ===
void SetUp_Peripherals(void) {
	// 1. Подаем питание на порты GPIOA, GPIOC, а также на TIM3, ADC1 и USART2
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN | RCC_APB1ENR_USART2EN;
	RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

	// 2. Настройка GPIO
	// Пины PA0 (Ось X) и PA1 (Ось Y) переводятся в аналоговый режим (биты 11)
	GPIOA->MODER |= (3 << (0 * 2)) | (3 << (1 * 2));

	// Пин PA5 (Встроенный светодиод LD2) переводится в режим выхода (биты 01)
	GPIOA->MODER &= ~(3 << (5 * 2));
	GPIOA->MODER |= (1 << (5 * 2));

	// Пин PA10 (Кнопка) переводится в режим входа (биты 00) и включается Pull-up подтяжка (биты 01)
	GPIOA->MODER &= ~(3 << (10 * 2));
	GPIOA->PUPDR &= ~(3 << (10 * 2));
	GPIOA->PUPDR |= (1 << (10 * 2));

	// Пин PC7 (Красный светодиод) переводится в режим альтернативной функции (биты 10)
	GPIOC->MODER &= ~(3 << (7 * 2));
	GPIOC->MODER |= (2 << (7 * 2));
	// Назначаем для PC7 альтернативную функцию AF2 (Таймер TIM3) через регистр AFR[0] (он же AFRL)
	GPIOC->AFR[0] &= ~(0xF << (7 * 4));
	GPIOC->AFR[0] |= (2 << (7 * 4));

	// Пины PA2 (TX) и PA3 (RX) для связи по UART2. Переводим в альтернативную функцию (биты 10)
	GPIOA->MODER |= (2 << (2 * 2)) | (2 << (3 * 2));
	// Назначаем для них AF7 (Интерфейс USART) через регистр AFR[0]
	GPIOA->AFR[0] |= (7 << (2 * 4)) | (7 << (3 * 4));

	// 3. Настройка Таймера TIM3 (Генерация ШИМ)
	TIM3->PSC = 16 - 1; // Предделитель частоты (если шина тактируется на 16 МГц, таймер получит 1 МГц)
	TIM3->ARR = 999; // Период ШИМ (счетчик считает от 0 до 999, задавая частоту ШИМ в 1 кГц)
	// Настраиваем Канал 2 в режим PWM mode 1 (биты 110 в поле OC2M) и включаем буферизацию (бит OC2PE)
	TIM3->CCMR1 |= (6 << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
	TIM3->CCER |= TIM_CCER_CC2E; // Активируем выход Канала 2 таймера
	TIM3->CR1 |= TIM_CR1_CEN;   // Запускаем счетчик таймера

	// 4. Настройка АЦП (ADC1)
	ADC1->CR2 |= ADC_CR2_ADON;  // Включаем модуль АЦП

	// 5. Настройка USART2 (Связь с ПК)
	// Задаем скорость 115200 бод (при частоте тактирования шины в 16 МГц)
	USART2->BRR = 0x008B;
	USART2->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE; // Включаем передатчик, приемник и сам модуль UART
}


// === Функция последовательного ручного опроса каналов АЦП ===
uint32_t Read_ADC_Channel(uint8_t channel) {
	ADC1->SQR3 = channel; // Записываем номер опрашиваемого канала в регистр последовательности
	ADC1->CR2 |= ADC_CR2_SWSTART; // Программный запуск преобразования АЦП

	// Ожидаем окончания конверсии (ждем, пока флаг EOC в регистре SR станет равным 1)
	while (!(ADC1->SR & ADC_SR_EOC))
		;

	return ADC1->DR; // Возвращаем результат замера из регистра данных (0...4095)
}


// === Функция отправки строки по UART2 без использования библиотек HAL ===
void UART2_Send_String(char *str) {
	while (*str) {
		uint32_t timeout = 20000; // Защитный счетчик

		// Ждем, пока освободится регистр передачи (флаг TXE)
		// Если флаг не взводится слишком долго (зависла линия) — выходим по таймауту
		while (!(USART2->SR & USART_SR_TXE)) {
			timeout--;
			if (timeout == 0) {
				// Сбрасываем возможные ошибки регистра статуса, чтобы разблокировать UART
				volatile uint32_t dummy = USART2->SR;
				dummy = USART2->DR;
				(void) dummy;
				return; // Выходим из функции, не давая контроллеру зависнуть
			}
		}

		// Отправляем текущий символ и переходим к следующему
		USART2->DR = (*str++ & 0xFF);
	}
}

