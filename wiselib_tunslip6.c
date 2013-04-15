//**********************************************
//**	Wiselib IPv6 SLIP communication tool  **
//**    created by: Dániel Géhberger (2013)   **
//**********************************************

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h> 
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "wiselib_tunslip6.h"

//These are helper functions for the system calls
int ssystem(const char *fmt, ...) __attribute__((__format__ (__printf__, 1, 2)));

int ssystem(const char *fmt, ...)
{
    char cmd[2000];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    printf("%s\n", cmd);
    fflush(stdout);
    return system(cmd);
}

//Working mode of the tunneling application
int working_mode = WISEBED_MODE;

//Wisebed listening pipe filedescriptor
FILE* wisebed_listening_file = NULL;
char * wisebed_listening_pipe = "/tmp/wisebed_listening_pipe";

//Wisebed sending pipe filedescriptor
FILE* wisebed_sending_file = NULL;
char * wisebed_sending_pipe = "/tmp/wisebed_sending_pipe";

//Buffer for the global address of the border-router
unsigned char global_address_buffer[sizeof(struct in6_addr)];

//Global WISEBED parameters
//Global IP address of the tunnel
char* ipaddr = NULL;
char* reservation_key = NULL;
char* config_path = NULL;
char* list_type = "line";
//Border router node URN
char* border_router_node = NULL;
char* listen_java_location = "wisebed/scripts/wb-listen-pipe.java";
char* send_java_location = "wisebed/scripts/wb-send-pipe.java";
//Maximum size for the outgoing packets
int to_wisebed_max_size = 499;

//IPv6 tunnel filedescriptor
int tunnel_fd = 0;
//Ipv6 tunnel name
char tundev[IFNAMSIZ];

//Variables for the timestamps and keepalive function
time_t rawtime;
struct tm * timeinfo;
char time_buffer [40];
time_t last_send_time;
time_t last_listen_time;

int main(int argc, char **argv)
{
	atexit(cleanup);
	signal(SIGHUP, sigcleanup);
	signal(SIGTERM, sigcleanup);
	signal(SIGINT, sigcleanup);
	
	char* prog = argv[0];
	int c;
	while((c = getopt(argc, argv, "W::R::B::C::s::t::")) != -1) {
		switch(c) {
		case 'W':
		working_mode = WISEBED_MODE;
		break;

		case 'R':
		reservation_key = optarg;
		break;
	
		case 'B':
		border_router_node = optarg;
		break;
		
		case 'C':        
		config_path = optarg;    
		break;
		
		case 's':
		to_wisebed_max_size = (int)(*optarg);
		break;

		case 't':
		if(strncmp("/dev/", optarg, 5) == 0) {
		strncpy(tundev, optarg + 5, sizeof(tundev));
		} else {
		strncpy(tundev, optarg, sizeof(tundev));
		}
		break;

		/*case 'v':
		verbose = 2;
		if (optarg) verbose = atoi(optarg);
		break;*/
	
		case '?':
		case 'h':
		default:
		fprintf(stderr,"usage: sudo %s -W -R[RESERVATION-KEY] -B[BORDER-ROUTER-URN] [TUN-IP]\n", prog);
		fprintf(stderr,"Options are:\n");
		fprintf(stderr," -W                     Enable usage with WISEBED\n");
		fprintf(stderr," -R  reservation_code   WISEBED: Reservation code\n");
		fprintf(stderr," -B  border_router_URN  WISEBED: URN of the border router node (eg:urn:wisebed:uzl1:0x2100)\n");
		fprintf(stderr," -C  config_file        WISEBED: Place of the config file (default: wisebed/live.properties)\n");
		fprintf(stderr," -s  size               WISEBED: max fragment length to WISEBED (default 499)\n");
		fprintf(stderr," -t  tundev             Name of the IPv6 interface (default tun0)\n");
		/*fprintf(stderr," -v[level]      Verbosity level\n");
		fprintf(stderr,"    -v0         No messages\n");
		fprintf(stderr,"    -v1         Encapsulated SLIP debug messages (default)\n");
		fprintf(stderr,"    -v2         Printable strings after they are received\n");
		fprintf(stderr,"    -v3         Printable strings and SLIP packet notifications\n");
		fprintf(stderr,"    -v4         All printable characters as they are received\n");
		fprintf(stderr,"    -v5         All SLIP packets in hex\n");*/
		exit(1);
		break;
		}
	}
	argc -= (optind - 1);
	argv += (optind - 1);
	
	//Parameter check
	if(working_mode == 0)
	{
		perror( "No working mode selected!" );
		exit(EXIT_FAILURE);
	}
	if(working_mode == WISEBED_MODE)
	{
		if( reservation_key == NULL )
		{
			perror( "No reservation key!" );
			exit(EXIT_FAILURE);
		}
		
		if( border_router_node == NULL )
		{
			perror( "No border router!" );
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		perror( "Only the WISEBED mode is supported at the moment" );
		exit(EXIT_FAILURE);
	}
	
	if(argv[1] == NULL)
	{
		perror( "No IPv6 address!" );
		exit(EXIT_FAILURE);
	}
	
	//copy the IPv6 address
	ipaddr = argv[1];
	
	//Default values
	if( config_path == NULL )
	{
		config_path = (char*) malloc( 24 );
		strcpy(config_path, "wisebed/live.properties");
	}
	
	if( tundev == NULL )
	{
		strcpy(tundev, "tun0");
	}
	
	//     printf("things: %s %s %s %s", reservation_key, border_router_node, config_path, ipaddr );

	//For IO handling
	fd_set rset;
	//struct timeval timeout;
	int wisebed_listening_fd = 0;
	
	printf( "WISEBED Tunslip6 program\n" );
	
	//---------------------------- Create and open WISEBED pipe -----------------------------
	
	//****** LISTEN ********
	//Create the listening pipe WISEBED java --> pipe
	if( access(wisebed_listening_pipe, F_OK ) != 0 )
		if( mkfifo(wisebed_listening_pipe, 0666) < 0 )
		{
			perror( "L Pipe creation error (/tmp/wisebed_listening_pipe)" );
			exit(EXIT_FAILURE);
		}
		
	//Open the pipe
	wisebed_listening_fd = open( wisebed_listening_pipe, O_RDONLY | O_NONBLOCK );        
	if( wisebed_listening_fd < 1 )
	{
		perror( "L Pipe opening error" );
		exit(EXIT_FAILURE);
	}
	
	//Convert filedescriptor to FILE because of fread
	wisebed_listening_file = fdopen(wisebed_listening_fd, "r");
	if( wisebed_listening_file == NULL )
	{
		perror( "L Pipe opening error" );
		exit(EXIT_FAILURE);
	}
	
	//******* SEND **********
	//Create the sending pipe
	if( access(wisebed_sending_pipe, F_OK ) != 0 )
		if( mkfifo(wisebed_sending_pipe, 0666) < 0 )
		{
			perror( "S Pipe creation error (/tmp/wisebed_sending_pipe)" );
			exit(EXIT_FAILURE);
		}
	
	//NOTE: The file is opened only when there is something to write
	
	//Start the Wisebed send script
	ssystem( "java -Dtestbed.secretreservationkeys=%s -Dtestbed.listtype=%s -Dtestbed.nodeurns=%s -Dtestbed.max_size=%i -jar wisebed/lib/tr.scripting-client-0.8-onejar.jar -p %s -f %s &", reservation_key, list_type, border_router_node, to_wisebed_max_size, config_path, send_java_location );
	
	//Wait 3 seconds to establish the connection
	sleep(3);    
	
	//Start the Wisebed listen script
	ssystem( "java -Dtestbed.secretreservationkeys=%s -Dtestbed.listtype=%s -Dtestbed.nodeurns=%s -jar wisebed/lib/tr.scripting-client-0.8-onejar.jar -p %s -f %s &", reservation_key, list_type, border_router_node, config_path, listen_java_location );
	
	//Wait 3 seconds to establish the connection
	sleep(3);

	//---------------------------- Create and open TUN interface -----------------------------
		
	//Create and open the tunnel file
	tunnel_fd = tun_alloc( tundev );
	if(tunnel_fd == -1) 
	{
		perror(  "Tunnel opening error" );
		exit(EXIT_FAILURE);
	}
	
	printf( "Opened tunnel device ''/dev/%s''\n", tundev );
	
	//Configure the interface
	ifconf_tun( tundev, ipaddr );
	
	//---------------------------- Loop I/O handling with FD_* macros -----------------------------
	while(1)
	{
		//This is for the select command
		int maxfd = 0;
		
		//Reset the sets
		FD_ZERO(&rset);
		
		//Set the rset for the listening pipe
		FD_SET(wisebed_listening_fd, &rset);
		if(wisebed_listening_fd > maxfd) maxfd = wisebed_listening_fd;
		//Set the rset for the tunnel
		FD_SET(tunnel_fd, &rset);
		if(tunnel_fd > maxfd) maxfd = tunnel_fd;
		
		/* Initialize the timeout data structure. */
		struct timeval timeout;
		timeout.tv_sec = 120;
		timeout.tv_usec = 0;
		
		//Wait here, until one of the files are ready or the timer expires
		int ret = select(maxfd + 1, &rset, NULL, NULL, &timeout);
		
		if( ret < 0 )
		{
			perror( "I/O handling (select) failed" );
			exit(EXIT_FAILURE);
		}
		
		//Timeout
		else if( ret == 0 )
		{
			//Send a ping to the border router to keep the connection alive
			//after 2 minutes if there wasn't any activity
			if( working_mode == WISEBED_MODE )
			{
				time_t actual_time;
				time (&actual_time);
				if(  difftime(actual_time,last_listen_time) > 119 || difftime(actual_time,last_send_time) > 119 )
				{
					printf("Keepalive ping: ");
					
					char strR[INET6_ADDRSTRLEN];
					if( inet_ntop(AF_INET6, global_address_buffer, strR, INET6_ADDRSTRLEN) != NULL )
						ssystem( "ping6 -c 1 -s 0 %s > /dev/null &", strR );
					else
						printf(" address parsing failed, is there a border router?\n" );
				}
			}
		}
		else
		{
			//Test wisebed listening pipe
			if(FD_ISSET(wisebed_listening_fd, &rset)) 
			{
				//Read from the pipe, write to the tun interface
				pipe_to_tun( wisebed_listening_file, tunnel_fd );
			}
			
			//Tunnel
			if(FD_ISSET(tunnel_fd, &rset)) 
			{
				//Read from the ipv6 tunnel and call the java send
				tun_to_buffer();
			}
		}
	}
}

/*---------------------------------------------------------------------*/

//Buffer to store the IP message
unsigned char pipe_buffer[BUFFER_SIZE];
int buffer_actual_position = 0;

void pipe_to_tun( FILE* pipe_file, int tunnel_fd )
{
	while(1)
	{
		unsigned char c;
		if( fread(&c, 1, 1, pipe_file ) == 0 )
		{
			clearerr(pipe_file);
			break;
		}

		//Get the END and ESC escape characters
		if( c == SLIP_ESC )
		{
			//Escaped byte --> Read one more
			if( fread(&c, 1, 1, pipe_file ) == 0 )
			{
				perror( "Pipe to TUN SLIP_ESC error" );
				exit(EXIT_FAILURE);
			}
			
			//Escaped ESC
			if( c == SLIP_ESC_ESC )
				pipe_buffer[buffer_actual_position++] = SLIP_ESC;
			if( c == SLIP_ESC_END )
				pipe_buffer[buffer_actual_position++] = SLIP_END;
		}
		else if( c == SLIP_END )
		{
			//If this is the initial END (buffer_actual_position == 0) then just continue
			if( buffer_actual_position > 0 )
			{
				//Simple IPv6 tests
				int ipv6_payload_size = pipe_buffer[4] << 8 | (pipe_buffer[5]);
				if( ((pipe_buffer[0] >> 4) == 6) && ((ipv6_payload_size + 40) == buffer_actual_position) )
				{
					//Detect Router Solicitation from the node
					//[6] --> ICMPv6, [40] --> RS
					if ( pipe_buffer[6] == 0x3a && pipe_buffer[40] == 0x85 )
					{
						router_configuration_to_buffer( pipe_buffer, buffer_actual_position );
					}
					//Send to the TUN
					else
					{
						//Write the buffer to the tunnel interface
						if(write(tunnel_fd, pipe_buffer, buffer_actual_position) != buffer_actual_position) 
						{
							perror( "From pipe: Error when writing to tun" );
							exit(EXIT_FAILURE);
						}
						
						time (&last_listen_time);
						timeinfo = localtime (&last_listen_time);
						strftime (time_buffer,40,"%r",timeinfo);
						printf( "%s: From pipe: IPv6 packet has been written to tun (size %i)\n", time_buffer, buffer_actual_position);
					}
				}
				else
				{
					printf( "From pipe: Non IPv6 packet, dropped\n");
				}
				
				//Reset the storage
				buffer_actual_position = 0;
			}
		}
		//Only store the byte
		else
		{
			pipe_buffer[buffer_actual_position++] = c;
		}
	}
}

/*---------------------------------------------------------------------*/

void router_configuration_to_buffer( unsigned char* RS_buffer, int RS_size )
{
	//Construct the message with defined fix values, the address parts are padded with 0-s here
	unsigned char configbuffer[] = { C_ND_INITIALS C_IPV6_HEADER C_IPV6_SOURCE C_IPV6_DESTINATION C_PADDING_8 C_COMA C_ICMPV6_HEADER C_ABRO C_PADDING_16 C_PIO C_PADDING_16 C_6CO C_PADDING_8 };
	int size = C_SIZE;
	
	//---------------Add the hostID part to the padded places, node ID from the RS message
	int i;
	unsigned char hostID[8];
	for( i = 0; i < 8; i++ )
		hostID[i] = RS_buffer[i+16];
	
	//IPv6 Target address hostID 34 - 41 and ABRO 74 - 81
	for( i = 0; i < 8; i++ )
	{
		configbuffer[i+34] = hostID[i];
		configbuffer[i+74] = hostID[i];
	}
	
	//---------------Add the prefix part to the padded places
	//Ipv6 string to byte array conversion
	char* ipaddr_without_prefix_len = strdup( ipaddr );
	ipaddr_without_prefix_len[strlen(ipaddr_without_prefix_len)-4] = '\0';
	
	if (inet_pton(AF_INET6, ipaddr_without_prefix_len, global_address_buffer) != 1)
	{
		perror( "IPv6 parsing failed! Exit (Please do not skip zeros from the last part: aaaa::200:0:0:1 --> aaaa::200:0:0:0001)" );
		exit(EXIT_FAILURE);
	}
	
	//ABRO: 66 - 74, PIO: 98 - 106, 6CO 122 - 130
	for( i = 0; i < 8; i++ )
	{
		//Generate the border router's global IPv6 address in parelel
		global_address_buffer[i+8] = hostID[i];
		
		//Set the prefix parts in the message
		configbuffer[i+66] = global_address_buffer[i];
		configbuffer[i+98] = global_address_buffer[i];
		configbuffer[i+122] = global_address_buffer[i];
	}
	
	char strU[INET6_ADDRSTRLEN];
	char strR[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET6, global_address_buffer, strU, INET6_ADDRSTRLEN);
	
	//Convert to Radio address
	global_address_buffer[14] &= 0x7F;
	inet_ntop(AF_INET6, global_address_buffer, strR, INET6_ADDRSTRLEN);
	printf( " Border router addresses: UART: %s RADIO: %s\n", strU, strR );
	
	//send the buffer to the mote
	buffer_to_border_router(configbuffer, size);
	
	free(ipaddr_without_prefix_len);
}

/*---------------------------------------------------------------------*/

void tun_to_buffer()
{
	unsigned char tunbuffer[BUFFER_SIZE];
	int size;
	
	//Read from the tun file
	if((size = read(tunnel_fd, tunbuffer, BUFFER_SIZE)) == -1)
	{
		perror( "From tun: Error when reading from tun" );
		exit(EXIT_FAILURE);
	}
	
	//send the buffer to the mote
	buffer_to_border_router( tunbuffer, size );
}

/*---------------------------------------------------------------------*/

void buffer_to_border_router( unsigned char* tunbuffer, int size )
{
	//Indicate that the initial END is needed
	int first_fragment = 1;
	
	if(size > 0)
	{
		int sending_shift = 0;
		do
		{
			int maximum_sendable_size = to_wisebed_max_size;
			
			//Determine the size of the actual message fragment,
			int actual_sending_size = maximum_sendable_size;
			
			//If the message is shorter
			if( size < actual_sending_size )
				actual_sending_size = size;
			
			
			//Parse into a tunbuffer
			unsigned char buf_str[actual_sending_size];
			int buffer_actual_position = 0;
			
			//Initial 0x0A byte which is needed for the WISEBED communication
			buf_str[buffer_actual_position++] = SLIP_SEND_WISEBED_INITIAL;
			
			//Initial END for the IP packet
			if( first_fragment == 1 )
			{
				buf_str[buffer_actual_position++] = SLIP_END;
				first_fragment = 0;
				//Reduce the max bytes to prevent longer messages than expected if it is needed
				if( actual_sending_size == maximum_sendable_size )
				{
					maximum_sendable_size--;
					actual_sending_size--;
				}
			}
			
			int i;
			for (i = 0; i < actual_sending_size; i++)
			{
				//Escape the ESC and END characters
				if( tunbuffer[i+sending_shift] == SLIP_END )
				{
					buf_str[buffer_actual_position++] = SLIP_ESC;
					buf_str[buffer_actual_position++] = SLIP_ESC_END;
					//Reduce the max bytes to prevent longer messages than expected if it is needed
					//If this was the last byte in the loop, then the message will be longer...
					//but do not loose a byte from the IP packet
					if( actual_sending_size == maximum_sendable_size && i < (actual_sending_size-1) )
					{
						maximum_sendable_size--;
						actual_sending_size--;
					}
				}
				else if( tunbuffer[i+sending_shift] == SLIP_ESC )
				{
					buf_str[buffer_actual_position++] = SLIP_ESC;
					buf_str[buffer_actual_position++] = SLIP_ESC_ESC;
					//Reduce the max bytes to prevent longer messages than expected if it is needed
					//If this was the last byte in the loop, then the message will be longer...
					//but do not loose a byte from the IP packet
					if( actual_sending_size == maximum_sendable_size && i < (actual_sending_size-1) )
					{
						maximum_sendable_size--;
						actual_sending_size--;
					}
				}
				else
					buf_str[buffer_actual_position++] = tunbuffer[i+sending_shift];
			}
			//add sent bytes
			sending_shift += actual_sending_size;
			//reduce remaining size
			size -= actual_sending_size;
			
			//Add the byte END to the end of the IP packet
			if( size == 0 )
			{
				buf_str[buffer_actual_position++] = SLIP_END;
				first_fragment = 0;
			}
			
			time (&last_send_time);
			timeinfo = localtime (&last_send_time);
			strftime (time_buffer,40,"%r",timeinfo);
			printf( "%s: To WISEBED size %i\n", time_buffer, buffer_actual_position );
			
			//Open the pipe
			int wisebed_sending_fd = open( wisebed_sending_pipe, O_RDWR | O_NONBLOCK );        
			if( wisebed_sending_fd < 1 )
			{
				perror( "S Pipe opening error" );
				exit(EXIT_FAILURE);
			}
			
			//Convert filedescriptor to FILE because of fwrite
			wisebed_sending_file = fdopen(wisebed_sending_fd, "w");
			if( wisebed_sending_file == NULL )
			{
				perror( "S Pipe opening error" );
				exit(EXIT_FAILURE);
			}
			
			if( fwrite(buf_str, 1, buffer_actual_position, wisebed_sending_file ) == 0 )
			{
				perror( "To WISEBED: Error when writing to pipe" );
				exit(EXIT_FAILURE);
			}
			
			fclose( wisebed_sending_file );
			close(wisebed_sending_fd);
		}while( size > 0 );
	}
}

/*---------------------------------------------------------------------*/

int tun_alloc(char *dev)
{
	struct ifreq ifr;
	int fd, err;

	/* open the clone device */
	if( (fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
		return -1;
	}
	
	/* preparation of the struct ifr, of type "struct ifreq" */
	memset(&ifr, 0, sizeof(ifr));

	/* Flags: IFF_TUN   - TUN device (no Ethernet headers)
	*        IFF_NO_PI - Do not provide packet information
	*/
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	
	//Device name specified copy it
	if (*dev)
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);

	/* try to create the device */
	if((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
		close(fd);
		return err;
	}
	
	/* if the operation was successful, write back the name of the
	* interface to the variable "dev", so the caller can know it */
	strcpy(dev, ifr.ifr_name);
	
	return fd;

}

/*---------------------------------------------------------------------*/

void ifconf_tun(const char *tundev, const char *ipaddr)
{
	ssystem("ifconfig %s inet `hostname` up", tundev);
	ssystem("ifconfig %s add %s", tundev, ipaddr);
	ssystem("ifconfig %s add fe80::200:0:0:1/64", tundev);
	ssystem("ifconfig %s\n", tundev);
}

/*--------------------------------------------------------------------- */


/* Close files, delete pipe etc... */
void cleanup(void)
{
	printf( "Shut down, close files, kill the WISEBED communication\n" );
	if( wisebed_listening_file != NULL )
		fclose( wisebed_listening_file );
	
	if( tunnel_fd > 0 )
	{
		ssystem("ifconfig %s down", tundev);
		close( tunnel_fd );
	}
	
	ssystem( "ps aux | grep \"java -Dtestbed.secretreservationkeys\" | awk '{print $2}' | xargs kill -9" );
	
	ssystem( "ps aux | grep \"java -Dtestbed.secretreservationkeys\" | awk '{print $2}' | xargs kill -9" );
	
	//Delete the pipes
	unlink(wisebed_listening_pipe);
	unlink(wisebed_sending_pipe);   
}

/*--------------------------------------------------------------------- */

void sigcleanup(int signo)
{
    exit(0);
}
