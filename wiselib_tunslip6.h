
void cleanup(void);
void sigcleanup(int signo);
int pipe_to_tun( FILE* pipe_fd, int tun_fd );
int tun_alloc(char *dev);
void ifconf_tun(const char *tundev, const char *ipaddr);
void tun_to_pipe();
unsigned char read_stringhex( FILE* pipe_file );

#define BUFFER_SIZE 1500
#define SENDING_TO_WISEBED_MAX_SIZE 100

#define WISEBED_MODE 1
#define SERIAL_MODE 2