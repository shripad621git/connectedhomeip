# Matter Lighting App — ESP32-C6 on Zephyr

A Matter **On/Off Light** for the ESP32-C6 running **Zephyr RTOS** (not ESP-IDF),
built on the generic `zephyr` CHIP device platform — the same path NXP's RW612
Zephyr port uses. There is intentionally **no** new `src/platform/*` directory: the
generic `src/platform/Zephyr/` layer provides the whole stack.

This is a **prototype / bring-up** build. It uses the built-in *example* Device
Attestation Credentials and the standard test passcode (`20202021`) / discriminator
(`3840`), so it commissions against `chip-tool` with no factory data. **Do not ship
this configuration.**

> **This variant has BLE disabled and is WiFi-only.** Because there is no CHIPoBLE,
> a fresh device cannot be BLE-commissioned. Instead you bring WiFi up manually over
> the serial **shell**, then commission over the network (`chip-tool pairing
> onnetwork` / `pairing code`). This is a deliberate trade to reclaim ~92 KB of SRAM
> and isolate the WiFi path. See [Design decisions](#design-decisions--gotchas).

---

## What this build uses

| Concern | Choice | Why |
|---|---|---|
| Device platform | generic `zephyr` (`chip_device_platform="zephyr"`) | no vendor platform dir needed |
| Transport | WiFi only, **BLE disabled** (`CONFIG_BT=n`) | frees ~92 KB SRAM; WiFi isolation |
| WiFi driver | generic `ZephyrWifiDriver` over `net_mgmt` | ESP32-native driver, **no** WPA supplicant |
| WiFi provisioning | Zephyr `wifi` shell → manual `wifi connect` | BLE (the normal cred path) is gone |
| Commissioning | **on-network** (`_matterc._udp`) | device is already on WiFi when paired |
| Crypto / PASE | `chip_crypto="mbedtls"` + CHIP's portable SPAKE2+ | avoids the unimplemented PSA SPAKE2+ |
| DNS-SD | CHIP minimal mDNS | sidesteps Zephyr's missing DNS-SD subtypes |
| Storage | Zephyr `settings` over NVS | survives cold boot |
| Logging | `CONFIG_LOG_MODE_IMMEDIATE=y` | lossless boot log on the slow UART |

Final footprint on `esp32c6_devkitc/esp32c6/hpcore`: **~71 % SRAM, ~31 % flash**.

---

## Prerequisites (the validated toolchain)

These exact versions are load-bearing — **do not deviate** (see
[the mbedTLS wall](#the-mbedtls-wall-why-zephyr-v431)):

| Component | Version / location | Notes |
|---|---|---|
| Zephyr tree | **v4.3.1** (`~/zephyr/zephyr`) | mbedTLS **3.6.6**. v4.4.x ships mbedTLS 4.1 → **breaks** CHIP's crypto PAL |
| Zephyr SDK | **0.17.4** (`~/zephyr-sdk-0.17.4`) | v4.3.1 rejects SDK 1.0.1 |
| Build venv | **`~/zephyr/.venv313`** (Python 3.13, e.g. pyenv) | `west` + Zephyr reqs live here. Avoid Homebrew 3.14 (broken `pyexpat`) |
| ZAP | **v2026.05.12** (`.zap-2026/…`) | pinned; version-validated by the data-model codegen |
| CHIP host tools | `gn`, `ninja`, `pw_python` | from `scripts/activate.sh` |
| chip-tool | built separately | the Matter commissioner used to pair/control |

### One-time setup

```bash
# 1. Zephyr west workspace pinned to v4.3.1 (adjust to your layout)
cd ~/zephyr
west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.3.1
west update
west blobs fetch hal_espressif        # pull the C6 WiFi/PHY radio blobs

# 2. Python venv for west (3.13)
python3.13 -m venv ~/zephyr/.venv313
~/zephyr/.venv313/bin/pip install west
~/zephyr/.venv313/bin/pip install -r ~/zephyr/zephyr/scripts/requirements.txt
~/zephyr/.venv313/bin/pip install esptool        # needed by `west flash` for esp32

# 3. Zephyr SDK 0.17.4 → ~/zephyr-sdk-0.17.4  (riscv64 toolchain)

# 4. CHIP host build tools (once)
cd ~/esp/connectedhomeip
source scripts/bootstrap.sh

# 5. Pinned ZAP
python scripts/tools/zap/zap_download.py --extract-root ./.zap-2026
```

---

## Per-shell environment

Everything below assumes this env is set in your shell:

```bash
cd ~/esp/connectedhomeip
source scripts/activate.sh                                     # gn/ninja/pw on PATH
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-0.17.4
export ZEPHYR_BASE=~/zephyr/zephyr
export ZAP_INSTALL_PATH=~/esp/connectedhomeip/.zap-2026/zap-v2026.05.12-nightly
WEST=~/zephyr/.venv313/bin/west                                # the venv313 west
```

---

## Build

```bash
$WEST build -b esp32c6_devkitc/esp32c6/hpcore \
  -d ~/zephyr/build/matter_light_c6 \
  ~/esp/connectedhomeip/examples/lighting-app/esp32/zephyr
```

- Add `-p always` for a clean (pristine) build after changing Kconfig/board files.
- The `boards/esp32c6_devkitc_esp32c6_hpcore.{overlay,conf}` files apply
  automatically (4 MB flash fix, `led0` on **GPIO2**, trimmed WiFi/net buffers).
- Success prints `Successfully created ESP32-C6 image` and a memory report.

Output image: `~/zephyr/build/matter_light_c6/zephyr/zephyr.bin`.

---

## Flash

> **One owner of the serial port at a time.** A held `screen`/monitor session is
> the #1 cause of flash failures (`Resource busy`, `No serial data received`).

```bash
ls /dev/cu.usbserial-*                       # find your port, e.g. -3120
lsof /dev/cu.usbserial-*                       # anything listed = holder → kill -9 <PID>

$WEST flash -d ~/zephyr/build/matter_light_c6 \
  --esp-device /dev/cu.usbserial-3120          # <- your port
```

Here `-d` **is** the build directory (standard `west` meaning).
If flashing errors with `No module named 'esptool'`:
`~/zephyr/.venv313/bin/pip install esptool`, then retry.

---

## Monitor

`west espressif monitor` is preferred over `screen` because it **auto-decodes
panic backtraces** using the ELF.

```bash
cd ~/zephyr/build/matter_light_c6            # MUST cd into the build dir
$WEST espressif monitor -p /dev/cu.usbserial-3120
# exit: Ctrl-]
```

> ⚠️ **`-d` trap:** for `west espressif monitor`, `-d` means
> `--enable-address-decoding` (on by default), **not** build-dir. The build dir is
> taken from the current directory — so you must `cd` into it. (Contrast
> `west flash -d <builddir>`, where `-d` *is* the build dir.)

Plain `screen /dev/cu.usbserial-3120 115200` also works (exit `Ctrl-A` `K`).

---

## Run: connect WiFi, then commission

### 1. Bring WiFi up over the shell

At the `uart:~$` prompt in the monitor:

```bash
uart:~$ wifi scan                                  # confirm the AP is visible
uart:~$ wifi connect "MyNetwork" -p "MyPassword" -k 1   # -k 1 = WPA2-PSK (-k 0 = open)
uart:~$ net iface                                  # confirm DHCP gave an IPv4
```

You want to see `DHCPv4 state: bound` and an address (e.g. `192.168.20.186`).

### 2. Confirm the commissioning window is open

When WiFi connects, the mDNS advertise line **must** show `cm=1`:

```
[DIS]Advertise commission parameter ... discriminator=3840/15 cm=1 cp=0 jf=0
```

`cm=1` = commissioning window **open** (pairable). `cm=0` = closed (discoverable
but pairing will fail) — see [troubleshooting](#troubleshooting). The window is
time-limited (~15 min from boot), so commission promptly; a reboot re-opens it.

### 3. Commission with chip-tool (on-network)

```bash
# node id 1, test passcode + discriminator — NOT `pairing ble-wifi`
chip-tool pairing onnetwork 1 20202021
#   or, using the manual pairing code the device prints at boot:
chip-tool pairing code 1 34970112332
```

### 4. Control the light

```bash
chip-tool onoff toggle 1 1        # endpoint 1
chip-tool onoff on     1 1
chip-tool onoff off    1 1
```

Wire an LED to **GPIO2** (a non-strapping pin) to see On/Off, or repoint `led0`
in `boards/…overlay`.

---

## Design decisions & gotchas

These are the non-obvious things that make this build work. If you edit the
config, keep them in mind.

### BLE off ⇒ AES must be requested explicitly
`CONFIG_BT=n` compiles BLE out of both the ESP host/controller and CHIP
(`chip_config_network_layer_ble` and `CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE` both
derive from `CONFIG_BT`). But the Zephyr Bluetooth stack was *implicitly* enabling
the classic mbedTLS AES cipher. Without it, `MBEDTLS_CIPHER_CCM_ENABLED` (gated by
`MBEDTLS_SOME_AEAD_CIPHER_ENABLED`) and `MBEDTLS_CTR_DRBG_C` (which `depends on
MBEDTLS_CIPHER_AES_ENABLED`) silently drop, and CHIP's crypto PAL fails to link
(`undefined reference to mbedtls_ccm_* / mbedtls_ctr_drbg_*`). The fix in `prj.conf`
is `CONFIG_MBEDTLS_CIPHER_AES_ENABLED=y`, which makes the crypto config
self-sufficient regardless of BT.

### `cm=0` ⇒ pairing autostart
On the Zephyr platform, `CHIP_DEVICE_CONFIG_ENABLE_PAIRING_AUTOSTART` defaults to
**0** (unlike CHIP's generic default of 1) unless `CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART`
is set. With it unset, `Server::Init` never opens the basic commissioning window, so
the device advertises `_matterc._udp` with `cm=0` (discoverable via extended
discovery, but **not pairable**). `prj.conf` sets
`CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART=y` so the window opens at boot.

### The mbedTLS wall — why Zephyr v4.3.1
CHIP's mbedTLS crypto PAL needs the **classic** mbedTLS API/header layout
(`mbedtls/bignum.h`, `mbedtls_ecp_muladd`, `mbedtls_pkcs5_pbkdf2_hmac`). Zephyr
**v4.4.x** ships mbedTLS **4.1** (TF-PSA-Crypto), which removed those — the build
fails at the crypto step. Zephyr **v4.3.1** ships mbedTLS **3.6.6** (classic layout)
and compiles CHIP's crypto unchanged. This is the same line every shipping Matter
platform stays on (ESP-IDF esp-matter, NXP nxp-zsdk, NCS all use mbedTLS 3.6). You
cannot escape to PSA crypto either: `PSASpake2p.cpp` needs `PSA_ALG_SPAKE2P_MATTER`,
unimplemented in mainline TF-PSA-Crypto (vendors supply it from their own PSA
backends). So: **pin Zephyr to v4.3.1.**

### User Label cluster removed
The lighting ZAP's only consumer of `DeviceInfoProvider` is the User Label cluster,
whose init does `VerifyOrDie(GetDeviceInfoProvider() != nullptr)`. This minimal app
does not register a provider, so a local ZAP (`zap/lighting-app.zap` + `.matter`)
with User Label removed is used instead — avoiding a boot-time kernel panic.

---

## RAM notes (the binding budget)

The C6 has ~497 KiB SRAM and **no PMP hardware stack guard** — a stack overflow
corrupts silently, so this build enables `CONFIG_STACK_CANARIES` and
`CONFIG_THREAD_ANALYZER`. Profile with:

```bash
$WEST build -d ~/zephyr/build/matter_light_c6 -t ram_report
```

Levers, in order of impact:
- **BLE** — the biggest single win (~92 KB); already off here.
- `CONFIG_CHIP_MALLOC_SYS_HEAP_SIZE` (16 KB here; nrf uses 14 KB, NXP 28 KB).
- `CONFIG_SYSTEM_CONFIG_PACKETBUFFER_POOL_SIZE` in `CHIPProjectConfig.h` (8, from 15).
- `CONFIG_ESP32_WIFI_*` buffer counts and `NET_BUF_*` counts in the board `.conf`.

**Hard floor:** the largest single block (~94 KB) is the Zephyr sys-heap built from
`HEAP_MEM_POOL_ADD_SIZE_ESP_WIFI`, hard-coded by `hal_espressif` as the C6 radio-blob
minimum — not app-configurable.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Resource busy` / `No serial data received` on flash | a `screen`/monitor holds the port | `lsof /dev/cu.usbserial-*` → `kill -9 <PID>` |
| `No module named 'esptool'` | esptool missing in the venv west uses | `~/zephyr/.venv313/bin/pip install esptool` |
| `west espressif monitor` → `could not find build configuration` | didn't `cd` into the build dir (`-d` ≠ build-dir here) | `cd ~/zephyr/build/matter_light_c6` first |
| Advertise shows `cm=0`, pairing fails | commissioning window not open | ensure `CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART=y`; commission within ~15 min; reboot re-opens |
| Link error `undefined reference to mbedtls_ccm_* / mbedtls_ctr_drbg_*` | AES cipher off (BT no longer pulling it in) | `CONFIG_MBEDTLS_CIPHER_AES_ENABLED=y` |
| Crypto fails to compile (`bignum.h` not found, `mbedtls_ecp_muladd` undeclared) | Zephyr v4.4.x → mbedTLS 4.1 | pin the Zephyr tree to **v4.3.1** |
| Boot kernel panic in User Label cluster init | `DeviceInfoProvider` is null | use the local ZAP with User Label removed (already the default here) |
| `--- N messages dropped ---` in the log | deferred logging + slow UART | `CONFIG_LOG_MODE_IMMEDIATE=y` (already set) |
| WiFi won't associate | wrong key type / band | `wifi connect "SSID" -p "PSK" -k 1` (WPA2-PSK); the C6 is 2.4 GHz only |
