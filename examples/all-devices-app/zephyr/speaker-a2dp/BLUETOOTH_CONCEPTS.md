# Bluetooth concepts behind the Matter speaker

A learning guide for the pieces used in this work: BLE as Matter uses it, the
controller and HCI, Bluetooth Classic (BR/EDR), and A2DP as sink and source.
The code snippets are from this repository and from the Zephyr tree, shortened
where marked.

## 1. Two radios in one chip

Bluetooth has two radio technologies that share the 2.4 GHz band and little
else.

|                    | Bluetooth LE (BLE)                              | Bluetooth Classic (BR/EDR)                       |
| ------------------ | ----------------------------------------------- | ------------------------------------------------ |
| Channels           | 40, each 2 MHz. 3 are advertising channels.     | 79, each 1 MHz                                   |
| Finding devices    | Advertising and scanning                        | Inquiry and inquiry scan                         |
| Connecting         | Connect to an advertiser                        | Page and page scan                               |
| Data model         | GATT: services and characteristics              | Profiles on L2CAP channels (A2DP, HFP, SPP, ...) |
| Typical use        | Sensors, commissioning, small data              | Audio streaming, serial links                    |
| Use in this app    | Matter commissioning                            | Receiving music                                  |

A chip that does both is "dual mode". The ESP32-S31 is dual mode, and it also
shares the same antenna with Wi-Fi. A coexistence module hands the radio to one
of them at a time.

## 2. Host, controller and HCI

A Bluetooth stack is split in two halves that talk through the Host Controller
Interface (HCI).

```
 +--------------------------------------------------+
 |  Application (Matter BLE layer, A2dpSink.c)       |
 +--------------------------------------------------+
 |  HOST (Zephyr subsys/bluetooth/host)              |
 |   GAP, GATT, SMP        |  SDP, AVDTP, A2DP       |
 |   L2CAP (LE)            |  L2CAP (BR/EDR)         |
 +--------------------------------------------------+
 |  HCI: commands down, events up, ACL data both ways |
 +--------------------------------------------------+
 |  HCI driver (drivers/bluetooth/hci/hci_esp32.c)   |
 +--------------------------------------------------+
 |  CONTROLLER (Espressif binary libraries)          |
 |   link layer (BLE)      |  baseband + LMP (BR/EDR)|
 |   radio scheduling, coexistence with Wi-Fi        |
 +--------------------------------------------------+
```

-   **Host**: protocols and profiles. Open source, part of Zephyr.
-   **Controller**: everything with hard timing, such as hopping, packets,
    retransmissions and encryption on the air. On Espressif chips it is a set
    of binary libraries.
-   **HCI**: a small packet format. A command has an opcode made of a group
    (OGF) and a command number (OCF). The controller answers with events.

On a single chip the two halves live in the same firmware. Espressif calls the
in-memory transport "VHCI". The Zephyr driver passes packets with two calls:

```c
/* drivers/bluetooth/hci/hci_esp32.c (shortened) */
esp_bt_controller_init(&bt_cfg);
esp_bt_controller_enable(mode);                 /* ESP_BT_MODE_BLE or ESP_BT_MODE_BTDM */
esp_vhci_host_register_callback(&vhci_host_cb); /* controller -> host */
...
esp_vhci_host_send_packet(buf->data, buf->len); /* host -> controller */
```

### The ESP32-S31 controller

| Library              | Content                                                        |
| -------------------- | -------------------------------------------------------------- |
| `libbtdm_common.a`   | Shared task, HCI routing and flow control for both radios      |
| `libble_app.a`       | BLE link layer                                                 |
| `libbredr_app.a`     | BR/EDR baseband and Link Manager (LMP)                         |
| `libbtbb.a`          | Baseband and RF control                                        |
| `libcoexist.a`       | Radio sharing between Wi-Fi and Bluetooth                      |

Open source glue around them (`components/bt/porting_btdm` in the HAL) provides
memory, timers, crypto and the HCI transport. The mode is fixed at build time
by one macro, and the same `bt.c` then initializes one or both stacks:

```c
/* hal_espressif: components/bt/controller/esp32s31/esp_bt_cfg.h */
#if defined(CONFIG_BT_CLASSIC)
#define CONFIG_BT_CTRL_BREDR_ENABLE 1
#define CONFIG_BTDM_CTRL_MODE_BTDM 1     /* dual mode */
#else
#define CONFIG_BTDM_CTRL_MODE_BLE_ONLY 1
#endif
```

```c
/* hal_espressif: components/bt/controller/esp32s31/bt.c (shortened) */
#if UC_BT_CTRL_BLE_IS_ENABLE
    ret = ble_stack_init(cfg);
#endif
#if UC_BT_CTRL_BR_EDR_IS_ENABLE
    ret = bredr_stack_init(cfg);
#endif
    ret = hci_transport_init(BT_HCI_TRANSPORT_MODE);
```

### A lesson from this port: the feature bits

At start-up the host sends `Read Local Supported Features`. The reply is 8
bytes of LMP feature bits. Bit 5 of byte 4 means "BR/EDR Not Supported" and
bit 6 means "LE Supported". Zephyr checks the first one:

```c
/* zephyr: include/zephyr/bluetooth/hci_types.h */
#define BT_FEAT_BREDR(feat)   !BT_FEAT_TEST(feat, 0, 4, 5)
```

The S31 controller returned `bf 06 06 7e f8 1f 59 87`. Byte 4 is `0xf8`: real
BR/EDR features and both bits set. The host then stopped with "Non-BR/EDR
controller detected". The driver now clears that bit for this chip.

## 3. BLE as Matter uses it

Matter uses BLE only to commission a device that is not on a network yet.

1.  **Advertising.** The device advertises the Matter service UUID `0xFFF6`
    with service data that carries the discriminator (3840 in the examples),
    vendor ID and product ID.
2.  **Connection.** The commissioner (chip-tool) scans, filters by
    discriminator and connects. The device is the GAP peripheral and the GATT
    server.
3.  **GATT service.** Two characteristics carry the data: C1 is written by the
    commissioner, C2 is indicated by the device. Matter's Bluetooth Transport
    Protocol (BTP) fragments Matter messages over them.
4.  **PASE.** The passcode (20202021) secures the session with SPAKE2+.
5.  **Credentials.** The commissioner sends the Wi-Fi credentials and the
    operational certificate. The device joins Wi-Fi and BLE is closed.

```cpp
// src/platform/Zephyr/BLEManagerImpl.cpp
bt_uuid_16 UUID16_CHIPoBLEService = BT_UUID_INIT_16(0xFFF6);
```

Two details mattered once Classic was enabled:

-   **Identities.** A BLE identity is an address the device uses. With bonding
    enabled, Matter leaves identity 0 (the public address, which Classic uses)
    alone and creates identity 1 with a random static address for its own
    advertising. That needs `CONFIG_BT_ID_MAX=2`.

    ```cpp
    // src/platform/Zephyr/BLEManagerImpl.cpp
    #ifdef CONFIG_BT_BONDABLE
    constexpr uint8_t kMatterBleIdentity = 1;
    #else
    constexpr uint8_t kMatterBleIdentity = 0;
    #endif
    ```

-   **Crypto.** `BT_CLASSIC` selects the Security Manager (`BT_SMP`), which
    switches P-256 to the p256-m driver and removes the Mbed TLS ECP code that
    SPAKE2+ uses. `CONFIG_MBEDTLS_PSA_P256M_DRIVER_ENABLED=n` keeps it.

## 4. Bluetooth Classic: being found and connected

### Inquiry and page

-   **Inquiry**: a phone that scans sends inquiry packets on a hopping
    sequence. A device in **inquiry scan** listens on one frequency for a short
    window and answers with its address, its Class of Device and an Extended
    Inquiry Response (EIR).
-   **Page**: to connect, the phone pages the address. A device in **page
    scan** listens the same way and answers.

"Discoverable" means inquiry scan is on. "Connectable" means page scan is on.

```c
/* examples/all-devices-app/zephyr/src/devices/A2dpSink.c */
err = bt_br_set_connectable(true, NULL);     /* page scan */
err = bt_br_set_discoverable(true, false);   /* inquiry scan, general */
```

### Scan timing

Listening costs radio time, so the device listens for a **window** once per
**interval**. Both are in units of 0.625 ms. The defaults are 11.25 ms every
1.28 s. With Wi-Fi sharing the radio, an 8 second scan from a Mac missed the
board. Interlaced scan listens on two frequencies per window.

```c
/* A2dpSink.c */
#define SCAN_INTERVAL 0x0400 /* 640 ms */
#define SCAN_WINDOW 0x0024   /* 22.5 ms */
#define SCAN_TYPE_INTERLACED 0x01

activity->interval = sys_cpu_to_le16(SCAN_INTERVAL);
activity->window = sys_cpu_to_le16(SCAN_WINDOW);
err = bt_hci_cmd_send_sync(BT_HCI_OP_WRITE_INQUIRY_SCAN_ACTIVITY, buf, NULL);
...
net_buf_add_u8(buf, SCAN_TYPE_INTERLACED);
err = bt_hci_cmd_send_sync(BT_HCI_OP_WRITE_INQUIRY_SCAN_TYPE, buf, NULL);
```

### Name, EIR and Class of Device

The inquiry answer has no name unless the EIR contains one. Without it the
phone must connect briefly to ask for the name, and many phones hide a device
until that succeeds.

```c
/* A2dpSink.c */
static const uint8_t sServiceUuids[] = { BT_UUID_16_ENCODE(BT_SDP_AUDIO_SINK_SVCLASS) };
const char *name = bt_get_name();
const struct bt_data eir[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, name, strlen(name)),
	BT_DATA(BT_DATA_UUID16_ALL, sServiceUuids, sizeof(sServiceUuids)),
};
return bt_br_write_eir(eir, ARRAY_SIZE(eir), false);
```

The Class of Device is a 24-bit number that tells the phone which icon and list
to use. `CONFIG_BT_COD=0x200414` means: service "Audio", major class
"Audio/Video", minor class "Loudspeaker".

### Pairing

Classic pairing (Secure Simple Pairing) is an ECDH key exchange that ends in a
**link key**. Both sides store it and use it to authenticate later
connections.

-   The exchange runs between the two **controllers** (LMP), unlike BLE, where
    the host's Security Manager does it.
-   Older devices use the P-192 curve, newer ones P-256 ("Secure Connections").
    The controller creates a key pair for both at start-up.
-   The Espressif controller calls back into the glue for the math:

```c
/* hal_espressif: porting_btdm/controller/bredr/src/bredr.c */
const bredr_ecc_ops_t g_bredr_ecc_ops = {
    .ecc256_gen_key_pair = bredr_gen_keypair_ecc256,
    .ecc256_gen_dhkey = bredr_gen_dhkey_ecc256,
    .ecc192_gen_key_pair = bredr_gen_keypair_ecc192,
    .ecc192_gen_dhkey = bredr_gen_dhkey_ecc192,
};
```

Zephyr's crypto library (TF-PSA-Crypto) has no P-192, so the P-192 callback
failed and the controller asserted at boot. The glue is now built against
Espressif's tinycrypt, which has both curves.

This app stores no link key (`CONFIG_BT_SETTINGS` is off). After a reset the
phone still has a key and the board does not, and the phone's connection is
refused until the device is removed on the phone and paired again.

## 5. From a link to audio: L2CAP, SDP, AVDTP, A2DP

Once two devices are connected they have one ACL link. Everything else is
multiplexed on it.

```
  A2DP   (rules for audio: roles, codecs)
  AVDTP  (stream setup and transport)       SDP (service lookup)
  L2CAP  (channels identified by a PSM: SDP 0x0001, AVDTP 0x0019)
  ACL link (controller)
```

### SDP: what do you offer?

The phone opens an L2CAP channel to SDP and reads the service records. An A2DP
sink publishes one record that says "Audio Sink, reachable through L2CAP and
AVDTP".

```c
/* A2dpSink.c (shortened) */
static struct bt_sdp_attribute sAudioSinkAttributes[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(BT_SDP_ATTR_SVCLASS_ID_LIST, ...
		BT_SDP_ARRAY_16(BT_SDP_AUDIO_SINK_SVCLASS)      /* 0x110B */ ...),
	BT_SDP_LIST(BT_SDP_ATTR_PROTO_DESC_LIST, ...
		BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP) ...
		BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL) ...),
	BT_SDP_LIST(BT_SDP_ATTR_PROFILE_DESC_LIST, ...
		BT_SDP_ARRAY_16(BT_SDP_ADVANCED_AUDIO_SVCLASS)  /* 0x110D */ ...),
	BT_SDP_SERVICE_NAME("A2DPSink"),
};
bt_sdp_register_service(&sAudioSinkRecord);
```

### AVDTP: setting up a stream

AVDTP has two kinds of L2CAP channel on the same PSM:

-   a **signalling** channel for commands,
-   a **media** channel for the audio packets.

Each side registers Stream End Points (SEP), one per codec and direction. The
side that starts a procedure is the initiator (INT), the other the acceptor
(ACP). A phone is normally the initiator.

| Step | Command              | Meaning                                         | Callback in `A2dpSink.c` |
| ---- | -------------------- | ----------------------------------------------- | ------------------------ |
| 1    | Discover             | Which end points do you have?                   | (host answers)           |
| 2    | Get Capabilities     | Which codecs and parameters?                    | (host answers)           |
| 3    | Set Configuration    | Use SBC, 44.1 kHz, joint stereo, ...            | `A2dpConfigRequested`, `StreamConfigured` |
| 4    | Open                 | Open the media channel                          | `StreamEstablished`      |
| 5    | Start                | Audio flows                                     | `StreamStarted`          |
| 6    | Suspend              | Pause                                           | `StreamSuspended`        |
| 7    | Close / Abort        | Tear down                                       | `StreamReleased`         |

A phone often runs steps 1 to 5 right after connecting, sends a few seconds of
silence and suspends. That is why the first capture contained only silence.

### A2DP roles

-   **Source (SRC)**: produces audio. Phone, laptop.
-   **Sink (SNK)**: consumes audio. Speaker, headphones.

Two sinks cannot talk to each other. That is why wireless earbuds could not be
used to listen to this board: both are sinks.

## 6. The SBC codec and the media packet

SBC is the codec every A2DP device must support.

-   The encoder splits the signal into 4 or 8 **subbands** and processes 4, 8,
    12 or 16 **blocks** per frame. With 8 subbands and 16 blocks a frame is
    128 samples per channel: 2.9 ms at 44.1 kHz, 344.5 frames per second.
-   The **bitpool** sets how many bits a frame gets, and with it the bit rate.
    The streams in this work were about 230 kbit/s.
-   Each frame starts with the sync byte `0x9C`.

A media packet on the L2CAP media channel:

```
| RTP header (12 bytes) | SBC media header (1 byte) | SBC frame | SBC frame | ... |
                          low 4 bits: number of frames
```

Zephyr removes the RTP header and gives the rest to the application:

```c
/* A2dpSink.c (shortened) */
static void PlayMediaPacket(struct net_buf *buf)
{
	uint8_t frameCount = BT_A2DP_SBC_MEDIA_HDR_NUM_FRAMES_GET(net_buf_pull_u8(buf));
	const void *in = buf->data;
	uint32_t inSize = buf->len;

	for (uint8_t i = 0; i < frameCount; i++) {
		uint32_t outSize = sizeof(sPcmFrame);

		if (sbc_decode(&sDecoder, &in, &inSize, sPcmFrame, &outSize) != 0) {
			return;
		}
		DownmixToMono(sPcmFrame, outSize / sizeof(int16_t));
		TapPcmFrame(sPcmFrame, outSize / sizeof(int16_t), sSampleRate);
		QueuePcmFrame(outSize);
	}
}
```

## 7. The sink flow in this app, end to end

```
 Matter                        A2dpSink.c                         Phone
 ------                        ----------                         -----
 kCommissioningComplete  -->   A2dpSinkStart()
 (or kServerReady with         register SDP record
  a fabric)                    register SBC sink end point
                               write EIR, scan timing
                               connectable + discoverable   <--   inquiry, page, pairing
                                                             <--   SDP query
                               A2dpConnected                 <--   AVDTP signalling channel
                               A2dpConfigRequested           <--   Set Configuration
                               StreamConfigured -> configure I2S
                               StreamEstablished             <--   Open (media channel)
                               StreamStarted                 <--   Start
                               StreamReceived, per packet:   <--   media packets
                                 SBC decode
                                 audio tap (UDP to host)
                                 I2S queue -> codec
 On/Off, Level Control   -->   A2dpSinkSetVolume() -> codec mute and volume
```

Starting the sink from Matter events:

```cpp
// examples/all-devices-app/zephyr/src/devices/ZephyrSpeaker.cpp
void StartSinkOnceOnFabric(const DeviceLayer::ChipDeviceEvent * event, intptr_t)
{
    const bool joinedNow = event->Type == DeviceLayer::DeviceEventType::kCommissioningComplete;
    const bool alreadyOnAFabric =
        event->Type == DeviceLayer::DeviceEventType::kServerReady && Server::GetInstance().GetFabricTable().FabricCount() > 0;
    VerifyOrReturn(joinedNow || alreadyOnAFabric);

    int err = A2dpSinkStart();
    VerifyOrReturn(err == 0, ChipLogError(DeviceLayer, "ZephyrSpeaker: A2DP sink start failed: %d", err));
}
```

Registering the end point and accepting a configuration:

```c
/* A2dpSink.c (shortened) */
BT_A2DP_SBC_SINK_EP_DEFAULT(sSbcSinkEndpoint);

static int A2dpConfigRequested(struct bt_a2dp *a2dp, struct bt_a2dp_ep *ep,
			       struct bt_a2dp_codec_cfg *codec_cfg, struct bt_a2dp_stream **stream,
			       uint8_t *rsp_err_code)
{
	*sSbcConfig.codec_config = *codec_cfg->codec_config;   /* what the phone chose */
	bt_a2dp_stream_cb_register(&sSbcStream, &sStreamOps);
	*stream = &sSbcStream;
	*rsp_err_code = 0;                                      /* accept */
	return 0;
}

bt_a2dp_register_ep(&sSbcSinkEndpoint, BT_AVDTP_AUDIO, BT_AVDTP_SINK);
bt_a2dp_register_cb(&sA2dpCallbacks);
```

Mapping Matter to volume:

```cpp
// ZephyrSpeaker.cpp
void ZephyrSpeaker::UpdateSinkVolume()
{
    const bool muted     = !OnOffCluster().GetOnOff();
    const uint8_t level  = LevelControlCluster().GetCurrentLevel().ValueOr(Clusters::LevelControlCluster::kMaxLevel);
    const uint8_t volume = muted ? 0 : level;

    A2dpSinkSetVolume(volume);
}
```

## 8. The source flow, for comparison

A source does the same steps from the other side: it searches, connects and
starts the procedures. This is the order in Zephyr's sample
`samples/bluetooth/classic/a2dp_source/src/main.c` (shortened):

```c
/* 1. Offer the Audio Source service and a source end point */
bt_sdp_register_service(&a2dp_source_rec);
bt_a2dp_register_ep(&sbc_source_ep, BT_AVDTP_AUDIO, BT_AVDTP_SOURCE);

/* 2. Inquiry: look for devices */
bt_br_discovery_start(&br_discover, scan_result, CONFIG_BT_A2DP_SOURCE_DISCOVER_RESULT_COUNT);

/* 3. Page: connect to the chosen device */
default_conn = bt_conn_create_br(&results[i].addr, BT_BR_CONN_PARAM_DEFAULT);

/* 4. SDP: does the peer have an Audio Sink? */
bt_sdp_discover(default_conn, &sdp_discover);

/* 5. Open the AVDTP signalling channel */
default_a2dp = bt_a2dp_connect(conn);

/* 6. Discover the peer's end points, then configure a stream */
bt_a2dp_discover(a2dp, &ep_discover_param);
bt_a2dp_stream_config(a2dp, &sbc_stream, &sbc_source_ep, &sbc_sink_ep, &sbc_cfg_default);

/* 7. Open the media channel and start */
bt_a2dp_stream_establish(stream);
bt_a2dp_stream_start(&sbc_stream);

/* 8. Encode PCM to SBC and send one media packet per interval */
buf = bt_a2dp_stream_create_pdu(&a2dp_tx_pool, K_FOREVER);
out_size = sbc_encode(&encoder, pcm, net_buf_tail(buf));
*sbc_hdr = (uint8_t)BT_A2DP_SBC_MEDIA_HDR_ENCODE(frame_num, 0, 0, 0);
bt_a2dp_stream_send(&sbc_stream, buf, send_count, send_samples_count);
```

| Step                    | Sink (this app)                       | Source                                   |
| ----------------------- | ------------------------------------- | ---------------------------------------- |
| Discovery               | Inquiry scan (is found)               | Inquiry (searches)                       |
| Connection              | Page scan (is connected to)           | Page (connects)                          |
| SDP                     | Publishes Audio Sink                  | Publishes Audio Source, queries the peer |
| AVDTP                   | Acceptor: answers and accepts         | Initiator: discover, configure, open, start |
| Codec                   | SBC decoder                           | SBC encoder                              |
| Timing                  | Follows the incoming packets          | Sends on its own timer                   |

A board that is a source could play a test tone into wireless earbuds. That was
considered as a way to hear the result and not built.

## 9. After decoding: I2S, the codec and timing

```
 SBC frame -> decoder -> PCM (16 bit, 44.1 kHz) -> I2S queue -> DMA -> I2S pins -> ES8311 -> NS4150B -> speaker
```

-   **I2S** carries PCM over three signals: a bit clock (BCLK), a word clock
    (LRCK, the sample rate) and data. The codec also gets a master clock
    (MCLK, 256 times the sample rate here).
-   **DMA** moves each block from memory to the I2S hardware. The I2S driver
    names the request line of its peripheral to the DMA controller. On the S31
    that number differs from the other chips, which was the reason nothing
    played at first.
-   **ES8311** is the codec (DAC). It is configured over I2C and is an I2S
    slave. **NS4150B** is the amplifier, switched on by a GPIO.

### Why a jitter buffer is needed

The phone sends about 7 to 9 frames every 20 to 26 ms, and the radio is shared
with Wi-Fi, so packets arrive in uneven groups. The I2S output consumes one
frame every 2.9 ms without pause.

-   Queue too short: it runs empty during a gap (underrun) and overflows when
    packets arrive together. Both are audible.
-   The app starts the output only after some frames are queued, and the I2S
    driver waits up to 40 ms on an empty queue before it reports an error.

```c
/* A2dpSink.c (shortened) */
sQueuedFrames++;
if (!sOutputRunning && sQueuedFrames >= PLAYBACK_PRIME_FRAMES) {
	sOutputRunning = i2s_trigger(sI2s, I2S_DIR_TX, I2S_TRIGGER_START) == 0;
}
```

The sender's clock and the board's clock are never exactly equal. A product
corrects that slowly, for example by adjusting the sample rate. This app does
not.

## 10. Checking audio without a speaker

The audio tap sends each decoded frame to a host, which removes the codec and
amplifier from the test.

```c
/* A2dpSink.c: one datagram per frame */
sys_put_le32(sTapSequence++, &datagram[0]);   /* lets the host count lost frames */
sys_put_le32(sampleRate, &datagram[4]);
for (size_t i = 0; i < monoCount; i++) {
	sys_put_le16((uint16_t)stereo[2 * i], &datagram[TAP_HEADER_BYTES + 2 * i]);
}
zsock_sendto(sTapSocket, datagram, ..., ZSOCK_MSG_DONTWAIT, ...);
```

`capture_song.py` writes the samples to a WAV file and fills lost frames with
silence. The audio is about 88 KB per second, which Wi-Fi carries easily and
the 115200 baud console could not.

## 11. Terms

| Term      | Meaning |
| --------- | ------- |
| ACL       | The data link between two connected devices |
| AVDTP     | Audio/Video Distribution Transport Protocol: sets up and carries streams |
| A2DP      | Advanced Audio Distribution Profile: audio over AVDTP |
| BR/EDR    | Basic Rate / Enhanced Data Rate, the Classic radio |
| BTP       | Matter's transport over BLE GATT |
| CoD       | Class of Device, 24 bits describing the device type |
| EIR       | Extended Inquiry Response, up to 240 bytes in the inquiry answer |
| GAP/GATT  | BLE roles and procedures / BLE data model |
| HCI       | Host Controller Interface |
| L2CAP     | Channel multiplexing on an ACL link |
| LMP       | Link Manager Protocol, controller to controller (pairing, features) |
| PASE      | Matter passcode session (SPAKE2+) used during commissioning |
| PSM       | L2CAP channel number of a protocol |
| SBC       | Subband Codec, mandatory A2DP codec |
| SDP       | Service Discovery Protocol |
| SEP       | Stream End Point of AVDTP |
| SSP       | Secure Simple Pairing |
| VHCI      | Espressif's in-memory HCI transport |
