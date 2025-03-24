# Executable name
SHELL := /bin/bash

HM_DIR = /run/media/grace/archlinux_data/OS/hm
# OVMF_DIR = ./edk2/Build/OvmfX64/DEBUG_GCC5/FV
# KERNEL = ./kvm/linux-5.15.178/arch/x86/boot/bzImage
# INITRAMFS = ./kvm/busybox-1.35.0/initramfs.cpio.gz

# HM_DIR = .
OVMF_DIR = ./edk2/Build/OvmfX64/DEBUG_GCC5/FV
KERNEL = ./kvm/linux-5.15.178/arch/x86/boot/bzImage
INITRAMFS = ./kvm/busybox-1.35.0/initramfs.cpio.gz

init_build:
	cd edk2 && \
	export EDK_TOOLS_PATH=${HM_DIR}/edk2/BaseTools && \
	source edksetup.sh BaseTools && \
	build

only_kernel:
	qemu-system-x86_64 \
		-kernel "${KERNEL}" \
		-initrd "${INITRAMFS}" \
		-nographic \
		-append "init=/init console=ttyS0"

only_ovmf:
	qemu-system-x86_64 \
		-nographic \
		-drive if=pflash,format=raw,unit=0,file="${OVMF_DIR}/OVMF_CODE.fd",readonly=on \
		-drive if=pflash,format=raw,unit=1,file="${OVMF_DIR}/OVMF_VARS.fd"

kernel_and_ovmf:
	qemu-system-x86_64 \
		-kernel "${KERNEL}" \
		-initrd "${INITRAMFS}" \
		-nographic \
		-drive if=pflash,format=raw,unit=0,file="${OVMF_DIR}/OVMF_CODE.fd",readonly=on \
		-drive if=pflash,format=raw,unit=1,file="${OVMF_DIR}/OVMF_VARS.fd" \
		-append "init=/init console=ttyS0"
server_bios:
	qemu-system-x86_64 \
		-m 4096 \
		-cdrom ./ubuntu-24.04.2-live-server-amd64.iso \
		-bios ./edk2/Build/OvmfX64/DEBUG_GCC5/FV/OVMF.fd\
		-nographic
server:
	qemu-system-x86_64 \
		-m 4096 \
		-cdrom ./ubuntu-24.04.2-live-server-amd64.iso \
		-drive if=pflash,format=raw,unit=0,file="${OVMF_DIR}/OVMF_CODE.fd",readonly=on \
		-drive if=pflash,format=raw,unit=1,file="${OVMF_DIR}/OVMF_VARS.fd" \
		-nographic 

ovmf:
	qemu-system-x86_64 \
		-m 2G\
		-drive if=pflash,format=raw,unit=0,file="${OVMF_DIR}/OVMF_CODE.fd",readonly=on \
		-drive if=pflash,format=raw,unit=1,file="${OVMF_DIR}/OVMF_VARS.fd" \
		-drive format=raw,file=fat:rw:./ovmf -net none \
		-nographic \

run:
	make init_build
	cp ./edk2/Build/MyAcpiPkg/DEBUG_GCC5/X64/MyAcpi.efi ovmf/esp
	make ovmf
headers:
	make -C kvm/linux-5.15.178 headers

# build_kernel:
# 	make headers
# 	make -C kvm/linux-5.15.178 -j$(nproc) 
	
ctest:
	gcc -static -o ./ctest/$(project) ./ctest/$(project).c
	cp ./ctest/$(project) ./kvm/busybox-1.35.0/_install/bin/
	cd kvm/busybox-1.35.0/_install && \
	find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initramfs.cpio.gz
	make only_kernel 

.PHONY: init_edk only_kernel only_ovmf kernel_and_ovmf server_bios server toy_esp ovmf ctest