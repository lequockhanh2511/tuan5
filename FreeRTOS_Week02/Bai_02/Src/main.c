#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>

UART_HandleTypeDef huart1;
QueueHandle_t q_btn_count = NULL;
uint32_t button_press_count = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);

// Task nhận giá trị đếm từ Queue và truyền UART lên máy tính
void vUartTask(void *pvParameters) {
    uint32_t count = 0;
    char msg[64];

    for (;;) {
        // Đưa Task về trạng thái Blocked để chờ dữ liệu từ Queue
        if (xQueueReceive(q_btn_count, &count, portMAX_DELAY) == pdTRUE) {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

            int len = snprintf(msg, sizeof(msg), "DTMT01_N02: Button Pressed = %lu\r\n", count);
            HAL_UART_Transmit(&huart1, (uint8_t*)msg, len, 200);

            // Nghỉ 200ms để đợi tiếp điểm nút nhấn ổn định hoàn toàn
            vTaskDelay(pdMS_TO_TICKS(200));

            // Xóa cờ ngắt tồn đọng do rung phím trong thanh ghi EXTI
            __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);
            NVIC_ClearPendingIRQ(EXTI0_IRQn);

            // Bật lại ngắt EXTI0 cho lần nhấn tiếp theo
            HAL_NVIC_EnableIRQ(EXTI0_IRQn);
        }
    }
}

// Hàm phục vụ ngắt ngoài EXTI (chân PB0)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_0) {
        // KHÓA NGẮT NGAY LẬP TỨC ở tầng NVIC để chặn toàn bộ xung rung tiếp điểm
        HAL_NVIC_DisableIRQ(EXTI0_IRQn);

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        button_press_count++;

        // Gửi giá trị số đếm vào Queue từ ISR
        xQueueSendFromISR(q_btn_count, &button_press_count, &xHigherPriorityTaskWoken);

        // Chuyển ngữ cảnh ngay lập tức sang Task nhận nếu có yêu cầu
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

int main(void) {
    HAL_Init();
    SystemClock_Config();

    // Cấu hình 4 bit Preemption bắt buộc cho FreeRTOS trên Cortex-M3
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

    MX_GPIO_Init();
    MX_USART1_UART_Init();

    // Tạo Queue chứa tối đa 10 phần tử uint32_t
    q_btn_count = xQueueCreate(10, sizeof(uint32_t));

    if (q_btn_count != NULL) {
        xTaskCreate(vUartTask, "UART_Task", 128, NULL, 2, NULL);
        vTaskStartScheduler();
    }

    while (1) {
    }
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    // LED PC13
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // Chân PB0 làm ngắt ngoài EXTI cạnh xuống kèm điện trở kéo lên nội
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // Cấu hình mức ưu tiên ngắt NVIC cho EXTI0
    HAL_NVIC_SetPriority(EXTI0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);

    // PA9 (USART1_TX)
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PA10 (USART1_RX)
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void MX_USART1_UART_Init(void) {
    __HAL_RCC_USART1_CLK_ENABLE();

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9; // 72MHz
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}