/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/vdso.h>

/*
 * Linker script for vDSO.  This is an ELF shared object prelinked to
 * its virtual address, and with only one read-only segment.
 * This script controls its layout.
 */

SECTIONS
{
	/*
	 * User/kernel shared data is before the vDSO.  This may be a little
	 * uglier than putting it after the vDSO, but it avoids issues with
	 * non-allocatable things that dangle past the end of the PT_LOAD
	 * segment.
	 */

	vvar_start = . - 4 * PAGE_SIZE;
	vvar_page  = vvar_start;

	/* Place all vvars at the offsets in asm/vvar.h. */
#define EMIT_VVAR(name, offset) vvar_ ## name = vvar_page + offset;
#include <asm/vvar.h>
#undef EMIT_VVAR

	pvclock_page = vvar_start + PAGE_SIZE;
	hvclock_page = vvar_start + 2 * PAGE_SIZE;
	timens_page  = vvar_start + 3 * PAGE_SIZE;

	/* 添加：为task_struct定义起始页面 */
    /* vtask_page = vvar_start + VVAR_TASK_STRUCT_OFFSET * PAGE_SIZE; */


#undef _ASM_X86_VVAR_H
	/* Place all vvars in timens too at the offsets in asm/vvar.h. */
#define EMIT_VVAR(name, offset) timens_ ## name = timens_page + offset;
#include <asm/vvar.h>
#undef EMIT_VVAR

	. = SIZEOF_HEADERS;

	.hash		: { *(.hash) }			:text
	.gnu.hash	: { *(.gnu.hash) }
	.dynsym		: { *(.dynsym) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }

	.dynamic	: { *(.dynamic) }		:text	:dynamic

	.rodata		: {
		*(.rodata*)
		*(.data*)
		*(.sdata*)
		*(.got.plt) *(.got)
		*(.gnu.linkonce.d.*)
		*(.bss*)
		*(.dynbss*)
		*(.gnu.linkonce.b.*)
	}						:text

	/*
	 * Discard .note.gnu.property sections which are unused and have
	 * different alignment requirement from vDSO note sections.
	 */
	/DISCARD/ : {
		*(.note.gnu.property)
	}
	.note		: { *(.note.*) }		:text	:note

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text


	/*
	 * Text is well-separated from actual data: there's plenty of
	 * stuff that isn't used at runtime in between.
	 */

	.text		: {
		*(.text*)
		*(.fixup)
	}						:text	=0x90909090,



	.altinstructions	: { *(.altinstructions) }	:text
	.altinstr_replacement	: { *(.altinstr_replacement) }	:text

	__ex_table		: { *(__ex_table) }		:text

	/DISCARD/ : {
		*(.discard)
		*(.discard.*)
		*(__bug_table)
	}
}

/*
 * Very old versions of ld do not recognize this name token; use the constant.
 */
#define PT_GNU_EH_FRAME	0x6474e550

/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
 */
PHDRS
{
	text		PT_LOAD		FLAGS(5) FILEHDR PHDRS; /* PF_R|PF_X */
	dynamic		PT_DYNAMIC	FLAGS(4);		/* PF_R */
	note		PT_NOTE		FLAGS(4);		/* PF_R */
	eh_frame_hdr	PT_GNU_EH_FRAME;
}
