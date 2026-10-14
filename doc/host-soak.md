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

The October 14, 2026 regression completed 240000 ticks in 396.96 seconds with
1712 dialogue additions, five choice menus and 189 saved captures. Four exact
dialogue targets were identified by complete MSB glyph sequences and captured
after 240 settling ticks in a separate run. All four complete RGB images were
identical to the accepted PC baseline. Other saved stable and choice images
received structural checks, without claiming OCR or all-frame coverage.

Movie opening now handles a null codec descriptor, unavailable decoder and
codec-open failure before creating worker threads or dereferencing streams.
Failed video initialization closes the input and leaves the player stopped;
movie VM flags reflect that state so scripts can continue. In this build,
`EFF_BG15B.bk2` is Bink 1 (`BIKi`) and opens successfully, while `OP01.bk2` is
Bink 2 (`KB2j`) and has no usable FFmpeg decoder. Its cancellation is logged;
the regression does not establish Bink 2 playback support.

Host movie audio initialization safely releases both the sample storage and
pointer array, including before their first allocation. Conversion staging is
stored as floats, matching the sample counts used by resizing and FIFO output.
A conversion regression exercised three rounds of initialization, 1024 stereo
frames with distinct channel values, exact sample readback and repeated unload.

Local evidence and reproducible test sources are preserved under
`/tmp/opencode/round10/`, with the final run in `long-final/`, exact dialogue in
`exact-final/`, and the original crashes in `movie-before/gdb.log` and
`op-before-gdb.log`. The evidence directory contains source snapshots and a
manifest; game assets and screenshots are not added to this repository.
