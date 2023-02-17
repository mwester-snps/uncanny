/* beacon.c - Issue periodic messages on CAN (using socketcan)             */
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
#define USAGE "\
Usage: %s [-i <id>] [-p <ms>] [-s] [-d] [<iface>]\n"
#define HELP "\n\
Sends a periodic CAN frame to the specified interface.\n\
\n\
Options:\n\
-p <ms> Specifies the period in milliseconds from frame to frame,\n\
        default is 100ms.\n\
-i <id> Specifies the CAN id to use, range is 0x001 to 0x7FF,\n\
        default is 0x123.  This can (and should) be specified\n\
        in hexadecimal format, e.g. 0x7d8\n\
-d      Increases verbosity, may be repeated for more verbosity.\n\
-s      Makes the payload static content, instead of placing the\n\
        current timestamp in the payload.\n\
<iface> Specifies the CAN socket interface name to use,\n\
        default is vcan0.\n\
\n\
Jitter and Frame Loss Options:\n\
-T <v>  Sets the timing jitter.  The default is zero, meaning\n\
        no additional jitter added. The maximum value is 200,\n\
        which results in the period ranging from 1x to 3x.\n\
-J <v>  Sets the percentage of frames affected by jitter.\n\
-L <v>  Sets the percentage of frames that may be lost.\n\
-S <s>  Sets the seed value used for the random numbers.\n\
\n\
"

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int main(int argc, char *argv[]) {
  struct sockaddr_can addr;
  struct ifreq ifr;
  struct timespec tspec;
  long lastSent = 0;
  long tsbase = 0;
  long tjitter = 0;
  long tnow;
  int opt, sock, i, n;

  /* Parse command line args */

  char *ifname = "vcan0"; /* SocketCAN interface */
  int debug = 0;          /* Debug/verbosity level */
  int period = 100;       /* period between frames (ms) */
  int fixed = 0;          /* static/fixed or timestamp */
  int can_id = 0x123;     /* CAN id for message */

  int seed = 1;           /* Random number generate seed */
  int timing = 0;         /* Frame timing jitter amount */
  int jitter = 0;         /* Pct frames affected by jitter */
  int loss = 0;           /* Frame loss percentage */

  while ((opt = getopt(argc, argv, "sdhp:i:T:J:L:S:")) >= 0) {
    switch (opt) {
    case 'd':
      debug++;
      break;
    case 's':
      fixed = 1;
      break;
    case 'i':
      can_id = strtol(optarg, NULL, 0);
      if (can_id < 1 || can_id > 0x7FF) {
	fprintf(stderr, "Error: invalid CAN id: \"%s\"\n", optarg);
	return(1);
      }
      break;
    case 'p':
      period = atoi(optarg);
      break;
    case 'h':
      printf(USAGE, argv[0]);
      printf(HELP);
      return(0);
    case 'T':
      timing = atoi(optarg);
      if ( timing < 0 || timing > 200) {
	fprintf(stderr, "Error: Invalid percentage: \"%s\"\n", optarg);
	return(1);
      }
      break;
    case 'J':
      jitter = atoi(optarg);
      if ( jitter < 0 || jitter > 100) {
	fprintf(stderr, "Error: Invalid percentage: \"%s\"\n", optarg);
	return(1);
      }
      break;
    case 'L':
      loss = atoi(optarg);
      if (loss < 0 || loss > 99) {
	fprintf(stderr, "Error: Invalid percentage: \"%s\"\n", optarg);
	return(1);
      }
      break;
    case 'S':
      seed = strtol(optarg, NULL, 0);
      break;

    default:  /* '?' */
      fprintf(stderr, USAGE, argv[0]);
      return(1);
    }
  }
  if (optind < argc) ifname = argv[optind];

  printf("Sending %s frames to id 0x%03X every %d milliseconds\n",
	 (fixed)?"fixed":"different", can_id, period);
  printf(" over CAN-bus socket \"%s\" (debug level %d)...\n",
	 ifname, debug);
  if (loss) printf(" Frame loss percentage is %d.\n", loss);
  if (jitter)
    printf(" Jitter up to 1.%dx will affect %d percent of frames.\n",
	   timing, jitter);
  if (jitter || loss)
    printf(" Random number seed is set to %d.\n", seed);

  /* Initialize the random number seed */
  srand(seed);

  /* Initialize the CAN frame */
  struct can_frame buf;
  buf.can_id  = can_id;
  buf.can_dlc = 8;
  buf.data[0] = 0x00;
  buf.data[1] = 0x11;
  buf.data[2] = 0x22;
  buf.data[3] = 0x33;
  buf.data[4] = 0x44;
  buf.data[5] = 0x55;
  buf.data[6] = 0x66;
  buf.data[7] = 0x77;

  /* Open and bind the CAN socket */
  if ((sock = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
    perror("Error opening socket");
    return 1;
  }

  strcpy(ifr.ifr_name, ifname);
  ioctl(sock, SIOCGIFINDEX, &ifr);

  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

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

    /* Establish "now" in terms of milliseconds since start */
    clock_gettime(CLOCK_REALTIME, &tspec);
    if (tsbase == 0) tsbase = tspec.tv_sec;
    tnow = ((tspec.tv_sec-tsbase)*1000)+(tspec.tv_nsec/1.0e6);

    /* Check if it's time to send a message */
    if (tnow >= (lastSent + period + tjitter)) {

      /* Randomly we may choose to simulate message loss... */
      if (! loss || ((rand() % 100) > loss)) {

	/* Update payload if not static */
	if (! fixed) memcpy(buf.data, &tnow, 8);

	/* Send the message - retry if necessary */
	i = 3;
	while (i > 0) {
	  n = write(sock, &buf, sizeof(struct can_frame));
	  if (n < 0) {
	    if ((errno != ENETDOWN) || (i == 0)) {
	      perror("write(): Error sending CAN frame");
	      return 3;
	    } else {
	      i--;
	      usleep(500);
	    }
	  } else {
	    i = 0;
	  }
	}

	/* TODO: check the number of bytes sent? */
	if (debug > 2) printf("DEBUG: write(): wrote %d bytes\n", n);

	/* Print out tx buffer we just sent */
	if (debug) {
	  printf("%5d.%03d %5d+%s  %03X  [%d]",
		 (tnow / 1000), (tnow % 1000), tnow - lastSent,
		 (tjitter)?"+":" ", buf.can_id, buf.can_dlc);
	  for (i=0;i<buf.can_dlc;i++) printf(" %02X", buf.data[i]);
	  printf("\n");
	}

      } else {

	/* Print out the frame lost message */
	if (debug) {
	  printf("%5d.%03d %5d+   *** frame dropped ***\n",
		 (tnow / 1000), (tnow % 1000), tnow - lastSent);
	}

      }

      /* Update the "last frame sent" timestamp */
      lastSent = tnow;

      /* Randomly, we may add some jitter time... */
      if (jitter && ((rand() % 100) < jitter)) {
	tjitter = ((rand() % 100) * period * timing) / 10000;
	/* Announce jitter, if any */
	if ((debug > 1) && tjitter) {
	  printf("          %5d+   jitter\n", tjitter);
	}
      } else {
	tjitter = 0;
      }

    }

    /* Sleep for 1/2 ms */
    usleep(500);

  } /* while (1) */

} /* main() */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
