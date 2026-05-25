/* Minimal DNS server that resolves all queries to the AP IP (192.168.4.1).
   Used for captive portal: any DNS lookup → device IP → browser opens config page. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "dns_server.h"

static const char *TAG = "dns_srv";
static TaskHandle_t s_task = NULL;
static int s_sock = -1;

#define DNS_PORT 53
#define AP_IP    0xC0A80401  // 192.168.4.1 in network byte order (big endian)

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static void dns_task(void *arg)
{
    uint8_t buf[512];
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);

    while (1) {
        int len = recvfrom(s_sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&src, &srclen);
        if (len < (int)sizeof(dns_header_t)) continue;

        dns_header_t *hdr = (dns_header_t *)buf;
        // Build reply: copy question section, set QR=1, AA=1, answer with A record
        hdr->flags   = htons(0x8400); // QR=1, AA=1, RCODE=0
        hdr->ancount = htons(1);

        // Find end of question section (skip QNAME + QTYPE + QCLASS)
        int pos = sizeof(dns_header_t);
        while (pos < len && buf[pos] != 0) {
            pos += buf[pos] + 1;
        }
        pos += 1 + 4; // null label + QTYPE(2) + QCLASS(2)

        if (pos + 16 < (int)sizeof(buf)) {
            // Append answer: pointer to question name + A record
            buf[pos++] = 0xC0;
            buf[pos++] = sizeof(dns_header_t); // pointer to start of question
            buf[pos++] = 0x00; buf[pos++] = 0x01; // TYPE A
            buf[pos++] = 0x00; buf[pos++] = 0x01; // CLASS IN
            buf[pos++] = 0x00; buf[pos++] = 0x00;
            buf[pos++] = 0x00; buf[pos++] = 0x3C; // TTL = 60s
            buf[pos++] = 0x00; buf[pos++] = 0x04; // RDLENGTH = 4
            // 192.168.4.1
            buf[pos++] = 192;
            buf[pos++] = 168;
            buf[pos++] = 4;
            buf[pos++] = 1;
        }

        sendto(s_sock, buf, pos, 0, (struct sockaddr *)&src, srclen);
    }
    vTaskDelete(NULL);
}

void dns_server_start(void)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket failed");
        return;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind failed");
        close(s_sock);
        s_sock = -1;
        return;
    }

    xTaskCreate(dns_task, "dns_srv", 4096, NULL, 5, &s_task);
    ESP_LOGI(TAG, "DNS server started");
}

void dns_server_stop(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}
