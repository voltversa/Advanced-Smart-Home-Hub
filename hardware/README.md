# Hardware design and manufacturing data

## Final four-layer submission

The later attached handoff is preserved under [`submission/Mahmoud_Mostafa_Adv_pcb/`](../submission/Mahmoud_Mostafa_Adv_pcb/):

- `01_Altium_Project.zip` contains the native project, a four-layer `PCB1.PcbDoc`, `PCB1.step`, and `PCB1.cvg`.
- `PCB1.cvg` is an IPC-2581 Revision B XML document despite its nonstandard extension.
- The active native stack is Top copper → GND plane → 3V3 plane → Bottom copper and is configured at approximately 0.624 mm including solder masks; confirm finished thickness with the fabricator.
- `02_Production_Data.zip` includes matching `GTL`, `GP1`, `GP2`, and `GBL` files, drill data, pick-and-place, and an XLSX BOM.
- The final DRC report records 23 unwaived violations: one solder-mask sliver and 22 silkscreen-to-mask entries. Electrical routing rule groups report zero, but the release is not DRC-clean.

The final native PCB/STEP and IPC-2581 are dated in August, while the production outputs remain dated 17 July. Regenerate the production set from the final native PCB before fabrication. The XLSX BOM/PnP reference sets agree at 122 designators, but value/description/MPN conflicts remain and require line-by-line procurement review.

This final submission supersedes the layer count of the earlier snapshot below, but both are preserved for traceability.

## Earlier `altium/` snapshot

Native design data recovered from the archived Altium project:

- `Hub_Unit - Copy.PrjPcb`
- six hierarchical `SchDoc` sheets
- `PCB1.PcbDoc`
- `Hub_Unit - Copy.OutJob`
- CAM files
- compiled smart schematic PDF

The original filenames are preserved to avoid breaking Altium project references.

The project file still references three external integrated/schematic/PCB libraries that were not present in the recovered archive, and the OutJob retains dated absolute Windows output paths. The native documents can be inspected, but this is not a self-contained Altium workspace.

## Earlier `manufacturing/` snapshot

This directory preserves a 17 July 2026 output snapshot without regenerating it:

- Gerber/layer files
- plated, non-plated, slot, and counter-hole drill data
- drill reports
- pick-and-place CSV
- BOM CSV (`Hub_Unit - Copy.csv`)
- DRC report

Keep this package unchanged for traceability. Do **not** submit it unchanged for a new order. Its native `PCB1.PcbDoc` master stack contains top copper, one dielectric, and bottom copper; its Gerbers likewise contain only top and bottom copper. This is an earlier two-layer snapshot, not the later four-layer handoff.

The archived DRC report is historical output, not a zero-violation signoff. It records 55 detected violations, two waived violations, and a modified-polygon warning. Repour polygons and run a clean DRC before placing a new manufacturing order.

The BOM and PnP files contain the same populated reference set, but several BOM value/description/manufacturer-part fields conflict. Treat them as historical assembly records, not procurement-ready data; verify every line against the schematic, the assembled board, and the selected component datasheet.

One known report/schematic discrepancy is the buzzer driver: the Rev 2.1 narrative says 5 V with a 100 kΩ gate pull-down, while the recovered schematic and BOM show 3.3 V with 10 kΩ. Verify the physical board before updating or reproducing this section.

The compiled MCP2221A/USBLC6 sheet also appears to route D− and VUSB through incompatible protection-device pins and does not represent a valid USB path. The reworked prototype enumerated after a D+ repair, which means this archived drawing is not a dependable as-built record. Trace the physical board and correct/regenerate the Altium source before fabricating this section.

## Release caveats

The earlier expanded snapshot lacks IPC-2581, STEP, and a standalone XLSX BOM; the later submission provides all three. Neither revision includes a clean DRC signoff or evidence that the output set was independently approved by a fabricator. Verify stack-up, dimensions, apertures, drill classes, BOM fields, and DRC from the final native project before ordering.

The compiled Altium smart PDF contains embedded JavaScript and vendor-link actions. Use a trusted viewer with active content disabled, or export a flattened schematic PDF from Altium before wider distribution.
