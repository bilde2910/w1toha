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

ssize_t send_mqtt(int sockfd, const struct packet_t *packet) {
    uint32_t remain_len = packet->offset;
    struct packet_t tmp;
    tmp.data = malloc(remain_len + 5);
    tmp.offset = 0;
    write_byte(&tmp, packet->type_flags);
    write_varint(&tmp, remain_len);
    memcpy(tmp.data + tmp.offset, packet->data, remain_len);
    ssize_t written = send(sockfd, tmp.data, remain_len + tmp.offset, 0);
    free_packet(&tmp);
    return written;
}

void free_packet(struct packet_t *packet) {
    free(packet->data);
}

int read_mqtt(int sockfd, struct packet_t *packet) {
    uint8_t byte;
    packet->offset = 0;
    uint32_t multiplier = 1;
    read(sockfd, &packet->type_flags, 1);
    do {
        read(sockfd, &byte, 1);
        packet->offset += (byte & 127) * multiplier;
        if (multiplier > 128 * 128 * 128) return -1;
        multiplier *= 128;
    } while ((byte & 128) != 0);
    packet->data = malloc(packet->offset);
    read(sockfd, packet->data, packet->offset);
    return 0;
}

void write_byte(struct packet_t *packet, uint8_t val) {
    packet->data[packet->offset] = val;
    packet->offset++;
}

void write_short(struct packet_t *packet, uint16_t val) {
    val = htons(val);
    memcpy(packet->data + packet->offset, &val, 2);
    packet->offset += 2;
}

void write_int(struct packet_t *packet, uint32_t val) {
    val = htonl(val);
    memcpy(packet->data + packet->offset, &val, 4);
    packet->offset += 4;
}

int read_varint(struct packet_t *packet, uint32_t *out) {
    uint32_t multiplier = 1;
    *out = 0;
    do {
        *out += packet->data[packet->offset] * multiplier;
        if (multiplier > 128 * 128 * 128) return -1;
        multiplier *= 128;
    } while ((packet->data[packet->offset++] & 128) != 0);
    return 0;
}

void write_varint(struct packet_t *packet, uint32_t val) {
    do {
        packet->data[packet->offset] = val % 128;
        val /= 128;
        if (val > 0) packet->data[packet->offset] |= 128;
        packet->offset++;
    } while (val > 0);
}

void write_string(struct packet_t *packet, const char *str) {
    uint16_t len = strlen(str);
    write_short(packet, len);
    memcpy(packet->data + packet->offset, str, len);
    packet->offset += len;
}

struct packet_t* begin_properties() {
    struct packet_t *props = malloc(sizeof(struct packet_t));
    props->offset = 0;
    props->data = malloc(MQTT_MAX_PACKET_SIZE);
    return props;
}

void write_properties(struct packet_t *packet, struct packet_t *props) {
    write_varint(packet, props->offset);
    memcpy(packet->data + packet->offset, props->data, props->offset);
    packet->offset += props->offset;
    free_packet(props);
    free(props);
}

char* alloc_client_id() {
    static const char *prefix = "w1toha-";
    uint8_t clid_len = 22;
    char *client_id = malloc(clid_len);
    strncpy(client_id, prefix, clid_len);
    srand(time(NULL));
    static const char *rand_alpha = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = strlen(prefix); i < clid_len; i++) {
        int idx = rand() % strlen(rand_alpha);
        client_id[i] = rand_alpha[idx];
    }
    return client_id;
}

ssize_t send_connect(const int sockfd, const char *client_id, const char *username, const char *password, const uint16_t keepalive, const char *will_topic, const char *will_message) {
    struct packet_t packet;
    packet.type_flags = MQTT_TYPE_CONNECT;
    packet.offset = 0;
    packet.data = malloc(MQTT_MAX_PACKET_SIZE);

    // header:
    // write header
    write_string(&packet, "MQTT");
    // set MQTT version
    write_byte(&packet, MQTT_CONNECT_VERSION_5);
    // set flags
    write_byte(
        &packet,
        MQTT_CONNECT_FLAG_CLEAN_SESSION
        | MQTT_CONNECT_FLAG_WILL
        | MQTT_CONNECT_FLAG_PASSWORD
        | MQTT_CONNECT_FLAG_USERNAME
    );
    // set keep-alive
    write_short(&packet, keepalive);

    // properties:
    struct packet_t *props = begin_properties();
    // - max packet size
    write_byte(props, MQTT_PROP_MAXIMUM_PACKET_SIZE);
    write_int(props, MQTT_MAX_PACKET_SIZE);
    // - max topic size
    write_byte(props, MQTT_PROP_TOPIC_ALIAS_MAXIMUM);
    write_short(props, MQTT_MAX_TOPIC_SIZE);
    // commit properties
    write_properties(&packet, props);

    // payload:
    // write client ID
    write_string(&packet, client_id);
    // will properties
    props = begin_properties();
    // - payload format indicator
    write_byte(props, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR);
    write_byte(props, 0x01);  // utf-8
    // - content type
    write_byte(props, MQTT_PROP_CONTENT_TYPE);
    write_string(props, "application/json");
    // commit will properties
    write_properties(&packet, props);
    // will topic
    write_string(&packet, will_topic);
    // will message
    write_string(&packet, will_message);

    // authenticate
    write_string(&packet, username);
    write_string(&packet, password);

    ssize_t written = send_mqtt(sockfd, &packet);
    free_packet(&packet);
    return written;
}

ssize_t send_publish(const int sockfd, const char *topic, const char *message, const int flags, const uint32_t ttl) {
    struct packet_t packet;
    packet.type_flags = flags
        | MQTT_TYPE_PUBLISH
        | MQTT_FLAG_PUBLISH_QOS_AT_MOST_ONCE;
    packet.offset = 0;
    packet.data = malloc(MQTT_MAX_PACKET_SIZE);

    // header:
    // write topic
    write_string(&packet, topic);

    // properties:
    struct packet_t *props = begin_properties();
    // - payload format indicator
    write_byte(props, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR);
    write_byte(props, 0x01);  // utf-8
    // - content type
    write_byte(props, MQTT_PROP_CONTENT_TYPE);
    write_string(props, "application/json");
    // - message expiry interval
    if (ttl) {
        write_byte(props, MQTT_PROP_MESSAGE_EXPIRY_INTERVAL);
        write_int(props, ttl);
    }
    // commit properties
    write_properties(&packet, props);

    // payload:
    memcpy(packet.data + packet.offset, message, strlen(message));
    packet.offset += strlen(message);

    ssize_t written = send_mqtt(sockfd, &packet);
    free_packet(&packet);
    return written;
}
