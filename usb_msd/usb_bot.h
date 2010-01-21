#ifndef USB_BOT_H_
#define USB_BOT_H_

#include "config.h"
#include "usb_msd_defs.h"
#include "usb.h"

//! \brief  Address of Bulk OUT endpoint
#define USB_BOT_EPT_BULK_OUT    UDP_EP_OUT

//! \brief  Address of Bulk IN endpoint
#define USB_BOT_EPT_BULK_IN     UDP_EP_IN

#define USB_BOT_IN_EP_SIZE      UDP_EP_OUT_SIZE
#define USB_BOT_OUT_EP_SIZE     UDP_EP_IN_SIZE


typedef enum
{
//! \brief  Method was successful
    USB_BOT_STATUS_SUCCESS = 0x00,
//! \brief  There was an error when trying to perform a method
    USB_BOT_STATUS_ERROR = 0x01,
} usb_bot_status_t;


//! \brief  Actions to perform during the post-processing phase of a command
//! \brief  Indicates that the CSW should report a phase error
#define USB_BOT_CASE_PHASE_ERROR            (1 << 0)

//! \brief  The driver should halt the Bulk IN pipe after the transfer
#define USB_BOT_CASE_STALL_IN               (1 << 1)

//! \brief  The driver should halt the Bulk OUT pipe after the transfer
#define USB_BOT_CASE_STALL_OUT              (1 << 2)

//! \name Possible direction values for a data transfer
//@{
#define USB_BOT_DEVICE_TO_HOST              0
#define USB_BOT_HOST_TO_DEVICE              1
#define USB_BOT_NO_TRANSFER                 2
//@}

//! \brief  Structure for holding the result of a USB transfer
//! \see    MSD_Callback
typedef struct
{
    uint32_t  dBytesTransferred; //!< Number of bytes transferred
    uint32_t  dBytesRemaining;   //!< Number of bytes not transferred
    unsigned char bSemaphore;    //!< Semaphore to indicate transfer completion
    unsigned char bStatus;       //!< Operation result code
} S_usb_bot_transfer;


//! \brief  Status of an executing command
//! \see    usb_msd_cbw_t
//! \see    usb_msd_csw_t
//! \see    S_usb_bot_transfer
typedef struct 
{
    S_usb_bot_transfer sTransfer;   //!< Current transfer status
    usb_msd_cbw_t sCbw;             //!< Received CBW
    usb_msd_csw_t sCsw;             //!< CSW to send
    uint8_t bCase;    	        //!< Actions to perform when command is complete
    uint32_t dLength;           //!< Remaining length of command
} S_usb_bot_command_state;


bool usb_bot_configured_p (void);

bool usb_bot_ready_p (void);

bool usb_bot_update (void);

bool usb_bot_init (uint8_t num, const usb_descriptors_t *descriptors);

usb_bot_status_t
usb_bot_write (const void *buffer, uint16_t size, void *pTransfer);

usb_bot_status_t
usb_bot_read (void *buffer, uint16_t size, void *pTransfer);

bool
usb_bot_command_get (S_usb_bot_command_state *pCommandState);

bool
usb_bot_status_set (S_usb_bot_command_state *pCommandState);

void
usb_bot_get_command_information (usb_msd_cbw_t *pCbw, uint32_t *pLength, 
                                 uint8_t *pType);

void
usb_bot_abort (S_usb_bot_command_state *pCommandState);

#endif /*USB_BOT_H_*/
