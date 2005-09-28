/***************************************************************************
 * lmplatform.c:
 * 
 * Platform portability routines.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License (GNU-LGPL) for more details.  The
 * GNU-LGPL and further information can be found here:
 * http://www.gnu.org/
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified: 2005.271
 ***************************************************************************/

/* Define _LARGEFILE_SOURCE to get ftello on some systems (Linux) */
#define _LARGEFILE_SOURCE 1

#include <errno.h>

#include "lmplatform.h"


/***************************************************************************
 * lmp_strerror:
 *
 * Return a description of the last system error, in the case of Win32
 * this will be the last Windows Sockets error.
 ***************************************************************************/
const char *
lmp_strerror (void)
{
#if defined(LMP_WIN32)
  static char errorstr[100];

  snprintf (errorstr, sizeof(errorstr), "%d", WSAGetLastError());
  return (const char *) errorstr;

#else
  return (const char *) strerror (errno);

#endif
}  /* End of lmp_strerror() */


/***************************************************************************
 * lmp_ftello:
 *
 * Return the current file position for the specified descriptor using
 * the system's closest match to the POSIX ftello.
 ***************************************************************************/
off_t
lmp_ftello (FILE *stream)
{
#if defined(LMP_WIN32)
  return (off_t) ftell (stream);

#else
  return (off_t) ftello (stream);

#endif
}  /* End of lmp_ftello() */


