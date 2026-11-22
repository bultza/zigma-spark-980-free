# AM380 MCU documentation and firmware acquisition

Research date: 2026-11-22. Scope: local binary analysis and public-document
research only. No robot connection, debug attach, firmware execution or writes.

## Datasheet status

**The complete AM380E/AM380S datasheet was not located.** The PDF saved here is
an Amicro patent, not a datasheet. Pinout, electrical limits, debug-lock bits,
readout-protection behavior and a complete memory map remain unavailable.

The project identifies the chassis controller as AM380. The existing
[board photograph](../../images/zigma_chassis_controller_board_AM380_top.jpg)
appears to show **AM380E**; treat the suffix as a photographic reading to confirm,
not proof for every robot variant. Do not substitute an AM380S pinout or assume
STM32-compatible debug/flash registers. The AM580 Linux processor is separate.

Searches covered AM380/AM380E/AM380S, English and Chinese datasheet/manual terms,
manufacturer pages and indexed PDFs. Many results refer to unrelated MOSFETs,
TI processors or Asus Merlin firmware. None is an AM380 controller datasheet.

Manufacturer references:

- [Current product catalogue](https://www.amicro.com.cn/en/products): no complete
  AM380 datasheet was retrieved from this page.
- [Manufacturer history](https://www.amicro.com.cn/en/about-history): identifies
  AM380S as an Amicro product and distinguishes it from AM580.
- [Manufacturer contact](https://www.amicro.com.cn/en/contact): suitable route to
  request AM380E datasheet, pinout, debug and bootloader documentation. No message
  was sent.
- Legacy product link `https://en.amicro.com.cn/?product/chip/master/1675.html`
  could not be retrieved successfully during this search.

## Primary evidence for a debug interface

[Amicro patent CN112034330A](https://patents.google.com/patent/CN112034330A/en),
original applicant Zhuhai Amicro Semiconductor, explicitly names AM380S and
AM380E in a factory-test setup. It describes SWD access, loading code into flash
or SRAM and reading test results from SRAM. Paragraphs 0033–0036 describe the
chip selection and example execution addresses, flash `0x0` and SRAM
`0x20000000`. These are a factory-test description, not a guaranteed memory map
or unlocked readback interface on a production robot. The described memory
tests overwrite contents and are unsuitable for firmware preservation.

Local original: [CN112034330A-Amicro-SWD-QC.pdf](CN112034330A-Amicro-SWD-QC.pdf),
14 pages, 773730 bytes. Downloaded from
[Google Patents PDF storage](https://patentimages.storage.googleapis.com/24/5b/39/4c4dded7ac51a6/CN112034330A.pdf).
Integrity recorded in [SHA256SUMS](SHA256SUMS).

**Inference:** SWD is a substantially better-founded hardware acquisition lead
than guessing an undocumented UART command. Availability and protection on this
board still require independent confirmation.

## Candidate acquisition routes

| Route | Evidence and feasibility | Current blocker |
| --- | --- | --- |
| Obtain the matching MCU update image (`.afw`) | `upgrade380` parses an image supplied by Linux. An offline decoder could recover its contents without touching the MCU. This obtains distributed firmware, not necessarily the exact installed bytes or per-device data. | No matching MCU image identified in the reviewed original Caccamora filesystem paths. Package format and encryption require further offline analysis. |
| Read through SWD | Primary manufacturer evidence supports investigating this interface. If accessible and readable, it may permit a memory dump. | Exact package/pins, board routing, voltage, debug access state, memory map and read permissions unconfirmed. |
| UART bootloader | Update transport and message handling have been reverse-engineered locally. | No memory-read command found in the inspected updater. Update capability is not readback capability. |
| Normal MCU UART / SPI | Navigation has a diagnostic text reader and normal control messages. | No verified arbitrary-memory-read or firmware-dump operation identified. |
| External flash, if present and used for MCU code | A separate memory device could potentially be read independently. | The actual MCU program storage on this board has not been established. An eight-pin chip or a Linux flash dump is not sufficient evidence. |

## Why `upgrade380` must not be used as a dumper

See [MCU–Linux communication analysis](../../hack/MCU-Linux-Comms.md).
The observed UART update flow is F1/F2 handshake, F3/F4 erase, F5/F6 erase
status, F7/F8/F9 blocks/acknowledgements, FA/FB completion. With an image prepared,
a valid handshake can lead automatically to erase. The program also stops
navigation and manipulates MCU reset signals before transferring firmware.

Its file-reading and decryption routines consume the host's input image; they
are not evidence of reading firmware back from the MCU. Exported names include
`set_firmware_data`, `decrypt_data` and `xxtea_decrypt_main`. Reimplementing only
the parsing/decryption offline is preferable to invoking the updater to inspect
an image. No command-line dump/readback mode was identified in the inspected
usage string and downloader paths; this does not rule out undocumented MCU
bootloader features.

## Preservation-first next steps

1. Confirm the chip suffix/package and map any debug pads using board evidence;
   obtain the actual voltage and pin assignments before attaching hardware.
2. Continue offline search for the exact MCU update package and analyze its
   parser. Never flash a different robot's image merely to test compatibility.
3. If SWD is mapped, prepare an explicitly read-only identification/readback
   procedure with all automatic flash programming, recovery and unlock actions
   disabled. Merely connecting a debugger can affect execution; this is a later
   physical test, not an action taken during this investigation.
4. If reads are blocked, record that result and stop. AM380-specific protection
   removal semantics are unknown; do not assume an unlock preserves firmware.
5. If reads succeed, obtain two independent dumps, compare their hashes, retain
   the raw bytes and separately identify application, bootloader and calibration
   areas before interpreting or changing anything.

Conclusion: firmware extraction is plausible, with SWD the best-supported
hardware lead and an update package the least intrusive analysis route. A
working extraction method for this particular MCU has **not** been demonstrated.

## English translation

[English PDF](CN112034330A-English.pdf): complete unofficial translation,
retaining all 14 pages, claim and paragraph numbering, and the three diagrams
with English labels. Font sizes and wrapping differ to accommodate English.
See [translation notes](TRANSLATION-NOTES.md) for source inconsistencies
retained without silent correction. The original Chinese PDF is unchanged.
