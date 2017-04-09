# <p >SeisAn to Mini-SEED converter</p>

1. [Name](#)
1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [List Files](#list-files)
1. [About Seisan](#about-seisan)
1. [Author](#author)

## <a id='synopsis'>Synopsis</a>

<pre >
seisan2mseed [options] file1 [file2 file3 ...]
</pre>

## <a id='description'>Description</a>

<p ><b>seisan2mseed</b> converts SeisAn waveform data files to Mini-SEED. One or more input files may be specified on the command line.  If an input file name is prefixed with an '@' character or explicitly named 'filenr.lis' the file is assumed to contain a list of input data files, see <i>LIST FILES</i> below.</p>

<p >The default translation of SeisAn components to SEED channel codes is as follows: a 3 character SEED channel is composed of the first, second and fourth characters of the component; furthermore if the second character is a space and the first and fourth are not spaces an 'H' is substituted for the 2nd character (i.e. 'S__Z' -> 'SHZ'). The default SEED location code is '00', if the third character of the SeisAn component is not a space it will be placed in the first character of the SEED location code.  Other translations may be explicitly specified using the -T command line option.</p>

<p >If the input file name is a standard SeisAn file name the default output file name will be the same with the 'S' at character 19 replaced by an 'M'.  Otherwise the output file name is the input file name with a "_MSEED" suffix.  The output data may be re-directed to a single file or stdout using the -o option.</p>

## <a id='options'>Options</a>

<b>-V</b>

<p style="padding-left: 30px;">Print program version and exit.</p>

<b>-h</b>

<p style="padding-left: 30px;">Print program usage and exit.</p>

<b>-v</b>

<p style="padding-left: 30px;">Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.</p>

<b>-S</b>

<p style="padding-left: 30px;">Include SEED blockette 100 in each output record with the sample rate in floating point format.  The basic format for storing sample rates in SEED data records is a rational approximation (numerator/denominator).  Precision will be lost if a given sample rate cannot be well approximated.  This option should be used in those cases.</p>

<b>-B</b>

<p style="padding-left: 30px;">Buffer all input data into memory before packing it into Mini-SEED records.  The host computer must have enough memory to store all of the data.  By default the program will flush it's data buffers after each input block is read.  An output file must be specified with the -o option when using this option.</p>

<b>-rfy</b>

<p style="padding-left: 30px;">Retain far future time stamps.  By default the converter will shift all data time stamps beyond the year 2050 to the year 2050 in order to maximize compatibility for miniSEED readers.  This option negates this default behavior and leaves far future dates as is.</p>

<b>-n </b><i>netcode</i>

<p style="padding-left: 30px;">Specify the SEED network code to use, if not specified the network code will be blank.  It is highly recommended to specify a network code.</p>

<b>-l </b><i>loccode</i>

<p style="padding-left: 30px;">Specify the SEED location code to use, if not specified the location code will be blank.</p>

<b>-r </b><i>bytes</i>

<p style="padding-left: 30px;">Specify the Mini-SEED record length in <i>bytes</i>, default is 4096.</p>

<b>-e </b><i>encoding</i>

<p style="padding-left: 30px;">Specify the Mini-SEED data encoding format, default is 11 (Steim-2 compression).  Other supported encoding formats include 10 (Steim-1 compression), 1 (16-bit integers) and 3 (32-bit integers).  The 16-bit integers encoding should only be used if all data samples can be represented in 16 bits.</p>

<b>-b </b><i>byteorder</i>

<p style="padding-left: 30px;">Specify the Mini-SEED byte order, default is 1 (big-endian or most significant byte first).  The other option is 0 (little-endian or least significant byte first).  It is highly recommended to always create big-endian SEED.</p>

<b>-o </b><i>outfile</i>

<p style="padding-left: 30px;">Write all Mini-SEED records to <i>outfile</i>, if <i>outfile</i> is a single dash (-) then all Mini-SEED output will go to stdout.  All diagnostic output from the program is written to stderr and should never get mixed with data going to stdout.</p>

<b>-T </b><i>comp=chan</i>

<p style="padding-left: 30px;">Specify an explicit SeisAn component to SEED channel mapping, this option may be used several times (e.g. "-T SBIZ=SHZ -T SBIN=SHN -T SBIE=SHE").  Spaces in components must be quoted, i.e. "-T 'S  Z'=SHZ".</p>

## <a id='list-files'>List Files</a>

<p >If an input file is prefixed with an '@' character the file is assumed to contain a list of file for input.  As a special case an input file named 'filenr.lis' is always assumed to be a list file.  Multiple list files can be combined with multiple input files on the command line.</p>

<p >The last, space separated field on each line is assumed to be the file name to be read.  This accommodates both simple text, with one file per line, or the formats created by the SeisAn dirf command (filenr.lis).</p>

<p >An example of a simple text list:</p>

<pre >
2003-06-20-0643-41S.EDI___003
2005-07-23-1452-04S.CER___030
</pre>

<p >An example of an equivalent list in the dirf (filenr.lis) format:</p>

<pre >
 #  1  2003-06-20-0643-41S.EDI___003
 #  2  2005-07-23-1452-04S.CER___030
</pre>

## <a id='about-seisan'>About Seisan</a>

<p >SeisAn is a widely used seismic data analysis package available from the University of Bergen, Norway: http://www.geo.uib.no/seismo/</p>

## <a id='author'>Author</a>

<pre >
Chad Trabant
IRIS Data Management Center
</pre>


(man page 2013/02/22)
