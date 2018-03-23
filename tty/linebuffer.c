/** @file   linebuffer.h
    @author M. P. Hayes
    @date   12 May 2015
    @brief  A wrapper for a ring buffer with primitive line editing
    support and mapping of carriage returns to newlines.
*/

#include <stdlib.h>
#include "errno.h"
#include "utility/ring.h"
#include "linebuffer.h"

struct linebuffer_struct
{
    ring_t ring;
    uint8_t newlines;
};


/** Initialise line buffer.
    @param size is the maximum linebuffer size
    @return pointer to linebuffer 
*/
linebuffer_t *
linebuffer_init (int size)
{
    char *buffer;
    linebuffer_t *linebuffer;
    
    buffer = malloc (size);
    if (!buffer)
        return 0;

    linebuffer = malloc (sizeof (*linebuffer));
    if (!linebuffer)
        return 0;
    linebuffer->newlines = 0;
    
    ring_init (&linebuffer->ring, buffer, size);
    return linebuffer;
}


/** Add new character to linebuffer.  Backspace will delete the
    previous character in the linebuffer (if present).  Carriage
    returns are converted to newlines.  */
void
linebuffer_add (linebuffer_t *linebuffer, char ch)
{
    /* Perhaps could have cooked/raw modes and head down the rabbit
     hole of termio?  */

    switch (ch)
    {
    case '\b':
        /* Discard last character from the linebuffer up until last
           newline.  */
        if (ring_peek (&linebuffer->ring) != '\n')
            ring_getc (&linebuffer->ring);
        break;

    case '\r':
    case '\n':        
        /* Replace carriage return with newline.  */
        if (ring_putc (&linebuffer->ring, '\n'))
            linebuffer->newlines++;        
        break;

    default:
        ring_putc (&linebuffer->ring, ch);
        break;
    }
}


/** This is a non-blocking version of fgetc.  
    @param linebuffer a pointer to the linebuffer
    @return next character from line buffer if it contains a newline 
            otherwise -1.
*/
int
linebuffer_getc (linebuffer_t *linebuffer)
{
    int ch;

    if (linebuffer->newlines == 0)
    {
        errno = EAGAIN;
        return -1;
    }

    ch = ring_getc (&linebuffer->ring);
    if (ch == -1)
    {
        /* Something is wrong.  We think we have some newlines in the
           ring buffer but the ring buffer is empty!  */
        while (1)
            continue;
        linebuffer->newlines = 0;
        return ch;
    }
    
    if (ch == '\n')
        linebuffer->newlines--;
    return ch;
}
