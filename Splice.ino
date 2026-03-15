// Minimal ESP32/Arduino sketch for running embedded Splice bytecode.
// Place this file in the project root as `splice.ino` and open the folder in the Arduino IDE.

#define SPLICE_PLATFORM_ARDUINO 1

extern "C" {
#include "src/splice.h"
}

extern "C" void splice_embed_print(const char *s) {
    Serial.print(s ? s : "");
}

extern "C" void splice_embed_println(const char *s) {
    Serial.println(s ? s : "");
}

extern "C" void splice_embed_delay_ms(unsigned long ms) {
    delay(ms);
}

extern "C" int splice_embed_input_available(void) {
    return Serial.available();
}

extern "C" int splice_embed_input_read(void) {
    return Serial.read();
}

// SPC program:
//   push_const "Hello, ESP32!"
//   print
//   halt
static const unsigned char kHelloEsp32Program[] = {
    'S', 'P', 'C', 0x00,
    0x02,

    0x01, 0x00,
    0x01, 0x0D, 0x00, 0x00, 0x00,
    'H', 'e', 'l', 'l', 'o', ',', ' ', 'E', 'S', 'P', '3', '2', '!',

    0x00, 0x00,
    0x00, 0x00,

    0x05, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x15, 0x20
};

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting Splice on ESP32...");

    const int ok = splice_run_embedded_program(
        kHelloEsp32Program,
        sizeof(kHelloEsp32Program)
    );

    if (!ok) {
        Serial.println("Splice execution failed.");
        return;
    }

    Serial.println("Splice execution finished.");
}

void loop() {
    delay(1000);
}
