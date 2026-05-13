#include "stm32f10x.h"
#include <stdint.h>

extern volatile uint32_t msTicks;

#define IR_PIN            GPIO_Pin_0
#define IR_PORT           GPIOA

#define IR_CONFIRM_MS     200
#define LOOP_DELAY_MS     50
#define NO_SIGNAL_MSG_MS  1000
#define HEARTBEAT_MS      1000

typedef enum {
    SEARCHING = 0,
    CONFIRMING,
    REVERSING
} DockState;

DockState state = SEARCHING;

uint32_t detectStartMs = 0;
uint32_t lastNoSignalMsgMs = 0;
uint32_t lastHeartbeatMs = 0;

uint32_t millis(void) {
    return msTicks;
}

void delay_ms(uint32_t ms) {
    uint32_t start = millis();
    while ((millis() - start) < ms);
}

void USART1_SendChar(char c) {
    while (!(USART1->SR & USART_SR_TXE));
    USART1->DR = c;
}

void USART1_SendString(const char *s) {
    while (*s) {
        USART1_SendChar(*s++);
    }
}

void USART1_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9);
    GPIOA->CRH |= (GPIO_CRH_MODE9_1 | GPIO_CRH_MODE9_0);
    GPIOA->CRH |= GPIO_CRH_CNF9_1;

    GPIOA->CRH &= ~(GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_CNF10_0;

    USART1->BRR = 0x0271;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void GPIO_Input_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOA->CRL |= GPIO_CRL_CNF0_0;
}

int irDetected(void) {
    return ((IR_PORT->IDR & IR_PIN) == 0);   // LOW = detected
}

void spinSlowly(void) {
}

void stopBot(void) {
}

void goBackward(void) {
}

int main(void) {
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    GPIO_Input_Init();
    USART1_Init();

    USART1_SendString("RECEIVER READY\r\n");

    while (1) {
        uint32_t now = millis();
        int ir = irDetected();

        if ((now - lastHeartbeatMs) >= HEARTBEAT_MS) {
            USART1_SendString("STM32 ALIVE\r\n");
            lastHeartbeatMs = now;
        }

        switch (state) {
            case SEARCHING:
                spinSlowly();

                if (!ir) {
                    if ((now - lastNoSignalMsgMs) >= NO_SIGNAL_MSG_MS) {
                        USART1_SendString("NO SIGNAL DETECTED\r\n");
                        lastNoSignalMsgMs = now;
                    }
                } else {
                    USART1_SendString("IR FIRST DETECTED\r\n");
                    detectStartMs = now;
                    state = CONFIRMING;
                }
                break;

            case CONFIRMING:
                spinSlowly();

                if (!ir) {
                    USART1_SendString("IR LOST\r\n");
                    state = SEARCHING;
                } else if ((now - detectStartMs) >= IR_CONFIRM_MS) {
                    USART1_SendString("IR CONFIRMED\r\n");
                    stopBot();
                    delay_ms(100);
                    USART1_SendString("REVERSING\r\n");
                    state = REVERSING;
                }
                break;

            case REVERSING:
                goBackward();
                break;

            default:
                state = SEARCHING;
                break;
        }

        delay_ms(LOOP_DELAY_MS);
    }
}
