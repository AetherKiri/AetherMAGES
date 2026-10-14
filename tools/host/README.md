# MO8 host regression probes

Build against an existing Release host build and install:

```sh
bash tools/host/build_probes.sh "$PWD/host-build" "$PWD/host-install" /tmp/opencode/probes
/tmp/opencode/probes/mo8_audio_conversion_test
```

The audio probe checks exact stereo samples, retained data across ticks, volume,
48000-to-44100 resampling, EOF drain and addition to an existing channel mix.
The lifecycle probe opens Bink 1, Bink 2 and Bink 1 again through the real VFS,
then stops each without concurrent ticks or GL calls:

```sh
/tmp/opencode/probes/mo8_movie_probe "$PWD/host-install" /absolute/writable /absolute/game 10
```

Run the real story with normal Return input and first-focused choices. Use new
output and writable directories for each run. The supervisor writes stdout to
the same `run.log` that the driver reads, records exit status and preserves a
GDB stack for a log-progress timeout. Looping background movies allow dialogue
input; non-looping movies release Return and pace ticks to 16ms including work.
No ForceSkip is used unless `IMPACTO_SOAK_FORCE_SKIP` is explicitly set.

```sh
mkdir -p /tmp/opencode/soak /tmp/opencode/soak-w
IMPACTO_HOST_RENDER_INTERVAL=60 IMPACTO_MOVIE_TRACE=1 IMPACTO_TEXT_TRACE=1 \
IMPACTO_CHOICE_TRACE=1 IMPACTO_PORTRAIT_TRACE=1 \
python3 tools/host/mo8_soak_supervisor.py /tmp/opencode/soak \
  /tmp/opencode/probes/mo8_long_run "$PWD/host-install" /tmp/opencode/soak-w \
  /absolute/game /tmp/opencode/soak 240000 1920 /tmp/opencode/soak/run.log
```

Supply your own game files to the standalone decoder probe. It checks the
complete declared frame count and finite audio, with selected visible YUV hashes
and optional native RGB captures. Silence is legal for an effect movie.

```sh
mkdir -p /tmp/opencode/decode
/tmp/opencode/probes/bink_decode_probe /absolute/game/movie1080/OP01.bk2 \
  /tmp/opencode/decode > /tmp/opencode/decode/run.log 2>&1
```

The Python image checks require Pillow and NumPy. `mo8_movie_acceptance.py`
compares the 26 selected OP01 hashes, all 3750 presented frames, natural EOF,
125 seconds of output audio, clock differences, looping-background evidence,
four decoded RGB captures and dialogue after playback:

```sh
python3 tools/host/mo8_movie_acceptance.py /tmp/opencode/soak \
  /tmp/opencode/decode/run.log /tmp/opencode/decode
```

For dialogue regressions, `mo8_visual_test` accepts data, writable, game root,
game id, output, progression ticks, width and final-input tick. Set
`IMPACTO_REVIEW_LOG` to its redirected stdout file, and
`IMPACTO_REVIEW_TARGETS` to four lines of `label<TAB>complete AddString glyph
signature`. Targets are identified by glyphs rather than tick numbers. The
driver releases Return for 240 ticks and captures each full image. Then run:

```sh
python3 tools/host/mo8_automated_acceptance.py /tmp/opencode/soak \
  /absolute/exact-captures --baseline /absolute/accepted-baseline \
  --targets /absolute/targets.txt
```

Checks apply to saved native 1920x1080 RGB captures and logged execution.
Stable text regions follow the logged mode: bottom ADV for mode 0, full-screen
narration for mode 1 using the script's window position and line width.
Structural checks do not establish OCR/translation or all-frame correctness;
decoder comparisons do not establish equivalence with proprietary RAD output.
The source repository includes no proprietary assets, screenshots or baselines.
