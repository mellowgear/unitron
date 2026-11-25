# BUG?: --pal 3 with a one-palette file is not an error: the sheet silently uses palette 0
# (indices 0-3), as if --pal were not given
gfx topng tiles.bin out.png --palette pal.bin --pal 3
