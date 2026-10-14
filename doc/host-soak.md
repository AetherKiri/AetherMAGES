# Embedded host soak runs

The host normally updates, renders and captures every tick. For unattended
acceptance runs, `IMPACTO_HOST_RENDER_INTERVAL=N` keeps every update and renders
once per N ticks. Valid values are 1 through 120; omitted or invalid values use
1. This option is intended for testing. Asset scheduling and wall-clock movie
playback mean that tick numbers are not exact dialogue identifiers.

With an interval greater than 1, `impacto_host_read_frame_rgba` synchronously
renders any pending state on the engine thread before returning the frame.
Captures retain the configured resolution. Consumers that read every tick
still render every tick; clients must also reduce their read frequency to gain
speed. The host ABI is unchanged.

MO8 acceptance used 1920x1080, interval 60, a read every 60 ticks, normal Return
press/release every 10 ticks, and the first focused choice. Input was released
while capturing choices and settling dialogue. The driver drained the audio
FIFO each tick. Ctrl is available for ForceSkip, but the accepted run did not
hold it. All settling pauses counted toward the 240000 tick budget.

The round 10 regression on October 14, 2026 completed 240000 ticks in 396.96 seconds with
1712 dialogue additions, five choice menus and 189 saved captures. Four exact
dialogue targets were identified by complete MSB glyph sequences and captured
after 240 settling ticks in a separate run. All four complete RGB images were
identical to the accepted PC baseline. Other saved stable and choice images
received structural checks, without claiming OCR or all-frame coverage.
That run cancelled unsupported Bink 2 playback. Round 11 uses the decoder
overlay below and tests OP01 through to natural completion.

Movie opening now handles a null codec descriptor, unavailable decoder and
codec-open failure before creating worker threads or dereferencing streams.
Failed video initialization closes the input and leaves the player stopped;
movie VM flags reflect that state so scripts can continue. The dependency
overlay now uses FFmpeg 7.1.2 revision 6 with Librempeg's Bink 2 decoder and the
existing Bink 1 video/audio decoders. The manifest enables `bink2`, `gpl`, and
`version3`. See [`README.bink2.md`](../portfiles/ffmpeg/README.bink2.md) for the
pinned source, license, and supported audio path. The host installer resolves
dependencies from the build library so an earlier install's same-SONAME FFmpeg
cannot shadow the overlay.

`IMPACTO_MOVIE_TRACE=1` adds visible YUV hashes and audio clock positions every
150 presented frames. Start/end/stop events include the file and frame count;
start also records whether playback loops. The MO8 driver keeps normal input
for looping background movies, and releases Return and paces ticks during a
non-looping movie. Saved movie captures retain 1920x1080. A decoder hash check
and a framebuffer check measure different parts of the playback path.

Movie audio is mixed into the same per-tick stereo buffer as other channels,
with master and movie volume applied. Converted samples are retained across
ticks, and the resampler drains at EOF. Natural movie completion waits for the
remaining movie audio. Seeking stops and joins all decoder/reader threads
before resetting codecs and queues, then restarts them with consistent packet
serials. Decoder threads sleep when their input queues are empty.

Build the reproducible probes with:

```sh
bash tools/host/build_probes.sh "$PWD/host-build" "$PWD/host-install" /tmp/opencode/probes
/tmp/opencode/probes/bink_decode_probe /absolute/path/to/OP01.bk2 /absolute/capture/directory
/tmp/opencode/probes/mo8_audio_conversion_test
```

The decode probe checks the complete declared video frame count, audio sample
count and finiteness, and selected visible YUV hashes; it exports original-size
RGB PPM images when supplied an existing capture directory. Silence is valid
for effect movies. No proprietary assets are distributed. These checks do not
establish pixel equality with the proprietary RAD decoder.

Host movie audio initialization safely releases both the sample storage and
pointer array, including before their first allocation. Conversion staging is
stored as floats, matching the sample counts used by resizing and FIFO output.
A conversion regression exercised three rounds of initialization, 1024 stereo
frames with distinct channel values, exact sample readback, half-volume output,
retained samples across ticks and repeated unload. A separate one-second
48000-to-44100 test checks exact stereo output added to an existing channel mix
and resampler EOF drain.

Round 11's standalone probes decoded all declared frames of OP01 and OP02
(3750 each), fd_kokuchi (539), EFF_NOISE (45), and the Bink 1 EFF_BG15B (136),
with finite audio. OP01 produced 12000000 input audio samples; engine playback
mixes 5512500 stereo frames at 44100Hz before natural EOF. The movie acceptance
script compares 26 selected visible YUV hashes and four original-size RGB
captures against independent software decoding, checks audio/video clock
differences within 100ms, verifies a complete looping background movie and
requires dialogue after OP01. RGB comparisons allow shader/chroma interpolation
rounding (MAE at most 3/255 and at least 95% of pixels within 10/255).

The final staged runtime completed 240000 ticks in 803.03 seconds, reaching
1833 dialogue additions and five confirmed first-option choices. OP01 presented
all 3750 frames, drained 5512500 stereo audio frames, and continued to 890 later
dialogue additions. All 26 selected YUV hashes matched; the largest sampled
audio/video clock difference was 63856 microseconds. Nine background loop ends
completed without the earlier seek race. OP01's start-to-end log interval was
187 seconds for its 125.005-second media duration under concurrent software
rendering and readback. These checks establish complete, synchronized media
progression; they do not establish real-time playback speed on this test setup.
The final image acceptance passed all 227 saved native captures (100 progress,
82 stable dialogue, 40 movie and five choice images). Each of the five menus
confirmed option 0 and continued dialogue; no decode/seek error or logged
progress blocker was found. All four exact dialogue targets had whole-image
MAE 0 and exact pixel fraction 1. Original-size PNG conversion preserved every
source RGB pixel. Structural checks cover these saved images, not every frame
of every route.

Stable-image checks follow the logged dialogue mode. Mode 0 uses the bottom
ADV text area; mode 1 uses the full-screen narration area derived from
`_system.scx`'s mode format (WindowPos 123,70 and MaxLineWidth 1034 at 720p).
Unknown modes fail instead of silently receiving an ADV check. Four exact
dialogue captures on the final runtime remain pixel-identical to round 8's
accepted native RGB baseline. The source and commands are in
[`tools/host/README.md`](../tools/host/README.md).

Local round 10 evidence and reproducible test sources are preserved under
`/tmp/opencode/round10/`, with the final run in `long-final/`, exact dialogue in
`exact-final/`, and the original crashes in `movie-before/gdb.log` and
`op-before-gdb.log`. The evidence directory contains source snapshots and a
manifest; game assets and screenshots are not added to this repository.
Round 11 evidence is under `/tmp/opencode/round11/`: the final runtime runs in
`story-delivery2/` and `exact-delivery/`, full decoder logs in `decode-final/`,
independent RGB in `decode-op01/`, the seek race stack in
`crash-gdb2/backtrace.log`, dependency-install checks and a source/binary/asset
hash manifest. Earlier failures and the original ADV-only check failure remain
available. Software-rendered soak wall time includes readback, logging and
concurrent runs; it is not a playback performance benchmark.
