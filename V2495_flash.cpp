#include "V2495_flash.h"
#include "CAENComm.h"
#include "cvUpgradeV2495.h"

#include <math.h>
#include <stdlib.h>
#include <fstream>
#include <cstring>
#include <vector>

#ifndef WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <dirent.h>
#endif

V2495_flash::V2495_flash(controller_t controller_offset)
{
	uint32_t idcode;
	int32_t ret;

	handle = -1;
	
	/* Connection to target module 
	**  Please change VME Base address accordingly to your setup:
	**  i.e. for a VME Base address = 0x32100000 call:
	**  CAENComm_OpenDevice(CAENComm_USB, 0, 0, 0x32100000, &handle);
	*/
	ret = CAENComm_OpenDevice(CAENComm_USB, 0, 0, 0, &handle);
	if (ret != CAENComm_Success) {
		fprintf(stderr, "Device open failed with CAENComm error %d.\n", ret);
		throw cuhRetCode_Open;
	}
	
	try {
		switch (controller_offset) {
		case MAIN_CONTROLLER_OFFSET:
			controller_base_address = MAIN_CONTROLLER_OFFSET;
			bitstream_length = MAIN_FIRMWARE_BITSTREAM_LENGTH;
			break;
	    case USER_CONTROLLER_OFFSET:
		    controller_base_address = USER_CONTROLLER_OFFSET;
		    bitstream_length = USER_FIRMWARE_BITSTREAM_LENGTH;
		    break;

		default:
			break;
		}

		bitstream = new uint8_t[bitstream_length];
		if (bitstream == NULL)
			throw cuhRetCode_Memory;

		// If the controller is accessible
		// we must be able to read a unique IDCODE
		ReadRegister(controller_base_address + IDCODE_OFFSET, &idcode);

		if (idcode != 0xCAEF2495)
			_flash_controller_present = 0; // controller not present/mapped
		else
			_flash_controller_present = 1;

		// MUST enable flash access from controller!
		enable_flash_access(); // HACK giusto farlo nel costruttore????

		// Sblocca accesso al controllore flash
		WriteRegister(controller_base_address + UNLOCK_OFFSET, 0xABBA5511);
	}
	catch (cuhRetCode_t err) {
		closeDevice();
		throw err;
	}
}


V2495_flash::~V2495_flash()
{
	// MUST disable flash access from controller!
	disable_flash_access(); // HACK giusto farlo nel distruttore?
	closeDevice();
}

void V2495_flash::get_flash_status(uint32_t * status)
{
	if (!_flash_controller_present)
		throw cuhRetCode_ControllerNotPresent;

	WriteRegister(controller_base_address + OPCODE_OFFSET, READ_STATUS_OPCODE);
	ReadRegister(controller_base_address + OPCODE_OFFSET, status);

	*status >>= 8;
}


void V2495_flash::load_bitstream_from_file(char *filename) {
	printf("Opening %s\n", filename);
	ifstream bitstream_file(filename, ios::in | ios::binary);

	if (!(bitstream_file.is_open())) {
		fprintf(stderr, "Can't open file %s.\n", filename);
		throw cuhRetCode_FileOpen;
	}
	
	if (!(bitstream_file.read((char *)bitstream, bitstream_length))) { // HACK conversione uint8_t * => char *
		printf("Error reading file: different length from expected.\n");
		throw cuhRetCode_InvalidFile;
	}
	
	if (!bitstream) {
		printf("Can't initialize bitstream.\n");
		throw cuhRetCode_InvalidFile;
	}
}

void V2495_flash::get_controller_status(uint32_t * status)
{
	if (!_flash_controller_present)
		throw cuhRetCode_ControllerNotPresent;

	ReadRegister(controller_base_address + OPCODE_OFFSET, status);
}

void V2495_flash::sector_erase(uint32_t start_address)
{
	if (!_flash_controller_present)
		throw cuhRetCode_ControllerNotPresent;

	WriteRegister(controller_base_address + ADDRESS_OFFSET, start_address);
	WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_ENABLE_OPCODE);
	WriteRegister(controller_base_address + OPCODE_OFFSET, SECTOR_ERASE_OPCODE);

	wait_flash();
}

void V2495_flash::page_erase(uint32_t start_address)
{
	uint32_t sector_address;
	uint8_t *buf = new uint8_t[SECTOR_SIZE];
	uint8_t *buf1 = new uint8_t[SECTOR_SIZE];
	uint32_t pageOffset;

	if (!_flash_controller_present)
		throw cuhRetCode_ControllerNotPresent;

	try {
		sector_address = ((uint32_t)(start_address / SECTOR_SIZE)) * SECTOR_SIZE;
		pageOffset = start_address - sector_address;
		read_sector(sector_address, buf);
		sector_erase(sector_address);
		read_sector(sector_address, buf1);
		// write 1 on the selected page in the buffer
		memset(buf + pageOffset, 0xFF, PAGE_SIZE);
		write_sector(sector_address, buf);
	}
	catch (cuhRetCode_t err) {
		if (buf != NULL)
			delete[] buf;
		if (buf1 != NULL)
			delete[] buf1;
		throw;
	}
	if (buf != NULL)
		delete[] buf;
	if (buf1 != NULL)
		delete[] buf1;
}


void V2495_flash::write_page(uint32_t start_address, uint8_t  *buf)
{
	uint32_t addrs[64];
	uint32_t datas[64];

	if (!_flash_controller_present)
		throw cuhRetCode_ControllerNotPresent;

	WriteRegister(controller_base_address + ADDRESS_OFFSET, start_address);
	WriteRegister(controller_base_address + PAYLOAD_OFFSET, 255); // 256 bytes payload

	for (int i = 0; i < 64; ++i) {
		addrs[i] = controller_base_address + BRAM_START_OFFSET + 4 * i;
		datas[i] = *((uint32_t *)buf + i);
	}

	MultiWriteRegister(64, addrs, datas);

	WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_ENABLE_OPCODE);

	WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_PAGE_OPCODE);

	wait_flash();
}


void V2495_flash::read_page(uint32_t start_address, uint8_t*  buf)
{
	uint32_t addrs[64];
	uint32_t datas[64];

	if (!_flash_controller_present)
		throw cuhRetCode_ControllerNotPresent;

	WriteRegister(controller_base_address + ADDRESS_OFFSET, start_address);
	WriteRegister(controller_base_address + PAYLOAD_OFFSET, PAGE_SIZE - 1); // 256 bytes payload


	WriteRegister(controller_base_address + OPCODE_OFFSET, READ_PAGE_OPCODE);

	wait_controller();

	// HACK implementare con MultiRead32/BLT
	//for (int i = 0; i < 64; ++i)
	//	ReadRegister(controller_base_address + BRAM_START_OFFSET + 4 * i, (uint32_t *)buf + i);

	for (int i = 0; i < 64; ++i) {
		addrs[i] = controller_base_address + BRAM_START_OFFSET + 4 * i;
	}

	MultiReadRegister(64, addrs, datas);

	std::memcpy(buf, datas, 64 * sizeof(uint32_t));
}

void V2495_flash::read_sector(uint32_t start_address, uint8_t*  buf) {
	uint32_t sector_address = ((uint32_t)(start_address / SECTOR_SIZE)) * SECTOR_SIZE;
	int32_t pagesPerSector = SECTOR_SIZE / PAGE_SIZE, i;

	for (i = 0; i < pagesPerSector; i++)
		read_page(sector_address + i * PAGE_SIZE, buf + i * PAGE_SIZE);
}

void V2495_flash::write_sector(uint32_t start_address, uint8_t*  buf) {
	uint32_t sector_address = ((uint32_t)(start_address / SECTOR_SIZE)) * SECTOR_SIZE;
	int32_t pagesPerSector = SECTOR_SIZE / PAGE_SIZE, i;

	for (i = 0; i < pagesPerSector; i++)
		write_page(sector_address + i * PAGE_SIZE, buf + i * PAGE_SIZE);
}


void V2495_flash::set_flash_status(uint8_t status)
{
	if (!_flash_controller_present)
		throw cuhRetCode_ControllerNotPresent;

	WriteRegister(controller_base_address + ADDRESS_OFFSET, (uint32_t)status);
	WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_STATUS_OPCODE);
}



void V2495_flash::wait_controller()
{
	uint32_t data;


	while (1) { // TODO potential deadlock!!! => timeout

		// Attende che il controllore flash sia pronto ad accettare un nuovo comando
		ReadRegister(controller_base_address + OPCODE_OFFSET, &data);
		if ((data & 0xFE) == 0) 
			break;
	}
}


void V2495_flash::wait_flash()
{
	uint32_t data;

	while (1) { //  TODO potential deadlock!!! => timeout

		// Attende che il controllore della flash abbia terminato una eventuale 
		// operazione in corso.
		wait_controller();

		// Rilegge  registro di status della flash per verificare che
		// abbia finito l'operazione di scrittura.
		WriteRegister(controller_base_address + OPCODE_OFFSET, READ_STATUS_OPCODE);
		ReadRegister(controller_base_address + OPCODE_OFFSET, &data);
		if (((data >> 8) & 1) == 0)
			return;
	}

	return;
}

uint8_t  inline V2495_flash::rev_byte(uint8_t v) {
	uint8_t t = v;
	for (int i = sizeof(v) * 8 - 1; i; i--)
	{
		t <<= 1;
		v >>= 1;
		t |= v & 1;
	}
	return t;
}


void V2495_flash::program_firmware(fw_region_t region, char *filename, int verify, int no_bit_reverse, int skip_erase) {

	uint8_t * buf;
	uint8_t * buf_ver;
	int sectors_to_write;
	int bytes_to_write;
	uint32_t start_address;
	
	buf = new uint8_t[PAGE_SIZE];
	buf_ver = new uint8_t[PAGE_SIZE];
	
	load_bitstream_from_file(filename);

	switch (controller_base_address) {
	case MAIN_CONTROLLER_OFFSET:
		sectors_to_write = MAIN_FIRMWARE_SECTORS;

		switch (region) {
		case BOOT_FW_REGION:
			start_address = MAIN_FACTORY_START_ADDRESS;
			break;
		case APPLICATION1_FW_REGION:
			start_address = MAIN_APPLICATION_START_ADDRESS;
			break;
		case APPLICATION2_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION3_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION4_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION5_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		default:
			throw cuhRetCode_InvalidRegion;
			break;
		}

		break;

	case USER_CONTROLLER_OFFSET:
		sectors_to_write = USER_FIRMWARE_SECTORS;

		switch (region) {
		case BOOT_FW_REGION:
			start_address = USER_FACTORY_START_ADDRESS;
			break;
		case APPLICATION1_FW_REGION:
			start_address = USER_APPLICATION1_START_ADDRESS;
			break;
		case APPLICATION2_FW_REGION:
			start_address = USER_APPLICATION2_START_ADDRESS;
			break;
		case APPLICATION3_FW_REGION:
			start_address = USER_APPLICATION3_START_ADDRESS;
			break;
		case APPLICATION4_FW_REGION:
			start_address = USER_APPLICATION4_START_ADDRESS;
			break;
		case APPLICATION5_FW_REGION:
			start_address = USER_APPLICATION5_START_ADDRESS;
			break;
		default:
			break;
		}

		break;

	default:
		throw cuhRetCode_InvalidController;
		break;
	}

	// Se si deve aggiornare l'iimagine di boot bisogna
	// sproteggere i settori dedicati al firmware FACTORY (BOOT)
	if (region == BOOT_FW_REGION)
		write_unprotect();

	// Cancella i settori a partire da quello pi� basso,
	// in modo da lasciare "corrotta" la flash in caso di interruzione prematura
	// della cancellazione.
	if (!skip_erase)
		// Erase sectors
		for (int i = 0; i < sectors_to_write; ++i) {
			sector_erase(start_address + i * SECTOR_SIZE);
                        printf("Erasing sector %i.\n",i);
		}

	// Programma le pagine di ciascun settore
	// Programma i settori a partire da quello pi� alto
	// in modo da lasciare "corrotta" la flash in caso di interruzione prematura
	// della programmazione.
	for (int sector = sectors_to_write - 1; sector >= 0; --sector){
                printf("Writing sector %i.\n",sector);
		for (int page = SECTOR_SIZE / PAGE_SIZE - 1; page >= 0; --page) {
			int offset = sector * SECTOR_SIZE + page * PAGE_SIZE;

			uint32_t remain = (bitstream_length - offset) > 0 ? bitstream_length - offset : 0;

			if (remain == 0)
				continue;

			bytes_to_write = (remain < PAGE_SIZE) ? remain : PAGE_SIZE;

			memset(buf, 0, PAGE_SIZE);

			// Get next data chunk in bitstream buffer
			memcpy(buf, bitstream + offset, bytes_to_write);

			if (!no_bit_reverse) {
				// Bit reversal
				for (int k = 0; k < bytes_to_write; ++k)
					buf[k] = rev_byte(buf[k]);
			}

			// Write buffer into flash page
			write_page(start_address + offset, buf);

			if (verify) {
				read_page(start_address + offset, buf_ver);
				for (int ii = 0; ii < bytes_to_write; ii++) {
					if (buf_ver[ii] != buf[ii])
						throw cuhRetCode_InvalidFirmware;
				}
			}
		}
	}

	// Nel caso di programmazione del boot
	// al termine si proteggono nuovamente i suoi settori
	if (region == BOOT_FW_REGION)
		write_protect();

}


void V2495_flash::verify_firmware(fw_region_t region, char *filename, int no_bit_reverse) {

	load_bitstream_from_file(filename);

	uint8_t * buf;
	uint8_t * buf_ver;
	buf = new uint8_t[PAGE_SIZE];
	buf_ver = new uint8_t[PAGE_SIZE];

	int sectors_to_read;
	int bytes_to_read;

	uint32_t start_address;

	switch (controller_base_address) {
	case MAIN_CONTROLLER_OFFSET:
		sectors_to_read = MAIN_FIRMWARE_SECTORS;

		switch (region) {
		case BOOT_FW_REGION:
			start_address = MAIN_FACTORY_START_ADDRESS;
			break;
		case APPLICATION1_FW_REGION:
			start_address = MAIN_APPLICATION_START_ADDRESS;
			break;
		case APPLICATION2_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION3_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION4_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION5_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		default:
			throw cuhRetCode_InvalidRegion;
			break;
		}

		break;
	case USER_CONTROLLER_OFFSET:
		sectors_to_read = USER_FIRMWARE_SECTORS;

		switch (region) {
		case BOOT_FW_REGION:
			start_address = USER_FACTORY_START_ADDRESS;
			break;
		case APPLICATION1_FW_REGION:
			start_address = USER_APPLICATION1_START_ADDRESS;
			break;
		case APPLICATION2_FW_REGION:
			start_address = USER_APPLICATION2_START_ADDRESS;
			break;
		case APPLICATION3_FW_REGION:
			start_address = USER_APPLICATION3_START_ADDRESS;
			break;
		case APPLICATION4_FW_REGION:
			start_address = USER_APPLICATION4_START_ADDRESS;
			break;
		case APPLICATION5_FW_REGION:
			start_address = USER_APPLICATION5_START_ADDRESS;
			break;
		default:
			break;
		}

		break;

	default:
		throw cuhRetCode_InvalidRegion;
		break;
	}


	// Programma le pagine di ciascun settore
	// Programma i settori a partire da quello pi� alto
	for (int sector = 0; sector < sectors_to_read; ++sector){
		for (uint32_t page = 0; page < SECTOR_SIZE / PAGE_SIZE; ++page) {
			int offset = sector * SECTOR_SIZE + page * PAGE_SIZE;

			uint32_t remain = (bitstream_length - offset) > 0 ? bitstream_length - offset : 0;

			if (remain == 0)
				continue;

			bytes_to_read = (remain < PAGE_SIZE) ? remain : PAGE_SIZE;

			// Point to next data chunk in bitstream buffer
			//buf = bitstream + i * 64 * 1024 + j * 256;
			// Get next data chunk in bitstream buffer
			memcpy(buf, bitstream + offset, bytes_to_read);


			if (!no_bit_reverse) {
				// Bit reversal
				for (int k = 0; k < bytes_to_read; ++k)
					buf[k] = rev_byte(buf[k]);
			}

			read_page(start_address + offset, buf_ver);
			for (int ii = 0; ii < bytes_to_read; ii++) {
				if (buf_ver[ii] != buf[ii])
					throw cuhRetCode_InvalidFirmware;
			}
		}
	}
}

void V2495_flash::erase_firmware(fw_region_t region) {

	int sectors_to_erase;
	uint32_t start_address;

	switch (controller_base_address) {
	case MAIN_CONTROLLER_OFFSET:
		sectors_to_erase = MAIN_FIRMWARE_SECTORS;
		switch (region) {
		case BOOT_FW_REGION:
			start_address = MAIN_FACTORY_START_ADDRESS;
			break;
		case APPLICATION1_FW_REGION:
			start_address = MAIN_APPLICATION_START_ADDRESS;
			break;
		case APPLICATION2_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION3_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION4_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION5_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		default:
			throw cuhRetCode_InvalidRegion;
			break;
		}
		break;
	case USER_CONTROLLER_OFFSET:
		sectors_to_erase = USER_FIRMWARE_SECTORS;

		switch (region) {
		case BOOT_FW_REGION:
			start_address = USER_FACTORY_START_ADDRESS;
			break;
		case APPLICATION1_FW_REGION:
			start_address = USER_APPLICATION1_START_ADDRESS;
			break;
		case APPLICATION2_FW_REGION:
			start_address = USER_APPLICATION2_START_ADDRESS;
			break;
		case APPLICATION3_FW_REGION:
			start_address = USER_APPLICATION3_START_ADDRESS;
			break;
		case APPLICATION4_FW_REGION:
			start_address = USER_APPLICATION4_START_ADDRESS;
			break;
		case APPLICATION5_FW_REGION:
			start_address = USER_APPLICATION5_START_ADDRESS;
			break;
		default:
			break;
		}

		break;

	default:
		throw cuhRetCode_InvalidRegion;
		break;
	}


	// Se si deve aggiornare l'iimagine di boot bisogna
	// sproteggere i settori dedicati al firmware FACTORY (BOOT)
	if (region == BOOT_FW_REGION)
		write_unprotect();

	// Erase sectors
	for (int i = 0; i < sectors_to_erase; ++i) {
		sector_erase(start_address + i * SECTOR_SIZE);
	}

	// Nel caso di programmazione del boot
	// al termine si proteggono nuovamente i suoi settori
	if (region == BOOT_FW_REGION)
		write_protect();
}

void V2495_flash::dump_firmware(fw_region_t region, char *filename, int no_bit_reverse) {

	// TODO
/*
	uint32_t start_address;
	uint32_t sectors_to_dump;

	switch (controller_base_address) {
	case MAIN_CONTROLLER_OFFSET:
		sectors_to_dump = MAIN_FIRMWARE_SECTORS;

		switch (region) {
		case BOOT_FW_REGION:
			start_address = MAIN_FACTORY_START_ADDRESS;
			break;
		case APPLICATION1_FW_REGION:
			start_address = MAIN_APPLICATION_START_ADDRESS;
			break;
		case APPLICATION2_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION3_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION4_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		case APPLICATION5_FW_REGION:
			throw cuhRetCode_InvalidRegion;
			break;
		default:
			throw cuhRetCode_InvalidRegion;
			break;
		}
		break;
	case USER_CONTROLLER_OFFSET:
		sectors_to_dump = USER_FIRMWARE_SECTORS;
		switch (region) {
		case BOOT_FW_REGION:
			start_address = USER_FACTORY_START_ADDRESS;
			break;
		case APPLICATION1_FW_REGION:
			start_address = USER_APPLICATION1_START_ADDRESS;
			break;
		case APPLICATION2_FW_REGION:
			start_address = USER_APPLICATION2_START_ADDRESS;
			break;
		case APPLICATION3_FW_REGION:
			start_address = USER_APPLICATION3_START_ADDRESS;
			break;
		case APPLICATION4_FW_REGION:
			start_address = USER_APPLICATION4_START_ADDRESS;
			break;
		case APPLICATION5_FW_REGION:
			start_address = USER_APPLICATION5_START_ADDRESS;
			break;
		default:
			break;
		}
		break;
	default:
		throw cuhRetCode_InvalidRegion;
		break;
	}*/
}


void V2495_flash::write_protect() {

	uint32_t data;
	uint32_t region;


	switch (controller_base_address) {
	case MAIN_CONTROLLER_OFFSET:
		region = PROTECT_SECTORS_0_63 << 2;
		WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_ENABLE_OPCODE);
		WriteRegister(controller_base_address + ADDRESS_OFFSET, region);
		WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_STATUS_OPCODE);
		wait_flash();
		break;
	case USER_CONTROLLER_OFFSET:
		region = PROTECT_SECTORS_0_127 << 2;
		WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_ENABLE_OPCODE);
		WriteRegister(controller_base_address + ADDRESS_OFFSET, region);
		WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_STATUS_OPCODE);
		wait_flash();
		break;
	default:
		// TODO
		break;
	}

	WriteRegister(controller_base_address + OPCODE_OFFSET, READ_STATUS_OPCODE);
	ReadRegister(controller_base_address + OPCODE_OFFSET, &data);

	if (((data >> 8) & 0xFC) != region)
		throw cuhRetCode_Write;
}

void V2495_flash::write_unprotect() {

	uint32_t region;
	uint32_t data;


	region = UNPROTECT_ALL << 2;
	WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_ENABLE_OPCODE);
	WriteRegister(controller_base_address + ADDRESS_OFFSET, region);
	WriteRegister(controller_base_address + OPCODE_OFFSET, WRITE_STATUS_OPCODE);
	wait_flash();


	WriteRegister(controller_base_address + OPCODE_OFFSET, READ_STATUS_OPCODE);
	ReadRegister(controller_base_address + OPCODE_OFFSET, &data);

	if (((data >> 8) & 0xFC) != region)
		throw cuhRetCode_Write;
}

void V2495_flash::get_protection_status(uint32_t& status) {
	uint32_t data;


	switch (controller_base_address) {
	case MAIN_CONTROLLER_OFFSET|USER_CONTROLLER_OFFSET:
		WriteRegister(controller_base_address + OPCODE_OFFSET, READ_STATUS_OPCODE);
		ReadRegister(controller_base_address + OPCODE_OFFSET, &data);
		status = data >> 10;
		break;
	default:
		break;
	}
}

void V2495_flash::enable_flash_access() {
	// Sconfigura FPGA
	// Evita che la flash sia inaccessibile perch� FPGA User non programmata o pin in conflitto
	WriteRegister(controller_base_address + FPGA_ACCESS_OFFSET, 0);

	// enable flash access (remove tristate)
	WriteRegister(controller_base_address + FLASH_ACCESS_OFFSET, 1);
}

void V2495_flash::disable_flash_access() {
	// disable flash access (tristate)
	WriteRegister(controller_base_address + FLASH_ACCESS_OFFSET, 0);

	// restart fpga 
	WriteRegister(controller_base_address + FPGA_ACCESS_OFFSET, 1);
}

void V2495_flash::WriteRegister(uint32_t address, uint32_t data) {
	int32_t ret;
	if ((ret = CAENComm_Write32(handle, address, data)) != CAENComm_Success) {
		fprintf(stderr, "WriteRegister(0x%X, 0x%X) failed with error %d\n.", address, data, ret);
		throw cuhRetCode_Comm;
	}
}

void V2495_flash::ReadRegister(uint32_t address, uint32_t *data) {
	int32_t ret;
	if ((ret = CAENComm_Read32(handle, address, data)) != CAENComm_Success) {
		fprintf(stderr, "ReadRegister(0x%X) failed with error %d\n.", address, ret);
		throw cuhRetCode_Comm;
	}
}

void V2495_flash::MultiWriteRegister(int32_t count, uint32_t *addresses, uint32_t *datas) {
	int32_t ret;
	CAENComm_ErrorCode errs[count];
	if ((ret = CAENComm_MultiWrite32(handle, addresses, count, datas, errs)) != CAENComm_Success) {
		fprintf(stderr, "CAENComm_MultiWrite32() failed with error %d\n.", ret);
		throw cuhRetCode_Comm;
	}
	for (int i = 0; i < count; i++) {
		if (errs[i] != CAENComm_Success) {
			fprintf(stderr, "Write Register failed during multiwrite. address=0x%X, data=0x%X, err=%d\n.", addresses[i], datas[i], ret);
			throw cuhRetCode_Comm;
		}
	}
}

void V2495_flash::MultiReadRegister(int32_t count, uint32_t *addresses, uint32_t *datas) {
	int32_t ret;
	CAENComm_ErrorCode errs[count];
	if ((ret = CAENComm_MultiRead32(handle, addresses, count, datas, errs)) != CAENComm_Success) {
		fprintf(stderr, "CAENComm_MultiRead32() failed with error %d\n.", ret);
		throw cuhRetCode_Comm;
	}
	for (int i = 0; i < count; i++) {
		if (errs[i] != CAENComm_Success) {
			fprintf(stderr, "Read Register failed during multiwrite. address=0x%X, err=%d\n.", addresses[i], ret);
			throw cuhRetCode_Comm;
		}
	}
}

void V2495_flash::closeDevice() {
	if (handle != -1)
		CAENComm_CloseDevice(handle);
}

void V2495_flash::sleep(uint32_t ms) {
#ifdef WIN32
	Sleep(ms);
#else
	usleep(ms * 1000);
#endif
}
