# Aurora OS — G5 Desktop Shell & Infinite Living Canvas
Status: **Design in progress — approved decisions D01–D31**
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
- G5-D30/D31 exact process inventory and privilege/dependency map; G5.IPC.v1 on-wire layout, queue backpressure and failure isolation tests.
- Definition of the G5 minimal shippable acceptance gate.

## 8. Change control
Each approved decision adds an ID and a short design contract. Mark proposed features as proposals until accepted. Design approval never implies source implementation or runtime verification. Update this document incrementally; link implementation evidence to the graphics implementation roadmap rather than rewriting achieved status here.
