/*
 * main.c - ESP32 firmware: wire → parser → dispatcher → wire
 *
 * WHAT:    Starts the wire (USB), then runs one task forever: read bytes,
 *          feed them to the parser one by one, and when a complete good
 *          packet comes out, hand it to the dispatcher and send its reply.
 * WHY:     This is the only glue. All the decisions live in the tested
 *          protocol code (components/proto); this file just connects it
 *          to the hardware.
 * ANALOGY: A post office counter: letters come in through the slot (wire),
 *          the clerk checks each one (parser), the manager decides the
 *          answer (dispatcher), and the reply goes back out the slot.
 *
 * INTERVIEW LINE:
 *   "One FreeRTOS task blocks on the USB driver with a 20 ms timeout,
 *    feeds each byte to the state-machine parser, and answers every good
 *    packet through the dispatcher. Fixed buffers, no malloc."
 */
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "frame.h"
#include "parser.h"
#include "dispatch.h"
#include "transport.h"

#define PROTO_TASK_STACK  4096   /* bytes (ESP-IDF counts task stacks in bytes) */
#define PROTO_TASK_PRIO   5
#define READ_TIMEOUT_MS   20     /* wake up at least this often */
#define WRITE_TIMEOUT_MS  100

static const char *TAG = "main";

/* Protocol state lives here, for the life of the program (not on the stack). */
static parser_t   s_parser;
static dispatch_t s_dispatch;

/* Milliseconds since boot. Wraps after ~49 days; the parser's timeout math handles that. */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void proto_task(void *arg)
{
    (void)arg;
    uint8_t rx[64];                       /* bytes from the wire, a chunk at a time */
    uint8_t reply[PROTO_MAX_FRAME];       /* the reply being sent */
    frame_t frame;                        /* a finished packet from the parser */

    for (;;) {
        /* Wait for bytes (or 20 ms). While waiting, this task uses no CPU. */
        int n = transport_read(rx, sizeof rx, READ_TIMEOUT_MS);

        for (int i = 0; i < n; i++) {
            /* One byte at a time into the state machine (Lesson 3). */
            if (parser_feed(&s_parser, rx[i], now_ms(), &frame) == PARSE_OK) {
                size_t len = dispatch_handle(&s_dispatch, &frame, &s_parser.stats,
                                             now_ms(), reply, sizeof reply);
                if (len > 0) {
                    transport_write(reply, len, WRITE_TIMEOUT_MS);
                }
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "serial command framework, protocol v%d", PROTO_VERSION);

    parser_init(&s_parser);
    dispatch_init(&s_dispatch);

    /*
     * From here the USB port carries binary packets. A log line printed
     * while a reply is going out would land inside that packet, so logging
     * goes quiet. (The laptop skips the boot text above: plain text never
     * contains the 0xAA sync byte.)
     */
    ESP_LOGI(TAG, "ready on the USB port; logs off from here");
    esp_log_level_set("*", ESP_LOG_NONE);

    ESP_ERROR_CHECK(transport_init());
    xTaskCreate(proto_task, "proto", PROTO_TASK_STACK, NULL, PROTO_TASK_PRIO, NULL);
}
