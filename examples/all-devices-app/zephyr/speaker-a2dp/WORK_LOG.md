# Work log: Matter and Bluetooth Classic audio on ESP32-S31 under Zephyr

Period: 2026-11-12 to 2026-11-13. Board: ESP32-S31-Function-CoreBoard-1
(ESP32-S31-WROOM-3, chip revision v0.0, 16 MB flash, 16 MB PSRAM).

The goal was to find out how far the draft ESP32-S31 Zephyr port is usable for
Matter, and then to verify Bluetooth Classic on the same firmware by receiving
audio after commissioning, as a speaker product would.

## Summary

1.  The Matter lighting app and all-devices app build for the ESP32-S31 on the
    draft pull requests with two board files each. BLE-Wi-Fi commissioning
    works on hardware.
2.  The draft port had no Bluetooth Classic support. The controller library and
    its glue existed at Espressif and were connected to the port.
3.  The all-devices app got a Matter Speaker that is a Bluetooth Classic A2DP
    sink. Pairing and audio streaming work next to a commissioned Matter node
    on Wi-Fi.
4.  Audio output needed an I2S devicetree node, an ES8311 driver and three
    driver fixes. I2S plays. The codec does not answer yet, so there is no
    sound from the board.
5.  To check the audio without the codec, the decoded audio is sent to a host
    over Wi-Fi and played there.

## Timeline

### 1. Survey of the upstream pull requests

-   Zephyr [#125547](https://github.com/zephyrproject-rtos/zephyr/pull/125547)
    (draft) adds the SoC `esp32s31` and the board
    `esp32s31_function_coreboard`. It pins `hal_espressif` to `pull/696/head`.
-   `hal_espressif`
    [#696](https://github.com/zephyrproject-rtos/hal_espressif/pull/696) adds
    the HAL sources and the binary blobs.
-   Present in the port: Wi-Fi, BLE, Ethernet, coexistence. Absent: 802.15.4
    (no Thread), Bluetooth Classic, I2S. PSRAM is declared but off.

### 2. Workspace

A separate west workspace `~/zephyr-s31-pr125547` with its own Python venv, the
Zephyr checkout at the pull request head `0beac8d8344f`, `hal_espressif` at
`c92bdba14303`, Zephyr SDK 1.0.1, esptool 5.6.0. Fixes needed during setup:

| Problem                                             | Fix                                                            |
| --------------------------------------------------- | -------------------------------------------------------------- |
| `esptool>=5.0.2 not found`                          | `west packages pip --install` in the venv                      |
| `No module named 'python_path'` during ZAP codegen  | `pip install python_path lark jinja2 click coloredlogs`        |

### 3. Matter lighting app (branch `esp32s31-zephyr-lighting`)

Two files under `examples/lighting-app/esp32_zephyr/boards/`:

-   `esp32s31_function_coreboard_esp32s31_hpcore.conf`: the ESP32-C6 values
    plus `CONFIG_ETH_DRIVER=n`, because the board enables Ethernet by default.
-   `esp32s31_function_coreboard_esp32s31_hpcore.overlay`: `led0` on GPIO2.

Result: 1.70 MB image, 85.8 % of internal SRAM. BLE-Wi-Fi commissioning with
chip-tool succeeded on hardware.

The first flash attempts failed with "No serial data received". esptool 5.6.0
does support the chip. The cause was on the board side (port and connection),
not in the tooling.

### 4. Matter all-devices app

`socs/esp32s31_hpcore.conf` and
`boards/espressif/esp32s31_function_coreboard.overlay`, on branch
`esp32s31-zephyr-pr125547`. Build tested.

### 5. Bluetooth Classic in the port

Investigation showed where each layer stood:

| Layer                                  | Classic support                                                         |
| -------------------------------------- | ----------------------------------------------------------------------- |
| Chip                                   | Yes (`SOC_BT_CLASSIC_SUPPORTED`)                                        |
| Espressif controller library           | Yes, `libbredr_app.a` in `esp32s31-bt-lib`, next to `libble_app.a`      |
| Controller glue in the HAL             | Yes, `porting_btdm/controller/bredr/bredr.c`, not built                 |
| Zephyr HCI driver                      | No, `ESP_BT_MODE_BLE` forced for the S31                                |
| Zephyr host                            | Yes (experimental): A2DP, AVDTP, SDP, SBC                               |

The changes that connect them are in sections "Zephyr changes" and "HAL
changes" below.

### 6. Speaker device with an A2DP sink

The all-devices app already has the Matter Speaker device type (0x0022, On/Off
as mute and Level Control as volume). On Zephyr it only logged. Added:

-   `ZephyrSpeaker`: starts the A2DP sink at `kCommissioningComplete`, or at
    `kServerReady` when the node already has a fabric. Maps mute and level to
    the sink volume.
-   `A2dpSink.c`: SDP record, SBC sink endpoint, stream callbacks, SBC
    decoding, I2S output, codec volume.

### 7. Audio output

-   Pin mapping from the Espressif schematic (`esp32-s31-function-coreboard-1-schematics.pdf`):

    | Signal                | GPIO |
    | --------------------- | ---- |
    | I2C SCL / SDA         | 50 / 51 |
    | I2S MCLK              | 52   |
    | I2S SCLK (BCLK)       | 53   |
    | I2S ASDOUT (codec out)| 54   |
    | I2S LRCK              | 55   |
    | I2S DSDIN (codec in)  | 56   |
    | PA_CTRL (NS4150B)     | 57   |

    The codec address pin CE is tied to ground (address 0x18). The I2C lines
    have 22 pF filter capacitors and no pull-up resistors on the board.

-   Added the `i2s0`/`i2s1` devicetree nodes and an ES8311 playback driver.

### 8. Audio tap to the host

`CONFIG_ALL_DEVICES_SPEAKER_AUDIO_UDP_TAP` sends every decoded frame as a UDP
datagram. `capture_song.py` writes a WAV file and plays it. First run on
hardware: 1166 frames in 3.4 s, 2 lost (0.2 %).

## Problems and fixes

Each row was seen on hardware unless marked otherwise.

| # | Symptom | Cause | Fix | Where |
| - | ------- | ----- | --- | ----- |
| 1 | Matter build fails: `mbedtls_ecp_muladd` not declared | `BT_CLASSIC` selects `BT_SMP`, which implies the p256-m driver. That driver removes the Mbed TLS ECP code that SPAKE2+ uses. | `CONFIG_MBEDTLS_PSA_P256M_DRIVER_ENABLED=n` | `overlay-speaker-a2dp.conf` |
| 2 | Link error: `tlsf_create_with_pool`, `tlsf_malloc`, `tlsf_free` | The BR/EDR glue uses the TLSF allocator of the ESP-IDF heap component, which the HAL does not build. | Three functions on a Zephyr `sys_heap` | HAL `zephyr/esp32s31/src/bt/bredr_mempool.c` |
| 3 | Crash at boot: `psa_generate_key failed: -134`, then `assert src/odm_main.c, 3036` | The controller creates a P-192 key pair at start-up and asserts on failure. TF-PSA-Crypto has no P-192 curve. | Build the glue against Espressif's tinycrypt (ECC, SHA-256, HMAC), software ECC | HAL `components/bt/common/tinycrypt`, `esp_bt_cfg.h` |
| 4 | `bt_hci_core: Non-BR/EDR controller detected` | The controller returns its BR/EDR LMP features together with the "BR/EDR Not Supported" bit (`bf 06 06 7e f8 1f 59 87`). The host refuses BR/EDR when the bit is set. | The HCI driver clears the bit in the Read Local Features reply | Zephyr `drivers/bluetooth/hci/hci_esp32.c` |
| 5 | `Failed to create BLE identity: -12` | With `BT_BONDABLE` (enabled through `BT_SMP`), Matter keeps identity 0 and creates identity 1 for advertising. `BT_ID_MAX` was 1. | `CONFIG_BT_ID_MAX=2` | `overlay-speaker-a2dp.conf` |
| 6 | RAM overflow by 8 KB, later by 12.6 KB | Internal SRAM is 504 KB. BT heap, Wi-Fi heap and the audio path fill it. | BT heap 80 KB instead of 96 KB, smaller BT/net buffers and main stack | Zephyr `Kconfig.esp32`, `overlay-speaker-a2dp.conf` |
| 7 | I2S accepts 14 frames, then every write fails with -12 | Zephyr passes DMA trigger 3 for I2S0. On the S31 I2S0 has four request lines (1 to 4) and standard mode uses the first, so the DMA never got a request. | Use `SOC_GDMA_TRIG_PERIPH_I2S0CH0` / `I2S1CH0` | Zephyr `drivers/i2s/i2s_esp32.c` |
| 8 | `TX queue empty: -35`, `Invalid state: 4`, repeated many times per second | The queue held 17 ms of audio and A2DP packets arrive about every 20 ms with jitter. An empty queue is an error in the driver. | Larger queue, later start, 40 ms empty-queue deferral, no debug logging on that path | `overlay-speaker-a2dp.conf`, `A2dpSink.c` |
| 9 | GPIO50/51 read 0 in every configuration, bit-banged I2C returns chip ID 0x00 after 4.4 s | The GPIO driver accesses `in1`/`out1` through the devicetree address of `gpio1` (base + 0x800). On the S31 nothing is mapped there. GPIO32 to GPIO61 always read 0 and port writes do nothing. | Use the GPIO base for both ports | Zephyr `drivers/gpio/gpio_esp32.c` |
| 10 | Phone does not list "Matter Speaker", a short scan from a Mac misses it | The inquiry response carried no name, and the controller listens with the default scan timing (11.25 ms every 1.28 s) while Wi-Fi shares the radio. | Name and Audio Sink UUID in the EIR. Interlaced inquiry and page scan, 22.5 ms every 640 ms. | `A2dpSink.c` |
| 11 | Phone reconnects, link drops after 2 s | The board keeps no link key across a reset, the phone still has one. | Not fixed. Remove the device on the phone and pair again. | - |
| 12 | Capture script recorded 3.4 s of silence | Phones open the stream for a few seconds after connecting. | The script waits for the first frame that is not silence | `capture_song.py` |

Problem 4 was first treated by enabling `CONFIG_BT_CTRL_MULTI_LINK_ENABLED`,
which ESP-IDF sets for the dual-mode controller. That did not change the
feature bit. The setting is kept because it matches ESP-IDF.

Problem 9 made two earlier diagnostics invalid: the bit-banged I2C result and
the first pin level log were measuring the driver bug.

## Zephyr changes

Local branch `s31-classic-bt` in `~/zephyr-s31-pr125547/zephyr`, on top of
`0beac8d8344f`. Saved as `patches/zephyr/`.

| Commit         | Subject                                                        | Content |
| -------------- | -------------------------------------------------------------- | ------- |
| `886338cffc5`  | drivers: bluetooth: esp32: enable br/edr on esp32-s31          | Dual mode in `hci_esp32.c`, BT heap default |
| `9d8d7cd615c`  | esp32s31: add i2s nodes and the es8311 audio codec             | `i2s0`/`i2s1` nodes, `drivers/audio/es8311.c`, binding, board codec node and pins |
| `07ccb85859b`  | esp32s31: probe the es8311 at configure, drop br/edr psa selects | Chip ID check moved to configure, I2C address scan on failure |
| `b5c28986424`  | drivers: bluetooth: esp32: clear the no-br/edr bit on esp32-s31 | Workaround for problem 4, LMP feature dump |
| `e2100d156c6`  | drivers: i2s: esp32: use the esp32-s31 dma request line        | Fix for problem 7 |
| `e339708e746`  | drivers: gpio: esp32: use the gpio base for port 1 registers   | Fix for problem 9 |

## HAL changes

Local branch `s31-classic-bt` in `modules/hal/espressif`, on top of
`c92bdba14303`. Saved as `patches/hal_espressif/`.

| Commit       | Subject                                              | Content |
| ------------ | ---------------------------------------------------- | ------- |
| `23d543841`  | feat(esp32s31): enable br/edr in the bt controller   | `libbredr_app.a` blob entry, `bredr.c` in the build, dual mode and BR/EDR options in `esp_bt_cfg.h`, `bredr_mempool.c` |
| `f6428ff4e`  | fix(esp32s31): use tinycrypt for br/edr pairing keys | tinycrypt sources from ESP-IDF, software ECC |
| `8afa2fab3`  | fix(esp32s31): enable multi-link in dual mode        | `CONFIG_BT_CTRL_MULTI_LINK_ENABLED` |

## Matter changes

Branch `esp32s31-zephyr-speaker-a2dp`, on top of `esp32s31-zephyr-pr125547`.
See the file tables in `README.md`.

## Evidence from hardware

Commissioning with the dual-mode controller, then the sink starting:

```
chip: [ZCL]Commissioning complete, notify platform driver to persist network credentials.
a2dp_sink: A2DP sink discoverable as "Matter Speaker"
```

Streaming from a Mac for two minutes, Matter node on Wi-Fi:

```
a2dp_sink: A2DP connected
a2dp_sink: A2DP stream configured: 44100 Hz, 2 channel(s)
a2dp_sink: A2DP audio: 193 packets, 144364 bytes in 5002 ms, volume 100
a2dp_sink: A2DP playback: 1671 frames played, 66 dropped (last error -35), 0 output restarts, codec failed
```

Audio tap on the host:

```
Receiving from 192.168.1.6: 44100 Hz, mono, 16-bit
Saved matter_speaker_capture.wav: 3.4 s, 1166 frames received, 2 lost (0.2%)
```

## Open issues

1.  **ES8311 does not acknowledge.** `Chip ID read at 0x18 failed: -5` with the
    I2C0 controller and, after the GPIO fix, with the bit-banged bus. Wiring,
    address and supply match the schematic. Next steps: read the pin level log
    after the GPIO fix, check whether MCLK reaches GPIO52, measure the codec
    supply.
2.  **About 4 % of I2S frames dropped.** Bursty packet arrival against a short
    queue. A long jitter buffer needs PSRAM.
3.  **Internal RAM.** 96.7 % static in the last build. At 98.6 % the boot log
    showed `libc heap size 6 kB`, which is all the Matter heap had.
4.  **No stored pairing keys** (`CONFIG_BT_SETTINGS` is off).
5.  **Not tested:** Matter mute and volume during playback, sound through the
    amplifier, behaviour when the phone and the commissioner are active at the
    same time.
6.  **Diagnostic code to remove:** I2C address scan (`es8311.c`), LMP feature
    dump (`hci_core.c`), pin level log (`A2dpSink.c`).

## PSRAM plan (analysis only, nothing implemented)

| Memory                             | Size      | Place      | Reason |
| ---------------------------------- | --------- | ---------- | ------ |
| Bluetooth controller heap          | 80 KB     | Internal   | Timing critical, the port offers only the system heap |
| Wi-Fi heap                         | 51 KB     | PSRAM      | `ESP_WIFI_HEAP_SPIRAM` exists, Matter traffic is light |
| Audio jitter buffer                | 300 ms+   | PSRAM      | Removes the dropped frames |
| I2S queue                          | 8 blocks  | Internal   | The I2S driver needs DMA-capable memory |
| Stacks, SBC decoder, Matter pools  | -         | Internal   | Accessed all the time |

PSRAM shares the bus with the flash. Access stops while flash is written, so
the internal I2S queue must cover that time. Order: bring up PSRAM alone with
its memory test, move the Wi-Fi heap, add the jitter buffer, restore the
trimmed buffers.

## Items worth reporting to the pull request authors

-   GPIO port 1 register base (problem 9). It breaks every GPIO above 31 on the
    S31, including the BOOT button of the board.
-   I2S DMA trigger for the S31 (problem 7).
-   The "BR/EDR Not Supported" LMP feature bit of the dual-mode controller
    (problem 4).
-   P-192 for BR/EDR Secure Simple Pairing without tinycrypt (problem 3).
