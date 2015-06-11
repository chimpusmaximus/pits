// Globals

#include <termios.h>

typedef enum {lmIdle, lmListening, lmSending} tLoRaMode;

struct TLoRaDevice
{
	int InUse;
	int DIO0;
	int DIO5;
	char Frequency[8];
	int SpeedMode;
	int Power;
	int PayloadLength;
	int ImplicitOrExplicit;
	int ErrorCoding;
	int Bandwidth;
	int SpreadingFactor;
	int LowDataRateOptimize;
	int CycleTime;
	int Slot;
	int RepeatSlot;
	int UplinkSlot;
	int Binary;
	int LastTxAt;
	int LastRxAt;
	int AirCount;
	int GroundCount;
	int BadCRCCount;
	char LastCommand[128];
	unsigned char PacketToRepeat[256];
	unsigned char UplinkPacket[256];
	int PacketRepeatLength;
	int UplinkRepeatLength;
	int SendRepeatedPacket;
	tLoRaMode LoRaMode;
	char CallingFrequency[8];
	int CallingCount;
	int PacketsSinceLastCall;
	int ReturnStateAfterCall;
};

// Structure for all possible radio devices
// 0 is RTTY
// 1 is APRS
// 2/3 are for LoRa
struct TChannel
{
	int Enabled;
	unsigned int SentenceCounter;
	char PayloadID[16];
	int SendTelemetry;						// TRUE to send telemetry on this channel
	char SSDVFolder[200];
	int ImagePackets;						// Image packets per telemetry packet
	int ImagePacketCount;					// Image packets since last telemetry packet
	int ImageWidthWhenLow;
	int ImageHeightWhenLow;
	int ImageWidthWhenHigh;
	int ImageHeightWhenHigh;
	int ImagePeriod;						// Time in seconds between photographs
	int	TimeSinceLastImage;
	unsigned int BaudRate;
	char SSDVFileName[200];
	FILE *ImageFP;
	int SSDVRecordNumber;
	int SSDVTotalRecords;
	int NextSSDVFileReady;
	int SSDVFileNumber;
	int ImagesRequested;
};

#define RTTY_CHANNEL 0
#define APRS_CHANNEL 1
#define LORA_CHANNEL 2


struct TConfig
{
	int DisableMonitor;
	int Camera;
	int SSDVHigh;
	int EnableBMP085;
	int ExternalDS18B20;
	
	// Logging
	int EnableGPSLogging;
	int EnableTelemetryLogging;
	
	// LEDs
	int LED_OK;
	int LED_Warn;

	// GPS Settings
	int SDA;
	int SCL;
	
	// RTTY Settings
	int DisableRTTY;
	char Frequency[8];
	speed_t TxSpeed;
	
	// APRS section
	char APRS_Callsign[8];
	int APRS_ID;
	int APRS_Period;	
		
	// LoRa section
	struct TLoRaDevice LoRaDevices[2];

	// Radio channels
	struct TChannel Channels[5];		// 0 is RTTY, 1 is APRS, 2/3 are LoRa, 4 is for full-size images
	
	// GPS faking
	char GPSSource[128];
	
	// Landing prediction
	int EnableLandingPrediction;
	float cd_area;
	float payload_weight;
	char PredictionID[16];
};

extern struct TConfig Config;

char Hex(char Character);
void WriteLog(char *FileName, char *Buffer);
short open_i2c(int address);
int ReadBooleanFromString(FILE *fp, char *keyword, char *searchword);
int ReadBoolean(FILE *fp, char *keyword, int Channel, int NeedValue, int *Result);
void ReadString(FILE *fp, char *keyword, int Channel, char *Result, int Length, int NeedValue);
int ReadInteger(FILE *fp, char *keyword, int Channel, int NeedValue, int DefaultValue);
double ReadFloat(FILE *fp, char *keyword, int Channel, int NeedValue, double DefaultValue);
void AppendCRC(char *Temp);
