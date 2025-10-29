// Build: gcc mqtt_can_bridge.c -o mqtt_can_bridge -lmosquitto
// Usage example:
//   sudo ./mqtt_can_bridge \
//     --host broker.local --port 8883 \
//     --cafile /etc/ssl/certs/ca.crt \
//     --cert /etc/ssl/certs/client.crt \
//     --key /etc/ssl/private/client.key \
//     --canif can0
//
// Before running, bring up SocketCAN, e.g.:
//   sudo ip link set can0 up type can bitrate 125000

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include <mosquitto.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define TOPIC "status/door"
#define CAN_ID_CMD 0x200
#define CMD_LOCK   0x30
#define CMD_UNLOCK 0x31

static volatile int g_running = 1;
static int can_sock = -1;
static char can_ifname[IFNAMSIZ] = "can0";

static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

static void trim(char *s) {
    if (!s) return;
    // trim leading
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    // trim trailing
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

static int can_open(const char *ifname) {
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("socket(PF_CAN)");
        return -1;
    }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        close(s);
        return -1;
    }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind(can)");
        close(s);
        return -1;
    }
    return s;
}

static int can_send_cmd(uint8_t cmd_byte) {
    if (can_sock < 0) return -1;
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = CAN_ID_CMD;        // standard 11-bit ID
    frame.can_dlc = 1;                 // 1 byte payload: command
    frame.data[0] = cmd_byte;

    ssize_t n = write(can_sock, &frame, sizeof(frame));
    if (n != (ssize_t)sizeof(frame)) {
        perror("write(can)");
        return -1;
    }
    return 0;
}

/* ---------- MQTT callbacks ---------- */

static void on_connect(struct mosquitto *mosq, void *userdata, int rc) {
    (void)userdata;
    if (rc == 0) {
        fprintf(stdout, "[MQTT] Connected. Subscribing to %s\n", TOPIC);
        mosquitto_subscribe(mosq, NULL, TOPIC, 1);
    } else {
        fprintf(stderr, "[MQTT] Connect failed, rc=%d\n", rc);
    }
}

static void on_message(struct mosquitto *mosq, void *userdata,
                       const struct mosquitto_message *msg) {
    (void)mosq; (void)userdata;
    if (!msg || !msg->payload || msg->payloadlen <= 0) return;

    // Copy payload into a null-terminated buffer
    char *buf = strndup((const char*)msg->payload, (size_t)msg->payloadlen);
    if (!buf) return;
    trim(buf);

    fprintf(stdout, "[MQTT] %s => '%s'\n", msg->topic, buf);

    int rc = 0;
    if (strcasecmp(buf, "lock") == 0) {
        rc = can_send_cmd(CMD_LOCK);
        fprintf(stdout, rc==0 ? "[CAN] Sent LOCK (0x30)\n" : "[CAN] Failed to send LOCK\n");
    } else if (strcasecmp(buf, "unlock") == 0) {
        rc = can_send_cmd(CMD_UNLOCK);
        fprintf(stdout, rc==0 ? "[CAN] Sent UNLOCK (0x31)\n" : "[CAN] Failed to send UNLOCK\n");
    } else {
        fprintf(stderr, "[WARN] Unknown payload: '%s' (expected 'lock' or 'unlock')\n", buf);
    }

    free(buf);
}

/* ---------- CLI parsing (simple) ---------- */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: sudo %s "
        "--host <broker> --port <8883> "
        "--cafile <path> --cert <path> --key <path> "
        "--canif <can0>\n", prog);
}

int main(int argc, char **argv) {
    const char *host = NULL;
    int port = 8883;
    const char *cafile = NULL;
    const char *certfile = NULL;
    const char *keyfile = NULL;

    // Parse very simple flags
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i+1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i+1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cafile") && i+1 < argc) cafile = argv[++i];
        else if (!strcmp(argv[i], "--cert") && i+1 < argc) certfile = argv[++i];
        else if (!strcmp(argv[i], "--key") && i+1 < argc) keyfile = argv[++i];
        else if (!strcmp(argv[i], "--canif") && i+1 < argc) {
            strncpy(can_ifname, argv[++i], IFNAMSIZ-1);
            can_ifname[IFNAMSIZ-1] = '\0';
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!host || !cafile || !certfile || !keyfile) {
        usage(argv[0]);
        return 1;
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // Open CAN
    can_sock = can_open(can_ifname);
    if (can_sock < 0) {
        fprintf(stderr, "Failed to open CAN interface '%s'\n", can_ifname);
        return 2;
    }
    fprintf(stdout, "[CAN] Opened interface %s\n", can_ifname);

    // Init MQTT
    mosquitto_lib_init();
    struct mosquitto *mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        fprintf(stderr, "mosquitto_new failed\n");
        close(can_sock);
        mosquitto_lib_cleanup();
        return 3;
    }

    // TLS (mTLS)
    // cafile: CA cert to verify broker
    // certfile/keyfile: client certificate & private key for mutual auth
    if (mosquitto_tls_set(mosq, cafile, NULL, certfile, keyfile, NULL) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mosquitto_tls_set failed\n");
        mosquitto_destroy(mosq);
        close(can_sock);
        mosquitto_lib_cleanup();
        return 4;
    }
    // Enforce TLS v1.2+ and default verification
    mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);

    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);

    int rc = mosquitto_connect(mosq, host, port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mosquitto_connect failed: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        close(can_sock);
        mosquitto_lib_cleanup();
        return 5;
    }

    // Run network loop in background thread
    mosquitto_loop_start(mosq);

    fprintf(stdout, "[MAIN] Running. Press Ctrl+C to exit.\n");
    while (g_running) {
        // Simple wait loop; all work is done in callbacks
        sleep(1);
    }

    fprintf(stdout, "[MAIN] Shutting down...\n");
    mosquitto_loop_stop(mosq, true);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if (can_sock >= 0) close(can_sock);
    return 0;
}
