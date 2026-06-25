#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <termios.h>

#define W1_MATCH_ROM 0x55
#define W1_CONVERT_T 0x44
#define W1_READ_SCRATCHPAD 0xBE

#define SCRATCHPAD_SIZE sizeof(struct scratchpad_t)

struct device_t {
    char *tty;
    uint64_t rom;
    int fd;
    float temp_c;
};

struct scratchpad_t {
    uint8_t temp_lsb;
    uint8_t temp_msb;
    uint8_t reg_th;
    uint8_t reg_tl;
    uint8_t reg_conf;
    uint8_t res_1;
    uint8_t res_2;
    uint8_t res_3;
    uint8_t crc;
};

int set_if_attrs(int fd, speed_t speed);
uint8_t w1_exchange_byte(int fd, uint8_t byte);
void w1_read(int fd, uint8_t *buf, size_t len);
void w1_reset(int fd);
void w1_write_rom(int fd, uint64_t rom);
int crc_verify(struct scratchpad_t *pad);
int task_ds18b20(struct device_t *device);
int read_ds18b20(struct device_t *device);
