
#include <stdint.h>        // For fixed-width integer types (e.g., uint16_t)
#include <stdlib.h>        // For standard library functions like malloc, exit
#include <stdio.h>         // For input/output functions like printf, perror
#include <string.h>        // For string manipulation functions like strcpy, memset
#include <net/if.h>        // For network interface structures and constants
#include <sys/ioctl.h>     // For ioctl system call used for device-specific input/output operations
#include <linux/sockios.h> // For socket ioctl operations
#include <unistd.h>        // For miscellaneous constants and functions like getopt
#include "syscalls.h"      // Custom header for system calls (specific to your environment)



void die( const char *reason ) {
   // Print the error message and exit the program with a failure status
  perror( reason );
  exit( EXIT_FAILURE );
}


void print_usage(void) {
    // Print usage instructions for the program
    printf("xl710_unlock\n");
    printf("  -n <device_name>, required\n");   // User must provide the network device name
    printf("  -i <device_id>, default: 0x1572\n"); // Device ID is optional; defaults to 0x1572
    printf("  -p lock/unlock\n");                // Parameter to lock or unlock the device

    exit(EXIT_FAILURE); // Exit after printing usage
}



int main(int argc, char *const *argv) {
  /* Parse arguments */
  // Initialize default and null values for device ID and name
  char *c_devid = "0x1572";// Default device ID
  char *c_devname = NULL;// Device name (must be provided by user)
  int patching = 0;// Flag to indicate if patching is requested

  int c;

  // Parse command-line arguments using getopt
  while( ( c = getopt( argc, argv, "i:n:h?" ) ) != -1 )
  {
    switch( c )
    {
      case 'i':
        c_devid = optarg; // Set device ID if provided
        break;
      case 'n':
        c_devname = optarg;// Set device name if provided
        break;
      case 'h':
      case '?':
      default:
        print_usage();// Print usage if the argument is invalid
        break;
    }
  }
    // If no device name is provided, print usage and exit
  if( c_devname == NULL ) print_usage();

  int mod = 0;// Modifier variable for the EEPROM magic number
  uint16_t length = 0x02;// Length of data to read from EEPROM
  int fd;// File descriptor for the socket
  struct ifreq ifr;// Structure for interface request (used with ioctl)
  struct ethtool_eeprom *eeprom;// Pointer to the EEPROM structure

  const int devid  = strtol( c_devid, 0, 0 );// Convert device ID from string to integer
  const char *ethDev = c_devname; // Set device name to the user-provided name

    // Create a socket for communication with the network device
  fd = socket( AF_INET, SOCK_DGRAM, 0 );
  if( fd == -1 ) die( "socket" ); // Exit if socket creation fails

    // Allocate memory for the EEPROM structure, including space for the data
  eeprom = calloc( 1, sizeof( *eeprom ) + ( length << 1 ) );
  if( !eeprom ) die( "calloc" );// Exit if memory allocation fails

    // Set up the EEPROM structure for reading data
  eeprom->cmd    = ETHTOOL_GEEPROM;// Command to get EEPROM data
  eeprom->magic  = (devid << 16) | (I40E_NVM_SA << I40E_NVM_TRANS_SHIFT) | mod;// Magic number calculation
  eeprom->len    = length;// Set length of data to read
  memset(&ifr, 0, sizeof(ifr));// Clear the ifreq structure
  strcpy(ifr.ifr_name, ethDev);// Copy the device name into the ifreq structure

  /*
    Get offset to EMP SR
    offset 0x48
    length 0x2
  */
     // Set the offset to the EMP SR and read from the EEPROM
  eeprom->offset = 0x48 << 1;// Set EEPROM offset for EMP SR
  ifr.ifr_data = (void*)eeprom;// Associate EEPROM structure with ifreq

  // Perform ioctl to read data from the EEPROM
  if (ioctl(fd, SIOCETHTOOL, &ifr) == -1) die("ioctl");

    // Extract the EMP SR offset from the EEPROM data
  uint16_t emp_offset = *(uint16_t*)(eeprom+1);
  printf("EMP SR offset: 0x%04x\n", emp_offset);// Print the EMP SR offset

  /*
    Get offset to PHY Capabilities 0
    emp_offset + 0x19
    length 0x2
  */
  // Calculate the offset to PHY Capabilities 0
  uint16_t cap_offset = 0x19;// Offset to PHY Capabilities 0

  eeprom->offset = (emp_offset + cap_offset) << 1;// Set EEPROM offset

  ifr.ifr_data = (void*)eeprom;// Associate EEPROM structure with ifreq again
  if (ioctl(fd, SIOCETHTOOL, &ifr) == -1) die("ioctl");

    // Extract the PHY offset from the EEPROM data
  uint16_t phy_offset = *(uint16_t*)(eeprom+1) + emp_offset + cap_offset;
  printf("PHY offset: 0x%04x\n", phy_offset);// Print the PHY offset

  /*
    Get PHY data size
    offset phy_offset
  */
   // Set EEPROM offset to the PHY data size and read from the EEPROM
  eeprom->offset = phy_offset << 1;

  ifr.ifr_data = (void*)eeprom;
  if( ioctl( fd, SIOCETHTOOL, &ifr ) == -1 ) die( "ioctl" );

    // Extract the PHY data structure size
  uint16_t phy_cap_size = *(uint16_t*)(eeprom + 1);
  printf("PHY data struct size: 0x%04x\n", phy_cap_size);// Print PHY data size

  /*
    Get misc0
  */
  // Offset for miscellaneous (MISC) settings within PHY data
  uint16_t misc_offset = 0x8;

  int i;
  uint16_t misc0 = 0x0; // Initial value for MISC comparison
  int change_count = 0; // Counter to track changes in MISC values

    // Loop to read and compare MISC settings across 4 PHY data blocks
  for( i = 0; i < 4; ++i)
  {
    eeprom->offset = (phy_offset + misc_offset + (phy_cap_size + 1) * i) << 1;

    ifr.ifr_data = (void*)eeprom;
    if( ioctl( fd, SIOCETHTOOL, &ifr ) == -1 ) die( "ioctl" );

    uint16_t misc = *(uint16_t*)(eeprom + 1);
    printf( "MISC: 0x%04x", misc); // Print current MISC value

        // Check if the MISC setting indicates a lock
    if( misc & 0x0800 ) printf( " <- locked\n" );
    else printf( " <- unlocked\n" );

    // Track if the MISC value has changed
    if( misc != misc0 )
    {
      ++change_count;
      misc0 = misc;
    }
  }
    // If MISC values are inconsistent, exit with an error
  if( change_count > 1 ) die( "Different MISC's values" );

  /*
    Patching
  */
    // Ask the user if they want to patch (modify) the EEPROM
  printf( "Ready to fix it? [y/N]: " );
  char choice = getchar();
  switch( choice )
  {
    case 'y':
    case 'Y':
      patching = 1;// Set patching flag if user agrees
      break;
    default:
      patching = 0;// Do not patch if user declines
  }
    // If patching is enabled, modify the MISC settings to lock/unlock
  if( patching )
  {
    for( i = 0; i < 4; ++i)
    {
      eeprom->cmd = ETHTOOL_SEEPROM;// Command to set (write) EEPROM data
      eeprom->offset = (phy_offset + misc_offset + (phy_cap_size + 1) * i) << 1;

      // Flip the lock bit in the MISC setting
      *(uint16_t*)(eeprom + 1) = misc0 ^ 0x0800;
      ifr.ifr_data = (void*)eeprom;
      if (ioctl(fd, SIOCETHTOOL, &ifr) == -1) die("write");

      sleep(1); // Pause between writes
    }

    // update checksum
    // Update checksum after patching to maintain EEPROM integrity
    eeprom->cmd    = ETHTOOL_SEEPROM;
    eeprom->magic  = (devid << 16) | ((I40E_NVM_CSUM|I40E_NVM_SA) << I40E_NVM_TRANS_SHIFT) | mod;
    eeprom->len    = 2;
    eeprom->offset = 0;

    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, ethDev);
    ifr.ifr_data = (void*)eeprom;
    if (ioctl(fd, SIOCETHTOOL, &ifr) == -1) die("checksum");
  }

  return 0;
}
