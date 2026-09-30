ARCH ?= x86_64

LIMINE_VERSION := 12.9.1
LIMINE_DIR := .cache/limine-$(LIMINE_VERSION)
BUILD_ROOT := build/$(ARCH)
KERNEL := $(BUILD_ROOT)/aurora-kernel.elf
ISO_ROOT := $(BUILD_ROOT)/iso_root
ISO := build/AuroraOS-$(ARCH).iso

.PHONY: all deps kernel iso run-bios run-uefi clean distclean

all: iso

deps:
	sh scripts/bootstrap-deps.sh

kernel: deps
	$(MAKE) -C kernel ARCH=$(ARCH)

$(LIMINE_DIR)/limine:
	$(MAKE) -C "$(LIMINE_DIR)"

iso: kernel $(LIMINE_DIR)/limine
	rm -rf "$(ISO_ROOT)"
	mkdir -p "$(ISO_ROOT)/boot/limine" "$(ISO_ROOT)/EFI/BOOT"
	cp "$(KERNEL)" "$(ISO_ROOT)/boot/aurora-kernel.elf"
	cp limine.conf "$(ISO_ROOT)/boot/limine/limine.conf"
	cp "$(LIMINE_DIR)/limine-uefi-cd.bin" "$(ISO_ROOT)/boot/limine/"
	cp "$(LIMINE_DIR)/limine-bios.sys" "$(ISO_ROOT)/boot/limine/"
	cp "$(LIMINE_DIR)/limine-bios-cd.bin" "$(ISO_ROOT)/boot/limine/"
	cp "$(LIMINE_DIR)/BOOTX64.EFI" "$(ISO_ROOT)/EFI/BOOT/"
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		"$(ISO_ROOT)" -o "$(ISO)"
	"$(LIMINE_DIR)/limine" bios-install "$(ISO)"
	rm -rf "$(ISO_ROOT)"
	@echo "Built $(ISO)"

run-bios: iso
	qemu-system-x86_64 \
		-M q35 \
		-m 512M \
		-cdrom "$(ISO)" \
		-serial stdio

run-uefi: iso
	@test -n "$(OVMF_CODE)" || (echo "Set OVMF_CODE=/path/to/OVMF_CODE.fd" && exit 1)
	qemu-system-x86_64 \
		-M q35 \
		-m 512M \
		-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
		-cdrom "$(ISO)" \
		-serial stdio

clean:
	$(MAKE) -C kernel clean
	rm -rf "$(BUILD_ROOT)/iso_root" "$(ISO)"

distclean: clean
	rm -rf build .cache third_party
