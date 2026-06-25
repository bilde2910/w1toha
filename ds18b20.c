#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ds18b20.h"

int set_if_attrs(int fd, speed_t speed) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "error %d from tcgetattr", errno);
        return 1;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag |= (CLOCAL | CREAD);    // ignore modem controls
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         // 8-bit characters
    tty.c_cflag &= ~PARENB;     // no parity bit
    tty.c_cflag &= ~CSTOPB;     // only need 1 stop bit
    tty.c_cflag &= ~CRTSCTS;    // no hardware flowcontrol

    // setup for non-canonical mode
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    // fetch bytes as they become available
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "error %d from tcsetattr", errno);
        return 2;
    }

    return 0;
}

uint8_t w1_exchange_byte(int fd, uint8_t byte) {
    uint8_t buf[8];
    for (int i = 0; i < 8; i++) {
        buf[i] = ((byte >> i) & 1) ? 0xFF : 0x00;
    }
    write(fd, &buf, 8);
    read(fd, &buf, 8);
    uint8_t resp = 0;
    for (int i = 0; i < 8; i++) {
        resp |= (buf[i] == 0xFF ? 1 : 0) << i;
    }
    return resp;
}

void w1_read(int fd, uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = w1_exchange_byte(fd, 0xFF);
    }
}

void w1_reset(int fd) {
    set_if_attrs(fd, B9600);
    write(fd, "\xF0", 1);
    uint8_t resp;
    read(fd, &resp, 1);
    set_if_attrs(fd, B115200);
}

void w1_write_rom(int fd, uint64_t rom) {
    for (int i = 7; i >= 0; i--) {
        uint8_t c = (rom >> i * 8) & 0xFF;
        w1_exchange_byte(fd, c);
    }
}

int crc_verify(struct scratchpad_t *pad) {
    uint8_t crc = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t c = ((uint8_t*) pad)[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ c) & 1;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            c >>= 1;
        }
    }
    return crc == pad->crc;
}

int task_ds18b20(struct device_t *device) {
    device->fd = open(device->tty, O_RDWR | O_NOCTTY | O_SYNC);
    if (device->fd < 0) {
        fprintf(stderr, "error %d opening %s: %s", errno, device->tty, strerror(errno));
        return -1;
    }
    w1_reset(device->fd);
    w1_exchange_byte(device->fd, W1_MATCH_ROM);
    w1_write_rom(device->fd, device->rom);
    w1_exchange_byte(device->fd, W1_CONVERT_T);
    return 0;
}

int read_ds18b20(struct device_t *device) {
    if (device->fd < 0) return -1;
    w1_reset(device->fd);
    w1_exchange_byte(device->fd, W1_MATCH_ROM);
    w1_write_rom(device->fd, device->rom);
    w1_exchange_byte(device->fd, W1_READ_SCRATCHPAD);
    struct scratchpad_t *pad = malloc(SCRATCHPAD_SIZE);
    w1_read(device->fd, (uint8_t*)pad, SCRATCHPAD_SIZE);
    close(device->fd);
    device->fd = -1;

    if (!crc_verify(pad)) {
        fprintf(stderr, "error reading temperature: CRC check failed");
        return -1;
    }

    int16_t raw = (pad->temp_msb << 8) | pad->temp_lsb;
    device->temp_c = raw * 0.0625f;
    free(pad);
    return 0;
}
