/*
 * Copyright (c) 2014, Intel Corporation
 * All rights reserved.
 *
 * Authors: Sylvain Chouleur <sylvain.chouleur@intel.com>
 *          Jeremy Compostella <jeremy.compostella@intel.com>
 *          Jocelyn Falempe <jocelyn.falempe@intel.com>
 *          Andrew Boie <andrew.p.boie@intel.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <lib.h>
#include <vars.h>

#include "uefi_utils.h"
#include "flash.h"
#include "hashes.h"
#include "fastboot.h"
#include "fastboot_ui.h"
#include "gpt.h"

#include "fastboot_oem.h"

#define OFF_MODE_CHARGE		"off-mode-charge"

static void fastboot_oem_publish(void)
{
	fastboot_publish(OFF_MODE_CHARGE, get_current_off_mode_charge() ? "1" : "0");
}


static void cmd_oem_off_mode_charge(__attribute__((__unused__)) INTN argc,
				    CHAR8 **argv)
{
	EFI_STATUS ret;

	if (argc != 2) {
		fastboot_fail("Invalid parameter");
		return;
	}

	if (strcmp(argv[1], (CHAR8* )"1") && strcmp(argv[1], (CHAR8 *)"0")) {
		fastboot_fail("Invalid value");
		error(L"Please specify 1 or 0 to enable/disable charge mode");
		return;
	}

        ret = set_off_mode_charge(!strcmp(argv[1], (CHAR8* )"1"));
	if (EFI_ERROR(ret)) {
		fastboot_fail("Failed to set %a", OFF_MODE_CHARGE);
		return;
	}

	fastboot_oem_publish();
	fastboot_okay("");
}

static void cmd_oem_setvar(INTN argc, CHAR8 **argv)
{
	EFI_STATUS ret;
	CHAR16 *varname;
	CHAR8 *value = NULL;

	if (argc < 2 || argc > 3) {
		fastboot_fail("Invalid parameter");
		return;
	}

	varname = stra_to_str(argv[1]);
	if (argc == 3)
		value = argv[2];

	ret = set_efi_variable(&fastboot_guid, varname,
			       value ? strlen(value) + 1 : 0, value,
			       TRUE, FALSE);
	if (EFI_ERROR(ret))
		fastboot_fail("Unable to %a '%s' variable",
			      value ? "set" : "clear", varname);
	else
		fastboot_okay("");

	FreePool(varname);
}

static void cmd_oem_reboot(INTN argc, CHAR8 **argv)
{
	CHAR16 *target;
	EFI_STATUS ret;

	if (argc != 2) {
		fastboot_fail("Invalid parameter");
		return;
	}

	target = stra_to_str(argv[1]);
	if (!target) {
		fastboot_fail("Unable to convert string");
		return;
	}

	ret = set_efi_variable_str(&loader_guid, LOADER_ENTRY_ONESHOT,
				   TRUE, TRUE, target);
	if (EFI_ERROR(ret)) {
		fastboot_fail("unable to set %a reboot target",
			      target);
		FreePool(target);
		return;
	}

	ui_print(L"Rebooting to %s ...", target);
	FreePool(target);
	fastboot_okay("");
	reboot();
}

static void cmd_oem_garbage_disk(__attribute__((__unused__)) INTN argc,
				 __attribute__((__unused__)) CHAR8 **argv)
{
	EFI_STATUS ret = garbage_disk();

	if (ret == EFI_SUCCESS)
		fastboot_okay("");
	else
		fastboot_fail("Garbage disk failed, %r", ret);
}

static void cmd_oem_gethashes(__attribute__((__unused__)) INTN argc,
			__attribute__((__unused__)) CHAR8 **argv)
{
	get_boot_image_hash(L"boot");
	get_boot_image_hash(L"recovery");
	get_esp_hash();
	get_ext4_hash(L"system");
	fastboot_okay("");
}

void fastboot_oem_init(void)
{
	fastboot_oem_publish();
	fastboot_oem_register(OFF_MODE_CHARGE, cmd_oem_off_mode_charge, FALSE);

	/* The following commands are not part of the Google
	 * requirements.  They are provided for engineering and
	 * provisioning purpose only and those which modify the
	 * device are restricted to the unlocked state.  */
	fastboot_oem_register("setvar", cmd_oem_setvar, TRUE);
	fastboot_oem_register("garbage-disk", cmd_oem_garbage_disk, TRUE);
	fastboot_oem_register("reboot", cmd_oem_reboot, FALSE);
	fastboot_oem_register("get-hashes", cmd_oem_gethashes, FALSE);
}
