ARCH := x86_64
ANDROID_BUILD_TOP := /home/sheep/ubuntu_ext/iwork8/tree

ifeq ($(ARCH),x86_64)
ARCH_DIR := linux-x86_64
LIBGCC := $(shell $(CC) -print-libgcc-file-name)
else
ARCH_DIR := linux-x86
LIBGCC := $(shell $(CC) -m32 -print-libgcc-file-name)
endif

GNU_EFI_TOP := $(ANDROID_BUILD_TOP)/hardware/intel/efi_prebuilts/gnu-efi/$(ARCH_DIR)/
GNU_EFI_INCLUDE := $(GNU_EFI_TOP)/include/efi
GNU_EFI_LIB :=  $(GNU_EFI_TOP)/lib

OPENSSL_TOP := $(ANDROID_BUILD_TOP)/hardware/intel/efi_prebuilts/uefi_shim/
EFI_LIBS := -lefi -lgnuefi --start-group $(OPENSSL_TOP)/$(ARCH_DIR)/libcryptlib.a \
		$(OPENSSL_TOP)/$(ARCH_DIR)/libopenssl.a --end-group \
		$(LIBGCC)

# The key to sign kernelflinger with
DB_KEY_PAIR ?= $(ANDROID_BUILD_TOP)/device/intel/build/testkeys/DB
VENDOR_KEY_PAIR ?= $(ANDROID_BUILD_TOP)/device/intel/build/testkeys/vendor

CPPFLAGS := -DKERNELFLINGER -I$(GNU_EFI_INCLUDE) \
	-I$(GNU_EFI_INCLUDE)/$(ARCH) -I$(OPENSSL_TOP)/include -I$(OPENSSL_TOP)/include/Include \
	-Iinclude/libkernelflinger -Iinclude/libfastboot

CFLAGS := -ggdb -O3 -fno-stack-protector -fno-strict-aliasing -fpic \
	 -fshort-wchar -Wall -Wextra -mno-red-zone -maccumulate-outgoing-args \
	 -mno-mmx -fno-builtin -fno-tree-loop-distribute-patterns

#ifneq ($(INSECURE_LOADER),)
    CFLAGS += -DINSECURE
#endif

# Key pair used to sign & validate keystores
OEM_KEY_PAIR ?= $(ANDROID_BUILD_TOP)/device/intel/build/testkeys/oem

# We'll use the verity key in the build as our testing keystore for signing
# boot images. We'll extract the public key from the PEM private key
VERITY_PRIVATE_KEY := $(ANDROID_BUILD_TOP)/build/target/product/security/verity_private_dev_key

KEYSTORE_SIGNER := $(ANDROID_BUILD_TOP)/out/host/linux-x86/bin/keystore_signer

ifeq ($(ARCH),x86_64)
CFLAGS += -DEFI_FUNCTION_WRAPPER -DGNU_EFI_USE_MS_ABI
else
CFLAGS += -m32
endif

LDFLAGS	:= -nostdlib -znocombreloc -T $(GNU_EFI_LIB)/elf_$(ARCH)_efi.lds \
	-shared -Bsymbolic --warn-common --no-undefined --fatal-warnings \
	-L$(GNU_EFI_LIB) \
	-L$(OPENSSL_TOP)/$(ARCH_DIR) $(GNU_EFI_LIB)/crt0-efi-$(ARCH).o

LIB_OBJS := libkernelflinger/android.o \
	    libkernelflinger/efilinux.o \
	    libkernelflinger/acpi.o \
	    libkernelflinger/lib.o \
	    libkernelflinger/options.o \
	    libkernelflinger/security.o \
	    libkernelflinger/asn1.o \
	    libkernelflinger/keystore.o \
	    libkernelflinger/vars.o \
	    libkernelflinger/ui.o \
	    libkernelflinger/ui_font.o \
	    libkernelflinger/ui_textarea.o \
	    libkernelflinger/ui_image.o

LIBFASTBOOT_OBJS := \
	    libfastboot/fastboot.o \
	    libfastboot/fastboot_oem.o \
	    libfastboot/fastboot_usb.o \
	    libfastboot/fastboot_ui.o \
	    libfastboot/flash.o \
	    libfastboot/gpt.o \
	    libfastboot/sparse.o \
	    libfastboot/uefi_utils.o \
	    libfastboot/smbios.o \
	    libfastboot/info.o \
	    libfastboot/intel_variables.o \
	    libfastboot/oemvars.o \
	    libfastboot/hashes.o

OBJS := kernelflinger.o \
	ux.o

all: kernelflinger.db.efi kernelflinger.vendor.efi kernelflinger.unsigned.efi

kernelflinger.db.efi: kernelflinger.unsigned.efi $(DB_KEY_PAIR).x509.pem kernelflinger.db.key
	sbsign --key kernelflinger.db.key \
		--cert $(DB_KEY_PAIR).x509.pem \
		--output $@ $<

kernelflinger.vendor.efi: kernelflinger.unsigned.efi $(VENDOR_KEY_PAIR).x509.pem kernelflinger.vendor.key
	sbsign --key kernelflinger.vendor.key \
		--cert $(VENDOR_KEY_PAIR).x509.pem \
		--output $@ $<

oem.key: $(OEM_KEY_PAIR).pk8
	openssl pkcs8 -inform DER -nocrypt -in $< -out $@

oem.cer: $(OEM_KEY_PAIR).x509.pem
	openssl x509 -outform der -in $< | dd of=$@ ibs=4096 count=1 conv=sync

# DER formatted public verity key
verity.cer: $(VERITY_PRIVATE_KEY)
	openssl rsa -pubout -inform PEM -outform der -in $< -out $@

keystore.bin: oem.key verity.cer $(KEYSTORE_SIGNER)
	$(KEYSTORE_SIGNER) oem.key $@ verity.cer

keystore.padded.bin: keystore.bin
	dd ibs=32768 if=$< of=$@ count=1 conv=sync

oemkeystore.o: oemkeystore.S keystore.padded.bin oem.cer
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@ -DOEM_KEYSTORE_FILE=\"keystore.padded.bin\" -DOEM_KEY_FILE=\"oem.cer\"

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

kernelflinger.db.key: $(DB_KEY_PAIR).pk8
	openssl pkcs8 -nocrypt -inform DER -outform PEM -in $^ -out $@

kernelflinger.vendor.key: $(VENDOR_KEY_PAIR).pk8
	openssl pkcs8 -nocrypt -inform DER -outform PEM -in $^ -out $@

%.unsigned.efi: %.so
	objcopy -j .text -j .sdata -j .data \
		-j .dynamic -j .dynsym  -j .rel \
		-j .rela -j .reloc -j .eh_frame \
		-j .oemkeys \
		--target=efi-app-$(ARCH) $^ $@

%.debug.efi: %.so
	objcopy -j .text -j .sdata -j .data \
		-j .dynamic -j .dynsym  -j .rel \
		-j .rela -j .reloc -j .eh_frame \
		-j .oemkeys \
		-j .debug_info -j .debug_abbrev -j .debug_aranges \
		-j .debug_line -j .debug_str -j .debug_ranges \
		--target=efi-app-$(ARCH) $^ $@

libkernelflinger/res/font_res.h:
	./libkernelflinger/tools/gen_fonts.sh ./libkernelflinger/res/fonts/ $@

libkernelflinger/res/img_res.h:
	./libkernelflinger/tools/gen_images.sh ./libkernelflinger/res/images/ $@

$(LIB_OBJS): libkernelflinger/res/font_res.h libkernelflinger/res/img_res.h

libkernelflinger.a: $(LIB_OBJS)
	ar rcs $@ $^

libfastboot.a: $(LIBFASTBOOT_OBJS)
	ar rcs $@ $^

kernelflinger.so: $(OBJS) libkernelflinger.a libfastboot.a
	$(LD) $(LDFLAGS) $^ -o $@ -lefi $(EFI_LIBS)

clean:
	rm -f $(OBJS) $(LIB_OBJS) $(LIBFASTBOOT_OBJS) *.a *.cer *.key *.bin *.so *.efi libkernelflinger/res/font_res.h libkernelflinger/res/img_res.h
