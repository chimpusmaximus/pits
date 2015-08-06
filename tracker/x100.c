#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <wiringPi.h>

#include "gps.h"
#include "misc.h"

const int ShutPin = 28;
const int FocusPin = 29;

int *RX100Loop(void *some_void_ptr)
	{
   	struct TGPS *GPS;
	GPS = (struct TGPS *)some_void_ptr;
	pinMode(ShutPin, OUTPUT);
	pinMode(FocusPin, OUTPUT);

	int x = 0;
	while (1 ) {
			digitalWrite(FocusPin, HIGH);
			delay(1000);
			digitalWrite(ShutPin, HIGH);
			delay(500);
			digitalWrite(ShutPin, LOW);
			digitalWrite(FocusPin, LOW);
			sleep(10);
		}
		return 0;


	}
