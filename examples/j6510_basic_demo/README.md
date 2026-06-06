# j6510 BASIC Demo

This Arduino-compatible demo runs OSI Microsoft BASIC for 6502 through the
`j6510` emulator on an ESP32-S2 Saola board.

Build and upload with PlatformIO:

```sh
~/.platformio/penv/bin/pio run -e esp32s2-saola-basic-demo
~/.platformio/penv/bin/pio run -e esp32s2-saola-basic-demo -t upload
~/.platformio/penv/bin/pio device monitor -p /dev/cu.usbserial-210 -b 115200
```

The demo injects two initial carriage returns so OSI BASIC accepts the default
memory size and terminal width. After the BASIC prompt appears, try:

```basic
PRINT 2+2
POKE 53248,1
POKE 53248,0
```

`POKE 53248,1` turns the Saola onboard WS2812 RGB LED on. `POKE 53248,0` turns
it off.

This is not a full C64 or PET machine. It only provides enough monitor I/O and
one memory-mapped LED register for an interactive serial BASIC demo.
