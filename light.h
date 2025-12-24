// max7219_32x8.c
// Controls a 32x8 (4x MAX7219) dot-matrix module over SPI (spidev).
// Build: gcc -O2 -Wall max7219_32x8.c -o max7219_32x8
// Run : sudo ./max7219_32x8

#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_DEV        "/dev/spidev0.0"
#define SPI_HZ         10000000u   // 10 MHz is typically fine for MAX7219
#define NUM_DEVICES    4           // 4 cascaded MAX7219 = 32x8
#define WIDTH          32
#define HEIGHT         8

// MAX7219 registers
#define REG_NOOP       0x00
#define REG_DIGIT0     0x01  // rows are sent as DIGIT1..DIGIT8 (1..8)
#define REG_DECODE     0x09
#define REG_INTENSITY  0x0A
#define REG_SCANLIMIT  0x0B
#define REG_SHUTDOWN   0x0C
#define REG_DISPLAYTEST 0x0F

static int spi_fd = -1;

static int cur_light_x = 0, cur_light_y = 0;

// Framebuffer: 8 rows, 32 columns. Each row is 32 bits; bit=1 means LED on.
static uint32_t fb[HEIGHT];

static int spi_init(void) {
    spi_fd = open(SPI_DEV, O_RDWR);
    if (spi_fd < 0) {
        perror("open spidev");
        return -1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_HZ;

    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) { perror("SPI_IOC_WR_MODE"); return -1; }
    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) { perror("SPI_IOC_WR_BITS_PER_WORD"); return -1; }
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) { perror("SPI_IOC_WR_MAX_SPEED_HZ"); return -1; }

    return 0;
}

// Send one register+data pair to ALL devices (broadcast).
static int max7219_broadcast(uint8_t reg, uint8_t data) {
    // For daisy-chain, you clock out pairs for each device, farthest device first.
    uint8_t tx[NUM_DEVICES * 2];
    for (int i = 0; i < NUM_DEVICES; i++) {
        tx[i * 2 + 0] = reg;
        tx[i * 2 + 1] = data;
    }
    ssize_t n = write(spi_fd, tx, sizeof(tx));
    if (n != (ssize_t)sizeof(tx)) {
        perror("spi write");
        return -1;
    }
    return 0;
}

// Send a single row (digit 1..8) with per-device data bytes (one byte per 8x8).
static int max7219_send_row(int digit_1to8, const uint8_t dev_data[NUM_DEVICES]) {
    if (digit_1to8 < 1 || digit_1to8 > 8) return -1;

    uint8_t tx[NUM_DEVICES * 2];
    // IMPORTANT: order is farthest -> nearest in the chain.
    // We'll assume "device 0" is the LEFTMOST 8x8 and "device 3" is RIGHTMOST.
    // If your display is mirrored, just reverse dev indexing below.
    for (int d = 0; d < NUM_DEVICES; d++) {
        int chain_index = (NUM_DEVICES - 1) - d;      // farthest first
        tx[chain_index * 2 + 0] = (uint8_t)digit_1to8;
        tx[chain_index * 2 + 1] = dev_data[d];
    }

    ssize_t n = write(spi_fd, tx, sizeof(tx));
    if (n != (ssize_t)sizeof(tx)) {
        perror("spi write");
        return -1;
    }
    return 0;
}

static int max7219_init(uint8_t intensity_0to15) {
    if (intensity_0to15 > 15) intensity_0to15 = 15;

    // Recommended init sequence
    if (max7219_broadcast(REG_DISPLAYTEST, 0x00) < 0) return -1; // off
    if (max7219_broadcast(REG_DECODE, 0x00) < 0) return -1;      // no decode
    if (max7219_broadcast(REG_SCANLIMIT, 0x07) < 0) return -1;   // 8 rows
    if (max7219_broadcast(REG_INTENSITY, intensity_0to15) < 0) return -1;
    if (max7219_broadcast(REG_SHUTDOWN, 0x01) < 0) return -1;    // normal op

    // Clear display
    for (int row = 1; row <= 8; row++) {
        uint8_t zeros[NUM_DEVICES] = {0,0,0,0};
        if (max7219_send_row(row, zeros) < 0) return -1;
    }
    return 0;
}

static void fb_clear(void) {
    for (int y = 0; y < HEIGHT; y++) fb[y] = 0;
}

static void fb_set_pixel(int x, int y, int on) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    uint32_t mask = 1u << x;
    if (on) fb[y] |= mask;
    else    fb[y] &= ~mask;
}

// Push framebuffer to MAX7219.
// Mapping note: MAX7219 digit rows are 1..8. Each device gets 8 columns as one byte.
static int fb_flush(void) {
    for (int y = 0; y < HEIGHT; y++) {
        uint8_t dev_bytes[NUM_DEVICES] = {0,0,0,0};

        // Split 32 bits into 4 chunks of 8 columns.
        // We treat x=0 as leftmost. Each chunk becomes one byte for one 8x8 device.
        for (int d = 0; d < NUM_DEVICES; d++) {
            uint8_t b = 0;
            for (int bit = 0; bit < 8; bit++) {
                int x = d * 8 + bit;
                // Put left->right into MSB->LSB (often matches common 8x8 orientation).
                // If yours is rotated, you may need to flip bit order or swap x/y.
                int on = (fb[y] >> x) & 1u;
                b |= (uint8_t)(on ? (1u << bit) : 0u);
            }
            dev_bytes[d] = b;
        }

        if (max7219_send_row(y + 1, dev_bytes) < 0) return -1;
    }
    return 0;
}

static int init_lights(void) {
    if (spi_init() < 0) return 1;
    if (max7219_init(4) < 0) return 1; // intensity 0..15

    fb_clear();
    return 0;
}

static void finish_lights(void) {
	fb_clear();
    fb_flush();
    close(spi_fd);
}

static int next_light(void) {
	if (++cur_light_x > HEIGHT) {
		cur_light_x == 0;
		if (++cur_light_y > HEIGHT) return 1;
	}
	fb_clear();
    fb_set_pixel(cur_light_x, cur_light_y, 1);
    if (fb_flush() < 0) return 1;
    return 0;
}

