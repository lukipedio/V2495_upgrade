//#pragma once
#include <stdint.h> // for fixed-width integers

using namespace std;

class V2495_flash
{

private:
	int handle;
	int _flash_controller_present;

	uint32_t controller_base_address; 

	const static uint32_t OPCODE_OFFSET           = 0x00;
	const static uint32_t ADDRESS_OFFSET          = 0x04;
	const static  uint32_t PAYLOAD_OFFSET          = 0x08;
	const static uint32_t REBOOT_OFFSET           = 0x0C;
	const static uint32_t REBOOT_ADDRESS_OFFSET   = 0x10;
	const static uint32_t UNLOCK_OFFSET           = 0x14;
	const static uint32_t FPGA_ACCESS_OFFSET      = 0x18;
	const static uint32_t FLASH_ACCESS_OFFSET     = 0x1C;
	const static uint32_t IDCODE_OFFSET           = 0xF0;
	const static uint32_t BRAM_START_OFFSET       = 0x100;

	// Errors
	const static int      CONTROLLER_NOT_PRESENT = -1;
	const static int      COMMUNICATION_ERROR = -2;

	// PROTECTION OPCODES
	const static uint32_t PROTECT_SECTORS_0_63 = 0x0F;
	const static uint32_t PROTECT_SECTORS_0_127 = 0x18;
	const static uint32_t UNPROTECT_ALL         = 0x08;

	//OPCODES:
	//
	//	0 Reset state machine
	//		1 Scrittura Flash(write Enable)
	//		2 Read Flash status(Read status register)
	//		3 Sector Erase
	//		4 Page Program
	//		5 Read Data
	//		6 Write Status Register(for sectors lock / unlock)
	//		15 Flash NOP command
	const static uint32_t RESET_CONTROLLER_OPCODE = 0;
	const static uint32_t WRITE_ENABLE_OPCODE = 1;
	const static uint32_t READ_STATUS_OPCODE = 2;
	const static uint32_t SECTOR_ERASE_OPCODE = 3;
	const static uint32_t WRITE_PAGE_OPCODE = 4;
	const static uint32_t READ_PAGE_OPCODE = 5;
	const static uint32_t WRITE_STATUS_OPCODE = 6;
	const static uint32_t NOP_OPCODE = 15;

	// ************ MAIN FIRMWARE FLASH MAP ****************
	//	Start Address 	Description 	Sectors
	//		0000_0000 	Factory FW 	0 - 63
	//		0040_0000 	Appl.FW 	64 - 105
	//		006A_0000 		        106 - 510
	//		01FF_0000 	Conf.ROM 	511
	//

	const static uint32_t MAIN_FACTORY_START_ADDRESS = 0x00000000;
	const static uint32_t MAIN_APPLICATION_START_ADDRESS = 0x00800000;
	const static uint32_t MAIN_CONFIG_ROM_START_ADDRESS = 0x007F0000;

	// ************ USER FIRMWARE FLASH MAP ****************
	//	Start Address 	Description 	Sectors
	//		0000_0000 	User Factory 	0 - 127
	//		0080_0000 	User Appl. 1 	128 - 193
	//		00C2_0000 	User Appl. 2 	194 - 259
	//		0104_0000 	User Appl. 3 	260 - 325
	//		0146_0000 	User Appl. 4 	326 - 391
	//		0188_0000 	User Appl. 5 	392 - 457
	//		01CA_0000 	Free 	        458 - 511
	const static uint32_t USER_FACTORY_START_ADDRESS = 0x00000000;
	const static uint32_t USER_APPLICATION1_START_ADDRESS = 0x00800000;
	const static uint32_t USER_APPLICATION2_START_ADDRESS = 0x00C20000;
	const static uint32_t USER_APPLICATION3_START_ADDRESS = 0x01040000;
	const static uint32_t USER_APPLICATION4_START_ADDRESS = 0x01460000;
	const static uint32_t USER_APPLICATION5_START_ADDRESS = 0x01880000;
	
	// *********** HV RANGES ADDRESS ****************
	const static uint32_t ACTIVE_RANGES_PAGE_ADDRESS = 0x012C0000;
	const static uint32_t HV_CHANNELS_ROM_PAGE_ADDRESS = 0x00670000;

	const static uint32_t PAGE_SIZE                      = 256; // bytes
	const static uint32_t SECTOR_SIZE                    = 64 * 1024; // 64KB

	const static uint32_t MAIN_FIRMWARE_SECTORS          = 66;
	const static uint32_t MAIN_FIRMWARE_BITSTREAM_LENGTH = 4321299; // bytes

	const static uint32_t USER_FIRMWARE_SECTORS          = 66;
	const static uint32_t USER_FIRMWARE_BITSTREAM_LENGTH = 4321299; // bytes

	uint8_t *bitstream;
	int bitstream_length;
	
	// Controller status
	void get_controller_status(uint32_t * status);

	// Flash status register 
	void set_flash_status(uint8_t status);
	void get_flash_status(uint32_t * status);

	// Bit reverse in bytes
	uint8_t rev_byte(uint8_t x);

	// Wait functions
	void wait_flash();
	void wait_controller();

	// Bitstream load from file on disk
	void load_bitstream_from_file(char *filename);

	// Control flash access from controller
	void enable_flash_access();
	void disable_flash_access();
	
	void WriteRegister(uint32_t address, uint32_t data);
	void ReadRegister(uint32_t address, uint32_t *data);
	
	void MultiWriteRegister(int32_t count, uint32_t *addresses, uint32_t *datas); // IMPROVE add some 'const'
	void MultiReadRegister(int32_t count, uint32_t *addresses, uint32_t *datas); // IMPROVE add some 'const'
	
	void closeDevice();
	void sleep(uint32_t ms);


public:
	typedef enum {MAIN_CONTROLLER_OFFSET = 0x8500, USER_CONTROLLER_OFFSET = 0x8700} controller_t;
	typedef enum {BOOT_FW_REGION, APPLICATION1_FW_REGION, APPLICATION2_FW_REGION, APPLICATION3_FW_REGION, APPLICATION4_FW_REGION, APPLICATION5_FW_REGION } fw_region_t;

	V2495_flash(controller_t controller_offset);
	~V2495_flash();

	// Cancella un settore da 64KB
	// Lo start_address deve essere allineato a 64KB
	void sector_erase(uint32_t start_address);
	void page_erase(uint32_t start_address);

	// Scrive una pagina da 256 bytes
	// Lo start_address deve essere allineatoa 256 bytes
	void write_page(uint32_t start_address, uint8_t*  buf);

	// Legge una pagina di 256 bytes
	// Lo start_address deve essere allineatoa 256 bytes
	void read_page(uint32_t start_address, uint8_t*  buf);

	// Read 64KB sector. start address must be aligned to
	// 64K (or it is truncated to smaller one). buf must be
	// allocated at least to 64K.
	void read_sector(uint32_t start_address, uint8_t*  buf);

	// Write 64KB sector. start address must be aligned to
	// 64K (or it is truncated to smaller one). buf must be
	// allocated at least to 64K.
	void write_sector(uint32_t start_address, uint8_t*  buf);

	void program_firmware(fw_region_t region, char *filename, int verify = 0, int no_bit_reverse = 0, int skip_erase = 0); // HACK NOTE : skip_erase e verify potrebbero essere attributi settabili con un set_mode ...
	void verify_firmware(fw_region_t region, char *filename, int no_bit_reverse = 0);
	void dump_firmware(fw_region_t region, char *filename, int no_bit_reverse = 0);
	void erase_firmware(fw_region_t region);
		
	void get_protection_status(uint32_t& status);

	// Sector write protect/unprotect
	void write_protect();
	void write_unprotect();
};

