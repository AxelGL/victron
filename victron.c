/* victron.c
 * This file is part of kplex
 * Copyright  AxelGL
 * For copying information see the file COPYING distributed with this software
 *
 * This file contains code for Victron Solar Charger. 
   It reads the data from the UART interface and converts it to a NMEA string according to the statement in the kplex.conf file. 
   Example: 
   [victron]
filename=/dev/ttyAMA0
direction=in
baud=19200
eol=rn
strict=no
#nmeastring=V,$SSMTW,C
#nmeastring=V,$IIDPT,0
#nmeastring=I,$SSMTW,C
nmeastring=P,$IIMTW,C
#nmeastring=W,$SSMTW,C
#nmeastring=W,$IIXDR,C

There is a new command nmeastring that configures the NMEA string that is transmitted. 
First value: Can be 
V: Battery Voltage 
I: Battery Current
P: Panel Voltage 
E: Energy harvest from the same day
Y: Energy harvest from yesterday 
O: Energy harvest Overall  

Next is the NMEA String that is used. Depending on the string the data is displayed in the appropriate format:
IIXDR is the default. Others an be used to show data on devices that cannot display IIXDR )which is probably the norm). 
The last letter defines the Unit that is used in the NMEA String. This way a battery Voltage can for example 
be sent as a DPT Value with the unit m and displayed on a display that will only display depth values. It is also possible
to use identical NMEA String for two different values, they will then be displayed  */

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#if defined  __APPLE__ || defined __NetBSD__ || defined __OpenBSD__
#include <util.h>
#elif defined __FreeBSD__
#include <libutil.h>
#else
#include <pty.h>
#endif
#include <grp.h>
#include <pwd.h>
#include <termios.h>

#define DEFSERIALQSIZE 128
#define BUFSIZE 1024

struct if_serial {
    int fd;
    char *slavename;            /* link to pty slave (if it exists) */
    int saved;                  /* Are stored terminal settins valid? */
    struct termios otermios;    /* To restore previous interface settings
                                 *  on exit */
};


struct victron_nmea {
     char nmeastring[7];    //
     char unit;             // Each value can have a different unit to match to receiver
};
      
char nmeadispl_temp = 0xff;
char nmeastring[7];
char nmeatype[9];   // Which nmea data  should be send
char nmeaadd = 0;   // send both nmeatypes or just one

struct victron_nmea nmeastring0;
struct victron_nmea nmeastringv;
struct victron_nmea nmeastringp;
struct victron_nmea nmeastringw;
struct victron_nmea nmeastringi;
struct victron_nmea nmeastringe;
struct victron_nmea nmeastringy;
struct victron_nmea nmeastringo;



//returns delta of char in string
int charstr(char *string, char c) {
  int i;
  for (i=0; i< 10; i++) {
    if (*string == c) return i;  
    string++;                  
  }
  return (-1);
}

/*
 * Read from a serial interface from Victron Controller, comvert to NMEA end put into Buffer
 * Args: pointer to interface structure pointer to buffer
 * Returns: Number of bytes read, zero on error or end of file
 */
int read_victron(int fd, char *buf)
{
  char* ptr=NULL;
  char* bufi;
  char* startblock;
  int ret=0;
  int nodata = 0;
  int number, amount;
  const char* limiter = "\n\t";
#define IBUFSIZE 500
  char bufint[IBUFSIZE];     // Buffer for data from UART

    tcflush(fd, TCIFLUSH);

  do {    // Repeat until data is there
  amount = 0;     // Amount of data read from Victron
  number = 0;     // Number of NMEA char to send to network 

  bufi = buf;
  int i = 0;
  int j = 0;
  unsigned char sum = 1;
  char nmea_set = 0;      // Special NMEA String in kplex.opt requested

            printf("New Start ****************\n");

  while (sum != 0) {  
    amount = 0;
    i = 0;
    // Initialize Buffer
    for (i=0; i< IBUFSIZE; i++)  bufint[i] = 0;

    i= 0;
    do {
        usleep(40000);
        signed char chars_read = read(fd,&bufint[amount],50) ;   // Get approx. two blocks data from input
        if (chars_read <= 0) {
		nodata = 1; 
		break;
	}
	amount += chars_read; 
	bufint [amount + 1] = 0;
    } while (amount<(IBUFSIZE - 51));    //  All we know: Last line is 0x0a, 0x0a, Checksum followed by 0x09, xx
        printf("Data %s amount = %i\n ",&bufint[0], amount);

        //    printf("*****************************************Got data \n");

    bufint[IBUFSIZE] = 0;  // Add eol just in case

    char *checksump;
    if ((startblock = strstr(&bufint[0], "hecksum")) == 0) {
	printf ("No checksum found");
     	nodata = 1;	 
        sum = 0;	
       //return (0);
    }
    else {
       nodata = 0;
       startblock += 9;   // Point to checksumbyte+1 -> Start of valid block
      i = startblock - &bufint[0];   // i is index for first valid byte
       // printf("Index = %i\n",i);
      if ((checksump = strstr(&bufint[i], "hecksum")) == 0) 
         { sum = 0; nodata = 1;  break;}
      checksump += 8;   // Point to checksumbyte

      sum = 0;
      do {
        sum += bufint[i]; 
//printf("%x\n",bufint[i]);
      } while ((&bufint[i++] != checksump))  ;
      if (sum != 0) printf("Checksum wrong = %i ",sum);
    }
  }

            printf("\n**************  Got Data amount = %i\n ", amount);
  

  // Go throught the different possible defined NMEA output strings if not defined, skip
  char valid = 0;

  do {
    switch(nmeadispl_temp) {
      case 0x02:    nmeadispl_temp = 0x04; break;
      case 0x04:    nmeadispl_temp = 0x08; break;
      case 0x08:    nmeadispl_temp = 0x10; break;
      case 0x10:    nmeadispl_temp = 0x20; break;
      case 0x20:    nmeadispl_temp = 0x40; break;
      case 0x40:    nmeadispl_temp = 0x80; break;
      case 0x80:    nmeadispl_temp = 0xff; break;
      case 0xff:    nmeadispl_temp = 0x02; break;
      default:      nmeadispl_temp = 0xff;
    }
    if (nmeadispl_temp == 0x02) valid = nmeastringv.nmeastring[0];     // nmeastrin[0] is 0 if not requested, set to $ in init otherwise
    else if (nmeadispl_temp == 0x04) valid = nmeastringp.nmeastring[0];
    else if (nmeadispl_temp == 0x08) valid = nmeastringi.nmeastring[0];
    else if (nmeadispl_temp == 0x10) valid = nmeastringw.nmeastring[0];
    else if (nmeadispl_temp == 0x20) valid = nmeastringo.nmeastring[0];
    else if (nmeadispl_temp == 0x40) valid = nmeastringe.nmeastring[0];
    else if (nmeadispl_temp == 0x80) valid = nmeastringy.nmeastring[0];
    else if (nmeadispl_temp == 0xff) valid = nmeastring0.nmeastring[0];  // 0xff always runs once to send default $IIXDR NMEA string
  } while (valid == 0); 

      printf("nmeadispl_temp  %x nodata %i\n", nmeadispl_temp, nodata);

  if (nodata == 1) {
	  printf("Nodata\n");
    switch(nmeadispl_temp)  {
	    case 0x02:    sprintf(bufi,"%s,88.8,%c\n\r",nmeastringv.nmeastring,nmeastringv.unit); printf("bufi:%s",bufi); return(15);
	    case 0x04:    sprintf(bufi,"%s,88.8,%c\n\r",nmeastringp.nmeastring,nmeastringp.unit); printf("bufi:%s",bufi); return(15);
      case 0x08:    return(0);
      case 0x10:    return(0);
      case 0x20:    return(0);
      case 0x40:    return(0);
      case 0x80:    return(0);
    case 0xff:    sprintf(bufi,"$IIXDR,U,88.8,V,U1,I,88,mA,U1,P,8.88,V,U1,W,88,W,U1,O,88,Wh,U1,E,88,Wh,U1\n\r"); printf("bufi:%s",bufi); return(76);
      default:      return(0);
    }
	  return (0);
  }

  printf("Startblock: %s Limiter %x\n",startblock+2, limiter[2]);
  ptr = strtok(startblock, limiter);    // PTR contains the next token from the data from the victron after checksum
    //printf("PTR = %s startblock = %x ptr = %x \n",ptr, startblock, ptr);

  while(strcmp(ptr, "Checksum") != NULL)
  {
    //printf("PTR = %s ptr = %x \n",ptr, ptr);
      	  char marker;    //  
    if ((strcmp(ptr, "VPV")==0) && (strlen(ptr) == 3)) marker = 'P';   // Voltage Solar Panel
    else if((strcmp(ptr, "V") == 0) && (strlen(ptr) == 1))  marker = 'V'; // Voltage Battery
    else if ((strcmp(ptr, "I") == 0) && (strlen(ptr) == 1)) marker = 'I'; // Current Battery
    else if ((strcmp(ptr, "PPV")==0) && (strlen(ptr) == 3)) marker = 'W'; // Power SolarPanel
    else if ((strcmp(ptr, "H19")==0) && (strlen(ptr) == 3)) marker = 'O'; // Energy Yield overall 
    else if ((strcmp(ptr, "H20")==0) && (strlen(ptr) == 3)) marker = 'E'; // Energy Yield Today
    else if ((strcmp(ptr, "H22")==0) && (strlen(ptr) == 3)) marker = 'Y'; // Energy Yield Yesterday
    else marker = 0;

    if (((marker == 'V') && (nmeadispl_temp & 0x02)) || ((marker == 'P') && (nmeadispl_temp & 0x04)))    // handle voltage or solarvoltage values with decimal point
    {

      if ((nmeadispl_temp == 0x0ff) && (number < 5)) {    // Just started, put default NMEA string $IIXDR to target string
         strncpy(bufi, nmeastring0.nmeastring, 6);
         number += 6;
         bufi+=6;
      }
      else if ((marker == 'V') && (nmeadispl_temp ==  0x02)) {   // Special NMEAString for V
        strncpy(bufi, nmeastringv.nmeastring, 6);   
         number += 6;
         bufi+=6;
      }
      else if ((marker == 'P') && (nmeadispl_temp ==  0x04)) {   // Special defined NMEA String for P
        strncpy(bufi, nmeastringp.nmeastring, 6);  
         number += 6;
         bufi+=6;
      }

      //DEBUG(9,"bufi = %s", buf);
      ptr = strtok(NULL, limiter);   // look for TAB
      if (ptr!=NULL)  
      {              // Generate NMEA sentence and copy to buffer from mV  $IIXDR,U,x.x,V,V001,checksum
           if ((marker == 'P') && (nmeadispl_temp ==  0x0ff)) { 
             strncpy(bufi, ",P", 2);  
             bufi += 2;
             number += 2;
           }
           else if (nmeadispl_temp == 0x0ff) {
             strncpy(bufi, ",U", 2);    // Marker is V for Voltage 
             bufi += 2;
             number += 2;
           }
          
           //DEBUG(8,"Data %i: %s",number, bufi);
 
           strncpy(bufi, ",", 1);
           bufi++;
           number++;
           int post = charstr(ptr, 0x0d);   // Position of TAB marker  (end of number)
           printf("Post  %i marker %c ptr %s",post, marker, ptr);
           if (post == 5) {        // Number has 5 digits
             strncpy(bufi, ptr, 2);   // First two voltage digits
             bufi+=2;
             ptr+=2;
             number += 2;
           }
           else if (post == 4) { 
             strncpy(bufi, ptr, 1);   // First voltage digit
             bufi+=1;
             ptr+=1;
             number += 1;
           }
           else {    // Post == 3,2,1   Only mV Value no number before .
             *bufi = '0';   // First voltage digit
             bufi+=1;
             number += 1;
           }
           strncpy(bufi++, ".", 1);
           number += 1; 
           // ptr points to next valid value
           if (post >= 3) strncpy(bufi, ptr, 1);    // 100mV value
           else *bufi = '0'; 
           bufi+=1;
           ptr+=1;
           number += 1;
           if (nmeadispl_temp == 0x0ff) {
             if (post >= 2) strncpy(bufi, ptr, 1);  // 10mV Value
             else *bufi = '0';    // 10mV
             bufi+=1;
             ptr+=1; 
             number += 1;
             strcpy(bufi, ",V,U1");    //  Now Voltage and U001 as index 
             bufi+=5;
             //DEBUG(9,"Data to %s",buf);
             //return(24);
             number += 5;
           }
           else {    // For normal NMEA the 10 mV and 1mV are not shown and skipped
             *bufi = ','; 
             bufi++;
             number++; 
             if (marker == 'V') 
                *bufi= nmeastringv.unit;
             else
                *bufi= nmeastringp.unit;
             bufi++;
             number++; 
           }
      }
      else
      {
        bufi -= 2;
        printf("value of main battery voltage missing!\n");
      }
    }
    else if ((marker == 'I') && (nmeadispl_temp & 0x08))    // handle  "I" for Battery Current
    {
      printf("Handling Marker I nmeadispl_temp %x", nmeadispl_temp);
      ptr = strtok(NULL, limiter);   // look for TAB
      if (ptr!=NULL)  
      {              // Generate NMEA sentence and copy to buffer  $IIXDR,I,xx,mA,U001,
        if (nmeadispl_temp == 0x0ff) { 
           strncpy(bufi, ",I,", 3);  
           bufi+=3; 
           char i = 0;
           while ( (('0' <= *(ptr+i)) && (*(ptr+i) <= '9')) || *(ptr+i)== '-') {
            *bufi++ = *(ptr+i); 
            number++;
            i++;
           }
          strncpy(bufi, ",mA,U1",8);    //  Now mA  and U001 as index 
          bufi+=6;
          number += 9;
        }
        else {
          strncpy(bufi, nmeastringi.nmeastring, 6);  
          bufi+=6; 
          *bufi++ = ','; 
          number++;
          char i = 0;
          j = 0;
          // Value from Vitronic is in mA, switch to A
          while ( (('0' <= *(ptr+i)) && (*(ptr+i) <= '9')) || *(ptr+i)== '-') {  // How many digits ?
            i++;
          }
          printf("Handling Current I: %i", i);
          if (*(ptr)== '-') {*bufi++ = '-'; number++; j++;}
          if (i == 5) {*bufi++ = *(ptr+j); j++; number++;}               // 10A
          if (i == 4) {*bufi++ = *(ptr+j); j++;} else *bufi++ = '0';  // A
          number++;
          *bufi++ = '.'; 
          number++;
          if (i == 3) {*bufi++ = *(ptr+j); j++;} else {*bufi++ = '0';} // 100mA
          number++;
          //if (i == 2) {*bufi++ = *(ptr+j); j++;} else {*bufi++ = '0';} // 10mA
          //number++;
          //*bufi++ = *(ptr+j);    // mA
          //number++;
          
          *bufi++ = ','; 
          number++;
          *bufi++ = nmeastringi.unit;
          number++;
        }

      }
      else
      {
        bufi -= 2;
        printf("value of main current missing!\n");
      }
    }
    else if ((marker == 'W') && (nmeadispl_temp & 0x10))    // handle  "PPV" for Solar Panel Power
    {
      printf("Handling Marker W nmeadispl_temp %x", nmeadispl_temp);
      ptr = strtok(NULL, limiter);   // look for TAB
      if (ptr!=NULL)  
      {              // Generate NMEA sentence and copy to buffer  $IIXDR,W,xx,V,U001,
        if (nmeadispl_temp == 0x0ff) { 
          strncpy(bufi, ",W,", 3);  
          bufi+=3;
          char i = 0;
          while (('0' <= *(ptr+i)) && (*(ptr+i) <= '9')) {
            *bufi++ = *(ptr+i); 
            number++;
            if (i++ > 5) break;
          }
          strcpy(bufi, ",W,U1");    //  Now mA  and U001 as index 
          bufi+=5;
          number += 8;
        }
        else
        {
          strncpy(bufi, nmeastringw.nmeastring, 6);
          bufi+=6;
          *bufi++ = ',';
          number+=7;
          char i = 0;
          j = 0;
          // Value from Vitronic is in W, 
          while ( (('0' <= *(ptr+i)) && (*(ptr+i) <= '9')) || *(ptr+i)== '-') {  // How many digits ?
            i++;
          }
          printf("Handling Solar Power W: %i digits", i);  // i contains amount of numbers
          if (*(ptr)== '-') {*bufi++ = '-'; number++; j++;}
          if (i == 2) {*bufi++ = *(ptr+j); j++;} else {*bufi++ = '0';} // 10W
          number++;
          *bufi++ = *(ptr+j);    // W
          number++;
          *bufi++ = '.'; 
          number++;
          *bufi++ = '0'; 
          number++;

          *bufi++ = ',';
          number++;
          *bufi++ = nmeastringw.unit;
          number++;
         }
      }
    }
    else if ((marker == 'E') && (nmeadispl_temp & 0x40) || (marker == 'O') && (nmeadispl_temp & 0x20))    // handle  "H19" for Energy total 
    {
      printf("Handling Marker E nmeadispl_temp %x", nmeadispl_temp);
      ptr = strtok(NULL, limiter);   // look for TAB
      if (ptr!=NULL)  
      {              // Generate NMEA sentence and copy to buffer  $IIXDR,W,xx,V,U001,
        if (nmeadispl_temp == 0x0ff) { 
          if (marker == 'E') strncpy(bufi, ",E,", 3);  
          else strncpy(bufi, ",O,", 3);
          bufi+=3;
          char i = 0;
          while ((('0' <= *(ptr+i)) && (*(ptr+i) <= '9') ) || (*(ptr+i)== '-')) {   //  as long as we get numbers
            *bufi++ = *(ptr+i); 
            number++;
            if (i++ > 5) break;       // Something wrong, give up
          }
          strcpy(bufi, "0,Wh,U1");    //  Now mA  and U001 as index 
          bufi+=7;
          number += 10;
        }
        else
        {
          if (marker == 'E')  strncpy(bufi, nmeastringe.nmeastring, 6);
          else  strncpy(bufi, nmeastringo.nmeastring, 6);
          bufi+=6;
          *bufi++ = ',';
          number+=7;
          char i = 0;
          j = 0;
          // Value from Vitronic is in 10 Wh, 
          i = 0;
          while ((('0' <= *(ptr+i)) && (*(ptr+i) <= '9') ) || (*(ptr+i)== '-')) {   //  as long as we get numbers
            *bufi++ = *(ptr+i); 
            number++; 
            if (i++ > 5) break;       // Something wrong, give up
          }
          strcpy(bufi, "0,Wh");    //  Now mA  and U001 as index
          bufi+=4;
          number += 7;
          printf("Handling Energy: %i digits", i);
         }
      }
      else
      {
        bufi -= 2;
        printf("value of PPV missing!\n");
      }
    }
    ptr = strtok(NULL, limiter);   // look for next value
  }
 //           DEBUG(9,"Victron Done");


  strncpy(bufi, "\n\r\0", 3);    //  Checksum not implemented
  number += 2;
           printf("Final Data %i: %s",number, buf);
//     if (number < 5)  {
 //        for (i=0; i<100; i++)
//            printf( "Data %i %c %x",i, bufint[i], bufint[i]);
  //   }
  } while (number < 10);   //  Until we got some data
  return(number);
}


int main(int argc, char *argv[])
{
  int sockfd, newsockfd, portno;
  socklen_t clilen;
  struct sockaddr_in serv_addr, cli_addr;
  struct hostent *server;
  char NMEADPTstring[200];
  int n, j;
  int dev;

  if (argc < 3) {
       //nmeastring=P,$IIMTW,C
    printf("Use:  victron serial_port target_port nmeastringP,IIMTW,C \n");
    exit(1);
  }

    /* Open interface or die */
printf ("Starting %s\n",argv[1] );
   /* Open device (RW for now..let's ignore direction...) */
  struct termios attribs;
      dev = open(argv[1], (O_RDWR|O_NOCTTY));
 //     if ((dev = open(argv[1], (O_RDWR|O_NOCTTY|O_NONBLOCK))) < 0) {
      if (dev < 0) {
	 printf("Failed to open %s\n",*argv[1]);
         return(-1);
      }
    speed_t speed;
    /*
    ** Get the current settings. This saves us from
	 *           * having to initialize a struct termios from
	 *                * scratch.
	 *                     */
    if(tcgetattr(dev, &attribs) < 0)
    {
       printf("Stdin Error");
       return;
    }
	    /*
	     *      * Set the speed data in the structure
	     *           */
    if(cfsetospeed(&attribs, B19200) < 0)
    {
     printf("invalid baud rate");
     return;
						    }
	        /*
		 *      * Apply the settings.
		 *           */
    attribs.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON and ECHO. */
    attribs.c_iflag &= ~(INLCR|ICRNL); /* Clear ICANON and ECHO. */
    attribs.c_cc[VMIN] = 0;    // VMIN must be set to 0 so the VTIME works 
    attribs.c_cc[VTIME] = 20;   // Return, when a gapof 0.2 sec between the bytes is detected

    tcflush(dev, TCIFLUSH);
    if(tcsetattr(dev, TCSANOW, &attribs) < 0)
    {
            printf("Stdin Error");
            return;
    }



    printf("Opened serial device %s %i \n",argv[1],dev );
  
    strncpy(nmeastring0.nmeastring, "$IIXDR\0", 7);  
    nmeastringv.nmeastring[0] = 0;
    nmeastringp.nmeastring[0] = 0;
    nmeastringw.nmeastring[0] = 0;
    nmeastringi.nmeastring[0] = 0;
    nmeastringe.nmeastring[0] = 0;
    nmeastringy.nmeastring[0] = 0;
    nmeastringo.nmeastring[0] = 0;

    printf ("Starting %s\n",argv[3]);

          char select = (argv[3][0]);

	  printf ("Starting %c\n",select);
          switch (select) {
            case 'I':                       // Battery Current
              nmeastringi.nmeastring[0] = '$';
              for (j=1; j<6; j++) {
                nmeastringi.nmeastring[j] = (argv[3][j+1]);
              }
              nmeastringi.nmeastring[6] = 0;
              nmeastringi.unit = argv[3][8];
              break; 
            case 'P':                       // Solar Voltage (panel)
              nmeastringp.nmeastring[0] = '$';
              printf("P\n");
	      for (j=1; j<6; j++) {
                nmeastringp.nmeastring[j] = (argv[3][j+1]);
              }
              nmeastringp.nmeastring[6] = 0;
              nmeastringp.unit = argv[3][8];
              break; 
            case 'W':                       // Solar power 
              nmeastringw.nmeastring[0] = '$';
              for (j=1; j<6; j++) {
                nmeastringw.nmeastring[j] = (argv[3][j+1]);
              }
              nmeastringw.nmeastring[6] = 0;
              nmeastringw.unit = argv[3][8];
              break; 
            case 'V':                       // Battery Voltage
              nmeastringv.nmeastring[0] = '$';
              for (j=1; j<6; j++) {
                nmeastringv.nmeastring[j] = (argv[3][j+1]);
              }
              nmeastringv.nmeastring[6] = 0;
              nmeastringv.unit = argv[3][8];
              break; 
            case 'E':                       // Energy total same day 
              nmeastringe.nmeastring[0] = '$';
              for (j=1; j<6; j++) {
                nmeastringe.nmeastring[j] = (argv[3][j+1]);
              }
              nmeastringe.nmeastring[6] = 0;
              nmeastringe.unit = argv[3][8];
              break; 
            case 'Y':                       // Energy Yield  Yesterday 
              nmeastringy.nmeastring[0] = '$';
              for (j=1; j<6; j++) {
                nmeastringy.nmeastring[j] = (argv[3][j+1]);
              }
              nmeastringy.nmeastring[6] = 0;
              nmeastringy.unit = argv[3][8];
              break; 
            case 'O':                       // Energy Yield  overall 
              nmeastringo.nmeastring[0] = '$';
              for (j=1; j<6; j++) {
                nmeastringo.nmeastring[j] = (argv[3][j+1]);
              }
              nmeastringo.nmeastring[6] = 0;
              nmeastringo.unit = argv[3][8];
              break; 
            default: 
               printf( "No Config");
          }
  
    printf("Nmeastring %s: nmeaunit %c\n", &nmeastringp.nmeastring, nmeastringp.unit);
  
  
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);   // UDP
  portno = atoi(argv[2]);
  if (sockfd < 0) 
    printf("ERROR opening socket");
  else 
    printf("Opening UDP socket portno is %i\n",portno);
  /* build the server's Internet address */
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  if (inet_aton("127.0.0.1", &serv_addr.sin_addr) == 0) printf("Wrong IP"); // store IP in antelope
  serv_addr.sin_port = htons(portno);
  do {
    printf("Reading Victron\n");
    n = read_victron (dev, &NMEADPTstring[0]);
    printf("Got %d bytes\n",n);
    printf("sockfd: %x, NMEAString: %s, n: %i", sockfd,NMEADPTstring, n); 
    n = sendto(sockfd,NMEADPTstring , n, 0, &serv_addr, sizeof(serv_addr));
    printf("Sent %d bytes\n",n);
    sleep(10);
  } while (n>=0);
  if (n < 0) printf("ERROR writing to socket");
  close(sockfd);
  return 0; 
}

