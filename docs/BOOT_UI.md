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

## 2026-10-12 — Fix layout e avanzamento visivo

La prima registrazione QEMU ha mostrato una fascia scura troppo alta
(47%-94%) che copriva l'emblema e la grafica di avvio. Adesso solo il
pannello di stato inferiore (x 12%-88%, y 65%-93%) viene aggiornato.
Il titolo, la barra e una sola frase corrente rimangono separati dal
marchio centrale; non vengono piu' disegnate sette frasi sovrapposte.
I passaggi di stato aggiornano la UI sia con `BOOT_VALIDATION=0` sia
con `BOOT_VALIDATION=1`, senza attendere artificialmente tra le fasi.
L'ultimo frame del bootstrap non viene mai cancellato deliberatamente
prima della pubblicazione del primo frame di Identity.

Attenzione: la build di validazione contiene test che presentano veri
buffer sul display e possono cancellare temporaneamente la splash e
mostrare campioni colorati. Questi non sono effetti desiderati nel
normale boot, percio' la validazione funzionale e la verifica visuale
`BOOT_VALIDATION=0` hanno finalita' distinte. I test non vanno rimossi
per nascondere il problema; per una sequenza pulita va registrata la
build normale e vanno mantenute le verifiche QEMU separate.
