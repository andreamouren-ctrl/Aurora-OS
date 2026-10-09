.section .rodata.security_activity_assets,"a",@progbits
.balign 16

/*
 * Strong symbols override the weak zero-length placeholders in
 * security_activity_asset_stub.S only when the verified native catalog is
 * linked successfully.
 */
.macro AURORA_ACTIVITY_ASSET name, file
.global aurora_security_activity_\name\()_png_start
.global aurora_security_activity_\name\()_png_end
aurora_security_activity_\name\()_png_start:
.incbin "assets/identity/security_activity/\file"
aurora_security_activity_\name\()_png_end:
.balign 16
.endm

AURORA_ACTIVITY_ASSET panel, "aurora_security_activity_panel.png"
AURORA_ACTIVITY_ASSET event_card, "aurora_security_activity_event_card.png"
AURORA_ACTIVITY_ASSET status_info, "aurora_security_activity_status_info.png"
AURORA_ACTIVITY_ASSET status_warning, "aurora_security_activity_status_warning.png"
AURORA_ACTIVITY_ASSET status_critical, "aurora_security_activity_status_critical.png"
AURORA_ACTIVITY_ASSET category_auth, "aurora_security_activity_category_auth.png"
AURORA_ACTIVITY_ASSET category_session, "aurora_security_activity_category_session.png"
AURORA_ACTIVITY_ASSET category_credential, "aurora_security_activity_category_credential.png"
AURORA_ACTIVITY_ASSET header, "aurora_security_activity_header.png"
AURORA_ACTIVITY_ASSET empty, "aurora_security_activity_empty.png"
AURORA_ACTIVITY_ASSET error, "aurora_security_activity_error.png"
AURORA_ACTIVITY_ASSET loading, "aurora_security_activity_loading.png"
AURORA_ACTIVITY_ASSET load_more, "aurora_security_activity_load_more.png"
AURORA_ACTIVITY_ASSET filter_button, "aurora_security_activity_filter_button.png"
AURORA_ACTIVITY_ASSET filter_active, "aurora_security_activity_filter_active.png"
AURORA_ACTIVITY_ASSET end, "aurora_security_activity_end.png"

.section .note.GNU-stack,"",@progbits
