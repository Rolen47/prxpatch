/*
 *  MHP3patch kernel module: data install patcher
 *
 *  Copyright (C) 2011  Codestation
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "data_install.h"
#include "sceio.h"
#include "misc.h"
#include "logger.h"

#include <pspiofilemgr.h>
#include <pspthreadman.h>
#include <string.h>

#define MAX_INSTALL_FILES 256
#define BMP_SIZE 391734

int install_enabled = 0;

unsigned int install_count = 0;

unsigned int install_id[MAX_INSTALL_FILES];

//position where it starts a new offset group
unsigned int install_pos[MAX_INSTALL_FILES];

// data install fd's
SceUID install_fd[MAX_INSTALL_FILES];

SceSize *install_offset;

void fill_install_tables(SceUID fd) {
    if(fd < 0)
        return;
    SceSize calculated_length = data_start;
    unsigned int i;
    for(i = 0; i < patch_count;i++) {
        calculated_length += patch_size[i];
    }
    SceOff actual_length = sceIoLseek(fd, 0, PSP_SEEK_END);
    if((calculated_length + BMP_SIZE) < actual_length) {
        install_enabled = 1;
        sceIoLseek(fd, calculated_length, PSP_SEEK_SET);
        sceIoRead(fd, &install_count, 4);
        if(install_count > MAX_INSTALL_FILES)
            install_count = MAX_INSTALL_FILES;
        for(i = 0; i < install_count; i++) {
            sceIoRead(fd, &install_id[i], 4);
            sceIoRead(fd, &install_pos[i], 4);
            install_fd[i] = -1;
        }
        // initialize install_offset with our 3rd table address
        install_offset = &patch_size[patch_count];
        for(i = 0; i < patch_count; i++) {
            sceIoRead(fd, &install_offset[i], 4);
        }
    }
}

void register_install(const char *file, SceUID fd) {
    if(!install_enabled)
        return;
    // ms0:\.\PSP\SAVEDATA\ULJM05800DAT\000000XX  | length: 41
    // if(strstr(file,"ULJM05800DAT")) {  //idk why strstr isn't working here :/
    if(strlen(file) == 41) {
        unsigned int i = 0;
        while(i < install_count) {
            const char *offset = file + strlen(file) - 4;
            if(!memcmp(offset, &install_id[i], 4)) {
                install_fd[i] = fd;
                break;
            }
            i++;
        }
    }
}

void unregister_install(SceUID fd) {
    if(!install_enabled)
        return;
    unsigned int i = 0;
    while(i < install_count) {
        if(install_fd[i] == fd) {
            install_fd[i] = -1;
            break;
        }
        i++;
    }
}

int read_install(SceUID fd, void *data, SceSize size) {
	int seeked = 0;
	SceOff pos = 0;
    if(install_enabled) {
	    unsigned int i = 0;
    	while(i < install_count) {
        	if(install_fd[i] == fd) {
				if(!seeked) {
	            	pos = sceIoLseek(fd, 0, PSP_SEEK_CUR);
					seeked = 1;
				}
    	        SceSize offset = data_start;
        	    unsigned int j = install_pos[i];
            	unsigned int l = i < (install_count - 1) ? install_pos[i+1] : patch_count;
	            while(j < l) {
    	            if(install_offset[j] != 0xFFFFFFFF && pos < install_offset[j] + patch_size[j] && pos + size > install_offset[j]) {
        	            unsigned int k;
            	        for(k = 0; k < j; k++)
	                        offset += patch_size[k];
            	        u32 k1 = pspSdkSetK1(0);
        	            reopen_translation();
	                    sceIoLseek(transfd, offset + (pos - install_offset[j]), PSP_SEEK_SET);
    	                int res = sceIoRead(transfd, data, size);
    	                if(res != (int)size) {
    	                    kprintf("Failed to read data install\n");
    	                }
    	                pspSdkSetK1(k1);
            	        sceIoLseek(fd, size, PSP_SEEK_CUR);
    	                return res;
        	        }
            	    j++;
	            }
    	    }
        	i++;
	    }
	}
    return sceIoRead(fd, data, size);
}
