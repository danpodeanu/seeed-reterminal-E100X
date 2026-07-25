# Upstream image pipeline

These decoder and dithering sources originate from Seeed Studio's official
`Seeed_GFX` reTerminal SD-card examples at commit
`a2de1abca0597c202193f22d01e9fa35d1ff613b`:

`examples/ePaper/reTerminal_SDcard_Bitmap/reTerminal_E1001_SDcard_Gray4/`

The shared dither implementation contains Seeed's Gray4, Gray16, and E6
palette paths used by the corresponding E1001–E1004 firmware targets.

`pngle` retains its MIT notice and `miniz` retains its public-domain notice in
the source files. Seeed_GFX's license is available in the upstream repository.
