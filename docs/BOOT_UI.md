# Aurora OS Native Boot UI

Status: **Implemented prototype**
Version: **0.1**

Aurora OS renders its boot experience directly from the kernel framebuffer.

The loading bar is **milestone-driven**, not time-driven. Aurora never sleeps merely to make the animation appear longer. A faster computer should complete the sequence faster.

## Visual direction

The native boot UI follows Aurora's canonical visual language:

- deep navy / black background;
- cyan, blue and violet aurora ribbons;
- star field;
- dark mountain / horizon silhouette;
- reflective lower surface;
- central Aurora mark;
- minimal futuristic typography;
- real progress bar;
- poetic boot-stage easter eggs.

The current implementation is procedural and integer-only. It does not require a filesystem, PNG decoder, floating-point state, or graphics stack.

This is intentional: the screen must work extremely early in boot.

## Real milestone mapping

| Progress | Kernel milestone | Display phrase |
| ---: | --- | --- |
| 4% | framebuffer acquired | CREAZIONE DELL'UNIVERSO... |
| 16% | PMM + VMM online | CREAZIONE DELL'UNIVERSO... |
| 27% | GDT/TSS, hardening and syscall base | ACCENSIONE DELLE PRIME STELLE... |
| 39% | ACPI/MADT/APIC/I/O APIC | SORGERE DELL'AURORA... |
| 49% | monotonic clock online | ALLINEAMENTO DEI FLUSSI COSMICI... |
| 58% | SMP processors brought online | ALLINEAMENTO DEI FLUSSI COSMICI... |
| 66% | tickless timer verified | RISVEGLIO DEL NUCLEO DI AURORA... |
| 74% | heap + clock probe verified | RISVEGLIO DEL NUCLEO DI AURORA... |
| 82% | capability model verified | SINCRONIZZAZIONE DEGLI ORIZZONTI... |
| 88% | bounded IPC verified | SINCRONIZZAZIONE DEGLI ORIZZONTI... |
| 93% | preemptive scheduler verified | SINCRONIZZAZIONE DEGLI ORIZZONTI... |
| 98% | Ring 3 user-space path verified | BENVENUTO NELL'INFINITO... |
| 100% | bootstrap complete | BENVENUTO NELL'INFINITO... |

## Panic behavior

Once the framebuffer boot UI has initialized, a kernel panic replaces the loading screen with a visible Aurora panic screen.

Serial output remains the authoritative technical diagnostic channel.

A panic that occurs before a usable framebuffer exists can only be reported through early serial logging.

## Future evolution

When Aurora has a filesystem, image loader and compositor, the same boot-state API can drive a richer image-backed or animated boot experience without changing the milestone semantics.
