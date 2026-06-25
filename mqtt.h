#pragma once
#include <stdint.h>
#include <sys/types.h>

#define MQTT_TYPE_CONNECT 0x10
#define MQTT_TYPE_CONNACK 0x20
#define MQTT_TYPE_PUBLISH 0x30

#define MQTT_FLAG_PUBLISH_QOS_AT_MOST_ONCE 0x00
#define MQTT_FLAG_PUBLISH_RETAIN 0x01

#define MQTT_CONNECT_VERSION_3_1_1 0x04
#define MQTT_CONNECT_FLAG_USERNAME 0x80
#define MQTT_CONNECT_FLAG_PASSWORD 0x40
#define MQTT_CONNECT_FLAG_CLEAN_SESSION 0x02

struct packet_t {
    uint8_t type_flags;
    uint32_t len;
    uint8_t *data;
};

struct connack_t {
    uint8_t ack_flags;
    uint8_t return_code;
};

int open_socket(const char *addr, const char *port);
uint8_t encode_length(uint8_t *buf, uint32_t len);
ssize_t send_mqtt(int sockfd, const struct packet_t *packet);
void free_packet(struct packet_t *packet);
int read_mqtt(int sockfd, struct packet_t *packet);
void write_byte(struct packet_t *packet, uint8_t val);
void write_short(struct packet_t *packet, uint16_t val);
void write_string(struct packet_t *packet, const char *str);
ssize_t send_connect(int sockfd, const char *username, const char *password);
ssize_t send_publish(int sockfd, const char *topic, const char *message, int flags);