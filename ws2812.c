/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/spi.h>
#include <time.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"


#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
#define UART_ID uart0
#define BAUD_RATE 115200
int dma_chan_face_left;
int dma_chan_face_right;

typedef struct {
    uint8_t w;
    uint8_t b;
    uint8_t r;
    uint8_t g; // ignored for normal rgb use
} RGBW;

#define FBLEN 64*7
//Double buffering
RGBW framebuffer_1_face_left[FBLEN];
RGBW framebuffer_2_face_left[FBLEN];
volatile uint8_t fb_idx_left = 0;

RGBW framebuffer_1_face_right[FBLEN];
RGBW framebuffer_2_face_right[FBLEN];
volatile uint8_t fb_idx_right = 0;

volatile uint32_t cycles = 0;
void fill_random(RGBW fb[FBLEN], size_t length) {

    cycles += 1;
    for (uint i = 0; i < length; i++) {

        fb[i].r = 0;
        fb[i].g = 0;
        fb[i].b = 0;
        fb[i].w = 0;

        if (cycles % 3 == 0){
            fb[i].r = ((rand() % 255)) / 32;
            //puts("red");
        }

        if ((cycles + 1) % 3 == 0){
            fb[i].g = ((rand() % 255)) / 32;
            //puts("green");
        }

        if ((cycles + 2) % 3 == 0){
            fb[i].b = ((rand() % 255)) / 32;
            //puts("blue");
        }

    }

//    puts("filled random");
//    printf("first: %d, %d, %d\n", fb[0].r, fb[0].g, fb[0].b);
//    printf("cycles: %d\n", cycles);

}

uint16_t wheelpos = 0;
RGBW wheel(uint16_t WheelPos) {
    int scaling = 1;
    RGBW ret;
    ret.w = 0;
    if(WheelPos < 1365) {
        ret.r = (3*WheelPos) / scaling;
        ret.g = (4095 - 3*WheelPos) / scaling;
        ret.b = 0;
    } else if(WheelPos < 2731) {
        WheelPos -= 1365;
        ret.r = (4095 - 3*WheelPos) / scaling;
        ret.g = 0;
        ret.b = (3*WheelPos) / scaling;
    } else {
        WheelPos -= 2731;
        ret.r = 0;
        ret.g = (3*WheelPos) / scaling;
        ret.b = (4095 - 3*WheelPos) / scaling;
    }
    return ret;
}

void fill_wheel(RGBW fb[], size_t length) {
    //uint ms_since_boot = to_ms_since_boot(get_absolute_time()) / 100;
    for (uint i = 0; i < length; i++) {
        RGBW values = wheel(wheelpos);
//        fb[i].r = values.r / 16;
//        fb[i].g = values.g / 16;
//        fb[i].b = values.b / 16;
//        fb[i].r = 0;
////        fb[i].g = 0;
//        fb[i].b = 0;
        double pos = ((double)i / length) * 2 * M_PI;
        pos += (double)wheelpos / 3;
        if (rand() % 12 == 0) {
            fb[i].r = (uint8_t)(255.0 * ((sin(pos) + 1) / 2)) / 4;
            fb[i].g = (uint8_t)(255.0 * ((sin(pos + 0.33 * M_PI * 2) + 1) / 2)) / 4;
            fb[i].b = (uint8_t)(255.0 * ((sin(pos + 0.67 * M_PI * 2) + 1) / 2)) / 4;
        } else {
                    fb[i].r = 0;
                    fb[i].g = 0;
                    fb[i].b = 0;
        }

    }
}


const int PIN_TX_LEFT = 10;
const int PIN_TX_RIGHT = 11;

uint last_time_measurement;
int64_t start_dma_transfer_left(alarm_id_t id, void *user_data) {
    //puts("Alarm fired");
    uint us_now = to_us_since_boot(get_absolute_time());
    uint deltatime = us_now - last_time_measurement;
    last_time_measurement = us_now;
    dma_channel_start(dma_chan_face_left);
    //printf("us since last update: %d \n", deltatime);
    return 0;
}
int64_t start_dma_transfer_right(alarm_id_t id, void *user_data) {
    dma_channel_start(dma_chan_face_right);
    return 0;
}

void dma_handler_left() {
    // Clear the interrupt request.
    dma_hw->ints0 = 1u << dma_chan_face_left;
    // Give the channel a new adress to read from, and re-trigger it
    if (fb_idx_left == 0) {
        dma_channel_set_read_addr(dma_chan_face_left, framebuffer_1_face_left, false);
    } else {
        dma_channel_set_read_addr(dma_chan_face_left, framebuffer_2_face_left, false);
    }
    add_alarm_in_us(1000, start_dma_transfer_left, NULL, false);
    //puts("DMA RETRIG");
}
void dma_handler_right() {
    // Clear the interrupt request.
    dma_hw->ints0 = 1u << dma_chan_face_right;
    // Give the channel a new adress to read from, and re-trigger it

    if (fb_idx_right == 0) {
        dma_channel_set_read_addr(dma_chan_face_right, framebuffer_1_face_right, false);
    } else {
        dma_channel_set_read_addr(dma_chan_face_right, framebuffer_2_face_right, false);
    }

    add_alarm_in_us(1000, start_dma_transfer_right, NULL, false);
}

int main() {
    //set_sys_clock_48();
    stdio_init_all();

    srand(time(0));

    puts("WS2812 Smoke Test\n");

    // todo get free sm
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, 0, offset, PIN_TX_LEFT, 800000, false);
    ws2812_program_init(pio, 1, offset, PIN_TX_RIGHT, 800000, false);
    puts("Inited program");

    // Setup dma

    // ---- LEFT ----
    // Configure a channel to write the same word (32 bits) repeatedly to PIO0
    // SM0's TX FIFO, paced by the data request signal from that peripheral.
    dma_chan_face_left = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan_face_left);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_PIO0_TX0);


    dma_channel_configure(
            dma_chan_face_left,
            &c,
            &pio->txf[0], // Write address (only need to set this once)
            NULL,             // Don't provide a read address yet
            FBLEN,            // Write the whole framebuffer_1_face_left, then interrupt
            false             // Don't start yet
            );

    // Tell the DMA to raise IRQ line 0 when the channel finishes a block
    dma_channel_set_irq0_enabled(dma_chan_face_left, true);

    // Configure the processor to run dma_handler_left() when DMA IRQ 0 is asserted
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler_left);
    irq_set_enabled(DMA_IRQ_0, true);

    fill_random(framebuffer_1_face_left, FBLEN);
    dma_handler_left();

    // ---- RIGHT ----
    // Configure a channel to write the same word (32 bits) repeatedly to PIO0
    // SM0's TX FIFO, paced by the data request signal from that peripheral.
    dma_chan_face_right = dma_claim_unused_channel(true);
    dma_channel_config c_right = dma_channel_get_default_config(dma_chan_face_right);
    channel_config_set_transfer_data_size(&c_right, DMA_SIZE_32);
    channel_config_set_read_increment(&c_right, true);
    channel_config_set_dreq(&c_right, DREQ_PIO0_TX1);


    dma_channel_configure(
            dma_chan_face_right,
            &c_right,
            &pio->txf[1], // Write address (only need to set this once)
            NULL,             // Don't provide a read address yet
            FBLEN,            // Write the whole framebuffer_1_face_left, then interrupt
            false             // Don't start yet
            );

    // Tell the DMA to raise IRQ line 0 when the channel finishes a block
    dma_channel_set_irq1_enabled(dma_chan_face_right, true);

    // Configure the processor to run dma_handler_right() when DMA IRQ 1 is asserted
    irq_set_exclusive_handler(DMA_IRQ_1, dma_handler_right);
    irq_set_enabled(DMA_IRQ_1, true);

    fill_random(framebuffer_1_face_right, FBLEN);
    dma_handler_right();

    // --- SETUP SPI SLAVE -----
    // Enable SPI 0 at 1 MHz and connect to GPIOs
    uint spi_hz = spi_init(spi0, 1000*1000);// * 1000);
    spi_set_slave(spi0, true);
    spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_CSN_PIN, GPIO_FUNC_SPI);
    printf("SPI baud rate: %d Hz\n", spi_hz);


    for (uint32_t i = 0; i < 60; i++) {

        uint64_t start_time = to_us_since_boot(get_absolute_time());
        // Rainbow bs
        if (fb_idx_right == 0) {
            fill_wheel(framebuffer_2_face_right, FBLEN);
            fb_idx_right = 1;
        } else {
            fill_wheel(framebuffer_1_face_right, FBLEN);
            fb_idx_right = 0;
        }

        if (fb_idx_left == 0) {
            fill_wheel(framebuffer_2_face_left, FBLEN);
            fb_idx_left = 1;
        } else {
            fill_wheel(framebuffer_1_face_left, FBLEN);
            fb_idx_left = 0;
        }
        sleep_ms(8);
        wheelpos++;

        uint64_t end_time = to_us_since_boot(get_absolute_time());
        int d = end_time - start_time;
        //printf("loop took %d us \n", d);

    }

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

#define PACKET_BEGIN_PIN 20
    gpio_init(PACKET_BEGIN_PIN);
    gpio_set_dir(PACKET_BEGIN_PIN, GPIO_IN);

    while (1) {
        gpio_put(PICO_DEFAULT_LED_PIN, 1);



        puts("Waiting for spi...");
//        while (!gpio_get(PACKET_BEGIN_PIN)){}
        // Read left
        if (fb_idx_left == 0) {
            spi_read_blocking(spi0, 0b00110011, (uint8_t*)framebuffer_2_face_left, FBLEN * sizeof(RGBW));
            fb_idx_left = 1;
        } else {
            spi_read_blocking(spi0, 0b00110011, (uint8_t*)framebuffer_1_face_left, FBLEN * sizeof(RGBW));
            fb_idx_left = 0;
        }
        // Read right
        if (fb_idx_right == 0) {
            spi_read_blocking(spi0, 0b00110011, (uint8_t*)framebuffer_2_face_right, FBLEN * sizeof(RGBW));
            fb_idx_right = 1;
        } else {
            spi_read_blocking(spi0, 0b00110011, (uint8_t*)framebuffer_1_face_right, FBLEN * sizeof(RGBW));
            fb_idx_right = 0;
        }
        gpio_put(PICO_DEFAULT_LED_PIN, 0);

        // Rainbow bs
//        if (fb_idx_right == 0) {
//            fill_wheel(framebuffer_2_face_right, FBLEN);
//            fb_idx_right = 1;
//        } else {
//            fill_wheel(framebuffer_1_face_right, FBLEN);
//            fb_idx_right = 0;
//        }
//
//        if (fb_idx_left == 0) {
//            fill_wheel(framebuffer_2_face_left, FBLEN);
//            fb_idx_left = 1;
//        } else {
//            fill_wheel(framebuffer_1_face_left, FBLEN);
//            fb_idx_left = 0;
//        }
//
//        sleep_ms(13);
//        wheelpos++;
    }
}
#pragma clang diagnostic pop