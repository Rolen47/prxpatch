/*
 *  MHP3patch user module
 *
 *  Copyright (C) 2010  Codestation
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

#include <pspsdk.h>
#include <pspiofilemgr.h>
#include <string.h>
#include <psppower.h>
#include "sceio.h"
#include "logger.h"

#define DATABIN_PATH "disc0:/PSP_GAME/USRDIR/DATA.BIN"
#define TRANSLATION_PATH "ms0:/MHP3RD_DATA.BIN"
#define SUSPEND_FUNCNAME "power"

// offset and size tables container
SceSize patch_offset[32];
unsigned int patch_size[32];

// number of tables
unsigned int patch_count;

// starting offset of the data
SceSize data_start = 0;

SceUID datafd = -1;
SceUID transfd = -1;

SceUID sema = -1;
SceSize suspend_pos = 0;
int suspending = 0;

SceKernelCallbackFunction power_cb;

void fill_tables() {
    if(transfd < 0)
        return;
    sceIoLseek32(transfd, 0, PSP_SEEK_SET);
    sceIoRead(transfd, &patch_count, 4);
    int i;
    for(i = 0; i < patch_count; i++) {
        sceIoRead(transfd, &patch_offset[i], 4);
        sceIoRead(transfd, &patch_size[i], 4);
    }
    data_start = ((patch_count + 1) * 8);
    if(data_start % 16 > 0)
        data_start += 16 - (data_start % 16);
}

int power_callback(int unknown, int pwrflags, void *common) {
    int ret = power_cb(unknown, pwrflags, common);
    if(pwrflags & PSP_POWER_CB_SUSPENDING || pwrflags & PSP_POWER_CB_POWER_SWITCH) {
        if(!suspending) {
            suspending = 1;
            sceKernelWaitSema(sema, 1, NULL);
            suspend_pos = sceIoLseek32(transfd, 0, PSP_SEEK_CUR);
        }
    }
    if(pwrflags & PSP_POWER_CB_RESUME_COMPLETE) {
        suspending = 0;
        transfd = sceIoOpen(TRANSLATION_PATH, PSP_O_RDONLY, 0777);
        sceIoLseek32(transfd, suspend_pos, PSP_SEEK_SET);
        sceKernelSignalSema(sema, 1);
    }
    return ret;
}

int callback(const char *name, SceKernelCallbackFunction func, void *arg) {
    if(strcmp(name, SUSPEND_FUNCNAME) == 0) {
        power_cb = func;
        func = power_callback;
    }
    return sceKernelCreateCallback(name, func, arg);
}

SceUID open(const char *file, int flags, SceMode mode) {
    SceUID fd = sceIoOpen(file, flags, mode);
    if(fd >= 0 && strcmp(file, DATABIN_PATH) == 0) {
        transfd = sceIoOpen(TRANSLATION_PATH, PSP_O_RDONLY, 0777);
        fill_tables();
        datafd = fd;
        sema = sceKernelCreateSema("mhp3patch_suspend", 0, 1, 1, NULL);
    }
    return fd;
}

int read(SceUID fd, void *data, SceSize size) {
    if(fd == datafd) {
        SceSize pos = sceIoLseek32(fd, 0, PSP_SEEK_CUR);
        int i = 0;
        SceSize offset = data_start;
        while(i < patch_count) {
            if(pos < patch_offset[i] + patch_size[i] && pos + size > patch_offset[i]) {
                sceKernelWaitSema(sema, 1, NULL);
                sceIoLseek32(transfd, offset + (pos - patch_offset[i]), PSP_SEEK_SET);
                int res = sceIoRead(transfd, data, size);
                sceIoLseek32(fd, res, PSP_SEEK_CUR);
                sceKernelSignalSema(sema, 1);
                return res;
            }
            offset += patch_size[i];
            ++i;
        }
    }
    return sceIoRead(fd, data, size);
}

int close(SceUID fd) {
    if(fd == datafd) {
		datafd = -1;
		sceKernelDeleteSema(sema);
	}
    return sceIoClose(fd);
}