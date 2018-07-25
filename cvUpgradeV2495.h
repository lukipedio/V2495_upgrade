#ifndef CVUPGRADEV2495_H
#define CVUPGRADEV2495_H

#include <string>

#define VER_MAJ 1
#define VER_MIN 1
#define VER_PATCH 0
#define VER_BUILD 20180719

enum cuhRetCode_t {
	cuhRetCode_Success = 0,
	cuhRetCode_Usage = -1,
	cuhRetCode_Open = -2,
	cuhRetCode_Memory = -3,
	cuhRetCode_Comm = -4,
	cuhRetCode_FileOpen = -5,
	cuhRetCode_InvalidFile = -6,
	cuhRetCode_InvalidRegion = -7,
	cuhRetCode_InvalidController = -8,
	cuhRetCode_ControllerNotPresent = -9,
	cuhRetCode_InvalidFirmware = -10,
	cuhRetCode_Write = -11,
	cuhRetCode_InvalidHeader = -12,
	cuhRetCode_DirCreate = -13,
	cuhRetCode_DirOpen = -14,
	cuhRetCode_InvalidFilename = -15,
	cuhRetCode_Read = -16,
};

enum workMode_t {
	workMode_FWUPDATE
};

#endif
