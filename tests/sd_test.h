#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DEBUG(...) ((void)0)
#define uart_print_hex_digits(...) ((void)0)
#define uart_print(...) ((void)0)
uint8_t spi_send(uint8_t);
uint8_t spi_receive(void);
uint32_t spi_receive_word(void);
void spi_send_word(uint32_t);
uint8_t spi_sendrecv(uint8_t);
int time_millis(void);
void delay(int);
