# Aurora OS — G5 Desktop Shell & Infinite Living Canvas
Status: **Design in progress — approved decisions D01–D20**
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
| G5-D13 | Hub-to-Canvas module transfer | Drag a compatible Hub module onto Living Canvas as a spatial panel, preserving its logical state and permissions |
| G5-D14 | Bidirectional Hub ↔ Canvas transfer | Move compatible modules in either direction with preserved state and permissions; simultaneous multi-view instances are not included in this decision |
| G5-D15 | Fully adaptive modules | Module interface automatically reorganizes available controls, information density and presentation in response to panel geometry, Hub/Canvas context and semantic zoom; application state remains intact |
| G5-D16 | Aurora Smart Transfer | Cross-module drag-and-drop offers context-sensitive, compatible actions such as open, embed, copy or link; explicit consent and capability controls protect data |
| G5-D17 | Aurora Canvas History | Undo/redo, restore points and a navigable timeline for Canvas layout and relation changes; no implicit rollback of application-internal data |
| G5-D18 | Spatial and semantic selection | Multi-select by rectangle/lasso and semantic graph criteria, plus group transforms, alignment and locks, subject to permissions |
| G5-D19 | Hybrid Aurora Spatial Groups | Visual spatial regions and semantic memberships coexist; groups may be moved, collapsed to one node or expanded without duplicating content |
| G5-D20 | Aurora Spatial Focus: contextual | Center the selected item/group, emphasize relevant items and links and dim unrelated Canvas regions; easily reversible with no layout mutation |

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


### 3.7 Hub-to-Canvas module transfer (G5-D13)
- A compatible system module in the adaptive Hub can be dragged onto Living Canvas and presented as a spatial panel at the drop location.
- Transfer preserves the same logical module/application session and its state (for example current browser page, document context or media playback), without creating an unintended duplicate session.
- The operation changes presentation/placement ownership through trusted Shell policy; it does not grant new privileges or move client execution into the Shell process.
- A drag preview indicates intended placement. On failure or cancellation, Hub and app state remain unchanged. On successful transfer, focus and input routing switch atomically to the authorized Canvas surface.
- Presentation must respect capability-scoped configure/ack, surface generation, session scope, and secure input restrictions. A crashed module cannot crash the Shell.
- G5-D14 approves returning a Canvas panel to Hub: the Shell performs a bidirectional, transactional presentation reparenting while keeping the same logical application instance and state. Failure or cancellation leaves the original presentation intact.
- Neither direction may bypass surface generation, configure/ack, authorization, focus, input or session boundaries. The transfer must not silently duplicate a process, live session or view.
- Simultaneous multi-view instances, cloning and persistent Hub shortcuts remain candidates for later decisions, not yet approved.


### 3.8 Fully adaptive Hub and Canvas modules (G5-D15)
- One application/module retains a stable logical identity and underlying session when moving between Hub and Canvas or changing size.
- The Shell supplies authorized layout context: container dimensions, available input modalities, scale/zoom representation and visibility. It must not dictate untrusted application internals.
- Modules respond through responsive layout variants automatically: compact (essential controls), regular (standard interactive UI), expanded (richer tools/data), and zoom-derived summary/icon presentations. These are conceptual variants, not user-selectable manual layout presets.
- Resize and context transitions must not discard unsaved edits, navigation/media state, accessibility focus or application permissions. Focus continuity should be preserved when feasible; otherwise a safe accessible focus target is chosen.
- Geometric resize and semantic zoom are distinct: enlarging a panel at the same camera zoom may reorganize controls, while zooming the camera out may switch to summary/icon representation without mutating panel dimensions.
- Transitions should be stable (hysteresis, minimum readable control size, no oscillation at thresholds), performance-bounded and compatible with reduced-motion settings.
- Module authoring contract should expose size-class changes and semantic-level change notifications through a versioned, capability-scoped protocol; clients must not be able to alter placement or grant themselves elevated Shell privileges.
- Fallback for non-adaptive or legacy clients: bounded scaling/letterboxing or scroll containers with Shell-managed chrome; never claim fully adaptive behavior for an incompatible client.
- Validation: resize Hub panel and Canvas panel, transfer both directions, change zoom, verify continuity of unsaved text/state, focus/input ownership, and absence of oscillating layout.


### 3.9 Aurora Smart Transfer (G5-D16)
- Aurora supports user-initiated drag-and-drop between Hub modules, Canvas spatial panels, Content Fabric objects and relationship nodes.
- A drag carries a bounded, typed **transfer offer**, not an unrestricted file pointer or direct access to the originating process. The receiving module advertises compatible operations, such as **open**, **embed**, **copy** or **link**; only genuinely supported choices appear.
- The user chooses the operation where ambiguous or consequential. Hover/preview must never silently mutate data or establish a permanent relation.
- **Open** invokes the destination app on an authorized read handle; **embed** creates an app-defined embedding/reference only with explicit destination support; **copy** creates distinct content only when confirmed; **link** creates a semantic Content Fabric/Relation Graph edge without duplicating the payload.
- Drag success must be acknowledged by the destination before the Shell treats the operation as completed. Cancellation, target disappearance, session change, timeout or failed authorization must leave the source intact and cleanly revoke temporary transfer capabilities.
- All transfer authority is session-scoped, short-lived and operation-specific. Cross-application data transfer is mediated by the trusted Shell/content broker with data ownership, type/size bounds and access checks. Untrusted applications must not impersonate drop targets or read a drag without being the authorized receiver.
- Respect secure surfaces, private objects, accessibility/keyboard drag alternatives and contextual focus. Transfers cannot bypass Aurora Identity or client isolation.
- Initial G5 acceptance: native test clients exchange a document/image reference with explicit open/copy/link alternatives, an approved graph relation is reflected in Canvas, and rejected/aborted transfers produce no side effects.
- Automatic AI interpretation and arbitrary format conversion are future extensions, not required for the deterministic Smart Transfer baseline.


### 3.10 Aurora Canvas History (G5-D17)
- Keep an **undo/redo transaction journal** for user-visible Canvas-structure changes: item placement, size, grouping, organization choices, visual relationship edits, and compatible Hub↔Canvas presentation moves.
- Offer **restore points** and a **navigable timeline** with preview-before-restore. Restoring a prior arrangement must not silently delete documents, reverse external web actions, or alter a module's own persisted data.
- Define typed reversible commands with preconditions, object-generation checks, transaction IDs, actor/session scope and inverse operations; record atomic composite operations (such as accepted automatic layout and approved Smart Transfer link) as one logical history entry.
- A restore creates a new current state rather than erasing past history. Failed or inapplicable operations leave the live Canvas unchanged and offer a precise conflict explanation.
- Memory and storage use must be bounded via journal compaction, checkpointing and explicit retention policy; persistent recoverability across reboot is coordinated with G7, while G5 supports live-session history.
- Journal entries must not embed credentials, full private document payloads, arbitrary URLs with secrets or privileged capabilities. Stale/unauthorized references are sanitized and cannot be resurrected by undo/restore.
- Multiple-client concurrency and module teardown require clear conflict handling; only Shell-authorized operations enter Canvas structural history. Future collaborative history is out of scope.
- Accessibility: keyboard undo/redo, readable timeline entries and restore confirmation showing which Canvas objects will change.
- Acceptance: verify placement → link → group → undo/redo; save restore point → further edits → preview/restore; failed restore rollback; session permission revocation; no document-content modifications.


### 3.11 Aurora Spatial & Semantic Selection (G5-D18)
- Support single selection, additive/toggle selection, rectangular marquee, freeform lasso and keyboard-accessible selection. Gestures must not steal interactions from an active application: selection mode and Shell chrome handle boundaries are explicit.
- Selected Canvas objects may be moved, aligned, grouped, locked and resized together where supported. Composite edits are atomic Canvas History transactions (G5-D17), previewable and undoable.
- Semantic selection can select by object type/category, manually defined group, explicit relationships, and a bounded graph neighborhood (e.g. directly connected nodes). Selection scope is visible before applying destructive or bulk actions.
- Keep **selection** distinct from **modification**: following a graph relation does not automatically relocate, modify or share linked items. Graph-derived selection respects link direction/type, permissions and hidden/private nodes.
- Manage large selections via viewport-aware highlights, virtualization, bounded graph traversal and cancellation; distant/offscreen matches are summarized and navigable via Aurora Navigator.
- Spatial transforms preserve relative positions; locked objects cannot be silently moved, and incompatible element types must be skipped with transparent feedback or cause an atomic operation to abort.
- Multi-user/multi-session and stale object-generation safety: validate ownership, lifetime and capabilities at command execution, not solely at selection time. Never select or reveal inaccessible nodes through relation metadata.
- Provide accessible selection counts, keyboard alternatives, clear-focus and clear-selection commands, reduced-motion behavior and touch/pen extensibility without requiring those hardware transports for G5.
- Acceptance: select via lasso/rectangle and via connected-node rule; preview selection count; batch align/move; undo a grouped edit; validate locks, stale objects, protected nodes and large-graph traversal bounds.


### 3.12 Hybrid Aurora Spatial Groups (G5-D19)
- A Spatial Group combines optional **visual region** (title, theme, bounds, positioning) with **semantic collection membership** (typed references to content, panels or other eligible groups). Logical membership need not imply physical proximity.
- The visual region can gather associated spatial items, while a semantically related item may remain elsewhere on the Canvas; the Shell must clearly distinguish membership from actual spatial containment and avoid involuntary movement.
- Support **collapse to a single representative group node** and **expand back to its member display**. Smart Semantic Zoom (G5-D06) may automatically select compact aggregated representations at distant scales, without changing membership or deleting child surfaces.
- Group operations: create, add/remove references, rename, style, collapse/expand, pin/lock, move contained items as an explicitly selected batch, align or reorganize with preview and user confirmation. Every mutable group operation participates in Canvas History (G5-D17).
- A content object may appear in multiple groups through references; no duplication of underlying files or application instances occurs. Sharing a reference is not equivalent to granting read/modify access.
- Relation Graph (G5-D09/D10) supports connections between individual members, between groups and their members, and between two groups. At low zoom, aggregated edges show that links exist without leaking protected titles or counts.
- Support explicit nesting of groups only with bounded depth and cycle prevention; cyclic *semantic links* can exist, but a group cannot recursively contain itself as a visual parent.
- When moving a group, distinguish **move visual frame only** from **move frame and visible contained items**, with accessible controls and a clear operation preview. Distant semantic members are not silently repositioned.
- Per-item capability, ownership and session checks still apply during bulk operations and group expansion. Removing a group must not delete referenced documents, application state or unrelated items.
- Very large groups use bounded graph walks, virtualized previews, incremental search and cancellation; inaccessible members are filtered before rendering or summarizing.
- Acceptance: create a visual group, add local and distant semantic references, collapse/expand under zoom, move with preview, edit graph link, undo actions, enforce permissions, and demonstrate that no source content was duplicated/deleted.


### 3.13 Aurora Spatial Focus — contextual mode (G5-D20)
- An explicit user action focuses an individual Canvas element, project or Spatial Group. The Shell smoothly centers/frames the relevant spatial region and chooses a readable camera scale while respecting accessibility reduced-motion settings.
- Highlight relevant Canvas nodes and authorized Relation Graph edges. Dim unrelated content without deleting, changing positions, suspending application logic or silently modifying group membership.
- Preserve a reversible **camera/focus context** (previous pan, zoom and active selection) so the user can exit immediately and return to the earlier overview. Focus state is presentation/navigation state, not a new security boundary.
- Focus must not disclose protected, off-session or otherwise inaccessible related objects through highlights, edge counts, tooltips or spatial search.
- Support clear exit/escape behavior, keyboard navigation, focus-within-context and visible indication of the active focus target. Explicit user navigation or Hub invocation must not trap the user inside focused mode.
- Unrelated interactive apps remain isolated. Input routing uses authoritative surface focus/capture rules; visually dimmed elements cannot accidentally intercept a click intended for the focused view.
- Semantic relevance can use explicit group membership and approved relation types; suggestions/inferred links are not silently treated as confirmed graph edges.
- Rendering must be bounded by visible spatial objects and edge budgets; provide graceful degradation for large graphs or when compositor effects are limited.
- Initial G5 acceptance: focus a group with near/distant semantic members; verify camera framing, relation highlighting, dimming, exit-to-previous-view, accessibility controls and security filtering. Verify no Canvas layout/history mutation from focus navigation alone.

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
- G5.5: Navigator, manual relation pins/links, basic suggested layouts, capability-mediated Smart Transfer, bounded live-session Canvas History, spatial/semantic selection, hybrid Spatial Groups and contextual Spatial Focus.
- G5.6: QEMU/CI end-to-end, security, performance and recovery tests.
- Rich Content Fabric, full native browser engine and advanced graph indexing may require later dedicated milestones, not blockers for a coherent first G5 release.

## 6. Quality and acceptance requirements
- Demonstrate multiple isolated Ring 3 clients with spatial panels, move/resize/focus and correct input routing.
- Demonstrate Hub enter/reduce/hide/restore without losing app state.
- Demonstrate Hub → Canvas → Hub for a live module, preserving application state and correct focus, including abort/failed-transfer rollback.
- Show navigation to offscreen content; semantic zoom behaves without flicker or stale input.
- User-approved relation creation and undo; rejected proposals have no effect.
- Demonstrate typed cross-module Smart Transfer with explicit user-selected open/copy/link operations, authorization checks and cancellation rollback.
- Verify Canvas History undo/redo and restore-point preview, conflict-safe restore, and isolation from application-internal document state.
- Verify marquee/lasso and graph-based selection with permission filtering, bounded traversal, atomic batch edits and history undo.
- Verify visual and semantic group membership, collapse/expand, nested-group cycle rejection, permission-scoped aggregations, and non-destructive group removal.
- Verify Spatial Focus camera framing, relevant-edge highlighting, exit/restore navigation, safe dimmed input routing and no layout mutation.
- No cross-client graphics/control privilege escalation, stale-generation use or cross-session content leak.
- Closing a process/session cleans all spatial objects and tokens safely.
- Failure injection: broken module/browser does not terminate system shell; safe graphics recovery remains available.
- Explicit runtime QEMU tests and CI evidence are required before marking any item implemented.

## 7. Pending design decisions
- Actual Hub module layout and lifecycle; responsive size-class thresholds and client notifications for G5-D15; gesture/accessibility details of G5-D13, bidirectional transfer interaction details and view cloning.
- Camera gesture details, shortcuts, zoom thresholds and 64-bit coordinate limits.
- Widget security model, spatial surface protocol and focus strategy.
- Content Fabric storage/index format and relation graph semantic schema.
- Visual theme tokens, accessibility and motion-reduction behavior.
- G5/G7 persistence boundary (especially Canvas History retention/checkpoint survival); browser engine milestone.
- Definition of the G5 minimal shippable acceptance gate.

## 8. Change control
Each approved decision adds an ID and a short design contract. Mark proposed features as proposals until accepted. Design approval never implies source implementation or runtime verification. Update this document incrementally; link implementation evidence to the graphics implementation roadmap rather than rewriting achieved status here.
