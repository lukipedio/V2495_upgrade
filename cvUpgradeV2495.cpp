// firmware_upgrade.cpp : Defines the entry point for the console application.
//
#include "V2495_flash.h"
#include "cvUpgradeV2495.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>

void printVersion(const char *pname) {
	printf("%s version %u.%u.%u - build %u\n",
			pname, VER_MAJ, VER_MIN, VER_PATCH, VER_BUILD);
}

int usage(const char *pname, int retcode) {
	FILE *dest = (retcode == 0) ? stdout : stderr;
	fprintf(dest, "Usage: %s [[-h | -v] | [-f]] [options] <arguments>\n", pname);
	fprintf(dest, "  -h: show this message and exit\n");
	fprintf(dest, "  -v: print version\n");
	fprintf(dest, "  -f: firmware update mode (default)\n");
	fprintf(dest, "FIRMWARE UPDATE MODE ARGUMENTS:\n");
	fprintf(dest, "  <arguments> = <firmware_file>\n\n");
	fprintf(dest, "FLASH UPDATE MODE ARGUMENTS:\n");
	fprintf(dest, "  <arguments> = NULL\n");

	return retcode;
}

int main(int argc, char *argv[])
{
	int32_t ret = cuhRetCode_Success;
	V2495_flash* main_flash = NULL;
	workMode_t wm = workMode_FWUPDATE;
	int32_t index, nargs;
	const char *progname = basename(argv[0]);
	int c;
	bool opt_s = false;
	bool opt_y = false;

	while ((c = getopt (argc, argv, "fhv")) != -1)
	switch (c)
	{
	case 'f':
		wm = workMode_FWUPDATE;
		break;
	case 'h':
		return usage(progname, cuhRetCode_Success);
	case 'v':
		printVersion(progname);
		return 0;
	case 'y':
		opt_y = true;
		break;
	case '?':
		if (isprint (optopt))
			fprintf (stderr, "Unknown option `-%c'.\n", optopt);
		else
			fprintf (stderr,
					"Unknown option character `\\x%x'.\n",
					optopt);
		return usage(progname, cuhRetCode_Usage);
	default:
		return usage(progname, cuhRetCode_Usage);
	}
	
	index = optind;
	nargs = argc - index;
	
	if (wm == workMode_FWUPDATE) {
		char *fwfile;
		
		if (nargs < 1) {
			fprintf(stderr, "Too few arguments for firmware update mode.\n");
			return usage(progname, cuhRetCode_Usage);
		}
		fwfile = argv[index];
		
		try {
			main_flash = new V2495_flash(V2495_flash::MAIN_CONTROLLER_OFFSET); // Main flash controller

			// *************************************
			// Application programming 
			// *************************************
			printf("Upgrading V2495 application firmware image from file %s....\n", fwfile);
			main_flash->program_firmware(V2495_flash::APPLICATION1_FW_REGION, fwfile);
		}
		catch (cuhRetCode_t err) {
			fprintf(stderr, "Firmware upgrade failed with error %d\n", err);
			ret = err;
		}
	}
	
	if (main_flash != NULL)
		delete main_flash;
	
	return ret;
}

