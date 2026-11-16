# Matter Speaker with a Bluetooth Classic A2DP sink on ESP32-S31 (Zephyr)

This folder holds everything needed to rebuild and test the all-devices app as a
Matter Speaker that receives audio over Bluetooth Classic (A2DP sink) on the
ESP32-S31-Function-CoreBoard-1 under Zephyr.

The work is experimental. It depends on two draft pull requests and on local
changes to Zephyr and `hal_espressif` that are not upstream.

| Document                                         | Content                                                |
| ------------------------------------------------ | ------------------------------------------------------ |
| `README.md` (this file)                          | What works, how to build, flash and test               |
| [`WORK_LOG.md`](WORK_LOG.md)                     | Everything that was done, each problem and its fix     |
| [`BLUETOOTH_CONCEPTS.md`](BLUETOOTH_CONCEPTS.md) | Background on BLE, the controller, Classic and A2DP    |

## Status

Tested on hardware on 2026-11-13.

| Function                                                        | Status                                |
| --------------------------------------------------------------- | ------------------------------------- |
| Matter commissioning over BLE and Wi-Fi, controller in dual mode | Works                                 |
| Classic discovery, pairing and A2DP connection (phone and Mac)   | Works                                 |
| SBC audio received and decoded on the board                      | Works                                 |
| Decoded audio sent to a host over Wi-Fi and played there         | Works (0.2 % of frames lost in a test) |
| I2S output                                                       | Runs, about 4 % of frames dropped     |
| ES8311 codec                                                     | Does not answer on I2C. Open.         |
| Sound from the board's speaker port                              | Not working, blocked by the codec     |
| Matter On/Off and Level Control during playback                  | Wired up, not tested on hardware      |
| Pairing keys kept across a reboot                                | Not implemented                       |

## Folder content

| File                         | Purpose                                                                                |
| ---------------------------- | -------------------------------------------------------------------------------------- |
| `capture_song.py`            | Host script: receives the audio tap, saves a WAV file, plays it                        |
| `audio-tap.conf`             | Kconfig fragment that enables the UDP audio tap                                        |
| `es8311-i2c-bitbang.overlay` | Diagnostic devicetree overlay: codec on a bit-banged I2C bus, enables the pin level log |
| `patches/zephyr/`            | The six Zephyr commits on top of zephyr#125547                                         |
| `patches/hal_espressif/`     | The three HAL commits on top of hal_espressif#696                                      |

The application code is in the parent folder:

| File                                                              | Purpose                                                       |
| ----------------------------------------------------------------- | ------------------------------------------------------------- |
| `../src/devices/ZephyrSpeaker.cpp`, `../include/devices/ZephyrSpeaker.h` | Matter Speaker device. Starts the sink once the node is on a fabric. |
| `../src/devices/A2dpSink.c`, `../include/devices/A2dpSink.h`      | A2DP sink, SBC decoding, I2S playback, codec volume, audio tap |
| `../overlay-speaker-a2dp.conf`                                    | Selects the Speaker and enables Classic, A2DP and audio        |
| `../Kconfig`                                                      | `ALL_DEVICES_SPEAKER_A2DP_SINK`, `ALL_DEVICES_SPEAKER_AUDIO_UDP_TAP` |
| `../boards/espressif/esp32s31_function_coreboard.overlay`         | Enables I2S0 and the DMA controller                            |

## Versions used

| Component        | Base                                                                  | Base commit    | Local commits on top |
| ---------------- | --------------------------------------------------------------------- | -------------- | -------------------- |
| `zephyr`         | [#125547](https://github.com/zephyrproject-rtos/zephyr/pull/125547)   | `0beac8d8344f` | 6 (`patches/zephyr`) |
| `hal_espressif`  | [#696](https://github.com/zephyrproject-rtos/hal_espressif/pull/696)  | `c92bdba14303` | 3 (`patches/hal_espressif`) |
| Toolchain        | Zephyr SDK 1.0.1 (`riscv64-zephyr-elf`)                               |                |                      |
| Flasher          | esptool 5.6.0                                                         |                |                      |

## Setting up from scratch

Prerequisites:

-   A Matter build environment, see `docs/guides/BUILDING.md`.
-   Zephyr SDK 1.0.1 or newer with the `riscv64-zephyr-elf` toolchain.
-   Python 3.12 or newer for the Zephyr venv.

The steps were checked on 2026-11-16: both pull requests were still at the
commits below, and the patches applied to clean checkouts of those commits
with `git am` and gave the same source trees the tested image was built from.
A complete run of these steps on a new machine was not done.

### 1. Get this branch

In an existing `connectedhomeip` checkout:

```shell
export CHIP=~/esp/connectedhomeip
cd "$CHIP"
git fetch https://github.com/shripad621git/connectedhomeip esp32s31-zephyr-speaker-a2dp
git checkout -b esp32s31-zephyr-speaker-a2dp FETCH_HEAD
export PATCHES="$CHIP/examples/all-devices-app/zephyr/speaker-a2dp/patches"
```

### 2. Create the Zephyr workspace at the pull request commits

```shell
export ZEPHYR_WORKSPACE=~/zephyr-s31-pr125547

mkdir -p "$ZEPHYR_WORKSPACE" && cd "$ZEPHYR_WORKSPACE"
python3 -m venv .venv
.venv/bin/pip install west esptool
source .venv/bin/activate

west init -m https://github.com/zephyrproject-rtos/zephyr .
git -C zephyr fetch origin pull/125547/head
git -C zephyr checkout -b s31-classic-bt 0beac8d8344f
west update
git -C modules/hal/espressif checkout -b s31-classic-bt c92bdba14303
```

`west update` fetches every Zephyr module, which takes a while and several GB.
To fetch only what this build needs, run this before `west update`:

```shell
west config manifest.project-filter -- "-.*,+hal_espressif,+mbedtls,+tf-psa-crypto,+mcuboot,+openthread,+hostap,+picolibc,+zcbor,+cmsis_6,+libsbc"
```

If a pull request has been updated since, its old commit may not come with
`pull/<n>/head`. Fetch it by its full hash, for example
`git -C zephyr fetch origin 0beac8d8344f45757553d4fd7130652cf09e2e35`.

### 3. Apply the patches

```shell
git -C zephyr am "$PATCHES"/zephyr/*.patch
git -C modules/hal/espressif am "$PATCHES"/hal_espressif/*.patch
```

The second command prints whitespace warnings for the tinycrypt sources. They
are warnings only. Each repository must end with the patches on top:

```shell
git -C zephyr log --oneline -1                 # drivers: gpio: esp32: use the gpio base for port 1 registers
git -C modules/hal/espressif log --oneline -1  # fix(esp32s31): enable multi-link in dual mode
```

### 4. Install the Python packages and fetch the blobs

```shell
pip install -r zephyr/scripts/requirements.txt
pip install -r "$CHIP/scripts/setup/requirements.build.txt" \
            -r "$CHIP/scripts/setup/requirements.zephyr.txt"
# After the HAL patches: also fetches libbredr_app.a, the BR/EDR controller library
west blobs fetch hal_espressif
```

Do not run `west update` again afterwards. The manifest of the pull request
pins `hal_espressif` to `pull/696/head`, and the update would move the HAL off
the `s31-classic-bt` branch.

## Building

In every new terminal, activate the Matter environment first and then the
Zephyr venv:

```shell
export CHIP=~/esp/connectedhomeip ZEPHYR_WORKSPACE=~/zephyr-s31-pr125547
cd "$CHIP"
source scripts/activate.sh
source "$ZEPHYR_WORKSPACE/.venv/bin/activate"
source "$ZEPHYR_WORKSPACE/zephyr/zephyr-env.sh"
export ZEPHYR_EXTRA_MODULES="$PWD" ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-1.0.1

# Speaker with A2DP sink
west build -p always -b esp32s31_function_coreboard/esp32s31/hpcore \
    -d out/esp32-zephyr-s31-speaker-a2dp examples/all-devices-app/zephyr \
    -- -DEXTRA_CONF_FILE=overlay-speaker-a2dp.conf
```

To also send the audio to a host, set the host address in `audio-tap.conf` and
build with both fragments:

```shell
west build -p always -b esp32s31_function_coreboard/esp32s31/hpcore \
    -d out/esp32-zephyr-s31-speaker-a2dp examples/all-devices-app/zephyr \
    -- "-DEXTRA_CONF_FILE=overlay-speaker-a2dp.conf;speaker-a2dp/audio-tap.conf"
```

The image that was tested last also used the diagnostic overlay:

```shell
    -DEXTRA_DTC_OVERLAY_FILE=speaker-a2dp/es8311-i2c-bitbang.overlay
```

## Flashing and monitoring

```shell
export PORT=/dev/cu.usbserial-1120      # ls /dev/cu.usb*

west flash -d out/esp32-zephyr-s31-speaker-a2dp --no-rebuild --esp-device $PORT
cd out/esp32-zephyr-s31-speaker-a2dp && west espressif monitor -p $PORT
```

Erase the flash first (`esptool --port $PORT erase-flash`) when the board holds
the fabric of a different application.

## Testing

### 1. Commission

Build chip-tool for the host if needed, for example on an Apple silicon Mac:
`./scripts/build/build_examples.py --target darwin-arm64-chip-tool build`.

```shell
chip-tool pairing ble-wifi 1 <SSID> <PASSWORD> 20202021 3840
```

After commissioning, and on every later boot, the log shows:

```
a2dp_sink: A2DP sink discoverable as "Matter Speaker"
```

### 2. Connect an audio source

Pair a phone or a Mac with "Matter Speaker" and play audio. The log shows:

```
a2dp_sink: A2DP connected
a2dp_sink: A2DP stream configured: 44100 Hz, 2 channel(s)
a2dp_sink: A2DP stream started
a2dp_sink: A2DP audio: 193 packets, 144364 bytes in 5002 ms, volume 100
a2dp_sink: A2DP playback: 1671 frames played, 66 dropped (last error -35), 0 output restarts, codec failed
a2dp_sink: Audio tap: 1722 frames sent, 2 not sent
```

The board does not store pairing keys. After a board reset, remove ("Forget")
the device on the phone and pair again.

### 3. Listen on the host

With the audio tap built in, run the script on the host whose address is in
`audio-tap.conf`, then play a song on the phone:

```shell
# Capture a whole song, then play it
python3 capture_song.py my_song.wav

# Capture only, play later
python3 capture_song.py my_song.wav --no-play
afplay my_song.wav
```

The capture starts with the first frame that is not silence and stops after 8
seconds without audio (`--idle` changes that) or on Ctrl+C. The file is mono,
because the board mixes both channels for its mono codec. The tap is taken
before the volume is applied.

### 4. Control from Matter

The Speaker is endpoint 1.

```shell
chip-tool onoff on 1 1
chip-tool levelcontrol move-to-level 200 0 0 0 1 1
chip-tool onoff off 1 1
```

Each command logs `ZephyrSpeaker: volume ...`, and the `volume` value of the
`A2DP audio` line follows. With a working codec, On/Off sets the codec mute and
the level maps to -48 dB ... 0 dB.

## Known limitations

-   The ES8311 does not acknowledge on I2C, with the I2C0 controller and with a
    bit-banged bus. See `WORK_LOG.md`, "Open issues".
-   Internal RAM is almost full (96.7 % static). The board's 16 MB PSRAM is not
    used yet. `WORK_LOG.md` has the plan.
-   Workarounds that the pull request authors may want solved differently: the
    cleared "BR/EDR Not Supported" feature bit and tinycrypt for the BR/EDR
    pairing keys.
-   Diagnostic code is still present: the I2C address scan in the ES8311
    driver, the LMP feature dump in the Bluetooth host, and the pin level log
    in `A2dpSink.c`.
