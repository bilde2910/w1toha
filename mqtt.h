#pragma once
#include <stdint.h>
#include <sys/types.h>

#define MQTT_MAX_PACKET_SIZE 1536
#define MQTT_MAX_TOPIC_SIZE 128

#define MQTT_TYPE_CONNECT 0x10
#define MQTT_TYPE_CONNACK 0x20
#define MQTT_TYPE_PUBLISH 0x30

#define MQTT_FLAG_PUBLISH_QOS_AT_MOST_ONCE 0x00
#define MQTT_FLAG_PUBLISH_RETAIN 0x01

#define MQTT_CONNECT_VERSION_5 0x05
#define MQTT_CONNECT_FLAG_CLEAN_SESSION 0x02
#define MQTT_CONNECT_FLAG_WILL 0x04
#define MQTT_CONNECT_FLAG_PASSWORD 0x40
#define MQTT_CONNECT_FLAG_USERNAME 0x80

#define MQTT_PROP_PAYLOAD_FORMAT_INDICATOR 0x01
#define MQTT_PROP_MESSAGE_EXPIRY_INTERVAL 0x02
#define MQTT_PROP_CONTENT_TYPE 0x03
#define MQTT_PROP_TOPIC_ALIAS_MAXIMUM 0x22
#define MQTT_PROP_MAXIMUM_PACKET_SIZE 0x27

struct packet_t {
    uint8_t type_flags;
    uint32_t offset;
    uint8_t *data;
};

struct connack_t {
    uint8_t ack_flags;
    uint8_t reason_code;
    // we ignore the properties
};

int open_socket(const char *addr, const char *port);
ssize_t send_mqtt(int sockfd, const struct packet_t *packet);
void free_packet(struct packet_t *packet);
int read_mqtt(int sockfd, struct packet_t *packet);
void write_byte(struct packet_t *packet, uint8_t val);
void write_short(struct packet_t *packet, uint16_t val);
void write_int(struct packet_t *packet, uint32_t val);
int read_varint(struct packet_t *packet, uint32_t *out);
void write_varint(struct packet_t *packet, uint32_t val);
void write_string(struct packet_t *packet, const char *str);
struct packet_t* begin_properties();
void write_properties(struct packet_t *packet, struct packet_t *props);
char* alloc_client_id();
ssize_t send_connect(const int sockfd, const char *client_id, const char *username, const char *password, const uint16_t keepalive, const char *will_topic, const char *will_message);
ssize_t send_publish(const int sockfd, const char *topic, const char *message, const int flags, const uint32_t ttl);
