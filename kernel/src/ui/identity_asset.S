.section .rodata.identity_assets,"a",@progbits
.balign 16
.global aurora_identity_background_png_start
.global aurora_identity_background_png_end
aurora_identity_background_png_start:
.incbin "assets/identity/aurora_identity_background.png"
aurora_identity_background_png_end:
.balign 16
.global aurora_identity_mark_png_start
.global aurora_identity_mark_png_end
aurora_identity_mark_png_start:
.incbin "assets/identity/aurora_mark.png"
aurora_identity_mark_png_end:
.section .note.GNU-stack,"",@progbits
