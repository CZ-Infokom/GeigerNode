# GeigerNode hardware

KiCad 10 project for PCB rev A. Open `GeigerNode.kicad_pro`. Symbols and footprints are stored in the schematic and board. 3D models resolve through `${KICAD10_3DMODEL_DIR}`.

`production/` is the fabrication export from 2026-09-17: `GeigerNode_A.zip` (Gerbers and drills), `bom.csv`, `positions.csv`, `designators.csv`, and `netlist.ipc`. The schematic and board files were saved a few minutes after that export. Regenerate this directory before the next fab order.

KiCad automatic backups and `GeigerNode.kicad_prl` are gitignored.
