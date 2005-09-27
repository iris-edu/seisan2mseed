/***************************************************************************
 * seisan2mseed.c
 *
 * Simple waveform data conversion from SeisAn (ver >= 7.0) to Mini-SEED.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified 2005.270
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#include <libmseed.h>

#define VERSION "0.1"
#define PACKAGE "seisan2mseed"

struct listnode {
  char *key;
  char *data;
  struct listnode *next;
};

static int seisan2group (char *seisanfile, TraceGroup *mstg);
static char swaptest (int32_t testint, int32_t value, char *message);
static char *translatechan (char *component);
static int parameter_proc (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static void addnode (struct listnode **listroot, char *key, char *data);
static void addmapnode (struct listnode **listroot, char *mapping);
static void record_handler (char *record, int reclen);
static void usage (void);
static void term_handler (int sig);

static int   verbose     = 0;
static int   packreclen  = -1;
static int   encoding    = -1;
static int   byteorder   = -1;
static char  srateblkt   = 0;
static char *forcenet    = 0;
static char *forceloc    = 0;
static char *outputfile  = 0;
static FILE *ofp         = 0;

/* A list of input files */
struct listnode *filelist = 0;

/* A list of component to channel translations */
struct listnode *chanlist = 0;

int
main (int argc, char **argv)
{
  struct listnode *flp;
  TraceGroup *mstg = 0;
  Trace *mst;

  int packedsamples = 0;
  int trpackedsamples = 0;
  int packedrecords = 0;
  int trpackedrecords = 0;
  
  /* Signal handling, use POSIX calls with standardized semantics */
  struct sigaction sa;
  
  sa.sa_flags = SA_RESTART;
  sigemptyset (&sa.sa_mask);
  
  sa.sa_handler = term_handler;
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGQUIT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  
  sa.sa_handler = SIG_IGN;
  sigaction (SIGHUP, &sa, NULL);
  sigaction (SIGPIPE, &sa, NULL);

  /* Process given parameters (command line and parameter file) */
  if (parameter_proc (argc, argv) < 0)
    return -1;
  
  /* Init TraceGroup */
  mstg = mst_initgroup (mstg);
  
  /* Open the output file if specified otherwise stdout */
  if ( outputfile )
    {
      if ( strcmp (outputfile, "-") == 0 )
        {
          ofp = stdout;
        }
      else if ( (ofp = fopen (outputfile, "w")) == NULL )
        {
          fprintf (stderr, "Cannot open output file: %s (%s)\n",
                   outputfile, strerror(errno));
          return -1;
        }
    }
  else
    {
      ofp = stdout;
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

  /* Pack data into Mini-SEED records using per-Trace templates */
  packedrecords = 0;
  mst = mstg->traces;
  while ( mst )
    {
      trpackedrecords = mst_pack (mst, &record_handler, packreclen, encoding, byteorder,
				  &trpackedsamples, 1, verbose-1, (MSrecord *) mst->private);
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
  
  fprintf (stderr, "Packed %d trace(s) of %d samples into %d records\n",
	   mstg->numtraces, packedsamples, packedrecords);
  
  /* Make sure everything is cleaned up */
  mst_freegroup (&mstg);
  
  if ( ofp )
    fclose (ofp);
  
  return 0;
}  /* End of main() */


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
  FILE *ifp;
  MSrecord *msr = 0;
  Trace *mst;
  struct blkt_100_s Blkt100;
  
  char *record = 0;
  size_t recordbufsize = 0;
  
  int32_t reclen;
  int32_t reclenmirror;
  
  size_t readoff;
  size_t readlen;
  
  char component[5];

  long year;
  char timestr[30];
  char ratestr[10];
  char sampstr[10];
  
  char *cat, *mouse;

  char uctimeflag;
  
  char gainstr[15];
  char gainflag;
  double gain = 1.0;
  
  off_t filepos;

  char swapflag = -1;
  char expectdata = 0;
  char done;

  /* Open input file */
  if ( (ifp = fopen (seisanfile, "r")) == NULL )
    {
      fprintf (stderr, "Cannot open input file: %s (%s)\n",
	       seisanfile, strerror(errno));
      return -1;
    }
  
  if ( ! (msr = msr_init(msr)) )
    {
      fprintf (stderr, "Cannot initialize MSrecord strcture\n");
      return -1;
    }
  
  /* Read a record at a time */
  readoff = 0;
  readlen = 0;
  done = 0;
  while ( ! done )
    {
      /* Get current file position */
      filepos = ftello (ifp);
      
      /* Read next record length */
      if ( (readlen = fread (&reclen, 4, 1, ifp)) < 1 )
	{
	  if ( ferror(ifp) )
	    fprintf (stderr, "Error reading file %s\n", seisanfile);
	  break;
	}
      
      /* Test if byte swapping is needed */
      if ( swapflag < 0 )
	if ( (swapflag = swaptest (reclen, 80, seisanfile)) < 0 )
	  break;
      
      if ( swapflag ) gswap4 ( &reclen );
      
      if ( verbose > 1 )
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
      
      /* Read previous record length */
      if ( (readlen = fread (&reclenmirror, 4, 1, ifp)) < 1 )
	{
	  if ( ferror(ifp) )
	    fprintf (stderr, "Error reading file %s\n", seisanfile);
	  break;
	}
      
      if ( swapflag ) gswap4 ( &reclenmirror );
      
      if ( reclen != reclenmirror )
	{
	  fprintf (stderr, "At byte offset %lld in %s:\n", (long long) filepos, seisanfile);
	  fprintf (stderr, "  Next and previous record length values do not match: %d != %d\n",
		   reclen, reclenmirror);
	  break;
	}
      
      /* Check if this is a channel header: length == 1040 and first char not a space */
      if ( ! expectdata && reclen == 1040 && *record != ' ' )
	{
	  ms_strncpclean (msr->network, forcenet, 2);
	  ms_strncpclean (msr->station, record, 5);
	  ms_strncpclean (msr->location, forceloc, 2);
	  
	  /* Map component to SEED channel */
	  memset (component, 0, sizeof(component));
	  memcpy (component, record + 5, 4);
	  
	  strcpy (msr->channel, translatechan(component));
	  
	  /* Construct time string */
	  memset (timestr, 0, sizeof(timestr));
	  memcpy (timestr, record + 9, 3);
	  year = strtoul (timestr, NULL, 10);
	  year += 1900;
	  sprintf (timestr, "%4ld", year);
	  
	  strcat (timestr, ",");
	  strncat (timestr, record + 13, 3);
          strcat (timestr, ",");
	  strncat (timestr, record + 23, 2);
          strcat (timestr, ":");
	  strncat (timestr, record + 26, 2);
          strcat (timestr, ":");
	  strncat (timestr, record + 29, 6);
	  
	  /* Remove spaces */
	  cat = mouse = timestr;
	  while ( *mouse )
	    if ( *mouse++ != ' ' )
	      *cat++ = *(mouse-1);
	  *cat = '\0';
	  
	  msr->starttime = ms_seedtimestr2hptime (timestr);

	  /* Parse sample rate */
	  memset (ratestr, 0, sizeof(ratestr));
	  memcpy (ratestr, record + 36, 7);
	  msr->samprate = strtod (ratestr, NULL);
	  
	  /* Parse sample count */
          memset (sampstr, 0, sizeof(sampstr));
          memcpy (sampstr, record + 43, 7);
	  msr->samplecnt = strtoul (sampstr, NULL, 10);

	  /* Detect uncertain time */
	  uctimeflag = ( *(record+28) == 'E' ) ? 1 : 0;
	  
	  /* Detect gain */
	  gainflag = ( *(record+75) == 'G' ) ? 1 : 0;
	  if ( gainflag )
	    {
	      memset (gainstr, 0, sizeof(gainstr));
	      memcpy (gainstr, record + 147, 12);
	      gain = strtod (gainstr, NULL);

	      fprintf (stderr, "Gain of %f detected\n", gain);
	      fprintf (stderr, "Gain NOT applied, no support for that yet!\n");
	    }
	  
	  if ( verbose )
	    fprintf (stderr, "[%s] '%s_%s' (%s): %s%s, %d samps @ %.4f Hz\n",
		     seisanfile, msr->station, component, msr->channel,
		     timestr, (uctimeflag) ? " [UNCERTAIN]" : "",
		     msr->samplecnt, msr->samprate);
	  
	  expectdata = 1;
	  continue;
	}
      
      if ( expectdata )
	{  
	  /* Add data to TraceGroup */
	  msr->datasamples = record;
	  msr->numsamples = reclen / 4;  /* Assuming 4-byte integers */
	  msr->sampletype = 'i';
	  
	  if ( msr->samplecnt != msr->numsamples )
	    fprintf (stderr, "[%s] Number of samples in channel header != data section\n", seisanfile);
	  
	  /* Swap data samples if needed */
	  if ( swapflag )
	    {
	      int32_t *sampptr = (int32_t *) &record;
	      int32_t numsamples = msr->numsamples;
	      
	      while (numsamples--)
		gswap4a (sampptr++);
	    }

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
	  
	  /* Create an MSrecord template for the Trace if requested by copying
	   * the current MSrecord holder and adding a blockette 100. */
	  if ( srateblkt && ! mst->private )
	    {
	      mst->private = malloc (sizeof(MSrecord));
	      memcpy (mst->private, msr, sizeof(MSrecord));
	      
	      memset (&Blkt100, 0, sizeof(struct blkt_100_s));
	      Blkt100.samprate = (float) msr->samprate;
	      msr_addblockette ((MSrecord *) mst->private, (char *) &Blkt100,
				sizeof(struct blkt_100_s), 100, 0);
	    }
	  
	  /* Cleanup and reset state */
	  msr->datasamples = 0;
	  msr = msr_init (msr);
	  
	  expectdata = 0;
	}
    }
  
  if ( record )
    free (record);
  
  if ( msr )
    msr_free (&msr);
  
  return 0;
}  /* End of seisan2group() */


/***************************************************************************
 * swaptest:
 * Compare the test integer to the value, if it does not equate swap
 * the test integer and compare again.
 *
 * Returns 0 if swapping is not needed, 1 if swapping is needed and -1
 * on failure.
 ***************************************************************************/
static char
swaptest (int32_t testint, int32_t value, char *message)
{
  
  if ( testint == value )
    {
      if ( verbose > 1 )
	fprintf (stderr, "Swapping NOT needed for %s\n", message);
      return 0;
    }
  
  gswap4 ( &testint );

  if ( testint == value )
    {
      if ( verbose > 1 )
	fprintf (stderr, "Swapping needed for %s\n", message);
      return 1;
    }

  fprintf (stderr, "Could not detect byte order of data for %s\n", message);

  return -1;
}  /* End of swaptest() */


/***************************************************************************
 * translatechan:
 *
 * Translate a SeisAn componet to a SEED channel.  If none of the
 * hardcoded translations match the default action is to return a 3
 * character channel composed of the first 2 and fourth characters of
 * the component.  If the 2nd character of the component is a space
 * but the first and fourth are not 'H' will be substituted for the space.
 *
 * Returns a pointer to a channel code on success and NULL on failure.
 ***************************************************************************/
static char *
translatechan (char *component)
{
  static char channel[4];
  struct listnode *clp;
  
  /* Check user defined translations */
  clp = chanlist;
  while ( clp != 0 )
    {
      if ( ! strcmp (component, clp->key) )
	return clp->data;
      
      clp = clp->next;
    }
  
  /* Default translation:
   * Create 3 character channel from first 2 and fourth character of component,
   * if the 2nd character is a space but the first and fourth are not substitute
   * 'H' for the space, for example: 'S  Z' -> 'SHZ'
   */
  memcpy (channel, component, 2);
  memcpy (channel+2, component+3, 1);
  channel[3] = '\0';
  
  if ( channel[0] != ' ' && channel[2] != ' ' && channel[1] == ' ' )
    channel[1] = 'H';
  
  return channel;
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
  }
  
  /* Special case of '-o -' usage */
  if ( (argopt+1) < argcount && strcmp (argvec[argopt], "-o") == 0 )
    if ( strcmp (argvec[argopt+1], "-") == 0 )
      return argvec[argopt+1];
  
  if ( (argopt+1) < argcount && *argvec[argopt+1] != '-' )
    return argvec[argopt+1];
  
  fprintf (stderr, "Option %s requires a value\n", argvec[argopt]);
  exit (1);
}  /* End of getoptval() */


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
  fprintf (stderr, "Convert SeisAn (>= 7.0) waveform data to Mini-SEED.\n\n");
  fprintf (stderr, "Usage: %s [options] file1 file2 file3...\n\n", PACKAGE);
  fprintf (stderr,
	   " ## Options ##\n"
	   " -V             Report program version\n"
	   " -h             Show this usage message\n"
	   " -v             Be more verbose, multiple flags can be used\n"
	   " -S             Include SEED blockette 100 for very irrational sample rates\n"
	   " -n netcode     Specify the SEED network code, default is blank\n"
	   " -l loccode     Specify the SEED location code, default is blank\n"
	   " -r bytes       Specify record length in bytes for packing, default: 4096\n"
	   " -e encoding    Specify SEED encoding format for packing, default: 11 (Steim2)\n"
	   " -b byteorder   Specify byte order for packing, MSBF: 1 (default), LSBF: 0\n"
	   " -o outfile     Specify the output file, default is stdout.\n"
	   "\n"
	   " -T comp=chan   Specify component-channel mapping, can be used many times\n"
	   "                  e.g.: '-T SBIZ=SHZ -T SBIN=SHN -T SBIE=SHE'\n"
	   "\n"
	   " file(s)        File(s) of SeisAn input data\n"
	   "\n"
	   "Supported Mini-SEED encoding formats:\n"
	   " 1  : 16-bit integers (only works if samples can be represented in 16-bits)\n"
	   " 3  : 32-bit integers\n"
	   " 10 : Steim 1 compression\n"
	   " 11 : Steim 2 compression\n"
	   "\n");
}  /* End of usage() */


/***************************************************************************
 * term_handler:
 * Signal handler routine.
 ***************************************************************************/
static void
term_handler (int sig)
{
  exit (0);
}
