# Allwinner V3s reference documents

Retrieved 2026-11-22. These are comparative references, **not AM580 documentation**.
The AM580 firmware identifies a Cortex-A7 CPU; the V3s is a documented Cortex-A7
SoC with 64 MB integrated RAM. This does not establish compatible pin assignments,
register addresses, clock/reset bits, or an identical underlying chip design.

## Files and provenance

| Local document | Pages | Download source |
| --- | --- | --- |
| [Datasheet V1.0](Allwinner_V3s_Datasheet_V1.0.pdf) | 420 | [Public document mirror](https://raw.githubusercontent.com/DeciHD/allwinner_docs/main/v3s_v3x_s3/Allwinner_V3s_Datasheet_V1.0.pdf) |
| [User Manual V1.0](Allwinner_V3s_User_Manual_V1.0.pdf) | 420 | [Sipeed Lichee Zero documentation](https://dl.sipeed.com/fileList/LICHEE/Zero/HDK/Allwinner_V3s_User_Manual_V1.0.pdf) |

Both files identify themselves internally as **Allwinner V3s Datasheet,
Version 1.0, November 17, 2014**. They are differently packaged copies of the
same revision, not two complementary specifications. The manual filename is
Sipeed's distribution name. The datasheet mirror retains a confidentiality
watermark and PDF permission restrictions; the Sipeed copy has different
presentation and metadata. Neither downloaded file was modified here.

Contents include pin descriptions, memory mapping, clock control, peripheral
register descriptions, electrical characteristics and package information.
Checks: PDF parsing and page counts, cover rendering, and [SHA-256 hashes](SHA256SUMS).
Allwinner copyright and notices remain in the documents; these third-party
references are not covered by this repository's MIT license.

Manufacturer reference: [V3s product summary](https://www.allwinnertech.com/uploads/pdf/2016080317552746.pdf).
See also [AM580 documentation status](../am580/README.md).
