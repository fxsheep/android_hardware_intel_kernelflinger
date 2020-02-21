/*
 * Copyright (c) 2014, Intel Corporation
 * All rights reserved.
 *
 * Authors: Jeremy Compostella <jeremy.compostella@intel.com>
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
#include <efilib.h>
#include <lib.h>
#include <vars.h>
#include <ui.h>

#include "uefi_utils.h"
#include "fastboot_oem.h"
#include "fastboot_ui.h"
#include "smbios.h"
#include "info.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

static const char *DROID_IMG_NAME = "droid_operation";
static const UINTN SPACE = 20;

/* Image menu. */
static struct res_action {
	const char *img_name;
	ui_image_t *image;
	enum boot_target target;
} menu_actions[] = {
	{ "start",		NULL,	NORMAL_BOOT },
	{ "restartbootloader",	NULL,	FASTBOOT },
	{ "recoverymode",	NULL,	RECOVERY },
	{ "reboot",		NULL,	REBOOT },
	{ "power_off",		NULL,	POWER_OFF }
};

static UINTN margin;
static UINTN swidth, sheight;
static UINTN menu_current;
static UINTN area_x;
static UINTN area_y;

static UINTN fastboot_ui_menu_draw(UINTN x, UINTN y)
{
	ui_textline_t lines[] = {
		{ &COLOR_LIGHTGRAY, "Volume DOWN button to choose boot option", TRUE },
		{ &COLOR_LIGHTGRAY, "Volume UP button to select boot option", TRUE },
		{ NULL, NULL, TRUE }
	};
	ui_font_t *font;
	ui_image_t *image = menu_actions[menu_current].image;

	if (!image)
		return y;

	ui_image_draw(image, x, y);
	y += image->height + SPACE;

	font = ui_font_get("18x32");
	if (!font) {
		efi_perror(EFI_UNSUPPORTED, "Unable to find 18x32 font");
		return y;
	}

	ui_textarea_display_text(lines, font, x, &y);

	return y;
}

static EFI_STATUS fastboot_ui_clear_dynamic_part(void)
{
	return ui_clear_area(area_x, area_y,
			     swidth - area_x,
			     sheight - area_y - margin);
}

static void fastboot_ui_info_product_name(ui_textline_t *line)
{
	line->str = info_product();
}

static void fastboot_ui_info_variant(ui_textline_t *line)
{
	line->str = info_variant();
}

static void fastboot_ui_info_hw_version(ui_textline_t *line)
{
	line->str = SMBIOS_GET_STRING(1, Version);
}

static void fastboot_ui_info_bootloader_version(ui_textline_t *line)
{
	line->str = info_bootloader_version();
}

static void fastboot_ui_info_ifwi_version(ui_textline_t *line)
{
	line->str = SMBIOS_GET_STRING(0, BiosVersion);
}

static void fastboot_ui_info_serial_number(ui_textline_t *line)
{
	line->str = SMBIOS_GET_STRING(1, SerialNumber);
}

static void fastboot_ui_info_signing(ui_textline_t *line)
{
	BOOLEAN state = info_is_production_signing();

	line->str = state ? "PRODUCTION" : "DEVELOPMENT";
}


struct info_text_fun {
	const char *header;
	void (*get_value)(ui_textline_t *textline);
} const INFOS[] = {
	{ "PRODUCT NAME", fastboot_ui_info_product_name },
	{ "VARIANT", fastboot_ui_info_variant },
	{ "HW_VERSION", fastboot_ui_info_hw_version },
	{ "BOOTLOADER VERSION", fastboot_ui_info_bootloader_version },
	{ "IFWI VERSION", fastboot_ui_info_ifwi_version },
	{ "SERIAL NUMBER", fastboot_ui_info_serial_number },
	{ "SIGNING", fastboot_ui_info_signing },
};

static UINTN fastboot_ui_info_draw(UINTN x, UINTN y)
{
	static const UINTN LINE_LEN = 40;
	UINTN i;
	ui_textarea_t *textarea;
	ui_font_t *font;
	char *dst;

	font = ui_font_get("18x32");
	if (!font) {
		efi_perror(EFI_UNSUPPORTED, "Unable to find 18x32 font");
		return y;
	}

	textarea = ui_textarea_create(ARRAY_SIZE(INFOS) + 2, LINE_LEN, font, NULL);
	dst = AllocatePool(LINE_LEN);
	if (!dst)
		return y;

	memcpy(dst, "FASTBOOT MODE", strlen((CHAR8 *)"FASTBOOT MODE") + 1);
	ui_textarea_set_line(textarea, 0, dst, &COLOR_RED, TRUE);
	ui_textarea_set_line(textarea, 1, NULL, NULL, FALSE);
	for (i = 2; i < textarea->line_nb; i++) {
		char *dst = AllocatePool(LINE_LEN);
		if (!dst) {
			ui_textarea_free(textarea);
			return y;
		}

		ui_textline_t line = { &COLOR_WHITE, NULL, FALSE };
		INFOS[i - 2].get_value(&line);

		snprintf((CHAR8 *)dst, LINE_LEN, (CHAR8 *)"%a - %a",
			 INFOS[i - 2].header, line.str);
		ui_textarea_set_line(textarea, i, dst, line.color, line.bold);
	}

	ui_textarea_draw(textarea, x, y);
	ui_textarea_free(textarea);

	return y + textarea->height;
}


static EFI_STATUS fastboot_ui_menu_load(void)
{
	UINTN i;

	for (i = 0; i < ARRAY_SIZE(menu_actions) ; i++) {
		menu_actions[i].image = ui_image_get(menu_actions[i].img_name);
		if (!menu_actions[i].image)
			return EFI_OUT_OF_RESOURCES;
	}

	return EFI_SUCCESS;
}

void fastboot_ui_refresh(void)
{
	UINTN y = area_y;

	fastboot_ui_clear_dynamic_part();
	y = fastboot_ui_menu_draw(area_x, y);
	fastboot_ui_info_draw(area_x, y + 20);
}

EFI_STATUS fastboot_ui_init(void)
{
	static ui_image_t *droid;
	UINTN width, height, x, y;
	EFI_STATUS ret = EFI_SUCCESS;

	ret = ui_init(&swidth, &sheight, FALSE);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, "Init screen failed");
		return ret;
	}

	ui_clear_screen();

	margin = swidth * 10 / 100;
	ret = EFI_UNSUPPORTED;

	droid = ui_image_get(DROID_IMG_NAME);
	if (!droid) {
		efi_perror(EFI_OUT_OF_RESOURCES,
			   "Unable to load '%a' image",
			   DROID_IMG_NAME);
		return EFI_OUT_OF_RESOURCES;
	}

	if (swidth > sheight) {	/* Landscape orientation. */
		width = (swidth / 2) - (2 * margin);
		height = droid->height * width / droid->width;
		x = margin;
		y = (sheight / 2) - (height / 2);
	} else {		/* Portrait orientation. */
		height = sheight / 3;
		width = droid->width * height / droid->height;
		x = (swidth / 2) - (width / 2);
		y = margin;
	}

	ret = ui_image_draw_scale(droid, x, y, width, height);
	if (EFI_ERROR(ret))
		return ret;

	if (swidth > sheight) {	/* Landscape orientation. */
		area_x = swidth / 2 + margin;
		area_y = y;
	} else {		/* Portrait orientation. */
		area_x = x;
		area_y = sheight / 2;
	}

	ret = fastboot_ui_menu_load();
	if (EFI_ERROR(ret)) {
		efi_perror(ret, "Failed to build menu");
		return ret;
	}

	fastboot_ui_refresh();

	uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);

	return ret;
}

enum boot_target fastboot_ui_event_handler()
{
	switch (ui_read_input()) {
	case EV_UP:
		return menu_actions[menu_current].target;
	case EV_DOWN:
		menu_current = (menu_current + 1) % ARRAY_SIZE(menu_actions);
		fastboot_ui_menu_draw(area_x, area_y);
	default:
		break;
	}

	return UNKNOWN_TARGET;
}

void fastboot_ui_destroy(void)
{
	ui_print_clear();
	ui_clear_screen();
	ui_default_screen();
}
