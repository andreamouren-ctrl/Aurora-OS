# Aurora Identity — Security Activity Graphics

Status: **Integrated renderer substrate**
Version: **0.1**

## Purpose

This module is the graphical presentation substrate for the authenticated user's
Security Activity view. It is deliberately separate from audit persistence,
authorization and record retrieval.

The graphics layer never reads Protected State and never decides which audit
records a caller is allowed to see. Its inputs are the already-scoped headless
Security Activity model and semantic presentation state.

## Asset catalog

The catalog contains 16 text-free RGBA PNG assets:

- main Security Activity panel;
- event card;
- informational, warning and critical indicators;
- authentication, session and credential category icons;
- section header/separator;
- empty, error and loading state illustrations;
- load-more control;
- reusable filter button;
- active-filter indicator;
- end-of-history indicator.

Text remains dynamically rendered so localization, accessibility and scaling do
not require regenerating artwork.

## Reproducible asset pipeline

The runtime artwork is stored as a deterministic Base64-encoded tar bundle under
`kernel/assets/identity/security_activity/source/`.

`scripts/prepare-security-activity-assets.py`:

1. concatenates the ordered source chunks;
2. performs strict Base64 decoding;
3. verifies SHA-256
   `a28a2ff0008a5b4d3287356a3a1cc52b10f79e1da44040a8739649096bf26ed4`;
4. accepts only the 16 explicitly whitelisted filenames;
5. rejects path traversal/non-regular archive members;
6. verifies PNG signatures;
7. regenerates the PNG files consumed by the kernel build.

The generated PNG files are build products; the chunked bundle plus expected
digest are the repository source of truth.

## Renderer contract

`security_activity_graphics` decodes and caches the asset catalog on demand,
supports bilinear scaling and alpha-composites RGBA artwork onto a 32-bpp Aurora
framebuffer.

Semantic helpers map:

- presentation category -> authentication/session/credential icon;
- presentation severity -> info/warning/critical indicator;
- view state -> loading/empty/end/error illustration.

Unknown semantic values return no graphic rather than guessing.

The asset catalog is loaded lazily. Production boot does not retain these
decoded images unless a future Security Activity surface explicitly opens them.

## Validation

Boot-validation builds execute a dedicated acceptance probe that:

- decodes all 16 assets;
- verifies every decoded image has non-zero dimensions;
- verifies semantic mapping;
- draws the panel and an alpha-bearing status icon into a scratch framebuffer;
- verifies the framebuffer changed;
- releases all decoded artwork and verifies the cache returned to the not-ready
  state.

A corrupt, incomplete or wrongly mapped bundle therefore fails the boot gate.

## Security boundary

The renderer:

- receives no Aurora Key;
- receives no credential id;
- receives no grant or re-authentication proof;
- receives no audit-store capability;
- cannot widen user scope;
- cannot reinterpret a rendered item as authorization.

Security Activity authority remains:

`Session Manager -> Identity client -> scoped AUDIT_READ -> Identity Service -> user-filtered audit record`

The renderer consumes only the resulting presentation model.

## Window-system boundary

This work intentionally does **not** create a private framebuffer-only account
manager or bypass the Aurora Shell.

The final post-login Aurora Identity System App must be a normal
compositor-backed system application. Dynamic toplevel creation, focus,
hit-testing, resize/close/decorations and task/window lifecycle belong to G5
WP-04+.

Until that dependency is complete, this module freezes the visual asset and
rendering substrate so the later System App can bind to it without changing the
Identity audit authority model.
