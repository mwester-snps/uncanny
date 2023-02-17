/* dut.c - Device-Under-Test simulator for CAN                                              */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/can.h>
#include <linux/raw.h>

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* For convenience... */
typedef unsigned char uchar;

/* Debug/verbosity level */
int debug = 1;

/* Simulate crash -- this is triggered by UDS test case 37802 on the
 * CAN-bus suite 1.11.0; there are likely other test cases as well */
int crashdemo = 1;

/* Background traffic suppression */

int suppressId = 0x123;

/* Background traffic generation */

int trafficEnabled   = 0;     /* enable/disable periodic message */
int trafficStaticMsg = 1;     /* static/fixed or timestamp       */
int trafficId        = 0x123; /* CanId for message               */
int trafficPeriod    = 50;    /* milliseconds between messages   */

/* Globals */

int sock = 0;
long time_baseline = 0;
long lastSent = 0;

/* Long ISO-TP message buffer */

int bufflen = 0;
int buffull = 0;
uchar buffie[4096];

/* Static transmit buffers */

struct can_frame rtx;
struct can_frame ptx;

/* Static receive buffer */

struct can_frame rx;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Return current time offset, in milliseconds.  The baseline is
 * automatically set the first time this function is called, so for
 * all practical usage, the return value is the roughly the number of
 * milliseconds since the first call. */

long timenow(void) {
  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  long s = spec.tv_sec;
  long ms = spec.tv_nsec / 1.0e6;
  if (time_baseline == 0) time_baseline = s;
  return (((s - time_baseline) * 1000) + ms);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Diagnostic output - print a frame to stdout */

void printFrame(int dir, struct can_frame f) {
  int i;
  long t = timenow();
  //printf("%s%5d.%03d  %03X  [%d]", (dir ? " -in->" : "<-out "),
  printf("%s%5d.%03d  %03X  [%d]", (dir ? " >" : "< "),
	 (t / 1000), (t % 1000), f.can_id, f.can_dlc);
  for (i = 0; i < f.can_dlc; i++) printf(" %02X", f.data[i]);
  printf("\n");
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Construct and send a CAN frame */

void sendFrame(int id, uchar d0, uchar d1, uchar d2, uchar d3,
	       uchar d4, uchar d5, uchar d6, uchar d7) {
  /* Construct frame to transmit */
  rtx.can_id  = id;
  rtx.can_dlc = 8;
  rtx.data[0] = d0;
  rtx.data[1] = d1;
  rtx.data[2] = d2;
  rtx.data[3] = d3;
  rtx.data[4] = d4;
  rtx.data[5] = d5;
  rtx.data[6] = d6;
  rtx.data[7] = d7;
  /* Send frame */
  int n = write(sock, &rtx, sizeof(struct can_frame));
  /* Display frame */
  printFrame(0, rtx);
  /* Debug: should be 16 bytes (size of total frame) */
  if (debug > 2) printf("sendFrame: wrote %d bytes\n", n);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Periodically send some traffic */

void doPeriodic() {
  int i;
  if (! trafficEnabled) return;
  long tnow = timenow();
  if (tnow < (lastSent + trafficPeriod)) return;
  ptx.can_id = trafficId;
  ptx.can_dlc = 8;
  if (trafficStaticMsg) {
    ptx.data[0] = 0x00;
    ptx.data[1] = 0x11;
    ptx.data[2] = 0x22;
    ptx.data[3] = 0x33;
    ptx.data[4] = 0x44;
    ptx.data[5] = 0x55;
    ptx.data[6] = 0x66;
    ptx.data[7] = 0x77;
  } else {
    memcpy(ptx.data, &tnow, 8);
  }
  int n = write(sock, &ptx, sizeof(struct can_frame));
  if (debug > 1) printFrame(0, ptx);
  if (debug > 2) printf("doPeriodic: wrote %d bytes\n", n);
  lastSent = tnow;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Convenience macros for comparison purposes */

#define isUDS1(l,d,d0) \
  (l==2 && d[0]==d0)
#define isUDS2(l,d,d0,d1) \
  (l==2 && d[0]==d0 && d[1]==d1)
#define isUDS3(l,d,d0,d1,d2) \
  (l==3 && d[0]==d0 && d[1]==d1 && d[2]==d2)
#define isUDS4(l,d,d0,d1,d2,d3) \
  (l==4 && d[0]==d0 && d[1]==d1 && d[2]==d2 && d[3]==d3)
#define isUDS5(l,d,d0,d1,d2,d3,d4) \
  (l==6 && d[0]==d0 && d[1]==d1 && d[2]==d2 && d[3]==d3 && d[4]==d4)
#define isUDS6(l,d,d0,d1,d2,d3,d4,d5) \
  (l==6 && d[0]==d0 && d[1]==d1 && d[2]==d2 && d[3]==d3 && d[4]==d4 \
   && d[5]==d5)
#define isUDS7(l,d,d0,d1,d2,d3,d4,d5,d6)			    \
  (l==7 && d[0]==d0 && d[1]==d1 && d[2]==d2 && d[3]==d3 && d[4]==d4 \
   && d[5]==d5 && d[6]==d6)
#define isUDS8(l,d,d0,d1,d2,d3,d4,d5,d6,d7)			    \
  (l==8 && d[0]==d0 && d[1]==d1 && d[2]==d2 && d[3]==d3 && d[4]==d4 \
   && d[5]==d5 && d[6]==d6 && d[7]==d7)

/* Convenience macro for debug output */
#define UDSmsg(s) {if (debug) printf(" --> %s\n", s);}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Process a UDS frame */

void udsFrame(int id, int len, uchar *data) {
  int i;

  if (debug) {
    printf(" -> %s: %03X [%d] ",
	   (data[0]<0x10)?"ODB-II":"UDS", id, len);
    for (i=0; i<len; i++) printf(" %02X", data[i]);
    printf("\n");
  }

  int diagId = 0x7e8;

  /* OBD-II */

  /* 01 00   Unknown... */
  if (isUDS2(len, data, 0x01, 0x00)) {
    UDSmsg("OBD-II Unknown? 01 00");
    sendFrame(diagId, 6, 0x41, 0, 25, 25, 25, 25, 0);
  }

  /* 09 00   VIN?... */
  else if (isUDS2(len, data, 0x09, 0x00)) {
    UDSmsg("OBD-II VIN supported?");
    sendFrame(diagId, 6, 0x49, 0, 25, 25, 25, 25, 0);
  }

  /* 09 02   VIN... */
  else if (isUDS2(len, data, 0x09, 0x02)) {
    UDSmsg("OBD-II VIN?");
    sendFrame(diagId, 0x10, 13, 0x49, 2, 'S', 'y', 'n', 'o'); 
    sendFrame(diagId, 0x21, 'p', 's', 'y', 's', 'S', 'I', 'G');
  }

  /* UDS */

  /* 10 01  Diagnostic Session Control -- Unknown... */
  else if (isUDS2(len, data, 0x10, 0x01)) {
    UDSmsg("DSC 01");
    //sendFrame(diagId, 6, 0x50, 0x01, 0x00, 0x03, 0x00, 0x00, 0);
    sendFrame(diagId, 2, 0x50, 0x01, 0, 0, 0, 0, 0);
  }

  /* 10 02  Diagnostic Session Control -- Unknown... */
  else if (isUDS2(len, data, 0x10, 0x02)) {
    UDSmsg("DSC 02");
    //sendFrame(diagId, 6, 0x50, 0x02, 0x00, 0x03, 0x00, 0x00, 0);
    sendFrame(diagId, 2, 0x50, 0x02, 0, 0, 0, 0, 0);
  }

  /* 10 03  Diagnostic Session Control -- Unknown... */
  else if (isUDS2(len, data, 0x10, 0x03)) {
    UDSmsg("DSC 03");
    //sendFrame(diagId, 6, 0x50, 0x03, 0x00, 0x03, 0x00, 0x00, 0);
    sendFrame(diagId, 2, 0x50, 0x03, 0, 0, 0, 0, 0);
  }

  /* 11 01  ECU Reset */
  else if (isUDS2(len, data, 0x11, 0x01)) {
    UDSmsg("ER ECU Reset");
    sendFrame(diagId, 2, 0x51, 0x01, 0, 0, 0, 0, 0);
  }

  /* 14 FF FF FF  Clear Diagnostic Information -- Unknown... */
  else if (isUDS4(len, data, 0x14, 0xff, 0xff, 0xff)) {
    UDSmsg("CDTCI FF FF FF");
    sendFrame(diagId, 1, 0x54, 0, 0, 0, 0, 0, 0);
  }

  /* 19 01 00  Read DTC Information -- Unknown... */
  else if (isUDS3(len, data, 0x19, 0x01, 0x00)) {
    UDSmsg("RDTCI 01 00");
    sendFrame(diagId, 3, 0x59, 0x01, 0x00, 0, 0, 0, 0);
  }

  /* 22 FF 00  Read Data by Identifier -- Unknown... */
  else if (isUDS3(len, data, 0x22, 0xff, 0x00)) {
    UDSmsg("RDBI FF 00");
    sendFrame(diagId, 3, 0x62, 0xff, 0x00, 0, 0, 0, 0);
  }

  /* 23 22 FF FF FF FF  Read Memory by Address -- Unknown... */
  else if (isUDS6(len, data, 0x23, 0x22, 0xff, 0xff, 0xff, 0xff)) {
    UDSmsg("RMBA 22 FF FF FF FF");
    sendFrame(diagId, 6, 0x63, 0x22, 0xFF, 0xFF, 0xFF, 0xFF, 0);
  }

  /* 24 FF 00  Read Scaling Data by Identifier -- Unknown... */
  else if (isUDS3(len, data, 0x24, 0xff, 0x00)) {
    UDSmsg("RSDBI FF 00");
    sendFrame(diagId, 4, 0x64, 0xff, 0x00, 0x00, 0, 0, 0);
  }

  /* 27 01  Security Access -- request seed */
  else if (isUDS2(len, data, 0x27, 0x01)) {
    UDSmsg("SA request seed");
    /* Negative response before seed */
    //sendFrame(diagId, 3, 0x7f, 0x27, 0x78, 0, 0, 0, 0);
    /* Now send security access seed request */
    //sendFrame(diagId, 0x10, 0x0c, 0x67, 0x07, 0x01, 0x02, 0x03, 0x04);
    //usleep(10000);
    //sendFrame(diagId, 0x20, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b);
    sendFrame(diagId, 6, 0x67, 0x01, 0x12, 0x34, 0x56, 0x78, 0);
  }

  /* 27 02  Security Access -- send key */
  else if (isUDS4(len, data, 0x27, 0x02, 0x32, 0x10)) {
    UDSmsg("SA send key");
    sendFrame(diagId, 4, 0x67, 0x02, 0x32, 0x10, 0, 0, 0);
  }

  /* 28 00 00  Communications Control -- Unknown... */
  else if (isUDS3(len, data, 0x28, 0x00, 0x00)) {
    UDSmsg("Unknown 28 00 00");
    //sendFrame(diagId, 3, 0x68, 0x02, 0x00, 0, 0, 0, 0);
    sendFrame(diagId, 6, 0x67, 0x02, 0x00, 0x00, 0x00, 0x00, 0);
  }

  /* 2A 01  Read Data by Periodic Identifier -- Unknown... */
  else if (isUDS2(len, data, 0x2A, 0x01)) {
    UDSmsg("RDBPI 01");
    sendFrame(diagId, 1, 0x6A, 0, 0, 0, 0, 0, 0);
  }

  /* 2C 01 F2 00 00 00 01 00 -- Unknown... */
  else if (isUDS8(len, data, 0x2C, 0x01, 0xF2, 0x00, 0x00, 0x00, 0x01, 0x00)) {
    UDSmsg("Unknown 2C 01 F2 00 00 00 01 00");
    /* Not supported - send negative response */
    sendFrame(diagId, 3, 0x7F, 0x2C, 0x12, 0, 0, 0, 0);
  }

  /* 2F FF 00 00  I/O Control by Identifier -- Unknown... */
  else if (isUDS4(len, data, 0x2F, 0xff, 0x00, 0x00)) {
    UDSmsg("IOCBI FF 00 00");
    sendFrame(diagId, 4, 0x6F, 0xFF, 0x00, 0x00, 0, 0, 0);
  }

  /* 31 01 01 00  Routine Control -- Unknown... */
  else if (isUDS4(len, data, 0x31, 0x01, 0x01, 0x00)) {
    UDSmsg("RC 01 01 00");
    sendFrame(diagId, 4, 0x71, 0x01, 0x01, 0x00, 0, 0, 0);
  }
  
  /* 3E 00  Tester Present... */
  else if (isUDS2(len, data, 0x3e, 0x00)) {
    UDSmsg("TP Tester Present");
    sendFrame(diagId, 2, 0x7e, 0x00, 0, 0, 0, 0, 0);
  }

  /* 85 01  Control DTC Setting -- Unknown... */
  else if (isUDS2(len, data, 0x85, 0x01)) {
    UDSmsg("CDTCS 01");
    sendFrame(diagId, 2, 0xc5, 0x01, 0, 0, 0, 0, 0);
  }

  /* 86 00 00  Response on Event -- Unknown... */
  else if (isUDS3(len, data, 0x86, 0x00, 0x00)) {
    UDSmsg("ROE 00 00");
    sendFrame(diagId, 3, 0xc6, 0x00, 0x00, 0, 0, 0, 0);
  }

  /* 87 01 00  Link Control -- Unknown... */
  else if (isUDS3(len, data, 0x87, 0x01, 0x00)) {
    UDSmsg("LC 01 00");
    sendFrame(diagId, 3, 0xc7, 0x01, 0x00, 0, 0, 0, 0);
  }

  /* We don't handle this size message (yet) */
  else {
    if (debug) printf("! Unsupported UDS message:");
    for (i=0; i<len; i++) printf(" %02X", data[i]);
    printf("\n");
  }

  if (debug) printf("\n");
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Process an ISO-TP frame */

void isotpFrame(int id, int dlc, uchar *data) {

  if (data[0] < 0x10) {
    if (debug > 2) printf("* ISO-TP: single frame message...\n");
    udsFrame(id, data[0], data + 1);

  } else if (data[0] < 0x20) {
    int i;
    if (debug > 1) printf("* ISO-TP: first frame message...\n");
    buffull = ((((int)data[0]) & 0xf) << 8) + (int)data[1];
    if (debug > 1) printf("*  len: %d\n", buffull);
    bufflen = 0;
    for (i=2; i<dlc; i++) buffie[bufflen++] = data[i];
    //sendFrame(0x7d8, 0x30, 0, 5, 0, 0, 0, 0, 0);
    sendFrame(0x7d8, 0x30, 255, 1, 0, 0, 0, 0, 0);

  } else if (data[0] < 0x30) {
    int i;
    if (debug > 1) printf("* ISO-TP: consecutive frame message...\n");
    int idx = ((int)data[0]) & 0xf;
    if (debug > 1) printf("*  idx: %d\n", idx);
    for (i=1; i<dlc && bufflen<buffull; i++) buffie[bufflen++] = data[i];
    if (debug > 1) printf("*  tot: %d/%d\n", bufflen, buffull);
    if (bufflen == buffull) {
      if (debug > 1) printf("*  ISO-TP long message complete...\n");
      udsFrame(id, bufflen, buffie);
    }

  } else if (data[0] < 0x40) {
    /* Nothing done with this, as of yet... */
    if (debug > 1) printf("* ISO-TP: flow-control frame message...\n");

  } else {
    printf("* Unexpected ISO-TP Frame type %0x2...\n", data[0]);
  }

}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Raw frames are handled here */

void rawFrame(struct can_frame f) {

  int id = f.can_id;
  int dlc = f.can_dlc;

  /* Check if we should ignore this frame */
  if (id == suppressId) return;

  /* Display frame */
  printFrame(1, f);

  /* Ignore empty frames... if that ever happens */
  if (dlc < 1) {
    printf("* Empty frame, ignored...\n");
    return;
  }

  /* Handle the crash simulation, if enabled */
  /* Test case: Diag Sess Ctrl, Resp on Evnt, type 6 */
  if (crashdemo) {
    if (dlc == 8 && f.data[0] == 0x03 && f.data[1] == 0x10 &&
        f.data[2] == 0x06 && f.data[3] == 0x00 && f.data[4] == 0x00 &&
        f.data[5] == 0x00 && f.data[6] == 0x00 && f.data[7] == 0x00) {
      printf("* Simulating DuT crash and restart...\n");
      printf("* Restarting... please wait...\n");
      sleep(5);
      printf("* Almost there...\n");
      sleep(5);
      printf("* Recovered...\n");
      printf("* Simulated DuT crash and restart complete.\n");
      return;
    }	
  }

  /* Handle ISO-TP for selected CAN Ids */
  if (id == 0x7d0 || id == 0x71f) {
    isotpFrame(id, dlc, f.data);

  /* Unknown CAN Id - not an error, CAN is a broadcast bus! */
  } else {
    printf("* (info) unknown canId (0x%03x), ignored...\n", id);
  }
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Main routine */

int main(int argc, char *argv[]) {
  struct sockaddr_can addr;
  struct ifreq ifr;

  const char *ifname = "vcan0";

  if ((sock = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
    perror("Error opening socket");
    return 1;
  }

  strcpy(ifr.ifr_name, ifname);
  ioctl(sock, SIOCGIFINDEX, &ifr);

  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (debug) printf("%s at index %d\n", ifname, ifr.ifr_ifindex);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Error in socket bind");
    return 2;
  }

  if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
    perror("Error in socket fcntl (setting to non-blocking)");
    return 3;
  }

  /* Main loop */
  while (1) {

    doPeriodic();

    int n = read(sock, &rx, sizeof(struct can_frame));

    if (debug > 2) printf("read(): n=%d, s=%d, errno=%d\n",
			  n, sizeof(struct can_frame), errno);

    if (n == sizeof(struct can_frame)) {
      rawFrame(rx);

    } else if (n == 0) {
      /* No data yet, sleep for a millisecond */
      usleep(1000);

    } else if (n < 0) {     /* handle error... */
      if (errno == ENETDOWN || errno == EAGAIN) {  /* 100 and 11 */
        /* No data yet, sleep for a millisecond */
	usleep(1000);
      } else {
	/* unrecoverable error, just exit... */
	perror("can raw socket read");
	printf("errno=%d\n", errno);
	return 1;
      }

    } else {
      printf("Error: read(): CAN frame wrong size: actual=%d, expected=%d\n");
      return 2;
    }

  } /* while (1) */

} /* main() */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
