/* GD32F470VK - 3072K Flash, 256K RAM */
MEMORY
{		
	rom (rx) : ORIGIN = 0x08000000, LENGTH = 64K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
}

SECTIONS
{
	.confidential (NOLOAD) : {
		*(confidential)
		ASSERT ((SIZEOF(.confidential) <= 32K), "Error: Confidential section too big!");
	} >ram
}

INCLUDE cortex-m-generic.ld

_ram_start = ORIGIN(ram);
_ram_end = ORIGIN(ram) + LENGTH(ram);
_stay_in_bootloader_flag_addr = ABSOLUTE(0x20010000);
_stack = _ram_end - 8;
__stack_chk_guard = _ram_end - 8;
system_millis = _ram_end - 4;

_data_size = SIZEOF(.data);

