#include "radio_test.h"
#include "sx126x_hal_stm32.h"
#include "sx126x_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef test_gpio_a={.MODER=1U<<8},test_gpio_b,test_gpio_c={.MODER=1};
UART_HandleTypeDef huart2;
sx126x_stm32_ctx_t sx126x_ctx;
static uint32_t tick;
static uint8_t registers[4096],chip_mode=2,cmd_status=5,packet[220];
static uint8_t packet_size;
static uint16_t irq,errors=0x20;
static bool tcxo,tx_timeout,fail_irq;
static unsigned rx_starts,tx_starts,image_calibrations,lora_configurations;
uint32_t HAL_GetTick(void){return tick++;}
void HAL_Delay(uint32_t ms){tick+=ms;}
int xTaskGetSchedulerState(void){return 0;}
void vTaskDelay(uint32_t ms){tick+=ms;}
uint32_t HAL_SPI_GetError(SPI_HandleTypeDef *spi){(void)spi;return 0;}
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState value){
    if(value){p->IDR|=pin;p->ODR|=pin;}else{p->IDR&=~pin;p->ODR&=~pin;}
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p,uint16_t pin){return p->IDR&pin?GPIO_PIN_SET:GPIO_PIN_RESET;}
int HAL_UART_Transmit(UART_HandleTypeDef *u,uint8_t *s,uint16_t n,uint32_t timeout){
    (void)u;(void)s;(void)n;(void)timeout;return 0;
}
void sx126x_hal_stm32_attach(SPI_HandleTypeDef *spi){sx126x_ctx.hspi=spi;}
sx126x_hal_status_t sx126x_hal_reset(const void *context){
    (void)context;chip_mode=2;cmd_status=5;errors=0x20;tcxo=false;irq=0;registers[0x740]=0x14;return SX126X_HAL_STATUS_OK;
}
sx126x_hal_status_t sx126x_hal_wakeup(const void *context){(void)context;return SX126X_HAL_STATUS_OK;}
sx126x_hal_status_t sx126x_hal_write(const void *context,const uint8_t *c,uint16_t cn,const uint8_t *data,uint16_t n){
    (void)context;(void)cn;
    switch(c[0]){
    case 0x0D:memcpy(registers+((c[1]<<8)|c[2]),data,n);break;
    case 0x80:chip_mode=c[1]?3:2;cmd_status=1;if(c[1]&&!tcxo){errors|=0x20;chip_mode=2;cmd_status=5;}break;
    case 0x07:errors=0;break;
    case 0x97:assert(c[1]==1&&c[2]==0&&c[3]==5&&c[4]==0);tcxo=true;break;
    case 0x93:assert(c[1]==0x30);break;
    case 0x98:assert(tcxo&&chip_mode==2&&c[1]==0xD7&&c[2]==0xDB);image_calibrations++;break;
    case 0x8A:assert(chip_mode==2);lora_configurations++;break;
    case 0x82:assert(chip_mode==3);rx_starts++;chip_mode=5;cmd_status=1;irq=0;break;
    case 0x83:assert(chip_mode==3);tx_starts++;cmd_status=6;irq=tx_timeout?0x200:1;break;
    case 0x02:irq=0;break;
    default:break;
    }
    return SX126X_HAL_STATUS_OK;
}
sx126x_hal_status_t sx126x_hal_read(const void *context,const uint8_t *c,uint16_t cn,uint8_t *data,uint16_t n){
    (void)context;(void)cn;memset(data,0,n);
    switch(c[0]){
    case 0xC0:data[0]=(chip_mode<<4)|(cmd_status<<1);break;
    case 0x1D:memcpy(data,registers+((c[1]<<8)|c[2]),n);break;
    case 0x17:data[0]=errors>>8;data[1]=errors;break;
    case 0x12:if(fail_irq)return SX126X_HAL_STATUS_ERROR;data[0]=irq>>8;data[1]=irq;break;
    case 0x13:data[0]=packet_size;data[1]=0;break;
    case 0x1E:memcpy(data,packet,n);break;
    default:break;
    }
    return SX126X_HAL_STATUS_OK;
}
int main(void){
    SPI_HandleTypeDef spi={.Init={SPI_DATASIZE_8BIT}};
    uint8_t received[221];
    assert(radio_diagnostic_run(&spi));assert(tcxo&&chip_mode==3&&errors==0);
    assert(radio_rx_start()&&radio_rx_health()&&chip_mode==5);
    assert(radio_rx_poll(received,sizeof(received))==0);
    memcpy(packet,"CMD,41,PING",11);packet_size=11;irq=2;
    assert(radio_rx_poll(received,sizeof(received))==11&&!strcmp((char *)received,"CMD,41,PING"));
    irq=2|0x40;unsigned before=rx_starts;
    assert(radio_rx_poll(received,sizeof(received))==0&&rx_starts==before+1&&chip_mode==5);
    irq=2|0x20;assert(radio_rx_poll(received,sizeof(received))==0&&chip_mode==5);
    irq=2;packet_size=221;assert(radio_rx_poll(received,sizeof(received))==0);
    fail_irq=true;assert(radio_rx_poll(received,sizeof(received))==-1);fail_irq=false;
    assert(radio_send_payload((const uint8_t *)"ACK,41,OK,PONG",14)&&tx_starts==1);
    assert(radio_rx_start()&&chip_mode==5);assert(radio_rx_poll(received,sizeof(received))==0); /* TX IRQ cleared. */
    tx_timeout=true;assert(!radio_send_payload((const uint8_t *)"ACK,41,OK,PONG",14));
    assert(radio_rx_start()&&radio_rx_health());
    assert(image_calibrations==1&&lora_configurations==1);
    chip_mode=2;assert(!radio_rx_health());
    puts("STM32 RX/TX/CRC/header/timeout/rearm checks passed (SX1262 command model only)");
}
