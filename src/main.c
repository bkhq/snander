/*
 * Copyright (C) 2018-2022 McMCC <mcmcc@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#include "flashcmd_api.h"
#include "spi_controller.h"
#include "spi_nand_flash.h"

struct flash_cmd prog;
extern unsigned int bsize;
const struct spi_controller *spi_controller;

#define _VER "1.8.1"

void title(void)
{
	printf("SNANDer - Spi Nor/nAND programmER " _VER " by McMCC\n\n");
}

void usage(void)
{
	const char use[] =
		"  Usage:\n"\
		" -h             display this message\n"\
		" -p <name>      select programmer device (mstar, ch341a)\n"\
		" -s             enable i2c fast speed mode, 400 kHz instead of 100 kHz (for mstar programmer)\n"\
		" -q             query connected i2c devices, can be used on its own (for mstar programmer)\n"\
		" -d             disable internal ECC (use read and write page size + OOB size)\n"\
		" -o <bytes>     manual set OOB size with disable internal ECC (default 0)\n"\
		" -I             ECC ignore errors (for read test only)\n"\
		" -k             Skip BAD pages, try read or write in to next page\n"\
		" -L             print list support chips\n"\
		" -i             read the chip ID info\n"\
		" -e             erase, needs -l (whole chip: -l <flash size>); use with -a [-l]\n"\
		" -l <bytes>     set length (-w defaults to the file size, -r to the end of the flash)\n"\
		" -a <address>   manually set address\n"\
		" -w <filename>  write chip with data from filename\n"\
		" -r <filename>  read chip and save data to filename\n"\
		" -v             verify after write on chip (compares every byte)\n";
	printf(use);
	exit(0);
}

int main(int argc, char* argv[])
{
	int c, vr = 0, svr = 0, ret = 0;
	char *str, *fname = NULL, op = 0;
	unsigned char *buf;
	int long long len = 0, addr = 0, flen = 0, wlen = 0, fsz = 0;
	char *programmer = CH341A_I2C_DEVICE;
	char options[128] = "";

	FILE *fp;
	title();

	while ((c = getopt(argc, argv, "diIhveLkqsl:a:w:r:o:p:")) != -1)
	{
		switch(c)
		{
			case 'q':
				strcat(options, "query");
				break;
			case 's':
				strcat(options, "speed");
				break;
			case 'p':
				programmer = strdup(optarg);
				break;
			case 'I':
				ECC_ignore = 1;
				break;
			case 'k':
				Skip_BAD_page = 1;
				break;
			case 'd':
				ECC_fcheck = 0;
				_ondie_ecc_flag = 0;
				break;
			case 'l':
				str = strdup(optarg);
				len = strtoll(str, NULL, *str && *(str + 1) == 'x' ? 16 : 10);
				break;
			case 'o':
				str = strdup(optarg);
				OOB_size = strtoll(str, NULL, *str && *(str + 1) == 'x' ? 16 : 10);
				break;
			case 'a':
				str = strdup(optarg);
				addr = strtoll(str, NULL, *str && *(str + 1) == 'x' ? 16 : 10);
				break;
			case 'v':
				vr = 1;
				break;
			case 'i':
			case 'e':
				if(!op)
					op = c;
				else
					op = 'x';
				break;
			case 'r':
			case 'w':
				if(!op) {
					op = c;
					fname = strdup(optarg);
				} else
					op = 'x';
				break;
			case 'L':
				support_flash_list();
				exit(0);
			case 'h':
			default:
				usage();
		}
	}

	if (op == 0 && strstr(options, "query"))
		op = 'q';

	if (op == 0) usage();

	if (op == 'x' || (ECC_ignore && !ECC_fcheck) || (ECC_ignore && Skip_BAD_page) || (op == 'w' && ECC_ignore)) {
		printf("Conflicting options, only one option at a time.\n\n");
		return -1;
	}

	if (strcmp(programmer, CH341A_I2C_DEVICE) == 0) {
		spi_controller = &ch341a_i2c_ctrl;
	} else if (strcmp(programmer, CH341A_SPI_DEVICE) == 0) {
		spi_controller = &ch341a_spi_ctrl;
	} else {
		printf("Unknown programmer selected!\n");
		return -1;
	}

	if (spi_controller->init(options) < 0) {
		printf("Programmer device not found!\n");
		return -1;
	}

	if (op == 'q')
		goto okout;

	if((flen = flash_cmd_init(&prog)) <= 0)
		goto out;

	if (op == 'i') goto okout;

	if (OOB_size) {
		if (ECC_fcheck == 1) {
			printf("Ignore option -o, use with -d only!\n");
			OOB_size = 0;
		} else {
			if (OOB_size > 256) {
				printf("Error: Maximum set OOB size <= 256!!!\n");
				goto out;
			}
			if (OOB_size < 64) {
				printf("Error: Minimum set OOB size >= 64!!!\n");
				goto out;
			}
			printf("Set manual OOB size = %d.\n", OOB_size);
		}
	}
	if (op == 'e') {
		printf("ERASE:\n");
		if(!len) {
			printf("Refusing to erase without -l. Give the length explicitly (whole chip: -l 0x%llX).\n", flen);
			goto out;
		}
		if(addr + len > flen) {
			printf("Error: addr 0x%llX + len 0x%llX is past the end of the flash 0x%llX\n", addr, len, flen);
			goto out;
		}
		if(len % bsize) {
			printf("Please set len = 0x%016llX multiple of the block size 0x%08X\n", len, bsize);
			goto out;
		}
		printf("Erase addr = 0x%016llX, len = 0x%016llX\n", addr, len);
		ret = prog.flash_erase(addr, len);
		if(!ret){
			printf("Status: OK\n");
			goto okout;
		}
		else
			printf("Status: BAD(%d)\n", ret);
		goto out;
	}

	if ((op == 'r') || (op == 'w')) {
		if (op == 'w') {
			fp = fopen(fname, "rb");
			if (!fp) {
				printf("Couldn't open file %s for reading.\n", fname);
				goto out;
			}
			fseek(fp, 0, SEEK_END);
			fsz = ftell(fp);
			fclose(fp);
			if (!len)
				len = fsz;
			else if (len > fsz) {
				printf("Error: file %s is 0x%llX bytes, shorter than len 0x%llX\n", fname, fsz, len);
				goto out;
			}
			if (addr + len > flen) {
				printf("Error: addr 0x%llX + len 0x%llX is past the end of the flash 0x%llX\n", addr, len, flen);
				goto out;
			}
		}
		else if(addr && !len)
			len = flen - addr;
		else if(!addr && !len) {
			len = flen;
		}
		buf = (unsigned char *)malloc(len + 1);
		if (!buf) {
			printf("Malloc failed for read buffer.\n");
			goto out;
		}
	}

	if (op == 'w') {
		printf("WRITE:\n");
		fp = fopen(fname, "rb");
		if (!fp) {
			printf("Couldn't open file %s for reading.\n", fname);
			free(buf);
			goto out;
		}
		wlen = fread(buf, 1, len, fp);
		if (ferror(fp)) {
			printf("Error reading file [%s]\n", fname);
			if (fp)
				fclose(fp);
			free(buf);
			goto out;
		}
		if(len == flen)
			len = wlen;
		printf("Write addr = 0x%016llX, len = 0x%016llX\n", addr, len);
		ret = prog.flash_write(buf, addr, len);
		if(ret > 0) {
			printf("Status: OK\n");
			if (vr) {
				op = 'r';
				svr = 1;
				printf("VERIFY:\n");
				goto very;
			}
			fclose(fp);
			free(buf);
			goto okout;
		}
		printf("Status: BAD(%d)\n", ret);
		fclose(fp);
		free(buf);
		goto out;
	}

very:
	if (op == 'r') {
		if (!svr) printf("READ:\n");
		else memset(buf, 0, len);
		printf("Read addr = 0x%016llX, len = 0x%016llX\n", addr, len);
		ret = prog.flash_read(buf, addr, len);
		if (ret < 0) {
			printf("Status: BAD(%d)\n", ret);
			free(buf);
			goto out;
		}
		if (svr) {
			unsigned char *fbuf = (unsigned char *)malloc(len + 1);
			int long long i = 0;
			int same = 0;

			if (fbuf) {
				fseek(fp, 0, SEEK_SET);
				if (fread(fbuf, 1, len, fp) == (size_t)len) {
					while (i < len && fbuf[i] == buf[i])
						i++;
					same = (i == len);
				}
				free(fbuf);
			}
			if (same) {
				printf("Status: OK\n");
				fclose(fp);
				free(buf);
				goto okout;
			}
			printf("Status: BAD (first mismatch at offset 0x%llX)\n", i);
			fclose(fp);
			free(buf);
			goto out;
		}
		fp = fopen(fname, "wb");
		if (!fp) {
			printf("Couldn't open file %s for writing.\n", fname);
			free(buf);
			goto out;
		}
		fwrite(buf, 1, len, fp);
		if (ferror(fp)){
			printf("Error writing file [%s]\n", fname);
			fclose(fp);
			free(buf);
			goto out;
		}
		fclose(fp);
		free(buf);
		printf("Status: OK\n");
		goto okout;
	}

out:
	spi_controller->shutdown();
	return -1;

okout:
	spi_controller->shutdown();
	return 0;
}
