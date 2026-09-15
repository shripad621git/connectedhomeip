# Matter Lighting App — ESP32-C6 on Zephyr

A Matter **On/Off Light** for the ESP32-C6 running **Zephyr RTOS** (not ESP-IDF),
built on the generic `zephyr` CHIP device platform — the same path NXP's RW612
Zephyr port uses. There is intentionally **no** new `src/platform/*` directory: the
generic `src/platform/Zephyr/` layer provides the whole stack.

This is a **prototype / bring-up** build. It uses the built-in *example* Device
Attestation Credentials and the standard test passcode (`20202021`) / discriminator
(`3840`), so it commissions against `chip-tool` with no factory data. **Do not ship
this configuration.**

> **BLE + WiFi.** CHIPoBLE is enabled, so a fresh device is commissioned the normal
> Matter way — over BLE, which hands the device its WiFi credentials:
> `chip-tool pairing ble-wifi 1 "<SSID>" "<PSK>" 20202021 3840`. This is verified
> working on hardware. Earlier revisions of this example were WiFi-only with BLE
> off; that path (manual `wifi connect` + `pairing onnetwork`) still works if you
> flip `CONFIG_BT=n`, but is no longer the default.

---

## What this build uses

| Concern | Choice | Why |
|---|---|---|
| Device platform | generic `zephyr` (`chip_device_platform="zephyr"`) | no vendor platform dir needed |
| Zephyr | **v4.4.2** | mbedTLS 4.1 (TF-PSA-Crypto); see [crypto](#crypto-psa-not-classic-mbedtls) |
| Transport | **BLE + WiFi** (`CONFIG_BT=y`, `CONFIG_CHIP_WIFI=y`) | standard Matter commissioning |
| Commissioning | **`pairing ble-wifi`** (CHIPoBLE → WiFi) | creds delivered over BLE |
| WiFi driver | generic `ZephyrWifiDriver` over `net_mgmt` | ESP32-native driver, **no** WPA supplicant |
| Crypto / PASE | `chip_crypto="psa"` + `chip_crypto_spake2p="mbedtls"` | required on 4.4.x; keeps SPAKE2+ off PSA |
| C library | **picolibc** + `THREAD_LOCAL_STORAGE` | newlib no longer selectable on 4.4.x |
| DNS-SD | CHIP minimal mDNS | operational discovery after commissioning |
| Storage | Zephyr `settings` over NVS | fabric survives cold boot |
| Logging | `CONFIG_LOG_MODE_IMMEDIATE=y` | lossless boot log on the slow UART |

Final footprint on `esp32c6_devkitc/esp32c6/hpcore`: **~81 % SRAM, ~37 % flash**.

---

## Prerequisites (the validated toolchain)

These versions are load-bearing — **do not deviate**:

| Component | Version / location | Notes |
|---|---|---|
| Zephyr tree | **v4.4.2** (`~/zephyr-4.4.2/zephyr`) | ships mbedTLS 4.1 + tf-psa-crypto → PSA crypto path |
| Zephyr SDK | **1.0.1** (`~/zephyr-sdk-1.0.1`) | riscv64 toolchain; 4.4.2 wants ≥ 1.0.x |
| Build venv | **Python 3.13** (`~/zephyr-4.4.2/.venv`) | CHIP tooling uses `match` statements — system Python 3.9 fails with a bare `SyntaxError` |
| ZAP | **≥ v2026.9.9** (`.zap-2026-9/…`) | master's codegen version-validates this; the cipd zap in `.environment` is too old |
| CHIP host tools | `gn`, `ninja`, `pw_python` | from `scripts/bootstrap.sh` (`.environment/`) |
| chip-tool | built separately | the Matter commissioner used to pair/control |

### One-time setup

```bash
# 1. Zephyr west workspace pinned to v4.4.2
mkdir ~/zephyr-4.4.2 && cd ~/zephyr-4.4.2
west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.4.2 .
west update
west blobs fetch hal_espressif        # pull the C6 WiFi/PHY/BLE radio blobs

# 2. Python 3.13 venv for west + CHIP codegen (system python3 3.9 will NOT work)
python3.13 -m venv ~/zephyr-4.4.2/.venv
~/zephyr-4.4.2/.venv/bin/pip install west esptool
~/zephyr-4.4.2/.venv/bin/pip install -r ~/zephyr-4.4.2/zephyr/scripts/requirements.txt
# CHIP build + codegen deps (provides python-path, lark, jinja2, ...):
~/zephyr-4.4.2/.venv/bin/pip install \
  -r ~/esp/connectedhomeip/scripts/setup/requirements.build.txt \
  -r ~/esp/connectedhomeip/scripts/setup/requirements.zephyr.txt

# 3. Zephyr SDK 1.0.1 → ~/zephyr-sdk-1.0.1  (riscv64 toolchain)

# 4. CHIP host build tools (once) — bootstraps .environment/ (gn/ninja/pigweed)
cd ~/esp/connectedhomeip
source scripts/bootstrap.sh

# 5. ZAP >= 2026.9.9 (auto-detected from the SDK's zap.json)
~/zephyr-4.4.2/.venv/bin/python scripts/tools/zap/zap_download.py --extract-root ./.zap-2026-9
```

---

## Per-shell environment

The GN sub-build needs the pigweed environment root, and the build must run on the
3.13 venv, not system Python:

```bash
WS=~/zephyr-4.4.2
CHIP=~/esp/connectedhomeip
export ZEPHYR_BASE="$WS/zephyr"
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-1.0.1
export _PW_ACTUAL_ENVIRONMENT_ROOT="$CHIP/.environment"   # GN imports pigweed_environment.gni from here
export ZAP_INSTALL_PATH="$CHIP/.zap-2026-9/zap-v2026.09.09"
export PATH="$WS/.venv/bin:$CHIP/.environment/cipd/packages/pigweed:$PATH"
WEST="$WS/.venv/bin/west"
```

A ready-made `build-c6-matter.sh` that sets all of the above lives in the workspace
root; the rest of this doc assumes the env is set.

---

## Build

```bash
$WEST build -b esp32c6_devkitc/esp32c6/hpcore \
  -d "$WS/build/matter_light_c6" \
  "$CHIP/examples/lighting-app/esp32/zephyr" \
  -- -DPython3_EXECUTABLE="$WS/.venv/bin/python"
```

- Add `-p always` for a clean build after changing Kconfig/board files.
- The `boards/esp32c6_devkitc_esp32c6_hpcore.{overlay,conf}` files apply
  automatically (4 MB flash fix, `led0` on **GPIO2**, trimmed WiFi/net buffers).
- Success prints `Successfully created ESP32-C6 image` and a memory report.

Output image: `$WS/build/matter_light_c6/zephyr/zephyr.bin`.

---

## Flash

> **One owner of the serial port at a time.** A held monitor session is the #1 cause
> of flash failures (`Resource busy`, `No serial data received`).
> `lsof /dev/cu.usbserial-*` → `kill -9 <PID>`.

Flashing the built image directly with `esptool` is the most reliable path (it
cannot accidentally reconfigure against a different Zephyr tree, and needs no env):

```bash
$WS/.venv/bin/python -m esptool \
  --chip esp32c6 --port /dev/cu.usbserial-120 --baud 460800 \
  write_flash 0x0 "$WS/build/matter_light_c6/zephyr/zephyr.bin"
```

`west flash` also works but re-runs CMake, so it needs the full env above **and**
an explicit `-d`, or it will pick up whatever build tree your shell is standing in:

```bash
$WEST flash -d "$WS/build/matter_light_c6"
```

---

## Monitor

`west espressif monitor` is preferred over `screen` because it **auto-decodes panic
backtraces** using the ELF. On macOS you **must** pass `-p`, or it probes the
phantom `/dev/cu.Bluetooth-Incoming-Port` and `/dev/cu.debug-console` first and
stalls ~30 s on each:

```bash
cd "$WS/build/matter_light_c6"                 # build dir comes from CWD here
$WEST espressif monitor -p /dev/cu.usbserial-120
# exit: Ctrl-]
```

> ⚠️ **`-d` trap:** for `west espressif monitor`, `-d` means
> `--enable-address-decoding`, **not** build-dir — the build dir is taken from the
> current directory, so you must `cd` into it. (Contrast `west flash -d <builddir>`,
> where `-d` *is* the build dir.)

Plain `python -m serial.tools.miniterm /dev/cu.usbserial-120 115200` is a simple
raw alternative (exit `Ctrl-]`).

---

## Run: commission over BLE + WiFi

BLE advertising comes up ~30 s after boot (the WiFi/net stack initialises first).
Watch the monitor for:

```
[DL]CHIPoBLE advertising started
[DIS]Advertise commission parameter ... discriminator=3840/15 cm=1 cp=0 jf=0
[SVR]Manual pairing code: [34970112332]
```

`cm=1` = commissioning window open. Then, from the host (BLE within range,
2.4 GHz AP reachable):

```bash
# node id 1, SSID, PSK, test passcode, discriminator
chip-tool pairing ble-wifi 1 "MyNetwork" "MyPassword" 20202021 3840
```

Success ends with `Commissioning complete for node ID ...: success`. Control the
light (LED on **GPIO2**, a non-strapping pin):

```bash
chip-tool onoff toggle 1 1
chip-tool onoff on     1 1
chip-tool onoff read on-off 1 1
```

The fabric survives reboot (NVS); after a reset, `onoff read` still answers without
re-commissioning. Clear the commissioner's state between fresh commissionings with
`rm -rf /tmp/chip_tool_kvs`.

---

## Design decisions & gotchas

These are the non-obvious things that make this build work. If you edit the config,
keep them in mind.

### `THREAD_ANALYZER_AUTO` silently kills BLE advertising on C6
**The one that cost the most to find.** With `CONFIG_THREAD_ANALYZER_AUTO=y`, the
device advertises according to every log — `bt_le_adv_start()` returns 0, the
controller ACKs `LE_Set_Advertising_Enable` with status 0x00, `cm=1` — yet **no
commissioner ever discovers it** and BLE commissioning silently fails. The
analyzer's periodic thread walks every thread's stack, and its first pass lands
exactly when advertising is enabled; on the C6 that stops the controller from
putting the advertiser on air. An HCI trace confirmed the host sequence and
controller acks are byte-identical to a working build. `prj.conf` keeps
`THREAD_ANALYZER`/`AUTO` **off** (they were only ever for one-time stack sizing) and
`THREAD_STACK_INFO=y` (CHIP's `DiagnosticDataProvider` links against
`k_thread_stack_space_get`). Turn AUTO on only transiently, off-target.

### Crypto: PSA, not classic mbedTLS
Zephyr 4.4.x ships mbedTLS 4.1 (TF-PSA-Crypto), which moved the classic API
(`mbedtls/bignum.h`, `mbedtls/ecp.h`, `mbedtls_pkcs5_pbkdf2_hmac`) under
`mbedtls/private/`. `CHIPCryptoPALmbedTLS.cpp` includes them unguarded and no longer
compiles, so the classic backend is not an option on 4.4.x (the `mbedtls-3.6` module
in the tree exists only for TF-M). This build selects **`CONFIG_CHIP_CRYPTO_PSA=y`**
(`CHIPCryptoPALPSA.cpp` is version-gated for 4.x) and keeps
**`CONFIG_CHIP_CRYPTO_SPAKE2P_MBEDTLS=y`** — with `spake2p="psa"`,
`src/crypto/BUILD.gn` would compile `PSASpake2p.cpp`, which needs
`PSA_ALG_SPAKE2P_MATTER`, unimplemented in mainline TF-PSA-Crypto. Entropy uses
`CONFIG_MBEDTLS_PSA_DRIVER_GET_ENTROPY=y`.

### `recvmsg()` redefinition on Zephyr 4.4
Zephyr 4.4 added `recvmsg()` to `<zephyr/posix/sys/socket.h>`; CHIP's
`src/inet/ZephyrSocket.h` also defines a `static inline recvmsg()`, and GCC rejects
the static-after-extern clash — but only in some translation units, so it is latent.
Fixed upstream in this branch by gating the shim on `ZEPHYR_VERSION_CODE < 4.4.0`.

### picolibc, not newlib
On 4.4.x, `NEWLIB_LIBC` no longer wins the libc choice ("PICOLIBC ended up as the
choice selection"). `prj.conf` selects `CONFIG_PICOLIBC=y` +
`CONFIG_THREAD_LOCAL_STORAGE=y` (CHIP uses `thread_local`).

### `cm=0` ⇒ pairing autostart
On the Zephyr platform, `CHIP_DEVICE_CONFIG_ENABLE_PAIRING_AUTOSTART` defaults to
**0** (unlike CHIP's generic default of 1) unless `CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART`
is set. Without it, `Server::Init` never opens the basic commissioning window and
the device advertises with `cm=0` (discoverable but **not pairable**). `prj.conf`
sets `CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART=y`.

### User Label cluster removed
The lighting ZAP's only consumer of `DeviceInfoProvider` is the User Label cluster,
whose init does `VerifyOrDie(GetDeviceInfoProvider() != nullptr)`. This minimal app
does not register a provider, so a local ZAP (`zap/lighting-app.zap` + `.matter`)
with User Label removed is used — avoiding a boot-time panic. (Alternatively,
register a `DeviceInfoProviderImpl` in `AppTask` and use the stock ZAP.)

---

## RAM notes (the binding budget)

The C6 has ~497 KiB SRAM and **no PMP hardware stack guard** — a stack overflow
corrupts silently, so this build keeps `CONFIG_STACK_CANARIES=y`. Profile with:

```bash
$WEST build -d "$WS/build/matter_light_c6" -t ram_report
```

Levers, in order of impact:
- `CONFIG_CHIP_MALLOC_SYS_HEAP_SIZE` (16 KB here; nrf uses 14 KB, NXP 28 KB).
- `CONFIG_MBEDTLS_PSA_KEY_SLOT_COUNT` (16 here; one slot per CASE session + transients).
- `CONFIG_SYSTEM_CONFIG_PACKETBUFFER_POOL_SIZE` in `CHIPProjectConfig.h` (8, from 15).
- `CONFIG_ESP32_WIFI_*` buffer counts and `NET_BUF_*` counts in the board `.conf`.

**Hard floor:** the largest single block (~101 KB) is the Zephyr sys-heap built from
`HEAP_MEM_POOL_ADD_SIZE_ESP_BT` (50 KB) + `_ESP_WIFI` (51 KB), hard-coded by
`hal_espressif` as the C6 radio-blob minimum — promptless, not app-configurable.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Device advertises (`cm=1`, "advertising started") but **no commissioner ever sees it** | `CONFIG_THREAD_ANALYZER_AUTO=y` stops the C6 controller radiating the advertiser | disable `THREAD_ANALYZER`/`THREAD_ANALYZER_AUTO` (already off here) |
| `west espressif monitor` hangs on `cu.Bluetooth-Incoming-Port` | macOS phantom serial ports probed first | always pass `-p /dev/cu.usbserial-XXX` |
| `SyntaxError` (`match sys.platform:`) from a CHIP script | ran on system Python 3.9 | use the 3.13 venv (`$WS/.venv/bin/python`) |
| `ModuleNotFoundError: python_path` at CMake configure | codegen deps missing in the venv | `pip install -r scripts/setup/requirements.build.txt -r scripts/setup/requirements.zephyr.txt` |
| GN error: `Unable to load ".../pigweed_environment.gni"` | `_PW_ACTUAL_ENVIRONMENT_ROOT` unset | `export _PW_ACTUAL_ENVIRONMENT_ROOT=$CHIP/.environment` |
| ZAP version-validation failure at build | `.zap` older than 2026.9.9 | `zap_download.py --extract-root ./.zap-2026-9` |
| `Resource busy` / `No serial data received` on flash | a monitor holds the port | `lsof /dev/cu.usbserial-*` → `kill -9 <PID>` |
| Reconfigures against Zephyr v4.3.x and warns `undefined symbol MBEDTLS_PSA_DRIVER_GET_ENTROPY` | `west flash`/`build` picked up an old build tree | pass `-d $WS/build/matter_light_c6`, and `ZEPHYR_BASE=$WS/zephyr` |
| Advertise shows `cm=0`, pairing fails | commissioning window not open | `CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART=y`; commission within ~15 min; reboot re-opens |
| `[IN]IPV6_PKTINFO failed: 109` at boot | `CONFIG_NET_CONTEXT_RECV_PKTINFO` unset | harmless to BLE/commissioning; set it for robust IPv6 operational routing |
| WiFi won't associate | wrong key type / band | the C6 is 2.4 GHz only; check SSID/PSK |
