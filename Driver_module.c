#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>


#define BUFFER_MAX 30
#define DIRECTION_MAX 45

#define IN 0
#define OUT 1
#define PWM 0

#define LOW 0
#define HIGH 1
#define VALUE_MAX 256

#define PIN 24
#define POUT 23
#define POUT2 18

double distance = 0;
char msg[2];
int sock;
struct sockaddr_in serv_addr;
int str_len;
int signal;

static int PWMExport(int pwmnum) {
   char buffer[BUFFER_MAX];
   int bytes_written;
   int fd;
   
   fd = open("/sys/class/pwm/pwmchip0/export", O_WRONLY);
   if(-1 == fd){
      fprintf(stderr,"Failed to open in export!\n");
      return (-1);
   }
   bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
   write(fd, buffer, bytes_written);
   close(fd);
   sleep(1);
   return (0);
}

static int PWMUnexport(int pwmnum) {
   char buffer[BUFFER_MAX];
   int bytes_written;
   int fd;
   
   fd = open("/sys/class/pwm/pwmchip0/unexport", O_WRONLY);
   if(-1 == fd){
      fprintf(stderr,"Failed to open in unexport!\n");
      return (-1);
   }
   
   bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
   write(fd, buffer, bytes_written);
   close(fd);
   
   sleep(1);
   return (0);
}

static int PWMEnable(int pwmnum)
{
    static const char s_unenable_str[] = "0";
    static const char s_enable_str[] = "1";
    
   char path[DIRECTION_MAX];
   int fd;
   
   snprintf(path, DIRECTION_MAX, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);
   fd = open(path, O_WRONLY);
   if(-1 == fd) {
      fprintf(stderr,"Failed to open in enable!\n");
      return (-1);
   }
   
   write(fd, s_unenable_str, strlen(s_unenable_str));
   close(fd);
   
   fd = open(path, O_WRONLY);
   if(-1 == fd) {
      fprintf(stderr,"Failed to open in enable!\n");
      return (-1);
   }
   
   write(fd, s_enable_str, strlen(s_enable_str));
   close(fd);
   return(0);
}

static int PWMunable(int pwmnum)
{
    static const char s_unable_str[] = "0";
    
    char path[DIRECTION_MAX];
    int fd;
   
    snprintf(path, DIRECTION_MAX, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);
    fd = open(path, O_WRONLY);
    if(-1 == fd) {
       fprintf(stderr,"Failed to open in enable!\n");
       return (-1);
    }
   
    write(fd, s_unable_str, strlen(s_unable_str));
    close(fd);

    return(0);
}

static int PWMWritePeriod(int pwmnum, int value)
{
    char s_values_str[VALUE_MAX];
    char path[VALUE_MAX];
    int fd, byte;

    snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/period", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in period!\n");
        return(-1);
    }
    
    byte = snprintf(s_values_str, VALUE_MAX, "%d", value);
    
    if(-1 == write(fd, s_values_str, byte))
    {
        fprintf(stderr, "Failed to write value in period!\n");
   close(fd);
        return(-1);
    }
    close(fd);
    return(0);

}

static int PWMWriteDutyCycle(int pwmnum, int value)
{
    char path[VALUE_MAX];
    char s_values_str[VALUE_MAX];
    int fd,byte;

    snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/duty_cycle", pwmnum);
    fd = open(path,  O_WRONLY);
    if(-1 == fd)
    {
        fprintf(stderr, "Failed to open in duty_cycle!\n");
        return(-1);
    }

    byte = snprintf(s_values_str, VALUE_MAX, "%d", value);

    if(-1 == write(fd, s_values_str, byte))
    {
        fprintf(stderr, "Failed to write value! in duty_cycle\n");
        close(fd);
        return(-1);
    }

    close(fd);
    return(0);
}

static int GPIOExport(int pin) {

   char buffer[BUFFER_MAX];
   ssize_t bytes_written;
   int fd;
   
   fd = open("/sys/class/gpio/export", O_WRONLY);
   if(-1 == fd){
      fprintf(stderr,"Failed to open export for writing!\n");
      return (-1);
   }
   
   bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
   write(fd, buffer, bytes_written);
   close(fd);
   return (0);
}

static int GPIOUnexport(int pin) {
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;
   
    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if(-1 == fd){
          fprintf(stderr,"Failed to open unexport for writing!\n");
        return (-1);
    }
   
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int GPIODirection(int pin, int dir) {
    static const char s_directions_str[] = "in\0out";
    char path[DIRECTION_MAX]="/sys/class/gpio/gpio%d/direction";
    int fd;
   
    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
   
    fd = open(path, O_WRONLY);
    if(-1 == fd) {
        fprintf(stderr,"Failed to open gpio for writing!\n");
        return (-1);
    }
    if(-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2: 3)) {
        fprintf(stderr,"Failed to set direction!\n");
        return (-1);
    }
   
    close(fd);
    return(0);
}

static int GPIORead(int pin){
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path,VALUE_MAX,"/sys/class/gpio/gpio%d/value", pin);
    fd=open(path,O_RDONLY);
    if(-1==fd){
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return(-1);
    }
    if(-1==read(fd,value_str,3)){
        fprintf(stderr, "Failed to read value!\n");
        return(-1);
    }

    close(fd);

    return(atoi(value_str));
}

static int GPIOWrite(int pin, int value){
    static const char s_values_str[]="01";

    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value",pin);
    fd=open(path,O_WRONLY);
    if(-1==fd){
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return(-1);
    }

    if(1!=write(fd, &s_values_str[LOW==value?0:1],1)){
        fprintf(stderr, "Failed to write value!\n");
        return(-1);
    }
    close(fd);
    return(0);
    
}


void error_handling(char *message){
    
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

void *ultrawave_thd(){
    clock_t start_t, end_t;
    double time;

    usleep(10000);

    //init ultrawave trigger
    GPIOWrite(POUT, 0);
    usleep(10000);

    // start
    while(1)
    {
        if (-1 == GPIOWrite(POUT, 1))
        {
            printf("gpio write/tirgger err\n");
            exit(0);
        }

        //1sec == 1000000ultra_sec, ims = 1000ultra_sec
        usleep(10);
        GPIOWrite(POUT, 0);

        while(GPIORead(PIN) == 0)
        {
            start_t = clock();
        }
        while(GPIORead(PIN) == 1)
        {
            end_t = clock();
        }
        
        time = (double)(end_t-start_t)/CLOCKS_PER_SEC;
        distance = time/2*34000;

        if(distance > 900)
            distance = 900;
       
        printf("time : %.4lf\n", time);
        printf("distance : %.2lfcm\n", distance);
   
        usleep(500000);
    }
}

void *led_thd() {
    
    int target_bright = 0;
    int prev_bright = 0;
    int repeat = 10;
    
    PWMExport(PWM);
    PWMWritePeriod(PWM, 20000000);
    PWMWriteDutyCycle(PWM, 0);
    PWMEnable(PWM);
    
    setsid();
    umask(0);
    
    while(1)
    {
        //printf("LED");
        if(signal == 0)
        {
            if(distance > 50)
            {
                GPIOWrite(POUT2, 0);
                usleep(1000000);
            }
            else if(distance > 20)
            {
                if (-1 == GPIOWrite(POUT2, repeat % 2))
                    return(3);
                usleep(350000);
            }
            else
            {
                if (-1 == GPIOWrite(POUT2, repeat % 2))
                    return(3);
                usleep(100000);
            }
        }
        else if(signal == 1)
        {
            if (-1 == GPIOWrite(POUT2, 0))
                    return(3);
        }
            
        repeat--;
    }
    exit(0);
}

void *data_thd() {
    while(1)
    {
        str_len = read(sock, msg, sizeof(msg));
        if(str_len == -1)
            error_handling("read() error");
        signal = atoi(msg);
        printf("Receive message from Server : %s\n",msg);
    }
    exit(1);
    
}

int main(int argc, char *argv[])
{
    pthread_t p_thread[3];
    int thr_id;
    int status;
    char p1[] = "thread_1";
    char p2[] = "thread_2";
    char p3[] = "thread_3";
    
    if(argc!=3){
      printf("Usage : %s <IP> <port>\n",argv[0]);
      exit(1);
   }
    //ultrawave_thd();
    //led_thd();

    if (-1 == GPIOUnexport(POUT) || -1 == GPIOUnexport(PIN) || -1 == GPIOUnexport(POUT2))
        return(-1);
        //Disable PWM

        //Enable GPIO pins
    if (-1 == GPIOExport(POUT) || -1 == GPIOExport(PIN) || -1 == GPIOExport(POUT2))
    {
        printf("gpio export err\n");
        exit(0);
    }

    //wait for writing to export file
    usleep(100000);

    //Set GPIO direction
    if (-1 == GPIODirection(POUT, OUT) || -1 == GPIODirection(PIN, IN) || -1 == GPIODirection(POUT2, OUT))
    {
        printf("gpio direction err\n");
        exit(0);
    }
    /*******socket*********/
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if(sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1)
        error_handling("connect() error");
    /**************************/

    
    thr_id = pthread_create(&p_thread[0], NULL, ultrawave_thd, (void *)p1);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[1], NULL, led_thd, (void *)p2);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[2], NULL, data_thd, (void *)p3);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    
    pthread_join(p_thread[0], (void **)&status);
    pthread_join(p_thread[1], (void **)&status);
    pthread_join(p_thread[2], (void **)&status);
    
    PWMUnexport(PWM);
    //close(sock);

    return 0;
}

