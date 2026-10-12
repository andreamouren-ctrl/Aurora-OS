# Aurora OS Native Boot UI

Status: **Recovery-only early framebuffer renderer**
Version: **0.2**

The lightweight early framebuffer UI is reserved for **fatal boot recovery**. In a healthy boot Aurora does not render the provisional early logo, text, status panel or bar. It clears firmware residue to deep navy and publishes only the completed native Aurora Identity presentation when the renderer is ready. The full-quality Identity artwork/font are the normal boot destination.

The early boot system **tracks** real milestones without painting them on the healthy path. If the kernel panics, the embedded recovery scene is drawn and the last reached milestone is shown. The bar is milestone-driven rather than time-driven.

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

Once the usable framebuffer is acquired, the kernel registers the low-level emergency renderer **before** display-bootstrap initialization. A kernel panic makes the provisional emergency artwork visible, with an explicit `AVVIO DI EMERGENZA` headline, last reached boot milestone and brief failure reason.

Serial output remains the authoritative technical diagnostic channel.

A panic that occurs before a usable framebuffer exists can only be reported through early serial logging.

## Future evolution

Identity's full-quality compositor scene remains independent of the emergency renderer. Future animated/graphical boot presentation must never depend on the emergency surface, and recovery must remain usable even when the compositor or artwork loader fails.

## 2026-10-12 — Formal decision: emergency splash only

The project owner confirmed that the original early bitmap/primitive
splash, including its alternate mark, is **not** the final branded splash.
It must remain hidden on all successful boot paths and may become visible
only after a fatal bootstrap error.

### Runtime invariants

1. `boot_ui_init()` validates/caches framebuffer geometry and performs a
   single dark clear; it **never decodes or paints** early splash assets.
2. `boot_ui_stage()` updates only the in-memory real milestone. Even in
   validation builds, it does not reveal the provisional splash.
3. `boot_ui_complete()` initializes the official Identity renderer but
   still does not paint the provisional splash. Identity is responsible for
   publishing the final secure pre-session compositor frame.
4. `boot_ui_panic()` activates the independent embedded image/bitmap
   renderer on fatal boot failures, with the actual milestone and
   serial-backed diagnostics. The fallback requires no filesystem, heap,
   service, font parser or compositor.
5. Failed Identity artwork decoding remains a nonfatal degraded presentation
   path: the existing credential UI retains the authority policy and its
   procedural fallback. Authentication never succeeds because of missing art.
6. Panics before any usable framebuffer exists can only report via serial.
   Firmware/bootloader imagery before kernel entry is outside this policy.

The prior lower-panel occlusion/layout fix is retained for the **emergency**
presentation only. Validation/QEMU graphics probes may independently paint
diagnostic buffers; those tests remain intact and should not be mistaken for
normal startup UX. Test normal visuals with `BOOT_VALIDATION=0` while still
running the separate boot-validation and cold-boot test suites.
