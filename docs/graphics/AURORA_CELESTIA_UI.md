# Aurora Celestia UI 1.0 — System Typeface Candidate

Status: **release 1.0 binaries committed and byte-for-byte verified on `main`; runtime integration pending**.

Aurora Celestia UI is the original geometric, softly rounded, space-inspired proportional typeface selected for Aurora OS.

- Weights: Regular 400, Medium 500, SemiBold 600, Bold 700.
- Formats: TTF, OTF, WOFF2 for each weight (12 files).
- Coverage: 802 code points, including Italian, French and US English, currency and technical symbols.
- All 12 binary fonts are committed in `assets/fonts/aurora-celestia/ui/v1.0/` on `main` (upload commit `62795c1e4aff264cacdfeed880ed76920752a66a`).
- License for public redistribution must be approved by the project owner before any public release.
- This is not Aurora Celestia Mono; fallback fonts remain required for other scripts, emoji and rare symbols.

## Repository layout

```text
assets/fonts/aurora-celestia/ui/v1.0/
  AuroraCelestiaUI-Regular.ttf
  AuroraCelestiaUI-Medium.ttf
  AuroraCelestiaUI-SemiBold.ttf
  AuroraCelestiaUI-Bold.ttf
  AuroraCelestiaUI-{Regular,Medium,SemiBold,Bold}.otf
  AuroraCelestiaUI-{Regular,Medium,SemiBold,Bold}.woff2
```

The original package is named `AURORA_CELESTIA_UI_Versione_1.0_COMPLETA.zip`. All 12 Git blob IDs were compared with `git hash-object` of the locally validated original release and matched. Typography is not automatically connected to the current bitmap bootstrap/recovery font or the compositor.

## Verified release SHA-256 manifest

All values below are for the original bytes in the committed release; Git blob SHA-1 comparison established byte-for-byte correspondence with `main`.

```text
4637123e5ac90e21e9b16efc0ec8c6ee3a75d17727f33305137e3539b54eee59  AuroraCelestiaUI-Regular.ttf
607f4061ffc148ac949faa4739bae53f0ec0ed298adac7827fa5c951ead23092  AuroraCelestiaUI-Medium.ttf
22eb223074e1a98393c94526b76d1dcd4f46b8eec4fb4d1e530fc05c5caaec2e  AuroraCelestiaUI-SemiBold.ttf
f9a1855012a8e5c36d4522aa4e99ee1f8eb1ad565d35287e903d60f9a42a399c  AuroraCelestiaUI-Bold.ttf
8ff1d9305513741dea75d05bce98546adaa9ee6c25386600d20e80802e207ef8  AuroraCelestiaUI-Regular.otf
700a2dba3814a7212419c9aa9775f9b1605b845f83d5be6b852d0c1fd0e449bd  AuroraCelestiaUI-Medium.otf
a2b4d488e727a467458e9f84db42cb249ab81db015d8dd07047302e59c2f35c7  AuroraCelestiaUI-SemiBold.otf
edb942bb123f4b7bc872acf35c25a3b44add8406048658ceb4116687714b209d  AuroraCelestiaUI-Bold.otf
f8085d29df725984a26b71db42213ee9bbd537650c4aabea2e97c7b918c06992  AuroraCelestiaUI-Regular.woff2
9123a4d514ceb7872cc42b65861b8235e7d395ccb15ff68474e050ec4c189578  AuroraCelestiaUI-Medium.woff2
09165445c8a5a839a81e8f1348241046b60329bf5b7b2e60f59c7e38e34076b6  AuroraCelestiaUI-SemiBold.woff2
96ec98fc792817f53ffe22fccc19530f3485af06ca57ef054bb12152d5198c87  AuroraCelestiaUI-Bold.woff2
```

Validation: 12/12 Git blobs match, all 12 parse with fontTools, 802 mapped Unicode characters per font, family `Aurora Celestia UI`, version `1.0.0`. Tests in the actual Aurora Ring 3 graphics stack and font rasterizer remain pending.

## Acceptance required before shipping

1. Verify 12 font signatures, SHA-256, family and style naming, Unicode mappings and kerning.
2. Verify Italian, French and US English samples, accents, money symbols, numbers, and punctuation.
3. Test 10–18 px readability and scaling on both light and dark Shell themes.
4. Integrate a bounded text layout/rasterizer in Ring 3; preserve a safe fallback for missing glyphs.
5. Keep kernel early-boot/recovery renderer independent of font parsing.

Do not describe this candidate as a font already deployed in the OS or as universally Unicode-complete.
