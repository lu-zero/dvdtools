# dvdtools

collection of utilities to dissect and compose dvd video.

## License

All the code is under LGPL-2.1 see the COPYING file.

## Tools

### Inquiry

#### dump_ifo
Print the content of the ifo file in an human readable fashion.

#### print_vobu
Print the VOB Units, essentially parsing the NAV Packet information.

#### print_cell
Print the CELL information.

### Dissection

#### dump_vobu
Split a title or a menu into its independently decodable group of vobus.

#### dump_cell
Split a title or a menu into single units, basically from NAV to NAV.

### Restructure

#### make_vob
Repair the NAV sector information so it matches the current file structure.

#### rewrite_ifo
Repair the sector offsets to match the ones in the title and menu files.


## Usage

The tools currently let you try to fix and repair partially broken DVD, including re-encoding/concealing broken segment and make sure the damage to the menu is limited.

