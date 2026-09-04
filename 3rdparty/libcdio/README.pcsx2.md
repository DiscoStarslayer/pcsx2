# PCSX2 integration notes

This directory contains libcdio from upstream commit
`548989a0c7369e84f61080e957e017f84e6bf375` (2.4.1dev0), retrieved from
<https://github.com/libcdio/libcdio>.

PCSX2 supplies CMake and Visual Studio build adapters, a static version header,
and focused BIN/CUE driver changes for optional missing CD-Text files, raw
sector reads, and multi-file CUE sheets. Upstream Autotools files and sources
remain intact to simplify future updates and patch review.
