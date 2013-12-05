#include "stdafx.h"
#include "ExtIODll.h"
#include "DialogClasses.h"
#include "ExtIODialog.h"

#include "libusb\lusb0_usb.h"
#include "ExtIOFunctions.h"

#include <conio.h>
#include <process.h>

void (* ExtIOCallback)(int, int, float, void *) = NULL;

// Task for receiving samples from radio and communicating back to HDSDR


// Our static data buffers. We need them all the time, so no need to allocate memory dynamically.

unsigned char tmpData[60*1024];			//Bulk RX buffer. Keep huge to allow USB receive loop to fetch whatever is there, 
										//so radio side of USB can transmit as fast as it could. This buffer will be copied off immediately 
										//after bulk receive function returns. 
unsigned char SDRData[SDRIQDATASIZE];	//This buffer is where we copy the tmpData to after each rx attempt. Once it has enough data to call ExtIOCallback, it will be
										//copied off and the write pointer reset.

unsigned char PANData[PANDATAMASK+1];	//If panadapter is active, ExtIODatatask will mux between panadapter and SDR data, so panadapter
										//dat ais put here. PANDATAMASK is used for ring buffer masking, so it has to be contignous 
										//bits, like 00111111b, 

//unsigned long iqdata_wptr;

extern usb_dev_handle *dev;

//extern CDataRateInfo *ptr_DataRateInfo;
extern CExtIODialog* m_pmodeless;
extern unsigned long lo_freq;

volatile bool callbacktask_running=false;
volatile bool do_callbacktask=true;
volatile bool fifo_loaded=false;

extern volatile bool do_callback105;			// indicate, if datatask should call callback 105
extern volatile bool do_callback101;
extern volatile bool channelmode_changed;		// indicate, that GUI has requested channel mode change
extern volatile bool update_lo;
extern volatile bool samplerate_changed;		// indicates, that someone somewhere has requested sample rate change
extern volatile bool IFgainA_changed;
extern volatile bool IFgainB_changed;

extern volatile bool do_pantableupdate;
extern volatile bool panadaptertask_exited, run_panadaptertask;
extern PANENTRY panentry;

bool pancaching=true;

volatile bool update_phaseA=false;
volatile bool update_IFgain=false;
volatile bool update_RFgain=false;

extern int ChannelMode;
extern int SyncTuning;
extern int SyncGain;

extern int IFGainA, IFGainB;
extern int RFGainA, RFGainB;
extern int PhaseCoarse, PhaseFine;
extern int AGC;

extern long IQSampleRate;

extern unsigned long phaseword;

double callbackinterval;										// note, that interval calculations are done with floating point, because callback interval is not integer ms

unsigned int pandata_wptr = 0;
//unsigned char* PANData=NULL;

extern HANDLE sleepevent;

/*
usb_dev_handle *open_dev(void)
{
struct usb_bus *bus;
struct usb_device *dev;

    for (bus = usb_get_busses(); bus; bus = bus->next)
    {
        for (dev = bus->devices; dev; dev = dev->next)
        {
            if (dev->descriptor.idVendor == SDR_VID
                    && dev->descriptor.idProduct == SDR_PID)
            {
                return usb_open(dev);
            }
        }
    }
    
	return NULL;
}
*/

UINT ExtIOCallbackTask(LPVOID dummy);		// see at the end of file 
unsigned long sdr_wptr, sdr_readptr;
bool buffer_filled;

extern char global_dataratestr[64];

UINT ExtIODataTask(LPVOID dummy)
{
int ret;
unsigned long i, j, k;
unsigned long starttime, millis, totalbytes, lasttotalbytes, buffered;
double mbits;
int last_synctuning;
//unsigned long phaseword;
unsigned int iterations=0;
unsigned long bytecounter=0;
unsigned long samplescached=0;
char tmp[128];

	EnterCriticalSection(&CriticalSection);
		threadcount++;
		datatask_done=false;		// show that we are now inside the thread
		datatask_running=true;
	LeaveCriticalSection(&CriticalSection);

	_cprintf("ExtIODataTask() thread started\n");

	last_synctuning=SyncTuning;

	totalbytes=0;
	lasttotalbytes=0;
	buffered=0;

	sdr_wptr=0;
	bytecounter=0;
	//iqdata_wptr=0;
	buffer_filled=false;

	// reset whatever is going on at the endpoint
	usb_clear_halt(dev, SDREP_IN);
	
	// disable bulk interface
	usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
							LIBUSB_MODE, 
							LIBMODE_OFF,  
							SDR_BULKIF,  
							tmp, 1, 1000);

	switch(ChannelMode)
	{
	case CHMODE_A:
	case CHMODE_B:
		*(unsigned long*)&tmp[0]=IQSAMPLERATE_FULL;
		break;

	case CHMODE_APB:
	case CHMODE_AMB:
	case CHMODE_BMA:
		*(unsigned long*)&tmp[0]=IQSAMPLERATE_DIVERSITY;
		break;

	case CHMODE_ABPAN:
	case CHMODE_BAPAN:
		*(unsigned long*)&tmp[0]=IQSAMPLERATE_PANADAPTER;
		break;

	case CHMODE_IABQ:
	default:
		*(unsigned long*)&tmp[0]=IQSAMPLERATE_FULL;
		break;
	}

	tmp[4]=16;
	usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
							LIBUSB_SAMPLEMODE, 
							0,  
							SDR_BULKIF,  
							tmp, 5, 1000);

	_cprintf("purge ..\n");
	// fetch all data in buffer to establish I/Q sync
	while ((do_datatask)&&(!globalshutdown))
	{
		if ((ret=usb_bulk_read(dev, SDREP_IN, (char*)tmpData, sizeof(tmpData), 500)) <= 0)
			break;
		WaitForSingleObject(sleepevent, 0);					// give away timeslice
	}

	if (ret < 0)
		_cprintf("usb_bulk_read() status %d: %s\n", ret, usb_strerror());

	starttime=GetTickCount();

	_cprintf("datatask about to enter main working loop\n");

	// ChannelMode variable got initialized from registry at InitHW()
	usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
							LIBUSB_MODE, 
							ChannelMode /*LIBMODE_16A*/,  
							SDR_BULKIF,  
							tmp, 1, 1000);

	// we must now be able to fetch the data before first double-buffered endpoint transfer would fail, otherwise sync may be lost!

	while ((do_datatask)&&(!globalshutdown))
	{
		//always fetch the data from SDR immediately, otherwise we will not be able to maintain the required 7.5Mbit (2x117k (6.2Mbit for 2x96k)) data rate!
		//EnterCriticalSection(&CriticalSection);
		ret = usb_bulk_read(dev, SDREP_IN, (char*)tmpData, sizeof(tmpData), 2000);
		//LeaveCriticalSection(&CriticalSection);

		//if there was any data, put it to staging ring buffer
		if (ret > 0)
		{
#pragma message(" ------- do another magic check here!")
			
			for (i=0, j=0, sdr_wptr=0; i<(unsigned int)ret; i++)
			{										
				// multiplex off data for panadapter
				if ((ChannelMode == CHMODE_ABPAN)&&((bytecounter+i)&0x4))				//A + panadapter. sdr_wptr+pandata_wptr+i is a method to calculate byte count since the beginning
				{										
					PANData[(pandata_wptr+j)&PANDATAMASK]=tmpData[i];	// 256K ring buffer
					j++;
				}
				else if ((ChannelMode == CHMODE_BAPAN)&&(!((bytecounter+i)&0x4)))		//B + panadapter
				{									
					PANData[(pandata_wptr+j)&PANDATAMASK]=tmpData[i];	// 256K ring buffer
					j++;
				}
				else
				{
					SDRData[sdr_wptr++]=tmpData[i];			// 

					if (sdr_wptr==SDRIQDATASIZE)
					{
						(*ExtIOCallback)(IQPAIRS, 0, 0, SDRData);		// IQPAIRS parameter is not used really by system for nothing else than checking if its negative
						sdr_wptr=0;
					}
				}
			}

			EnterCriticalSection(&CriticalSection);						
				pandata_wptr+=j;
			LeaveCriticalSection(&CriticalSection);

			totalbytes+=ret;
			bytecounter+=ret;
		}

		//stop and exit from task if there is any errors
		
		if (ret < 0)			// if we havent received data for 5 seconds, it is quite likely that radio is not responding, so quit data alltogether
		{
			_cprintf("ExtIODataTask() error %d retrieving data: %s\n", ret, usb_strerror());
			_cprintf("stopping ..\n");

			AfxMessageBox(_T("No I/Q Stream from Radio!"));
	
			// feed some data to Winrad, or otherwise winrad will hang, if there is no data whatsoever given after start button is pressed

			EnterCriticalSection(&CriticalSection);
				sdr_wptr=DATAMASK;							// just fake full buffer of data, so winrad will not block on us
				buffer_filled=true;							// set true, or data pump task will block
			LeaveCriticalSection(&CriticalSection);

			while(!fifo_loaded)								// wait until calback has filled winrad buffer
				WaitForSingleObject(sleepevent, 0);			// give away timeslice		

			EnterCriticalSection(&CriticalSection);
				do_datatask=false;							// shut down datatasks
			LeaveCriticalSection(&CriticalSection);

			(*ExtIOCallback)(-1, 108, 0, NULL);				// ask winrad to stop

			continue;					// resume loop, so while() will pop out with next loop
		}

		//update bitrate counter
		
		if (totalbytes > (120000*8))
		{
			lasttotalbytes=totalbytes;
			millis=GetTickCount()-starttime;
			mbits=totalbytes;
			mbits/=millis;
			mbits/=1000;
			mbits*=8;

			// global_dataratebuff is actually outputted by CExtIODialog::OnTimer()
			EnterCriticalSection(&CriticalSection);
			sprintf_s(global_dataratestr, 64, "%.3f Mbit/s", mbits);
			LeaveCriticalSection(&CriticalSection);

			starttime=GetTickCount();
			totalbytes=0;
			lasttotalbytes=0;
		}		

		//WaitForSingleObject(sleepevent, 0);		// give away timeslice		

		if (update_lo)
		{
			*(unsigned long*)&tmp[0]=lo_freq;
			last_synctuning=SyncTuning;

			EnterCriticalSection(&CriticalSection);
			update_lo=false;
			LeaveCriticalSection(&CriticalSection);

			if (SyncTuning)
			{
				//lastlo_freqA=lo_freq;
				usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
									LIBUSB_SETFREQ, 
									LIBUSB_CHA,  
									SDR_BULKIF,  
									tmp, 4, 1000);
				usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
									LIBUSB_SETFREQ, 
									LIBUSB_CHB,  
									SDR_BULKIF,  
									tmp, 4, 1000);
			}
			else
			{			
				switch (ChannelMode)
				{
				case CHMODE_B:	
				case CHMODE_BMA:
				case CHMODE_BAPAN:
					//lastlo_freqB=lo_freq;
					usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
										LIBUSB_SETFREQ, 
										LIBUSB_CHB,  
										SDR_BULKIF,  
										tmp, 4, 1000);
					break;

				default:
					// just reset frequencys and do nothing
					//lastlo_freqA=lo_freq;
					//lastlo_freqB=lo_freq;
					usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
										LIBUSB_SETFREQ, 
										LIBUSB_CHA,  
										SDR_BULKIF,  
										tmp, 4, 1000);
					break;
				}		
			}
		}

		if (do_pantableupdate)
		{
		char convertedbuff[24];

			EnterCriticalSection(&CriticalSection);
				do_pantableupdate=false;					// clear request flag 
			LeaveCriticalSection(&CriticalSection);

			convertedbuff[0]=(panentry.startfreq>>24)&0xFF;
			convertedbuff[1]=(panentry.startfreq>>16)&0xFF;
			convertedbuff[2]=(panentry.startfreq>>8)&0xFF;
			convertedbuff[3]=panentry.startfreq&0xFF;

			convertedbuff[4]=(panentry.samples>>24)&0xFF;
			convertedbuff[5]=(panentry.samples>>16)&0xFF;
			convertedbuff[6]=(panentry.samples>>8)&0xFF;
			convertedbuff[7]=panentry.samples&0xFF;

			convertedbuff[8]=(panentry.stepfreq>>24)&0xFF;
			convertedbuff[9]=(panentry.stepfreq>>16)&0xFF;
			convertedbuff[10]=(panentry.stepfreq>>8)&0xFF;
			convertedbuff[11]=panentry.stepfreq&0xFF;

			convertedbuff[12]=(panentry.steps>>24)&0xFF;
			convertedbuff[13]=(panentry.steps>>16)&0xFF;
			convertedbuff[14]=(panentry.steps>>8)&0xFF;
			convertedbuff[15]=panentry.steps&0xFF;

			convertedbuff[16]=(panentry.magic_I>>8)&0xFF;
			convertedbuff[17]=panentry.magic_I&0xFF;

			convertedbuff[18]=(panentry.magic_Q>>8)&0xFF;
			convertedbuff[19]=panentry.magic_Q&0xFF;

			convertedbuff[20]=(panentry.skip>>8)&0xFF;
			convertedbuff[21]=panentry.skip&0xFF;

			convertedbuff[22]=(panentry.dummy>>8)&0xFF;
			convertedbuff[23]=panentry.dummy&0xFF;

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
							LIBUSB_PANTABLE, 
							1,  //single PAN table entry
							SDR_BULKIF,  
							(char*)&convertedbuff[0], 24, 1000);
		}

		if (do_callback101)
		{
			EnterCriticalSection(&CriticalSection);
				do_callback101=false;					// clear callback request flag 
			LeaveCriticalSection(&CriticalSection);

			(*ExtIOCallback)(-1, 101, 0, NULL);			// sync LO frequency
		}	

		if (do_callback105)
		{
			EnterCriticalSection(&CriticalSection);
				do_callback105=false;					// clear callback request flag 
			LeaveCriticalSection(&CriticalSection);

			(*ExtIOCallback)(-1, 105, 0, NULL);			// sync tune frequency
		}		

		if (samplerate_changed)
		{
			EnterCriticalSection(&CriticalSection);
				samplerate_changed=false;					// clear sample rate change request flag 
			LeaveCriticalSection(&CriticalSection);
			
			*(unsigned long*)&tmp[0]=IQSampleRate;

			tmp[4]=16;
			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
									LIBUSB_SAMPLEMODE, 
									0,  
									SDR_BULKIF,  
									tmp, 5, 1000);

			(*ExtIOCallback)(-1, 100, 0, NULL);				// sync sampling rate
		}

		if (channelmode_changed)
		{
			EnterCriticalSection(&CriticalSection);
				channelmode_changed=false;					// clear channel mode change request flag 
			LeaveCriticalSection(&CriticalSection);

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
							LIBUSB_MODE, 
							ChannelMode /*LIBMODE_16A*/,  
							SDR_BULKIF,  
							tmp, 1, 1000);
		}

		if (update_phaseA)
		{
			EnterCriticalSection(&CriticalSection);
				update_phaseA=false;					// clear change request flag 
			LeaveCriticalSection(&CriticalSection);

			*(unsigned __int16*)&tmp[0]=(unsigned __int16)phaseword;		// global phaseword is calculated in slider dialog code

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
										LIBUSB_SETPHASE, 
										LIBUSB_CHA,  
										SDR_BULKIF,  
										tmp, 2, 1000);

			_cprintf("Phase for Ch A requested to set %u\n", *(unsigned __int16*)&tmp[0]);
		}

		if (update_IFgain)
		{
			EnterCriticalSection(&CriticalSection);
				update_IFgain=false;					// clear gain update request flag 
			LeaveCriticalSection(&CriticalSection);

			*(__int16*)&tmp[0]=(__int16)IFGainA;

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
										LIBUSB_SETGAIN, 
										LIBUSB_CHA,  
										SDR_BULKIF,  
										tmp, 2, 1000);

			*(__int16*)&tmp[0]=(__int16)IFGainB;

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
										LIBUSB_SETGAIN, 
										LIBUSB_CHB,  
										SDR_BULKIF,  
										tmp, 2, 1000);

			_cprintf("IFGains set: ChA=%d ChB=%d\n", IFGainA, IFGainB);
			
		}

		if (update_RFgain)
		{
			EnterCriticalSection(&CriticalSection);
				update_RFgain=false;					// clear gain update request flag 
			LeaveCriticalSection(&CriticalSection);

			_cprintf("AGC %s\n", (AGC)?"ON":"OFF");

			//doc sais that in order to go for manual gain, initial conditions must be programmed BEFORE agc loop is open, so we
			//will enabling AGC as a first thing always

			//read register with AGC integrator
			ret=usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
											LIBUSB_READREG, 
											20,			
											SDR_BULKIF,  
											tmp, 1, 1000);

			_cprintf("Reg(20): 0x%02X (retcode %d)\n", tmp[0], ret);

			if (AGC)
				tmp[0]&=0xF7;			//integrator in closed loop		
			else
				tmp[0]|=0x8;			//disable AGC

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
										LIBUSB_WRITEREG, 
										20,  
										SDR_BULKIF,  
										tmp, 1, 1000);

			if (!AGC)
			{
				tmp[1]=RFGainA;
				tmp[2]=RFGainB;

				tmp[1]<<=5;
				tmp[2]<<=5;
				tmp[1]|=0x1F;
				tmp[2]|=0x1F;
				tmp[1]=~tmp[1];
				tmp[2]=~tmp[2];

				usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
											LIBUSB_WRITEREG, 
											23,  
											SDR_BULKIF,  
											tmp+1, 2, 1000);

				_cprintf("RFGain values set: ChA=%d(reg23=0x%02X) ChB=%d(reg24=%02.2X)\n", RFGainA, (unsigned char)tmp[1], RFGainB, (unsigned char)tmp[2]);
			}			
		}

		//theoretically not needed, as we are calling extio callback and this seems to be taking care of timeslice.
		//However, if we have no data to give the load will likely peak so give away time forcefully every once and a while
		iterations++;
		if (!(iterations%10000))
			WaitForSingleObject(sleepevent, 1);		// give away timeslice		
	}	

	if (dev)
	{
		// return SDR to audio mode
		*(unsigned long*)&tmp[0]=48000;
		tmp[4]=16;
		usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
								LIBUSB_SAMPLEMODE, 
								0,  
								SDR_BULKIF,  
								tmp, 5, 1000);

		usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
								LIBUSB_MODE, 
								LIBMODE_OFF,  
								SDR_BULKIF,  
								tmp, 1, 1000);
		/*
		usb_release_interface(dev, 0);
		usb_close(dev);
		*/
	}

	_cprintf("ExtIODataTask() now leaving!\n");
	// indicate to main, that thread is done
	EnterCriticalSection(&CriticalSection);
		threadcount--;		
		datatask_running=false;
		datatask_done=true;
		sprintf_s(global_dataratestr, 64, "0 Mbit/s");		// reset datarate buffer, as we have stopped data pumps
	LeaveCriticalSection(&CriticalSection);

	return 1;
}
