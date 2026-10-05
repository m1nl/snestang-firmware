
// Original author: Bruno Levy
// https://github.com/BrunoLevy/learn-fpga/blob/master/FemtoRV/FIRMWARE/LIBFEMTORV32/spi_sd.c
//
// Changes by nand2mario, 1/2014
// - Use byte-based PIO instead of bit-banging
// - Simplified initialization process

#include "picorv32.h"

// How to use MMC/SDC: http://elm-chan.org/docs/mmc/mmc_e.html
// SD over SPI: https://onlinedocs.microchip.com/pr/GUID-F9FE1ABC-D4DD-4988-87CE-2AFD74DEA334-en-US-3/index.html?GUID-48879CB2-9C60-4279-8B98-E17C499B12AF
// http://www.dejazzer.com/ee379/lecture_notes/lec12_sd_card.pdf
// https://electronics.stackexchange.com/questions/77417/what-is-the-correct-command-sequence-for-microsd-card-initialization-in-spi
// send and receive a byte over SPI to sd card
uint8_t spi_send(uint8_t x) {
	reg_spimaster_byte = x;			// send
	return reg_spimaster_byte;		// receive
}

uint32_t spi_send_word(uint32_t x) {
    reg_spimaster_word = x;
    return reg_spimaster_word;
}

uint8_t spi_receive() {
    return spi_send(0xff);
}

uint32_t spi_receive_word() {
    return spi_send_word(0xFFFFFFFF);
}

uint8_t spi_sendrecv(uint8_t x) {
    spi_send(x);
    return spi_receive();
}

void spi_readblock(uint8_t *ptr, int length) {
    int i = 0;
    if ((((uintptr_t)ptr) & 3) == 0) {   // aligned on word boundaries
        // transfer in 4-byte words. this is about twice as fast
        for (; i+4<=length; i+=4) {
            *(uint32_t *)ptr = spi_receive_word();
            ptr+=4;
        }
    }
    for (; i<length; i++) {
        *ptr++ = spi_receive();
    }
}

void spi_writeblock(const uint8_t *ptr, int length) {
    int i = 0;
    if ((((uintptr_t)ptr) & 3) == 0) {   // aligned on word boundaries
        for (; i+4<=length; i+=4) {
            spi_send_word(*(uint32_t *)ptr);
            ptr += 4;
        }
    }
    for (; i<length; i++) {
        spi_send(*ptr++);
    }
}

#define CMD0_GO_IDLE_STATE              0
#define CMD1_SEND_OP_COND               1
#define CMD8_SEND_IF_COND               8
#define CMD12_STOP_TRANSMISSION         12
#define CMD17_READ_SINGLE_BLOCK         17
#define CMD18_READ_MULTIPLE_BLOCK       18
#define CMD24_WRITE_SINGLE_BLOCK        24
#define CMD13_SEND_STATUS               13
#define CMD16_SET_BLOCKLEN              16
#define CMD32_ERASE_WR_BLK_START        32
#define CMD33_ERASE_WR_BLK_END          33
#define CMD38_ERASE                     38
#define ACMD41_SD_SEND_OP_COND          41
#define CMD55_APP_CMD                   55
#define CMD58_READ_OCR                  58

#define CMD_START_BITS                  0x40
#define CMD0_CRC                        0x95
#define CMD8_CRC                        0x87

#define OCR_SHDC_FLAG                   0x40
#define CMD_OK                          0x01

#define CMD8_3V3_MODE_ARG               0x1AA

#define ACMD41_HOST_SUPPORTS_SDHC       0x40000000

#define CMD_START_OF_BLOCK              0xFE
#define CMD_DATA_ACCEPTED               0x05

static int sdhc_card = 0;

/* Set to 1 to make the existing sd_readsector() entry point use CMD18. */
#ifndef SD_USE_CMD18_READ
#define SD_USE_CMD18_READ 1
#endif

uint8_t sd_send_command(uint8_t cmd, uint32_t arg) {
    uint8_t response = 0xFF;
    uint8_t status;

    // If non-SDHC card, use byte addressing rather than block (512) addressing
    if(!sdhc_card) {
        switch (cmd) {
            case CMD17_READ_SINGLE_BLOCK:
            case CMD24_WRITE_SINGLE_BLOCK:
            case CMD32_ERASE_WR_BLK_START:
            case CMD33_ERASE_WR_BLK_END:
		        arg *= 512;
		        break;
        }
    }

    spi_send(cmd | CMD_START_BITS);
    spi_send((arg >> 24));
    spi_send((arg >> 16));
    spi_send((arg >> 8));
    spi_send((arg >> 0));

    // CRC required for CMD8 (0x87) & CMD0 (0x95) - default to CMD0
    spi_send((cmd == CMD8_SEND_IF_COND) ? CMD8_CRC : CMD0_CRC);

    // Wait for response (i.e MISO not held high)
    int count = 0;
    while((response = spi_receive()) == 0xff) {
        if(count > 500) {
                break;
        }
        ++count;
    }

    // CMD58 has a R3 response
    if(cmd == CMD58_READ_OCR && response == 0x00) {
        // Check for SDHC card
        status = spi_receive();
        if (!(status & 0x80)) response = 0xff; // OCR power-up must be complete
        if(status & OCR_SHDC_FLAG) {
                sdhc_card = 1;
        } else {
                sdhc_card = 0;
        }
        // Ignore other response bytes for now
        spi_receive();
        spi_receive();
        spi_receive();
    } else if (cmd == CMD8_SEND_IF_COND && response == CMD_OK) {
        // CMD8 has a R7 response with 32-bit return value
        uint32_t echo = (uint32_t)spi_receive() << 24;
        echo |= (uint32_t)spi_receive() << 16;
        echo |= (uint32_t)spi_receive() << 8;
        echo |= spi_receive();
        if (echo != CMD8_3V3_MODE_ARG) response = 0xff;
    }

    // Additional 8 clock cycles over SPI
    spi_send(0xFF);

    return response;
}

int flag;

int sd_init() {
    int retries;
    uint8_t response = 0xFF;
    uint8_t sd_version;
    sdhc_card = 0; // never retain addressing mode from a previous card

    // 74 or more clock pulses to SCLK
    for (int i = 0; i < 10; i++)
        spi_send(0xff);

    retries = 0;

    do {
        response = sd_send_command(CMD0_GO_IDLE_STATE, 0);
        if(retries++ > 8) {
            DEBUG("SD init failure: CMD0\n");
            return -1;
    }
    } while(response != CMD_OK);

    spi_send(0xff);
    spi_send(0xff);

    // Set to default to compliance with SD spec 2.x
    sd_version = 2;

    // Send CMD8 to check for SD Ver2.00 or later card
    flag = 1;
    retries = 0;
    do {
        // Request 3.3V (with check pattern)
        response = sd_send_command(CMD8_SEND_IF_COND, CMD8_3V3_MODE_ARG);
        if (response == 0x05) { // idle + illegal command: SD v1
            sd_version = 1;
            break;
        }
        if(retries++ > 8) {
            DEBUG("SD init failure: CMD8\n");
            return -3;
        }
    } while(response != CMD_OK);

    retries = 0;

    uint32_t init_started = (uint32_t)time_millis();
    do {
        // Send CMD55 (APP_CMD) to allow ACMD to be sent
        response = sd_send_command(CMD55_APP_CMD,0);
        if (response != 0x00 && response != CMD_OK) return -4;
        // delay(100);
        // Inform attached card that SDHC support is enabled
        response = sd_send_command(ACMD41_SD_SEND_OP_COND,
                                  sd_version == 2 ? ACMD41_HOST_SUPPORTS_SDHC : 0);
        retries++;
        if((uint32_t)((uint32_t)time_millis() - init_started) >= 1000) {
	        // CS_H(1);
            DEBUG("SD init failure: ACMD41, %d\n", response);
	        return -2;
        }
        if (response != 0x00) delay(1);
        // if (retries & 0x15 == 0)       // retry as long as 400ms
        //     delay(50);
    } while(response != 0x00);
    DEBUG("ACMD41 succeeded after %d tries.\n", retries);

    // Query card to see if it supports SDHC mode
    if (sd_version == 2) {
        retries = 0;

        do {
	        response = sd_send_command(CMD58_READ_OCR, 0);
	        if(retries++ > 8)
	            return -5;
        } while(response != 0x00);
    } else {
       // Standard density only
       sdhc_card = 0;
    }

    if (!sdhc_card && sd_send_command(CMD16_SET_BLOCKLEN, 512) != 0x00)
        return -6;
    DEBUG("SD init complete. sdhc_card=%d, sd_version=%d\n", sdhc_card, sd_version);
    return 0;
}

void debug_print_buf(uint8_t *buf, int len) {
#ifdef SECTOR_PRINT_FULL
    for (int i = 0; i < len; i++) {
        if ((i & 0xf) == 0 && i != 0)
            uart_print("\n");
        if ((i & 0xf) == 0) {
            uart_print_hex_digits(i, 3);
            uart_print(": ");
        }
        uart_print(" ");
        uart_print_hex_digits(buf[i], 2);
    }
#else
    uart_print_hex_digits(buf[0], 2);
    uart_print(" ");
    uart_print_hex_digits(buf[1], 2);
    uart_print(" ... ");
    uart_print_hex_digits(buf[510], 2);
    uart_print(" ");
    uart_print_hex_digits(buf[511], 2);
#endif
    uart_print("\n");
}

int sd_readsector(uint32_t start_block, uint8_t *buffer, uint32_t sector_count) {
    uint8_t response;
    int retries;

    // DEBUG("sd_readsector: %d %d\n", start_block, sector_count);
    if (sector_count == 0)
        return 0;
    while (sector_count--) {
        // Request block read
        response = sd_send_command(CMD17_READ_SINGLE_BLOCK, start_block++);
        if(response != 0x00) {
            // DEBUG("sd_readsector: Bad response %x\n", response);
            return 0;
        }

        retries = 0;

        // Wait for start of block indicator
        while(spi_receive() != CMD_START_OF_BLOCK) {
            // Timeout
            if(retries > 5000) {
                // DEBUG("sd_readsector: Timeout\n");
                return 0;
            }
            ++retries;
        }

        // Perform block read (512 bytes)
        spi_readblock(buffer, 512);

        // DEBUG("sector: %d\n", start_block-1);
        // debug_print_buf(buffer, 512);

        buffer += 512;

        // Ignore 16-bit CRC
        spi_receive();
        spi_receive();

        // DEBUG("sd_readsector: CRC over\n");

        // Additional 8 SPI clocks
        spi_sendrecv(0xFF);
        // DEBUG("sd_readsector: additional 8 spi clocks over\n");
    }
    // DEBUG("sd_readsector: return\n");
    return 1;
}

/*
 * Send a command without the extra clock generated by sd_send_command().
 * This is needed for CMD18 because that extra transfer could consume the
 * first data token. CMD12 has one stuff byte before its R1 response in SPI
 * mode, so the caller can request that it be discarded.
 */
static uint8_t sd_send_multiblock_command(uint8_t cmd, uint32_t arg,
                                          int discard_stuff_byte) {
    uint8_t response = 0xFF;
    int retries = 0;

    spi_send(cmd | CMD_START_BITS);
    spi_send(arg >> 24);
    spi_send(arg >> 16);
    spi_send(arg >> 8);
    spi_send(arg);
    spi_send(CMD0_CRC);      /* CRC is ignored after SPI initialization */

    if (discard_stuff_byte)
        spi_receive();

    while ((response = spi_receive()) == 0xFF) {
        if (retries++ > 500)
            break;
    }

    return response;
}

/*
 * Stop an active CMD18 transfer. CMD12 returns an R1b response: one stuff
 * byte, the R1 status, and then a busy period while the card drives MISO low.
 */
static int sd_stop_multiblock_read(void) {
    uint8_t response;
    int retries = 0;

    response = sd_send_multiblock_command(CMD12_STOP_TRANSMISSION, 0, 1);
    if (response != 0x00)
        return 0;

    while (spi_receive() != 0xFF) {
        if (retries++ > 15000)
            return 0;
    }

    /* Provide eight clocks after the completed transaction. */
    spi_send(0xFF);
    return 1;
}

/*
 * Alternative multi-sector reader using one CMD18 transaction followed by
 * CMD12. It has the same success convention as sd_readsector(): 1 on success,
 * 0 on failure. If a data block fails, CMD12 is still sent to leave the card
 * in a well-defined state.
 */
int sd_readsector_multi(uint32_t start_block, uint8_t *buffer,
                        uint32_t sector_count) {
    uint32_t arg;
    uint8_t response;
    int result = 1;

    if (sector_count == 0)
        return 0;

    arg = sdhc_card ? start_block : start_block * 512;
    response = sd_send_multiblock_command(CMD18_READ_MULTIPLE_BLOCK, arg, 0);
    if (response != 0x00)
        return 0;

    while (sector_count--) {
        int retries = 0;

        while (spi_receive() != CMD_START_OF_BLOCK) {
            if (retries++ > 15000) {
                result = 0;
                break;
            }
        }
        if (!result)
            break;

        spi_readblock(buffer, 512);
        buffer += 512;

        /* Ignore the 16-bit data CRC. */
        spi_receive();
        spi_receive();
    }

    if (!sd_stop_multiblock_read())
        result = 0;

    return result;
}

// A byte count is not a reliable programming timeout at different SPI speeds.
// The card is ready only when it releases MISO to 0xFF.
static int sd_wait_ready(uint32_t timeout_ms) {
    uint32_t started = (uint32_t)time_millis();
    do {
        if (spi_receive() == 0xff) return 1;
    } while ((uint32_t)((uint32_t)time_millis() - started) < timeout_ms);
    return 0;
}

int sd_sync(void) {
    if (!sd_wait_ready(1000)) return 0;
    // CMD13 has an R2 response. The generic command helper discards the
    // second status byte in its trailing clocks, so use the raw helper.
    uint8_t r1 = sd_send_multiblock_command(CMD13_SEND_STATUS, 0, 0);
    uint8_t r2 = spi_receive();
    spi_send(0xff);
    return r1 == 0 && r2 == 0;
}

int sd_writesector(uint32_t start_block, const uint8_t *buffer, uint32_t sector_count) {
    if (!buffer || sector_count == 0 || sector_count - 1 > UINT32_MAX - start_block)
        return 0;
    if (!sdhc_card && start_block + sector_count - 1 > UINT32_MAX / 512)
        return 0;
    while (sector_count--) {
        if (!sd_wait_ready(1000)) return 0;
        uint8_t response = sd_send_command(CMD24_WRITE_SINGLE_BLOCK, start_block++);
        if (response != 0x00) return 0;
        spi_send(CMD_START_OF_BLOCK);
        spi_writeblock(buffer, 512);
        buffer += 512;
        spi_send(0xff); spi_send(0xff); // CRC disabled in SPI mode
        // Some cards delay the data response token. Do not accept a rejected
        // block or report success while programming is still in progress.
        int polls = 0;
        do { response = spi_receive(); } while (response == 0xff && ++polls < 64);
        int accepted = (response & 0x1f) == CMD_DATA_ACCEPTED;
        if (!sd_wait_ready(1000) || !accepted) return 0;
        // Busy release alone does not report programming/write-protect errors.
        if (!sd_sync()) return 0;
    }
    return 1;
}
