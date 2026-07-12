---
title: Keyboard Driver
type: code-map
sources:
  - src/drivers/keyboard.c
  - src/kernel/syscalls.c
related:
  - "[[syscall-dispatch]]"
  - "[[architecture]]"
  - "[[harva-dev-kit]]"
created: 2026-07-12
updated: 2026-07-12
confidence: high
---

# Keyboard Driver

IRQ1-driven PS/2 / XT-compatible keyboard driver.  Source:
`src/drivers/keyboard.c`.

## Design

The IRQ1 handler (`keyboard_irq_handler`) runs in response to each scancode
byte from port `0x60`.  It processes the byte through `process_scancode()`,
which maintains state for modifier keys and extended key prefixes, and pushes
decoded events into a 16-entry ring buffer (`kbd_buf`).

Userspace reads via `SYSCALL_READ_CHAR` (returns `(scan << 8) | ascii`; 
`ascii == 0` means a special key) and `SYSCALL_GET_SHIFT` (returns the current
modifier byte: bits 0-1 = Shift, bit 2 = Ctrl, bit 3 = Alt).

The HDK layer (`lib/hdk/kbd.c`) wraps these into `kbd_get()` returning a
normalized `K_*` code and setting `kbd_shift`.

## Scancode handling

| Code | Meaning |
|------|---------|
| `0xE0` | Extended key prefix — sets `ext_prefix = 1`, waits for next byte |
| `0x2A`, `0x36` | Shift make (left/right) |
| `0xAA`, `0xB6` | Shift break |
| `0x1D` / `0x9D` | Ctrl make/break |
| `0x38` / `0xB8` | Alt make/break |
| `0x3B`–`0x44` | F1–F10 (stored as special, scan in high byte) |
| `0x3A` | Caps Lock toggle |

### Extended key fake-shift fix (2026-07-12)

Gray navigation keys (Home, End, PgUp, PgDn, arrow keys, Insert, Delete) are
extended keys.  On many PS/2 controllers they are preceded by an `E0 2A` ("fake
shift make") and followed by `E0 AA` ("fake shift break") sequence:

```
E0 2A  E0 <key-make>   <key action>   E0 AA  E0 <key-break>
```

Without special handling, `0x2A` was treated as a real Shift press
(`shift_pressed = 1`).  When `SYSCALL_GET_SHIFT` was called immediately
after `SYSCALL_READ_CHAR`, it reported SHIFT even though the user only pressed
an arrow/navigation key.  This caused NCD's `K_LEFT`/`K_RIGHT` guard
(`if (kbd_shift & SH_SHIFT)`) to route to "switch panel" instead of
"parent dir / enter dir."

**Fix:** at the top of `process_scancode()`, before the modifier make/break
checks, discard `2A`/`36`/`AA`/`B6` codes that arrive with `ext_prefix` set:

```c
if (ext_prefix && (sc == 0x2A || sc == 0x36 || sc == 0xAA || sc == 0xB6)) {
    ext_prefix = 0;
    return;   /* fake shift around a gray/extended key */
}
```

Real Shift make/break (`2A`/`AA` without a preceding `E0`) is unaffected.
