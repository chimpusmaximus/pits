/*------------------------------------------------------------------\
|                                                                   |
|                PI IN THE SKY TRACKER PROGRAM                      |
|                                                                   |
| This program is written for the Pi Telemetry Board                |
| produced by HAB Supplies Ltd.  No support is provided             |
| for use on other hardware. It does the following:                 |
|                                                                   |
| 1 - Sets up the hardware including putting the GPS in flight mode |
| 2 - Reads the current temperature                                 |
| 3 - Reads the current battery voltage                             |
| 4 - Reads the current GPS position                                |
| 5 - Builds a telemetry sentence to transmit                       |
| 6 - Sends it to the MTX2/NTX2B radio transmitter                  |
| 7 - repeats steps 2-6                                             |
|                                                                   ||                                                                   |
\------------------------------------------------------------------*/

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdio.h>   	// Standard input/output definitions
#include <string.h>  	// String function definitions
#include <unistd.h>  	// UNIX standard function definitions
#include <fcntl.h>   	// File control definitions
#include <errno.h>   	// Error number definitions
#include <termios.h> 	// POSIX terminal control definitions
#include <stdint.h>
#include <stdlib.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <wiringPi.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <sys/statvfs.h>
#include "gps.h"
#include "DS18B20.h"
#include "adc.h"
#include "adc_i2c.h"
#include "misc.h"
#include "snapper.h"
#include "led.h"
#include "bmp085.h"
#include "aprs.h"

struct TConfig Config;

// Pin allocations.  Do not change unless you're using your own hardware
#define NTX2B_ENABLE	0
#define UBLOX_ENABLE	2

FILE *ImageFP;
int Records, FileNumber;
struct termios options;
char *SSDVFolder="/home/pi/pits/tracker/download";
 
void BuildSentence(char *TxLine, int SentenceCounter, struct TGPS *GPS)
{
    int Count, i, j;
    unsigned char c;
    unsigned int CRC, xPolynomial;
	char TimeBuffer1[12], TimeBuffer2[10], ExtraFields1[20], ExtraFields2[20], ExtraFields3[20];
	
	// sprintf(TimeBuffer1, "%06.0f", GPS->Time);
	sprintf(TimeBuffer1, "%06ld", GPS->Time);
	TimeBuffer2[0] = TimeBuffer1[0];
	TimeBuffer2[1] = TimeBuffer1[1];
	TimeBuffer2[2] = ':';
	TimeBuffer2[3] = TimeBuffer1[2];
	TimeBuffer2[4] = TimeBuffer1[3];
	TimeBuffer2[5] = ':';
	TimeBuffer2[6] = TimeBuffer1[4];
	TimeBuffer2[7] = TimeBuffer1[5];
	TimeBuffer2[8] = '\0';
	
	ExtraFields1[0] = '\0';
	ExtraFields2[0] = '\0';
	ExtraFields3[0] = '\0';
	
	if (NewBoard())
	{
		sprintf(ExtraFields1, ",%.0f", GPS->BoardCurrent * 1000);
	}
	
	if (Config.EnableBMP085)
	{
		sprintf(ExtraFields2, ",%.1f,%.0f", GPS->BMP180Temperature, GPS->Pressure);
	}
	
	if (GPS->DS18B20Count > 1)
	{
		sprintf(ExtraFields3, ",%3.1f", GPS->DS18B20Temperature[Config.ExternalDS18B20]);
	}
	
    sprintf(TxLine, "$$%s,%d,%s,%7.5lf,%7.5lf,%05.5u,%d,%d,%d,%3.1f,%3.1f%s%s%s",
            Config.PayloadID,
            SentenceCounter,
			TimeBuffer2,
            GPS->Latitude,
            GPS->Longitude,
            GPS->Altitude,
			(GPS->Speed * 13) / 7,
			GPS->Direction,
			GPS->Satellites,            
            GPS->DS18B20Temperature[1-Config.ExternalDS18B20],
            GPS->BatteryVoltage,
			ExtraFields1,
			ExtraFields2,
			ExtraFields3);

    Count = strlen(TxLine);

    CRC = 0xffff;           // Seed
    xPolynomial = 0x1021;
   
     for (i = 2; i < Count; i++)
     {   // For speed, repeat calculation instead of looping for each bit
        CRC ^= (((unsigned int)TxLine[i]) << 8);
        for (j=0; j<8; j++)
        {
            if (CRC & 0x8000)
                CRC = (CRC << 1) ^ 0x1021;
            else
                CRC <<= 1;
        }
     }

    TxLine[Count++] = '*';
    TxLine[Count++] = Hex((CRC >> 12) & 15);
    TxLine[Count++] = Hex((CRC >> 8) & 15);
    TxLine[Count++] = Hex((CRC >> 4) & 15);
    TxLine[Count++] = Hex(CRC & 15);
	TxLine[Count++] = '\n';  
	TxLine[Count++] = '\0';  

    printf("%s", TxLine);
}


void ReadString(FILE *fp, char *keyword, char *Result, int Length, int NeedValue)
{
	char line[100], *token, *value;
 
	fseek(fp, 0, SEEK_SET);
	*Result = '\0';

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		line[strcspn(line, "\r")] = '\0';			// Ignore any CR (in case someone has edited the file from Windows with notepad)
		
		token = strtok(line, "=");
		if (strcmp(keyword, token) == 0)
		{
			value = strtok(NULL, "\n");
			strcpy(Result, value);
			return;
		}
	}

	if (NeedValue)
	{
		printf("Missing value for '%s' in configuration file\n", keyword);
		exit(1);
	}
}

int ReadInteger(FILE *fp, char *keyword, int NeedValue, int Default)
{
	char Temp[32];

	ReadString(fp, keyword, Temp, sizeof(Temp), NeedValue);

	if (*Temp)
	{
		return atoi(Temp);
	}
	
	return Default;
}

int ReadBoolean(FILE *fp, char *keyword, int NeedValue)
{
	char Temp[32];

	ReadString(fp, keyword, Temp, sizeof(Temp), NeedValue);

	return (*Temp == '1') || (*Temp == 'Y') || (*Temp == 'y');
}

int ReadBooleanFromString(FILE *fp, char *keyword, char *searchword)
{
	char Temp[100];

	ReadString(fp, keyword, Temp, sizeof(Temp), 0);

	if (strcasestr(Temp, searchword)) return 1; else return 0;
}

speed_t BaudToSpeed(int baud)
{
	switch (baud)
	{
		case 50: return B50;
		case 75: return B75;
		case 150: return B150;
		case 200: return B200;
		case 300: return B300;
		case 600: return B600;
		case 1200: return B1200;
	}

	return 0;
}

void LoadConfigFile(struct TConfig *Config)
{
	FILE *fp;
	int BaudRate;
	char *filename = "/boot/pisky.txt";

	if ((fp = fopen(filename, "r")) == NULL)
	{
		printf("\nFailed to open config file %s (error %d - %s).\nPlease check that it exists and has read permission.\n", filename, errno, strerror(errno));
		exit(1);
	}

	ReadString(fp, "payload", Config->PayloadID, sizeof(Config->PayloadID), 1);
	printf ("Payload ID = '%s'\n", Config->PayloadID);

	Config->Frequency[0] = '\0';
	ReadString(fp, "frequency", Config->Frequency, sizeof(Config->Frequency), 0);

	Config->DisableMonitor = ReadBoolean(fp, "disable_monitor", 0);
	if (Config->DisableMonitor)
	{
		printf("HDMI/Composite outputs will be disabled\n");
	}
	
	Config->EnableGPSLogging = ReadBooleanFromString(fp, "logging", "GPS");
	if (Config->EnableGPSLogging) printf("GPS Logging enabled\n");

	Config->EnableTelemetryLogging = ReadBooleanFromString(fp, "logging", "Telemetry");
	if (Config->EnableTelemetryLogging) printf("Telemetry Logging enabled\n");
	
	Config->EnableBMP085 = ReadBoolean(fp, "enable_bmp085", 0);
	if (Config->EnableBMP085)
	{
		printf("BMP085 Enabled\n");
	}
	
	Config->ExternalDS18B20 = ReadInteger(fp, "external_temperature", 0, 1);

	BaudRate = ReadInteger(fp, "baud", 1, 300);
	Config->TxSpeed = BaudToSpeed(BaudRate);
	if (Config->TxSpeed == B0)
	{
		printf ("Unknown baud rate %d\nPlease edit in configuration file\n", BaudRate);
		exit(1);
	}
	printf ("Radio baud rate = %d\n", BaudRate);

	Config->Camera = ReadBoolean(fp, "camera", 0);
	printf ("Camera %s\n", Config->Camera ? "Enabled" : "Disabled");
	if (Config->Camera)
	{
		Config->high = ReadInteger(fp, "high", 0, 2000);
		printf ("Image size changes at %dm\n", Config->high);
	
		Config->low_width = ReadInteger(fp, "low_width", 0, 320);
		Config->low_height = ReadInteger(fp, "low_height", 0, 240);
		printf ("Low image size %d x %d pixels\n", Config->low_width, Config->low_height);
	
		Config->high_width = ReadInteger(fp, "high_width", 0, 640);
		Config->high_height = ReadInteger(fp, "high_height", 0, 480);
		printf ("High image size %d x %d pixels\n", Config->high_width, Config->high_height);

		Config->image_packets = ReadInteger(fp, "image_packets", 0, 4);
		printf ("1 Telemetry packet every %d image packets\n", Config->image_packets);
	}
	
	if (ReadInteger(fp, "SDA", 0, 0))
	{
		Config->SDA = ReadInteger(fp, "SDA", 0, 0);
		printf ("I2C SDA overridden to %d\n", Config->SDA);
	}

	if (ReadInteger(fp, "SCL", 0, 0))
	{
		Config->SCL = ReadInteger(fp, "SCL", 0, 0);
		printf ("I2C SCL overridden to %d\n", Config->SCL);
	}
	
	
	Config->InfoMessageCount = ReadInteger(fp, "info_messages", 0, -1);

	// APRS settings
	ReadString(fp, "APRS_Callsign", Config->APRS_Callsign, sizeof(Config->APRS_Callsign), 0);
	Config->APRS_ID = ReadInteger(fp, "APRS_ID", 0, 11);
	Config->APRS_Period = ReadInteger(fp, "APRS_Period", 0, 1);
	Config->APRS_Offset = ReadInteger(fp, "APRS_Offset", 0, 0);
	Config->APRS_Random = ReadInteger(fp, "APRS_Random", 0, 0);
	if (*(Config->APRS_Callsign) && Config->APRS_ID && Config->APRS_Period)
	{
		printf("APRS enabled for callsign %s:%d every %d minute%s with offset %ds\n", Config->APRS_Callsign, Config->APRS_ID, Config->APRS_Period, Config->APRS_Period > 1 ? "s" : "", Config->APRS_Offset);
	}
	
	fclose(fp);
}

void SetMTX2Frequency(char *FrequencyString)
{
	float _mtx2comp;
	int _mtx2int;
	long _mtx2fractional;
	char _mtx2command[17];
	int fd;
	double Frequency;
	struct termios options;
	uint8_t setNMEAoff[] = {0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00, 0x80, 0x25, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0xA9           };

	// First disable transmitter
	digitalWrite (NTX2B_ENABLE, 0);
	pinMode (NTX2B_ENABLE, OUTPUT);
	delayMilliseconds (200);
	
	fd = open("/dev/ttyAMA0", O_WRONLY | O_NOCTTY);
	if (fd >= 0)
	{
		tcgetattr(fd, &options);

		options.c_cflag &= ~CSTOPB;
		cfsetispeed(&options, B9600);
		cfsetospeed(&options, B9600);
		options.c_cflag |= CS8;
		options.c_oflag &= ~ONLCR;
		options.c_oflag &= ~OPOST;
		options.c_iflag &= ~IXON;
		options.c_iflag &= ~IXOFF;
		options.c_lflag &= ~ECHO;
		options.c_cc[VMIN]  = 0;
		options.c_cc[VTIME] = 10;
		
		tcsetattr(fd, TCSANOW, &options);

		// Tel UBlox to shut up
		write(fd, setNMEAoff, sizeof(setNMEAoff));
		tcsetattr(fd, TCSAFLUSH, &options);
		close(fd);
		delayMilliseconds (1000);
		
		fd = open("/dev/ttyAMA0", O_WRONLY | O_NOCTTY);
		
		if (strlen(FrequencyString) < 3)
		{
			// Convert channel number to frequency
			Frequency = strtol(FrequencyString, NULL, 16) * 0.003125 + 434.05;
		}
		else
		{
			Frequency = atof(FrequencyString);
		}
		
		printf("Frequency set to %8.4fMHz\n", Frequency);
		
		_mtx2comp=(Frequency+0.0015)/6.5;
		_mtx2int=_mtx2comp;
		_mtx2fractional = ((_mtx2comp-_mtx2int)+1) * 524288;
		snprintf(_mtx2command,17,"@PRG_%02X%06lX\r",_mtx2int-1, _mtx2fractional);
		printf("MTX2 command  is %s\n", _mtx2command);

		// Let enable line float (but Tx will pull it up anyway)
		delayMilliseconds (200);
		pinMode (NTX2B_ENABLE, INPUT);
		pullUpDnControl(NTX2B_ENABLE, PUD_OFF);
		delayMilliseconds (20);

		write(fd, _mtx2command, strlen(_mtx2command)); 
		tcsetattr(fd, TCSAFLUSH, &options);
		delayMilliseconds (50);

		close(fd);

		// Switch on the radio
		delayMilliseconds (100);
		digitalWrite (NTX2B_ENABLE, 1);
		pinMode (NTX2B_ENABLE, OUTPUT);
	}
}

void SetNTX2BFrequency(char *FrequencyString)
{
	int fd, Frequency;
	char Command[16];
	struct termios options;
	uint8_t setNMEAoff[] = {0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00, 0x80, 0x25, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0xA9           };

	// First disable transmitter
	digitalWrite (NTX2B_ENABLE, 0);
	pinMode (NTX2B_ENABLE, OUTPUT);
	delayMilliseconds (200);
	
	fd = open("/dev/ttyAMA0", O_WRONLY | O_NOCTTY);
	if (fd >= 0)
	{
		tcgetattr(fd, &options);

		options.c_cflag &= ~CSTOPB;
		cfsetispeed(&options, B4800);
		cfsetospeed(&options, B4800);
		options.c_cflag |= CS8;
		options.c_oflag &= ~ONLCR;
		options.c_oflag &= ~OPOST;
		options.c_iflag &= ~IXON;
		options.c_iflag &= ~IXOFF;
		options.c_lflag &= ~ECHO;
		options.c_cc[VMIN]  = 0;
		options.c_cc[VTIME] = 10;
		
		tcsetattr(fd, TCSANOW, &options);

		// Tel UBlox to shut up
		write(fd, setNMEAoff, sizeof(setNMEAoff));
		tcsetattr(fd, TCSAFLUSH, &options);
		close(fd);
		delayMilliseconds (1000);
		
		fd = open("/dev/ttyAMA0", O_WRONLY | O_NOCTTY);
		
		if (strlen(FrequencyString) < 3)
		{
			// Already a channel number
			Frequency = strtol(FrequencyString, NULL, 16);
		}
		else
		{
			// Convert from MHz to channel number
			Frequency = (int)((atof(FrequencyString) - 434.05) / 0.003124);
		}
		
		sprintf(Command, "%cch%02X\r", 0x80, Frequency);

		printf("NTX2B-FA transmitter now set to channel %02Xh which is %8.4lfMHz\n", Frequency, (double)(Frequency) * 0.003125 + 434.05);

		// Let enable line float (but Tx will pull it up anyway)
		delayMilliseconds (200);
		pinMode (NTX2B_ENABLE, INPUT);
		pullUpDnControl(NTX2B_ENABLE, PUD_OFF);
		delayMilliseconds (20);

		write(fd, Command, strlen(Command)); 
		tcsetattr(fd, TCSAFLUSH, &options);
		delayMilliseconds (50);

		close(fd);

		// Switch on the radio
		delayMilliseconds (100);
		digitalWrite (NTX2B_ENABLE, 1);
		pinMode (NTX2B_ENABLE, OUTPUT);
	}
}


void SetFrequency(char *Frequency)
{
	if (NewBoard())
	{
		SetMTX2Frequency(Frequency);
		SetMTX2Frequency(Frequency);
	}
	else
	{
		SetNTX2BFrequency(Frequency);
	}
}

int OpenSerialPort(void)
{
	int fd;

	fd = open("/dev/ttyAMA0", O_WRONLY | O_NOCTTY);	// O_NDELAY);
	if (fd >= 0)
	{
		/* get the current options */
		tcgetattr(fd, &options);

		/* set raw input */
		options.c_lflag &= ~ECHO;
		options.c_cc[VMIN]  = 0;
		options.c_cc[VTIME] = 10;

		options.c_cflag |= CSTOPB;
		cfsetispeed(&options, Config.TxSpeed);
		cfsetospeed(&options, Config.TxSpeed);
		options.c_cflag |= CS8;
		options.c_oflag &= ~ONLCR;
		options.c_oflag &= ~OPOST;
		options.c_iflag &= ~IXON;
		options.c_iflag &= ~IXOFF;
	
		tcsetattr(fd, TCSANOW, &options);
	}

	return fd;
}

void SendSentence(int fd, char *TxLine)
{
	// int fd;

	
	// if ((fd = OpenSerialPort()) >= 0)
	{
		// printf("Sending sentence ...\n");
		write(fd, TxLine, strlen(TxLine));

		// Log now while we're waiting for the serial port, to eliminate or at least reduce downtime whilst logging
		if (Config.EnableTelemetryLogging)
		{
			WriteLog("telemetry.txt", TxLine);
		}
		
		// Wait till those characters get sent
		tcsetattr(fd, TCSAFLUSH, &options);
		// printf("Sent\n");
		
		// if (close(fd) < 0)
		// {
			// printf("NOT Sent - error %d\n", errno);
		// }
	}
	/*
	else
	{
		printf("Failed to open serial port\n");
	}
	*/
	
}

int FindAndConvertImage(void)
{
	size_t LargestFileSize;
	char LargestFileName[100], FileName[100], CommandLine[2000];
	DIR *dp;
	struct dirent *ep;
	struct stat st;
	int Done;
	
	LargestFileSize = 0;
	Done = 0;
	
	dp = opendir(SSDVFolder);
	if (dp != NULL)
	{
		while (ep = readdir (dp))
		{
			if (strstr(ep->d_name, ".jpg") != NULL)
			{
				sprintf(FileName, "%s/%s", SSDVFolder, ep->d_name);
				stat(FileName, &st);
				if (st.st_size > LargestFileSize)
				{
					LargestFileSize = st.st_size;
					strcpy(LargestFileName, FileName);
				}
			}
		}
		(void) closedir (dp);
	}

	if (LargestFileSize > 0)
	{
		char Date[20], SavedImageFolder[100];
		time_t now;
		struct tm *t;
	
		printf("Found file %s to convert\n", LargestFileName);
		
		// Now convert the file
		FileNumber++;
		FileNumber = FileNumber & 255;
		sprintf(CommandLine, "ssdv -e -c %.6s -i %d %6s /home/pi/pits/tracker/snap.bin", Config.PayloadID, FileNumber, LargestFileName);
		system(CommandLine);
		
		// And move those pesky image files
		now = time(NULL);
		t = localtime(&now);
		strftime(Date, sizeof(Date)-1, "%d_%m_%Y", t);		

		sprintf(SavedImageFolder, "%s/%s", SSDVFolder, Date);
		if (stat(SavedImageFolder, &st) == -1)
		{
			mkdir(SavedImageFolder, 0777);
		}
		system(CommandLine);
		sprintf(CommandLine, "mv %s/*.jpg %s", SSDVFolder, SavedImageFolder);
		system(CommandLine);

		Done = 1;
	}
	
	return (LargestFileSize > 0);
}

int SendImage(int fd)
{
    unsigned char Buffer[256];
    size_t Count;
    int SentSomething = 0;
	// int fd;

    if (ImageFP == NULL)
    {
		if (FindAndConvertImage())
		{
			ImageFP = fopen("/home/pi/pits/tracker/snap.bin", "r");
		}
        Records = 0;
    }

    if (ImageFP != NULL)
    {
        Count = fread(Buffer, 1, 256, ImageFP);
        if (Count > 0)
        {
            printf("Record %d, %d bytes\r\n", ++Records, Count);

			// if ((fd = OpenSerialPort()) >= 0)
			{
				printf("Sending image packet ...\n");
				write(fd, Buffer, Count);
				tcsetattr(fd, TCSAFLUSH, &options);
				printf("Sent\n");
				// close(fd);
			}

            SentSomething = 1;
        }
        else
        {
            fclose(ImageFP);
            ImageFP = NULL;
        }
    }

    return SentSomething;
}

void SendIPAddress(int fd)
{
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char *addr;

    getifaddrs (&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family==AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);
			if (strcmp(addr, "127.0.0.1") != 0)
			{
				char Sentence[200];
				
				sprintf(Sentence, "Interface %s has IP Address: %s\n", ifa->ifa_name, addr);
				printf(Sentence);
				SendSentence(fd, Sentence);
			}
        }
    }

    freeifaddrs(ifap);
}

void SendFreeSpace(int fd)
{
	struct statvfs vfs;

	if (statvfs("/home", &vfs) == 0)
	{
		char Sentence[200];
		
		sprintf(Sentence, "Free SD space = %.1fMB\n", (float)vfs.f_bsize * (float)vfs.f_bfree / (1024 * 1024));
		printf(Sentence);
		SendSentence(fd, Sentence);
	}
}

int main(void)
{
	int fd, ReturnCode, i;
	unsigned long Sentence_Counter = 0;
	char Sentence[100], Command[100];
	struct stat st = {0};
	struct TGPS GPS;
	pthread_t APRSThread, GPSThread, DS18B20Thread, ADCThread, CameraThread, BMP085Thread, LEDThread;
	
	printf("\n\nRASPBERRY PI-IN-THE-SKY FLIGHT COMPUTER\n");
	printf(    "=======================================\n\n");

	if (NewBoard())
	{
		if (NewBoard() == 2)
		{
			printf("RPi 2 B\n");
		}
		else
		{
			printf("RPi Model A+ or B+\n");
		}
		
		printf("PITS+ Board\n\n");
		
		Config.LED_OK = 25;
		Config.LED_Warn = 24;
		
		Config.SDA = 2;
		Config.SCL = 3;
	}
	else
	{
		printf("RPi Model A or B\n");
		printf("PITS Board\n\n");

		Config.LED_OK = 4;
		Config.LED_Warn = 11;
		
		Config.SDA = 5;
		Config.SCL = 6;
	}
	
	LoadConfigFile(&Config);

	if (Config.DisableMonitor)
	{
		system("/opt/vc/bin/tvservice -off");
	}
	
	if (FileExists("/boot/clear.txt"))
	{
		// remove SSDV and other camera images, plus log files

		printf("Removing existing photo files\n");
		remove("gps.txt");
		remove("telemetry.txt");
		remove("/boot/clear.txt");
		system("rm -rf /home/pi/pits/tracker/download/*.jpg");
		system("rm -rf /home/pi/pits/tracker/keep/*.jpg");
	}

	GPS.Time = 0.0;
	GPS.Longitude = 0.0;
	GPS.Latitude = 0.0;
	GPS.Altitude = 0;
	GPS.Satellites = 0;
	GPS.Speed = 0.0;
	GPS.Direction = 0.0;
	GPS.DS18B20Temperature[0] = 0.0;
	GPS.DS18B20Temperature[1] = 0.0;
	GPS.BatteryVoltage = 0.0;
	GPS.BMP180Temperature = 0.0;
	GPS.Pressure = 0.0;
	
	// Set up I/O
	if (wiringPiSetup() == -1)
	{
		exit (1);
	}

	// Switch off the radio till it's configured
	pinMode (NTX2B_ENABLE, OUTPUT);
	digitalWrite (NTX2B_ENABLE, 0);
		
	// Switch on the GPS
	if (!NewBoard())
	{
		pinMode (UBLOX_ENABLE, OUTPUT);
		digitalWrite (UBLOX_ENABLE, 0);
	}

	if ((fd = OpenSerialPort()) < 0)
	{
		printf("Cannot open serial port - check documentation!\n");
		exit(1);
	}
	// close(fd);

	if (*Config.Frequency)
	{
		SetFrequency(Config.Frequency);
	}

	// Switch off the radio till it's configured
	digitalWrite (NTX2B_ENABLE, 1);
		
	// Set up DS18B20
	system("sudo modprobe w1-gpio");
	system("sudo modprobe w1-therm");
	
	// SPI for ADC
	system("gpio load spi");

	if (stat(SSDVFolder, &st) == -1)
	{
		mkdir(SSDVFolder, 0777);
	}	

	if (pthread_create(&GPSThread, NULL, GPSLoop, &GPS))
	{
		fprintf(stderr, "Error creating GPS thread\n");
		return 1;
	}

	if (*(Config.APRS_Callsign) && Config.APRS_ID && Config.APRS_Period)
	{
		if (pthread_create(&APRSThread, NULL, APRSLoop, &GPS))
		{
			fprintf(stderr, "Error creating APRS thread\n");
			return 1;
		}
	}
	
	if (pthread_create(&DS18B20Thread, NULL, DS18B20Loop, &GPS))
	{
		fprintf(stderr, "Error creating DS18B20s thread\n");
		return 1;
	}

	if (I2CADCExists())
	{
		printf ("V2.4 or later board with I2C ADC\n");
		
		if (pthread_create(&ADCThread, NULL, I2CADCLoop, &GPS))
		{
			fprintf(stderr, "Error creating ADC thread\n");
			return 1;
		}
	}
	else
	{
		printf ("Older board with SPI ADC\n");
		
		if (pthread_create(&ADCThread, NULL, ADCLoop, &GPS))
		{
			fprintf(stderr, "Error creating ADC thread\n");
			return 1;
		}
	}

	if (Config.Camera)
	{
		if (pthread_create(&CameraThread, NULL, CameraLoop, &GPS))
		{
			fprintf(stderr, "Error creating camera thread\n");
			return 1;
		}
	}

	if (pthread_create(&LEDThread, NULL, LEDLoop, &GPS))
	{
		fprintf(stderr, "Error creating LED thread\n");
		return 1;
	}
	
	if (Config.EnableBMP085)
	{
		if (pthread_create(&BMP085Thread, NULL, BMP085Loop, &GPS))
		{
			fprintf(stderr, "Error creating BMP085 thread\n");
			return 1;
		}
	}

	if (Config.InfoMessageCount < 0)
	{
		// Default number depends on baud rate
		Config.InfoMessageCount = (Config.TxSpeed < B300) ? 2 : 4;
	}
	
	for (i=0; i<Config.InfoMessageCount; i++)
	{
		SendIPAddress(fd);
		SendFreeSpace(fd);
	}

	while (1)
	{	
		BuildSentence(Sentence, ++Sentence_Counter, &GPS);
		
		SendSentence(fd, Sentence);
		
		for (i=0; i< ((GPS.Altitude > Config.high) ? Config.image_packets : 1); i++)
		{
			SendImage(fd);
		}
	}
}
