#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "ff.h"
#include "f_util.h"

#define PS1_SCK_PIN   10
#define PS1_CMD_PIN   11
#define PS1_DATA_PIN  12
#define PS1_CS_PIN   13
#define PS1_ACK_PIN   14

#define PS1_FRAME_SIZE 128

void ps1_mc_gpio_init(void){
    gpio_init(PS1_SCK_PIN);
    gpio_set_dir(PS1_SCK_PIN, GPIO_OUT);
    gpio_put(PS1_SCK_PIN, 1);

    gpio_init(PS1_CMD_PIN);
    gpio_set_dir(PS1_CMD_PIN, GPIO_OUT);
    gpio_put(PS1_CMD_PIN, 1);

    gpio_init(PS1_CS_PIN);
    gpio_set_dir(PS1_CS_PIN, GPIO_OUT);
    gpio_put(PS1_CS_PIN, 1);

    gpio_init(PS1_DATA_PIN);
    gpio_set_dir(PS1_DATA_PIN, GPIO_IN);
    gpio_pull_up(PS1_DATA_PIN);

    gpio_init(PS1_ACK_PIN);
    gpio_set_dir(PS1_ACK_PIN, GPIO_IN);
    gpio_pull_up(PS1_ACK_PIN);
}

uint8_t ps1_transfer_byte(uint8_t out){
    uint8_t in = 0;

    for (int bit = 0; bit < 8; bit++){
        //PS1 send LSB first 
        gpio_put(PS1_CMD_PIN, (out >> bit) & 1u);
        sleep_us(2);

        //The clock's slope
        gpio_put(PS1_SCK_PIN, 0);
        sleep_us(2);

        //Reading a bit from a DAT
        if (gpio_get(PS1_DATA_PIN)){
            in |= (1u << bit);
        }

        gpio_put(PS1_SCK_PIN, 1);
        sleep_us(2);
    }

    return in;
}

bool ps1_wait_ack(uint32_t timeout_us){
    absolute_time_t deadline = make_timeout_time_us(timeout_us);

    while (!time_reached(deadline)) {
        if (gpio_get(PS1_ACK_PIN) == 0) {
            while (gpio_get(PS1_ACK_PIN) == 0) {
                if (time_reached(deadline)) {
                    return false;
                }
                tight_loop_contents();
            }
            return true;
        }
        tight_loop_contents();
    }

    return false;
}


uint8_t ps1_transfer_byte_ack(uint8_t out, const char *label) {
    uint8_t in = ps1_transfer_byte(out);

    bool ack = ps1_wait_ack(100);
    if (!ack) {
        printf("No ACK after %s, sent 0x%02X, got 0x%02X\n", label, out, in);
    }

    return in;
}

uint8_t ps1_frame_checksum(uint16_t frame_addr, const uint8_t data[128]){
    uint8_t c = 0;
    uint8_t addr_lsb = frame_addr & 0xFF;
    uint8_t addr_msb = (frame_addr >> 8) & 0xFF;

    c ^= addr_lsb;
    c ^= addr_msb;

    for (int i = 0; i<128; i++){
        c ^= data[i];
    }

    return c;
}

bool ps1_mc_read_frame(uint16_t frame_addr, uint8_t out[128]){
    uint8_t addr_lsb = frame_addr & 0xFF;
    uint8_t addr_msb = (frame_addr >> 8) & 0xFF;

    uint8_t rx;
    uint8_t marker;
    uint8_t card_addr_lsb;
    uint8_t card_addr_msb;
    uint8_t received_checksum;
    uint8_t end_flag;

    gpio_put(PS1_CS_PIN, 0);
    sleep_us(20);

    //
    rx = ps1_transfer_byte_ack(0x81, "0x81 access");
    printf("RX after 0x81: 0x%02X\n", rx);

    rx = ps1_transfer_byte_ack(0x52, "0x52 read");
    printf("RX after 0x52: 0x%02X\n", rx);

    //Headers 
    rx = ps1_transfer_byte_ack(0x00, "header 0");
    printf("RX header 0: 0x%02X\n", rx);

    rx = ps1_transfer_byte_ack(0x00, "header 1");
    printf("RX header 1: 0x%02X\n", rx);

    //MSB then LSB
    rx = ps1_transfer_byte_ack(addr_msb, "addr MSB");
    printf("RX addr MSB phase: 0x%02X\n", rx);

    rx = ps1_transfer_byte_ack(addr_lsb, "addr LSB");
    printf("RX addr LSB phase: 0x%02X\n", rx);
    
    //To command ACK: 0x5C
    marker = ps1_transfer_byte_ack(0x00, "command ack 0x5C");
    printf("Command ACK marker: 0x%02X\n", marker);

    //To start data flag: 0x5D
    marker = ps1_transfer_byte_ack(0x00, "start data 0x5D");
    printf("Start data marker: 0x%02X\n", marker);

    card_addr_msb = ps1_transfer_byte_ack(0x00, "card addr MSB");
    card_addr_lsb = ps1_transfer_byte_ack(0x00, "card addr LSB");

    printf("Card echoed addr: MSB=0x%02X LSB=0x%02X\n",
           card_addr_msb, card_addr_lsb);

    if (card_addr_msb != addr_msb || card_addr_lsb != addr_lsb) {
        printf("Address echo mismatch\n");
    }

    for (int i = 0; i < 128; i++) {
        out[i] = ps1_transfer_byte_ack(0x00, "data");
    }

    received_checksum = ps1_transfer_byte_ack(0x00, "checksum");
    end_flag = ps1_transfer_byte(0x00);

    gpio_put(PS1_CS_PIN, 1);
    gpio_put(PS1_CMD_PIN, 1);

    uint8_t calculated_checksum = ps1_frame_checksum(frame_addr, out);

    printf("Received checksum:   0x%02X\n", received_checksum);
    printf("Calculated checksum: 0x%02X\n", calculated_checksum);
    printf("End flag:            0x%02X\n", end_flag);

    if (received_checksum != calculated_checksum) {
        printf("Checksum mismatch\n");
        return false;
    }

    if (end_flag != 0x47) {
        printf("Unexpected end flag, expected 0x47\n");
        return false;
    }

    return true;

}

void dump_frame_hex(const uint8_t data[128]) {
    for (int i = 0; i < 128; i++) {
        if ((i % 16) == 0) {
            printf("\n%04X: ", i);
        }

        printf("%02X ", data[i]);
    }

    printf("\n");
}

void read_multi_frames_from_memory_card(void){
    stdio_init_all();
    sleep_ms(500);

    printf("\nPS1 memory card - multi frame read test\n");

    ps1_mc_gpio_init();

    uint8_t frame[128];

   for (uint16_t frame_no = 0; frame_no < 10; frame_no++) {
        printf("\nReading frame %u...\n", frame_no);

        if (ps1_mc_read_frame(frame_no, frame)) {
            printf("Frame %u OK\n", frame_no);
            dump_frame_hex(frame);
        } else {
            printf("Frame %u FAILED\n", frame_no);
        }

        sleep_ms(500);
    }   
}
   
int main(void){
    read_one_frame_from_memory_card();
    return 0;
}
