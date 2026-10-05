#ifndef FIRMWARE_H
#define FIRMWARE_H

#define ROM_READ_CHUNK (64 * 1024)

// Load backup file content into SNES BSRAM. A missing file starts with zero-filled RAM.
// name: save file name (.srm)
// size: in bytes
void backup_load(char *name, int size);

// Save a captured BSRAM image to SD, preserving the previous file on failure.
// name: save file name (.srm)
// size: in bytes
int backup_save(char *name, int size);

// Saves every 10 seconds
void backup_process();

int loadnes(int rom);
int loadsnes(int rom);
int loadgba(int rom);
int loadmd(int rom);

void message(char *msg, int center);

void status(char *msg);

#define CRC16 0x8005

uint16_t gen_crc16(const volatile uint8_t *data, int size);

#endif
