![](_page_0_Picture_0.jpeg)

**(rel. 0.1)**

![](_page_1_Picture_0.jpeg)

## *REVISION HISTORY*

rel. 0.1 Initial release 1998/07/28

## *TABLE OF CONTENTS*

| REVISION HISTORY                    | 2 |
|-------------------------------------|---|
| PREFACE                             | 4 |
| FLASH WRITER OUTLINE                | 5 |
| FLASH WRITER OPERATION INSTRUCTIONS | 6 |

![](_page_2_Picture_4.jpeg)

# *PREFACE*

The materials covered in this reference manual is provisional. There is the possibility of future updates.

### *FLASH WRITER OUTLINE*

This software uses the game development board to write to the flash cassette.

Currently, the devices supported are the 4M, 8M and 16M devices. Due to the game development board specification, data may be written to 15M of the 16M device. Please be aware that data may not be written to the latter 1M of the 16M.

## *FLASH WRITER OPERATION INSTRUCTIONS*

#### ¨**Controller Keys**

Cursor key: Used to choose one of the selections. A button: Used to confirm selection. B button: Used to cancel selection.

#### ¨**Menu Selections**

SOURCE ADDR: Defines the source start address. Range: 0x210000~0x3effff.

FLASH ADDR: Defines the write address of the flash. Range: 0x800000~0x9effff.

DATA SIZE: Defines the data size. Range: 0x000000~0x200000. Because 16

M of memory may not be written, in reality the last address is

0x1e0000.

ERS & WRT: Erases the requested DATA SIZE, address specified by FLASH ADDR, and then writes the data with data size DATA SIZE starting from SOURCE ADDR.

WRITE START: Writes the data with data size DATA SIZE to the address FLASH ADDR from the source with start address SOURCE ADDR.

ERASE START: Erases DATA SIZE worth of data from the starting address FLASH ADDR.

#### ¨**Data Load Method**

Please use Intel HEX h24 format for the data to be written. Give an offset of 0x010000 when using the EMUSE to load. Then please load and run the FLASH WRITER software as with other softwares.