# libmiplay

`libmiplay` is a small C11 receiver library for the Xiaomi MiPlay (妙播)
legacy control protocol. It follows the module/lifecycle style of
[`libraop`](https://github.com/philippe44/libraop), but it does not expose
AirPlay or DLNA semantics.

## Evidence and scope

The implementation was compared with local revisions of:

* `philippe44/libraop` (server lifecycle, callbacks, CMake/test-tool shape);
* `rosienosiesie/FusionPlay-Android`, especially `src/FusionPlay.MiPlaySdk`;
* `SUlTlUS/MiPlayForWindows`;
* `Para-Ecoli/miplay-bridge`, especially its `internal/miplay` package.

The mutually supported wire facts are:

* the service is `_mi-connect._udp.local.`;
* the advertisement uses an `appsData` base64 container and an advertised
  CoAP/SRV port of 56666;
* the legacy control connection is TCP port 8899;
* control frames are `0x24`, a big-endian command and sequence, a big-endian
  32-bit payload length, then payload;
* media frames use `0x24` followed by a three-byte big-endian payload length.

The source contains strict codecs for both frame formats and the legacy
challenge/session server. It deliberately does **not** claim full phone
compatibility: modern senders normally negotiate SafetyData and hand over a
reverse-WFD media endpoint. Those parts require device identity/crypto
behavior that is not publicly specified consistently by the references.
Compressed payloads are passed to the application; libmiplay does not invent
an AAC decoder.

## Build

On Linux:

```sh
sudo apt-get install build-essential cmake libssl-dev
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The shared and static libraries are `libmiplay.so` and `libmiplay.a`.
OpenSSL Crypto is required for the legacy MD5/HMAC-SHA1 challenge response;
pthread is required for the accept/session thread.

## API and ownership

Include `miplay.h`, call `miplay_server_create`, then `miplay_server_start`.
Stop before destroy. Callback invocations happen on the accept/session thread,
not the caller's thread. Callback data is borrowed and valid only during the
callback. The library copies configuration strings. `miplay_frame_encode`
allocates its result with `malloc`; callers release it with `free`.

`miplay_server_feed_media` is useful for an application-owned media transport
and for tests. The audio callback receives the payload without a codec
conversion. A real application should decode AAC/MPEG-TS according to its
media negotiation before writing PCM.

## Test tool

```sh
./build/miplay-test -p 8899 -o capture.bin -n "Living Room"
```

The tool prints connection, authentication, session, and error events and
stops cleanly with Ctrl-C. `capture.bin` contains media payloads only when an
application feeds complete media frames through the API. A real MiPlay sender
(Android/HyperOS or a compatible Windows sender) is needed for end-to-end
validation; this repository does not treat the offline tests as proof of
device compatibility.

## Relationship to libraop

The lifecycle is intentionally familiar: create/start, asynchronous callbacks,
stop/destroy, bounded network input, and an example receiver. There is no
source-level AirPlay compatibility layer: RTSP, RTP, ALAC, NTP synchronization,
AirPlay pairing, metadata plist handling, and AirPlay volume semantics are
not part of this API. A migration should replace the RAOP server object with a
`miplay_server_t`, map RAOP stream events to `MIPLAY_EVENT_AUDIO`, and move
codec/decryption policy into the caller.

## Known limitations and follow-up

功能：mDNS responder and full discovery announcements
当前状态：未实现；`miplay_apps_data` only builds the verified TXT payload.
无法实现或未确认的原因：the references disagree on discovery timing and
network-interface behavior; a responder needs interface and collision handling.
参考依据：FusionPlay `discovery.rs` and bridge `internal/miplay/mdns.go`.
对上层调用者的影响：the receiver is not automatically visible in a phone
picker.
后续实现建议：add a platform-neutral mDNS responder or inject an existing
Bonjour/Avahi service.

功能：SafetyData secure channel and reverse-WFD media transport
当前状态：未实现。
无法实现或未确认的原因：modern authentication and device trust material are
not stable/public in the references; guessing fields would be unsafe.
参考依据：FusionPlay `protocol.rs`/`media.rs` and bridge `control.go`.
对上层调用者的影响：modern senders may disconnect after legacy
authentication, and no automatic audio socket is opened.
后续实现建议：capture a consenting device exchange, implement the negotiated
state machine with test vectors, then add a separate transport module.

功能：AAC/PCM decoding and hardware audio output
当前状态：未实现；media bytes are delivered unchanged.
无法实现或未确认的原因：codec parameters are negotiated per media session and
the C project has no codec dependency.
参考依据：FusionPlay uses an AAC decoder and the bridge forwards a validated
stream to its audio pipeline.
对上层调用者的影响：the caller must decode and select an output device.
后续实现建议：add optional FFmpeg/fdk-aac integration behind a CMake option.

## Validation

The offline protocol test checks valid/partial framing, invalid magic, and
three-byte media lengths. No real Android or Windows sender was available in
this environment, so real-device compatibility remains unverified.