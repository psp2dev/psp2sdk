/*
 * Copyright (C) 2015 PSP2SDK Project
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "elf.h"
#include "rel.h"
#include "scn.h"

Elf32_Rel *findRelByOffset(const scn_t *scn, Elf32_Addr offset,
	const char *strtab)
{
	Elf32_Half i;
	Elf32_Rel *ent;

	if (scn == NULL || scn->content == NULL || strtab == NULL)
		return NULL;

	ent = scn->content;
	for (i = 0; i < scn->shdr.sh_size; i += sizeof(Elf32_Rel)) {
		if (ent->r_offset == offset)
			return ent;

		ent++;
	}

	fprintf(stderr, "%s: Relocation entry for offset 0x%08X not found\n",
		strtab + scn->shdr.sh_name, offset);

	errno = EILSEQ;
	return NULL;
}

int updateRel(FILE *fp, scn_t *scns, const char *strtab,
	scn_t **relScns, Elf32_Half relShnum)
{
	scn_t *dstScn;
	scn_t *scn;
	Elf32_Rel *rel;
	Elf32_Word i, type;
	int res;

	if (fp == NULL || scns == NULL || strtab == NULL || relScns == NULL)
		return EINVAL;

	while (relShnum) {
		scn = *relScns;
		if (scn == NULL)
			return EINVAL;
		if (scn->shdr.sh_type != SHT_REL)
			goto cont;

		res = loadScn(fp, scn, strtab + scn->shdr.sh_name);
		if (res)
			return res;

		dstScn = scns + scn->shdr.sh_info;

		rel = scn->content;
		for (i = 0; i < scn->orgSize; i += sizeof(Elf32_Rel)) {
			rel->r_offset += dstScn->segOffsetDiff;

			type = ELF32_R_TYPE(rel->r_info);
			if (type == R_ARM_ABS32 || type == R_ARM_TARGET1) {
				if (dstScn->content == NULL) {
					res = loadScn(fp, dstScn,
						strtab + dstScn->shdr.sh_name);
					if (res)
						return res;
				}

				*(Elf32_Word *)((uintptr_t)dstScn->content
					+ rel->r_offset - dstScn->segOffset)
						+= dstScn->addrDiff;
			}

			rel++;
		}

cont:
		relScns++;
		relShnum--;
	}

	return 0;
}

int convRelToRela(scn_t *scns, const Elf32_Sym *symtab,
	scn_t **relScns, Elf32_Half relShnum)
{
	Psp2_Rela_Short *buf, *cur;
	scn_t *scn, *dstScn;
	const Elf32_Rel *rel;
	const Elf32_Sym *sym;
	Elf32_Addr addend;
	Elf32_Word i, type;

	if (scns == NULL || symtab == NULL || relScns == NULL)
		return EINVAL;

	while (relShnum) {
		scn = *relScns;

		if (scn->shdr.sh_type != SHT_REL)
			goto cont;

		rel = scn->content;

		buf = malloc(scn->shdr.sh_size);
		cur = buf;

		dstScn = scns + scn->shdr.sh_info;

		for (i = 0; i < scn->orgSize; i += sizeof(rel)) {
			type = ELF32_R_TYPE(rel->r_info);
			sym = symtab + ELF32_R_SYM(rel->r_info);

			PSP2_R_SET_SHORT(cur, 1);
			PSP2_R_SET_SYMSEG(cur, sym->st_shndx == SHN_ABS ?
				15 : scns[sym->st_shndx].phndx);
			PSP2_R_SET_TYPE(cur, type);
			PSP2_R_SET_DATSEG(cur, dstScn->phndx);
			PSP2_R_SET_OFFSET(cur, rel->r_offset);

			if (type == R_ARM_ABS32 || type == R_ARM_TARGET1) {
				if (dstScn->content == NULL)
					return EINVAL;

				addend = *(Elf32_Word *)((uintptr_t)dstScn->content
					+ rel->r_offset - dstScn->segOffset);
			} else
				addend = sym->st_value;

			PSP2_R_SET_ADDEND(cur, addend);

			rel++;
			cur++;
		}

		free(scn->content);

		scn->shdr.sh_type = SHT_PSP2_RELA;
		scn->content = buf;

cont:
		relScns++;
		relShnum--;
	}

	return 0;
}
