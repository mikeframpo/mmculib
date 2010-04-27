/* Secure digital card driver.  */

#include "sdcard.h"

/*  The SD Card wakes up in the SD Bus mode.  It will enter SPI mode
    if the CS signal is asserted during the reception of the reset
    command (CMD0).

    The default command structure/protocol for SPI mode is that CRC
    checking is disabled.  Since the card powers up in SD Bus mode,
    CMD0 must be followed by a valid CRC byte (even though the command
    is sent using the SPI structure).  Once in SPI mode, CRCs are
    disabled by default.

    The host starts every bus transaction by asserting the CS signal
    low.  

    SanDisk cards allow partial reads (down to 1 byte) but not partial
    writes.  Writes have a minimum block size of 512 bytes. 

    In SPI mode, CRC checks are disabled by default.

    The maximum SPI clock speed is 25 MHz.
 */

enum {SD_CMD_LEN = 6};

typedef enum 
{
    SD_OP_GO_IDLE_STATE = 0,          /* CMD0 */
    SD_OP_SEND_OP_COND = 1,           /* CMD1 */
    SD_OP_SEND_CSD = 9,               /* CMD9 */
    SD_OP_SET_BLOCKLEN = 16,          /* CMD16 */
    SD_OP_READ_BLOCK = 17,            /* CMD17 */
    SD_OP_WRITE_BLOCK = 24,           /* CMD24 */
    SD_OP_WRITE_MULTIPLE_BLOCK = 25,  /* CMD25 */
    SD_OP_READ_OCR = 58,              /* CMD58 */
    SD_OP_CRC_ON_OFF = 59,            /* CMD59 */
} sdcard_op_t;


enum 
{
    SD_WRITE_OK = 5,
    SD_WRITE_CRC_ERROR = 11,
    SD_WRITE_ERROR = 13
};


/* The command format is 6 bytes:
   start bit (0)
   host bit (1)
   command 6-bits
   argument 32-bits
   CRC 7-bits
   stop bit (1)
*/

#define SD_HOST_BIT (BIT (6))
#define SD_STOP_BIT (BIT (0))


#ifndef SDCARD_DEVICES_NUM
#define SDCARD_DEVICES_NUM 4
#endif 

#define SDCARD_RETRIES_NUM 256



static uint8_t sdcard_devices_num = 0;
static sdcard_dev_t sdcard_devices[SDCARD_DEVICES_NUM];

/* The 16-bit CRC uses a standard CCIT generator polynomial:
   x^16 + x^12 + x^5 + 1   */


static uint16_t 
sdcard_crc16_bit (uint16_t crc, uint8_t in)
{
    uint8_t bit0;

    /* NB, the CRC is stored in reverse order to that specified
       by the polynomial.  */
    bit0 = crc & 1;

    crc >>= 1;    
    if (bit0 ^ in)
        crc = crc ^ (BIT (15 - 0) | BIT (15 - 5) | BIT (15 - 12));

    return crc;
}


uint16_t
sdcard_crc16_byte (uint16_t crc, uint8_t val)
{
    uint8_t i;
    
    for (i = 0; i < 8; i++)
    {
        crc = sdcard_crc16_bit (crc, val & 1);
        val >>= 1;
    }
    return crc;
}


uint16_t
sdcard_crc16 (uint16_t crc, const void *bytes, uint16_t size)
{
    uint8_t i;
    const uint8_t *data = bytes;
    
    for (i = 0; i < size; i++)
        crc = sdcard_crc16_byte (crc, data[i]);
    
    return crc;
}


/* The 7-bit CRC uses a generator polynomial:
   x^7 + x^3 + 1   */

uint8_t
sdcard_crc7_byte (uint8_t crc, uint8_t val, uint8_t bits)
{
    int i;
    
    for (i = bits; i--; val <<= 1)
    {
        crc = (crc << 1) | ((val & 0x80) ? 1 : 0);
        
        if (crc & 0x80)
            crc ^= (BIT(0) | BIT(3));
    }
    return crc & 0x7f;
}


uint8_t
sdcard_crc7 (uint8_t crc, const void *bytes, uint8_t size)
{
    uint8_t i;
    const uint8_t *data = bytes;

    for (i = 0; i < size; i++)
        crc = sdcard_crc7_byte (crc, data[i], 8);

    crc = sdcard_crc7_byte (crc, 0, 7);

    return crc;
}


// Keeps clocking the SD card until the desired byte is returned from the card
bool
sdcard_response_match (sdcard_t dev, uint8_t desired)
{
    uint16_t retries;
    uint8_t response;
    
    // Keep reading the SD card for the desired response
    for (retries = 0; retries < SDCARD_RETRIES_NUM; retries++)
    {
        spi_read (dev->spi, &response, 1, 0);
        if (response == desired)
            return 1;
    }
    return 0;
}


static void
sdcard_deselect (sdcard_t dev)
{
    uint8_t dummy[1] = {0xff};

    spi_cs_disable (dev->spi);

    /* After the last SPI bus transaction, the host is required to
       provide 8 clock cycles for the card to complete the operation
       before shutting down the clock. Throughout this 8-clock period,
       the state of the CS signal is irrelevant.  It can be asserted
       or de-asserted.  */

    spi_write (dev->spi, dummy, sizeof (dummy), 0);

    spi_cs_enable (dev->spi);
}


static uint8_t
sdcard_command (sdcard_t dev, sdcard_op_t op, uint32_t param)
{
    uint8_t command[SD_CMD_LEN];
    uint8_t response[SD_CMD_LEN];
    uint16_t retries;
    
    command[0] = op | SD_HOST_BIT;
    command[1] = param >> 24;
    command[2] = param >> 16;
    command[3] = param >> 8;
    command[4] = param;
    command[5] = (sdcard_crc7 (0, command, 5) << 1) | SD_STOP_BIT;
        
    /* Send command; the card will respond with a sequence of 0xff.  */
    spi_transfer (dev->spi, command, response, SD_CMD_LEN, 0);

    command[0] = 0xff;

    /* Search for R1 response; the command should respond with 0 to 8
       bytes of 0xff.  */
    for (retries = 0; retries < 4096; retries++)
    {
        spi_transfer (dev->spi, command, &dev->status, 1, 0);
        
#if 0
        if (! (dev->status & 0x80))
            break;

//        if (dev->status == 0xff)
//            continue;

#else
        /* Check for R1 response.  */
        if ((op == SD_OP_GO_IDLE_STATE && dev->status == 0x01)
            || (op != SD_OP_GO_IDLE_STATE && dev->status == 0x00))
            break;

//        /* The first bit is zero when the response is received.
//           The other bits indicate errors.  */
//        if (! (dev->status & 0x80))
//            break;
#endif
    }

    return dev->status;
}


uint8_t
sdcard_csd_read (sdcard_t dev)
{
    uint8_t message[17];
    
    message[0] = SD_OP_SEND_CSD | SD_HOST_BIT;
    message[1] = 0;
    message[2] = 0;
    message[3] = 0;
    message[4] = 0;
    message[5] = (sdcard_crc7 (0, message, 5) << 1) | SD_STOP_BIT;
        
    /* Send command and read R2 response.  */
    spi_transfer (dev->spi, message, message, sizeof (message), 0);

    return message[0];
}


sdcard_addr_t
sdcard_capacity (sdcard_t dev)
{
    uint8_t message[17];
    uint16_t c_size;
    uint16_t c_size_mult;
    uint16_t read_bl_len;
    uint16_t block_len;
    sdcard_addr_t capacity;
    
    message[0] = SD_OP_SEND_CSD | SD_HOST_BIT;
    message[1] = 0;
    message[2] = 0;
    message[3] = 0;
    message[4] = 0;
    message[5] = (sdcard_crc7 (0, message, 5) << 1) | SD_STOP_BIT;
        
    /* Send command and read R2 response.  */
    spi_transfer (dev->spi, message, message, sizeof (message), 0);

    /* C_SIZE bits 70:62
       C_SIZE_MULT bits 49:47
       READ_BL_LEN bits 83:80
    */

    c_size = ((message[7] & 0x7f) << 2) | (message[8] >> 6);
    c_size_mult = ((message[9] & 0x03) << 1) | (message[10] >> 7);
    read_bl_len = message[5] & 0x0f;
    
    block_len = 1 << read_bl_len;
    capacity = c_size * (1LL << (c_size_mult + 2)) * block_len;

    return capacity;
}


uint16_t
sdcard_write_block (sdcard_t dev, sdcard_addr_t addr, const void *buffer,
                    sdcard_block_t block)
{
    uint8_t status;
    uint16_t crc;
    uint8_t command[2];
    uint8_t response[1];

    addr = block * SDCARD_BLOCK_SIZE;

    status = sdcard_command (dev, SD_OP_WRITE_BLOCK, addr);
    if (status != 0)
    {
        sdcard_deselect (dev);
        return 0;
    }

    crc = sdcard_crc16 (0, buffer, SDCARD_BLOCK_SIZE);
    
    /* Send data begin token.  */
    command[0] = 0xFE;
    spi_write (dev->spi, command, 1, 1);

    /* Send the data.  */
    spi_write (dev->spi, buffer, SDCARD_BLOCK_SIZE, 1);

    command[0] = crc >> 8;
    command[1] = crc & 0xff;

    /* Send the crc.  */
    spi_write (dev->spi, command, 2, 1);

    /* Get the status response.  */
    command[0] = 0xff;
    spi_transfer (dev->spi, command, response, 1, 1);    
    
    /* Check to see if the data was accepted.  */
    if ((response[0] & 0x1F) != SD_WRITE_OK)
    {
        sdcard_deselect (dev);
        return 0;
    }
    
    /* Wait for card to complete write cycle.  */
    if (!sdcard_response_match (dev, 0x00))
    {
        sdcard_deselect (dev);
        return 0;
    }
    
    sdcard_deselect (dev);

    return SDCARD_BLOCK_SIZE;
}


// Read a 512 block of data on the SD card
sdcard_ret_t 
sdcard_read_block (sdcard_t dev, sdcard_addr_t addr, void *buffer,
                   sdcard_block_t block)
{
    uint8_t status;
    uint8_t command[2];

    addr = block * SDCARD_BLOCK_SIZE;

    status = sdcard_command (dev, SD_OP_READ_BLOCK, addr);
    if (status != 0)
    {
        sdcard_deselect (dev);
        return 0;
    }

    /* Send data begin token.  */
    command[0] = 0xFE;
    spi_write (dev->spi, command, 1, 1);

    /* Read the data.  */
    spi_read (dev->spi, buffer, SDCARD_BLOCK_SIZE, 1);

    /* Read the crc.  */
    spi_read (dev->spi, command, 2, 1);

    sdcard_deselect (dev);

    return SDCARD_BLOCK_SIZE;
}


sdcard_ret_t
sdcard_read (sdcard_t dev, sdcard_addr_t addr, void *buffer, sdcard_size_t size)
{
    uint16_t blocks;
    uint16_t i;
    sdcard_size_t total;
    sdcard_size_t bytes;
    uint8_t *dst;

    /* Ignore partial reads.  */
    if (addr % SDCARD_BLOCK_SIZE || size % SDCARD_BLOCK_SIZE)
        return 0;

    blocks = size / SDCARD_BLOCK_SIZE;
    dst = buffer;
    total = 0;
    for (i = 0; i < blocks; i++)
    {
        bytes = sdcard_read_block (dev, addr + i, dst, size);
        if (!bytes)
            return total;
        dst += bytes;
        total += bytes;
    }
    return total;
}


sdcard_ret_t
sdcard_write (sdcard_t dev, sdcard_addr_t addr, const void *buffer,
              sdcard_size_t size)
{
    uint16_t blocks;
    uint16_t i;
    sdcard_size_t total;
    sdcard_size_t bytes;
    const uint8_t *src;

    /* Ignore partial writes.  */
    if (addr % SDCARD_BLOCK_SIZE || size % SDCARD_BLOCK_SIZE)
        return 0;

    blocks = size / SDCARD_BLOCK_SIZE;
    src = buffer;
    total = 0;
    for (i = 0; i < blocks; i++)
    {
        bytes = sdcard_write_block (dev, addr + i, src, size);
        if (!bytes)
            return total;
        src += bytes;
        total += bytes;
    }
    return total;
}


sdcard_err_t
sdcard_probe (sdcard_t dev)
{
    uint8_t status;
    int retries;
    uint8_t dummy[10] = {0xff, 0xff, 0xff, 0xff, 0xff,
                         0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t command[4];
    uint8_t response[4];

    /* Send the card 80 clocks to activate it (at least 74 are
       required).  */
    spi_write (dev->spi, dummy, sizeof (dummy), 1);

    //sdcard_deselect (dev);

    /* Send software reset.  */
    status = sdcard_command (dev, SD_OP_GO_IDLE_STATE, 0);
    if (status != 0x01)
        return SDCARD_ERR_NO_CARD;

    sdcard_deselect (dev);

    /* Check to see if card happy with our supply voltage.  */

    for (retries = 0; retries < 256; retries++)
    {
        /* Need to keep sending this command until the in-idle-state
           bit is set to 0.  */
        status = sdcard_command (dev, SD_OP_SEND_OP_COND, 0);
        if ((status & 0x01) == 0)
            break;
    }

    sdcard_deselect (dev);

    if (status != 0)
    {
        /* Have an error bit set.  */
        return SDCARD_ERR_ERROR;
    }

#if 0
    /* Read operation condition register (OCR).  */
    for (retries = 0; retries < 65536; retries++)
    {
        status = sdcard_command (dev, SD_OP_READ_OCR, 0);
        if (status <= 1)
            break;
    }
    if (status > 1)
    {
        sdcard_deselect (dev);
        return SDCARD_ERR_ERROR;
    }

    spi_transfer (dev->spi, command, response, 4, 0);    
#endif
    
    status = sdcard_command (dev, SD_OP_SET_BLOCKLEN, SDCARD_BLOCK_SIZE);
    sdcard_deselect (dev);

    if (status != 0)
        return SDCARD_ERR_ERROR;

    return SDCARD_ERR_OK;
}


sdcard_t
sdcard_init (const sdcard_cfg_t *cfg)
{
    sdcard_t dev;

    if (sdcard_devices_num >= SDCARD_DEVICES_NUM)
        return 0;
    dev = sdcard_devices + sdcard_devices_num;
 
    dev->spi = spi_init (&cfg->spi);
    if (!dev->spi)
        return 0;

    // Hmmm, should we let the user override the mode?
    spi_mode_set (dev->spi, SPI_MODE_0);
    spi_cs_mode_set (dev->spi, SPI_CS_MODE_FRAME);

    /* Ensure chip select isn't asserted too soon.  */
    spi_cs_assert_delay_set (dev->spi, 16);    
    /* Ensure chip select isn't negated too soon.  */
    spi_cs_negate_delay_set (dev->spi, 16);    
   
    return dev;
}


void
sdcard_shutdown (sdcard_t dev)
{
    // TODO
    spi_shutdown (dev->spi);
}