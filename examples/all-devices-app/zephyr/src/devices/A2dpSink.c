/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "devices/A2dpSink.h"

#include <stdbool.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

/* Audio is played when the board has an I2S output (i2s-codec-tx alias) wired to a
 * codec (audio_codec node label). Without them the media packets are only counted.
 */
#if DT_HAS_ALIAS(i2s_codec_tx) && DT_NODE_EXISTS(DT_NODELABEL(audio_codec)) &&                     \
	IS_ENABLED(CONFIG_I2S) && IS_ENABLED(CONFIG_AUDIO_CODEC)
#define A2DP_SINK_HAS_PLAYBACK 1
#include <zephyr/audio/codec.h>
#include <zephyr/bluetooth/sbc.h>
#include <zephyr/drivers/i2s.h>
#else
#define A2DP_SINK_HAS_PLAYBACK 0
#endif

LOG_MODULE_REGISTER(a2dp_sink, LOG_LEVEL_INF);

/* TEMPORARY DIAGNOSTIC, only built with the scratch bit-banged I2C overlay: logs the
 * levels read on the codec's I2C pins under different pin configurations.
 */
#if DT_NODE_EXISTS(DT_NODELABEL(i2c_bitbang))
#include <zephyr/drivers/gpio.h>

static void LogCodecBusPinLevels(void)
{
	static const struct gpio_dt_spec scl =
		GPIO_DT_SPEC_GET(DT_NODELABEL(i2c_bitbang), scl_gpios);
	static const struct gpio_dt_spec sda =
		GPIO_DT_SPEC_GET(DT_NODELABEL(i2c_bitbang), sda_gpios);
	static const struct {
		const char *name;
		gpio_flags_t flags;
	} steps[] = {
		{"input, pull-up", GPIO_INPUT | GPIO_PULL_UP},
		{"input, pull-down", GPIO_INPUT | GPIO_PULL_DOWN},
		{"input, no pull", GPIO_INPUT},
		{"driven high", GPIO_INPUT | GPIO_OUTPUT_HIGH},
		{"driven low", GPIO_INPUT | GPIO_OUTPUT_LOW},
		{"open drain released, pull-up",
		 GPIO_INPUT | GPIO_OUTPUT_HIGH | GPIO_OPEN_DRAIN | GPIO_PULL_UP},
	};

	ARRAY_FOR_EACH(steps, i) {
		int sclErr = gpio_pin_configure(scl.port, scl.pin, steps[i].flags);
		int sdaErr = gpio_pin_configure(sda.port, sda.pin, steps[i].flags);

		k_msleep(5);
		LOG_INF("I2C pins %s: SCL=%d SDA=%d (configure %d/%d)", steps[i].name,
			gpio_pin_get_raw(scl.port, scl.pin), gpio_pin_get_raw(sda.port, sda.pin),
			sclErr, sdaErr);
	}
}
#else
static void LogCodecBusPinLevels(void) {}
#endif

#define A2DP_VERSION 0x0104
#define STREAM_REPORT_PERIOD_MS 5000

#define MAX_VOLUME 254

BT_A2DP_SBC_SINK_EP_DEFAULT(sSbcSinkEndpoint);
BT_A2DP_SBC_EP_CFG_DEFAULT(sSbcConfig, A2DP_SBC_SAMP_FREQ_44100);

static struct bt_a2dp_stream sSbcStream;
static atomic_t sVolume = ATOMIC_INIT(MAX_VOLUME);
static bool sStarted;

#if IS_ENABLED(CONFIG_ALL_DEVICES_SPEAKER_AUDIO_UDP_TAP)
#include <zephyr/net/socket.h>

#define TAP_HEADER_BYTES 8
#define TAP_MAX_SAMPLES 128

/* Written and read on the Bluetooth RX thread only */
static int sTapSocket = -1;
static struct sockaddr_in sTapHost;
static uint32_t sTapSequence;
static uint32_t sTapSent;
static uint32_t sTapFailed;

static void OpenTap(void)
{
	if (sTapSocket >= 0) {
		return;
	}

	sTapHost.sin_family = AF_INET;
	sTapHost.sin_port = htons(CONFIG_ALL_DEVICES_SPEAKER_AUDIO_UDP_TAP_PORT);
	if (zsock_inet_pton(AF_INET, CONFIG_ALL_DEVICES_SPEAKER_AUDIO_UDP_TAP_HOST,
			    &sTapHost.sin_addr) != 1) {
		LOG_ERR("Audio tap host address is not a valid IPv4 address");
		return;
	}

	sTapSocket = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sTapSocket < 0) {
		LOG_ERR("Audio tap socket failed: %d", errno);
		return;
	}

	LOG_INF("Audio tap sending to %s:%d", CONFIG_ALL_DEVICES_SPEAKER_AUDIO_UDP_TAP_HOST,
		CONFIG_ALL_DEVICES_SPEAKER_AUDIO_UDP_TAP_PORT);
}

/* Sends the left channel of one decoded stereo frame */
static void TapPcmFrame(const int16_t *stereo, size_t sampleCount, uint32_t sampleRate)
{
	static uint8_t datagram[TAP_HEADER_BYTES + TAP_MAX_SAMPLES * sizeof(int16_t)];
	const size_t monoCount = MIN(sampleCount / 2, TAP_MAX_SAMPLES);

	if (sTapSocket < 0) {
		return;
	}

	sys_put_le32(sTapSequence++, &datagram[0]);
	sys_put_le32(sampleRate, &datagram[4]);
	for (size_t i = 0; i < monoCount; i++) {
		sys_put_le16((uint16_t)stereo[2 * i], &datagram[TAP_HEADER_BYTES + 2 * i]);
	}

	if (zsock_sendto(sTapSocket, datagram, TAP_HEADER_BYTES + monoCount * sizeof(int16_t),
			 ZSOCK_MSG_DONTWAIT, (struct sockaddr *)&sTapHost, sizeof(sTapHost)) < 0) {
		sTapFailed++;
	} else {
		sTapSent++;
	}
}

static void ReportTap(void)
{
	LOG_INF("Audio tap: %u frames sent, %u not sent", sTapSent, sTapFailed);
	sTapSent = 0;
	sTapFailed = 0;
}
#else
static void OpenTap(void) {}
static void TapPcmFrame(const int16_t *stereo, size_t sampleCount, uint32_t sampleRate)
{
	ARG_UNUSED(stereo);
	ARG_UNUSED(sampleCount);
	ARG_UNUSED(sampleRate);
}
static void ReportTap(void) {}
#endif /* CONFIG_ALL_DEVICES_SPEAKER_AUDIO_UDP_TAP */

#if A2DP_SINK_HAS_PLAYBACK

/* One SBC frame decodes to at most 128 stereo 16-bit samples */
#define PCM_FRAME_BYTES 512
#define PCM_CHANNELS 2
#define PCM_BITS 16
#define CODEC_MCLK_MULTIPLE 256
/* Frames queued before the I2S output starts, to absorb radio jitter */
#define PLAYBACK_PRIME_FRAMES (CONFIG_I2S_ESP32_TX_BLOCK_COUNT * 2 / 5)
/* The codec volume is in 0.5 dB steps; level 1 maps to -48 dB */
#define CODEC_MIN_VOLUME_HALF_DB (-96)

K_MEM_SLAB_DEFINE_STATIC(sPcmSlab, PCM_FRAME_BYTES, CONFIG_I2S_ESP32_TX_BLOCK_COUNT + 2, 4);

static const struct device *const sI2s = DEVICE_DT_GET(DT_ALIAS(i2s_codec_tx));
static const struct device *const sCodec = DEVICE_DT_GET(DT_NODELABEL(audio_codec));

/* Written and read on the Bluetooth RX thread only */
static struct sbc_decoder sDecoder;
static int16_t sPcmFrame[PCM_FRAME_BYTES / sizeof(int16_t)];
static uint32_t sQueuedFrames;
static uint32_t sPlayedFrames;
static uint32_t sDroppedFrames;
static uint32_t sOutputRestarts;
static int sLastWriteError;
static uint32_t sSampleRate;
static bool sOutputConfigured;
static bool sCodecConfigured;
static bool sCodecAttempted;
static bool sOutputRunning;

static void ApplyCodecVolume(void)
{
	const uint8_t volume = (uint8_t)atomic_get(&sVolume);
	audio_property_value_t value;

	if (!sCodecConfigured) {
		return;
	}

	value.mute = volume == 0;
	(void)audio_codec_set_property(sCodec, AUDIO_PROPERTY_OUTPUT_MUTE, AUDIO_CHANNEL_ALL,
				       value);
	if (volume == 0) {
		return;
	}

	value.vol = CODEC_MIN_VOLUME_HALF_DB * (MAX_VOLUME - volume) / (MAX_VOLUME - 1);
	(void)audio_codec_set_property(sCodec, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL,
				       value);
}

static void ConfigureCodec(void)
{
	struct audio_codec_cfg codec = {
		.mclk_freq = sSampleRate * CODEC_MCLK_MULTIPLE,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_route = AUDIO_ROUTE_PLAYBACK,
		.dai_cfg.i2s = {
			.word_size = PCM_BITS,
			.channels = PCM_CHANNELS,
			.format = I2S_FMT_DATA_FORMAT_I2S,
			.options = I2S_OPT_BIT_CLK_TARGET | I2S_OPT_FRAME_CLK_TARGET,
			.frame_clk_freq = sSampleRate,
		},
	};
	int err;

	sCodecAttempted = true;
	err = audio_codec_configure(sCodec, &codec);
	if (err != 0) {
		LOG_ERR("Audio codec configuration failed: %d", err);
		return;
	}

	sCodecConfigured = true;
	ApplyCodecVolume();
	audio_codec_start_output(sCodec);
}

static void ConfigurePlayback(uint32_t sampleRate)
{
	struct i2s_config i2s = {
		.word_size = PCM_BITS,
		.channels = PCM_CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER,
		.frame_clk_freq = sampleRate,
		.mem_slab = &sPcmSlab,
		.block_size = PCM_FRAME_BYTES,
		.timeout = 0,
	};
	int err;

	sOutputConfigured = false;
	sCodecConfigured = false;
	sSampleRate = sampleRate;

	if (!device_is_ready(sI2s) || !device_is_ready(sCodec)) {
		LOG_ERR("I2S output or audio codec not ready");
		return;
	}

	err = i2s_configure(sI2s, I2S_DIR_TX, &i2s);
	if (err != 0) {
		LOG_ERR("I2S configuration failed: %d", err);
		return;
	}

	sOutputConfigured = true;
}

static void StartPlayback(void)
{
	if (!sOutputConfigured) {
		return;
	}

	sbc_setup_decoder(&sDecoder);
	sQueuedFrames = 0;
	sOutputRunning = false;
	sCodecAttempted = false;
	OpenTap();
}

static void StopPlayback(void)
{
	if (!sOutputConfigured) {
		return;
	}

	if (sCodecConfigured) {
		audio_codec_stop_output(sCodec);
	}
	(void)i2s_trigger(sI2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
	sOutputRunning = false;
	sCodecConfigured = false;
}

/* The codec is mono, so both channels carry the average of left and right */
static void DownmixToMono(int16_t *samples, size_t sampleCount)
{
	for (size_t i = 0; i + 1 < sampleCount; i += PCM_CHANNELS) {
		const int16_t mono = (int16_t)(((int32_t)samples[i] + samples[i + 1]) / 2);

		samples[i] = mono;
		samples[i + 1] = mono;
	}
}

static void QueuePcmFrame(uint32_t bytes)
{
	int err = i2s_buf_write(sI2s, sPcmFrame, bytes);

	if (err == -EIO) {
		/* The output ran dry and stopped. Clear the error and fill the queue again. */
		(void)i2s_trigger(sI2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
		sOutputRunning = false;
		sQueuedFrames = 0;
		sOutputRestarts++;
		err = i2s_buf_write(sI2s, sPcmFrame, bytes);
	}
	if (err != 0) {
		sDroppedFrames++;
		sLastWriteError = err;
		return;
	}

	sQueuedFrames++;
	sPlayedFrames++;
	if (!sOutputRunning && sQueuedFrames >= PLAYBACK_PRIME_FRAMES) {
		sOutputRunning = i2s_trigger(sI2s, I2S_DIR_TX, I2S_TRIGGER_START) == 0;
	}

	/* The codec is set up once the I2S controller is running, so that it has MCLK */
	if (sOutputRunning && !sCodecAttempted) {
		ConfigureCodec();
	}
}

static void PlayMediaPacket(struct net_buf *buf)
{
	const void *in;
	uint32_t inSize;
	uint8_t frameCount;

	if (!sOutputConfigured || buf->len < 1) {
		return;
	}

	frameCount = BT_A2DP_SBC_MEDIA_HDR_NUM_FRAMES_GET(net_buf_pull_u8(buf));
	in = buf->data;
	inSize = buf->len;

	for (uint8_t i = 0; i < frameCount; i++) {
		uint32_t outSize = sizeof(sPcmFrame);

		if (sbc_decode(&sDecoder, &in, &inSize, sPcmFrame, &outSize) != 0) {
			sDroppedFrames += frameCount - i;
			return;
		}
		DownmixToMono(sPcmFrame, outSize / sizeof(int16_t));
		TapPcmFrame(sPcmFrame, outSize / sizeof(int16_t), sSampleRate);
		QueuePcmFrame(outSize);
	}
}

/* A frame is accepted only while the I2S output drains its queue, so frames played
 * without drops show that the output is clocking audio out at the sample rate.
 */
static void ReportPlayback(void)
{
	LOG_INF("A2DP playback: %u frames played, %u dropped (last error %d), %u output restarts, "
		"codec %s",
		sPlayedFrames, sDroppedFrames, sLastWriteError, sOutputRestarts,
		sCodecConfigured ? "ok" : "failed");
	sPlayedFrames = 0;
	sDroppedFrames = 0;
	sOutputRestarts = 0;
}

#else /* !A2DP_SINK_HAS_PLAYBACK */

static void ApplyCodecVolume(void) {}
static void ConfigurePlayback(uint32_t sampleRate)
{
	ARG_UNUSED(sampleRate);
}
static void StartPlayback(void) {}
static void StopPlayback(void) {}
static void ReportPlayback(void) {}
static void PlayMediaPacket(struct net_buf *buf)
{
	ARG_UNUSED(buf);
}

#endif /* A2DP_SINK_HAS_PLAYBACK */

/* Written and read on the Bluetooth RX thread only */
static uint32_t sMediaPackets;
static uint32_t sMediaBytes;
static int64_t sReportStartMs;

static struct bt_sdp_attribute sAudioSinkAttributes[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_AUDIO_SINK_SVCLASS)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 16),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL)
			},
			)
		},
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_UUID_AVDTP_VAL)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(AVDTP_VERSION)
			},
			)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_ADVANCED_AUDIO_SVCLASS)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(A2DP_VERSION)
			},
			)
		},
		)
	),
	BT_SDP_SERVICE_NAME("A2DPSink"),
	BT_SDP_SUPPORTED_FEATURES(0x0001U),
};

static struct bt_sdp_record sAudioSinkRecord = BT_SDP_RECORD(sAudioSinkAttributes);

static void ResetStreamReport(void)
{
	sMediaPackets = 0;
	sMediaBytes = 0;
	sReportStartMs = k_uptime_get();
}

static void StreamConfigured(struct bt_a2dp_stream *stream)
{
	struct bt_a2dp_codec_sbc_params *sbc =
		(struct bt_a2dp_codec_sbc_params *)&sSbcConfig.codec_config->codec_ie[0];

	ARG_UNUSED(stream);
	LOG_INF("A2DP stream configured: %u Hz, %u channel(s)",
		bt_a2dp_sbc_get_sampling_frequency(sbc), bt_a2dp_sbc_get_channel_num(sbc));
	ConfigurePlayback(bt_a2dp_sbc_get_sampling_frequency(sbc));
}

static void StreamEstablished(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP stream established");
}

static void StreamReleased(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	StopPlayback();
	LOG_INF("A2DP stream released");
}

static void StreamStarted(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	ResetStreamReport();
	StartPlayback();
	LOG_INF("A2DP stream started");
}

static void StreamSuspended(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	StopPlayback();
	LOG_INF("A2DP stream suspended");
}

/* The periodic report shows that audio is arriving and which volume the Matter
 * Speaker endpoint currently requests.
 */
static void StreamReceived(struct bt_a2dp_stream *stream, struct net_buf *buf, uint16_t seq_num,
			   uint32_t ts)
{
	int64_t elapsedMs;

	ARG_UNUSED(stream);
	ARG_UNUSED(seq_num);
	ARG_UNUSED(ts);

	sMediaPackets++;
	sMediaBytes += buf->len;
	PlayMediaPacket(buf);

	elapsedMs = k_uptime_get() - sReportStartMs;
	if (elapsedMs >= STREAM_REPORT_PERIOD_MS) {
		LOG_INF("A2DP audio: %u packets, %u bytes in %u ms, volume %u", sMediaPackets,
			sMediaBytes, (uint32_t)elapsedMs, (uint32_t)atomic_get(&sVolume));
		ReportPlayback();
		ReportTap();
		ResetStreamReport();
	}
}

static struct bt_a2dp_stream_ops sStreamOps = {
	.configured = StreamConfigured,
	.established = StreamEstablished,
	.released = StreamReleased,
	.started = StreamStarted,
	.suspended = StreamSuspended,
	.recv = StreamReceived,
};

static void A2dpConnected(struct bt_a2dp *a2dp, int err)
{
	ARG_UNUSED(a2dp);

	if (err != 0) {
		LOG_ERR("A2DP connection failed: %d", err);
		return;
	}
	LOG_INF("A2DP connected");
}

static void A2dpDisconnected(struct bt_a2dp *a2dp)
{
	ARG_UNUSED(a2dp);
	LOG_INF("A2DP disconnected");
}

static int A2dpConfigRequested(struct bt_a2dp *a2dp, struct bt_a2dp_ep *ep,
			       struct bt_a2dp_codec_cfg *codec_cfg, struct bt_a2dp_stream **stream,
			       uint8_t *rsp_err_code)
{
	ARG_UNUSED(a2dp);
	ARG_UNUSED(ep);

	*sSbcConfig.codec_config = *codec_cfg->codec_config;
	bt_a2dp_stream_cb_register(&sSbcStream, &sStreamOps);
	*stream = &sSbcStream;
	*rsp_err_code = 0;

	return 0;
}

static struct bt_a2dp_cb sA2dpCallbacks = {
	.connected = A2dpConnected,
	.disconnected = A2dpDisconnected,
	.config_req = A2dpConfigRequested,
};

/* Puts the device name and the Audio Sink service into the inquiry response, so
 * that a phone can list the device by name without a separate name request.
 */
static int WriteInquiryResponse(void)
{
	static const uint8_t sServiceUuids[] = { BT_UUID_16_ENCODE(BT_SDP_AUDIO_SINK_SVCLASS) };
	const char *name = bt_get_name();
	const struct bt_data eir[] = {
		BT_DATA(BT_DATA_NAME_COMPLETE, name, strlen(name)),
		BT_DATA(BT_DATA_UUID16_ALL, sServiceUuids, sizeof(sServiceUuids)),
	};

	return bt_br_write_eir(eir, ARRAY_SIZE(eir), false);
}

/* Interlaced scanning listens on two frequencies per window, which shortens the time a
 * phone needs to find and to connect to the device while Wi-Fi shares the radio.
 */
#define SCAN_INTERVAL 0x0400 /* 640 ms */
#define SCAN_WINDOW 0x0024   /* 22.5 ms */
#define SCAN_TYPE_INTERLACED 0x01

static int WriteScanTiming(uint16_t activityOpcode, uint16_t typeOpcode)
{
	struct bt_hci_cp_write_page_scan_activity *activity;
	struct net_buf *buf;
	int err;

	buf = bt_hci_cmd_alloc(K_FOREVER);
	if (buf == NULL) {
		return -ENOBUFS;
	}
	/* Write Page Scan Activity and Write Inquiry Scan Activity take the same parameters */
	activity = net_buf_add(buf, sizeof(*activity));
	activity->interval = sys_cpu_to_le16(SCAN_INTERVAL);
	activity->window = sys_cpu_to_le16(SCAN_WINDOW);
	err = bt_hci_cmd_send_sync(activityOpcode, buf, NULL);
	if (err != 0) {
		return err;
	}

	buf = bt_hci_cmd_alloc(K_FOREVER);
	if (buf == NULL) {
		return -ENOBUFS;
	}
	net_buf_add_u8(buf, SCAN_TYPE_INTERLACED);

	return bt_hci_cmd_send_sync(typeOpcode, buf, NULL);
}

int A2dpSinkStart(void)
{
	int err;

	if (sStarted) {
		return 0;
	}

	err = bt_sdp_register_service(&sAudioSinkRecord);
	if (err != 0) {
		LOG_ERR("Audio Sink SDP record registration failed: %d", err);
		return err;
	}

	err = bt_a2dp_register_ep(&sSbcSinkEndpoint, BT_AVDTP_AUDIO, BT_AVDTP_SINK);
	if (err != 0) {
		LOG_ERR("SBC sink endpoint registration failed: %d", err);
		return err;
	}

	err = bt_a2dp_register_cb(&sA2dpCallbacks);
	if (err != 0) {
		LOG_ERR("A2DP callback registration failed: %d", err);
		return err;
	}

	err = WriteInquiryResponse();
	if (err != 0) {
		LOG_ERR("BR/EDR inquiry response write failed: %d", err);
		return err;
	}

	err = WriteScanTiming(BT_HCI_OP_WRITE_INQUIRY_SCAN_ACTIVITY,
			      BT_HCI_OP_WRITE_INQUIRY_SCAN_TYPE);
	if (err == 0) {
		err = WriteScanTiming(BT_HCI_OP_WRITE_PAGE_SCAN_ACTIVITY,
				      BT_HCI_OP_WRITE_PAGE_SCAN_TYPE);
	}
	if (err != 0) {
		/* The device stays usable with the controller's default scan timing */
		LOG_WRN("BR/EDR scan timing not changed: %d", err);
	}

	err = bt_br_set_connectable(true, NULL);
	if (err != 0) {
		LOG_ERR("BR/EDR set connectable failed: %d", err);
		return err;
	}

	err = bt_br_set_discoverable(true, false);
	if (err != 0) {
		LOG_ERR("BR/EDR set discoverable failed: %d", err);
		return err;
	}

	sStarted = true;
	LogCodecBusPinLevels();
	LOG_INF("A2DP sink discoverable as \"%s\"", bt_get_name());

	return 0;
}

void A2dpSinkSetVolume(uint8_t volume)
{
	atomic_set(&sVolume, MIN(volume, MAX_VOLUME));
	ApplyCodecVolume();
}
