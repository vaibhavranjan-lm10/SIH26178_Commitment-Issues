# Renode platform descriptions — PRAHARI L1 / L3-MCU

Platform (`.repl`) files for emulating the two Cortex-M targets in the
harness (test-env doc L1 + Code B's MCU side):

| Target | Runs | File | Base |
|---|---|---|---|
| Pod MCU, real target | Code A (L3 pipeline image) | `stm32l051.repl` | `stm32l071.repl` |
| Pod MCU, builds today | Code A on the `nucleo_l053r8` size stand-in | `stm32l053.repl` | `stm32l071.repl` |
| Pod MCU, smoke SoC | Code A driver/link layer (full image overflows) | `stm32l031.repl` | `stm32l071.repl` |
| Head node MCU | Code B MCU side (`arduino_uno_q` / `stm32u585xx`) | `stm32u585.repl` | `stm32l552.repl` + `stm32wba52.repl` |

## Which pod part does Code A actually target?

Blueprint §4.2 names **"STM32L031 / L051"**. `code_a_pod_firmware/README.md`
settles it: the **L051** is the real target (the L031's 32 KB flash cannot
hold the §4.3 pipeline, and a custom L051 board definition is still pending),
and Code A currently *builds* on `nucleo_l053r8` as a 64 KB size stand-in.
So all three L0 files are provided; `stm32l053.repl` is the one that runs
today's real firmware, `stm32l051.repl` is the one to switch to when the
L051 board lands, `stm32l031.repl` is for pre-pipeline bring-up only.

## Provenance of the base files (verbatim copies, Renode 1.17.0)

`stm32l071.repl` and `stm32l552.repl` are **byte-for-byte copies** of the
descriptions shipped in `renode_portable/platforms/cpus/`. Diff them against
your Renode install to confirm nothing was silently edited:

```
sha256  stm32l071.repl  fca5b91ee99c641ba3993f01956d3adc832aacb89f2afcb1447744f9f328064f
sha256  stm32l552.repl  d7caaa5cec5c20f12e3fc336af48665b4a929b0b88f769703e18bb6062f47738
```

Renode 1.17.0 ships **no** STM32L05x/L03x and **no** STM32U5 description,
hence the adaptation.

## Sources used for the adaptation

- ST **RM0377** (STM32L0x1) — one RM covers L011/L031/L051/L071/L081, so the
  L071 peripheral base addresses are already correct for L031/L051.
- ST **RM0456** (STM32U5) + ST datasheets (DS10688 L031, DS L051, DS L053).
- **Zephyr `dts/arm/st/l0/stm32l0*.dtsi` and `dts/arm/st/u5/stm32u5*.dtsi`**
  from this repo's own Zephyr checkout (`code_a_pod_firmware/zephyr/`). This
  is what Code A and Code B are literally compiled against, so for "will the
  emulated firmware find its registers" it is the authoritative source; every
  address below was taken from it and cross-checked against the RM.

---

## `stm32l051.repl` / `stm32l053.repl` / `stm32l031.repl` (pod)

Each is `using "stm32l071.repl"` plus **four scalar overrides**. Nothing
else is touched — the STM32L0x1 family shares RM0377, so every peripheral
address in the L071 base is already an RM0377 address.

### Hand-adapted (all verified against RM0377 + ST DS + Zephyr l0 dtsi)

| Property | L071 base | L031 | L051 | L053 | Source |
|---|---|---|---|---|---|
| `flash.size` | `0x30000` | `0x8000` (32K) | `0x10000` (64K) | `0x10000` (64K) | DS / `stm32l051X8.dtsi`, `stm32l031X6.dtsi` |
| `sram.size` | `0x5000` | `0x2000` (8K) | `0x2000` (8K) | `0x2000` (8K) | RM0377 Cat.2/3 |
| `eeprom.size` | `0x1800` | `0x400` (1K) | `0x800` (2K) | `0x800` (2K) | DS Table 2 / `stm32l051.dtsi` |
| `nvic.priorityMask` | `0xF0` | `0xC0` | `0xC0` | `0xC0` | `stm32l0.dtsi`: `arm,num-irq-priority-bits = 2`. **This is a correction to the upstream L071 file**, which carries the wrong 4-bit mask. |

*Not guessed:* the L071 base already has no RNG, which is correct — L051/L031
have no TRNG.

### Modeled superset (present in the base, absent on the real part)

Left in place deliberately — **harmless**, because Code A's devicetree never
addresses them, and removing nodes from a `using` base risks dangling
alternate-function references. A reviewer should know they are not real:

- **L051** lacks: `gpioPortE` (0x50001000), `usart4`/`usart5`
  (0x40004C00 / 0x40005000), `i2c3` (0x40007800).
- **L031** additionally lacks: `usart1`, `i2c2`, `spi2`, `timer3`, `timer7`.
  (`nucleo_l031k6.overlay` uses `usart2` + `lpuart1` + `spi1` + `i2c1` +
  `adc1`, all of which *are* real on the L031.)

### Already correct, inherited unchanged (RM0377 addresses)

`cpu` cortex-m0+ · `nvic` @ 0xE000E000 · flash @ 0x08000000 · sram @
0x20000000 · eeprom @ 0x08080000 · `flashController` (flash+eeprom) @
0x40022000 · `rcc` STM32L0_RCC @ 0x40021000 · `pwr` STM32L0_PWR @ 0x40007000
· `exti` @ 0x40010400 · `syscfg` @ 0x40010000 · `gpioPortA..D/H` @
0x50000000 step 0x400 · `usart1` @ 0x40013800 · `usart2` @ 0x40004400 ·
`lpuart1` @ 0x40004800 · `i2c1` @ 0x40005400 · `i2c2` @ 0x40005800 · `spi1`
@ 0x40013000 · `spi2` @ 0x40003800 · `adc1` @ 0x40012400 · `dma1` @
0x40020000 · `timer2/3/6/7/21/22` · `lptim1` @ 0x40007C00 · `iwdg` @
0x40003000 · `crc` @ 0x40023000 · `rtc` @ 0x40002800 · SVD `STM32L0x1`.

### Smoke test — PASS

Real `code_a_pod_firmware/build/pod_l053_r/zephyr/zephyr.elf` (Mode R build)
on `stm32l053.repl`:

```
renode -> mach create "pod"
         machine LoadPlatformDescription @stm32l053.repl
         sysbus LoadELF @.../pod_l053_r/zephyr/zephyr.elf
         emulation RunFor "0.5"
```

Vector table auto-detected at 0x08000000, PC=0x8002ACD **SP=0x200019A8**
(inside the 8 KB SRAM). Runs 0.5 s emulated through RCC/ADC/GPIO/I2C1/SPI2
init into the sample loop — **no HardFault, CPU not halted**. Warnings are
all benign: unmodelled register bits, and no sensor/SPI models attached
(`i2c1: Unknown slave at address 68/92` = SHT4x/LPS22HB, `spi2: no SPI
peripheral connected` = SX1262 — those belong to the L0 stimulus layer,
not the platform).

---

## `stm32u585.repl` (head node MCU)

**Self-contained** (no `using`) — the U5 changes are structural, not scalar,
so hand-building off `stm32l552.repl`'s structure was safer than override
gymnastics. The file's own header carries the full correct/substitute/stub
breakdown; summary:

### Address-correct, model-appropriate

| Peripheral | Address | Note |
|---|---|---|
| `cpu` cortex-m33, `nvic` mask `0xF0` | 0xE000E000 | armv8-m, 4 priority bits (`stm32u5.dtsi`) |
| `flash` 2 MB | 0x08000000 | `stm32u585Xi.dtsi` |
| `sram` 768 KB (SRAM1+2+3, contiguous) | 0x20000000 | `stm32u575.dtsi` (modeled as one block, as Zephyr does) |
| `sram4` 16 KB | 0x28000000 | `stm32u575.dtsi` |
| `flashController` STM32WBA_FlashController | 0x40022000 | same gen; L552 base already used this model here |
| `usart1` IRQ 61 (RS-485), `usart3` IRQ 63 (GNSS) | 0x40013800 / 0x40004800 | Code B overlay |
| **`lpuart1` IRQ 66 (MsgPack-RPC bridge)** | **0x46002400** | **moved from L552's 0x40008000 to the U5 APB3 address** — get this wrong and the entire MCU↔Linux bridge is dead |
| `spi2` IRQ 60 (SX1262) | 0x40003800 | Code B overlay |
| `adc1` IRQ 37 (batt/solar) | 0x42028000 | Code B overlay |
| `iwdg` (supervisor) | 0x40003000 | Code B |
| `rcc` / `pwr` / `exti` / `rtc` bases | 0x46020C00 / 0x46020800 / 0x46022000 / 0x46007800 | U5 moves all of these onto APB3 — L552 had them on APB1 |
| `rng` 94, `crc`, `timers1..17`, `usart2`, `uart4/5`, `spi1/3`, `i2c1` | — | standard STM32 M33 addresses, identical L5↔U5 |

### Substitute models (right address, register layout only approximated)

Marked inline in the file. **If Code B's boot ever hangs, look here first.**

- **`rcc: Miscellaneous.STM32WBA_RCC` @ 0x46020C00** — STM32WBA is the same
  RCC generation as U5 and `stm32wba52.repl` registers this model at the
  *same* 0x46020C00. Layout is **not** guaranteed byte-identical to RM0456.
  In the smoke test below the U5 clock driver hit
  `rcc: Unhandled read/write to offset 0x8 (ClockControl+0x8)` and **kept
  going** — tolerated, but this is the fragile point. Fallback: a Python
  flip-flop peripheral returning the RDY bits (pattern: `stm32l151.repl`
  `rccCsr`, `stm32g0.repl` `rcc`).
- **`pwr: Miscellaneous.STM32WBA_PWR` @ 0x46020800** — same story.
- **`exti: IRQControllers.STM32F4_EXTI` @ 0x46022000** — direct-interrupt
  stand-in (exactly the choice `stm32l552.repl` makes), relocated to the U5
  address. Lines 0–15 → NVIC 11–26 (`stm32u5.dtsi`), so the PB3 GNSS-PPS
  interrupt (EXTI3 → IRQ 14) fires. Higher-fidelity option:
  `IRQControllers.STM32WBA_EXTI` (needs the per-port `exti#N` GPIO wiring).
- **`adc1: Analog.STM32L5_ADC`**, **`rtc: STM32L_RTC`**,
  **`lptim1/2: Timers.STM32L0_LpTimer`** — same-family stand-ins at the
  correct U5 addresses.

### Stub

- **`gpdma1` @ 0x40020000 as plain `MappedMemory`** — the `arduino_uno_q`
  board dts sets `&gpdma1 { status = "okay" }`, so Zephyr's `st,stm32u5-dma`
  driver probes it. No functional GPDMA model in Renode 1.17; mapped as
  memory so the probe doesn't spew unhandled-access warnings. Code B's MCU
  app requests no DMA channels, so nothing ever needs it to move data.

### Guessed (not reverified against RM0456)

- **`syscfg` Tag @ 0x46000400** — U5 APB3 map, from memory, silenced only to
  keep pin-mux writes out of the log; nothing depends on it.
- **0x40030400** (TAMP/backup-domain, touched by Zephyr U5 boot) — left
  unmapped on purpose; the accesses are non-fatal warnings and a guessed
  tag would be worse than an honest gap.

### Smoke test — PASS

Real `code_a_pod_firmware/build/hn_mcu/zephyr/zephyr.elf` (`west build -b
arduino_uno_q ../code_b_head_node/mcu`) on `stm32u585.repl`:

```
Setting initial values: PC = 0x80025FD, SP = 0x20004040   (SP inside 768 KB SRAM)
emulation RunFor "0.5"  ->  PC advances to 0x8003a02, CPU not halted, no HardFault
```

The STM32WBA_RCC / STM32WBA_PWR substitutes are exercised during clock/power
bring-up and produce "unhandled offset" warnings **without stalling the
firmware** — i.e. behaving exactly as documented above.

---

## Reproduce the checks

```sh
RN=~/renode_portable/renode
for f in stm32l031 stm32l051 stm32l053 stm32u585; do
  $RN --disable-xwt --console --plain \
     -e "mach create \"m\"; machine LoadPlatformDescription @$PWD/$f.repl; peripherals; quit"
done
```
