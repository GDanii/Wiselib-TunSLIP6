
void cleanup(void);
void sigcleanup(int signo);
int pipe_to_tun( FILE* pipe_fd, int tun_fd );
int tun_alloc(char *dev);
void ifconf_tun(const char *tundev, const char *ipaddr);
void tun_to_buffer();
void router_configuration_to_buffer();
void buffer_to_wisebed( unsigned char* tunbuffer, int size );


#define BUFFER_SIZE 1500
#define SENDING_TO_WISEBED_MAX_SIZE 140

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
