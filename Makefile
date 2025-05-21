# Executable name
SHELL := /bin/bash

HM_DIR = /run/media/grace/archlinux_data/OS/hm
# OVMF_DIR = ./edk2/Build/OvmfX64/DEBUG_GCC5/FV
# KERNEL = ./kvm/linux-5.15.178/arch/x86/boot/bzImage
# INITRAMFS = ./kvm/busybox-1.35.0/initramfs.cpio.gz

# HM_DIR = .
QCOW2 = ./disk.qcow2
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
		-m 4G\
		-kernel "${KERNEL}" \
		-initrd "${INITRAMFS}" \
		-nographic \
		-append "init=/init console=ttyS0"\
		-enable-kvm \
		-smp 4 \
		-hda "${QCOW2}" 
		# -drive file=${QCOW2},format=qcow2,if=virtio
		# -s -S

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

build_kernel:
	make -C kvm/linux-5.15.178 -j$(nproc) 
# 	make headers
	
ctest:
	# g++ -static -o ./ctest/$(project) ./ctest/$(project).cpp
	# gcc -static -o ./ctest/$(project) ./ctest/$(project).c
	# g++ -static -o ./ctest/$(project) ./ctest/$(project).cpp
	# g++ -ldl -Wall -I ./kvm/linux-5.15.178/usr/include  -o ./ctest/$(project) ./ctest/$(project).cpp
	gcc -static -ldl -Wall -o ./ctest/$(project) ./ctest/$(project).c
	cp ./ctest/$(project) ./kvm/busybox-1.35.0/_install/bin/
	cd kvm/busybox-1.35.0/_install && \
	find . -print0 |	 cpio --null -ov --format=newc | gzip -9 > ../initramfs.cpio.gz
	make only_kernel 

start_uefi:
	qemu-system-x86_64 \
		-m 4G \
		-enable-kvm \
		-drive if=pflash,format=raw,readonly=on,file="${OVMF_DIR}/OVMF_CODE.fd" \
		-drive if=pflash,format=raw,file="${OVMF_DIR}/OVMF_VARS.fd" \
		-drive file=fat:rw:./uefi,format=raw,if=ide,index=0 \
		-nographic \
		-drive file=shared_disk.qcow2,format=qcow2,if=ide



		# -virtfs local,path=/run/media/grace/archlinux_data/OS/hm/qemu_shared,mount_tag=hostshare,security_model=mapped-file,id=host0


		# -device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare \
    	# -fsdev local,security_model=none,id=fs0,path=/run/media/grace/archlinux_data/OS/hm/qemu_shared





		# -no-reboot

initramfs_and_copy:
	cd kvm/busybox-1.35.0/_install && find . -print0 |	 cpio --null -ov --format=newc | gzip -9 > ../initramfs.cpio.gz
	cp ${INITRAMFS} ./uefi

copy_kernel2uefi:
	cp ${KERNEL} ./uefi

copy_myuefi:
	cp ./edk2/Build/MyAcpiPkg/DEBUG_GCC5/X64/MyAcpi.efi ./uefi

.PHONY: init_edk only_kernel only_ovmf kernel_and_ovmf server_bios server toy_esp ovmf ctest