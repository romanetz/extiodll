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


// note the triple buffering here. 
unsigned char tmpData[60*1024];			//Bulk RX buffer. Keep huge to allow USB receive loop to fetch whatever is there, so radio side of USB can transmit as fast as it could!
										//this buffer will be copied off immediately after bulk receive function returns. Note, that it has to be less than int/2
										//as we are indexing it with signed int and cant get negative idx
unsigned char SDRData[DATAMASK+1];		//this buffer is where we copy the tmpData to after each rx attempt. Note, that it is exactly an int long, so 
										//read and write indexes will automatically form a ring pointer
										//It is a ring buffer and 20kbytes will be cashed here just in case. IQData buffer content will be copied off from here.
unsigned char IQData[SDRIQDATASIZE];	//take this as global. This buffer is given to HDSDR for samples

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
extern volatile bool gainA_changed;
extern volatile bool gainB_changed;

extern volatile bool do_pantableupdate;
extern volatile bool panadaptertask_exited, run_panadaptertask;
extern PANENTRY panentry;

volatile bool update_phaseA=false;
volatile bool update_gain=false;

extern int ChannelMode;
extern int SyncTuning;
extern int SyncGain;

extern unsigned long lastlo_freqA;
extern unsigned long lastlo_freqB;
extern int GainA, GainB;
extern int PhaseCoarse, PhaseFine;

extern long IQSampleRate;

double callbackinterval;										// note, that interval calculations are done with floating point, because callback interval is not integer ms

unsigned int pandata_wptr = 0;
unsigned char* PANData=NULL;

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

void ExtIOCallbackTask(void* dummy);		// see at the end of file 
unsigned long sdr_wptr, sdr_readptr;
bool buffer_filled;

extern char global_dataratestr[64];

void ExtIODataTask(void* dummy)
{
HANDLE sleepevent = CreateEvent(NULL, FALSE, FALSE, NULL);		// we are using that instead of sleep(), as it is more kind to overall system resources
int ret;
unsigned long i, j, k;
unsigned long starttime, millis, totalbytes, lasttotalbytes, buffered;
double mbits;
int last_synctuning;
unsigned long phaseword;
unsigned int iterations=0;
unsigned long bytecounter=0;

//char errstring[128];
//unsigned long sdr_rptr_local, cached_len;
//usb_dev_handle *dev = NULL;			// device handle to be used by libusb
char tmp[128];

	EnterCriticalSection(&CriticalSection);
		threadcount++;
		datatask_done=false;		// show that we are now inside the thread
		datatask_running=true;
	LeaveCriticalSection(&CriticalSection);

	PANData=(unsigned char*)malloc(PANDATAMASK+1);		// allocate memory for panoramic data

	/*
	As the USB communication task is synchronous, we have to have another task what feeds the data with callback to software.
	There is no throttling mechanism prescribed by Winrad nor HDSDR, so we have to keep track ourselves after how many milliseconds 
	the callback has to be invoked.
	*/
	callbackinterval=((double)((SDRIQDATASIZE*1000)/(IQSampleRate*BYTESPERSAMPLE*2)));  //=callback interval ms
	/*
	192000 bytes/sec
	/8192=23.4375 x/sec
	1000ms/23.4375=42.6(6) msec interval
	*/

	_cprintf("ExtIODataTask() thread started\n");

	last_synctuning=SyncTuning;
/*	
	////////////
		// init libusb 		
	usb_init();			// initialize the library 
	usb_find_busses();	// find all busses
	usb_find_devices(); // find all connected devices
		
	if (!(dev = open_dev()))
	{
		sprintf_s(errstring, 128, "Error opening device: \n%s\n", usb_strerror());
		AfxMessageBox(errstring, MB_ICONEXCLAMATION);	
		_cprintf(errstring);
		//return(-10);
		do_datatask=false;
	}
	else if (usb_set_configuration(dev, SDR_CONFIG) < 0)
	{
		sprintf_s(errstring, 128, "Error setting config #%d: %s\n", SDR_CONFIG, usb_strerror());
		AfxMessageBox(errstring, MB_ICONEXCLAMATION);
		_cprintf(errstring);
		usb_close(dev);
		//return(-11);
		do_datatask=false;
	}
	else if (usb_claim_interface(dev, SDR_BULKIF) < 0) // check, if we can claim the bulk interface from radio?
	{
		sprintf_s(errstring, 128, "Error claiming interface #%d:\n%s\n", SDR_BULKIF, usb_strerror());
		AfxMessageBox(errstring, MB_ICONEXCLAMATION);
		_cprintf(errstring);
		usb_close(dev);
		//return(-12);
		do_datatask=false;
	}
*/
	totalbytes=0;
	lasttotalbytes=0;
	buffered=0;

	sdr_wptr=0;
	bytecounter=0;
	//iqdata_wptr=0;
	buffer_filled=false;

/*
	if (do_datatask)
	{	
		_cprintf("libusb started successfully (0x%04X:0x%04X_%02d)\n", SDR_VID, SDR_PID, SDR_BULKIF);
		do_callbacktask=true;
		_beginthread(ExtIOCallbackTask, 0, NULL);
	}
	else
		do_callbacktask=false;
*/

	// reset whatever is going on at the endpoint
	usb_clear_halt(dev, SDREP_IN);
	
	////////////


	// disable bulk interface
	usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
							LIBUSB_MODE, 
							LIBMODE_OFF,  
							SDR_BULKIF,  
							tmp, 1, 1000);

	// set sample mode now, to conserve time later!
	if (ChannelMode < 2)
	{
		*(unsigned long*)&tmp[0]=IQSAMPLERATE_FULL;
	}
	else if ((ChannelMode >=5) && (ChannelMode <=6))
	{
		*(unsigned long*)&tmp[0]=IQSAMPLERATE_PANADAPTER;
	}
	else
	{
		*(unsigned long*)&tmp[0]=IQSAMPLERATE_DIVERSITY;
	}

	tmp[4]=16;
	usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
							LIBUSB_SAMPLEMODE, 
							0,  
							SDR_BULKIF,  
							tmp, 5, 1000);

	/*
	StartHW() will set loop flag for us to perform gain and phase updates by datatask!
	// reset phase
	*(__int16*)&tmp[0]=(__int16)phaseword;
	usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
							LIBUSB_SETPHASE, 
							LIBUSB_CHA,  
							SDR_BULKIF,  
							tmp, 2, 1000);
	*/

	_cprintf("purge ..\n");
	//EnterCriticalSection(&CriticalSection);
	// fetch all data in buffer to establish I/Q sync
	while ((do_datatask)&&(!globalshutdown))
	{
		if ((ret=usb_bulk_read(dev, SDREP_IN, (char*)tmpData, sizeof(tmpData), 500)) <= 0)
			break;
		WaitForSingleObject(sleepevent, 0);					// give away timeslice
	}
	//LeaveCriticalSection(&CriticalSection);

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
	
	buffer_filled=false;

	while ((do_datatask)&&(!globalshutdown))
	{
		//always fetch the data from SDR immediately, otherwise we will not be able to maintain the required 7.5Mbit (2x117k (6.2Mbit for 2x96k)) data rate!
		//EnterCriticalSection(&CriticalSection);
		ret = usb_bulk_read(dev, SDREP_IN, (char*)tmpData, sizeof(tmpData), 2000);
		//LeaveCriticalSection(&CriticalSection);

		//if there was any data, put it to staging ring buffer
		if (ret > 0)
		{
			for (i=0, j=0, k=0; i<(unsigned int)ret; i++)
			{			
				//WaitForSingleObject(sleepevent, 0);					// give away timeslice		
				// multiplex off data for panadapter
				if ((ChannelMode == 5)&&((bytecounter+i)&0x4))				//A + panadapter. sdr_wptr+pandata_wptr+i is a method to calculate byte count since the beginning
				{										
					PANData[(pandata_wptr+j)&PANDATAMASK]=tmpData[i];	// 256K ring buffer
					j++;
				}
				else if ((ChannelMode == 6)&&(!((bytecounter+i)&0x4)))		//B + panadapter
				{									
					PANData[(pandata_wptr+j)&PANDATAMASK]=tmpData[i];	// 256K ring buffer
					j++;
				}
				else
				{
					SDRData[(sdr_wptr+k)&DATAMASK]=tmpData[i];			// somewhat slow but straightforward, as memmove() will likely not roll over at 64K	
					k++;
				}
			}

			EnterCriticalSection(&CriticalSection);		
				sdr_wptr+=k;								// copy current write offset from data pump task
				pandata_wptr+=j;
			LeaveCriticalSection(&CriticalSection);

			totalbytes+=ret;
			bytecounter+=ret;

			if (buffered < (SDRIQDATASIZE*DATATASK2CACHE))			// cache some amount of full buffers (we are going to write down immediately most of them to internal cache of HDSDR)
			{
				buffered+=ret;
				WaitForSingleObject(sleepevent, 0);		// give away timeslice	
				_cprintf("v");
				continue;
			}
			else
				buffer_filled=true;	
		}

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

		if (totalbytes > (120000*8))
		{
			lasttotalbytes=totalbytes;
			millis=GetTickCount()-starttime;
			mbits=totalbytes;
			mbits/=millis;
			mbits/=1000;
			mbits*=8;
			/*
			EnterCriticalSection(&CriticalSection);
			sdr_rptr_local=sdr_readptr;					// copy current read offset from callback task
			LeaveCriticalSection(&CriticalSection);

			if ((sdr_rptr_local&DATAMASK)<(sdr_wptr&DATAMASK))				// read ptr has to catch write ptr
				cached_len=(sdr_wptr&DATAMASK)-(sdr_rptr_local&DATAMASK);
			else															// rptr has to wrap over to catch wptr
				cached_len=(sdr_wptr&DATAMASK)+((DATAMASK+1)-(sdr_rptr_local&DATAMASK));

			cached_len/=SDRIQDATASIZE;
			*/
			// global_dataratebuff is actually outputted by CExtIODialog::OnTimer()
			EnterCriticalSection(&CriticalSection);
			sprintf_s(global_dataratestr, 64, "%.3f Mbit/s", mbits);
			LeaveCriticalSection(&CriticalSection);

			//NB! all methods below screw up the GUI (task will not shut down), so use only for testing purposes!
			//m_pmodeless->m_DataRateInfo.SetWindowText(ratebuff);
			//m_pmodeless->UpdateDataRate(ratebuff);		// ratebuff has to be global?
			//SendMessage(GetDlgItem(m_pmodeless->/*m_hWnd*/GetSafeHwnd(), IDC_EDIT2/*IDC_STATIC_TEST*/), WM_SETTEXT, 0, (LPARAM)ratebuff);
			//SendMessage(GetDlgItem(m_hWnd, IDC_STATIC_TEST), WM_SETTEXT, 0, (LPARAM)ratebuff);

			starttime=GetTickCount();
			totalbytes=0;
			lasttotalbytes=0;
		}		

		//WaitForSingleObject(sleepevent, 0);		// give away timeslice		

		if (/*((ChannelMode == CHMODE_A)&&(lastlo_freqA != lo_freq)) ||
			((ChannelMode == CHMODE_B)&&(lastlo_freqB != lo_freq)) ||
			((SyncTuning)&&(lastlo_freqA != lo_freq)) ||		// synchronous tuning is tracked by chA frequency
			(SyncTuning != last_synctuning)||*/
			(update_lo))		
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

			phaseword=1820;	//65535=2pi rad = 360deg; 65535/360*1000
			phaseword*=(PhaseCoarse*100)+PhaseFine;
			phaseword/=1000;

			*(__int16*)&tmp[0]=(__int16)phaseword;

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
										LIBUSB_SETPHASE, 
										LIBUSB_CHA,  
										SDR_BULKIF,  
										tmp, 2, 1000);

			_cprintf("Phase for Ch A set to %ld\n", phaseword);
		}

		if (update_gain)
		{
			EnterCriticalSection(&CriticalSection);
				update_gain=false;					// clear gain update request flag 
			LeaveCriticalSection(&CriticalSection);

			*(__int16*)&tmp[0]=(__int16)GainA;

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
										LIBUSB_SETGAIN, 
										LIBUSB_CHA,  
										SDR_BULKIF,  
										tmp, 2, 1000);

			*(__int16*)&tmp[0]=(__int16)GainB;

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
										LIBUSB_SETGAIN, 
										LIBUSB_CHB,  
										SDR_BULKIF,  
										tmp, 2, 1000);

			_cprintf("Gains set: ChA=%d ChB=%d\n", GainA, GainB);

		}

		iterations++;
		if (!(iterations%10000))
			WaitForSingleObject(sleepevent, 1);		// give away timeslice		
		else
			WaitForSingleObject(sleepevent, 0);		// give away timeslice		
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

	_cprintf("Waiting for callback task to exit ..\n");
	// shut down callback data pump
	if (do_callbacktask)
	{
		do_callbacktask=false;

		for (i=0; i<200; i++)			// wait alltogether two seconds
		{		
			if (!callbacktask_running)
				break;
			WaitForSingleObject(sleepevent, 10);		// give away timeslice		
		}
	}

	// have to wait for panadapter task to finish, otherwise PANData array gets lost before task finishes and HDSDR ends with exception
	_cprintf("Waiting for Panadapter task to exit ..\n");
	run_panadaptertask=false;
	for (i=0; i<200; i++)			// wait alltogether two seconds
	{
		if (panadaptertask_exited)
			break;
		WaitForSingleObject(sleepevent, 10);		// give away timeslice		
	}

	_cprintf("ExtIODataTask() now leaving!\n");
	// indicate to main, that thread is done
	EnterCriticalSection(&CriticalSection);
		threadcount--;		
		datatask_running=false;
		datatask_done=true;
		sprintf_s(global_dataratestr, 64, "0 Mbit/s");		// reset datarate buffer, as we have stopped data pumps
	LeaveCriticalSection(&CriticalSection);
}


void ExtIOCallbackTask(void* dummy)
{
HANDLE sleepevent = CreateEvent(NULL, FALSE, FALSE, NULL);		// we are using that instead of sleep(), as it is more kind to overall system resources
double nextpass;
int cachedblocks, iqdata_wptr;
unsigned long sdr_wptr_local;
unsigned int iterations=0;

	EnterCriticalSection(&CriticalSection);
		threadcount++;
		callbacktask_running=true;
	LeaveCriticalSection(&CriticalSection);	
	
	_cprintf("ExtIOCallbackTask() thread started\n");
	
	cachedblocks=0;
	sdr_readptr=0;
	iqdata_wptr=0;

	// wait for data task to cache enough data
	while((do_callbacktask)&&(!globalshutdown)&&(!buffer_filled))
	{
		WaitForSingleObject(sleepevent, 1);					// give away timeslice	
	}

	nextpass=GetTickCount();	// start counting time

	while((do_callbacktask)&&(!globalshutdown))
	{		
		// Oddly enough, if we are measuring time ourselves and just give time away with WaitForSingleObject(xx, 0)
		// the CPU gets 100% utilization. So we are now using WaitForSingleObject() for timings, what also gives away time then.

		//if ((cachedblocks < WINRAD2CACHE) || (nextpass <= GetTickCount()))
		{
			
			EnterCriticalSection(&CriticalSection);
			sdr_wptr_local=sdr_wptr;					// copy current write offset from data pump task
			LeaveCriticalSection(&CriticalSection);			

			//fill IQ data buffer 
			if (iqdata_wptr < SDRIQDATASIZE)
			{
				/*
				// If we are in panadapter mode, we have to split the stream in two between actual channel data and panadapter data
				if (ChannelMode == 5)	//A + panadapter
				{
					while (((sdr_readptr&DATAMASK) != (sdr_wptr_local&DATAMASK))&&(iqdata_wptr < SDRIQDATASIZE)&&(do_datatask)&&(!globalshutdown))
					{					
						if (!(sdr_readptr&0x4))
						{
							IQData[iqdata_wptr]=SDRData[sdr_readptr&DATAMASK];							
							iqdata_wptr++;
						}
						
						else
						{
							//data for panadapter
							//PANData[pandata_wptr&PANDATAMASK]=SDRData[sdr_readptr&DATAMASK];	// 256K ring buffer
							//pandata_wptr++;
						}
						

						sdr_readptr++;
						
						//WaitForSingleObject(sleepevent, 0);					// give away timeslice	
					}
				}
				else if (ChannelMode == 6)	//B + panadapter
				{
					while (((sdr_readptr&DATAMASK) != (sdr_wptr_local&DATAMASK))&&(iqdata_wptr < SDRIQDATASIZE)&&(do_datatask)&&(!globalshutdown))
					{					
						if (sdr_readptr&0x4)
						{
							IQData[iqdata_wptr]=SDRData[sdr_readptr&DATAMASK];									
							iqdata_wptr++;
						}
						
						else
						{
							//data for panadapter
							//PANData[pandata_wptr&PANDATAMASK]=SDRData[sdr_readptr&DATAMASK];	// 256K ring buffer
							//pandata_wptr++;
						}
						
						sdr_readptr++;
						
						//WaitForSingleObject(sleepevent, 0);					// give away timeslice	
					}
				}
				else
				{
				*/
					while (((sdr_readptr&DATAMASK) != (sdr_wptr_local&DATAMASK))&&(iqdata_wptr < SDRIQDATASIZE)&&(do_datatask)&&(!globalshutdown))
					{					
						IQData[iqdata_wptr]=SDRData[sdr_readptr&DATAMASK];		// note, that as sdr_rptr was initialized to -1, we must increment it _before_ usage.
						sdr_readptr++;
						iqdata_wptr++;
						//WaitForSingleObject(sleepevent, 0);					// give away timeslice	
					}
				//}
			}

			if (iqdata_wptr == SDRIQDATASIZE)
			{
				(*ExtIOCallback)(IQPAIRS, 0, 0, IQData);		// IQPAIRS parameter is not used really by system for nothing else than checking if its negative
				iqdata_wptr=0;

				if (!fifo_loaded)
				{
					cachedblocks++;
					_cprintf("^");
				}
				//else
				//	nextpass+=callbackinterval;						// get time for next pass
			}						
		}
		
		if ((!fifo_loaded)&&(cachedblocks == WINRAD2CACHE))
		{
			fifo_loaded=true;			
		}
		
		if (!fifo_loaded)
			WaitForSingleObject(sleepevent, 0);					// give away timeslice
		else
		{
			// just waiting by callbackinterval ms will give improper timing, as
			// our time granularity is around 55ms in windows and we may end up starving
			nextpass+=callbackinterval;
			WaitForSingleObject(sleepevent, max(0, (nextpass-GetTickCount())));
		}
	}

	EnterCriticalSection(&CriticalSection);
		threadcount--;		
		callbacktask_running=false;		
	LeaveCriticalSection(&CriticalSection);
}