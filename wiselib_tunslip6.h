//**********************************************
//**	Wiselib IPv6 SLIP communication tool  **
//**    created by: Dániel Géhberger (2013)   **
//**********************************************

/**
 * This program is a SLIP (Serial Line IP - RFC1055) implementation for the IPv6 protocol stack of the Wiselib.
 * The tool has only one working mode at the moment which is a communication with nodes in the Wisebed testbed environment.
 * (Support for direct attached devices is planned)
 * The tool uses the modified java scripts of the Wisebed experimentation scripts for the communication. This program starts
 * the listen and send scripts as backgorund processes, and uses linux named pipes for the communication between the
 * this C and the java programs.
 * 
 * The code for the sensor devices is available in the Wisebed's GIT: https://github.com/ibr-alg/wiselib
 * For the border-router node, the IPv6_SLIP_RS must be defined in the wiselib.testing/algorithms/6lowpan/lowpan_config.h
 * The nodes can be reserved with the experimentation scripts
 * 	https://github.com/wisebed/experimentation-scripts/wiki/Experimentation-Scripts-0.8
 * 
 * Then this tool can be started:
 * 
 * make
 * sudo ./wiselib_tunslip6 -W -R[RESERVATION-KEY] -B[BORDER-ROUTER-URN] [TUN-IP]
 * 
 * eg.: sudo ./wiselib_tunslip6 -W -Rurn:wisebed:uzl1:,823DF1FFC891875533D5F62B22A9AD24 -Burn:wisebed:uzl1:0x2140 aaaa::1/64
 */


/**
 * This function allocates the resoures for the TUN virtual interface
 */
int tun_alloc(char *dev);

/**
 * Configure the TUN interface and routing
 */
void ifconf_tun(const char *tundev, const char *ipaddr);

/** 
 * Called at shutdown of the application (registered fog signals as well)
 * Close files, kill the wisebed communication scripts
 * Delete pipes
 */
void cleanup(void);

/**
 * Only calls cleanup()
 */
void sigcleanup(int signo);

/**
 * This function is called by the main loop when there is some data from the pipe (Wisebed)
 * It reads all bytes from the pipe, which can be more then one packet.
 * Based on the SLIP bytes it writes the packets to the TUN interface's file
 */
void pipe_to_tun( FILE* pipe_fd, int tun_fd );

/**
 * This function is called by the main loop when there is some data from the TUN interface.
 * It only copies the packet into a temporary buffer and calls the buffer_to_border_router function.
 */
void tun_to_buffer();

/**
 * This function is called by the pipe_to_tun when there is a Router Sollicitation message from the border-router
 * The Router Advertisement message is mostly predefined, this function adds the IP addresses and calls
 * the buffer_to_border_router function
 */
void router_configuration_to_buffer( unsigned char* RS_buffer, int RS_size );

/**
 * This function gets a buffer with a normal IPv6 packet.
 * It encapsulates the packet with the SLIP protocol into an innner buffer, opens the pipe of the outgoing packets
 * and writes the conent into it.
 * This function supports the fragmentation of the packet, which can be configured with the -s option at startup, but
 * it is 1500 bytes by default (no fragmentation).
 */
void buffer_to_border_router( unsigned char* tunbuffer, int size );

//Buffer size for the IP packets
#define BUFFER_SIZE 1500

//Working modes
#define WISEBED_MODE 1
#define SERIAL_MODE 2

//SLIP special characters
#define SLIP_END 192 //0300 - 0xCO
#define SLIP_ESC 219 //0333 - 0xDB
#define SLIP_ESC_END 220 //0334 - 0xDC
#define SLIP_ESC_ESC 221 //0335 0xDD
#define SLIP_SEND_WISEBED_INITIAL 10 //0x0A


//------------------------------- ND config message -------------------------------

#define C_ND_INITIALS 0xCF,0x00,
#define C_IPV6_HEADER 0x60,0x0,0x0,0x0,0x0,0x58,0x3a,0xff,
#define C_IPV6_SOURCE 0xfe,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,
#define C_IPV6_DESTINATION 0xfe,0x80,0x0,0x0,0x0,0x0,0x0,0x0,
#define C_PADDING_8 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
#define C_COMA ,
//HOST PADDING indexes: 34 - 43
//0x0,0x0,0x0,0x0,0x0,0x0,0x21,0x40,

#define C_ICMPV6_HEADER 0x86,0x0,0x0,0x0,0xff,0x0,0x7,0x8,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,

#define C_ABRO 0x23,0x3,0x0,0x1,0x0,0x0,0x27,0x10,
#define C_PADDING_16 0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
//0x20,0x1,0x63,0x80,0x70,0xa0,0xb0,0x69,0x0,0x0,0x0,0x0,0x0,0x0,0x21,0x40
//PREFIX PADDING indexes: 66 - 74
//HOST PADDING indexes: 74 - 81

#define C_PIO 0x3,0x4,0x40,0xc0,0x0,0x27,0x8d,0x0,0x0,0x9,0x3a,0x80,0x0,0x0,0x0,0x0,
//0x20,0x1,0x63,0x80,0x70,0xa0,0xb0,0x69,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
//PREFIX PADDING indexes: 98 - 105

#define C_6CO 0x22,0x2,0x40,0x11,0x0,0x0,0xff,0xff,
//0x20,0x1,0x63,0x80,0x70,0xa0,0xb0,0x69
//PREFIX PADDING indexes: 122 - 129

#define C_SIZE 130
//Size: 130 bytes
