# AM580 Linux SoC documentation

Research date: 2026-11-22.

**No AM580 manufacturer datasheet or technical reference manual was located.**
There is no datasheet PDF in this directory. This note records the search
result; it is not a substitute for a manufacturer specification.

## Identification

The project identifies the Linux processor as Amicro AM580, with device-tree
compatible `amicro,am580`; see the [hardware overview](../../README.md) and
[main-board photograph](../../images/zigma_logic_board_AM580_top.jpg).
It is separate from the [AM380 chassis MCU](../am380/README.md).

Amicro's [official history](https://www.amicro.com.cn/en/article/about-history)
identifies AM580 as a dedicated LiDAR-navigation chip, with successful tape-out
in its 2017–2018 period. This establishes the product identity, not its pinout,
electrical specifications or register map.

## Search results and limits

English and Chinese searches covered AM580 with Amicro, datasheet, PDF,
芯片, 数据手册 and 手册, plus GitHub-targeted searches. No matching datasheet
was found among the returned results or the locally indexed project PDFs.
The [current manufacturer catalogue](https://www.amicro.com.cn/en/products)
was accessible, but no AM580 datasheet was retrieved from it. This is a
search result, not proof that a document does not exist or is necessarily
restricted by an NDA.

Many AM580 results describe a chemical compound, an AM-580 radio, or unrelated
semiconductors containing a similar part number. Those documents must not be
used as AM580 SoC documentation. The AM380 patent in the adjacent directory is
also not an AM580 datasheet.

## Documents to request from Amicro

Use the [manufacturer contact page](https://www.amicro.com.cn/en/contact) to
request the AM580 datasheet and technical reference manual, including package
pinout, electrical limits, clock/reset/power management, boot modes, memory map,
and peripheral register descriptions. Hardware design guidelines, errata and
Linux BSP documentation would also be useful. Ask whether the documents may be
redistributed before adding any privately supplied material to this repository.

No manufacturer message was sent. No robot connection or hardware operation
was performed during this search.
