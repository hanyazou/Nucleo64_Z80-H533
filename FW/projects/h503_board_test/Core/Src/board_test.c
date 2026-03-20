/*
 * board_test.c
 *
 *  Created on: Mar 25, 2024
 *      Author: hanyazou
 */

#include "main.h"
#include "board_test.h"

#include <stdio.h>
#include <stdint.h>

#define CAT(x, y) _CAT(x, y)
#define _CAT(x, y) x ## y
#define set_pin(pin, v) ((v) ? (CAT(pin,_GPIO_Port)->ODR |= CAT(pin,_Pin)) : \
                         (CAT(pin,_GPIO_Port)->ODR &= ~CAT(pin,_Pin)))
#define get_pin(pin) ((CAT(pin,_GPIO_Port)->IDR & CAT(pin,_Pin)) ? 1 : 0)
#define set_pin_dir(pin, v) \
    ((v) ? \
     (CAT(pin,_GPIO_Port)->MODER &= ~((CAT(pin,_Pin))*(CAT(pin,_Pin))*3)) : \
     (CAT(pin,_GPIO_Port)->MODER |=  ((CAT(pin,_Pin))*(CAT(pin,_Pin))  )))

typedef uint8_t __bit;

static inline void set_mask16(volatile uint16_t *r, uint16_t v, uint16_t m) {
    *r = (*r & ~m) | (v & m);
}

static inline void set_mask32(volatile uint32_t *r, uint32_t v, uint32_t m) {
    *r = (*r & ~m) | (v & m);
}

static uint8_t data_pins(void) { return (uint8_t)(GPIOB->IDR >> 8); }
static void set_data_pins(uint8_t v) { GPIOB->ODR = ((GPIOB->ODR & 0xff) | ((uint16_t)(v) << 8)); }
static void set_data_dir(uint8_t v) {
    GPIOB->MODER = ((GPIOB->MODER & 0x0000ffff) | ((v) ? 0 : 0x55550000));
}
static uint16_t addr_pins(void) {
    return (uint16_t)(((GPIOA->IDR & 0x0700) << 5) | (GPIOC->IDR & 0x1fff));
}
static void set_addr_pins(uint16_t v) {
    set_mask32(&GPIOA->ODR, v >> 5, 0x0700);  // PA8 ~ PA10
    set_mask32(&GPIOC->ODR, v     , 0x1fff);  // PC0 ~ PC12
}
static void set_addr_dir(uint8_t v) {
    set_mask32(&GPIOA->MODER, v ? 0UL : 0x55555555UL, 0x003f0000);  // PA8 ~ PA10
    set_mask32(&GPIOC->MODER, v ? 0UL : 0x55555555UL, 0x03ffffff);  // PC0 ~ PC12
}

#define PIN_DIR_OUTPUT 0
#define PIN_DIR_INPUT 1
#define PIN_HIGH 1
#define PIN_LOW 0
#define PIN_ACTIVE 0
#define PIN_INACTIVE 1

static __bit ioreq_pin(void) { return get_pin(Z80_IOREQ); }
//static __bit memrq_pin(void) { return get_pin(Z80_MEMRQ); }
static __bit wait_pin(void) { return get_pin(Z80_WAIT); }
//static __bit rd_pin(void) { return get_pin(Z80_RD); }
static __bit wr_pin(void) { return get_pin(Z80_WR); }
static void set_ioreq_pin(uint8_t v) { set_pin(Z80_IOREQ, v); }
static void set_memrq_pin(uint8_t v) { set_pin(Z80_MEMRQ, v); }
static void set_rd_pin(uint8_t v) { set_pin(Z80_RD, v); }
static void set_wr_pin(uint8_t v) { set_pin(Z80_WR, v); }
static void set_busrq_pin(uint8_t v) { set_pin(Z80_BUSRQ, v); }
static void set_reset_pin(uint8_t v) { set_pin(Z80_RESET, v); }
static void set_wait_pin(uint8_t v) { set_pin(Z80_WAIT, v); }
static void set_wait_pin_dir(uint8_t v) { set_pin_dir(Z80_WAIT, v); }

static void bus_master(int enable)
{
    if (enable) {
        // Set address bus as output
        set_addr_dir(PIN_DIR_OUTPUT);

        // Set /IOREQ, /MEMRQ, /RD and /WR as output
        set_ioreq_pin(PIN_INACTIVE);
        set_memrq_pin(PIN_INACTIVE);
        set_rd_pin(PIN_INACTIVE);
        set_wr_pin(PIN_INACTIVE);
        set_wait_pin(PIN_INACTIVE);
        set_pin_dir(Z80_IOREQ, PIN_DIR_OUTPUT);
        set_pin_dir(Z80_MEMRQ, PIN_DIR_OUTPUT);
        set_pin_dir(Z80_RD, PIN_DIR_OUTPUT);
        set_pin_dir(Z80_WR, PIN_DIR_OUTPUT);
    } else {
        // Set address and data bus as input
        set_addr_dir(PIN_DIR_INPUT);
        set_data_dir(PIN_DIR_INPUT);

        // Set /IOREQ, /MEMRQ, /RD and /WR as input
        set_pin_dir(Z80_IOREQ, PIN_DIR_INPUT);
        set_pin_dir(Z80_MEMRQ, PIN_DIR_INPUT);
        set_pin_dir(Z80_RD, PIN_DIR_INPUT);
        set_pin_dir(Z80_WR, PIN_DIR_INPUT);
    }
}

#define UART_DREG 0x00		//Data REG
#define UART_CREG 0x01		//Control REG
extern size_t rom_size;
extern const uint8_t rom[];

void board_test(void)
{
    uint16_t addr;
    uint8_t data;
    int io_write;

    /*
     * reset Z80
     */
    delay_us(8);
    set_reset_pin(PIN_ACTIVE);
    delay_us(8);

    set_pin(BANK_SEL0, 0);  // A16
    set_pin(BANK_SEL1, 1);  // for TC551001 CE2 pin
    #ifdef BANK_SEL2
    set_pin(BANK_SEL2, 0);  // A18
    #endif

    bus_master(1);

    addr = 0;
    set_memrq_pin(PIN_ACTIVE);
    set_data_dir(PIN_DIR_OUTPUT);
    set_addr_pins(addr);
    while (addr < rom_size) {
        set_data_pins(rom[addr]);
        set_wr_pin(PIN_ACTIVE);
        delay_us(1);
        set_wr_pin(PIN_INACTIVE);
        set_addr_pins(++addr);
    }

    /*
     * verify RAM data
     */
    addr = 0;
    set_data_dir(PIN_DIR_INPUT);
    set_addr_pins(addr);
    while (addr < rom_size) {
        set_rd_pin(PIN_ACTIVE);
        data = data_pins();
        if (data != rom[addr]) {
            printf("ERROR at %04X, %02X != %02X\r\n", addr, data, rom[addr]);
        }
        delay_us(1);
        set_rd_pin(PIN_INACTIVE);
        set_addr_pins(++addr);
    }
    set_memrq_pin(PIN_INACTIVE);

    set_busrq_pin(PIN_INACTIVE);
    set_wait_pin_dir(PIN_DIR_INPUT);
    bus_master(0);

    /*
     * release reset ...
     */
    delay_us(8);
    set_reset_pin(PIN_INACTIVE);

    /*
     * run Z80
     */
    while (1) {
        while (wait_pin() != PIN_ACTIVE) {
            ;
        }

        /*
         * handle I/O request
         */
        if (ioreq_pin() != PIN_ACTIVE) {
            printf("%s: IOREQ is not active\r\n", __func__);
            while (1);
        }

        addr = addr_pins();
        io_write = (wr_pin() == PIN_ACTIVE);

        if (io_write) {
            data = data_pins();
            switch (addr & 0xff) {
            case UART_DREG:
                printf("%c", data);
                break;
            default:
                printf("%s: write %02X, %02X %c\r\n", __func__, addr & 0xff, data,
                       (0x30 <= data && data <= 0x7f) ? data : ' ');
                break;
            }
        } else {
            switch (addr & 0xff) {
            case UART_DREG:
                data = getchar();
                break;
            case UART_CREG:
                data = input_key_available() ? 0x03 : 0x02;
                break;
            default:
                printf("%s:  read %02X\r\n", __func__, addr & 0xff);
                data = 0xff;
                break;
            }
            set_data_pins(data);
            set_data_dir(PIN_DIR_OUTPUT);
        }

        set_busrq_pin(PIN_ACTIVE);
        set_wait_pin(PIN_INACTIVE);
        set_wait_pin_dir(PIN_DIR_OUTPUT);
        while (ioreq_pin() == PIN_ACTIVE) {
            ;
        }
        set_wait_pin_dir(PIN_DIR_INPUT);
        if (!io_write) {
            set_data_dir(PIN_DIR_INPUT);
        }
        set_busrq_pin(PIN_INACTIVE);
    }

    /*
     * stop Z80
     */
    set_reset_pin(PIN_ACTIVE);
    delay_us(8);
    bus_master(1);

    printf("%s: halt.\r\n", __func__);
    while (1)
        HAL_Delay(10000);
}

const uint8_t rom[] = {
#include "emubasic.inc"
};
size_t rom_size = sizeof(rom);
