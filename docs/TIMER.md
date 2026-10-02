# Stage 3 clock — DMG divider and timer

`gb_cpu_step` still reports **T-cycles**. A `NOP` is 4. The timer does not use a
second instruction decoder.

The hardware clock inside the bus is one **machine cycle**, which is exactly
4 T-cycles. `gb_timer_advance` takes a T-cycle count and spends it in machine
cycles. A leftover of 1–3 T-cycles stays in the timer until a later call fills
out a machine cycle. The CPU only ever hands over multiples of 4.

Each instruction ticks one machine cycle for the opcode fetch, one for every
later bus read or write, and idle machine cycles for the internal time the
opcode tables list (16-bit increments, taken branches, the delay before a
`CALL` push, and so on). A direct `gb_memory_read` or `gb_memory_write` from a
test does not move the clock. The desktop window can inspect memory the same
way.

## Registers

| Address | Name | Behavior |
| --- | --- | --- |
| FF04 | DIV | High byte of a 16-bit counter that gains 4 on every machine cycle. Any write stores 0. |
| FF05 | TIMA | Incremented on the falling edge described below. |
| FF06 | TMA | Value copied into TIMA one machine cycle after overflow. |
| FF07 | TAC | Bit 2 enables TIMA. Bits 1–0 select the edge. Bits 3–7 read as 1. |

The selected bits of the T-cycle counter are 9, 3, 5, and 7, for TAC rates
`00`, `01`, `10`, and `11`. That is an increment every 1024, 16, 64, or 256
T-cycles (256, 4, 16, or 64 machine cycles). The edge is the falling edge of
`(TAC enable AND selected bit)`. On a DMG, disabling the timer while that bit
is 1, writing a different rate whose bit is 0, or writing DIV while the bit
is 1, increments TIMA once.

## Overflow

When an increment wraps TIMA, it reads as `00` for one machine cycle. The next
machine cycle copies TMA into TIMA and sets IF bit 2 through
`gb_cpu_request_interrupt`. A write to TIMA during the `00` cycle cancels the
copy and the interrupt. A write to TIMA on the reload cycle does not stick.
A write to TMA on the reload cycle is the value copied into TIMA. Writing DIV
or TAC during the `00` cycle does not cancel the reload.

`STOP` writes DIV (so the counter clears) and the CPU does not advance the
timer again until `gb_cpu_leave_stop` clears `cpu->stopped`. A new button
press is what the desktop app uses to leave `STOP`. `HALT` does not stop
the divider.

If IF is already set when a step begins and IME is on, the interrupt is taken
instead of the instruction. Service is 20 T-cycles. If the reload that sets IF
is the opcode-fetch machine cycle itself, that fetch is not executed and the
step reports 24 T-cycles: the 4-cycle fetch plus the 20-cycle service. The
pushed PC is the instruction that did not run. `acceptance/timer/rapid_toggle`
requires this, because otherwise the following `DEC BC` runs before the
service routine.

`HALT` idle also advances the divider. A request that appears during the idle
clears the halted flag; the following step dispatches or runs the next
instruction. Sampling is still once per `gb_cpu_step`, not once per T-cycle.
Each of those machine cycles also advances the picture processor and any
active OAM DMA transfer. See `docs/PPU.md`.
