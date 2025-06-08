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

edk2_build:
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
		-drive file=$(QCOW2),format=qcow2,if=ide,index=1
		# -hda "${QCOW2}" 
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
	make edk2_build
	cp ./edk2/Build/MyAcpiPkg/DEBUG_GCC5/X64/MyAcpi.efi ovmf/esp
	make ovmf
headers:
	make -C kvm/linux-5.15.178 headers

build_kernel:
	make -C kvm/linux-5.15.178 -j$(nproc) 
# 	make headers

# g++ -static -o ./ctest/$(project) ./ctest/$(project).cpp
# gcc -static -o ./ctest/$(project) ./ctest/$(project).c
# g++ -static -o ./ctest/$(project) ./ctest/$(project).cpp
# g++ -ldl -Wall -I ./kvm/linux-5.15.178/usr/include  -o ./ctest/$(project) ./ctest/$(project).cpp
ctest:
	gcc -static -ldl -Wall -pthread -o ./ctest/$(project) ./ctest/$(project).c
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
	cp ./edk2/Build/MyAddAcpiPkg/DEBUG_GCC5/X64/MyAddAcpiApp.efi ./uefi

qcow2:
	@if [ -f $(QCOW2) ]; then \
        echo "发现现有的磁盘镜像: $(QCOW2)"; \
        read -p "是否删除并重新创建? (y/n): " answer; \
        if [ "$$answer" = "y" ]; then \
            echo "删除现有磁盘镜像..."; \
            rm -f $(QCOW2); \
        else \
            echo "保留现有磁盘镜像，退出操作"; \
            exit 0; \
        fi; \
    fi
	@echo "=== 创建并格式化 QEMU 磁盘镜像 ==="
    # 创建磁盘镜像
	qemu-img create -f qcow2 $(QCOW2) 256M
	@echo "已创建磁盘镜像: $(QCOW2)"
	# 加载 NBD 内核模块
	sudo modprobe nbd max_part=8
	# 连接磁盘镜像到 NBD 设备
	@echo "连接磁盘镜像到 NBD 设备..."
	sudo qemu-nbd --connect=/dev/nbd0 $(QCOW2)
	# 创建分区
	@echo "创建分区..."
	echo -e "n\np\n1\n\n\nw" | sudo fdisk /dev/nbd0
	# 刷新分区表
	@echo "刷新分区表..."
	sudo partprobe /dev/nbd0
	# 格式化为 ext4 文件系统
	@echo "格式化分区为 ext4 文件系统..."
	sudo mkfs.ext4 /dev/nbd0p1
	@echo "断开 NBD 连接..."
	sudo qemu-nbd --disconnect /dev/nbd0
	@echo "=== disk.qcow2 已创建并格式化为 ext4 文件系统 ==="

.PHONY: init_edk only_kernel only_ovmf kernel_and_ovmf server_bios server toy_esp ovmf ctest