## **K1GE Resister Table**

1998.11.18 ver. 1.0 K. Takeda

|   |   | Register Name             | Abbreviation | Summary           | R/W   | Address | Control | L         |                                                       |
|---|---|---------------------------|--------------|-------------------|-------|---------|---------|-----------|-------------------------------------------------------|
| X | X | Window H Origin           | WBA.H        | WD Origin H       | W (R) | 8002    | User    | H         | “00”                                                  |
| X | X | Window V Origin           | WBA.V        | WD Origin V       | W (R) | 8003    | User    | H         | “00”                                                  |
| X | X | Window H Size             | WSI.H        | WD Size H         | W (R) | 8004    | User    | H         | “FF”                                                  |
| X | X | Window V Size             | WSI.V        | WD Size V         | W (R) | 8005    | User    | H         | “FF”                                                  |
| X | X | Raster Position H         | RAS.H        | Raster Address H  | R     | 8008    | User    |           | Negation Counter                                      |
| X | X | Raster Position V         | RAS.V        | Raster Address V  | R     | 8009    | User    |           |                                                       |
| X | X | 2D Status                 | C.OVR        |                   |       |         |         |           |                                                       |
|   |   |                           |              | 2D Status         | R     | 8010    | User    |           | D7: Character Over 0 = No, 1= Yes                     |
| X | X | 2D Control                | NEG          |                   |       |         |         |           |                                                       |
|   |   |                           |              | 2D Control        | W (R) | 8012    | User    | H         | “00” D7: 0 = POS display, 1 = NEG display             |
| X | X | H Position Offset         | PO.H         | Sprite Offset H   | W (R) | 8020    | User    | H         | “00”                                                  |
| X | X | V Position Offset         | PO.V         | Sprite Offset V   | W (R) | 8021    | User    | H         | “00”                                                  |
| X | X | Switch Scroll             | Plane        |                   |       |         |         |           |                                                       |
|   |   |                           | P.F          | Scroll Priority   | W (R) | 8030    |         |           | “00” D7: 0 = Scroll 1high priority, 1 = Scroll 2 high |
| X | X | Scroll Plane 1 H Scroll   |              |                   |       |         |         |           |                                                       |
|   |   |                           | S1SO.H       | Scroll 1 Offset H | W (R) | 8032    | User    | H         | “00”                                                  |
| X | X | Scroll Plane 1 V Scroll   |              |                   |       |         |         |           |                                                       |
|   |   |                           | S1SO.V       | Scroll 1 Offset V | W (R) | 8033    | User    | H         | “00”                                                  |
| X | X | Scroll Plane 2 H Scroll   |              |                   |       |         |         |           |                                                       |
|   |   |                           | S2SO.H       | Scroll 2 Offset H | W (R) | 8034    | User    | H         | “00”                                                  |
| X | X | Scroll Plane 2 V Scroll   |              |                   |       |         |         |           |                                                       |
|   |   |                           | S2SO.V       | Scroll 2 Offset V | W (R) | 8035    | User    | H         | “00”                                                  |
| X | X | Palette LUT01 for Sprites | SPPLT.01     | Sprite Palette 01 | W (R) | 8101    | User    | Immediate | “07”                                                  |
| X | X | Palette LUT02 for Sprites | SPPLT.02     | Sprite Palette 02 | W (R) | 8102    | User    | Immediate | “07”                                                  |
| X | X | Palette LUT03 for Sprites | SPPLT.03     | Sprite Palette 03 | W (R) | 8103    | User    | Immediate | “07”                                                  |

## (continued)

|   |   | Register Name             | Abbreviation | Summary             | R/W   | Address | Control | L         |                                                   |
|---|---|---------------------------|--------------|---------------------|-------|---------|---------|-----------|---------------------------------------------------|
| X | X | Palette LUT11 for Sprites | SPPLT.11     | Sprite Palette 11   | W (R) | 8105    | User    | Immediate | “07”                                              |
| X | X | Palette LUT12 for Sprites | SPPLT.12     | Sprite Palette 12   | W (R) | 8106    | User    | Immediate | “07”                                              |
| X | X | Palette LUT13 for Sprites | SPPLT.13     | Sprite Palette 13   | W (R) | 8107    | User    | Immediate | “07”                                              |
| X | X | Palette LUT01 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC1PLT.01    | Scroll 1 Palette 01 | W (R) | 8109    | User    | Immediate | “07”                                              |
| X | X | Palette LUT02 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC1PLT.02    | Scroll 1 Palette 02 | W (R) | 810A    | User    | Immediate | “07”                                              |
| X | X | Palette LUT03 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC1PLT.03    | Scroll 1 Palette 03 | W (R) | 810B    | User    | Immediate | “07”                                              |
| X | X | Palette LUT11 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC1PLT.11    | Scroll 1 Palette 11 | W (R) | 810D    | User    | Immediate | “07”                                              |
| X | X | Palette LUT12 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC1PLT.12    | Scroll 1 Palette 12 | W (R) | 810E    | User    | Immediate | “07”                                              |
| X | X | Palette LUT13 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC1PLT.13    | Scroll 1 Palette 13 | W (R) | 810F    | User    | Immediate | “07”                                              |
| X | X | Palette LUT01 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC2PLT.01    | Scroll 2 Palette 01 | W (R) | 8111    | User    | Immediate | “07”                                              |
| X | X | Palette LUT02 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC2PLT.02    | Scroll 2 Palette 02 | W (R) | 8112    | User    | Immediate | “07”                                              |
| X | X | Palette LUT03 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC2PLT.03    | Scroll 2 Palette 03 | W (R) | 8113    | User    | Immediate | “07”                                              |
| X | X | Palette LUT11 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC2PLT.11    | Scroll 2 Palette 11 | W (R) | 8115    | User    | Immediate | “07”                                              |
| X | X | Palette LUT12 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC2PLT.12    | Scroll 2 Palette 12 | W (R) | 8116    | User    | Immediate | “07”                                              |
| X | X | Palette LUT13 for Scroll  |              |                     |       |         |         |           |                                                   |
|   |   |                           | SC2PLT.13    | Scroll 2 Palette 13 | W (R) | 8117    | User    | Immediate | “07”                                              |
| X | X | Background Color          | BG           | Background Color    | W (R) | 8118    | User    | H         | “07” Using Background Color, the values are fixed |