# Aurora Identity — Security Activity Graphics

Status: **Native artwork candidate**
Version: **0.3**

## Safety architecture

Security Activity presentation is deliberately independent from Identity
authority, audit persistence and login/session lifecycle.

The renderer has two presentation paths:

1. verified native PNG artwork;
2. a procedural fail-safe renderer already accepted in main.

Native artwork is optional presentation data. Missing assets, failed decode,
memory pressure or a draw failure cannot widen authority and cannot make login
or the audit backend depend on artwork. The renderer falls back to the
procedural path.

## Asset catalog

The catalog contains 16 text-free PNG assets:

- panel;
- event card;
- info, warning and critical severity indicators;
- authentication, session and credential category icons;
- header/separator;
- empty, error and loading state illustrations;
- load-more control;
- filter button;
- active-filter indicator;
- end-of-history indicator.

Text remains renderer-owned and dynamic for localization and accessibility.

## Deterministic source bundle

Repository source is split into ordered Base64 chunks under:

`kernel/assets/identity/security_activity/source/`

The chunks reconstruct one deterministic gzip-compressed USTAR archive.

Expected archive SHA-256:

`258828456431ac46ac5494539c7d77fb48d1a467757bee5e844a3640de390253`

The deterministic archive is 66,742 bytes before Base64 encoding. Tar member
metadata and the gzip timestamp are fixed, so identical input artwork produces
identical bytes and the same digest.

`scripts/prepare-security-activity-assets.py`:

- concatenates source chunks in Makefile-sorted order;
- performs strict Base64 decoding;
- verifies the archive SHA-256 before extraction;
- accepts only the 16 whitelisted regular-file names;
- rejects duplicate/path-traversal members;
- checks PNG signature and leading IHDR;
- accepts only decoder-compatible 8-bit RGB/RGBA, non-interlaced PNGs;
- publishes files only after the complete archive passes validation.

Generated PNG files are build products and are removed by `make clean`.

## Link boundary

`security_activity_asset_stub.S` in main provides weak zero-length asset
symbols, so the kernel always links even when native artwork is absent.

`security_activity_asset.S` provides strong symbols backed by the verified
PNG files. When this object is built, its symbols override the weak stubs.

Therefore:

- missing artwork -> native initialization fails cleanly -> procedural fallback;
- corrupt source bundle -> build fails before kernel link;
- corrupt/unsupported decoded PNG -> native boot-validation probe fails;
- valid catalog -> native renderer becomes available.

## Validation

Boot-validation performs both independent gates:

1. procedural fallback renderer acceptance;
2. native artwork acceptance.

The native probe:

- decodes all 16 assets;
- verifies every image has non-zero dimensions;
- verifies category/severity/view-state mappings;
- alpha-composites panel and status artwork into a scratch 32-bpp framebuffer;
- checks that framebuffer content changed;
- releases the decoded cache and verifies it is no longer ready.

Production authority is unchanged:

`Session Manager -> Identity client -> scoped AUDIT_READ -> Identity Service -> user-filtered audit records -> headless presentation model -> renderer`

The graphics layer receives no Aurora Key, credential identifier, session grant,
reauthentication proof or audit-store authority.

## Window-system boundary

This completes the Security Activity rendering substrate, not the final
compositor-backed Identity System App. Dynamic system-app windows, focus,
hit-testing, resize/close/decorations and task/window lifecycle remain dependent
on G5 WP-04+.
