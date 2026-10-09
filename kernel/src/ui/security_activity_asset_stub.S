.section .rodata.security_activity_asset_stubs,"a",@progbits
.balign 16

/*
 * Always provide resolvable zero-length placeholders.  Real native artwork
 * objects export strong symbols with the same names and override these weak
 * definitions at link time.  With no artwork linked, start == end and the
 * graphics layer fails closed into the procedural renderer.
 */
.macro AURORA_ACTIVITY_ASSET_STUB name
.weak aurora_security_activity_\name\()_png_start
.weak aurora_security_activity_\name\()_png_end
aurora_security_activity_\name\()_png_start:
aurora_security_activity_\name\()_png_end:
.balign 16
.endm

AURORA_ACTIVITY_ASSET_STUB panel
AURORA_ACTIVITY_ASSET_STUB event_card
AURORA_ACTIVITY_ASSET_STUB status_info
AURORA_ACTIVITY_ASSET_STUB status_warning
AURORA_ACTIVITY_ASSET_STUB status_critical
AURORA_ACTIVITY_ASSET_STUB category_auth
AURORA_ACTIVITY_ASSET_STUB category_session
AURORA_ACTIVITY_ASSET_STUB category_credential
AURORA_ACTIVITY_ASSET_STUB header
AURORA_ACTIVITY_ASSET_STUB empty
AURORA_ACTIVITY_ASSET_STUB error
AURORA_ACTIVITY_ASSET_STUB loading
AURORA_ACTIVITY_ASSET_STUB load_more
AURORA_ACTIVITY_ASSET_STUB filter_button
AURORA_ACTIVITY_ASSET_STUB filter_active
AURORA_ACTIVITY_ASSET_STUB end

.section .note.GNU-stack,"",@progbits
