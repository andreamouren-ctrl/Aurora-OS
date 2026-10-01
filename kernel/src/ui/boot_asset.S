.section .rodata.boot_asset,"a",@progbits
.balign 16
.global aurora_boot_asset_start
.global aurora_boot_asset_end
aurora_boot_asset_start:
.incbin "assets/boot/aurora_boot.idx8"
aurora_boot_asset_end:
.section .note.GNU-stack,"",@progbits
