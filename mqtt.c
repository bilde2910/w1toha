#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "mqtt.h"

int open_socket(const char *addr, const char *port) {
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Must be TCP */

    int sockfd = -1, r;
    struct addrinfo *p, *servinfo;

    if ((r = getaddrinfo(addr, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "failed to open socket (getaddrinfo): %s\n", gai_strerror(r));
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) != -1) {
            break;
        }
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(servinfo);
    return sockfd;
}

uint8_t encode_length(uint8_t *buf, uint32_t len) {
    uint8_t idx = 0;
    do {
        buf[idx] = len % 128;
        len /= 128;
        if (len > 0) buf[idx] |= 128;
        idx++;
    } while (len > 0);
    return idx;
}

ssize_t send_mqtt(int sockfd, const struct packet_t *packet) {
    uint8_t *msg_buf = malloc(packet->len + 5);
    uint32_t offset = 0;
    msg_buf[offset++] = packet->type_flags;
    offset += encode_length(msg_buf + offset, packet->len);
    memcpy(msg_buf + offset, packet->data, packet->len);
    ssize_t written = send(sockfd, msg_buf, offset + packet->len, 0);
    free(msg_buf);
    return written;
}

void free_packet(struct packet_t *packet) {
    free(packet->data);
}

int read_mqtt(int sockfd, struct packet_t *packet) {
    uint8_t byte;
    packet->len = 0;
    uint32_t multiplier = 1;
    read(sockfd, &packet->type_flags, 1);
    do {
        read(sockfd, &byte, 1);
        packet->len += (byte & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128 * 128 * 128) return -1;
    } while ((byte & 128) != 0);
    packet->data = malloc(packet->len);
    read(sockfd, packet->data, packet->len);
    return 0;
}

void write_byte(struct packet_t *packet, uint8_t val) {
    packet->data[packet->len] = val;
    packet->len++;
}

void write_short(struct packet_t *packet, uint16_t val) {
    val = htons(val);
    memcpy(packet->data + packet->len, &val, 2);
    packet->len += 2;
}

void write_string(struct packet_t *packet, const char *str) {
    uint16_t len = strlen(str);
    write_short(packet, len);
    memcpy(packet->data + packet->len, str, len);
    packet->len += len;
}

ssize_t send_connect(int sockfd, const char *username, const char *password) {
    uint8_t clid_len = 22;
    char *client_id = malloc(clid_len);
    strncpy(client_id, "w1-mqtt-", clid_len);
    srand(time(NULL));
    static const char *rand_alpha = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 8; i < clid_len; i++) {
        int idx = rand() % strlen(rand_alpha);
        client_id[i] = rand_alpha[idx];
    }

    printf("mqtt client ID is %s\n", client_id);

    struct packet_t packet;
    packet.type_flags = MQTT_TYPE_CONNECT;
    packet.len = 0;
    packet.data = malloc(512);

    // write header
    write_string(&packet, "MQTT");
    // set MQTT version
    write_byte(&packet, MQTT_CONNECT_VERSION_3_1_1);
    // set flags
    write_byte(
        &packet,
        MQTT_CONNECT_FLAG_USERNAME
        | MQTT_CONNECT_FLAG_PASSWORD
        | MQTT_CONNECT_FLAG_CLEAN_SESSION
    );
    // set keep-alive
    write_short(&packet, 60);
    // write client ID
    write_string(&packet, client_id);
    free(client_id);
    // authenticate
    write_string(&packet, username);
    write_string(&packet, password);

    ssize_t written = send_mqtt(sockfd, &packet);
    free_packet(&packet);
    return written;
}

ssize_t send_publish(int sockfd, const char *topic, const char *message, int flags) {
    struct packet_t packet;
    packet.type_flags = flags
        | MQTT_TYPE_PUBLISH
        | MQTT_FLAG_PUBLISH_QOS_AT_MOST_ONCE;
    packet.len = 0;
    packet.data = malloc(2 + strlen(topic) + strlen(message));

    write_string(&packet, topic);
    memcpy(packet.data + packet.len, message, strlen(message));
    packet.len += strlen(message);

    ssize_t written = send_mqtt(sockfd, &packet);
    free_packet(&packet);
    return written;
}
