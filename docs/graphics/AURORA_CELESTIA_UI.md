# Aurora Celestia UI 1.0 — System Typeface Candidate

Status: **design release 1.0 verified locally; runtime integration pending**.

Aurora Celestia UI is the original geometric, softly rounded, space-inspired proportional typeface selected for Aurora OS.

- Weights: Regular 400, Medium 500, SemiBold 600, Bold 700.
- Formats: TTF, OTF, WOFF2 for each weight (12 files).
- Coverage: 802 code points, including Italian, French and US English, currency and technical symbols.
- Font source and release artifacts are produced separately; this document does **not** mean the binary font has been committed.
- License for public redistribution must be approved by the project owner before any public release.
- This is not Aurora Celestia Mono; fallback fonts remain required for other scripts, emoji and rare symbols.

## Intended repository layout

```text
assets/fonts/aurora-celestia/ui/v1.0/
  AuroraCelestiaUI-Regular.ttf
  AuroraCelestiaUI-Medium.ttf
  AuroraCelestiaUI-SemiBold.ttf
  AuroraCelestiaUI-Bold.ttf
  AuroraCelestiaUI-{Regular,Medium,SemiBold,Bold}.otf
  AuroraCelestiaUI-{Regular,Medium,SemiBold,Bold}.woff2
```

The original package is named `AURORA_CELESTIA_UI_Versione_1.0_COMPLETA.zip`. Import exact release files only; verify hashes before committing. Typography is not automatically connected to the current bitmap bootstrap/recovery font or the compositor.

## Acceptance required before shipping

1. Verify 12 font signatures, SHA-256, family and style naming, Unicode mappings and kerning.
2. Verify Italian, French and US English samples, accents, money symbols, numbers, and punctuation.
3. Test 10–18 px readability and scaling on both light and dark Shell themes.
4. Integrate a bounded text layout/rasterizer in Ring 3; preserve a safe fallback for missing glyphs.
5. Keep kernel early-boot/recovery renderer independent of font parsing.

Do not describe this candidate as a font already deployed in the OS or as universally Unicode-complete.
