# Aurora OS — G5 Desktop Shell & Infinite Living Canvas
Status: **Design in progress — approved decisions D01–D12**
Version: **0.1**
Updated: **2026-10-08**
Authority: **Project design decisions**; not an implementation-completion report.

## 1. Product vision
G5 develops Aurora Hybrid Desktop, a shell-first spatial desktop experience: the user enters through Aurora Hub, can dismiss it to reveal an infinite customizable Living Canvas, and works with applications embedded as spatial panels rather than conventional floating windows. The underlying G5 window protocol, compositor and isolation architecture must remain rigorous. The visual design does not replace the OS process/security model.

## 2. Approved decision register
| ID | Approved decision | Contract |
|---|---|---|
| G5-D01 | Aurora Hybrid Desktop | Unified, flexible desktop experience |
| G5-D02 | Aurora Hub at session entry | Hub displayed on login and dismissible, without ending its service state |
| G5-D03 | Infinite Living Canvas | Pan/zoom and freely place items in a virtually unbounded workspace |
| G5-D04 | Applications embedded in Canvas | Default application UI is a spatial panel, not a free-floating desktop window |
| G5-D05 | Content Fabric | Primary UX organizes content by collections, metadata and relationships instead of folders |
| G5-D06 | Smart Semantic Zoom | At different scales present interactive, preview and compact symbolic views |
| G5-D07 | Adaptive Aurora Navigator | On-demand minimap, spatial search, overview and jump-to-element |
| G5-D08 | Assisted smart organization | Suggest groupings/layouts, apply changes only after user confirmation |
| G5-D09 | Optional detective-board relation view | Nodes/pins and visible strings like an evidence board, optionally shown |
| G5-D10 | Hybrid-assisted links | Users create/edit links; system proposes links for approval |
| G5-D11 | Adaptive Aurora Hub | Immersive full-screen, reduced panel and hidden modes |
| G5-D12 | Modular customizable Aurora Hub | Add/remove/reorder/resize modules; universal search remains accessible |

## 3. User experience
### 3.1 Aurora Hub
- Initially immersive on session entry, with a consistent universal search entry.
- Switches between immersive, panel and hidden without terminating underlying application state.
- Hosts modular system experiences: Browser, Content, Search, Media, Settings, Activity, System. This is an integration goal, not a claim that a complete web engine ships during G5.
- Users configure module order, presence and dimensions; safe default layout and reset-to-default required.
- Integrated experiences are logically separated and should be isolated in independent processes when appropriate; a browser renderer failure must not kill the Shell.

### 3.2 Infinite Living Canvas
- Spatial items retain logical position, size, order, identity, owner and association to a session.
- Camera pans and zooms. Provide keyboard operation and an accessible means to locate elements.
- Spatial application panels run as independently authorized client surfaces; Shell owns placement and policies.
- Render only visible details where possible. Background operations (audio, download, saving) are not automatically suspended because a panel is visually compact or offscreen.
- Customization goals: backgrounds, visual themes, freely arranged cards/widgets and saved layouts. Details of persistence to be scoped against G7.

### 3.3 Semantic Zoom
- Three conceptual representations: full/interactable, summary preview, compact title/icon (not fixed numeric thresholds).
- Smooth geometric zoom; semantic transitions with hysteresis to prevent flicker.
- App state is not discarded by change of scale. No hidden input targets; accessibility/focus must remain predictable.
- Use zoom-level aggregation and bounds/visibility culling for performance.

### 3.4 Navigator
- Hidden by default; invoked when needed.
- Shows visible area relative to occupied elements, rather than trying to depict mathematical infinity.
- Supports overview, universal/spatial search, and camera navigation to a chosen element.

### 3.5 Smart Organization
- User-initiated or system-suggested groupings, alignment and thematic zones.
- Preview, approval, undo, and locks preventing movement of pinned elements.
- Deterministic metadata/rule-based baseline; AI optional and never required for Shell functionality.

### 3.6 Relation Graph — detective-board metaphor
- Content, apps, persons, places, web resources, notes, projects and events can become nodes.
- Relationships are semantic typed edges distinct from their visual strokes.
- Users drag a pin/string between items or select a link command; proposals are reviewed rather than silently committed.
- Optional investigative overlay: pins and colored threads (including familiar red threads), with labels, filtering and editing; hidden in clean view.
- Semantic zoom shows edge details nearby and aggregates/hides minor edges in overview. Permissions apply to relation indexing and results: private nodes must never leak through links or previews.

## 4. Architecture direction
```text
Aurora Desktop Shell (trusted policy)
  ├─ Hub Controller + module host
  ├─ Living Canvas Manager + camera/scene policy
  ├─ Navigator + Spatial Search facade
  ├─ Smart Organization proposals
  ├─ Relation Graph view
  └─ Window/Spatial Surface policy
       ↕ capability-scoped window protocol
Aurora Compositor / input routing / Display backend
       ↕ authorized surfaces
Isolated Ring 3 system and third-party application processes
```

- Policy and compositor remain distinct; untrusted clients cannot manage foreign surfaces or steal activation/focus.
- Secure session logout, process/surface teardown and generation checks must revoke references, input and graphics authority.
- Hub chrome and Canvas application surfaces follow scene layering and explicit input-focus rules.
- Prefer a logical spatial surface/role model compatible with existing toplevel configure/ack and tokens, not an incompatible replacement of verified G5 contracts.
- AuroraFS can retain hierarchical paths and POSIX-like compatibility even though folder navigation is not the primary GUI. Content Fabric logical collections never imply duplicate physical files.
- Identity/login compositor migration belongs to G6, and full Activity Spaces reconstruction/persistence to G7. G5 may lay compatible groundwork.

## 5. G5 delivery boundaries (proposal; not yet approved)
- G5.1: protocol/lifecycle hardening, teardown, concurrent Ring 3 tests.
- G5.2: compositor spatial scene, focus, mouse interactions, zoom/pan.
- G5.3: live application panels and semantic representations.
- G5.4: minimal modular Hub, system-module host, basic launcher/search.
- G5.5: Navigator, manual relation pins/links, basic suggested layouts.
- G5.6: QEMU/CI end-to-end, security, performance and recovery tests.
- Rich Content Fabric, full native browser engine and advanced graph indexing may require later dedicated milestones, not blockers for a coherent first G5 release.

## 6. Quality and acceptance requirements
- Demonstrate multiple isolated Ring 3 clients with spatial panels, move/resize/focus and correct input routing.
- Demonstrate Hub enter/reduce/hide/restore without losing app state.
- Show navigation to offscreen content; semantic zoom behaves without flicker or stale input.
- User-approved relation creation and undo; rejected proposals have no effect.
- No cross-client graphics/control privilege escalation, stale-generation use or cross-session content leak.
- Closing a process/session cleans all spatial objects and tokens safely.
- Failure injection: broken module/browser does not terminate system shell; safe graphics recovery remains available.
- Explicit runtime QEMU tests and CI evidence are required before marking any item implemented.

## 7. Pending design decisions
- Actual Hub module layout, module lifecycle and drag/drop UX.
- Camera gesture details, shortcuts, zoom thresholds and 64-bit coordinate limits.
- Widget security model, spatial surface protocol and focus strategy.
- Content Fabric storage/index format and relation graph semantic schema.
- Visual theme tokens, accessibility and motion-reduction behavior.
- G5/G7 persistence boundary; browser engine milestone.
- Definition of the G5 minimal shippable acceptance gate.

## 8. Change control
Each approved decision adds an ID and a short design contract. Mark proposed features as proposals until accepted. Design approval never implies source implementation or runtime verification. Update this document incrementally; link implementation evidence to the graphics implementation roadmap rather than rewriting achieved status here.
