# PCSX2 integration notes

This directory contains libcdio from upstream commit
`548989a0c7369e84f61080e957e017f84e6bf375` (2.4.1dev0), retrieved from
<https://github.com/libcdio/libcdio>.

PCSX2 supplies CMake and Visual Studio build adapters and carries focused
BIN/CUE driver changes for optional missing CD-Text files, complete raw-sector
reads, and CUE sheets which use more than one backing file. The upstream
Autotools files and sources are otherwise kept intact to make future updates
and patch review straightforward.
