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

//urn:wisebed:uzl1:,593972096FE9C1633325DC08C872FCF9
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
    
    printf("things: %s %s %s %s %s", reservation_key, border_router_node, config_path, exp_path, ipaddr );
    
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
    
    //Hack:
    //char* ipaddr = "2001:630:301:6453::1/64";
    
    //Configure the interface
    ifconf_tun( tundev, ipaddr );
    
  
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
        //Set the rset for the tunnel //NOTE TURNED OFF
        //FD_SET(tunnel_fd, &rset);
        //if(tunnel_fd > maxfd) maxfd = tunnel_fd;
    
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
                //Read from the pipe, write to the tun interface
                pipe_to_tun( wisebed_listening_file, tunnel_fd );
            }
            
            //Tunnel
            if(FD_ISSET(tunnel_fd, &rset)) 
            {
                //Read from the ipv6 tunnel and call the java send
                tun_to_pipe();
            }
        }
    }
}

/*---------------------------------------------------------------------*/

#define READ_PHASE_DATE 0
#define READ_PHASE_PIPE1 1
#define READ_PHASE_URN 2
#define READ_PHASE_PIPE2 3
#define READ_PHASE_STRING 4
#define READ_PHASE_PIPE3 5
#define READ_PHASE_HEX 6
#define READ_PHASE_NL 7
int read_from_pipe_phase = 0;
int used_sequence = 1;
//int synchronized = 0;

int pipe_to_tun( FILE* pipe_file, int tunnel_fd )
{
    unsigned char buffer[BUFFER_SIZE];
    int used_line = 1;
    
    //We interested in only the URN and the HEX
    
    if( (read_from_pipe_phase != READ_PHASE_URN) && (read_from_pipe_phase != READ_PHASE_HEX) )
        used_line = 0;
    
    
    int i = 0;
    for(i=0; i < BUFFER_SIZE; i++ )
         buffer[i] = 0;
    
    
    int actual_read_bytes = 0;
    
//     printf( "From pipe: UsedL: %d Called mode: %d\n", used_line, read_from_pipe_phase);
    while(1)
    {
        unsigned char c;
        if( fread(&c, 1, 1, pipe_file ) == 0 )
        {
            clearerr(pipe_file);
            break;
        }
        
        //Process only if it is a needed line
        if( used_line )
        {
            //Store the next char from the URN
            if( read_from_pipe_phase == READ_PHASE_URN )
            {
                buffer[actual_read_bytes++] = c;
            }
            else //READ_PHASE_HEX
            {
                unsigned char tmp = read_stringhex( pipe_file );
                
                if( actual_read_bytes == 0 )
                {
                    //First byte and it is 0x69 --> this is a UART message
                    if( tmp == 0x69 )
                    {
                        actual_read_bytes++;
                        continue;
                    }
                    //First byte with anything else --> this is Debug message, drop the line
                    else
                        used_line = 0;
                }
                //Non first byte --> only reached if it is a byte from a UART message
                else
                {   
                    // -1 because of the initial byte
                    buffer[actual_read_bytes-1] = tmp;
                    actual_read_bytes++;
                }
            }
        }
        //Drop the non needed lines --> DEBUG read
        else        
            buffer[actual_read_bytes++] = c;    
        
    }
    
    
    
//     if( read_from_pipe_phase == READ_PHASE_HEX )
        printf( "From pipe:Phase %d Line: %d sequence: %d Read bytes from pipe: %i char: %s\n", read_from_pipe_phase, used_line, used_sequence, actual_read_bytes, buffer);
    
    //If this is a URN, then it against the configured border router
    if( read_from_pipe_phase == READ_PHASE_URN )
    {
        //Mark the actual sequence
        if( 0 == strncmp( border_router_node, buffer, sizeof(border_router_node) ) )
            used_sequence = 1;
        else
            used_sequence = 0;
        
        printf( "From pipe: NODE (%s)\n", buffer);
    }    
    else if( read_from_pipe_phase == READ_PHASE_HEX && used_line && used_sequence )
    {
        //Correct the first byte
        actual_read_bytes -= 1;
        
        printf( "From pipe: Read bytes from pipe: %i\n", actual_read_bytes);
        
//         int b = 0;
//         for(b = 0; b < actual_read_bytes; b++)
//             printf("%x ", buffer[b]);
        
        //IPv6 version check
        if( (buffer[0] >> 4) == 6 )
        {
            //Get the IPv6 size from the header
            int ipv6_payload_size = buffer[4] << 8 | (buffer[5]);
            //Check the received bytes
            if( (ipv6_payload_size + 40) == actual_read_bytes )
            {
                //Write the buffer to the tunnel interface
                if(write(tunnel_fd, buffer, actual_read_bytes) != actual_read_bytes) 
                {
                    error(EXIT_FAILURE, 1, "From pipe: Error when writing to tun");
                }
                printf( "From pipe: IPv6 packet has been writen to tun\n");
            }
            else
            {
                printf( "From pipe: IPv6 packet size error (expected: %d, captured: %d), dropped\n", ipv6_payload_size+40, actual_read_bytes );
            }
        }
        else
        {
            printf( "From pipe: Non IPv6 packet, dropped\n");
        }
    }
    
    //Next phase modulo 8
    read_from_pipe_phase += 1;
    read_from_pipe_phase %= 8;
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

void tun_to_pipe()
{
    unsigned char tunbuffer[BUFFER_SIZE];
    int size;

    //Read from the tun file
    if((size = read(tunnel_fd, tunbuffer, BUFFER_SIZE)) == -1) 
        error(EXIT_FAILURE, 1, "From tun: Error when reading from tun");
    
    if(size > 0)
    {
            int sending_shift = 0;
            do
            {   
                    //Determine the size of the actual message fragment
                    int actual_sending_size = SENDING_TO_WISEBED_MAX_SIZE;
                    
                    //If the message is shorter
                    if( size < actual_sending_size )
                        actual_sending_size = size;
                    
                    
                    //Parse into the tunbuffer
                    int i;
                    unsigned char* buf_str = (unsigned char*) malloc (5*actual_sending_size);
                    unsigned char* buf_ptr = buf_str;
                    for (i = 0; i < size; i++)
                    {
                        buf_ptr += sprintf(buf_ptr, "0x%02X,", tunbuffer[i+sending_shift]);
                    }
                    *(buf_ptr - 1) = '\0';
                    
                    //Call the java sending
                    //ssystem( "java -Dtestbed.secretreservationkeys=%s -Dtestbed.message=%s -Dtestbed.listtype=%s -Dtestbed.nodeurns=%s -jar wisebed/lib/tr.scripting-client-0.8-onejar.jar -p %s -f wisebed/scripts/wb-send.java", reservation_key, buf_str, list_type, border_router_node, config_path );
                    printf( "from TUN: %s\n", buf_str );
                    
                    free( buf_str );
                    
                    //add sent bytes
                    sending_shift += actual_sending_size;
                    //reduce remaining size
                    size -= actual_sending_size;
                    

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