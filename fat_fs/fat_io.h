/** @file   fat_io.h
    @author Michael Hayes
    @date   23 November 2010
    @brief  FAT filesystem I/O operations.
*/

#ifndef FAT_IO
#define FAT_IO

#include "fat.h"


uint16_t
fat_io_read (fat_t *fat, fat_sector_t sector,
             uint16_t offset, void *buffer, uint16_t size);


uint16_t
fat_io_write (fat_t *fat, fat_sector_t sector,
              uint16_t offset, const void *buffer, uint16_t size);


uint8_t *
fat_io_cache_read (fat_t *fat, fat_sector_t sector);


uint16_t
fat_io_cache_write (fat_t *fat, fat_sector_t sector);


void
fat_io_cache_flush (fat_t *fat);


void
fat_io_cache_init (fat_t *fat);


#endif