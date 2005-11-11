/***************************************************************************
 * seisan2mseed.c
 *
 * Simple waveform data conversion from SeisAn to Mini-SEED.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified 2005.315
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <libmseed.h>

#define VERSION "0.8.1"
#define PACKAGE "seisan2mseed"

struct listnode {
  char *key;
  char *data;
  struct listnode *next;
};

static void packtraces (flag flush);
static int seisan2group (char *seisanfile, TraceGroup *mstg);
static int detectformat (FILE *ifp, char *formatflag, char *swapflag, char *seisanfile);
static int translatechan (char *component, char *channel, char *location);
static int parameter_proc (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static int readlistfile (char *listfile);
static void addnode (struct listnode **listroot, char *key, char *data);
static void addmapnode (struct listnode **listroot, char *mapping);
static void record_handler (char *record, int reclen);
static void usage (void);

static int   verbose     = 0;
static int   packreclen  = -1;
static int   encoding    = -1;
static int   byteorder   = -1;
static char  srateblkt   = 0;
static char  bufferall   = 0;
static char *forcenet    = 0;
static char *forceloc    = 0;
static char *outputfile  = 0;
static FILE *ofp         = 0;

/* A list of input files */
struct listnode *filelist = 0;

/* A list of component to channel translations */
struct listnode *chanlist = 0;

static TraceGroup *mstg = 0;

static int packedtraces  = 0;
static int packedsamples = 0;
static int packedrecords = 0;

int
main (int argc, char **argv)
{
  struct listnode *flp;
  
  /* Process given parameters (command line and parameter file) */
  if (parameter_proc (argc, argv) < 0)
    return -1;
  
  /* Init TraceGroup */
  mstg = mst_initgroup (mstg);
  
  /* Open the output file if specified */
  if ( outputfile )
    {
      if ( strcmp (outputfile, "-") == 0 )
        {
          ofp = stdout;
        }
      else if ( (ofp = fopen (outputfile, "wb")) == NULL )
        {
          fprintf (stderr, "Cannot open output file: %s (%s)\n",
                   outputfile, strerror(errno));
          return -1;
        }
    }
  
  /* Read input SeisAn files into TraceGroup */
  flp = filelist;
  while ( flp != 0 )
    {
      if ( verbose )
	fprintf (stderr, "Reading %s\n", flp->data);

      seisan2group (flp->data, mstg);
      
      flp = flp->next;
    }

  /* Pack any remaining, possibly all data */
  packtraces (1);
  packedtraces += mstg->numtraces;

  fprintf (stderr, "Packed %d trace(s) of %d samples into %d records\n",
	   packedtraces, packedsamples, packedrecords);
  
  /* Make sure everything is cleaned up */
  mst_freegroup (&mstg);
  
  if ( ofp )
    fclose (ofp);
  
  return 0;
}  /* End of main() */


/***************************************************************************
 * packtraces:
 *
 * Pack all traces in a group using per-Trace templates.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
packtraces (flag flush)
{
  Trace *mst;
  int trpackedsamples = 0;
  int trpackedrecords = 0;
  
  mst = mstg->traces;
  while ( mst )
    {
      if ( mst->numsamples <= 0 )
	{
	  mst = mst->next;
	  continue;
	}
      
      trpackedrecords = mst_pack (mst, &record_handler, packreclen, encoding, byteorder,
				  &trpackedsamples, flush, verbose-2, (MSrecord *) mst->private);
      if ( trpackedrecords < 0 )
	{
	  fprintf (stderr, "Error packing data\n");
	}
      else
	{
	  packedrecords += trpackedrecords;
	  packedsamples += trpackedsamples;
	}
      
      mst = mst->next;
    }
}  /* End of packtraces() */


/***************************************************************************
 * seian2group:
 * Read a SeisAn file and add data samples to a TraceGroup.  As the SeisAn
 * data is read in a MSrecord struct is used as a holder for the input
 * information.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static int
seisan2group (char *seisanfile, TraceGroup *mstg)
{
  FILE *ifp = 0;
  MSrecord *msr = 0;
  Trace *mst;
  struct blkt_100_s Blkt100;
  
  char *record = 0;
  size_t recordbufsize = 0;
  
  char swapflag = -1;
  char formatflag = 0;  /* 1: PC SeisAn <= 6.0, 4: SeisAn >= 7.0 */
  uint8_t reclen1 = 0;
  uint8_t reclenmirror1 = 0;
  uint32_t reclen4 = 0;
  uint32_t reclenmirror4 = 0;
  unsigned int reclen = 0;
  off_t filepos;
  
  size_t readlen;
  
  char expectheader = 1;
  char cheader[1040];
  int cheaderlen = 0;
  
  char expectdata = 0;
  char *data = 0;
  int datalen = 0;
  int maxdatalen = 0;
  int datasamplesize = 0;
  int expectdatalen = 0;
  
  int32_t *samplebuffer = 0;
  int maxsamplebufferlen = 0;
  int numsamples = 0;
  int32_t *sampleptr4 = 0;
  int16_t *sampleptr2 = 0;

  char component[5];
  long year;
  char timestr[30];
  char ratestr[10];
  char sampstr[10];
  char uctimeflag = 0;
  char gainstr[15];
  char gainflag = 0;
  double gain = 1.0;
  
  char *cat, *mouse;
  
  /* Open input file */
  if ( (ifp = fopen (seisanfile, "rb")) == NULL )
    {
      fprintf (stderr, "Cannot open input file: %s (%s)\n",
	       seisanfile, strerror(errno));
      return -1;
    }
  
  /* Detect format and byte order */
  if ( detectformat (ifp, &formatflag, &swapflag, seisanfile) )
    {
      if ( ferror(ifp) )
	fprintf (stderr, "Error reading file %s: %s\n",
		 seisanfile, strerror(errno));
      else
	fprintf (stderr, "Error detecting data format of %s\n", seisanfile);
      
      return -1;
    }
  
  /* Read the signature character for formatflag == 1, it's not needed. */
  if ( formatflag == 1 )
    fread (&reclen1, 1, 1, ifp);
  
  /* Report format and byte order detection results */
  if ( verbose > 1 )
    {
      if ( formatflag == 1 )
	fprintf (stderr, "Detected PC <= 6.0 format for %s\n", seisanfile);
      else if ( formatflag == 4 )
	fprintf (stderr, "Detected Sun/Linux and PC >= 7.0 format for %s\n", seisanfile);
      else
	{
	  fprintf (stderr, "Unknown format for %s\n", seisanfile);
	  return -1;
	}
      
      if ( swapflag == 0 )
	fprintf (stderr, "Byte swapping not needed for %s\n", seisanfile);
      else
	fprintf (stderr, "Byte swapping needed for %s\n", seisanfile);
    }
  
  /* Open output file if needed */
  if ( ! ofp )
    {
      char mseedoutputfile[1024];
      
      /* If a "standard" name change the S to an M otherwise add _MSEED to the end */
      if ( seisanfile[4] == '-' && seisanfile[7] == '-' && seisanfile[10] == '-' &&
	   seisanfile[15] == '-' && seisanfile[18] == 'S' && seisanfile[19] == '.' )
	{
	  strncpy (mseedoutputfile, seisanfile, sizeof(mseedoutputfile));
	  mseedoutputfile[18] = 'M';
	}
      else
	{
	  snprintf (mseedoutputfile, sizeof(mseedoutputfile), "%s_MSEED", seisanfile);
	}
      
      if ( (ofp = fopen (mseedoutputfile, "wb")) == NULL )
        {
          fprintf (stderr, "Cannot open output file: %s (%s)\n",
                   mseedoutputfile, strerror(errno));
          return -1;
        }
    }
  
  if ( ! (msr = msr_init(msr)) )
    {
      fprintf (stderr, "Cannot initialize MSrecord strcture\n");
      return -1;
    }
  
  /* Read a record at a time */
  for (;;)
    {
      /* Get current file position */
      filepos = lmp_ftello (ifp);
      
      /* Read next record length */
      if ( formatflag == 1 )
	{
	  if ( (readlen = fread (&reclen1, 1, 1, ifp)) < 1 )
	    {
	      if ( ferror(ifp) )
		fprintf (stderr, "Error reading file %s: %s\n", seisanfile, strerror(errno));
	      break;
	    }
	  reclen = reclen1;
	}
      if ( formatflag == 4 )
	{
	  if ( (readlen = fread (&reclen4, 4, 1, ifp)) < 1 )
	    {
	      if ( ferror(ifp) )
		fprintf (stderr, "Error reading file %s: %s\n", seisanfile, strerror(errno));
	      break;
	    }
	  
	  if ( swapflag ) gswap4 ( &reclen4 );
	  reclen = reclen4;
	}
      
      if ( verbose > 2 )
	fprintf (stderr, "Reading next record of length %d bytes from offset %lld\n",
		 reclen, (long long) filepos);
      
      /* Make sure enough memory is available */
      if ( reclen > recordbufsize )
	{
	  if ( (record = realloc (record, reclen)) == NULL )
	    {
	      fprintf (stderr, "Error allocating memory for record\n");
	      break;
	    }
	  else
	    recordbufsize = reclen;
	}

      /* Read the record */
      if ( (readlen = fread (record, 1, reclen, ifp)) < reclen )
	{
	  if ( ferror(ifp) )
	    fprintf (stderr, "Error reading file %s\n", seisanfile);
	  else if ( readlen < reclen )
	    fprintf (stderr, "Short read, only read %d of %d bytes.\n", (int)readlen, reclen);
	  
	  break;
	}
      
      /* Read record length mirror at the end of the record */
      if ( formatflag == 1 )
	{
	  if ( (readlen = fread (&reclenmirror1, 1, 1, ifp)) < 1 )
	    {
	      if ( ferror(ifp) )
		fprintf (stderr, "Error reading file %s: %s\n", seisanfile, strerror(errno));
	      break;
	    }
	  if ( reclen1 != reclenmirror1 )
	    {
	      fprintf (stderr, "At byte offset %lld in %s:\n", (long long) filepos, seisanfile);
	      fprintf (stderr, "  Next and previous record length values do not match: %d != %d\n",
		       reclen1, reclenmirror1);
	      break;
	    }
	}
      if ( formatflag == 4 )
	{
	  if ( (readlen = fread (&reclenmirror4, 4, 1, ifp)) < 1 )
	    {
	      if ( ferror(ifp) )
		fprintf (stderr, "Error reading file %s: %s\n", seisanfile, strerror(errno));
	      break;
	    }
	  if ( swapflag ) gswap4 ( &reclenmirror4 );
	  if ( reclen4 != reclenmirror4 )
	    {
	      fprintf (stderr, "At byte offset %lld in %s:\n", (long long) filepos, seisanfile);
	      fprintf (stderr, "  Next and previous record length values do not match: %d != %d\n",
		       reclen4, reclenmirror4);
	      break;
	    }
	}
      
      /* Expecting a channel header:
       * Either the channel header is starting (first char is not space)
       * Or we are already reading it (cheaderlen != 0) */
      if ( expectheader && (cheaderlen != 0 || *record != ' ') )
	{
	  /* Copy record into channel header buffer */
	  if ( (reclen + cheaderlen) <= 1040 )
	    {
	      memcpy (cheader + cheaderlen, record, reclen);
	      cheaderlen += reclen;
	    }
	  else
	    {
	      fprintf (stderr, "Record is too long for the expected channel header!\n");
	      fprintf (stderr, "  cheaderlen: %d, reclen: %d (channel header should be 1040 bytes)\n",
		       cheaderlen, reclen);
	      break;
	    }
	  
	  /* Continue reading records if channel header is not filled */
	  if ( cheaderlen < 1040 )
	    continue;
	  
	  /* Otherwise parse the header */
	  ms_strncpclean (msr->network, forcenet, 2);
	  ms_strncpclean (msr->station, cheader, 5);
	  
	  /* Map component to SEED channel and location */
	  memset (component, 0, sizeof(component));
	  memcpy (component, cheader + 5, 4);
	  
	  translatechan (component, msr->channel, msr->location);
	  
	  if ( forceloc )
	    ms_strncpclean (msr->location, forceloc, 2);
	  
	  /* Construct time string */
	  memset (timestr, 0, sizeof(timestr));
	  memcpy (timestr, cheader + 9, 3);
	  year = strtoul (timestr, NULL, 10);
	  year += 1900;
	  sprintf (timestr, "%4ld", year);
	  
	  strcat (timestr, ",");
	  strncat (timestr, cheader + 13, 3);
          strcat (timestr, ",");
	  strncat (timestr, cheader + 23, 2);
          strcat (timestr, ":");
	  strncat (timestr, cheader + 26, 2);
          strcat (timestr, ":");
	  strncat (timestr, cheader + 29, 6);
	  
	  /* Remove spaces */
	  cat = mouse = timestr;
	  while ( *mouse )
	    if ( *mouse++ != ' ' )
	      *cat++ = *(mouse-1);
	  *cat = '\0';
	  
	  msr->starttime = ms_seedtimestr2hptime (timestr);

	  /* Parse sample rate */
	  memset (ratestr, 0, sizeof(ratestr));
	  memcpy (ratestr, cheader + 36, 7);
	  msr->samprate = strtod (ratestr, NULL);
	  
	  /* Parse sample count */
          memset (sampstr, 0, sizeof(sampstr));
          memcpy (sampstr, cheader + 43, 7);
	  msr->samplecnt = strtoul (sampstr, NULL, 10);

	  /* Detect uncertain time */
	  uctimeflag = ( *(cheader+28) == 'E' ) ? 1 : 0;
	  
	  /* Detect gain */
	  gainflag = ( *(cheader+75) == 'G' ) ? 1 : 0;
	  if ( gainflag )
	    {
	      memset (gainstr, 0, sizeof(gainstr));
	      memcpy (gainstr, cheader + 147, 12);
	      gain = strtod (gainstr, NULL);

	      fprintf (stderr, "Gain of %f detected\n", gain);
	      fprintf (stderr, "Gain NOT applied, no support for that yet!\n");
	    }
	  
	  /* Determine data sample size */
	  datasamplesize = ( *(cheader+76) == '4' ) ? 4 : 2;
	  
	  if ( verbose )
	    fprintf (stderr, "[%s] '%s_%s' (%s): %s%s, %d %d byte samps @ %.4f Hz\n",
		     seisanfile, msr->station, component, msr->channel,
		     timestr, (uctimeflag) ? " [UNCERTAIN]" : "",
		     msr->samplecnt, datasamplesize, msr->samprate);
	  
	  expectdata = 1;
	  expectdatalen = msr->samplecnt * datasamplesize;
	  expectheader = 0;
	  cheaderlen = 0;
	  continue;
	}
      
      /* Expecting data */
      if ( expectdata )
	{
	  /* Copy record into data buffer */
	  if ( (reclen + datalen) <= expectdatalen )
	    {
	      /* Make sure enough memory is available */
	      if ( (reclen + datalen) > maxdatalen )
		{
		  if ( (data = realloc (data, (reclen+datalen))) == NULL )
		    {
		      fprintf (stderr, "Error allocating memory for record\n");
		      break;
		    }
		  else
		    maxdatalen = reclen + datalen;
		}
	      
	      memcpy (data + datalen, record, reclen);
	      datalen += reclen;
	    }
	  else
	    {
	      fprintf (stderr, "Record is too long for the expected data!\n");
	      fprintf (stderr, " datalen: %d, reclen: %d, expectdatalen: %d\n",
		       datalen, reclen, expectdatalen);
	      break;
	    }
	  
	  /* Continue reading records if enough data has not been read */
	  if ( datalen < expectdatalen )
	    continue;
	  
	  /* Number of samples implied by data record length */
	  msr->numsamples = datalen / datasamplesize;

	  if ( msr->samplecnt != msr->numsamples )
	    {
	      fprintf (stderr, "[%s] Number of samples in channel header != data section\n", seisanfile);
	      fprintf (stderr, "  Header: %d, Data section: %d\n", msr->samplecnt, msr->numsamples);
	    }

	  /* Make sure we have 32-bit integers in host byte order */
	  if ( datasamplesize == 2 )
	    {
	      msr->datasamples = samplebuffer;

	      if ( (datalen * 2) > maxsamplebufferlen )
		{
		  if ( (samplebuffer = realloc (samplebuffer, (datalen*2))) == NULL )
		    {
		      fprintf (stderr, "Error allocating memory for sample buffer\n");
		      break;
		    }
		  else
		    maxsamplebufferlen = datalen * 2;
		}
	      
	      sampleptr2 = (int16_t *) data;
	      sampleptr4 = samplebuffer;
	      numsamples = msr->numsamples;
	      
	      /* Convert to 32-bit and swap data samples if needed */
	      while (numsamples--)
		{
		  if ( swapflag )
		    gswap2a (sampleptr2);
		  
		  *(sampleptr4++) = *(sampleptr2++);
		}
	    }
	  else if ( datasamplesize == 4 )
	    {
	      msr->datasamples = data;
	      
	      /* Swap data samples if needed */
	      if ( swapflag )
		{
		  sampleptr4 = (int32_t *) msr->datasamples;
		  numsamples = msr->numsamples;
		  
		  while (numsamples--)
		    gswap4a (sampleptr4++);
		}
	      
	      if ( verbose > 1 && encoding == 1 )
		fprintf (stderr, "WARNING: attempting to pack 32-bit integers into 16-bit encoding\n");
	    }
	  else
	    {
	      fprintf (stderr, "Error, unknown data sample size: %d\n", datasamplesize);
	      break;
	    }
	  
	  /* Add data to TraceGroup */
	  msr->sampletype = 'i';
	  
	  if ( verbose > 1 )
	    {
	      fprintf (stderr, "[%s] %d samps @ %.6f Hz for N: '%s', S: '%s', L: '%s', C: '%s'\n",
		       seisanfile, msr->numsamples, msr->samprate,
		       msr->network, msr->station,  msr->location, msr->channel);
	    }
	  
	  if ( ! (mst = mst_addmsrtogroup (mstg, msr, -1.0, -1.0)) )
	    {
	      fprintf (stderr, "[%s] Error adding samples to TraceGroup\n", seisanfile);
	    }
	  
	  /* Create an MSrecord template for the Trace by copying the current holder */
	  if ( ! mst->private )
	    {
	      mst->private = malloc (sizeof(MSrecord));
	    }

	  memcpy (mst->private, msr, sizeof(MSrecord));
	  
	  /* If a blockette 100 is requested add it */
	  if ( srateblkt )
	    {
	      memset (&Blkt100, 0, sizeof(struct blkt_100_s));
	      Blkt100.samprate = (float) msr->samprate;
	      msr_addblockette ((MSrecord *) mst->private, (char *) &Blkt100,
				sizeof(struct blkt_100_s), 100, 0);
	    }
	  
	  /* Create a FSDH for the template */
	  if ( ! ((MSrecord *)mst->private)->fsdh )
	    {
	      ((MSrecord *)mst->private)->fsdh = malloc (sizeof(struct fsdh_s));
	      memset (((MSrecord *)mst->private)->fsdh, 0, sizeof(struct fsdh_s));
	    }
	  
	  /* Set bit 7 (time tag questionable) in the data quality flags appropriately */
	  if ( uctimeflag )
	    ((MSrecord *)mst->private)->fsdh->dq_flags |= 0x80;
	  else
	    ((MSrecord *)mst->private)->fsdh->dq_flags &= ~(0x80);
	  
	  /* Unless buffering all files in memory pack any Traces now */
	  if ( ! bufferall )
	    {
	      packtraces (1);
	      packedtraces += mstg->numtraces;
	      mst_initgroup (mstg);
	    }
	  
	  /* Cleanup and reset state */
	  msr->datasamples = 0;
	  msr = msr_init (msr);
	  
	  expectheader = 1;
	  expectdata = 0;
	  datalen = 0;
	  continue;
	}
    }
  
  fclose (ifp);
  
  if ( ofp  && ! outputfile )
    {
      fclose (ofp);
      ofp = 0;
    }
  
  if ( data )
    free (data);

  if ( samplebuffer )
    free (samplebuffer);
  
  if ( record )
    free (record);
  
  if ( msr )
    msr_free (&msr);
  
  return 0;
}  /* End of seisan2group() */


/***************************************************************************
 * detectformat:
 *
 * Detect the format and byte order of the specified SeisAn data file.
 *
 * Returns 0 on sucess and -1 on failure.
 ***************************************************************************/
static int
detectformat (FILE *ifp, char *formatflag, char *swapflag, char *seisanfile)
{
  int32_t ident;
  
  /* Read the first four bytes into ident */
  if ( fread (&ident, 4, 1, ifp) < 1 )
    {
      return -1;
    }
  
  /* Rewind the file position pointer to the beginning */
  rewind (ifp);
  
  /* If the first character is a 'K' assume the PC version <= 6.0
   * format, otherwise test if the ident is (80) with either byte
   * order which indicates the Sun/Linux and later PC versions
   * format. */
  if ( *(char*)&ident == 'K' )
    {
      *formatflag = 1;
      
      /* The PC <= 6.0 format should always be little-endian data */
      *swapflag = (ms_bigendianhost()) ? 1 : 0;
      
      return 0;
    }
  
  /* Test if the ident is 80 */
  if ( ident == 80 )
    {
      *formatflag = 4;
      *swapflag = 0;

      return 0;
    }
  
  /* Swap and test if the ident is 80 */
  gswap4 ( &ident );
  if ( ident == 80 )
    {
      *formatflag = 4;
      *swapflag = 1;

      return 0;
    }
  
  return -1;
}  /* End of detectformat() */


/***************************************************************************
 * translatechan:
 *
 * Translate a SeisAn componet to a SEED channel and location.  If
 * none of the specified translations match, the default translation is:
 *
 * channel: the first 2 and fourth characters of the component.  If
 * the 2nd character of the component is a space but the first and
 * fourth are not 'H' will be substituted for the space.
 *
 * location: the 3rd character of the component followed by a '0'.  If
 * the 3rd character of the component is a space it will be translated
 * to a '0'.
 *
 * Examples:
 * Component Chan    Loc
 * 'S  Z' -> 'SHZ'   '00'
 * 'SS Z' -> 'SSZ'   '00'
 * 'S IZ' -> 'SHZ'   'I0'
 * 'SBIZ' -> 'SBZ'   'I0'
 *
 * Returns a 0 on success and -1 on failure.
 ***************************************************************************/
static int
translatechan (char *component, char *channel, char *location)
{
  struct listnode *clp;
  
  strncpy (location, "00", 3);
  
  /* Check user defined translations */
  clp = chanlist;
  while ( clp != 0 )
    {
      if ( ! strcmp (component, clp->key) )
	{
	  strncpy (channel, clp->data, 6);
	  return 0;
	}
      
      clp = clp->next;
    }
  
  /* Default translation, described above */
  
  /* First 2 and fourth characters become the channel */
  memcpy (channel, component, 2);
  memcpy (channel+2, component+3, 1);
  memset (channel+3, 0, 1);
  
  /* 2nd channel character space->H if the others are not blank */
  if ( channel[0] != ' ' && channel[2] != ' ' && channel[1] == ' ' )
    channel[1] = 'H';
  
  /* If 3rd component character is not blank put it in the location code */
  if ( component[2] != ' ' )
    location[0] = component[2];
  
  return 0;
}  /* End of translatechan() */


/***************************************************************************
 * parameter_proc:
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure.
 ***************************************************************************/
static int
parameter_proc (int argcount, char **argvec)
{
  int optind;

  /* Process all command line arguments */
  for (optind = 1; optind < argcount; optind++)
    {
      if (strcmp (argvec[optind], "-V") == 0)
	{
	  fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);
	  exit (0);
	}
      else if (strcmp (argvec[optind], "-h") == 0)
	{
	  usage();
	  exit (0);
	}
      else if (strncmp (argvec[optind], "-v", 2) == 0)
	{
	  verbose += strspn (&argvec[optind][1], "v");
	}
      else if (strcmp (argvec[optind], "-S") == 0)
	{
	  srateblkt = 1;
	}
      else if (strcmp (argvec[optind], "-B") == 0)
	{
	  bufferall = 1;
	}
      else if (strcmp (argvec[optind], "-n") == 0)
	{
	  forcenet = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-l") == 0)
	{
	  forceloc = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-r") == 0)
	{
	  packreclen = atoi (getoptval(argcount, argvec, optind++));
	}
      else if (strcmp (argvec[optind], "-e") == 0)
	{
	  encoding = atoi (getoptval(argcount, argvec, optind++));
	}
      else if (strcmp (argvec[optind], "-b") == 0)
	{
	  byteorder = atoi (getoptval(argcount, argvec, optind++));
	}
      else if (strcmp (argvec[optind], "-o") == 0)
	{
	  outputfile = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-T") == 0)
	{
	  addmapnode (&chanlist, getoptval(argcount, argvec, optind++));
	}
      else if (strncmp (argvec[optind], "-", 1) == 0 &&
	       strlen (argvec[optind]) > 1 )
	{
	  fprintf(stderr, "Unknown option: %s\n", argvec[optind]);
	  exit (1);
	}
      else
	{
	  addnode (&filelist, NULL, argvec[optind]);
	}
    }

  /* Make sure an output file is specified if buffering all */
  if ( bufferall && ! outputfile )
    {
      fprintf (stderr, "Need to specify output file with -o if using -B\n");
      exit(1);
    }
  
  /* Make sure an input files were specified */
  if ( filelist == 0 )
    {
      fprintf (stderr, "No input files were specified\n\n");
      fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
      fprintf (stderr, "Try %s -h for usage\n", PACKAGE);
      exit (1);
    }

  /* Report the program version */
  if ( verbose )
    fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);

  /* Check the input files for any list files, if any are found
   * remove them from the list and add the contained list */
  if ( filelist )
    {
      struct listnode *prevln, *ln;
      char *lfname;
      
      prevln = ln = filelist;
      while ( ln != 0 )
	{
	  lfname = ln->data;

	  if ( *lfname == '@' || ! strcasecmp (lfname,"filenr.lis") )
	    {
	      /* Remove this node from the list */
	      if ( ln == filelist )
		filelist = ln->next;
	      else
		prevln->next = ln->next;
	      
	      /* Skip the '@' first character */
	      if ( *lfname == '@' )
		lfname++;

	      /* Read list file */
	      readlistfile (lfname);
	      
	      /* Free memory for this node */
	      if ( ln->key )
		free (ln->key);
	      free (ln->data);
	      free (ln);
	    }
	  else
	    {
	      prevln = ln;
	    }
	  
	  ln = ln->next;
	}
    }

  return 0;
}  /* End of parameter_proc() */


/***************************************************************************
 * getoptval:
 * Return the value to a command line option; checking that the value is 
 * itself not an option (starting with '-') and is not past the end of
 * the argument list.
 *
 * argcount: total arguments in argvec
 * argvec: argument list
 * argopt: index of option to process, value is expected to be at argopt+1
 *
 * Returns value on success and exits with error message on failure
 ***************************************************************************/
static char *
getoptval (int argcount, char **argvec, int argopt)
{
  if ( argvec == NULL || argvec[argopt] == NULL ) {
    fprintf (stderr, "getoptval(): NULL option requested\n");
    exit (1);
    return 0;
  }
  
  /* Special case of '-o -' usage */
  if ( (argopt+1) < argcount && strcmp (argvec[argopt], "-o") == 0 )
    if ( strcmp (argvec[argopt+1], "-") == 0 )
      return argvec[argopt+1];
  
  if ( (argopt+1) < argcount && *argvec[argopt+1] != '-' )
    return argvec[argopt+1];
  
  fprintf (stderr, "Option %s requires a value\n", argvec[argopt]);
  exit (1);
  return 0;
}  /* End of getoptval() */


/***************************************************************************
 * readlistfile:
 * Read a list of files from a file and add them to the filelist for
 * input data.
 *
 * Returns the number of file names parsed from the list or -1 on error.
 ***************************************************************************/
static int
readlistfile (char *listfile)
{
  FILE *fp;
  char line[1024];
  char *ptr;
  char lineformat;  /* 0 = simple text, 1 = dirf (filenr.lis) */
  int  filecnt = 0;
  int  nonspace;

  int  filenr;
  char filename[1024];
  int  fields;
  
  /* Open the list file */
  if ( (fp = fopen (listfile, "rb")) == NULL )
    {
      if (errno == ENOENT)
        {
          fprintf (stderr, "Could not find list file %s\n", listfile);
          return -1;
        }
      else
        {
          fprintf (stderr, "Error opening list file %s: %s\n",
		   listfile, strerror (errno));
          return -1;
        }
    }
  
  if ( verbose )
    fprintf (stderr, "Reading list of input files from %s\n", listfile);
  
  while ( (fgets (line, sizeof(line), fp)) !=  NULL)
    {
      /* Truncate line at first \r or \n and count non-space characters */
      nonspace = 0;
      ptr = line;
      while ( *ptr )
	{
	  if ( *ptr == '\r' || *ptr == '\n' || *ptr == '\0' )
	    {
	      *ptr = '\0';
	      break;
	    }
	  else if ( *ptr != ' ' )
	    {
	      nonspace++;
	    }
	  
	  ptr++;
	}
      
      /* Skip empty lines */
      if ( nonspace == 0 )
	continue;
      
      /* Detect line format by checking first character for # */
      lineformat = 0;
      ptr = line;
      while ( *ptr )
	{
	  if ( *ptr == ' ' )
	    {
	      ptr++;
	      continue;
	    }
	  
	  if ( *ptr == '#' )
	    {
	      lineformat = 1;
	      break;
	    }
	  else
	    {
	      lineformat = 0;
	      break;
	    }
	}
      /* 'ptr' should now point to the first non-space character */
      
      if ( ! *ptr )
	continue;
      
      if ( lineformat == 0 )
	{
	  fields = sscanf (ptr, "%s", filename);
	  
	  if ( fields != 1 )
	    {
	      fprintf (stderr, "Error parsing filename from: %s\n", line);
	      continue;
	    }
	  
	  if ( verbose > 1 )
	    fprintf (stderr, "Adding '%s' to input file list\n", filename);
	  
	  addnode (&filelist, NULL, filename);
	  filecnt++;
	  
	  continue;
	}
      
      if ( lineformat == 1 )
	{
	  fields = sscanf (ptr, "#%5d %s", &filenr, filename);
	  
	  if ( fields != 2 )
	    {
	      fprintf (stderr, "Error parsing file number and name from: %s\n", line);
	      continue;
	    }
	  
	  if ( verbose > 1 )
	    fprintf (stderr, "Adding (%d) '%s' to input file list\n", filenr, filename);
	  
	  addnode (&filelist, NULL, filename);
	  filecnt++;
	  
	  continue;
	}
    }

  fclose (fp);

  return filecnt;
}  /* End readlistfile() */


/***************************************************************************
 * addnode:
 *
 * Add node to the specified list.
 ***************************************************************************/
static void
addnode (struct listnode **listroot, char *key, char *data)
{
  struct listnode *lastlp, *newlp;
  
  if ( data == NULL )
    {
      fprintf (stderr, "addnode(): No file name specified\n");
      return;
    }
  
  lastlp = *listroot;
  while ( lastlp != 0 )
    {
      if ( lastlp->next == 0 )
        break;
      
      lastlp = lastlp->next;
    }
  
  newlp = (struct listnode *) malloc (sizeof (struct listnode));
  memset (newlp, 0, sizeof (struct listnode));
  if ( key ) newlp->key = strdup(key);
  else newlp->key = key;
  if ( data) newlp->data = strdup(data);
  else newlp->data = data;
  newlp->next = 0;
  
  if ( lastlp == 0 )
    *listroot = newlp;
  else
    lastlp->next = newlp;
  
}  /* End of addnode() */


/***************************************************************************
 * addmapnode:
 *
 * Add a node to a list deriving the key and data from the supplied
 * mapping string: 'key=data'.
 ***************************************************************************/
static void
addmapnode (struct listnode **listroot, char *mapping)
{
  char *key;
  char *data;

  key = mapping;
  data = strchr (mapping, '=');
  
  if ( ! data )
    {
      fprintf (stderr, "addmapmnode(): Cannot find '=' in mapping '%s'\n", mapping);
      return;
    }

  *data++ = '\0';
  
  /* Add to specified list */
  addnode (listroot, key, data);
  
}  /* End of addmapnode() */


/***************************************************************************
 * record_handler:
 * Saves passed records to the output file.
 ***************************************************************************/
static void
record_handler (char *record, int reclen)
{
  if ( fwrite(record, reclen, 1, ofp) != 1 )
    {
      fprintf (stderr, "Error writing to output file\n");
    }
}  /* End of record_handler() */


/***************************************************************************
 * usage:
 * Print the usage message and exit.
 ***************************************************************************/
static void
usage (void)
{
  fprintf (stderr, "%s version: %s\n\n", PACKAGE, VERSION);
  fprintf (stderr, "Convert SeisAn waveform data to Mini-SEED.\n\n");
  fprintf (stderr, "Usage: %s [options] file1 [file2 file3 ...]\n\n", PACKAGE);
  fprintf (stderr,
	   " ## Options ##\n"
	   " -V             Report program version\n"
	   " -h             Show this usage message\n"
	   " -v             Be more verbose, multiple flags can be used\n"
	   " -S             Include SEED blockette 100 for very irrational sample rates\n"
           " -B             Buffer data before packing, default packs at end of each block\n"
	   " -n netcode     Specify the SEED network code, default is blank\n"
	   " -l loccode     Specify the SEED location code, default is blank\n"
	   " -r bytes       Specify record length in bytes for packing, default: 4096\n"
	   " -e encoding    Specify SEED encoding format for packing, default: 11 (Steim2)\n"
	   " -b byteorder   Specify byte order for packing, MSBF: 1 (default), LSBF: 0\n"
	   " -o outfile     Specify the output file, default is <inputfile>_MSEED\n"
	   "\n"
	   " -T comp=chan   Specify component-channel mapping, can be used many times\n"
	   "                  e.g.: \"-T SBIZ=SHZ -T SBIN=SHN -T SBIE=SHE\"\n"
	   "                  spaces must be quoted: \"-T 'S  Z'=SLZ\"\n"
	   "\n"
	   " file(s)        File(s) of SeisAn input data\n"
	   "                  If a file is prefixed with an '@' it is assumed to contain\n"
	   "                  a list of data files to be read.  This list can either be\n"
	   "                  a simple text list or in the 'dirf' (filenr.lis) format.\n"
	   "\n"
	   "Supported Mini-SEED encoding formats:\n"
	   " 1  : 16-bit integers (only works if samples can be represented in 16-bits)\n"
	   " 3  : 32-bit integers\n"
	   " 10 : Steim 1 compression\n"
	   " 11 : Steim 2 compression\n"
	   "\n");
}  /* End of usage() */
