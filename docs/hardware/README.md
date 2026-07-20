# Hardware references

- `rp2350-usb-mini-pinout.jpg`, pin map for the RP2350-USB Mini used in
  Kestrel. Note: the seller's diagram photographs the RP2040 variant of the
  board ("RP2040 USB" silkscreen, RP2-B2 chip); the RP2040-USB and
  RP2350-USB minis share an identical board layout and pinout, and the chip
  on Kestrel's actual board is an RP2350 (verified against the physical
  part). Wiring shown is valid for both.
- STM32H750 board schematic, datasheets, and camera/LCD references: WeAct's
  upstream repository, https://github.com/WeActStudio/MiniSTM32H7xx (see
  `Hardware/STM32H7xx SchDoc V12.pdf` there for the H750 side, including the
  SB1/PA7 camera-PWDN solder bridge and the DCMI power tree).
