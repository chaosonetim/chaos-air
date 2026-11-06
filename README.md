# NUCLEO-N657X0-Q camera ID probe

The FSBL runs standalone, powers up the B-CAMS-IMX camera, and reads the
IMX335 ID over I2C2. LED1 (PG8) still toggles every 500 ms. The
secure/non-secure applications and ThreadX are not used by this demo.
Application code changes are inside CubeMX USER CODE sections; the FSBL
HAL configuration and CMake file also enable the I2C and UART drivers.

## Camera and logs

With power off, connect the B-CAMS-IMX to the board's CSI connector (CN6).
No additional control wires are needed. The connector carries I2C2
(PB10 SCL / PB11 SDA), power enable (PA0), and active-low reset (PO5).
The module supplies its sensor clock; no MCU camera-clock output is used.

Open the ST-LINK virtual COM port at **115200 baud, 8N1, no flow control**,
then reset the board. Logs use USART1 (PE5 TX / PE6 RX) through ST-LINK USB.
Expected output:

```text
chaos-air: B-CAMS-IMX camera ID probe
Camera: IMX335, I2C2 addr=0x1A, ID[0x3912]=0x00 (matched)
```

The ID is the sensor's one-byte identification register, not a unique serial
number. `0x00` is the expected value in ST's IMX335 driver; it is only logged
as matched after a successful I2C read. Failed reads log HAL status/error
codes; unexpected IDs are also reported. Both retry every five seconds.
On success the probe stops. Fatal initialization or UART errors leave LED1
solid on (inspect `Error_Handler` with the debugger if UART is unavailable).
Power off before reseating a camera cable; this is not a hot-plug demo.
This only verifies the control bus; CSI image capture is not configured.

## One AppNS frame into SRAM

The optional `CAMERA_SNAPSHOT=ON` build runs the capture in
`AppliNonSecure/Core/Src/camera_snapshot.c`, before ThreadX starts. The normal
ThreadX build and standalone FSBL ID probe remain available with this option off.
The secure companion configures clocks, SAU/RIF, GPIO ownership, and interrupt
targets, then enters the actual non-secure application.

The sensor outputs its native 2592x1944 RAW10 stream on two CSI-2 lanes at
1.6 Gbit/s/lane (VC0, data type 0x2B). By default, DCMIPP pipe0 captures **one top-left
640x480 crop**, without ISP, demosaicing, downscaling, or a test pattern.
RAW10 samples are stored as **little-endian uint16 values, LSB-aligned**:

| Property | Value |
|---|---|
| Pixels | 640 x 480 |
| Row stride | 1280 bytes |
| Payload | 614400 bytes |
| Buffer symbol | `camera_frame.pixels` |
| Payload address | `0x24120020` (non-secure SRAM2) |
| Pre/post guard sizes | 32 bytes each |

The snapshot linker layout reserves separate SRAM2 ranges for code (127 KiB),
the guarded frame (640 KiB), and application data/stack (256 KiB).
Pipe0 horizontal crop units are 32-bit words, so 320 words select 640 RAW10
pixels. The DMA counter is in bytes with 32-bit granularity.

Hardware snapshot mode is armed exactly once per run. After completion the
sensor is put in standby and the receiver is stopped. Errors and a 3-second
timeout fail explicitly without a retry/rearm. The code checks one frame event,
614400 transferred bytes (pipe0 only), both guards, every RAW sample's 10-bit range, and an
unchanged checksum after 250 ms before setting `camera_capture_complete = 1`.
The automatic ISP mode below intentionally uses multiple warm-up frames before
retaining one final frame.
Capture polling and the final idle loop stay awake to preserve debug access.
Caches are not enabled by this demo; an already-enabled non-secure D-cache is
disabled before DMA.

With JP2 at 2-3, build and run from PowerShell using the selected ST-LINK serial:

```powershell
STM32_Programmer_CLI -l st-link
.\scripts\Run-CameraSnapshot.ps1 -ProbeSerial <serial> -Port COM3
```

The script resets the target, downloads/verifies both ELFs into SRAM, starts
the secure reset handler, and checks the COM log for completion. It does not
program external flash or change option bytes. PowerShell must permit running
local scripts; the script does not change execution policy.

Equivalent build commands (stop after any failure):

```powershell
cmake -S .\AppliSecure -B .\build\snapshot-secure -G Ninja -DCMAKE_TOOLCHAIN_FILE=E:\chaos-air\gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug -DCAMERA_SNAPSHOT=ON
cmake --build .\build\snapshot-secure
cmake -S .\AppliNonSecure -B .\build\snapshot-ns -G Ninja -DCMAKE_TOOLCHAIN_FILE=E:\chaos-air\gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug -DCAMERA_SNAPSHOT=ON -DCAMERA_FULL_FRAME=OFF -DCAMERA_AUTO_ISP=OFF -DCAMERA_SECURE_IMPORT=E:\chaos-air\build\snapshot-secure\secure_nsclib.o
cmake --build .\build\snapshot-ns
```

The script contains the SRAM download/start commands. A successful log includes:

```text
Capture: frames=1 bytes=614400 errors=00000000
RAW10 samples: min=... max=..., guards intact
DONE: one frame retained, checksum=..., sensor standby, no rearm
```

After `DONE`, export the payload without resetting the board:

```powershell
STM32_Programmer_CLI -c port=SWD sn=<serial> mode=HOTPLUG freq=1000 -u 0x24120020 614400 .\build\snapshot-ns\frame.bin
```

CubeProgrammer requires the `.bin` extension for this raw binary export.

Decode the exported RGGB10 payload with Python (standard library only):

```powershell
python .\scripts\Decode-CameraSnapshot.py .\build\snapshot-ns\frame.bin
python -m unittest discover -s .\scripts -p test_camera_snapshot_decoder.py
```

The decoder validates the payload size and sample range, prints the firmware's
FNV-1a checksum for comparison with the `DONE` log, and writes `frame-preview.png`
(640x480 bilinear RGGB RGB) and `frame-gray.png` (320x240 Bayer-cell averages).
The previews use a shared 1st-to-99th-percentile contrast stretch, with no white
balance, gamma, or ISP processing; color is not calibrated. Use the grayscale
preview to check scene structure independently of the Bayer interpolation.
Transfer size, sample range, guards, and checksum stability alone do not prove
that a capture is a valid scene image.

### Full-field 640x480 RGB565 snapshot

Add `-FullFrame` to retain the entire sensor field of view, instead of cropping:

```powershell
.\scripts\Run-CameraSnapshot.ps1 -ProbeSerial <serial> -Port COM3 -FullFrame
STM32_Programmer_CLI -c port=SWD sn=<serial> mode=HOTPLUG freq=1000 -u 0x24120020 614400 .\build\snapshot-ns\frame-full-field.bin
python .\scripts\Decode-CameraSnapshot.py .\build\snapshot-ns\frame-full-field.bin --format rgb565
```

For manual builds, use `-DCAMERA_FULL_FRAME=ON` in the AppNS configure command.
This option requires `CAMERA_SNAPSHOT=ON`; omitting the script switch explicitly
restores the original RAW crop mode.

Pipe1 demosaics RGGB10 using fixed settings and downsizes all 2592x1944 pixels
to 640x480, preserving the 4:3 aspect ratio with crop and decimation disabled.
The fractional Q13 ratios are 33177 horizontally and vertically, with reciprocal
normalization factors of 252, following ST's `cmw_utils.c` calculation.
RGB565 is stored in little-endian uint16 values (R bits 15:11, G bits 10:5,
B bits 4:0); buffer address, stride, payload size, and guards remain as above.
Sensor timing is fixed at 30 fps, 20 ms exposure, and 0 dB gain.
There is no automatic exposure, white balance, color correction, or gamma;
the preview may look dark and green-tinted. The RGB decoder does not apply the
RAW decoder's percentile contrast stretch, and its grayscale output is 640x480.

Pipe1 has no hardware dump-byte counter or pipe0 DMA-limit event. It validates
one frame event, zero reported errors, both guards, and checksum stability.
Its log says `payload=614400`, not measured transferred bytes;
`camera_captured_bytes` remains zero in this mode. Compare the exported checksum
with the `DONE` log and inspect scene content as well.

### Automatic exposure, white balance, gamma, and mild sharpening

```powershell
.\scripts\Run-CameraSnapshot.ps1 -ProbeSerial <serial> -Port COM3 -FullFrame -AutoIsp
STM32_Programmer_CLI -c port=SWD sn=<serial> mode=HOTPLUG freq=1000 -u 0x24120020 614400 .\build\snapshot-ns\frame-auto-isp.bin
python .\scripts\Decode-CameraSnapshot.py .\build\snapshot-ns\frame-auto-isp.bin --format rgb565
```

`CAMERA_AUTO_ISP=ON` requires `CAMERA_FULL_FRAME=ON`. The script explicitly
disables automatic processing when `-AutoIsp` is omitted. Automatic mode uses
ST's software AEC and AWB algorithms and IMX335 tuning, plus DCMIPP hardware
black-level correction, color correction, and gamma. Sensor helpers use I2C2
for updates and cache the last successfully commanded exposure/gain, as in ST's
camera middleware. Statistics are gathered from pipe1 VSYNC interrupts; ISP
background processing runs in the foreground.

After the final frame is retained and DMA stops, AutoIsp mode applies a
software luminance unsharp mask to RGB565: a 3x3 Gaussian blur, 100% amount,
4/255 detail threshold, and a correction limited to +/-16/255. The same
correction is applied to all three channels, with RGB565 quantization and
clamping; the outermost pixel border is unchanged. Three original rows
(3840 bytes) are buffered to avoid in-place feedback. This is on-board
post-processing, not a DCMIPP hardware sharpening stage. RAW and fixed modes
are unchanged. The log reports the pre-sharpening checksum; the final `DONE`
checksum and exported payload include sharpening.

For an exact same-frame before/after export, add
`-ComparisonPrefix .\build\snapshot-ns\comparison` to the AutoIsp run command.
The script exports `comparison-before.bin`, then tells the board to sharpen
that retained frame and exports `comparison-after.bin`. Decode both as RGB565.
This enables `CAMERA_COMPARE_SHARPENING` for that run; the board waits up to
60 seconds for the script's command before failing explicitly. Normal script
runs turn comparison mode off again.

Unlike single-shot fixed/RAW mode, this mode captures 90 continuous warm-up
frames into the same buffer (10-second timeout), stops algorithm updates, allows
five more frames for control latency, and switches the active pipe to snapshot
mode to retain the final frame. Warm-up is bounded, not a guarantee of perfect
convergence under every lighting condition. Logs report total frame count,
commanded sensor exposure/gain, WB gains, and verified gamma enable state.
The sensor/receiver then stop; guards and checksum stability are checked as
before. The run script waits 15 seconds for this mode's log. The payload remains
640x480 RGB565, 614400 bytes, at `0x24120020`; no host color correction is added.

ST ISP runtime sources and IMX335 tuning are vendored under
`Middlewares/ST/isp`, from camera-capture commit
`e9441cdbb5072a6d3bcef1a5ca4e4d80f850283a`.
They retain ST copyright notices and the **SLA0044** license in
`Middlewares/ST/isp/LICENSE.txt`; they are not BSD/MIT-licensed and are restricted
to use with ST devices. USB tuning tools and USB streaming are not enabled.
The sole local ISP runtime adaptation is in `isp_services.c`: firmware metadata
uses the compile-time STM32N657 target in AppNS instead of calling the
secure-only `HAL_GetDEVID`/BSEC API. Existing security ownership is not broadened.

The camera data and application disappear on reset/power loss. This is a
development-mode RAM demo, not a production TrustZone policy or persistent boot
image. Do not run the FSBL blink image alongside this pair.

Sensor setup uses ST's BSD-3-Clause IMX335 component, vendored with its license
under `Drivers/BSP/Components/imx335`, pinned to commit
`be3b88695b163f29fbb9ac4e5a9563c14ed7bd78`.
The local driver additionally exposes `IMX335_InitStandby`, separating setup
from streaming as in the camera-capture reference while preserving the original
`IMX335_Init` streaming behavior. Full-field mode uses the standby entry point
so timing/exposure are set before the first stream starts. This avoids a
first-frame brightness discontinuity observed with the older init sequence.
Clock/CSI/snapshot setup follows ST's
[DCMIPP_SnapshotDecimationMode example](https://github.com/STMicroelectronics/STM32CubeN6/tree/main/Projects/STM32N6570-DK/Applications/DCMIPP/DCMIPP_SnapshotDecimationMode),
adapted to the Nucleo's camera-control pins and non-secure execution.

The requested application reference is
[ST x-cube-n6-camera-capture](https://github.com/STMicroelectronics/x-cube-n6-camera-capture/tree/e9441cdbb5072a6d3bcef1a5ca4e4d80f850283a).
Its `Lib/Camera_Middleware/cmw_camera.c` IMX335 setup uses the same two
physical CSI lanes, 1600 Mbit/s PHY setting, and 24 MHz sensor input clock.
The vendored driver's native-resolution, two-lane RAW10, and 24 MHz register
tables were compared with that package's IMX335 component and are identical.
Unlike the package's FreeRTOS/ISP/USB UVC webcam application, this AppNS
demo deliberately retains one pipe0 RAW or pipe1 RGB565 frame, without USB
streaming. The optional automatic mode uses the ISP middleware during warm-up.

Comparison against that pinned reference:

| Setting | ST reference | This snapshot |
|---|---|---|
| Sensor mode | 2592x1944, RGGB10 | Same; all 84 native-mode register entries match |
| CSI input | Two physical lanes, 1600 Mbit/s, VC0, DT 0x2B, BPP10 | Same |
| Sensor input clock | 24 MHz | Same; all seven clock-table entries match |
| Camera control (Nucleo) | I2C2, PA0 enable, PO5 reset | Same pins and bus |
| CSI reference clock (IC18) | 800 MHz / 40 = 20 MHz | 1200 MHz / 60 = 20 MHz |
| DCMIPP clock (IC17) | 1000 MHz / 3, about 333 MHz | 1200 MHz / 4 = 300 MHz |
| Sensor frame rate | Explicitly set to the requested USB stream rate | RAW: sensor defaults; full-field: explicitly 30 fps |
| Output processing | Pipe1, resizing, ISP, auto-exposure, auto-white-balance, gamma | RAW: pipe0 crop; full-field: pipe1 demosaic/resize, no automatic algorithms |
| Reset-pin supply setup | Explicitly enables VDDIO2 | No explicit VDDIO2 enable in this snapshot |
| Sensor start | Reference driver initializes in standby, then starts separately | RAW: original init then standby; full-field: standby init then separate start |

In `DCMIPP_DTMODE_DTIDA`, the reference sets the unused DataTypeIDB to zero,
whereas the snapshot supplies RAW10; IDB is not selected in this mode.
These differences are not proof of corrupted CSI data. In particular, a
top-left crop is not a resized view of the entire sensor, and unprocessed Bayer
data is not expected to have the reference application's calibrated colors.
For scene validation, use a high-contrast target covering the captured corner.

## Build

With STM32CubeCLT on PATH, run the VS Code **Build blink** task, or run
these commands from the project root in PowerShell (stop if a command fails):

```powershell
cmake -S .\FSBL -B .\build\blink -G Ninja -DCMAKE_TOOLCHAIN_FILE=E:\chaos-air\gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build .\build\blink
STM32_SigningTool_CLI -bin .\build\blink\chaos_air_FSBL.bin -nk -of 0x80000000 -t fsbl -hv 2.3 -align -o .\build\blink\chaos_air_FSBL-trusted.bin
```

The signing command adds an unsigned boot header for development; it does not
provision keys or change security settings. `-align` is required with
STM32CubeProgrammer 2.21 or newer.

## Program external flash

1. Connect the board's ST-LINK USB port with a data-capable cable.
2. With power off, set BOOT1 (**JP2**) to **2-3** (development mode).
   BOOT0 does not matter in this mode. Reconnect power.
3. Check detection: `STM32_Programmer_CLI -l st-link`.
4. The following command **overwrites the boot image in external flash**.
   Back up any existing firmware first. With the CubeCLT installation shown
   below, program and verify the image:

```powershell
$loader = 'C:\ST\STM32CubeCLT_1.22.0\STM32CubeProgrammer\bin\ExternalLoader\MX25UM51245G_STM32N6570-NUCLEO.stldr'
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $loader -w .\build\blink\chaos_air_FSBL-trusted.bin 0x70000000 -v
```

Adjust the loader path if CubeCLT is installed elsewhere. If multiple probes
are connected, select the intended probe with `sn=<serial>` in `-c`.
Do not mass-erase or change option bytes for this demo.

5. After verification succeeds, power off and set **both BOOT0 (JP1) and BOOT1 (JP2)
   to 1-2** (external-flash boot). Power on and press RESET.
6. Confirm LED1 blinks every half second, including after a power cycle.
   Open the virtual COM port before reset to verify the camera ID log too.

For a temporary RAM-only test in development mode (lost on reset/power-off):

```powershell
$elf = '.\build\blink\chaos_air_FSBL.elf'
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG freq=1000 -w $elf -v
# Stop if the download/verification failed.
$resetHandler = (arm-none-eabi-nm -n $elf | Select-String ' W Reset_Handler$').ToString().Split()[0]
$stackTop = (arm-none-eabi-nm -n $elf | Select-String ' _estack$').ToString().Split()[0]
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG freq=1000 -halt -coreReg MSP=0x$stackTop PC=0x$resetHandler XPSR=0x01000000 -run
```

This starts the ELF's reset handler directly. `0x34180400` is the vector
table, not executable code. With CubeProgrammer 2.23, `-g` did not start
this RAM application; setting the core registers and using `-run` was
verified with the camera ID log. Keep JP2 in development mode for RAM loading.

The STM32N657 has no internal program flash; writing the ELF to SRAM alone
does not install a persistent application.

Board pin and boot procedure reference:
[ST GPIO_IOToggle example](https://github.com/STMicroelectronics/STM32CubeN6/tree/main/Projects/NUCLEO-N657X0-Q/Examples/GPIO/GPIO_IOToggle).
Camera connector and VCP pinout: UM3417, sections 8.3 and 7.9.
Sensor ID reference:
[ST IMX335 register definitions](https://github.com/STMicroelectronics/stm32-imx335/blob/main/imx335_reg.h).
