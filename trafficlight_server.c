#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1
#define PIN 24
#define ROUT 17
#define GOUT 27
#define BOUT 22
#define STOP 16 //보행자 적색 신호
#define GO 20 //보행자 녹색 신호
#define VALUE_MAX 35

int sig_flag = 0;
int walk_flag = 1;
int serv_sock=-1;
int clnt_sock=-1;
char msg[2];
void error_handling(char *message){
	fputs(message,stderr);
	fputc('\n',stderr);
	exit(1);
}

static int GPIOExport(int pin)
{
#define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open export for writting!\n");
        return(-1);
    }


    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return(0);
}

static int GPIODirection(int pin, int dir)
{
    static const char s_directions_str[] = "in\0out";

#define DIRECTION_MAX 35
    //char path[DRICETION_MAX] = "/sys/class/gpio/gpio24/direction";
    char path[DIRECTION_MAX] = "/sys/class/gpio/gpio%d/direction";
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);

    fd = open(path, O_WRONLY);
    if(-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return(-1);
    }

    if(-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3))
    {
        fprintf(stderr, "Failed to set direction!\n");
        return(-1);
    }

    close(fd);
    return(0);
}

static int GPIOWrite(int pin, int value)
{
    static const char s_values_str[] = "01";

    char path[VALUE_MAX];
    int fd;

    //printf("write value!\n");

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return(-1);
    }

    if(1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1))
    {
        fprintf(stderr, "Failed to write value!\n");
        return(-1);
    }
    close(fd);
    return(0);

}

static int GPIOUnexport(int pin){
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd=open("/sys/class/gpio/unexport", O_WRONLY);
    if(-1==fd){
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return(-1);
    }
    bytes_written=snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return(0);
}

void (*breakCapture)(int);

void signalingHandler(int signo) {
    GPIOUnexport(ROUT);
    GPIOUnexport(GOUT);
    GPIOUnexport(BOUT);
    GPIOUnexport(STOP);
    GPIOUnexport(GO);
    exit(1);
}

void *sinho() {
    int sig=0;
    while(1)
    {
        if(sig % 3 == 0) // Red light
        {
            sig_flag = 0;
            if(-1 == GPIOWrite(GOUT, 0))
                exit (3);
            if(-1 == GPIOWrite(BOUT, 0))
                exit (3);
            if(-1 == GPIOWrite(ROUT, 1))
                exit (3);
            usleep(5000000);

        }
        else if(sig % 3 == 2) // Blue(Yellow) lihgt
        {
            sig_flag = 1;
            if(-1 == GPIOWrite(ROUT, 0))
                exit (3);
            if(-1 == GPIOWrite(GOUT, 0))
                exit (3);
            if(-1 == GPIOWrite(BOUT, 1))
                exit (3);
            usleep(500000);
    
        }
        else if(sig % 3 == 1) // Green light
        {
            sig_flag = 2;
            usleep(500000);
            if(-1 == GPIOWrite(ROUT, 0))
                exit (3);
            if(-1 == GPIOWrite(BOUT, 0))
                exit (3);
            if(-1 == GPIOWrite(GOUT, 1))
                exit (3);
            usleep(5000000);
            
        }
        sig ++;
    }

}
void *walker(){
    while(1)
    {
        if(sig_flag == 0){
            usleep(500000);
            walk_flag=1;
            if(-1 == GPIOWrite(STOP, 0))
                    exit (3);
            if(-1 == GPIOWrite(GO, 1))
                    exit (3);
            printf("msg : %d\n", walk_flag);
            snprintf(msg,2,"%d",walk_flag);
		    write(clnt_sock, msg, sizeof(msg));
        }
        else{
            walk_flag=0;
            if(-1 == GPIOWrite(GO, 0))
                    exit (3);
            if(-1 == GPIOWrite(STOP, 1))
                    exit (3);
            printf("msg : %d\n", walk_flag);
            snprintf(msg,2,"%d",walk_flag);
		    write(clnt_sock, msg, sizeof(msg));
        }
    }
    
}

int main(int argc, char *argv[])
{
    pthread_t p_thread[2];
    int thr_id[2];
    int status;
    char p1[] = "thread_1";
    char p2[] = "thread_2";
    
    setsid();
    umask(0);
    
    struct sockaddr_in serv_addr,clnt_addr;
	socklen_t clnt_addr_size;

    breakCapture = signal(SIGINT, signalingHandler);
    //Enable GPIO pins
    if (-1 == GPIOExport(ROUT) || -1 == GPIOExport(GOUT) || -1 == GPIOExport(BOUT) || -1 == GPIOExport(STOP) || -1 == GPIOExport(GO))
        return(1);
    
    //Set GPIO directions
    if (-1 == GPIODirection(ROUT, OUT) || -1 == GPIODirection(GOUT, OUT) || -1 == GPIODirection(BOUT, OUT) || -1 == GPIODirection(STOP, OUT) || -1 == GPIODirection(GO, OUT))
        return(2);
    
    
    thr_id[0] = pthread_create(&p_thread[0], NULL, sinho, (void *)p1);
    if(thr_id[0] < 0)
    {
       perror("thread create error : ");
       exit(0);
    }
    thr_id[1] = pthread_create(&p_thread[1], NULL, walker, (void *)p2);
    if(thr_id[1] < 0)
    {
       perror("thread create error : ");
       exit(0);
    }


    if(argc!=2){
		printf("Usage : %s <port>\n",argv[0]);
	}

	serv_sock = socket(PF_INET, SOCK_STREAM, 0);
	if(serv_sock == -1)
		error_handling("socket() error");
	
	memset(&serv_addr, 0 , sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1]));
 
	if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr))==-1)
		error_handling("bind() error");

    if(listen(serv_sock,5) == -1)
		error_handling("listen() error");

    if(clnt_sock<0){
		clnt_addr_size = sizeof(clnt_addr);
		clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr,  &clnt_addr_size);
		if(clnt_sock == -1)
		    error_handling("accept() error");
	}

    pthread_join(p_thread[0], (void**)&status);
    pthread_join(p_thread[1], (void**)&status);

    close(clnt_sock);
	close(serv_sock);

    //Disable GPIO pins
    if (-1 == GPIOUnexport(ROUT) || -1 == GPIOUnexport(GOUT) || -1 == GPIOUnexport(BOUT) || -1 == GPIOUnexport(STOP) || -1 == GPIOUnexport(GO))
        return(4);

    return(0);
}