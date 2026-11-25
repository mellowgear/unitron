# --count is optional: every cell, row by row. --trim drops blank cells (every pixel index 0)
# from the end of the last row only, and keeps that row's first cell. A cell drawn only in
# index 4 (palette 1, shade 0) is not blank, though its 2bpp bytes are zero.
gfx frompng indices.png out.bin --indices
