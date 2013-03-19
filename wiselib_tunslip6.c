#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h> 
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#include <ctype.h>

#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

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
int working_mode = 0;

//Wisebed listening pipe filedescriptor
FILE* wisebed_listening_file = NULL;
char * wisebed_listening_pipe = "/tmp/wisebed_listening_pipe";

//sudo ./a.out -W -Rurn:wisebed:uzl1:,9E3FD7EC778C38847F229707C300D347 -Burn:wisebed:uzl1:0x2100
//Global WISEBED parameters
char* ipaddr = NULL;
char* reservation_key = NULL;
char* config_path = NULL;
char* list_type = "line";
char* border_router_node = NULL;
char* exp_path = NULL;
char* listen_java_location = "wisebed/scripts/wb-listen.java";
char* send_java_location = "wisebed/scripts/wb-listen.java";

//IPv6 tunnel filedescriptor
int tunnel_fd = 0;
//Ipv6 tunnel name
char tundev[IFNAMSIZ];


int main(int argc, char **argv)
{
    atexit(cleanup);
    signal(SIGHUP, sigcleanup);
    signal(SIGTERM, sigcleanup);
    signal(SIGINT, sigcleanup);
    
    
    char* prog = argv[0];
    int c;
    while((c = getopt(argc, argv, "W::R::B::C::E::t::")) != -1) {
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
        
        case 'E':
        exp_path = optarg;
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
            fprintf(stderr,"usage: sudo %s [options] ipvaddress\n", prog);
            fprintf(stderr,"example: ... aaaa::1/64\n");
            fprintf(stderr,"Options are:\n");
            fprintf(stderr," -W                     Enable usage with WISEBED\n");
            fprintf(stderr," -R  reservation_code   WISEBED: Reservation code\n");
            fprintf(stderr," -B  border_router_URN  WISEBED: URN of the border router node (eg:urn:wisebed:uzl1:0x2100)\n");
            fprintf(stderr," -C  config_file        WISEBED: Place of the config file (default: wisebed/live.properties)\n");
            fprintf(stderr," -E  path               WISEBED: Experimentation Scripts directory (default: wisebed/)\n");
            fprintf(stderr," -t  tundev             Name of the IPv6 interface (default tun1)\n");
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
        error(EXIT_FAILURE,0,"No working mode selected!");
    if(working_mode == WISEBED_MODE)
    {
        if( reservation_key == NULL )
            error(EXIT_FAILURE,0,"No reservation key!");
        
        if( border_router_node == NULL )
            error(EXIT_FAILURE,0,"No border router!");
    }
    if(argv[1] == NULL)
         error(EXIT_FAILURE,0,"No IPv6 address!");
    
    //copy the IPv6 address
    ipaddr = argv[1];
    
    //Default values
    if( config_path == NULL )
    {
        config_path = (char*) malloc( 24 );
        strcpy(config_path, "wisebed/live.properties");
    }
    
    if( exp_path == NULL )
    {
        exp_path = malloc( 9 );
        strcpy(exp_path, "wisebed/");
    }
    
    if( tundev == NULL )
    {
        strcpy(tundev, "tun1");
    }
    
//     printf("things: %s %s %s %s %s", reservation_key, border_router_node, config_path, exp_path, ipaddr );

    //exit(0);
    //For IO handling
    fd_set rset, wset;
    //struct timeval timeout;
    int wisebed_listening_fd = 0;
  
    printf( "WISEBED Tunslip6 program\n" );
  
    //------- Create and open WISEBED pipe --------
    
    //Create the listening pipe WISEBED java --> pipe
    if( mkfifo(wisebed_listening_pipe, 0666) < 0 )
        error(EXIT_FAILURE, 1, "Pipe creation error");
        
    //Open the pipe
    wisebed_listening_fd = open( wisebed_listening_pipe, O_RDONLY | O_NONBLOCK );        
    if( wisebed_listening_fd < 1 )
        error(EXIT_FAILURE, 1, "Pipe opening error");
    
    //Convert filedescriptor to FILE because of fread
    wisebed_listening_file = fdopen(wisebed_listening_fd, "r");
    if( wisebed_listening_file == NULL )
        error(EXIT_FAILURE, 1, "Pipe opening error");
    
    printf( "WISEBED pipe opened\n" );
    
    
    //HACK: open a new terminal and run the command there, because if I send it to background in the same terminal, the java utilizes the CPU 100%
    ssystem( "gnome-terminal -x sh -c 'java -Dtestbed.secretreservationkeys=%s -Dtestbed.listtype=%s -jar wisebed/lib/tr.scripting-client-0.8-onejar.jar -p %s -f %s > %s' &", reservation_key, list_type, config_path, listen_java_location, wisebed_listening_pipe );
    
    //Wait 2 seconds
    sleep(2);
    
    //------- Create and open TUN interface --------
        
    //Create and open the tunnel file
    tunnel_fd = tun_alloc( tundev );
    if(tunnel_fd == -1) 
            error(EXIT_FAILURE, 1, "Tunnel opening error");
    
    printf( "Opened tunnel device ''/dev/%s''\n", tundev );
    
    //Configure the interface
    ifconf_tun( tundev, ipaddr );
    
    //Wait 2 seconds
    sleep(2);
    
    router_configuration_to_buffer();
    
  
    //Set the timeout for the select function
    //timeout.tv_sec = 1;
    //timeout.tv_usec = 0;
    
    //------- Loop I/O handling with FD_* macros --------
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
// 	printf("Wait\n");
        //Wait here, until one of the files are ready
        int ret = select(maxfd + 1, &rset, NULL, NULL, NULL);//&timeout);
        
        if( ret < 0 )
            error(EXIT_FAILURE, 1, "I/O handling (select) failed");
        //Timeout
        //else if (ret == 0 )
        //{
            //Check whether we are in the middle of a receiving process, and delete it
        //}
        else// if(ret > 0) 
        {
            
            //Test wisebed listening pipe
            if(FD_ISSET(wisebed_listening_fd, &rset)) 
            {
// 		    printf("Read from PIPE\n");
                //Read from the pipe, write to the tun interface
                pipe_to_tun( wisebed_listening_file, tunnel_fd );
            }
            
            //Tunnel
            if(FD_ISSET(tunnel_fd, &rset)) 
            {
// 		    printf("Read from TUN\n");
                //Read from the ipv6 tunnel and call the java send
                tun_to_buffer();
            }
        }
    }
}

/*---------------------------------------------------------------------*/


//Input: wisebedURN|sizeofmessage|message

#define READ_PHASE_URN 0
#define READ_PHASE_SIZE 1
#define READ_PHASE_HEX 2

int read_from_pipe_phase = 0;
unsigned char pipe_buffer[BUFFER_SIZE];
int buffer_actual_position = 0;

int pipe_to_tun( FILE* pipe_file, int tunnel_fd )
{
	//used_line indicates that the actual output line is from the selected border_router_node
	int used_line = 1;
	
	unsigned char URNbuffer[50];
    
	//read bytes for the actual message
	int read_bytes_from_hex_part = 0;
	
	//Size of the hex part (urn|SIZE|hex)
	int size_of_the_hex_part = -1;
	
	//Size of the URN for buffer storage
	int urn_size = 0;
	
// 	printf( "From pipe: Called mode: %d\n", used_line, read_from_pipe_phase);
	while(1)
	{
		unsigned char c;
		if( fread(&c, 1, 1, pipe_file ) == 0 )
		{
			clearerr(pipe_file);
			break;
		}
		
// 		printf( "Read: %i Max: %i Phase: %i \n", read_bytes_from_hex_part, size_of_the_hex_part, read_from_pipe_phase);
		
		//Process PHASE shifts, delimited with |
		if( read_from_pipe_phase != READ_PHASE_HEX && c == '|' )
		{
			//Test the URN
			if( read_from_pipe_phase == READ_PHASE_URN )
			{
				//Mark the actual sequence
				if( 0 == strncmp( border_router_node, URNbuffer, sizeof(border_router_node) ) )
					used_line = 1;
				else
					used_line = 0;
				
// 				printf( "From pipe: NODE (%s)\n", URNbuffer);
			}
// 			else
// 				printf( "From pipe: HEX size (%d)\n", size_of_the_hex_part);
			read_from_pipe_phase++;
			continue;
		}
		
		//----- PROCESS
		
		//Store the next char from the URN
		if( read_from_pipe_phase == READ_PHASE_URN )
		{
			URNbuffer[urn_size++] = c;
		}
		else if( read_from_pipe_phase == READ_PHASE_SIZE )
		{
			if( size_of_the_hex_part == -1 )
				size_of_the_hex_part = 0;
			else
				size_of_the_hex_part *= 10;
			//to ASCII
			size_of_the_hex_part += c - 48;
		}
		else //READ_PHASE_HEX
		{
			unsigned char tmp = read_stringhex( pipe_file );
			read_bytes_from_hex_part++;
			
// 			printf("READ HEX: %x\n", tmp);
			//Process only the used lines, and only read out the non used ones from the pipe
			//if( used_line == 0 )
			//	read_bytes_from_hex_part++;
			//else
			if( used_line == 1 )
			{
				if( read_bytes_from_hex_part - 1 == 0 )
				{
					
					//First byte and it is 0x69 --> this is a UART message
					if( tmp == 0x69 )
						continue;
					//First byte with anything else --> this is Debug message, drop the line
					else
						used_line = 0;
				}
				//Non first byte --> only reached if it is a byte from a UART message
				else
				{
					//Get the END and ESC escape characters
					if( tmp == SLIP_ESC )
					{
						//Escaped byte --> Read one more
						tmp = read_stringhex( pipe_file );
						read_bytes_from_hex_part++;
						
						//Escaped ESC
						if( tmp == SLIP_ESC_ESC )
							pipe_buffer[buffer_actual_position++] = SLIP_ESC;
						if( tmp == SLIP_ESC_END )
							pipe_buffer[buffer_actual_position++] = SLIP_END;
					}
					//End of the IP packet, send it to the tunnel
					else if( tmp == SLIP_END )
					{
						//If this is the initial END (buffer_actual_position == 0) then just continue
						if( buffer_actual_position > 0 )
						{
	// 						//IPv6 version check
	// 						if( (pipe_buffer[0] >> 4) == 6 )
	// 						{
	// 							//Get the IPv6 size from the header
	// 							int ipv6_payload_size = pipe_buffer[4] << 8 | (pipe_buffer[5]);
	// 							//Check the received bytes
	// 							if( (ipv6_payload_size + 40) == actual_read_bytes )
	// 							{
								
									
								//Write the buffer to the tunnel interface
								if(write(tunnel_fd, pipe_buffer, buffer_actual_position) != buffer_actual_position) 
								{
									error(EXIT_FAILURE, 1, "From pipe: Error when writing to tun");
								}
								printf( "From pipe: IPv6 packet has been writen to tun (size %i)\n", buffer_actual_position);
								
// 								int b = 0;
// 								for(b = 0; b < buffer_actual_position; b++)
// 									printf("%x ", pipe_buffer[b]);
// 								printf("\n");
								
								//Reset the storage
								buffer_actual_position = 0;
	// 							}
	// 							else
	// 							{
	// 								printf( "From pipe: IPv6 packet size error (expected: %d, captured: %d), dropped\n", ipv6_payload_size+40, actual_read_bytes );
	// 							}
	// 						}
	// 						else
	// 						{
	// 							printf( "From pipe: Non IPv6 packet, dropped\n");
	// 						}
						}
					}
					//Only store the byte
					else
					{
						pipe_buffer[buffer_actual_position++] = tmp;
					}
				}
			}
			
			
		}
		
		//End of the message, reset the phase to READ_PHASE_URN
		if( read_bytes_from_hex_part == size_of_the_hex_part )
		{
// 			int b = 0;
// 			for(b = 0; b < buffer_actual_position; b++)
// 				printf("%x ", pipe_buffer[b]);
// 			printf("\n");
// 								
// 			printf( "From pipe: End of message\n");
							
			read_from_pipe_phase = 0;
			read_bytes_from_hex_part = 0;
			size_of_the_hex_part = -1;
			urn_size = 0;
			used_line = 1;
		}
	}
}

unsigned char read_stringhex( FILE* pipe_file )
{
    unsigned char c;
    //Format for each byte from exp. scripts: "0xXX_" or "0xX_" but the 0 has been read by the caller loop
    //read 'x' from: "0xXX_"
    fread(&c, 1, 1, pipe_file );
    
    unsigned char first_part;
    //Read first_part (X) from "0xXX_"
    fread(&first_part, 1, 1, pipe_file );
    
    //ASCII conversion
    if( first_part >= 'a' )
        first_part -= 87;
    else
        first_part -= 48;
    
    //Read next from "0xXX_"
    fread(&c, 1, 1, pipe_file );

    //It was >=16 then this character is the lower part
    if( c != ' ' )
    {
        first_part *= 16;
        
        //ASCII conversion
        if( c >= 'a' )
            c -= 87;
        else
            c -= 48;
        
        first_part += c;
        
        //read and drop the space
        fread(&c, 1, 1, pipe_file );
    }
    
    return first_part;
}

/*---------------------------------------------------------------------*/

void router_configuration_to_buffer()
{
	//Construct the message with defined fix values, the address parts are padded with 0-s here
	unsigned char configbuffer[] = { C_ND_INITIALS C_IPV6_HEADER C_IPV6_SOURCE C_IPV6_DESTINATION C_PADDING_8 C_COMA C_ICMPV6_HEADER C_ABRO C_PADDING_16 C_PIO C_PADDING_16 C_6CO C_PADDING_8 };
	int size = C_SIZE;
	
	//---------------Add the hostID part to the padded places
	//TODO HACK
	unsigned char hostID[]= {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
	
	//IPv6 Target address hostID 34 - 41 and ABRO 74 - 81
	int i;
	for( i = 0; i < 8; i++ )
	{
		configbuffer[i+34] = hostID[i];
		configbuffer[i+74] = hostID[i];
	}
	
	//---------------Add the prefix part to the padded places
	//Ipv6 string to byte array conversion
	unsigned char global_address_buffer[sizeof(struct in6_addr)];
	unsigned char* ipaddr_without_prefix_len = strdup( ipaddr );
	ipaddr_without_prefix_len[strlen(ipaddr_without_prefix_len)-4] = '\0';

	if (inet_pton(AF_INET6, ipaddr_without_prefix_len, global_address_buffer) != 1)
	{
		error(EXIT_FAILURE,0,"IPv6 parsing failed! Exit");
	}
	
	//ABRO: 66 - 74, PIO: 98 - 106, 6CO 122 - 130
	for( i = 0; i < 8; i++ )
	{
		configbuffer[i+66] = global_address_buffer[i];
		configbuffer[i+98] = global_address_buffer[i];
		configbuffer[i+122] = global_address_buffer[i];
	}
	
	buffer_to_wisebed(configbuffer, size);
	
	free(ipaddr_without_prefix_len);
}

/*---------------------------------------------------------------------*/

void tun_to_buffer()
{
	unsigned char tunbuffer[BUFFER_SIZE];
	int size;

	//Read from the tun file
	if((size = read(tunnel_fd, tunbuffer, BUFFER_SIZE)) == -1) 
		error(EXIT_FAILURE, 1, "From tun: Error when reading from tun");
	
	buffer_to_wisebed( tunbuffer, size );
}

/*---------------------------------------------------------------------*/

void buffer_to_wisebed( unsigned char* tunbuffer, int size )
{
	int first_fragment = 1;
	
	if(size > 0)
	{
		int sending_shift = 0;
		do
		{ 
			int maximum_sendable_size = SENDING_TO_WISEBED_MAX_SIZE;
			
			//Determine the size of the actual message fragment,
			int actual_sending_size = maximum_sendable_size;
			
			//If the message is shorter
			if( size < actual_sending_size )
				actual_sending_size = size;
			
			
			//Parse into a tunbuffer
			unsigned char buf_str[10*actual_sending_size];
			unsigned char* buf_ptr = buf_str;
			
			//Initial 0x0A byte which is needed for the WISEBED communication
			buf_ptr += sprintf(buf_ptr, "0x%02X,", SLIP_SEND_WISEBED_INITIAL);
			
			//Initial END for the IP packet
			if( first_fragment == 1 )
			{
				buf_ptr += sprintf(buf_ptr, "0x%02X,", SLIP_END);
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
					buf_ptr += sprintf(buf_ptr, "0x%02X,", SLIP_ESC);
					buf_ptr += sprintf(buf_ptr, "0x%02X,", SLIP_ESC_END);
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
					buf_ptr += sprintf(buf_ptr, "0x%02X,", SLIP_ESC);
					buf_ptr += sprintf(buf_ptr, "0x%02X,", SLIP_ESC_ESC);
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
					buf_ptr += sprintf(buf_ptr, "0x%02X,", tunbuffer[i+sending_shift]);
			}
			//add sent bytes
			sending_shift += actual_sending_size;
			//reduce remaining size
			size -= actual_sending_size;
			
			//Add the END to the end of the IP packet
			if( size == 0 )
			{
				buf_ptr += sprintf(buf_ptr, "0x%02X,", SLIP_END);
				first_fragment = 0;
			}
			
			*(buf_ptr - 1) = '\0';
			
			//Call the java sending
			ssystem( "java -Dtestbed.secretreservationkeys=%s -Dtestbed.message=%s -Dtestbed.listtype=%s -Dtestbed.nodeurns=%s -jar wisebed/lib/tr.scripting-client-0.8-onejar.jar -p %s -f wisebed/scripts/wb-send.java", reservation_key, buf_str, list_type, border_router_node, config_path );
			//printf( "from TUN: %s size %i\n", buf_str, size );
			
			
			

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

/* radvd needs a link local address for routing */
// #if 0
/* fe80::1/64 is good enough */
    ssystem("ifconfig %s add fe80::200:0:0:1/64", tundev);
// #elif 1
// /* Generate a link local address a la sixxs/aiccu */
// /* First a full parse, stripping off the prefix length */
//   {
//     char lladdr[40];
//     char c, *ptr=(char *)ipaddr;
//     uint16_t digit,ai,a[8],cc,scc,i;
//     for(ai=0; ai<8; ai++) {
//       a[ai]=0;
//     }
//     ai=0;
//     cc=scc=0;
//     while(c=*ptr++) {
//       if(c=='/') break;
//       if(c==':') {
//         if(cc)
//           scc = ai;
//         cc = 1;
//         if(++ai>7) break;
//       } else {
//         cc=0;
//         digit = c-'0';
//         if (digit > 9) 
//           digit = 10 + (c & 0xdf) - 'A';
//         a[ai] = (a[ai] << 4) + digit;
//       }
//     }
//     /* Get # elided and shift what's after to the end */
//     cc=8-ai;
//     for(i=0;i<cc;i++) {
//       if ((8-i-cc) <= scc) {
//         a[7-i] = 0;
//       } else {
//         a[7-i] = a[8-i-cc];
//         a[8-i-cc]=0;
//       }
//     }
//     sprintf(lladdr,"fe80::%x:%x:%x:%x",a[1]&0xfefd,a[2],a[3],a[7]);
//     if (timestamp) stamptime();
//     ssystem("ifconfig %s add %s/64", tundev, lladdr);
//   }

    ssystem("ifconfig %s\n", tundev);
}

/*--------------------------------------------------------------------- */


/* Close files, delete pipe etc... */
void cleanup(void)
{
    printf( "Shut down, close files, kill the listening terminal\n" );
    if( wisebed_listening_file == NULL )
        fclose( wisebed_listening_file );
    
    if( tunnel_fd > 0 )
    {
        ssystem("ifconfig %s down", tundev);
        close( tunnel_fd );
    }
    
    ssystem( "ps aux | grep \"gnome-terminal -x sh -c java -Dtestbed.secretreservationkeys\" | awk '{print $2}' | xargs kill -9" );
    
    //Delete listening pipe
    unlink(wisebed_listening_pipe);
    
}

/*--------------------------------------------------------------------- */

void sigcleanup(int signo)
{
    exit(0);
}