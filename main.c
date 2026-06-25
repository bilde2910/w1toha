#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ds18b20.h"
#include "mqtt.h"

void print_help() {
    printf("Usage (* = required):\n");
    printf("  -s TTY (*): Path to serial device\n");
    printf("  -r ROM (*): Hexadecimal sensor ID\n");
    printf("  -h MQTT_HOST (*): MQTT server host\n");
    printf("  -P MQTT_PORT (1883): MQTT server port\n");
    printf("  -t MQTT_TOPIC (ds18b20): MQTT root topic\n");
    printf("  -u MQTT_USER (*): MQTT username\n");
    printf("  -p MQTT_PASS (*): MQTT password\n");
    printf("  -i INTERVAL (30): Seconds between readings\n");
    printf("  -a HA_ANNOUNCE_EVERY (-1): Send HA autodiscovery every N measurements (0 = send once, -1 = disable)\n");
}


int main(int argc, char * const argv[]) {
    int max_device = 0;
    char *portname = NULL;
    struct device_t *devices = NULL;

    char *mqtt_host = NULL;
    char *mqtt_port = "1883";
    char *mqtt_root = "ds18b20";
    char *mqtt_user = NULL;
    char *mqtt_pass = NULL;
    unsigned long interval = 30UL;
    long announce_every = -1L;

    int opt;
    while ((opt = getopt(argc, argv, "srhPtupia")) != -1) {
        if (optind > argc) {
            print_help();
            return 1;
        }
        switch (opt) {
            case 'r':
                if (portname == NULL) {
                    print_help();
                    return 1;
                }
                if (max_device == 0) {
                    devices = malloc(sizeof(struct device_t));
                } else {
                    devices = realloc(devices, (max_device + 1) * sizeof(struct device_t));
                }
                devices[max_device].tty = malloc(strlen(portname) + 1);
                strncpy(devices[max_device].tty, portname, strlen(portname) + 1);
                devices[max_device].rom = strtoull(argv[optind++], NULL, 16);
                max_device++;
                break;
            case 's': portname = argv[optind++]; break;
            case 'h': mqtt_host = argv[optind++]; break;
            case 'P': mqtt_port = argv[optind++]; break;
            case 't': mqtt_root = argv[optind++]; break;
            case 'u': mqtt_user = argv[optind++]; break;
            case 'p': mqtt_pass = argv[optind++]; break;
            case 'i': interval = strtoul(argv[optind++], NULL, 10); break;
            case 'a': announce_every = strtol(argv[optind++], NULL, 10); break;
            default: print_help(); return 1;
        }
    }

    if (!max_device) fprintf(stderr, "no devices specified\n");
    if (!mqtt_host) fprintf(stderr, "no MQTT host specified\n");
    if (!mqtt_user) fprintf(stderr, "no MQTT user specified\n");
    if (!mqtt_pass) fprintf(stderr, "no MQTT password specified\n");
    if (!max_device || !mqtt_host || !mqtt_user || !mqtt_pass) {
        print_help();
        return 1;
    }

    if (announce_every < 0) {
        printf("will not announce to Home Assistant\n");
    } else if (announce_every == 0) {
        printf("will announce to Home Assistant on first reading\n");
    } else {
        printf("will announce to Home Assistant every %ld readings\n", announce_every);
    }

    int sockfd = open_socket(mqtt_host, mqtt_port);
    if (sockfd == -1) {
        perror("failed to open socket: ");
        return 1;
    }
    printf("socket opened to %s:%s\n", mqtt_host, mqtt_port);

    printf("authenticating as %s...\n", mqtt_user);
    if (send_connect(sockfd, mqtt_user, mqtt_pass) < 0) {
        close(sockfd);
        fprintf(stderr, "failed to send MQTT connect\n");
        return 1;
    }
    
    struct packet_t packet;
    memset(&packet, 0, sizeof(packet));
    if (read_mqtt(sockfd, &packet) < 0) {
        close(sockfd);
        fprintf(stderr, "failed to read CONNACK\n");
        return 1;
    }

    if (!(packet.type_flags & MQTT_TYPE_CONNACK)) {
        close(sockfd);
        fprintf(stderr, "expected CONNACK, received packet 0x%02X\n", packet.type_flags);
        return 1;
    }

    struct connack_t *connack = (struct connack_t*) packet.data;
    if (connack->return_code != 0) {
        close(sockfd);
        fprintf(stderr, "CONNACK failed with return code 0x%02X\n", connack->return_code);
        free_packet(&packet);
        return 1;
    }
    printf("authentication successful!\n");
    free_packet(&packet);

    int i;
    long counter = 0L;
    char mqtt_topic[256];
    char mqtt_message[1024];
    while (1) {
        for (i = 0; i < max_device; i++) {
            if (task_ds18b20(&devices[i]) < 0) {
                fprintf(stderr, "failed to task DS18B20 sensor %016lx\n", devices[i].rom);
            }
        }
        sleep(1);
        for (i = 0; i < max_device; i++) {
            if (devices[i].fd >= 0) {
                if (read_ds18b20(&devices[i]) < 0) {
                    fprintf(stderr, "failed to read DS18B20 sensor %016lx\n", devices[i].rom);
                } else {
                    printf("sensor %016lx is %f C\n", devices[i].rom, devices[i].temp_c);
                    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%016lx", mqtt_root, devices[i].rom);
                    snprintf(mqtt_message, sizeof(mqtt_message), "{\"T\":%f}", devices[i].temp_c);
                    if (send_publish(sockfd, mqtt_topic, mqtt_message, 0) < 0) {
                        close(sockfd);
                        fprintf(stderr, "failed to publish to MQTT!\n");
                        return 1;
                    }
                    if (announce_every >= 0 && !counter) {
                        printf("announcing auto-discovery to Home Assistant for this sensor\n");
                        snprintf(
                            mqtt_topic,
                            sizeof(mqtt_topic),
                            "homeassistant/sensor/%s-%016lx_T/config",
                            mqtt_root,
                            devices[i].rom
                        );
                        snprintf(
                            mqtt_message,
                            sizeof(mqtt_message),
                            "{\"name\":\"Temperature\",\"stat_t\":\"%s/%016lx\",\"uniq_id\":\"%s-%016lx_T\",\"obj_id\":\"%s-%016lx_T\",\"unit_of_meas\":\"\\u00b0C\",\"val_tpl\":\"{{ value_json.T | is_defined }}\",\"dev\":{\"ids\":[\"%s-%016lx\"],\"name\":\"DS18B20 sensor: %016lx\"},\"dev_cla\":\"temperature\"}",
                            mqtt_root, devices[i].rom,
                            mqtt_root, devices[i].rom,
                            mqtt_root, devices[i].rom,
                            mqtt_root, devices[i].rom,
                            devices[i].rom
                        );
                        if (send_publish(sockfd, mqtt_topic, mqtt_message, MQTT_FLAG_PUBLISH_RETAIN) < 0) {
                            close(sockfd);
                            fprintf(stderr, "failed to publish to MQTT!\n");
                            return 1;
                        }
                    }
                }
            }
        }
        if (announce_every > 0) {
            printf("next HA announce in %ld reading(s)\n", counter ? counter : announce_every);
            if (counter) counter--;
            else counter = announce_every - 1;
        } else if (announce_every == 0) {
            printf("disabling HA announces\n");
            announce_every = -1;
        }
        printf("sleeping until next read...\n");
        sleep(interval - 1);
    }

    close(sockfd);
}
