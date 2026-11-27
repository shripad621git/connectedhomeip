# Matter Speaker on ESP32-S31 (Zephyr)

This example is a Matter Speaker for the ESP32-S31-Function-CoreBoard under
Zephyr. The board joins a Matter fabric over BLE and Wi-Fi, and then accepts
audio from a phone or a computer over Bluetooth Classic (A2DP sink, SBC codec).
The Matter On/Off and Level Control clusters of the Speaker endpoint set the
volume.

The example builds its Matter data model in code. It has no ZAP file and no
generated cluster code, and it does not use sources of another example.

The ESP32-S31 support in Zephyr is not merged completely yet. This example
builds against two open pull requests, so it is a preview and not a release.

## Status

Tested on an ESP32-S31-Function-CoreBoard on 2026-11-27.

| Function                                                 | Status                                                           |
| -------------------------------------------------------- | ---------------------------------------------------------------- |
| Matter commissioning over BLE and Wi-Fi                  | Works                                                            |
| Bluetooth Classic discovery, pairing and A2DP connection | Works                                                            |
| SBC audio received and decoded on the board              | Works                                                            |
| Decoded audio sent to a host over Wi-Fi and played there | Works, see [Known limitations](#known-limitations) for audio loss |
| Sound from the on-board speaker output (I2S and ES8311)  | Not working, see [Known limitations](#known-limitations)         |
| Matter On/Off and Level Control change the sink volume   | Volume value follows; not audible until the speaker output works |
| Bluetooth pairing keys kept across a reboot              | Not implemented                                                  |

## Files

| File                                                         | Purpose                                                               |
| ------------------------------------------------------------ | --------------------------------------------------------------------- |
| `prj.conf`                                                   | Matter, Wi-Fi, Bluetooth, audio and memory configuration              |
| `audio-tap.conf`                                             | Optional: sends the decoded audio to a host over UDP                  |
| `capture_song.py`                                            | Host script: receives the audio, saves a WAV file and plays it        |
| `boards/esp32s31_function_coreboard_esp32s31_hpcore.overlay` | I2S output, ES8311 codec and speaker amplifier pin                    |
| `main/AppTask.cpp`                                           | Starts the Matter stack and registers the two endpoints               |
| `main/RootNode.cpp`                                          | Endpoint 0: the clusters of every Matter node, and the Wi-Fi clusters |
| `main/Speaker.cpp`                                           | Endpoint 1: Speaker device type with On/Off and Level Control         |
| `main/A2dpSink.c`                                            | A2DP sink, SBC decoding, I2S playback, codec volume, audio tap        |
| `main/DeviceEndpoint.cpp`                                    | Common part of an endpoint: device types and the Descriptor cluster   |
| `main/FactoryResetButton.cpp`                                | BOOT button held for 5 seconds resets the node to factory state       |

## Versions

| Component       | Source                                                               | Commit         |
| --------------- | -------------------------------------------------------------------- | -------------- |
| `zephyr`        | [#125547](https://github.com/zephyrproject-rtos/zephyr/pull/125547)  | `ee7307433e3e` |
| `hal_espressif` | [#696](https://github.com/zephyrproject-rtos/hal_espressif/pull/696) | `ef39d22cfd10` |
| Toolchain       | Zephyr SDK 1.0.1 (`riscv64-zephyr-elf`)                              |                |
| Flasher         | esptool 5.6.0                                                        |                |

Both pull requests can change. If a build fails on a newer revision, check out
the commits above.

## Setup

Prerequisites:

-   A Matter build environment for this repository, see
    `docs/guides/BUILDING.md`.
-   Zephyr SDK 1.0.1 or newer with the `riscv64-zephyr-elf` toolchain.
-   Python 3.12 or newer.

The commands below use these two locations. Change them to match your machine.

```shell
export CHIP=~/connectedhomeip
export ZEPHYR_WORKSPACE=~/zephyr-esp32s31
```

Create the Zephyr workspace from the pull request:

```shell
mkdir -p "$ZEPHYR_WORKSPACE" && cd "$ZEPHYR_WORKSPACE"
python3 -m venv .venv
source .venv/bin/activate
pip install west

west init -m https://github.com/zephyrproject-rtos/zephyr .
git -C zephyr fetch origin pull/125547/head
git -C zephyr checkout -b esp32s31 FETCH_HEAD
west update
west blobs fetch hal_espressif
west packages pip --install
pip install -r "$CHIP/scripts/setup/requirements.build.txt" \
            -r "$CHIP/scripts/setup/requirements.zephyr.txt"
```

The manifest of the Zephyr pull request selects the matching `hal_espressif`
pull request, so `west update` fetches it.

`west update` downloads every Zephyr module. To download only what this build
needs, run this before `west update`:

```shell
west config manifest.project-filter -- "-.*,+hal_espressif,+mbedtls,+tf-psa-crypto,+mcuboot,+openthread,+hostap,+picolibc,+zcbor,+cmsis_6,+libsbc"
```

## Building

In every new terminal, activate the Matter environment first and then the
Zephyr environment:

```shell
cd "$CHIP"
source scripts/activate.sh
source "$ZEPHYR_WORKSPACE/.venv/bin/activate"
source "$ZEPHYR_WORKSPACE/zephyr/zephyr-env.sh"
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-1.0.1
```

Speaker:

```shell
west build -p always -b esp32s31_function_coreboard/esp32s31/hpcore \
    -d out/esp32-zephyr-speaker examples/speaker-app/esp32_zephyr
```

Speaker with the audio tap. Set the IPv4 address of the host that runs
`capture_song.py` in `audio-tap.conf` first
(`CONFIG_SPEAKER_AUDIO_UDP_TAP_HOST`):

```shell
west build -p always -b esp32s31_function_coreboard/esp32s31/hpcore \
    -d out/esp32-zephyr-speaker examples/speaker-app/esp32_zephyr \
    -- -DEXTRA_CONF_FILE=audio-tap.conf
```

## Flashing and monitoring

```shell
export PORT=/dev/ttyUSB0        # macOS: /dev/cu.usbserial-*

west flash -d out/esp32-zephyr-speaker --no-rebuild --esp-device $PORT
cd out/esp32-zephyr-speaker && west espressif monitor -p $PORT
```

Add `--erase` to the flash command when the board holds the data of another
application or of an earlier commissioning.

## Testing

### 1. Commission

The commissioner and the board must be on the same Wi-Fi network. Build
chip-tool for the host if needed, for example
`./scripts/build/build_examples.py --target linux-x64-chip-tool build`.

```shell
chip-tool pairing ble-wifi 1 <SSID> <PASSWORD> 20202021 3840
```

After commissioning, and on every later boot, the log shows:

```
a2dp_sink: A2DP sink discoverable as "Matter Speaker"
```

### 2. Connect an audio source

Pair a phone or a computer with "Matter Speaker" and play audio. The log shows
a report every 5 seconds:

```
a2dp_sink: A2DP connected
a2dp_sink: A2DP stream configured: 44100 Hz, 2 channel(s)
a2dp_sink: A2DP stream started
a2dp_sink: A2DP audio: 250 packets, 145500 bytes in 5120 ms, volume 100
a2dp_sink: A2DP playback: 967 frames played, 783 dropped (last error -12), 12 output restarts, codec failed
a2dp_sink: Audio tap: 350 datagrams sent, 0 not sent, 0 frames not queued, 6 frames queued
```

About 250 packets in 5 seconds is the complete stream of a 44.1 kHz source.
A lower number shows that Bluetooth did not receive all the audio. The
`A2DP playback` line reports the on-board speaker output, which does not work
yet. The `Audio tap` line is printed by the build with the audio tap.

The board does not store pairing keys. After a board reset, remove ("Forget")
the device on the phone and pair again.

### 3. Listen on the host

This step needs the build with the audio tap. Run the script on the host whose
address is in `audio-tap.conf`, then play a song on the phone:

```shell
cd examples/speaker-app/esp32_zephyr

# Capture a song, then play it
python3 capture_song.py my_song.wav

# Capture only
python3 capture_song.py my_song.wav --no-play
```

The capture starts with the first sound and stops after 8 seconds without audio
(`--idle` changes that) or on Ctrl+C. The script prints how much audio was lost
on the way:

```
Saved my_song.wav: 3 min 16.7 s, 0.0 s lost (0.00%)
```

The script counts only the audio that was lost between the board and the host.
Audio that Bluetooth did not receive is not counted; the `A2DP audio` line of
the log shows it.

The file is mono. The tap is taken before the volume is applied. The script
plays the file with `afplay`, which exists on macOS; on other systems use
`--no-play` and open the file with any player.

If nothing is received, check that the host still has the address that was
built into the firmware, and that the host and the board are on the same
network.

### 4. Control from Matter

The Speaker is endpoint 1.

```shell
chip-tool onoff on 1 1
chip-tool levelcontrol move-to-level 200 0 0 0 1 1
chip-tool onoff off 1 1
```

Each command logs `Speaker: volume ...`, and the `volume` value of the
`A2DP audio` line follows.

### 5. Factory reset

Hold the BOOT button for 5 seconds and release it. The node forgets its fabric
and Wi-Fi network and can be commissioned again. This function was not tested
on hardware.

## Data model

| Endpoint | Device type | Clusters                                                                                                                                                                                                                                           |
| -------- | ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0        | Root Node   | Descriptor, Basic Information, General Commissioning, Administrator Commissioning, General Diagnostics, Software Diagnostics, Group Key Management, Access Control, Operational Credentials, Network Commissioning (Wi-Fi), Wi-Fi Network Diagnostics |
| 1        | Speaker     | Descriptor, On/Off (mute), Level Control (volume)                                                                                                                                                                                                  |

`main/AppTask.cpp` creates the two endpoints. To add a cluster to the Speaker,
add it in `Speaker::Register` and remove it in `Speaker::Unregister`.

The example uses the test device attestation credentials and the test setup
code of the SDK (`main/include/CHIPProjectConfig.h`). A product replaces them.

## Memory layout

The ESP32-S31 has 504 KB of internal SRAM, which Wi-Fi, BLE, Bluetooth Classic
and Matter do not fit into together. `prj.conf` therefore uses the 16 MB PSRAM
of the board for:

-   the network stack buffers and thread stacks,
-   the Wi-Fi driver heap,
-   the zero-initialized data of the Matter library.

The data of the application stays in internal SRAM. The SBC decoder runs on the
Bluetooth RX thread, and with its state in PSRAM that thread is too slow and
the Bluetooth controller drops media packets.

## Known limitations

-   **No sound from the on-board speaker output.** The ES8311 codec does not
    answer on I2C at boot (`es8311: Failed to read chip id1 (-5)`, later
    `Audio codec not ready` and `codec failed` in the playback report), and
    the I2S output accepts only about half of the decoded frames. The cause is
    not found yet. Use the audio tap to verify the audio.
-   **Audio loss depends on the radio conditions.** Wi-Fi and Bluetooth share
    one radio. In the test on 2026-11-27 Bluetooth received 65% to 90% of the
    media packets, and 5% of the tap datagrams did not arrive at the host. An
    earlier test of the same sink code on another network received the
    complete stream and lost no tap datagram.
-   **Pairing keys are not stored.** The audio source has to pair again after
    every board reset.
-   **One audio source at a time**, with the SBC codec only.
-   **Not upstream.** The Zephyr and `hal_espressif` pull requests are open and
    can change.
