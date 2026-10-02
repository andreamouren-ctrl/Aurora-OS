# AuroraFS v2 — Level-3 milestone completion

Status: **runtime-verified and complete for the current bounded Level-3 design**.

AuroraFS v2 currently supports extent-tree roots through Level 3. The public hierarchy contract is:

- node fan-out: 126 entries;
- Level 1 capacity: 15,876 mappings;
- Level 2 capacity: 2,000,376 mappings;
- Level 3 capacity: 252,047,376 mappings;
- maximum supported root level: 3.

The Level-3 implementation is complete across the supported structural and persistent boundaries:

1. full Level-2 root growth into a Level-3 root;
2. continued append under an existing Level-3 root;
3. full final leaf growth;
4. full final Level-1 sibling growth;
5. full final Level-2 sibling growth;
6. persistent inode publication for every Level-3 mutation path;
7. reopen lookup of both newly appended and historical mappings;
8. child-first copy-on-write publication with the inode updated last;
9. explicit rejection of the 252,047,377th mapping while the root level remains capped at 3;
10. rejection leaves the old root, inode and allocation bitmap byte-identical.

All synthetic hierarchy gates are exercised on both 512-byte and 4096-byte logical block-device geometries. This is logical 4Kn-path verification, not certification on physical 4Kn hardware.

## Final runtime validation

Aurora OS Bootstrap Build run `37032168538` (#576), head `614838c47443020773f6a70c41497b50a710c4b4`, completed successfully after the Local APIC one-shot wake-up floor was made time-based rather than a fixed tiny tick count.

The workflow completed:

- kernel + ISO build;
- ELF inspection;
- BIOS/q35 smoke boot;
- ATA PIO storage boot;
- AuroraFS persistent reopen path;
- FAT32 and exFAT smoke coverage;
- the full AuroraFS v2 self-test chain.

The Level-3 capacity gate emitted by the boot suite is:

`[aurorafs-v2] full level-3 capacity limit rejects 252047377th mapping without mutating tree/inode/bitmap on 512/4096-byte devices`

## Scope boundary

Level 4 is intentionally not part of this milestone. Raising the root-level bound requires a new documented on-disk hierarchy milestone rather than an implicit extension of the current code.

The next AuroraFS work should therefore move away from extent-tree hierarchy growth and toward higher-level filesystem capabilities or recovery/durability work, unless a future milestone explicitly authorizes Level 4.
