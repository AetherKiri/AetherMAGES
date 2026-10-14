# Bink 2 backport

This overlay is vcpkg FFmpeg 7.1.2 port revision 5 (git tree
`bf414f1e4f2a63ff7410444dc4eabb5b392f6d16`), with revision 6 adding
`0051-bink2-decoder.patch`. The shared-library ABI and existing codec IDs are
preserved; the Bink 2 video ID is appended to the video range.

The decoder is from Paul B Mahol's Librempeg commit
`1c54cb76bb29e754df25732952e8a5f2d82dade4`, with decoder files pinned to
`7ac786568b26ca7e0bf77c44d46d827e9d57af0d`. This includes subsequent VLC
initialization, chroma motion compensation, dimension initialization, DC
clipping, refactoring, and block flag fixes. Copyright and GPL-3.0-or-later
headers remain in the patch. Local adaptations place the descriptor in codec
ID order, append the codec ID, and check the correct `prev_idc` allocation.

Enable `ffmpeg[bink2,gpl,version3,avcodec,avformat]`. `bink2` is disabled when
the feature is absent. Building this feature produces GPL version 3 or later
FFmpeg libraries, also reflected in the installed FFmpeg copyright file.

The demuxer maps supported Bink 2 tags to the new decoder. This backport retains
FFmpeg 7.1's existing Bink audio path and does not add the upstream Bink audio
version 2/`n` extension. The MO8 PC `KB2j` movies use the existing Bink DCT audio
decoder. No game assets are included.

`tools/bink_decode_probe.c` validates a caller-supplied file through the built
libraries: complete video frame count, selected visible YUV hashes, audio
sample statistics, and optional original-resolution RGB captures. It provides
deterministic integration evidence; it does not claim pixel equality with the
proprietary RAD decoder.
