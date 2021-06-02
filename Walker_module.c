#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <pthread.h>
#include <time.h>

#define IN 0
#define OUT 1
#define PWM 2

#define LOW 0
#define HIGH 1
#define PIN 20
#define POUT 21
#define VALUE_MAX 256
#define ARRAY_SIZE(array) sizeof(array) / sizeof(array[0])

static const char *DEVICE = "/dev/spidev0.0";
static uint8_t MODE=SPI_MODE_0;
static uint8_t BITS=8;
static uint32_t CLOCK=1000000;
static uint16_t DELAY=5;

double distance = 0;

static int PWMExport(int pwmnum) {
#define BUFFER_MAX 3
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

static int PWMEnable(int pwmnum)
{
    static const char s_unenable_str[] = "0";
    static const char s_enable_str[] = "1";
    
#define DIRECTION_MAX 45
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

static int prepare(int fd) {
    if(ioctl(fd, SPI_IOC_WR_MODE, &MODE) == -1){
   perror("Can't set MODE");
   return -1;
    }
    
    if(ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &BITS) == -1){
   perror("Can't set number of BITS");
   return -1;
    }
    
    if(ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &CLOCK) == -1){
   perror("Can't set write CLOCK");
   return -1;
    }
    
    if(ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &CLOCK) == -1){
   perror("Can't set read CLOCK");
   return -1;
    }
}

uint8_t control_bits_differential(uint8_t channel)
{
    return (channel & 7) << 4;
}

uint8_t control_bits(uint8_t channel)
{
    return 0x8 | control_bits_differential(channel);
}

int readadc(int fd, uint8_t channel)
{
    uint8_t tx[] = {1, control_bits(channel), 0};
    uint8_t rx[3];

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = ARRAY_SIZE(tx),
        .delay_usecs = DELAY,
        .speed_hz = CLOCK,
        .bits_per_word = BITS,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) == 1)
    {
        perror("IO Error");
        abort();
    }

    return ((rx[1] << 8) & 0x300) | (rx[2] & 0xFF);
}

void *ultrawave_thd(){
    clock_t start_t, end_t;
    double time;
    
    //Enable GPIO pins
    if (-1 == GPIOExport(POUT) || -1 == GPIOExport(PIN))
    {
        printf("gpio export err\n");
        exit(0);
    }

    //wait for writing to export file
    usleep(100000);

    //Set GPIO direction
    if (-1 == GPIODirection(POUT, OUT) || -1 == GPIODirection(PIN, IN))
    {
        printf("gpio direction err\n");
        exit(0);
    }

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

void *pressure_thd() {
    int press=0;

    PWMExport(0);
    PWMWritePeriod(0, 100000);
    PWMWriteDutyCycle(0, 100000);

    usleep(100000);
    PWMEnable(0);

    int fd=open(DEVICE, O_RDWR);
    if(fd<=0){
        printf("Device %s not found\n", DEVICE);
        return -1;
    }

    if(prepare(fd)==-1){
        return -1;
    }
    press = readadc(fd, 0);
    
    while(1){
        press = readadc(fd, 0);
        printf("%d\n", press);
        if(20 < readadc(fd, 0))
        {
            PWMWriteDutyCycle(0, press*100);
            printf("if문 들어오는지 확인\n");
            usleep(100000);
        }
        else
        {
            PWMWriteDutyCycle(0, 100000);
            usleep(100000);
        }
    }
    
    PWMWriteDutyCycle(0, 100000);
    close(fd);

    return 0;
}

void *buzer_thd() {
    PWMExport(0);
    PWMWritePeriod(0, 100000);
    PWMWriteDutyCycle(0, 100000);
    usleep(100000);
    PWMEnable(0);
    
    setsid();
    umask(0);
    
    while(1)
    {
        if(distance > 50)
        {
            PWMWriteDutyCycle(0, press*100);
            usleep(10000);
        }
        else
        {
            PWMWriteDutyCycle(0, 100000);
            usleep(100000);
        }
        
    }
    PWMWriteDutyCycle(0, 100000);
    exit(0);
}

int main(int argc, char **argv)
{
    pthread_t p_thread[3];
    int thr_id;
    int status;
    char p1[] = "thread_1";
    char p2[] = "thread_2";
    char p3[] = "thread_3";


    if (-1 == GPIOUnexport(POUT) || -1 == GPIOUnexport(PIN))
        return(-1);
        //Disable PWM
    
    thr_id = pthread_create(&p_thread[0], NULL, pressure_thd, (void *)p1);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[1], NULL, ultrawave_thd, (void *)p2);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[1], NULL, buzer_thd, (void *)p3);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    
    pthread_join(p_thread[0], (void **)&status);
    pthread_join(p_thread[1], (void **)&status);
    pthread_join(p_thread[2], (void **)&status);

    PWMUnexport(0);

    return 0;
}
