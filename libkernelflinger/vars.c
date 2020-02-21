/*
 * Copyright (c) 2014, Intel Corporation
 * All rights reserved.
 *
 * Author: Andrew Boie <andrew.p.boie@intel.com>
 *         Jeremy Compostella <jeremy.compostella@intel.com>
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
#include <efi.h>
#include <efiapi.h>

#include "vars.h"
#include "ui.h"
#include "lib.h"

#define OFF_MODE_CHARGE_VAR	L"off-mode-charge"
#define OEM_LOCK_VAR		L"OEMLock"

#define OEM_LOCK_UNLOCKED	(1 << 0)
#define OEM_LOCK_VERIFIED	(1 << 1)

const EFI_GUID fastboot_guid = { 0x1ac80a82, 0x4f0c, 0x456b,
	{0x9a, 0x99, 0xde, 0xbe, 0xb4, 0x31, 0xfc, 0xc1} };
/* Gummiboot's GUID, we use some of the same variables */
const EFI_GUID loader_guid = { 0x4a67b082, 0x0a4c, 0x41cf,
	{0xb6, 0xc7, 0x44, 0x0b, 0x29, 0xbb, 0x8c, 0x4f} };

/* GUIDs for various interesting Android partitions */
const EFI_GUID boot_ptn_guid = { 0x49a4d17f, 0x93a3, 0x45c1,
	{0xa0, 0xde, 0xf5, 0x0b, 0x2e, 0xbe, 0x25, 0x99 } };
const EFI_GUID recovery_ptn_guid = { 0x4177c722, 0x9e92, 0x4aab,
	{0x86, 0x44, 0x43, 0x50, 0x2b, 0xfd, 0x55, 0x06 } };
const EFI_GUID misc_ptn_guid = { 0xef32a33b, 0xa409, 0x486c,
	{0x91, 0x41, 0x9f, 0xfb, 0x71, 0x1f, 0x62, 0x66 } };

static BOOLEAN provisioning_mode = FALSE;
static CHAR8 current_off_mode_charge[2];

BOOLEAN get_current_off_mode_charge(void)
{
	UINTN size;
	CHAR8 *data;

	if (current_off_mode_charge[0] == '\0') {
		if (EFI_ERROR(get_efi_variable(&fastboot_guid, OFF_MODE_CHARGE_VAR,
					       &size, (VOID **)&data, NULL)))
			return FALSE;

		if (size != sizeof(current_off_mode_charge)
		    || (strcmp(data, (CHAR8 *)"0") && strcmp(data, (CHAR8 *)"1"))) {
			FreePool(data);
			return FALSE;
		}

		memcpy(current_off_mode_charge, data, sizeof(current_off_mode_charge));
		FreePool(data);
	}

	return !strcmp(current_off_mode_charge, (CHAR8 *)"0");
}

enum device_state get_current_state()
{
	return UNLOCKED;
}


EFI_STATUS set_off_mode_charge(BOOLEAN enabled)
{
	CHAR8 *val = (CHAR8 *)(enabled ? "1" : "0");
	EFI_STATUS ret = set_efi_variable(&fastboot_guid, OFF_MODE_CHARGE_VAR,
					  2, val, TRUE, FALSE);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to set %a variable", OFF_MODE_CHARGE_VAR);
		return ret;
	}

	memcpy(current_off_mode_charge, val, 2);
	return EFI_SUCCESS;
}

VOID clear_provisioning_mode(void)
{
	provisioning_mode = FALSE;
}

