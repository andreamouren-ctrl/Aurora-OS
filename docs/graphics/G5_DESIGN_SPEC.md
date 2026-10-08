# Aurora OS — G5 Desktop Shell & Infinite Living Canvas
Status: **Design in progress — approved decisions D01–D37**
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
| G5-D21 | Aurora Universal Command: hybrid | Deterministic command palette and universal search, with optional natural-language AI interpretation, explicit previews and confirmations for consequential operations |
| G5-D22 | Aurora Privacy Layers | Granular privacy for Canvas items, groups and relationships, private areas, presentation mode and Identity-mediated authorization |
| G5-D23 | Aurora Visual Studio | Visual theme editor for Canvas, nodes, relation threads, Hub modules, typography, transparency, borders and aesthetic behaviors; reusable exportable presets |
| G5-D24 | Aurora Spatial Gestures | Pan, pointer-anchored zoom, adjustable inertial navigation, keyboard shortcuts, and progressive multi-touch gesture support; Canvas rotation is not included |
| G5-D25 | Aurora Canvas Portals | Spatial bookmarks and visible portal nodes jump to saved coordinates, authorized groups or projects without duplicating content; live remote-area previews excluded |
| G5-D26 | Aurora Spatial Layers | Named Canvas layers support independent visibility, edit locks, stacking order and item membership, with authorized interactions and no advanced per-layer effects in this decision |
| G5-D27 | Aurora Spatial Notes | Canvas-native rich-text notes, sticky notes, arrows, shapes, highlighters, freehand annotations and links to Canvas content, with AI enhancements optional and not included |
| G5-D28 | Aurora Spatial Clipboard | Clipboard history, multi-item spatial copy/cut/paste, preview and paste-as-copy/link, preserving relative placement without automatically duplicating underlying files |
| G5-D29 | Aurora Canvas Templates | Bundled and user-created reusable, exportable Canvas templates for groups, layers, notes, portals and spatial layouts, with private data excluded by default |
| G5-D30 | Modular Shell with isolated services | Lightweight Shell coordinator, explicit versioned interfaces and separated processes at security/failure boundaries; not one monolith or a process for every helper |
| G5-D31 | Selective process isolation | Trusted Shell modules may share its process; compositor/display, privileged brokers and untrusted applications execute across justified isolation boundaries with supervised restart and fresh capabilities |
| G5-D32 | Hybrid capability-scoped IPC | Use current bounded Ring 3 IPC for control messages and authorized shared memory/data handles for bulk payloads; no parallel bespoke kernel messaging system |
| G5-D33 | Aurora Adaptive Buffering | Per-surface bounded buffer pools and frame scheduling adapt to visibility, activity, memory pressure and compositor capacity, preserving atomic commits, capability isolation and release correctness |
| G5-D34 | Aurora Spatial Rendering hybrid | Spatial visibility indexing, region/damage-driven composition, bounded caches and software-compositor-first rendering; GPU acceleration is optional and later |
| G5-D35 | Hybrid Spatial Index Engine | Stable versioned spatial-index interface with swappable benchmark-selected dynamic R-tree/loose quadtree backends, and an authoritative correctness reference scan |
| G5-D36 | Aurora Hybrid Coordinates | Persistent deterministic signed 64-bit fixed-point world coordinates with double-precision camera math, camera-relative origin rebasing and checked screen conversion |
| G5-D37 | Aurora Hybrid Scene Graph | Hierarchical visual transforms and ownership-independent semantic graph; explicit contracts with Spatial Index, Content Graph and compositor, without live-app duplication |

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


### 3.14 Aurora Universal Command — hybrid (G5-D21)
- Provide a Shell-accessible command palette for universal search across authorized content, applications, Canvas nodes, groups and approved relations, plus a registry of deterministic typed commands.
- Support keyboard invocation, accessible pointer entry points and discoverable command descriptions, shortcuts, argument validation and contextual availability.
- Optional natural-language interpretation may translate a request into a **proposed structured plan** referencing registered commands. Core navigation, launching and searching must function entirely offline without AI.
- The AI interpreter is **not an authority**: it cannot invent capabilities, bypass user permissions, execute arbitrary Shell code or directly manipulate private application internals. All proposed actions pass the same deterministic validation and authorization path as manual commands.
- Distinguish read-only operations (such as search, inspect or navigate) from state-changing operations (move/group/link/transfer) and high-impact actions (delete, share, overwrite, security settings). Display operation targets, side effects and permission scope; require explicit confirmation before consequential actions.
- Provide a preview for multi-step commands (for example, locate a document, arrange it near a browser and reveal approved relations), with cancel and safe per-step failure behavior. Changes to Canvas structure produce compatible Canvas History journal entries where reversible.
- Search results, context windows and AI prompts must filter private or out-of-session objects before disclosure; sending data to cloud AI services requires explicit user configuration and informed consent. Prefer a local model when available without requiring it.
- Execution is bounded by time, steps and resources; disallow silent privilege escalation or insecure command composition. Maintain audit-friendly command outcome metadata without storing sensitive content in logs.
- Natural-language confidence failures offer clarification or deterministic alternatives, not speculative execution.
- Acceptance: offline deterministic search/launch; optional structured-language parsing; preview and confirm an authorized multi-step Canvas edit; reject unauthorized targets, fail-safe on a stale surface, undo supported changes, and verify no execution occurs on cancelled proposals.


### 3.15 Aurora Privacy Layers (G5-D22)
- Privacy is an **enforced authorization boundary**, not merely hiding pixels. Assign policy to individual content objects, spatial panels, Spatial Groups and Relation Graph edges; deny access by default where authority is missing.
- Integrate with Aurora Identity session ownership and fresh reauthentication for protected areas where required. Authentication and privilege elevation remain in the trusted Identity path, not in widget/client code; reject stale proof and revoked session tokens.
- Privacy-aware modes: normal working view, protected areas (requiring appropriate authorization), and **presentation mode** for screen sharing or demonstrations. Presentation mode must conceal private metadata, thumbnails, labels, graph edges, search suggestions, content previews, notifications and sensitive contextual command outputs; it is not a substitute for an access control check.
- Relation Graph must not disclose even the existence, title, count or shape of inaccessible nodes/edges through endpoints, highlights, aggregate clusters or zoomed-out previews. Apply authorization **before** relationship discovery, graph traversal, ranking, selection, rendering and summary.
- Hub, Navigator, Content Fabric, Canvas History, Smart Transfer, Universal Command and semantic selection all consume one coherent scoped privacy policy. Transfer and clipboard/export require independent destination-specific authorization; an authorized screen view does not automatically grant copying/sharing permissions.
- Revocation, screen lock, logout or session switch invalidates sensitive render/input resources, cached previews and scoped capabilities. Safe output must not expose old frames after privilege revocation.
- Define policy hierarchy and explicit inheritance rules for groups; moving an item into a group must **not** silently grant wider privileges or downgrade stronger per-item restrictions. Conflicts resolve to stricter effective access until the user explicitly approves a valid change.
- Ensure accessibility surfaces, error messages, diagnostics, crash reports, logs, AI contexts and offscreen caches cannot leak protected names/content. Cloud AI integration is off by default and separately consent-gated.
- Protected storage and cryptographic enforcement are delegated to their appropriate AuroraFS/Identity/security services; G5 owns privacy-aware presentation and capability integration, not a parallel ad-hoc credential store.
- Acceptance: protected item and relation absent from unauthorized searches, minimap, group counts, focus highlights, history previews and smart proposals; authorized reauthentication permits access; presentation mode obscures private UI; lock/revoke/logout remove pixels and input authority; no protected content exposed by stale-frame or cross-session tests.
- Scope note: UI contract is approved for G5, while full Identity and secure pre-session compositor migration remains coordinated with G6.


### 3.16 Aurora Visual Studio — theme customization (G5-D23)
- Provide a visual editor for composing and previewing themes across **Living Canvas, Aurora Hub, Spatial Groups, Relation Graph pins/threads and adaptive modules**. This is the theme-authoring feature named Aurora Visual Studio; it does not refer to Microsoft's Visual Studio development environment.
- Editable design tokens include background colors/images/gradients, surface colors, typography families/scale (subject to installed/licensed font availability), icon styling, node and edge palettes, edge width/style, borders, corner radii, transparency and optional motion/visual effects. Not all tokens must be exposed in the initial implementation.
- Changes appear in a **live, reversible preview** before Save/Apply. Cancel restores the active theme; applying a theme is atomic, with an accessible fallback/default and recovery from invalid presets.
- Ship a coherent Aurora default theme. Allow user-owned named presets, duplication, import/export in a **versioned, validated, portable declarative format**. Never execute code or accept scripts through theme bundles.
- Theme data must not change object ownership, privacy/authorization, focus routing, application privileges, secure chrome or visibility of safety-critical system prompts. Protected or secure surfaces use controlled high-contrast/legibility rules regardless of user theme.
- Enforce minimum contrast/readability, scalable UI, reduced-motion preferences and meaningful non-color indicators for relation semantics. Alpha effects and animations degrade gracefully under the software compositor.
- Theme evaluation must have bounded resource costs (e.g. image size, shader/effect availability, animation complexity), and previews must not block ordinary app rendering.
- Keep themes separate from **Canvas layout/history** and from persistent authenticated-user profile ownership; user theme settings may be saved in G5, while cross-reboot full workspace reconstruction aligns with G7.
- Define stable token inheritance: global theme -> Hub/Canvas component defaults -> per-group/per-object visual override, with privacy/security overrides having the final say.
- Acceptance: edit Canvas background and relation-thread style, preview and cancel, apply, export/reimport valid preset, reject malformed/oversized/untrusted preset, verify accessibility defaults and safe recovery on Shell restart.
- Automatic activity/context-driven theme switching belongs to a distinct future decision; it is **not** approved by G5-D23.


### 3.17 Aurora Spatial Gestures (G5-D24)
- Provide consistent Canvas **pan and zoom** with pointer-anchored zoom (the world point under the pointer remains stationary during zoom), bounded zoom limits and accessible keyboard equivalents.
- Allow configurable pan gesture(s) that do not conflict with application input, selection/lasso, object dragging or relationship-link dragging. Gesture arbitration is an explicit Shell input-routing state machine with cancellation and pointer capture revocation on target destruction, lock or session switch.
- Support adjustable kinetic/inertial panning with friction, velocity/acceleration bounds and reliable stop on new input. Inertia is disabled under reduced-motion accessibility preferences and can be disabled by the user.
- Prefer pixel/line normalized scroll inputs and device-independent pointer events from G4. Mouse and keyboard are the required G5 baseline; precision touchpad, touchscreen and multitouch are progressive capabilities gated by actual HID/input transport support and hardware/runtime verification.
- Zoom has defined minimum/maximum scales, stable camera transform math, no significant accumulated coordinate drift over distant Canvas areas, and clamped animation work to preserve responsive application input.
- Semantic Zoom (D06), Navigator (D07), Spatial Focus (D20), Canvas History (D17) and Smart Selection (D18) must interoperate: camera motion alone does not alter structural Canvas history; focus/overview can restore the earlier camera; selection gestures have priority only while expressly active.
- Expose discoverable gesture help, remappable non-conflicting shortcuts, and predictable zoom reset/fit-selection/fit-group actions. Ensure screen reader and keyboard-only workflows can navigate without gestures.
- Test pan, pointer-centered zoom, inertia stop/cancel, rapid opposing wheel events, input capture lost during device removal, secure scene activation, session teardown and reduced-motion behavior. Test precision on large world-coordinate offsets and high zoom factors.
- Rotating the entire Canvas and advanced immersive spatial controls were not approved in D24; they require a later decision.


### 3.18 Aurora Canvas Portals (G5-D25)
- Users may create named spatial bookmarks and place **portal nodes** anywhere on Living Canvas. Activating one navigates the camera to a saved coordinate/zoom or to the current, authorized spatial bounds of a designated group/project.
- Portals are **navigation references**, not copies, mounts, file containers, remote-desktop streams or new capabilities. Linking to an object never grants access to that object's content.
- A target descriptor has a typed kind (camera bookmark, group, project), stable ID where applicable, target session/user scope, optional fallback camera coordinates, display label and creation/update metadata. Resolve references and permissions at activation time; never trust stale coordinates or a reused object ID.
- Show destination label and an optional static, privacy-filtered icon/summary; live rendering of a remote Canvas area is **not approved** by D25 and requires a separate design/performance/security review.
- Navigation can animate according to Spatial Gestures (D24) with reduced-motion fallback to an instant jump. Keep a reliable **Back to previous view** action; portal activation does not modify Layout/Canvas History structural entries.
- A portal can be associated with a Spatial Group (D19), appear in Navigator (D07) and be searchable from Universal Command (D21). Portal relationships are distinguishable from ordinary semantic graph edges (D09/D10).
- A moved/deleted group, unavailable project, invalid reference or insufficient authorization produces a safe unavailable-target state, with no leaked title/preview and no unauthorized traversal. User can repair or remove a broken portal.
- Portal visuals and labels may be themed through Aurora Visual Studio (D23), but secure status and privacy masking cannot be overridden by theme.
- Enforce no accidental jump while dragging/selecting; activation requires explicit click/keyboard action, not pointer hover.
- Acceptance: create/edit/delete bookmark and portal, jump to distant coordinate and group, navigate back, confirm privacy filtering, reject stale/deleted or cross-session targets, and ensure no item duplication or session-state reset.


### 3.19 Aurora Spatial Layers (G5-D26)
- Users may create, name, reorder, hide/show and lock/unlock **Canvas layers**, assigning eligible spatial items to a layer. A safe default layer exists; item ownership and application execution remain independent of layer membership.
- Layer order determines eligible Canvas item drawing/stacking policy, integrated with Shell-managed z-order; transient secure system overlays and trusted UI are **never** subordinated to arbitrary user layer ordering.
- A hidden layer removes its visual objects and related hit-test/input targets from ordinary interaction; hidden does **not** terminate app processes or delete content. Revocation, lock/logout and other higher-priority privacy decisions still apply.
- A locked layer prevents direct user edits to layout, membership or object transforms through the Canvas; it is **not** an access-control boundary. Authorization remains enforced through Aurora Identity and Privacy Layers (D22).
- Objects on a layer may belong to multiple semantic groups (D19), but each displayed spatial instance must have well-defined primary layer placement; semantic membership does not replicate a live application surface.
- Relation Graph (D09/D10) can display cross-layer links when permitted and visible under the active layer filter; hiding a layer must not leak private endpoints via stray edges or aggregate counts. Focus (D20) and Semantic Selection (D18) must respect visibility and locked edit states.
- Moving between layers, renaming, hide/show, locking and reordering are user-visible reversible structural changes recorded by Canvas History (D17), with atomic validation and stale-generation checks.
- Navigator (D07), Universal Command (D21), Portals (D25) and Smart Organization (D08) may offer layer filters and target-navigation hints, but may not silently reveal hidden/private content or override a user's intentional hidden/locked layer.
- Layer customization inherits Visual Studio (D23) theme tokens; **independent opacity, per-layer post-processing filters and complex blend modes are not approved by D26** and would require a separate compositor cost/security design.
- Accessibility: keyboard layer chooser, clear visible/hidden/locked states, accessible layer labels, and non-color-only distinctions.
- Acceptance: create/rename/reorder a layer; move an item between layers; hide/show and validate compositor visibility plus hit testing; lock and reject edits; undo/redo; enforce secure overlay priority; test protected cross-layer relations and process lifetime preservation.


### 3.20 Aurora Spatial Notes (G5-D27)
- Provide **Canvas-native notes** as independent typed spatial objects: sticky notes, rich-text note cards, labels, directional arrows, geometric shapes, highlighter marks, and freehand strokes. All can be moved/resized as appropriate and inherit spatial zoom/visibility rules.
- Rich-text notes support a safe bounded subset: paragraphs, emphasis, headings, lists and links to authorized content. No arbitrary HTML/script execution or implicit remote resource fetch. Document editing inside unrelated applications remains app-owned.
- Notes and drawing objects have stable IDs, explicit layer membership (D26), session ownership, bounds, transforms, accessible labels, and optional attachments to Spatial Groups (D19) or Relation Graph objects (D09/D10).
- Distinguish **annotations** (visual marks) from **semantic graph relations** (typed edges). An arrow is not automatically a verified relationship; users may explicitly convert/link it using approved relation workflows.
- Drawing tools: pen/freehand path, highlighter, basic shape/arrow creation, select/edit/move/resize, and erase. Stylus pressure and advanced pen hardware are progressive input extensions; initial G5 baseline must work with mouse and keyboard where meaningful.
- Respect semantic zoom (D06): detailed editing only at readable scales, compact/summarized representations when distant. Dimming, grouping, focus and layer visibility must preserve input correctness and not destroy unsaved notes.
- Integrate Canvas History (D17) with reversible create/edit/move/delete commands, appropriate edit batching (stroke-level and text-edit transactions), named restore points and conflict-safe recovery. Notes are protected user data: autosave and crash recovery must follow a durable storage design; full workspace reconstruction across reboot coordinates with G7.
- Notes can be transferred or linked through Smart Transfer (D16), searched via Universal Command (D21), selected semantically (D18) and themed through Visual Studio (D23), subject to Aurora Privacy Layers (D22). Never leak protected note text through previews, graph hints, clipboard/export, search or AI context.
- Rendering and data model limits: bound stroke vertex counts, object sizes and text length; clip offscreen content, simplify distant strokes, and avoid redraw of unchanged regions. Geometry operations and rich-text parsing must reject malformed or excessive input safely.
- Accessibility: keyboard note creation, text input, shape selection, alternative labels for drawings, sufficient contrast, non-color-only semantics and reduced-motion-compatible interaction.
- Initial G5 tests: create/edit rich-text note, sticky, arrow, shape, highlight and freehand stroke; link a note to a document without copying it; group/layer/focus/zoom interactions; undo/redo, save/reload (when persistence available), stale-reference denial, protected-note privacy and crash/failure recovery.
- Automatic AI classification/summarization/suggestions are *not* approved as part of G5-D27; they would require a separate later decision.


### 3.21 Aurora Spatial Clipboard (G5-D28)
- Provide keyboard-accessible **copy, cut and paste** across authorized Hub modules and Canvas items, together with a user-invoked clipboard history panel showing privacy-filtered previews and typed transfer options.
- A selection of multiple spatial objects is serialized into a versioned, bounded **spatial transfer manifest** containing object types, stable identifiers or allowed snapshots, relative positions, group/layer membership mappings and eligible intra-selection graph edges. Preserve positions relative to a common anchor when pasted, with collision-aware placement preview.
- Paste choices include **copy** (new eligible objects with new IDs; clone only data formats the source explicitly permits) and **link** (authorized logical references using Content Fabric/Relation Graph; no physical file duplication). Regular paste offers a safe documented default. Cut must not delete the source until the target operation commits successfully; failed paste/cancel retains the original.
- Distinguish cloning a Canvas visual object from copying a physical file, a live application process, private content or credentials. Live app sessions/capabilities are never duplicated through clipboard serialization. Non-clonable objects yield reference/shortcut or unsupported-option feedback, not unsafe approximation.
- Clipboard history is user/session scoped, bounded by item count, byte size and retention. Sensitive content is opt-in or excluded by policy; lock/logout/revocation wipe sensitive cached previews and invalidate ephemeral references. Protected/private objects and graph edges cannot leak through item counts, labels or search.
- Interoperate with Smart Transfer (D16) using typed formats, permission checks and a single trusted broker, while treating clipboard payloads as untrusted input (parse safely, size limits, type validation, no arbitrary code execution).
- Clipboard paste of group edits, visual notes and relation links must be atomic where feasible, report partial incompatibility before approval and enter Canvas History (D17) as one reversible transaction.
- Shortcuts, screen-reader-readable operations, explicit history removal, and predictable paste placement are required. Clipboard synchronization with other devices and AI-based paste suggestions are not approved in D28.
- Acceptance: copy/paste multi-object selection with preserved geometry, paste-as-link with no file clone, cancel/failed cut rollback, clipboard history previews, authorization isolation, stale-generation rejection and sensitive-history cleanup.


### 3.22 Aurora Canvas Templates (G5-D29)
- Provide bundled starting templates and user-authored reusable layouts for the Infinite Living Canvas, including selected spatial regions, group structures, layers, visual notes/shape placeholders, portal definitions and layout arrangements.
- Templates are **blueprints**, not copies of a user's live session. Default exports contain declarative structure and safe presentation tokens only; no embedded credentials, session tokens, app process state, document bytes, private previews or unrestricted filesystem paths.
- Support create-from-selection, preview, save-as-template, instantiate, rename, duplicate, delete and portable import/export through a versioned validated schema. Users may opt into explicitly supported non-sensitive assets with warnings and permissions checks; private references remain excluded unless a separate authorized export workflow is defined.
- Instantiation creates fresh Canvas object IDs and rebinds internal references among the new objects; unresolved external app/content/portal targets become clearly marked placeholders requiring deliberate user binding. Never create active application sessions or grant capabilities merely by importing a template.
- Template contents may specify default Group (D19), Layer (D26), Note (D27), Portal (D25), Relation Graph (D09/D10) and Visual Studio (D23) styling structures. Rendering adapts to target workspace dimensions, scale and user theme without overwriting stronger accessibility/privacy choices.
- A template application is a previewable, user-approved atomic Canvas History (D17) transaction; cancel or failure has no partial side effects. Applying a template must not overwrite existing Canvas content without explicit confirmation.
- A template is a user-controlled reusable asset, distinct from the persistent Activity Space reconstruction model planned for G7. Import/export and template inventory must be protected by Aurora Identity and Privacy Layers (D22).
- Resource policy: bounds on number of objects, nested group depth, graphs, embedded asset size and parse complexity; no executable scripts, arbitrary resource fetches or privileged settings in template bundles.
- Acceptance: create a template from a mixed selection (groups/layers/notes/portals), export/import it, preview/instantiate with fresh IDs, resolve placeholders, preserve relative positioning, undo application, reject malicious/oversized bundles and verify no protected content leaked.
- Dynamic AI-driven template generation/adaptation is not approved as part of G5-D29.


### 3.23 Modular Shell with isolated services (G5-D30)
**Approved architecture:** Desktop Shell is a relatively lightweight, trusted session coordinator. Internals use modular APIs; components cross a process boundary when justified by security, crash containment, privilege separation or independent lifetime. Not every module requires its own process.

Proposed logical boundaries (contracts drafted below; exact process topology not yet approved):
- **Shell Coordinator**: authority for UI policy, authorized placements, Hub/Canvas orchestration and session coordination. Never a host for untrusted page scripts or untrusted plugins.
- **Canvas Engine**: camera transform, spatial scene bookkeeping, visibility, layers and semantic-scale presentation policies.
- **Graph/Content facade**: authorized relationships, group references and portal target lookup, using Content Fabric services as appropriate.
- **Interaction**: normalized input gestures, selection state, mediated Smart Transfer and Clipboard interactions.
- **State**: structural transaction journal, previews, undo/redo, templates and crash-safe serialization.
- **Presentation/Compositor**: privileged surface compositing, buffer/focus/input ownership, scene submission and presentation timings.
- **Apps and optional modules**: separately isolated Ring 3 clients, including browser engine; no untrusted application owns Shell policy.

In-process boundaries may use direct versioned APIs, while out-of-process boundaries use capability-scoped IPC. Never move compositor ownership/authorization merely to avoid IPC overhead. A service crash must revoke its stale handles, preserve isolation, and permit controlled Shell-managed recovery or a safe fallback.

#### Draft technical interface contracts (proposed, not yet frozen)
| Interface | Producer -> consumer | Minimal operations/data | Authority and failure invariants |
|---|---|---|---|
| `G5.SessionContext.v1` | Identity/session supervisor -> Shell/graphics modules | authenticated session ID, capabilities, generation, revoke event | no cross-session reuse; reauth only via Identity |
| `G5.SpatialScene.v1` | Shell/Canvas -> Compositor | create/bind/remove scene node, logical world bounds, transforms, layer/order, visible region, configure/ack serial | Shell alone proposes placement; compositor validates owner/rights; stale generations rejected |
| `G5.Camera.v1` | Navigator/Interaction/Focus -> Canvas | pan, pointer-anchored zoom, fit/select, portal jump, current camera revision | bounded/finite math, cancelable transitions, no unauthorized focus |
| `G5.ModuleHost.v1` | Hub/Canvas -> module client | attach/detach representation, content rectangle, layout size-class, semantic level, visibility, requested focus | transfer preserves client identity/state; transactional rollback; no privilege transfer |
| `G5.GraphQuery.v1` | Canvas/Hub -> graph/content broker | authorized node/edge lookup, neighbor query with traversal limits, typed link proposals | privacy filtering before results/counts; no raw cross-client access |
| `G5.Interaction.v1` | normalized input -> Shell policy/Canvas | pointer/key/gesture stream, selection/lasso, drag sessions, target routing | capture revocable, trusted focus routing, timeout/teardown safe |
| `G5.Transfer.v1` | source/target apps -> transfer broker | MIME-like typed offers, available operations, single-target consent, commit/abort | temporary scoped capabilities; no partial cut; source intact on failure |
| `G5.State.v1` | Shell modules -> state manager | typed transaction, precondition check, undo/redo, checkpoint, preview | atomic commands, generation checks, no secret payloads in journal |
| `G5.Theme.v1` | Visual Studio -> theme resolver | versioned declarative tokens, validate, preview, apply, revert | no executable theme code; accessibility/security overrides |
| `G5.Command.v1` | Hub/command palette -> command registry | discover, validate, preview, authorize, execute, cancel | optional AI emits proposals only; deterministic executor is authoritative |

#### Common envelope (proposed)
Every cross-process request should carry a **version**, **request ID**, **session generation**, **caller capability handle**, **operation**, **bounded typed payload**, and optional **deadline/cancellation token**. Responses should carry a typed status, resulting object generation/revision where applicable and resource-release obligations.

#### Non-negotiable invariants
1. Authentication, ownership and compositor surface capabilities are validated at use, not only at request creation.
2. Never pass raw kernel pointers, implicit global object IDs or live application capabilities through visual templates, clipboard history or graph references.
3. Shell/window protocol must extend the currently verified toplevel configure/ACK serial and activation rules rather than fork a competing unvalidated protocol.
4. Process or service death revokes generation-bound handles, cancels queued work and does not expose stale pixels/input.
5. Graphics/gesture rendering can degrade gracefully; the desktop must stay usable without optional AI, animated effects or browser execution.
6. Resource envelopes, IPC queue depth, object/edge counts, timeouts and memory limits require measurable budgets before G5 completion.

#### Next architecture reviews
- Identify the actual Ring 3 process topology from current kernel/service code.
- Determine protocol framing/transport, capability token encoding, handle table ownership and exact state machines.
- Set latency/memory budgets and queue backpressure/restart policy.
- Write API header/schema files **only after** contract review and integration with existing Aurora interfaces.


### 3.24 Selective process isolation — concrete topology & recovery (G5-D31)

**Decision approved:** selective isolation. The roles/process placements below are an initial target topology for design review, not a claim that this exact process topology is running today.

#### Target topology
```text
Session Manager / trusted supervision [existing foundational services]
  | grants fresh session generation and explicit capabilities
  +-- Desktop Shell Ring 3 process (trusted session policy)
  |    +-- Shell Coordinator / Hub Controller
  |    +-- Canvas Engine + Camera + Semantic Zoom
  |    +-- Navigator + Interaction & Selection state
  |    +-- Graph view/cache (untrusted data parsed through broker)
  |    +-- Theme resolver + UI (validated declarative tokens)
  |    +-- State/History frontend + command registry
  |
  +-- Aurora Compositor service [separate privileged process TARGET]
  |    +-- authoritative surface ownership, composition, input focus/capture
  |    +-- Display backend may be initially co-located with compositor
  |
  +-- Identity / Session security services [separate trusted authority]
  +-- Content/Graph broker [separate if privileged indexing/storage is used]
  +-- State persistence broker [separate if durable storage privilege is needed]
  +-- Transfer/Clipboard broker [separate if inter-client data access demands it]
  +-- Application processes [untrusted Ring 3; isolated by process/capability]
       +-- browser engine/renderers [additional sandboxing as support evolves]
       +-- editor/media/third-party clients
Recovery framebuffer login remains independent of the normal compositor.
```

**Process placement principle:** logical modules may remain in the Shell process when they do not process unsafe executable payloads or need independent privileged access. A facade/IPC client in Shell is not permission to put the corresponding privileged broker in Shell. Broker deployment is conditional and requires a dependency/privilege assessment; do not prematurely create a separate service for every feature.

#### Reuse of existing runtime foundations
- `AURORA_SYS_IPC_SEND=4`, `RECEIVE=5`, `WAIT=8` already support capability-checked Ring 3 messages. Current IPC payload **maximum 256 bytes**; **maximum four transferred capability handles**. G5 messages must fit or use an explicitly authorized shared-memory/data broker path after its Ring 3 ABI is implemented and verified.
- IPC endpoint `WRITE` is required to send, `READ` to receive/wait. Current wait supports one waiter per endpoint; G5 designs may multiplex a single event loop and must not assume arbitrary multi-consumer waiting. Queue-full is an error; retry/backpressure is a client policy still requiring measured design.
- Existing service supervision supplies `NEVER`, `ON_FAILURE`, `ALWAYS` and bounded restart attempts, fresh process IDs/capability tables per generation and full reap before restart. Time-based backoff/health-registry/general service discovery are **not yet implemented**; G5 must add them or operate without claiming production-grade recovery.
- Existing Shell contract grants session-scoped `MANAGE_WINDOWS` only as required, never implicit filesystem, credential or display capture authority. Existing window protocol configure/ACK, activation token and generation rules remain authoritative.
- Exact process allocation and compositor migration require implementation work; architecture documentation explicitly allows early Display/Compositor co-location while keeping policy separate.

#### G5 IPC contract mapping (proposal, pending ABI review)
| Link | Channel/payload | Capability model | Failure rule |
|---|---|---|---|
| Session Manager → Shell | session-ready/revoke/generation | minted session-bound Shell policy rights | revoke before reuse; rebuild Shell on restart |
| Shell → Compositor | scene operations, focus/activation policy, configure/ack references | scoped `MANAGE_WINDOWS` and verified surface generation | reject stale/foreign surface, fail closed |
| App ↔ Compositor | typed surface attach/commit/configure and callback notifications | owned surface/buffer handles; no window-admin rights | app death detaches surfaces, revokes input |
| Hub/Canvas → Module App | layout size class, semantic zoom level, host attach/detach | one module instance, scoped endpoint & authorized surface | transfer rollback on timeout/crash |
| Shell ↔ Content/Graph broker | paginated IDs/relations/query tokens | least-privilege scoped query/metadata rights | unavailable broker means safe empty/error state |
| Shell ↔ State broker | typed journal operations, checkpoint/restore | user/session-scoped state authority | never claim commit before durable ack |
| Shell ↔ Transfer broker | typed offer/accept/commit/abort | time-limited source and destination handles | no premature delete on cut; revoke on abort |
| Command UI → deterministic registry | parse/preview/authorize/execute | no implicit execution from optional AI | reject unsupported or unauthorized operations |

Wire frames should include version, message type, request ID, session generation, target handle/generation, flags and bounded payload length. Capability handles are resolved through kernel tables, not raw pointers. Exact binary layout, endian policy and operation IDs remain to be frozen.

#### Crash/failure behavior matrix
| Failure | Required G5 behavior |
|---|---|
| One application crashes | close/invalidate only its surface and capture; Shell/other clients continue |
| Browser renderer crashes | isolate failure to renderer/module, show recoverable panel; never crash Shell |
| Shell crashes | compositor removes policy/focus authority, suppresses stale input, supervisor reconstructs fresh session-scoped Shell; recover UI from trusted durable state where available |
| Compositor crashes | revoke presentation/input handles and show safe recovery framebuffer path; rebuild compositor and graph bindings with fresh generations |
| Content/Graph broker crashes | no stale/private suggestions or graphs; mark unavailable and retry only after verified service readiness |
| State broker crashes | preserve confirmed state, refuse unacknowledged commits, journal conflicts are surfaced, no fabricated persistence success |
| Transfer broker crashes | abort pending copy/cut/link transactions and leave original source intact |
| Identity/session revoked | immediately invalidate all scoped Shell/client rights, cached private previews and display/input authority |
| Restart loops | bounded attempts; no uncontrolled spawning; safe fallback and diagnostics |

#### Implementation gates
1. Enumerate existing process/bootstrap, IPC and graphics capabilities; map smallest deployment topology.
2. Define typed `G5.IPC.v1` envelopes inside 256-byte cap, with explicit serialization and parser fuzz tests.
3. Define service ownership, generation revocation, lock ordering, queue-full/timeouts, event loop and capability handoff.
4. Separate Shell policy from compositor authority and verify multiple isolated Ring 3 window clients.
5. Inject crash/termination at every boundary and assert cleanup, no cross-session leakage and bounded recovery.
6. Require QEMU runtime markers/CI regression results before marking any topology or crash behavior **implemented**.

#### Explicit non-goals of D31
- No forced service-per-widget microservice architecture.
- No promises of implemented browser multi-process sandbox, production compositor restart or generic service discovery before their runtime gates.
- No shared-memory payload ABI assumed to exist merely because internal shared memory objects exist.


### 3.25 G5 Hybrid capability-scoped IPC — draft wire ABI (G5-D32)

**Approved decision:** reuse native capability-aware IPC for control and event delivery, while bulk/graphics data flows use individually authorized shared-memory objects or data-broker handles. **Design-only:** no claim that a public Ring 3 shared-memory mapping ABI exists today. The implementation is gated on real runtime verification.

#### Existing kernel constraints (source checked 2026-10-08)
- \`kernel/include/aurora/ipc.h\`: \`AURORA_IPC_PAYLOAD_MAX=256\`, \`AURORA_IPC_CAPS_MAX=4\`, \`AURORA_IPC_QUEUE_DEPTH=16\`; dual-ended channel and escrowed capability delegation.
- \`kernel/include/aurora/capability_abi.h\`: \`aurora_cap_handle\` is \`uint64_t\`; existing cap types include MEMORY, GRAPHICS_BUFFER and SURFACE; rights include READ, WRITE, MAP, CONTROL, TRANSFER. No G5-specific elevated cap type is assumed to exist.
- \`kernel/include/aurora/capability.h\`: generation-checked capability table, lookup/retain/release, revoke, rights reduction and delegation; 256 entries per table today.
- \`docs/RING3_IPC_SYSCALLS.md\`: SEND=4, RECEIVE=5, WAIT=8; one waiter per endpoint; queue-full send currently returns error. Received transfers are issued new receiver-local handles; never serialize sender-local handle numbers as authoritative.
- Privileged service discovery, bulk payload mapping, general service-registry and production backpressure require additional implementation.

#### Proposed header file structure (not created in source yet)
\`\`\`text
kernel/include/aurora/g5_ipc_abi.h       # fixed-width wire framing and opcodes only
kernel/include/aurora/g5_shared_abi.h    # descriptor for future mapped bulk objects
services/shell/include/g5_client.h       # userspace encode/decode and request lifecycle
services/shell/include/g5_protocol.h     # module-specific typed requests and replies
tests/g5_ipc_contract_tests.c            # serializer/parser/permission regression
\`\`\`
Paths are **proposals**; verify repo layout and ownership before implementation.

#### Proposed fixed 48-byte control frame
All fields are **explicit little-endian on wire**, no host C struct memory memcpy. G5 common frame bytes:
| Offset | Bytes | Field | Rule |
|---|---:|---|---|
| 0 | 4 | magic | ASCII \`G5IP\` |
| 4 | 2 | major | 1 |
| 6 | 2 | minor | 0 initially |
| 8 | 2 | header_bytes | 48 |
| 10 | 2 | message_type | request/response/event/cancel |
| 12 | 4 | operation | namespaced opcode |
| 16 | 4 | flags | reject unknown mandatory flags |
| 20 | 4 | payload_bytes | 0..208 |
| 24 | 8 | request_id | caller-unique in endpoint generation |
| 32 | 8 | session_generation | validated against trusted session context |
| 40 | 8 | object_generation | expected target revision/generation or 0 |
Total **48 bytes** + maximum **208-byte inline payload** = **256 bytes**. Target object authority is passed in a validated capability slot or resolved through a scoped broker; IDs in payload are not authority. Header has no raw pointers.

Proposed C-facing *logical* definitions:
\`\`\`c
#define G5_IPC_MAGIC 0x50493547u /* LE bytes: 'G','5','I','P' */
#define G5_IPC_MAJOR 1u
#define G5_IPC_HEADER_BYTES 48u
#define G5_IPC_INLINE_MAX (AURORA_IPC_PAYLOAD_MAX - G5_IPC_HEADER_BYTES)
#define G5_IPC_ATTACH_MAX AURORA_IPC_CAPS_MAX

enum g5_ipc_kind { G5_IPC_REQUEST=1, G5_IPC_RESPONSE=2,
                   G5_IPC_EVENT=3, G5_IPC_CANCEL=4 };
struct g5_ipc_frame_logical {
    uint16_t major, minor, header_bytes, kind;
    uint32_t operation, flags, payload_bytes;
    uint64_t request_id, session_generation, object_generation;
};
/* Encode/decode field-by-field; this is NOT a wire-castable packed struct. */
\`\`\`
**Review note:** Current field-count/offset layout above totals 48; any extension uses a new header size/version and strict bounds. Use explicit operation schemas with numeric bounds, length prefixes and type validation. Do not attempt fragmentation of large arbitrary payloads as a substitute for a validated bulk path.

#### Bulk descriptor: design candidate (requires ABI implementation)
A control message may identify a transfer of type \`G5_BULK_REF\` and carry an **IPC-transferred capability**, offset, length, read/write role, content type, object generation and expiration/lease policy. Kernel checks mapping rights and owner/session; receiver maps only its authorized range.
- For content/metadata: prefer immutable/read-only sealed snapshot after producer commit; for mutable buffer streams: an explicit writer/reader lifecycle with release and fence semantics.
- Validated: offset+length overflow, page alignment where required, per-client byte quotas, stale generation, MAP/READ/WRITE rights, ownership, mapping teardown, zeroing on reuse, and sender death during transfer.
- GRAPHICS_BUFFER ownership must preserve compositor-specific buffer attach/commit/fence rules; **G5_IPC** transports control, not necessarily pixel bytes.
- Large payload exchange is **blocked** until a verified Ring 3 create/map/unmap/revoke shared object or a vetted existing broker IPC path is available. The mere existence of internal shared-memory objects does not satisfy this acceptance gate.

#### Capability distribution and authority
- Only Session Manager/trusted supervisor bootstraps service endpoints; untrusted apps never acquire \`MANAGE_WINDOWS\` by messaging a privileged endpoint.
- SEND needs endpoint WRITE; RECEIVE/WAIT need READ; capability transfer needs source TRANSFER and delegated rights are an intersection of source rights and authorized policy, never an escalation.
- Limit to **four** transfers per IPC message. Transferred capability handles are receiver-local and must be interpreted by ordinal slot + validated expected type/rights; never trust the sender's numeric cap handle embedded in bytes.
- Every operation validates session generation, target object generation, caller endpoint identity/rights and per-operation allowed types. Reauthentication/identity secrets remain with protected Identity services.
- Session lock/logout or process death invalidates leases, mapped views, queued transfers, broker references and pending requests. Protect against escrow leaks when send/receive/cancel/restart fails.

#### Request/reply, cancellation, queues and errors
- Requests use unique monotonically advancing request IDs per endpoint generation; responses correlate request ID, operation and session generation. Replays/stale replies rejected.
- A CANCEL is best-effort, idempotent and scoped to the originating caller/request. Never roll back a committed side effect merely because cancellation arrives late; signal a terminal committed result.
- Use \`IPC_WAIT\` + \`IPC_RECEIVE\` event loop with **one waiter per endpoint**; replies and events are multiplexed. Queue depth is 16; producer must coalesce replaceable camera/motion updates but **never** coalesce irreversible commits, permission decisions or keyboard/button transitions.
- Queue full produces explicit backpressure and bounded retry/cancel. Avoid busy-spin; do not hold compositor or graph locks while waiting on RPC/IPC.
- Logical statuses: OK, INVALID_FORMAT, UNSUPPORTED_VERSION, UNKNOWN_OPERATION, PERMISSION_DENIED, STALE_HANDLE, WRONG_SESSION, BUSY, QUEUE_FULL, TOO_LARGE, TIMEOUT, CANCELLED, CONFLICT, INTERNAL_ERROR; map to existing kernel error transport without claiming distinct syscall errno support today.
- No secrets/credentials in logs; ensure request IDs and resource generations are sufficient to diagnose failure without exposing user data.

#### Tests and acceptance gates
| Test ID | Required proof |
|---|---|
| G5-IPC-01 | fixed 48-byte framing, little-endian encode/decode and 208-byte inline boundary |
| G5-IPC-02 | reject bad magic/version/header size, unknown required flags/opcodes, truncated and oversized messages |
| G5-IPC-03 | strict capability type/rights reduction and at-most-four transferred handles |
| G5-IPC-04 | stale handle/object/session generation, replayed request ID and spoofed caller rejection |
| G5-IPC-05 | full 16-slot queue behavior, bounded backpressure/coalescing and no dropped critical updates |
| G5-IPC-06 | cancellation races before/after commit; one waiter and lost-wakeup paths |
| G5-IPC-07 | bulk offset/length overflow, quotas, readonly mapping, teardown/revocation, reuse scrubbing |
| G5-IPC-08 | isolate crash of source/destination during mapping and capability escrow; no leaks |
| G5-IPC-09 | separate isolated Ring 3 clients exercise Shell ↔ compositor and Shell ↔ module control flows |
| G5-IPC-10 | repeated login/logout, restart and injected crashes show no cross-session data/graphics/input leaks |
| G5-IPC-11 | QEMU runtime plus CI smoke and negative/malformed-message tests; no unverified production claims |

**Blocking dependencies:** versioned user-space ABI headers and libraries, Ring 3 shared-memory create/map/unmap/revoke or approved broker path, scoped service endpoint discovery, queue overload policy and actual compositor Shell IPC migration. These are required before G5-D32 can be marked *implemented/runtime verified*.


### 3.26 Aurora Adaptive Buffering — graphics protocol and performance (G5-D33)

**Approved choice:** adaptive bounded buffer pools, not permanently fixed double/triple buffering and not unlimited dynamic allocation. Runtime tuning is a policy layered on existing surface/buffer commit contracts, not a different graphics transport.

#### Verified source-level foundations (inspected 2026-10-08)
- \`kernel/include/aurora/graphics_buffer.h\`: \`AURORA_GRAPHICS_BUFFER_MAX_OBJECTS=64\`, \`MAX_DIMENSION=8192\`, \`MAX_BYTES=64 MiB\`; buffer state FREE/READY/COMMITTED/IN_USE/RELEASED and generation/ref-counted capability-backed ownership.
- \`kernel/include/aurora/graphics_surface.h\`: \`MAX_OBJECTS=128\`, \`MAX_DAMAGE_RECTS=16\`, \`MAX_FRAME_CALLBACKS=8\`; pending/committed snapshot, commit serial and presentation callback fields.
- \`docs/graphics/SURFACE_BUFFER_PROTOCOL.md\`: attached pending/committed surface state, validated atomic commit, damage, explicit release and bounded backpressure.
- \`docs/graphics/COMPOSITOR_CONTRACT.md\`: compositor owns presentation scheduling, frame callbacks, release, clipping, damage and scene visibility; software rendering baseline, GPU optional.
- These are **existing definitions/contracts**, not evidence that the G5 adaptive policy has been implemented or performance-tested.

#### Pool and scheduling policy
- Each authorized application surface uses a **logical adaptive buffer pool** with policy-selected target depth: 1 retained completed frame for quiescent/non-updating scenes (if safe), generally **2** for interactive operation, and **up to 3** only when frame pacing benefits and memory permits. Values are policy targets, not promises of immediately released objects.
- New allocations require compositor/buffer-manager admission under global, per-session, per-client and per-surface budgets, constrained by existing kernel maximums. Track bytes by actual allocated stride * height, including simultaneously retained prior generations and intermediate compositor output buffers.
- An application cannot force a larger pool by excessive COMMIT or callback requests; limit outstanding frames and coalesce replaceable redraws. Give currently focused/interactive surfaces priority under measured demand while preserving fairness for visible background panels.
- Fully hidden/occluded, far-away or semantic-zoom-collapsed panels may be throttled and eventually release **reusable** old buffers after explicit compositor release; do not reclaim an \`IN_USE\` buffer or silently mutate a client buffer. Visibility alone is neither authorization to read private pixels nor permission to discard app/document state.
- Reduced refresh rate and texture/cache eviction are distinct from forced buffer deallocation. An inactive client may retain its last authorized completed image for fast redisplay; private/locked scenes follow G5-D22 secure-redaction policy, not generic cache optimization.
- Scale-aware representations under G5-D06 may use independent summary/icon render objects; the compositor does **not** invent low-resolution private app screenshots or rescale a surface into an unauthorized preview.

#### Proposed protocol additions — not yet existing ABI
| Operation/event | Meaning | Invariant |
|---|---|---|
| \`BUFFER_POOL_HINT\` | Client supplies optional expected cadence/latency class | Hint only; no allocation privilege |
| \`BUFFER_BUDGET\` | Trusted compositor communicates admitted limits and preferred maximum in-flight frames | Cannot exceed checked ownership/memory budgets |
| \`BUFFER_ATTACH\` + \`SURFACE_COMMIT\` | Use existing capability-checked surface snapshot flow | Atomic validation; old committed state remains on failure |
| \`FRAME_CALLBACK\` | Compositor signals an opportunity/result associated with a serial | Not guaranteed display timing or a license to overwrite buffers |
| \`BUFFER_RELEASE\` | Explicit generation-bound notification that compositor is finished reading | Old/stale generation ignored; never release twice |
| \`THROTTLE\` / \`RESUME\` | Advisory policy on new frames based on occlusion/pressure | Must not suspend client process without distinct policy authority |
| \`BUFFER_BUDGET_CHANGED\` | Pressure-related new limits | Existing in-use allocations safely drain before shrinking target |

Transport control messages through G5-D32 \`G5.IPC.v1\` where process boundaries exist. Existing internal \`graphics_surface_*()\` / \`graphics_buffer_*()\` functions and existing configure/ACK remain authoritative until explicit extension and QEMU verification. For 256-byte IPC messages, attach only bounded descriptors/handles, never inline large pixels.

#### Lifetime and synchronization state machine
\`\`\`text
FREE/RELEASED -> client obtains reusable writable buffer
 -> READY (client finished drawing)
 -> ATTACHED_PENDING -> COMMITTED -> IN_USE (compositor may sample)
 -> RELEASED (compositor no longer samples; callback/serial checked)
 -> reusable or destroyed when references/leases reach zero
\`\`\`
This is a **logical** state machine; map to existing enum states without renaming existing ABI. Never render into an in-use buffer. Buffer generation and commit/presentation serials reject out-of-order/stale releases; refcounts and process teardown must preserve memory while any authorized reader remains.

#### Performance design targets and measurements (to be calibrated in QEMU)
- **Correctness first:** no torn/partially committed presentation, use-after-free, uninitialized pixels or privacy leaks; safe degraded rendering under pressure.
- **Latency:** instrument input-to-frame-present time and median/p95; do not assert a specific production latency SLA before baseline benchmarks.
- **Frame pacing:** count dropped, coalesced, repeated and late frames, callback latency and per-client fairness. Favor smooth interaction without unlimited queuing.
- **Memory:** track per-buffer allocations, peak pool bytes, retained buffers by reason, memory-pressure evictions and failure rates; quotas must include 64 MiB object max, 64 total buffers today and compositor backbuffers.
- **Scalability:** measure increasing numbers of panels, visible surfaces, occluded surfaces and mixed refresh demands across multiple camera zoom levels; verify proportional CPU rendering where damage/occlusion culling is available.
- **Power/CPU:** background panels must avoid unnecessary redraws when not visible, unless app-level work is independently authorized.
- Runtime targets for frame interval/latency, minimum supported resolution/number of panels and memory thresholds will be set from test hardware plus QEMU observations, not fabricated in this document.

#### G5-D33 acceptance tests
1. Default interactive two-buffer workflow and opportunistic third buffer; simulate memory pressure and ensure bounded growth and reliable shrink.
2. Verify no overwrite of \`IN_USE\` buffer, no premature release, serial/generation checks, safe reuse and teardown.
3. Alternate visible/occluded/hidden semantic-zoom states rapidly; check frame callback, focus, input capture and privacy behavior.
4. Ensure pending/committed surface transactions are atomic under invalid metadata, stale configure ACK, window resize and failed buffer allocation.
5. Exercise 64 MiB-per-buffer and 64-object global limits, checked width/height/stride overflow and per-client quota denials.
6. Crash app, compositor and Shell between attach/commit/release; verify complete reference/lease recovery without leaking pixels or kernel memory.
7. Run QEMU software-compositor stress with multiple independent Ring 3 clients; record p50/p95 input latency, frame cadence, frame drops, peak bytes and CPU.
8. Regression: secure overlays supersede ordinary surfaces, revoke/lock prevents stale previews, and software mode remains usable without GPU acceleration.

**Implementation dependency:** Define and verify public buffer lifecycle notifications and any necessary Ring 3 graphics buffer mapping/transfer ABI. Exact scheduling/admission algorithm, measurable memory thresholds and frame cadence numbers remain engineering tasks, not features claimed complete.


### 3.27 Aurora Spatial Rendering — visibility, regions and scalability (G5-D34)

**Approved design:** a hybrid, CPU-first rendering system. The Shell-side Canvas Engine maintains a very large *logical* spatial model; only a bounded, relevant subset of visible graphical instances is submitted to the compositor. The compositor remains the trusted renderer/owner of surfaces, security overlays and input focus. The spatial index is an **optimization, never an authorization mechanism**.

#### Current source baseline checked 2026-10-08
- \`kernel/include/aurora/software_compositor.h\`: compositor presently supports at most **64 scene nodes** (\`AURORA_COMPOSITOR_MAX_NODES\`), pixel positions \`int32_t x/y\`, an integer scale limit \`AURORA_COMPOSITOR_MAX_SCALE=4\`, a single pending damage rectangle, occlusion accounting and composition/hit-test functions.
- \`docs/graphics/COMPOSITOR_CONTRACT.md\`: CPU software backbuffer, clipping, occlusion, atomic scene operations and damage-based frame scheduling are already canonical concepts.
- \`docs/graphics/SURFACE_BUFFER_PROTOCOL.md\`: bounded buffer damage, surface commit and explicit release.
- A Canvas-wide quadtree/R-tree index, arbitrary-camera transform bridge, multi-region dirty tiling and large-world scene virtualization are **proposed**, not verified existing runtime implementations. Do not pretend the current 64-node compositor is already capable of displaying unlimited panels.

#### Component ownership and data flow
\`\`\`text
User / Navigator / Spatial Gestures
            ↓ camera transform (Shell Canvas Engine)
Canvas spatial model + security-aware visibility filtering
            ↓ viewport query against bounded spatial index
Visible spatial instances + semantic zoom decisions
            ↓ ordered scene diff / dirty regions
Authorized Shell → G5.SpatialScene.v1 → compositor
            ↓ clipping, damage, occlusion, safe presentation
Display output (software first; GPU backend may follow)
\`\`\`
- Canvas model stores stable object IDs, world bounds, object revision, layer/group IDs, semantic zoom representation, visibility rules and coarse geometry; it does not own app graphics memory or bypass surface capability checks.
- World-to-screen transformation uses a bounded numeric representation (proposed signed 64-bit fixed-point or origin-rebased doubles with checked conversions) and **camera-relative rebasing** before casting to current compositor \`int32_t\` coordinates. Guard NaN/Inf, overflow, rounding jitter, extreme aspect ratios and huge camera offsets.
- Spatial index: begin with a benchmarked dynamic **R-tree or loose quadtree** selected after workload tests; a simple validated bounding-box scan remains the correctness/reference implementation and fallback. Index updates are versioned for add/remove/move/resize/reparent/layer changes and crash rebuild.
- Coarse search identifies possible viewport intersections; fine validation applies transform, clipping, z-order, semantic level, occlusion and D22 privacy policy **before** creating visible nodes or hit-test targets.
- Nodes outside viewport are **logical objects**, not compositor surface nodes. Cache only approved static snapshots or metadata within quotas; a hidden client's live application state persists independently of whether its surface is currently submitted.
- Large semantic groups, graph links, note strokes, overlays and layer regions may use **batched scene-native primitives** to avoid consuming one privileged compositor surface per line/annotation; these primitives require a trusted, bounded graphics protocol and must not interpret arbitrary executable content.
- Graph edges crossing the viewport are clipped; dangling endpoints cannot expose hidden/private nodes, including through labels, highlights or Navigator/minimap aggregations.

#### Region-based damage, draw scheduling and caches
- Track changes at world-object level; project their previous/new screen bounds to damage and merge into **bounded dirty tiles/rectangles** (or full-frame fallback if fragmentation is excessive). Camera movement, zoom, exposure/occlusion changes and theme updates invalidate impacted cached regions.
- Scene deltas carry object/scene generations so compositor rejects stale moves and discarded permission/session scopes. Visible set updates, input hit testing and surface visibility changes should publish transactionally to avoid ghost or clickable invisible objects.
- Limit per-frame work, number of visible compositor nodes, per-session index bytes, tile-cache bytes and queued scene deltas. Under pressure: evict optional tiles, reduce non-essential effect quality, coalesce redundant camera update frames, then use a documented quality fallback; **never** skip secure overlay or revoke/hide events.
- Adaptive Buffering D33 controls the per-surface buffer lifecycle; spatial tile caching is a separate Shell/compositor-level concern and cannot overwrite \`IN_USE\` buffers or alter client buffer ownership.
- Software rendering is the required baseline; cache/index design should not embed CPU-only pixel assumptions that prevent a later accelerated compositor backend.
- Hit-test: use the same versioned spatial candidate set but compositor-verified surface/input-region authority. On index/scene mismatch, cancel interaction safely rather than route to an old location.

#### Proposed additional contracts (not implemented)
| Contract | Data / operation | Safety |
|---|---|---|
| \`G5.SpatialIndex.v1\` | upsert/remove/batch, bounding-box query, revision, rebuild | bounded query, index mismatch fallback |
| \`G5.VisibleSet.v1\` | camera revision, representation level, ordered visible-object IDs | permission-filtered, stale-set rejection |
| \`G5.SceneDelta.v1\` | add/remove/update visible node, transform and damage, atomic scene generation | Shell authority required, transactional apply |
| \`G5.RenderBudget.v1\` | maximum scene nodes, tile memory, update rate, degrade hints | hard bounds; never controls access permissions |
| \`G5.HitTest.v1\` | screen point, camera/index revision, input-region lookup | validates compositor ownership and session |
These logical contracts may be implemented as internal C interfaces first; cross-process data must respect approved \`G5.IPC.v1\` 256-byte framing and capability-scoped bulk handles.

#### Performance targets and acceptance gates
1. **Reference correctness:** compare every indexed visibility result with a complete brute-force visibility pass on randomized canvases and edge-case coordinates; test moving/resizing objects and overlapping groups.
2. **Scale stress:** create synthetic logical scenes of 1k, 10k and 100k lightweight notes/nodes; viewport work must remain bounded by visible candidates and changed elements where applicable, without promising all are live compositor surfaces.
3. **Current compositor ceiling:** prove virtualized visible set respects the current 64-node limit, and document explicit fallback when more than 64 *live surfaces* are simultaneously visible. Any increase requires a separate bounded kernel/compositor change and QEMU validation.
4. **Pan/zoom precision:** very distant camera coordinates, rapid zoom, origin rebasing, screen-to-world-to-screen round-trips, deterministic hit testing and reduced motion.
5. **Damage:** compare full-frame output to tiled/dirty output pixel-for-pixel in software mode; include changing occlusion, zoom, theme, layer and secure overlay.
6. **Memory:** enforce cache/index quotas, eviction, teardown on lock/logout and memory-pressure fallback without exposing prior users' pixels.
7. **Crash/recovery:** Shell or compositor restart rebuilds index/visible sets from trusted state and invalidates stale nodes/handles; client processes remain isolated.
8. **Benchmark evidence:** collect p50/p95 viewport-query duration, visible candidates, index update duration, compositor frame time, CPU usage, memory peak, dropped/coalesced frames and input-to-present latency in QEMU; do not claim success before runtime measurements.

**Unresolved engineering choices:** R-tree vs loose quadtree; exact world coordinate representation; tile size/dirty thresholds; cache budget; scene-delta wire encoding; mitigation for the 64-live-node compositor limit; benchmark pass thresholds. These are planned for the next Canvas Engine architecture and MVP gate decisions.


### 3.28 Hybrid Spatial Index Engine — backend-neutral contracts (G5-D35)

**Approved:** use a stable \`G5.SpatialIndex.v1\` facade separating Canvas data/visibility consumers from index implementation. Support candidates (dynamic R-tree and loose quadtree) and a deterministic full-scan reference implementation; select the production backend only after repeatable benchmarks. This is not approval for implementing both optimized backends before the MVP.

#### Index responsibility and ownership
- The Canvas Engine owns one canonical object registry with stable opaque IDs, revisions, bounded world AABBs, layer ID and coarse visible/collapsed flags. The spatial index stores **derived location metadata**, not authoritative object ownership, credentials, raw surface handles or persistent document content.
- Query returns bounded candidate IDs and object revisions; privacy authorization, graph visibility, semantic zoom, exact transformed geometry, occlusion, hit-test eligibility and secure-scene gating are revalidated **after** index lookup. An index result must never itself be authority.
- Index may be reconstructed from the object registry after crash, backend switch or detected inconsistency; no requirement to persist implementation-specific tree nodes in the workspace.

#### Versioned logical API (draft)
\`\`\`c
/* Illustrative contract only; not yet a compiled ABI. */
typedef uint64_t g5_spatial_id;
typedef uint64_t g5_spatial_revision;
typedef struct { int64_t min_x, min_y, max_x, max_y; } g5_world_aabb;

typedef struct {
    g5_spatial_id object_id;
    g5_spatial_revision object_revision;
    g5_world_aabb bounds;
} g5_spatial_entry;

enum g5_index_status {
    G5_INDEX_OK, G5_INDEX_INVALID, G5_INDEX_OUT_OF_BUDGET,
    G5_INDEX_STALE, G5_INDEX_NEEDS_REBUILD
};

/* Internal logical operations: */
create(config, backend) -> index_handle
upsert_batch(entries, count, expected_scene_revision) -> new_scene_revision
remove_batch(ids, count, expected_scene_revision) -> new_scene_revision
query_aabb(viewport_bounds, scene_revision, result_limit, cursor) -> candidates
rebuild(registry_snapshot) -> index_generation
stats() -> counts, bytes, depth, build/query timings
destroy() -> void
\`\`\`
- Coordinate representation above is a **provisional signed 64-bit world-unit AABB**, with a fixed world-unit scale to be frozen in the camera contract. Validate \`min <= max\`, overflow, extreme spans, empty bounds and off-grid origins. Camera rebasing (D34) remains mandatory before converting to compositor-local screen coordinates.
- Updates are revisioned and either fully applied or rolled back; query is tied to a stable scene revision/index generation. A stale cursor or out-of-date result returns a retry/error instead of silently omitting newly visible objects.
- Pagination and bounded query count prevent unbounded allocation; stable ordering for deterministic tests (e.g. ID order) is performed outside the index when necessary.
- Objects spanning many quadtree cells must be stored once in a bounded ancestor/overflow strategy; forbid explosive per-cell duplication. R-tree nodes require controlled split/reinsert/rebalance and hard height/depth limits.
- Static immovable primitives may later have specialized indexes, but this is not a precondition for the initial dynamic index.

#### Backends, tuning and fallbacks
| Backend | Expected strength | Tradeoff/gating |
|---|---|---|
| Reference scan | Simple correctness oracle, predictable traversal | O(N) queries; safe fallback for modest scenes |
| Dynamic R-tree | Mixed sizes, large overlapping bounds and updates | Node split/rebalancing complexity |
| Loose quadtree | Locality and pan/zoom spatial subdivisions | Sparse huge extents, object movement and broad bounds require tuned expansion |
- Benchmark candidate backends against **identical** snapshots and deterministic event traces; collect insert/remove/move rates, viewport query p50/p95, memory per object, query candidate amplification, rebuild cost and time-to-first-scene.
- Select the primary backend via written measurement results, not preference; allow compile-time/configured fallback. Do **not** silently change backend mid-transaction or in response to an ordinary frame without generation-safe rebuild and validation.
- Under memory pressure reject unbounded growth; a correct bounded full scan can be used for small scenes, or an explicit degraded/error view for larger ones. Never show incorrectly filtered content or route input through stale results.

#### Index ↔ compositor consistency
1. Commit mutation in canonical scene transaction; update index generation; calculate visible-set delta.
2. Validate privacy/session/layer state and present a compositor scene delta tied to a known revision.
3. Update hit-test mapping only once corresponding compositor state is accepted; cancellation is safe on mismatch.
4. On Shell/compositor restart or permission revocation, discard stale generations and rebuild from trusted registry with no unauthorized cached hints.
5. Use coarse world-space AABB for candidate lookup, then screen-space exact check on object transform; never use the index to grant surface ownership.

#### Required test matrix
- Differential randomized property tests vs reference scan for 0, 1, 1k, 10k and 100k objects; deterministic seeds and reproducible traces.
- Stress frequent move/resize/reparent/group/layer changes, long thin annotations, zero-area edge cases and extremely distant 64-bit coordinates.
- Verify no missing viewport intersections (false negatives); extra coarse candidates permitted only with bounded amplification and subsequent exact filtering.
- Pagination boundary, cursor invalidation and atomic rollback on rejected mutations or budget exhaustion.
- Simulate index corruption/rebuild, backend switch, process restart, deleted object IDs and revocation while results are in flight.
- Benchmark initial load, update throughput, query latency p50/p95, memory footprint, full-scene fallback threshold and integration with D34 compositor's 64-node current ceiling.
- Use QEMU/CI for contract/runtime gates after implementation, distinguishing unit-level benchmark results from compositor end-to-end evidence.

**Deferred:** definitive R-tree vs loose-quadtree choice, world-unit fixed-point scale, concrete time/memory budgets, implementation headers and benchmark acceptance thresholds. These require measured workloads and source integration.


### 3.29 Aurora Hybrid Coordinates — numeric types, camera transforms and precision (G5-D36)

**Approved:** use deterministic integral/fixed-point coordinates for persistent Canvas world geometry, double-precision floating point for transient camera and zoom calculations, and camera-relative rebasing to compositor-local integer pixel coordinates. Actual numeric parameters below are a **proposed G5 v1 wire/storage contract**, subject to API review and precision tests before freeze.

#### Coordinate domains and proposed types
- Persistent **world units**: signed \`int64_t\` ticks, **1024 ticks per logical Canvas unit** (Q54.10 conceptual scale). \`g5_world_coord_t=int64_t\`; object AABBs use four such values, with min ≤ max. Avoid unchecked addition/subtraction even on integer offsets; valid workspace extents are bounded by signed 64-bit representable range, not literally mathematically infinite.
- Positions are absolute world ticks; widths/heights are validated nonnegative differences using checked wide intermediates. Long strokes/paths store bounded arrays of local deltas relative to an anchor, rather than repeated high-magnitude absolute coordinates.
- Camera state uses \`double center_x_units\`, \`center_y_units\`, \`zoom\`, viewport physical dimensions, display scale, and monotonic \`camera_revision\`; calculations use finite values only. For truly distant scene coordinates, first subtract **integer world-space origin** close to the camera using overflow-checked/wide arithmetic, then cast small differences to double.
- Screen-space compositor bridge uses checked rounded \`int32_t\` pixel positions after clipping to output/working viewport; existing compositor positions are \`int32_t\` and existing integer scale limit is 4. Fractional/arbitrary semantic zoom therefore needs Shell-side representation/transform support or an explicitly verified compositor extension; never assume the present compositor directly implements arbitrary \`double\` scales.
- World-space AABB is always authoritative for placement/indexing; screen-space bounds are temporary, recomputed from current camera state rather than persisted. Serializing raw C struct bytes is forbidden because of padding, endian and ABI evolution.

#### Camera transform (logical)
Given world point ticks \`W=(wx,wy)\`, integer rebasing origin \`O=(ox,oy)\`, camera center world ticks \`C=(cx,cy)\`, normalized zoom \`z>0\`, viewport center pixels \`V/2\`, and device scale \`d>0\`:
\`\`\`text
delta_ticks = checked_wide_sub(W, O) - checked_wide_sub(C, O)
delta_units = double(delta_ticks) / 1024
screen_px = viewport_center_px + delta_units * z * d
\`\`\`
Implement the delta using overflow-checked wide arithmetic (e.g. audited 128-bit intermediate) or safe branch decomposition **before** any double conversion. The origin terms must never silently overflow. Inverse pointer mapping starts from screen delta and applies \`1/(z*d)\`, then performs explicitly checked rounding into world ticks with a documented nearest-even or other consistent rounding rule. Treat camera center as an integer-tick anchor plus a **bounded fractional camera offset** if precision tests show plain doubles cannot retain sub-tick motion across far coordinates.
- Pointer-anchored zoom D24 preserves the same world point beneath the cursor to within an explicitly measured tolerance; focus D20, portal D25 and Navigator D07 share one camera implementation.
- Pan, wheel/pinch zoom and inertial movement operate on transient floating-point deltas and commit checked camera state; rejected/NaN/Inf inputs preserve the last valid camera unchanged.
- Zoom limits, pan velocity and device-scale ranges must be bounded and measured with G5-D06 semantic zoom and font/readability behavior; min/max values remain open until UI/graphics benchmarks.
- Store camera checkpoint/portal target as validated integer world anchor, bounded fractional offset if supported, zoom and version; camera navigation alone does not change object placement or structural Canvas History.

#### Type/interface sketch (proposed header, not written to source)
\`\`\`c
typedef int64_t g5_world_coord_t;
#define G5_WORLD_TICKS_PER_UNIT INT64_C(1024)
typedef struct {
    g5_world_coord_t x, y;
} g5_world_point;
typedef struct {
    g5_world_point min, max;
} g5_world_aabb;
typedef struct {
    g5_world_point center_anchor;
    double offset_x_units, offset_y_units;
    double zoom, device_scale;
    uint32_t viewport_width_px, viewport_height_px;
    uint64_t revision;
} g5_camera_state;
/* Candidate APIs: checked_world_add/sub, world_to_screen_clipped,
   screen_to_world_checked, camera_pan, camera_zoom_at_pointer,
   camera_rebase, camera_fit_bounds, camera_validate. */
\`\`\`
These types belong in a shared, versioned user-space Canvas/graphics header to be selected when implementing G5; they do not modify \`graphics_surface.h\` or kernel pointer ABI today.

#### Numeric invariants and concurrency
- Quantize persistent transforms once per committed layout transaction; reuse canonical world ticks in scene history, clipboard geometry and template manifests for deterministic round-trips.
- Negative coordinates work symmetrically. Sub-tick visual movements can be accumulated transiently but persist only after a consistent rounding policy; no hidden silent mutation across camera transitions.
- Camera generation and scene revision are independent. Each hit-test/visible-set query includes both, so input arriving after camera movement cannot act using a previous viewport transform.
- Clipped offscreen objects must never be passed to compositor with invalid pixel coordinates; privacy/secure scene priority applies independent of transform results.

#### Precision and regression tests
1. Unit conversion and checked arithmetic at zero, negatives, \`INT64_MIN/MAX\`, crossing origin and invalid AABB dimensions.
2. Round-trip world→screen→world and zoom→inverse zoom across distant world anchors, typical/small/large bounded zoom and high-DPI viewports; define maximum tolerable tick/pixel error after benchmark.
3. Repeated pan and pointer-anchored zoom cycles (e.g. 100k deterministic operations), checking drift, under/overflow, no NaN/Inf, no camera-jump on origin rebasing and stable snapped placement.
4. Pixel-perfect/deterministic reproducibility for recorded Canvas transactions, clipboard/template export and history undo/redo on supported target architectures.
5. Viewport intersection and \`G5.SpatialIndex.v1\` query equivalence with brute force near coordinate boundaries; no false negatives from rounding.
6. Hit-test correctness during rapid moving camera, portal jump, focus and compositor resize; reject stale generation and preserve valid input ownership.
7. Verify conversions to current compositor \`int32_t\` coordinates and maximum supported scale; explicitly gate future arbitrary zoom transform changes.
8. QEMU multi-client software rendering under stress, with measured p50/p95 camera-update and hit-test latency, jitter, frame time and memory; distinguish design unit tests from runtime integration evidence.

**Still to freeze after measurements:** supported world distance/window, fixed-point tick scale (1024 proposed), rounding mode, zoom clamp, transform tolerance, interpolation details and whether a compositor affine-transform protocol extension is required for arbitrary scales.


### 3.30 Aurora Hybrid Scene Graph — hierarchy and integration contracts (G5-D37)

**Approved:** visual scene structure is a bounded **parent–child tree** for transforms, clipping and rendering; *semantic* relationships are stored and queried in an independent graph. An object may have one visual parent while having many permitted semantic memberships. Neither relationship implicitly transfers application, filesystem or Identity authority.

#### Canonical scene model and invariants
- Shell Canvas Engine owns a canonical versioned registry of logical **scene nodes**, each with stable opaque ID, node generation, type, one visual parent (or root), sibling stacking order, local transform, local bounds, visibility/lock flags, layer ID and optional authorized application surface reference. The existing compositor surface graph is a **derived projection** of this registry, not its persistent source of truth.
- Define node types: ROOT, VISUAL_GROUP, APP_PANEL, CONTENT_REF, NOTE/SHAPE, RELATION_RENDER_PROXY and PORTAL; type-specific extensions use bounded validated payloads. Semantic group memberships and edge metadata live in the Content Graph, **not** in visual parent pointers.
- Visual parentage forms an **acyclic forest rooted at explicit workspace/layer roots**; one visual parent per node, bounded depth, checked fanout and atomic reparent. Reject self-parent, descendant-parent, duplicate ID, cross-session parent and stale revision.
- World placement is computed from local-to-parent transforms accumulated to the root using D36 checked hybrid coordinate math. Use camera-relative origin rebasing before final screen conversion. Cache world AABBs by scene generation; invalidate descendants after parent edits with bounded lazy recompute.
- A collapsed group changes its rendered representation and hit-testing target, not ownership of children or their application state; links to children remain in the semantic graph subject to Privacy Layers D22. Hidden/locked layers D26 and Spatial Focus D20 apply *after* transform resolution.
- Model a single logical live app instance with one authorized surface placement in the current scene; moving Hub ↔ Canvas (D13/D14) is a transactional rehost rather than cloning processes or buffers.

#### Logical interface contracts (proposed, not yet compiled)
| Contract | Producer → consumer | Operations/data | Revision and security checks |
|---|---|---|---|
| \`G5.SceneGraph.v1\` | Shell tools/History → Canvas Engine | create/destroy, reparent, set local transform, set visibility/layer/order, snapshot children | session-scoped IDs, bounded hierarchy, atomic transaction, revision |
| \`G5.SceneBounds.v1\` | Scene Graph → Spatial Index | object ID + computed **world AABB** + object revision; upsert/remove batch | update/rollback in same scene generation; no false-negative visibility queries |
| \`G5.SceneSemantic.v1\` | Scene Graph ↔ Content Graph broker | map visual node ↔ typed content reference; authorized edge/group query, visibility-filtered metadata | links are non-authoritative; no implicit read grants or private count leaks |
| \`G5.SceneProjection.v1\` | Canvas Engine → Compositor | authorized visible set, stable surface capability, screen transform, clip, opacity, z/order, damage and scene revision | compositor validates handles, ownership, secure surface ordering and accepts atomically |
| \`G5.SceneHitTest.v1\` | input router ↔ Canvas/Compositor | input point, camera revision, scene generation, candidate node IDs | final target must match committed compositor input region and session authority |
- Treat operations above as **logical C interface candidates**. Any out-of-process variant uses D32's bounded IPC envelope and capability-scoped data transfer, never raw pointers or implicit string object names.

#### Atomic mutation pipeline
1. Verify caller capability/session generation and object preconditions. Validate target node type, one-parent rule, layer edit-lock and graph permissions.
2. Stage a bounded scene transaction (e.g. reparent+transform+layer changes) without changing authoritative visible/hit-test state.
3. Compute affected subtree transforms/world AABBs and prepare D35 index batch. Either both scene registry and index advance coherently to a new scene revision, or reject/rollback and keep prior committed state. On index failure, rebuild/reference-scan fallback and block unsafe hit testing.
4. Independently resolve authorized D09/D10 semantic edges; edges do **not** imply visual parenthood, and an edge update does not duplicate/move the source panel.
5. Recalculate D34 viewport visible set, D06 representation levels, D26 layers and D22 privacy. Stage scene delta for compositor, respecting its current maximum of 64 scene nodes; batch notes/edges where a verified primitive API permits.
6. Publish compositor transaction and new hit-test revision only after compositor acknowledgement. On timeout/crash, invalidate outstanding revision and retry safe reconstruction; never expose partly applied secure scene changes.
7. Record user-facing structural mutation in Canvas History D17 after successful commit; do not mutate app-internal documents.

#### Canonical conceptual structures
\`\`\`c
/* Design sketch only; final ABI/layout not frozen. */
typedef uint64_t g5_scene_node_id;
typedef struct {
    g5_scene_node_id id, parent_id;
    uint64_t generation, scene_revision;
    uint32_t node_type, layer_id;
    int32_t sibling_order;
    g5_world_point local_origin; /* D36 fixed-point */
    g5_world_aabb local_bounds;
    uint32_t flags;
    /* secure surface capabilities are session-scoped external references */
} g5_scene_node_descriptor;
\`\`\`
Hierarchy translation-only transforms are the **proposed initial subset**. Scaling/rotation of groups would require explicit fixed-point matrix conventions, AABB broad-phase inflation and renderer support and must not be silently assumed present.

#### Safety, performance and recovery
- Bound max hierarchy depth and per-frame dirty-subtree work; lazy transform invalidation should propagate parent revision without forcing a full Canvas scan. Detect and reject cycles before committing any modification.
- Scene Graph and Spatial Index are separate derived data domains with independent cache layouts but one committed scene revision contract. Content Graph relations may be eventually indexed; user-facing privacy changes and revocations must be synchronously enforced at projection/query time.
- Reparenting a node across privileged session/workspace scopes is forbidden without an explicit higher-level authorized transfer; names, portals or clipboard manifests are never authority.
- On Shell restart, recreate the scene registry from authorized state where available, then rebuild indexes and compositor projection with fresh generation-bound surface handles. On compositor restart, discard stale projection IDs; never resurrect a dead app surface by copying its old numeric ID.

#### Acceptance test matrix
1. Create deep/wide bounded hierarchies, reparent and move groups; compare world bounds to a full recursive/reference transform walk.
2. Reject self/ancestor cycles, cross-session parentage, invalid child types, stale revisions and depth/fanout overflow **atomically**.
3. Verify one visual parent versus multiple semantic group/edge memberships; deleting a visual group does not delete unrelated content.
4. Differential index query after reparent/move/collapse/layer changes, with no false-negative viewport objects and bounded dirty work.
5. Validate secure/hide/focus/group-collapse projection and no private relation labels/previews, ghost inputs or bypass of compositor secure overlay ordering.
6. Verify one live app state and surface after Hub ↔ Canvas rehost; no duplication or unintended lifecycle reset.
7. Inject compositor/index/graph-broker failures before and after commit; maintain rollback or safe reconstruction, correct History ordering and stale-token rejection.
8. Record p50/p95 subtree update, viewport projection, hit-test and scene-commit latency for 1k/10k/100k logical nodes in software QEMU/CI, respecting the current 64-node compositor ceiling.

**Implementation blockers:** actual source layout for the Shell Canvas registry; graph broker authority; compositor atomic scene-delta API; validated surface rehost semantics; fixed numeric transform conventions; performance and depth limits. Design approval does not claim these APIs exist today.

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
- G5.2: compositor spatial scene, focus, mouse interactions, Spatial Gestures with pan/pointer-centered zoom and optional inertia.
- G5.3: live application panels and semantic representations.
- G5.4: minimal modular Hub, system-module host, launcher/search, deterministic Universal Command palette (AI interpretation optional), and Aurora Visual Studio theme-editor baseline.
- G5.5: Navigator, manual relation pins/links, basic suggested layouts, capability-mediated Smart Transfer, bounded live-session Canvas History, spatial/semantic selection, hybrid Spatial Groups, contextual Spatial Focus, basic Canvas Portals, functional Spatial Layers, Spatial Notes, Spatial Clipboard and Canvas Templates.
- G5.6: QEMU/CI end-to-end, privacy/authorization, performance and recovery tests.
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
- Verify Universal Command offline registry/search, safe action previews and confirmation, cancellation, authorization checks and optional AI-to-typed-command mediation.
- Verify G5-D22 privacy enforcement across Canvas, Hub, graph, search, Navigator, history, presentation mode, lock/revocation and cached rendering.
- Verify Visual Studio theme preview/apply/cancel, validated export/import, accessibility fallback and refusal of unsafe theme resources.
- Verify D24 pan, pointer-anchored zoom, inertial cancellation, keyboard alternatives, reduced-motion behavior and safe input capture revocation.
- Verify D25 bookmark/portal creation, typed destination resolution, back navigation, stale-target handling and privacy-safe cross-session denial.
- Verify D26 layer create/rename/reorder, visibility/hit-test coherence, edit locking, history undo and secure-overlay priority.
- Verify D27 rich-text note and drawing primitives, edit/undo, zoom/layers/groups integration, bounds limits and privacy-safe persistence/recovery.
- Verify D28 multi-object clipboard geometry, paste copy/link semantics, atomic cut rollback, permission isolation and sensitive-history cleanup.
- Verify D29 template round-trip, fresh-ID remapping, placeholder resolution, atomic instantiation/undo and non-disclosure of private data.
- No cross-client graphics/control privilege escalation, stale-generation use or cross-session content leak.
- Closing a process/session cleans all spatial objects and tokens safely.
- Failure injection: broken module/browser does not terminate system shell; safe graphics recovery remains available.
- Verify G5-D33 adaptive buffer admission, pressure reduction, release correctness, frame pacing, memory quotas, secure occlusion and crash recovery.
- Verify G5-D34 spatial query equivalence, distant-camera precision, scene node virtualization, damage correctness, privacy-aware hit testing and benchmarked scalability.
- Verify G5-D35 backend-neutral index mutations, full-scan differential equivalence, generation-safe queries, bounded memory and reproducible R-tree/quadtree benchmarking.
- Verify G5-D36 world coordinate overflow handling, camera-rebase precision, anchored-zoom stability, round-trip transform/hit-testing and negative/extreme world coordinates.
- Verify G5-D37 scene reparent/cycle prevention, semantic/visual separation, coherent index/projection generations, single-instance rehost, privacy-safe hit testing and crash recovery.
- Explicit runtime QEMU tests and CI evidence are required before marking any item implemented.

## 7. Pending design decisions
- Actual Hub module layout and lifecycle; responsive size-class thresholds and client notifications for G5-D15; gesture/accessibility details of G5-D13, bidirectional transfer interaction details and view cloning.
- Camera gesture details, shortcuts, zoom thresholds and 64-bit coordinate limits.
- Widget security model, spatial surface protocol and focus strategy.
- Content Fabric storage/index format and relation graph semantic schema.
- Visual theme tokens, accessibility and motion-reduction behavior.
- G5/G7 persistence boundary (especially Canvas History retention/checkpoint survival); browser engine milestone.
- Universal Command registry schema, AI provider policy and approval thresholds.
- G5-D23 theme token schema, preset export format, minimum accessibility contrast and graphics effect budgets.
- G5-D24 camera transform limits, pan gesture conflict policy, inertial parameters and touch gesture transport gates.
- G5-D30/D31 exact process inventory and privilege/dependency map; review G5-D32 proposed 48-byte wire envelope, bulk-memory Ring 3 API, queue backpressure, typed operation schemas and failure isolation tests.
- G5-D33 adaptive buffer pool admission/eviction thresholds, Ring 3 buffer-release notifications, frame pacing targets and multi-client performance baselines.
- G5-D34/D35 spatial-index benchmark backend selection, viewport virtualization, dirty-tile budgets and the compositor 64-node capacity constraint.
- G5-D36 fixed-point world-tick scale, camera-anchor arithmetic, checked screen conversion, zoom thresholds and numerical precision/drift acceptance.
- G5-D37 scene hierarchy depth and transform scope, atomic registry/index update, semantic graph linkage, compositor projection/ack and hit-test generation coherence.
- Definition of the G5 minimal shippable acceptance gate.

## 8. Change control
Each approved decision adds an ID and a short design contract. Mark proposed features as proposals until accepted. Design approval never implies source implementation or runtime verification. Update this document incrementally; link implementation evidence to the graphics implementation roadmap rather than rewriting achieved status here.
