#!/usr/bin/env python3
"""Capture a whole song from the Matter speaker app's audio tap and play it.

The board sends the decoded audio in UDP datagrams: index of the first sample
(u32 LE), sample rate (u32 LE), then mono 16-bit little-endian samples.

The capture starts with the first datagram that is not silence and ends when no
audio has arrived for a while (the song ended or was paused), or on Ctrl+C.
Samples lost on the way are replaced with silence so the timing stays right.

Usage:
    python3 capture_song.py [output.wav] [--idle SECONDS] [--port PORT] [--no-play]
"""
import argparse
import array
import socket
import struct
import subprocess
import sys
import time
import wave

HEADER = struct.Struct("<II")
MAX_GAP_SAMPLES = 6 * 48000  # do not write more than ~6 s of silence for one gap


def is_silent(pcm: bytes) -> bool:
    samples = array.array("h")
    samples.frombytes(pcm[: len(pcm) & ~1])
    return max((abs(s) for s in samples), default=0) < 8


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("output", nargs="?", default="matter_speaker_song.wav")
    parser.add_argument("--idle", type=float, default=8.0,
                        help="stop after this many seconds without audio (default 8)")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--no-play", action="store_true", help="save the file without playing it")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(0.5)

    print(f"Listening on UDP port {args.port}.")
    print("Play a song to 'Matter Speaker'. Capture starts with the first sound")
    print(f"and stops after {args.idle:g} s without audio, or when you press Ctrl+C.")

    wav = None
    rate = 0
    expected = None
    lost = samples = skipped_silent = 0
    last_audio = None
    last_report = time.time()
    sender = None

    try:
        while True:
            try:
                data, sender = sock.recvfrom(4096)
            except socket.timeout:
                if wav is not None and time.time() - last_audio >= args.idle:
                    print(f"\nNo audio for {args.idle:g} s: the song has ended.")
                    break
                continue

            if len(data) <= HEADER.size:
                continue
            first_sample, frame_rate = HEADER.unpack_from(data)
            pcm = data[HEADER.size:]
            count = len(pcm) // 2
            now = time.time()

            if wav is None:
                # Phones open the stream with a few seconds of silence when they
                # connect. Wait for real sound before starting the file.
                if is_silent(pcm):
                    skipped_silent += 1
                    expected = first_sample + count
                    if now - last_report >= 5:
                        print("  stream is open but silent; waiting for sound...")
                        last_report = now
                    continue
                rate = frame_rate
                wav = wave.open(args.output, "wb")
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(rate)
                expected = first_sample
                last_report = now
                print(f"Sound detected from {sender[0]}: {rate} Hz, mono, 16-bit. Capturing...")

            if first_sample < expected:
                # The board restarted its counter (new stream): carry on from here.
                expected = first_sample
            gap = first_sample - expected
            if gap > 0:
                lost += gap
                fill = min(gap, MAX_GAP_SAMPLES)
                wav.writeframes(b"\x00\x00" * fill)
                samples += fill
            expected = first_sample + count

            wav.writeframes(pcm[: count * 2])
            samples += count
            last_audio = now

            if now - last_report >= 5:
                print(f"  {samples / rate:6.1f} s captured, {lost / rate:.1f} s lost", end="\r", flush=True)
                last_report = now
    except KeyboardInterrupt:
        print("\nStopped by Ctrl+C.")

    if wav is None:
        if skipped_silent:
            print("Only silence was received. Check the phone's media volume and that")
            print("its audio output is 'Matter Speaker', then run the script again.")
        else:
            print("Nothing was received. Check that the board and this Mac are on the")
            print("same network and that the board was built with this Mac's address.")
        return 1

    wav.close()
    duration = samples / rate
    print(f"Saved {args.output}: {int(duration // 60)} min {duration % 60:.1f} s, "
          f"{lost / rate:.1f} s lost ({100.0 * lost / max(samples, 1):.2f}%)")

    if not args.no_play:
        print("Playing (Ctrl+C to stop)...")
        try:
            subprocess.run(["afplay", args.output])
        except KeyboardInterrupt:
            print("\nPlayback stopped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
