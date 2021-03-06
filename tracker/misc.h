// Globals

#include <termios.h>

struct TConfig
{
	char PayloadID[16];
	char Frequency[8];
	int DisableMonitor;
	int InfoMessageCount;
	speed_t TxSpeed;
	int Camera;
	int low_width;
	int low_height;
	int high;
	int high_width;
	int high_height;
	int image_packets;
	int ExternalDS18B20;
	int EnableBMP085;
	int EnableGPSLogging;
	int EnableTelemetryLogging;
	int LED_OK;
	int LED_Warn;
	int SDA;
	int SCL;
	char APRS_Callsign[16];
	int APRS_ID;
	int APRS_Period;
	int APRS_Offset;
	int APRS_Random;
};

extern struct TConfig Config;

char Hex(char Character);
void WriteLog(char *FileName, char *Buffer);
short open_i2c(int address);
int FileExists(char *filename);
