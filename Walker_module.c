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
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/i2c-dev.h>

#define IN 0
#define OUT 1
#define PWM 0

#define LOW 0
#define HIGH 1
#define PIN 24
#define POUT 23
#define VALUE_MAX 256
#define ARRAY_SIZE(array) sizeof(array) / sizeof(array[0])


#define PULSE_PERIOD 500
#define CMD_PERIOD 0 //4100

#define BACKLIGHT 8
#define DATA 1

static int iBackLight = BACKLIGHT;
static int file_i2c = -1;

static const char *DEVICE = "/dev/spidev0.0";
static uint8_t MODE=SPI_MODE_0;
static uint8_t BITS=8;
static uint32_t CLOCK=1000000;
static uint16_t DELAY=5;

double distance = 100;
int press=0;
int sock;
struct sockaddr_in serv_addr;
char msg[2];
int str_len;
int signal = 0;

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

static int PWMUnexport(int pwmnum)
{
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

static int PWMUnable(int pwmnum)
{
        static const char s_unenable_str[] = "0";
    
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
		fprintf(stderr,"Failed to open gpdio for writing!\n");
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

    close(fd);
    return(0);
    }
}


static void WriteCommand(unsigned char ucCMD)
{
unsigned char uc;

	uc = (ucCMD & 0xf0) | iBackLight; // most significant nibble sent first
	write(file_i2c, &uc, 1);
	usleep(PULSE_PERIOD); // manually pulse the clock line
	uc |= 4; // enable pulse
	write(file_i2c, &uc, 1);
	usleep(PULSE_PERIOD);
	uc &= ~4; // toggle pulse
	write(file_i2c, &uc, 1);
	usleep(CMD_PERIOD);
	uc = iBackLight | (ucCMD << 4); // least significant nibble
	write(file_i2c, &uc, 1);
	usleep(PULSE_PERIOD);
        uc |= 4; // enable pulse(or)
        write(file_i2c, &uc, 1);
        usleep(PULSE_PERIOD);
        uc &= ~4; // toggle pulse(and)
        write(file_i2c, &uc, 1);
	usleep(CMD_PERIOD);

} /* WriteCommand() */

//
// Control the backlight, cursor, and blink
// The cursor is an underline and is separate and distinct
// from the blinking block option
//
int lcd1602Control(int bBacklight, int bCursor, int bBlink)
{
unsigned char ucCMD = 0xc; // display control

	if (file_i2c < 0)
		return 1;
	iBackLight = (bBacklight) ? BACKLIGHT : 0;
	if (bCursor)
		ucCMD |= 2;
	if (bBlink)
		ucCMD |= 1;
	WriteCommand(ucCMD);
 	
	return 0;
} /* lcd1602Control() */

//
// Write an ASCII string (up to 16 characters at a time)
// 
int lcd1602WriteString(char *text)
{
unsigned char ucTemp[2];
int i = 0;

	if (file_i2c < 0 || text == NULL)
		return 1;

	while (i<16 && *text)
	{
		ucTemp[0] = iBackLight | DATA | (*text & 0xf0);
		write(file_i2c, ucTemp, 1);
		usleep(PULSE_PERIOD);
		ucTemp[0] |= 4; // pulse E
		write(file_i2c, ucTemp, 1);
		usleep(PULSE_PERIOD);
		ucTemp[0] &= ~4;
		write(file_i2c, ucTemp, 1);
		usleep(PULSE_PERIOD);
		ucTemp[0] = iBackLight | DATA | (*text << 4);
		write(file_i2c, ucTemp, 1);
		ucTemp[0] |= 4; // pulse E
                write(file_i2c, ucTemp, 1);
                usleep(PULSE_PERIOD);
                ucTemp[0] &= ~4;
                write(file_i2c, ucTemp, 1);
                usleep(CMD_PERIOD);
		text++;
		i++;
	}
	return 0;
} /* WriteString() */

//
// Erase the display memory and reset the cursor to 0,0
//
int lcd1602Clear(void)
{
	if (file_i2c < 0)
		return 1;
	WriteCommand(0x0E); // clear the screen
	return 0;

} /* lcd1602Clear() */

//
// Open a file handle to the I2C device
// Set the controller into 4-bit mode and clear the display
// returns 0 for success, 1 for failure
//
int lcd1602Init(int iChannel, int iAddr)
{
char szFile[32];
int rc;

	sprintf(szFile, "/dev/i2c-%d", iChannel);
	file_i2c = open(szFile, O_RDWR);
	if (file_i2c < 0)
	{
		fprintf(stderr, "Error opening i2c device; not running as sudo?\n");
		return 1;
	}
	rc = ioctl(file_i2c, I2C_SLAVE, iAddr);
	if (rc < 0)
	{
		close(file_i2c);
		fprintf(stderr, "Error setting I2C device address\n");
		return 1;
	}
	iBackLight = BACKLIGHT; // turn on backlight
	WriteCommand(0x02); // Set 4-bit mode of the LCD controller
	WriteCommand(0x28); // 2 lines, 5x8 dot matrix
	WriteCommand(0x0c); // display on, cursor off
	WriteCommand(0x06); // inc cursor to right when writing and don't scroll
	WriteCommand(0x80); // set cursor to row 1, column 1
	lcd1602Clear();	    // clear the memory

	return 0;
} /* lcd1602Init() */

//
// Set the LCD cursor position
//
int lcd1602SetCursor(int x, int y)
{
unsigned char cCmd;

	if (file_i2c < 0 || x < 0 || x > 15 || y < 0 || y > 1)
		return 1;

	cCmd = (y==0) ? 0x80 : 0xc0;
	cCmd |= x;
	WriteCommand(cCmd);
	return 0;

} /* lcd1602SetCursor() */


void warnLCD()
{
    
	lcd1602SetCursor(0,0);
	lcd1602WriteString("                ");
	lcd1602SetCursor(0,1);
	lcd1602WriteString("                ");
	
	lcd1602SetCursor(0,0);
	lcd1602WriteString("Warning!");
	lcd1602SetCursor(0,1);
	lcd1602WriteString("Find trespassing");
	usleep(100000);
}

void usualLCD()
{
    lcd1602SetCursor(0,0);
	lcd1602WriteString("                ");
	lcd1602SetCursor(0,1);
	lcd1602WriteString("                ");
	
	lcd1602SetCursor(0,0);
	lcd1602WriteString("Accident-prone");
	lcd1602SetCursor(0,1);
	lcd1602WriteString("area");
}

void *buzer_thd() {
    setsid();
    umask(0);
    
    while(1)
    {
            if(distance < 50 && signal == 0)
            {
                PWMWriteDutyCycle(0, 10000);
                usleep(10000);
            }
            else
            {
                PWMWriteDutyCycle(0, 100000);
                usleep(100000);
            }
    }
    //PWMWriteDutyCycle(0, 100000);
    //exit(0);
}


void *pressure_thd() {
    
    int fd=open(DEVICE, O_RDWR);
    int prev_press = 0, one = 0;
    
    if(fd<=0){
        printf("Device %s not found\n", DEVICE);
        exit (1);
    }

    if(prepare(fd)==-1){
        exit (1);
    }
    press = readadc(fd, 0);
    
    while(1){

            
            press = readadc(fd, 0);
            //printf("%d\n", press);
            //printf("%d\n", press);
            if(20 < readadc(fd, 0) && signal == 0)
            {
                PWMWriteDutyCycle(0, 10000);
                printf("trespassing!\n");
                if(prev_press == 0)
                    warnLCD();
                one = 0;   
                usleep(100000);
            }
            else
            {
                PWMWriteDutyCycle(0, 100000);
                if (one == 0){
                    usualLCD();
                    one = 1;
                }
                usleep(100000);
            }

    }
    
    PWMWriteDutyCycle(0, 100000);
    close(fd);

    return 0;
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

    usleep(10000);    }


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

void error_handling(char *message){
   fputs(message, stderr);
   fputc('\n', stderr);
   exit(1);
}

void *communicate(){
    while(1){
        str_len = read(sock, msg, sizeof(msg));
        if(str_len == -1)
            error_handling("read() error");
    
        printf("Receive message from Server : %s\n",msg);
        signal = atoi(msg);
    }
}

void setupPWM()
{
    PWMExport(0);
    PWMUnable(0);
    PWMWritePeriod(0, 100000);
    PWMWriteDutyCycle(0, 100000);
    usleep(5);
    printf("start\n");
    PWMEnable(0);
}

int main(int argc, char **argv)
{
    pthread_t p_thread[4];
    int thr_id;
    int status;
    char p1[] = "thread_1";
    char p2[] = "thread_2";
    char p3[] = "thread_3";
    char p4[] = "thread_4";
    
    int rc;
	rc = lcd1602Init(1, 0x27);
	
	if (rc)
	{
		printf("Initialization failed; aborting...\n");
		return 0;
	}
    
    if(argc!=3){
      printf("Usage : %s <IP> <port>\n",argv[0]);
      exit(1);
    }
    setupPWM();
    if (-1 == GPIOUnexport(POUT) || -1 == GPIOUnexport(PIN))
        return(-1);
        //Disable PWM
        
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if(sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1)
        error_handling("connect() error");

    
    thr_id = pthread_create(&p_thread[0], NULL, communicate, (void *)p1);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[1], NULL, pressure_thd, (void *)p2);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[2], NULL, ultrawave_thd, (void *)p3);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[3], NULL, buzer_thd, (void *)p4);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    
    pthread_join(p_thread[0], (void **)&status);
    pthread_join(p_thread[1], (void **)&status);
    pthread_join(p_thread[2], (void **)&status);
    pthread_join(p_thread[3], (void **)&status);

    PWMUnexport(0);

    return 0;
}
