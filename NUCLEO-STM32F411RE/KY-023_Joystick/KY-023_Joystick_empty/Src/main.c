#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Константы проекта
#define DEADZONE 20

// Глобальные переменные для хранения результатов
int32_t x_percent = 0;
int32_t y_percent = 0;
uint8_t btn_state = 0;
char tx_buf[64];

// Прототипы функций
void SetUp_Peripherals(void);
uint32_t Read_ADC_Channel(uint8_t channel);
void UART2_Send_String(char *str);

// Обязательная заглушка тактирования, чтобы линкер пустого проекта не выдавал ошибку
void SystemInit(void) {
}

int main(void) {
	// Отключаем внутреннюю буферизацию Си-библиотеки для предотвращения зависания консоли IDE
	setvbuf(stdout, NULL, _IONBF, 0);

	// 1. Инициализируем всю периферию через прямые физические адреса
	SetUp_Peripherals();

	// Переменные для организации программного ШИМ и неблокирующего таймера отправки UART
	uint32_t pwm_counter = 0;
	uint32_t uart_timer_counter = 0;
	uint32_t pwm_duty = 0; // Текущая яркость от 0 до 100

	while (1) {
		// 2. Поочередно опрашиваем аналоговые каналы джойстика через АЦП
		int32_t raw_x = (int32_t) Read_ADC_Channel(0) - 2048; // Ось X (Пин PA0, Канал 0)
		int32_t raw_y = (int32_t) Read_ADC_Channel(1) - 2048; // Ось Y (Пин PA1, Канал 1)
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
		// Проверяем 10-й бит регистра GPIOA_IDR (адрес 0x40020010). Если он 0 — кнопка прижата к GND
		if ((*((volatile uint32_t*) 0x40020010) & (1 << 10)) == 0) {
			btn_state = 1;
			*((volatile uint32_t*) 0x40020014) |= (1 << 5); // Включаем встроенный светодиод LD2 (PA5 = 1) в GPIOA_ODR
		} else {
			btn_state = 0;
			*((volatile uint32_t*) 0x40020014) &= ~(1 << 5); // Выключаем встроенный светодиод LD2 (PA5 = 0) in GPIOA_ODR
		}

		// 6. РЕАЛИЗАЦИЯ ПРОГРАММНОГО ШИМ ДЛЯ ПЛАВНОСТИ СВЕТОДИОДА НА PC7
		pwm_duty = abs(x_percent); // Целевая яркость совпадает с процентом отклонения оси X (от 0 до 100)

		// Инкрементируем счетчик ШИМ от 0 до 99
		pwm_counter++;
		if (pwm_counter >= 100) {
			pwm_counter = 0;
		}

		// Сравниваем счетчик с целевой яркостью
		if (pwm_counter < pwm_duty && pwm_duty > 5) {
			*((volatile uint32_t*) 0x40020814) |= (1 << 7); // Включаем PC7 (запись 1 в 7-й бит GPIOC_ODR)
		} else {
			*((volatile uint32_t*) 0x40020814) &= ~(1 << 7); // Выключаем PC7 (запись 0 в 7-й бит GPIOC_ODR)
		}

		// 7. НЕБЛОКИРУЮЩАЯ ОТПРАВКА В UART2 (Один раз в ~200 мс)
		// Счетчик увеличивается каждый шаг цикла. На частоте HSI 16 МГц значение 1200
		// примерно соответствует интервалу в 200 мс, что защищает буфер Eclipse от переполнения
		uart_timer_counter++;
		if (uart_timer_counter >= 1200) {
			uart_timer_counter = 0;

			// Формирование строки и отправка отчета на ПК через UART2
			sprintf(tx_buf, "X: %4ld%% | Y: %4ld%% | Button: %d\r\n", x_percent,
					y_percent, btn_state);
			UART2_Send_String(tx_buf);
		}
	}
}

// Пошаговая инициализация оборудования через прямые указатели на память
void SetUp_Peripherals(void) {
	// 1. --- ТАКТИРОВАНИЕ ПЕРИФЕРИИ (Регистры RCC) ---
	// Включаем тактирование GPIOA (бит 0) и GPIOC (бит 2) в RCC_AHB1ENR (адрес 0x40023830)
	*((volatile uint32_t*) 0x40023830) |= (1 << 0) | (1 << 2);
	// Включаем тактирование USART2 (бит 17) в RCC_APB1ENR (адрес 0x40023840)
	*((volatile uint32_t*) 0x40023840) |= (1 << 17);
	// Включаем тактирование ADC1 (бит 8) в RCC_APB2ENR (адрес 0x40023844)
	*((volatile uint32_t*) 0x40023844) |= (1 << 8);

	// 2. --- НАСТРОЙКА GPIO (Порт A, базовый адрес 0x40020000) ---
	// PA0 и PA1: Аналоговый режим для АЦП (запись '11' в биты MODER0 и MODER1)
	*((volatile uint32_t*) 0x40020000) |= (3 << 0) | (3 << 2);
	
	// PA5: Выход для встроенного светодиода LD2 (запись '01' в биты MODER5)
	*((volatile uint32_t*) 0x40020000) &= ~(3 << 10);
	*((volatile uint32_t*) 0x40020000) |= (1 << 10);

	// PA2 и PA3: Альтернативная функция для UART2 (запись '10' в биты MODER2 и MODER3)
	*((volatile uint32_t*) 0x40020000) &= ~((3 << 4) | (3 << 6));
	*((volatile uint32_t*) 0x40020000) |= (2 << 4) | (2 << 6);
	// Задаем AF7 (0111) для PA2 и PA3 в регистре альтернативных функций GPIOA_AFRL (адрес 0x40020020)
	*((volatile uint32_t*) 0x40020020) &= ~((0xF << 8) | (0xF << 12));
	*((volatile uint32_t*) 0x40020020) |= (7 << 8) | (7 << 12);

	// PA10: Вход для кнопки джойстика с подтяжкой к Питанию (Pull-up)
	*((volatile uint32_t*) 0x40020000) &= ~(3 << 20); // Режим входа '00' в MODER
	*((volatile uint32_t*) 0x4002000c) &= ~(3 << 20);
	*((volatile uint32_t*) 0x4002000c) |= (1 << 20); // Подтяжка вверх '01' в PUPDR (адрес 0x4002000c)

	// 3. --- НАСТРОЙКА GPIO (Порт C, базовый адрес 0x40020800) ---
	// PC7: Режим цифрового выхода GPIO_Output (запись '01' в биты 15:14 регистра GPIOC_MODER)
	*((volatile uint32_t*) 0x40020800) &= ~(3 << 14);
	*((volatile uint32_t*) 0x40020800) |= (1 << 14);

	// 4. --- НАСТРОЙКА АЦП (ADC1) ---
	// Включаем модуль АЦП (бит 0 = ADON в регистре ADC1_CR2, адрес 0x40012008)
	*((volatile uint32_t*) 0x40012008) |= (1 << 0);
	// Настройка общего предделителя АЦП: частота HSI/4 = 4 МГц (биты 17:16 = 01 в ADC_CCR, адрес 0x40012304)
	*((volatile uint32_t*) 0x40012304) &= ~(3 << 16);
	*((volatile uint32_t*) 0x40012304) |= (1 << 16);

	// 5. --- НАСТРОЙКА UART2 (базовый адрес 0x40004400) ---
	// Скорость 115200 Бод при частоте шины 16 МГц. Запись константы в USART2_BRR (адрес 0x40004408)
	*((volatile uint32_t*) 0x40004408) = 0x008B;
	// Включаем передатчик TX (бит 3) и приемник RX (бит 2) в USART2_CR1 (адрес 0x4000440c)
	*((volatile uint32_t*) 0x4000440c) |= (1 << 3) | (1 << 2);
	// Активируем модуль USART2 (бит 13 = UE в USART2_CR1)
	*((volatile uint32_t*) 0x4000440c) |= (1 << 13);
}

// Опрос конкретного канала АЦП через голые указатели на память
uint32_t Read_ADC_Channel(uint8_t channel) {
	// Задаем номер канала в регистр последовательности ADC1_SQR3 (адрес 0x40012034)
	*((volatile uint32_t*) 0x40012034) = channel;
	
	// Запускаем регулярное преобразование (бит 30 = SWSTART в регистре ADC1_CR2, адрес 0x40012008)
	*((volatile uint32_t*) 0x40012008) |= (1 << 30);
	
	// Ожидаем завершения оцифровки (бит 1 = EOC в регистре статуса ADC1_SR, адрес 0x40012000) с таймаутом
	uint32_t timeout = 20000;
	while (!(*((volatile uint32_t*) 0x40012000) & (1 << 1)) && timeout) {
		timeout--;
	}
	
	// Возвращаем итоговый 12-битный результат из регистра данных ADC1_DR (адрес 0x4001204c)
	return *((volatile uint32_t*) 0x4001204c);
}

// Посимвольная отправка строки через UART2 с защитой консоли от зависания
void UART2_Send_String(char *str) {
	while (*str) {
		uint32_t timeout = 40000; // Аппаратный защитный счетчик от зависания интерфейса

		// Ожидаем флаг опустошения регистра передачи (бит 7 = TXE в регистре статуса USART2_SR, адрес 0x40004400)
		while (!(*((volatile uint32_t*) 0x40004400) & (1 << 7))) {
			timeout--;
			if (timeout == 0) {
				volatile uint32_t dummy = *((volatile uint32_t*) 0x40004400);
				dummy = *((volatile uint32_t*) 0x40004404);
				(void) dummy;
				return; // Безопасно выходим из функции
			}
		}
		// Записываем текущий символ в регистр данных USART2_DR (адрес 0x40004404)
		*((volatile uint32_t*) 0x40004404) = (*str++ & 0xFF);
	}
}
