# Firmware save handling

Build with `make -C firmware`; run host fault-injection tests with
`make -C firmware test` (Python 3 and GCC). The tests exercise the production
backup and SD routines with mock file/card I/O, plus the actual FatFs disk adapter.
They cover failed opens, short writes, read/close errors, rename rollback,
backup recovery, snapshot CRCs, GSU range selection, SD response/busy/status errors,
SDHC/SDSC addressing, timer wrap, and FatFs `CTRL_SYNC` error propagation.
They do not replace validation on a real card.

## Saves

- Restore while the core is loading. Missing saves start with initialized RAM;
  shorter files get initialized tails (zero for SNES, `0xFF` for GBA). Read/open/close errors and oversized files
  disable autosave for that game. If the final file is missing, try its `.bak`.
- Capture BSRAM once into an RV RAM buffer. CRC and file data use that same image.
  SNES is not paused: writes during capture can still produce a mixed snapshot.
- Non-GSU change detection covers the full save. GSU change detection covers
  offsets `0x7C00-0x7FFF`, under the assumption that these contain all required
  persistent data. The complete `.srm` size/layout is retained; other GSU bytes
  read through RV can be stale because they may still reside in the cache.
- Write and close `name.srm.tmp` before moving an existing save to `name.srm.bak`.
  Install the temporary file and then remove the old backup. A failed install
  attempts rollback; failed rollback leaves `.bak` for recovery at next load.
  Update the CRC only after successful installation. This protects the previous
  file against reported I/O failures; FAT/exFAT metadata updates are not atomic
  against sudden power loss.
- GBA clears its dirty flag before capture, preserving later hardware writes.
  Failed saves remain eligible for retry through a software flag.

`BACKUP_MAX_SIZE` defaults to 131072 bytes (128 KiB). Larger saves are refused
rather than overflowing the buffer; raise it with a forced rebuild, for example
`make -C firmware -B BACKUP_MAX_SIZE=262144`. Check RV RAM and stack space before
raising it, especially on Nano (1 MiB RV RAM). Existing file name arguments are
used directly, with bounded paths and room for recovery suffixes.

## SD writes

Single-block writes check the command and data-response token, wait up to one
second for the card to release busy, then check both CMD13 status bytes. FatFs
`CTRL_SYNC` checks card readiness/status too, so `f_close` reports failures.
Initialization resets the addressing mode, validates CMD8/CMD58, and sets a
512-byte block length for SDSC cards. Sector ranges that overflow the command
address are rejected. Card removal, SPI electrical faults, and power-loss
behavior still require hardware testing.
