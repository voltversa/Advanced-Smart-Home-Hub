# Final submission package

This directory preserves the contents of the later `Mahmoud_Mostafa_Adv_pcb.zip` handoff in a browsable repository layout. Original deliverable names are retained under `Mahmoud_Mostafa_Adv_pcb/`, except that the unsafe firmware archive is replaced by `09_Firmware_sanitized.zip`.

- Source attachment SHA-256: `7dd5dbed5942abe31eb20fdf98b530309be2df47fd8e590c0eb552e7d38fed2e`
- Sanitized firmware SHA-256: `28649cce5e507eb17961e5c7566a2afb844a6b307e8eba77be5b6ce73bb02d63`
- File checksums: [`MANIFEST.sha256`](MANIFEST.sha256)

From the repository root, verify the complete submission with `sha256sum -c submission/MANIFEST.sha256`.

## Contents

| Item | Description |
|---|---|
| `01_Altium_Project.zip` | Final native four-layer Altium project, STEP model, and IPC-2581 Rev B file (`PCB1.cvg`) |
| `02_Production_Data.zip` | Four-layer Gerber/plane outputs, drill data, BOM, pick-and-place, and DRC snapshot |
| `03_*` through `08_*` | PCB/schematic PDFs, PCB documentation, XLSX BOM, DRC, ERC, and schematic-to-PCB difference report |
| `09_Firmware_sanitized.zip` | Complete CubeIDE project with HAL/CMSIS, startup/linker files, `.ioc`, Bosch BME68x, and application sources |
| `10_*` | Rev 2.1 report in DOCX and PDF |
| `11_*` | Editable oral-demonstration presentation |
| `images&videos/` | Submitted prototype screenshots, photographs, and demonstration video |
| `PCB_3D_photo.png` | Submitted PCB 3D rendering |

## Hardware evidence

The final native `PCB1.PcbDoc` uses four copper layers: Top, internal GND plane, internal 3V3 plane, and Bottom. The production archive contains corresponding `GTL`, `GP1`, `GP2`, and `GBL` outputs. The file named `PCB1.cvg` begins with an IPC-2581 Revision B XML root and is the requested IPC-2581 deliverable.

The configured native/IPC stack is approximately 0.624 mm including solder masks; confirm the intended finished thickness with the fabricator. The preserved DRC is not clean: it records 23 unwaived violations—one solder-mask sliver and 22 silkscreen-to-mask entries—although its electrical routing rule groups report zero violations. Review and resolve every entry before any fabrication order.

The production outputs and DRC are dated 17 July 2026, while the native PCB/STEP are dated 13 August and IPC-2581 was generated 16 August. Regenerate the complete output set from the final native PCB before ordering. The BOM/PnP reference sets agree, but several value/description/MPN fields remain inconsistent and are not procurement-ready.

Several Altium-generated PDFs in this package contain embedded JavaScript for document interactivity and vendor links. Open them only in a trusted viewer with active content disabled, or export flattened derivatives before wider distribution.

## Credential sanitization

The raw attached `09_Firmware.zip` is intentionally not committed. It contained live Wi-Fi/Blynk credentials in `Core/Inc/app_secrets.h`, with compiled copies embedded in debug binaries. `09_Firmware_sanitized.zip` removes that file and the entire `Debug/` tree while retaining the safe `app_secrets.h.example`, full source, device drivers, startup code, linker scripts, and project metadata.

The original outer ZIP is also omitted because it contains that unsafe nested archive; its safe contents are represented individually in this directory.

Rotate the exposed credentials before using the project again. Do not commit a replacement `app_secrets.h`.
