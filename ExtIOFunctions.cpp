/*

	Written by Andrus Aaslaid, ES1UVB
	andrus.aaslaid(6)gmail.com

	http://uvb-76.net

	This source code is licensed as Creative Commons Attribution-ShareAlike
	(CC BY-SA). 
	
	From http://creativecommons.org:

		This license lets others remix, tweak, and build upon your work even for commercial purposes, as long as they 
		credit you and license their new creations under the identical terms. This license is often compared to 
		“copyleft” free and open source software licenses. All new works based on yours will carry the same license, 
		so any derivatives will also allow commercial use. This is the license used by Wikipedia, and is recommended 
		for materials that would benefit from incorporating content from Wikipedia and similarly licensed projects. 


	This DLL provides an empty core for implementing hardware support functionality
	for the Winrad Software Defined Radio (SDR) application from Alberto Di Bene (http://www.weaksignals.com/)
	and its offsprings supporting the same ExtIO DLL format, most notably the outstanding HDSDR software (http://hdsdr.org)

	As the Winrad source is written on Borland C-Builder environment, there has been very little 
	information available of how the ExtIO DLL should be implemented on Microsoft Visual Studio 2008
	(VC2008) environment and the likes. 

	This example is filling this gap, providing the empty core what can be compiled as appropriate 
	DLL working for HDSDR

	Note, that Winrad and HDSDR are sometimes picky about the DLL filename. The ExtIO_blaah.dll for example,
	works, while ExtIODll.dll does not. I havent been digging into depths of that. It is just that if your
	custom DLL refuses to be recognized by application for no apparent reason, trying to change the DLL filename
	may be a good idea.

	To have the DLL built with certain name can be achieved changing the LIBRARY directive inside ExtIODll.def


	Revision History:

			30.05.2011	-	Initial 
			29.08.2011	-	Added Graphical interface and console window to example
			23.04.2012	-	Removed all extra stuff to strip it down to pure MFC GUI example
			24.04.2012	-	Cleaned up for public release

			08.09.2012	-	Replaced About information credits about Winrad according to what LC from hdsdr.de 
							suggested
						-	Started to implement libusb functionality
			16.09.2012	-	libusb data transfer at 192kHz works!! Was "only" one week steady head-banging to 
							figure out
							how to correctly feed data with extio callback ..
			18.09.2012	-	Switched over to 230kHz family of sample rates
						-	Created logic for LO drag-alone when frequency is approaching LO bounds
	v1.21	27.09.2012	-	Added libusb version check on startup at InitHW()
						-	Added libusb driver and dll version info to DLL screen

	v1.22	05.10.2012	-	Fixed WaitForSingleObject() issue what caused 100% utilization for one CPU core
							(ID_ JBJ01) (Thanx to Mario from hdsdr.de!)
						-	Slightly restructured thread invoking
						-	Changed default transparency setting to 90% opaque

	To Do:

		- Make diversity mode work so that both channels could be tweaked, so will get 2x360deg span
		- Diversity and gain selection for ExtIO mode
		- Sample rate switching for ExtIO mode
		x Cache size adjustment control for used form
		- A/B/HDSDR channel selection with LO and freq feedback through callback
		- Synchronous tuning
		- Last freq etc. parameters update for registry
		* LO frequency following when tuning reaches the border

*/



#include "stdafx.h"

#include "ExtIODll.h"
#include "ExtIODialog.h"
#include "ExtIOFunctions.h"

#include "libusb\lusb0_usb.h"

//serial.h is included from ExtIOFunctions.h

#include <conio.h>		// gives _cprintf()
#include <process.h>	// gives _beginthread()

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif


void ExtIODataTask(void* dummy);
void ExtIOCallbackTask(void* dummy);

/*

Some notes:

-	The functions are exported as simple functions, no being part of any class. This is caused by the fact that we
	do not need the class functionality, as well as the fact that this DLL will be used by Borland C-builder 
	environment, what sets the certain requirements for the DLL.
	See http://rvelthuis.de/articles/articles-cppobjs.html and http://bytes.com/topic/c/answers/731703-use-visual-c-dll-c-builder
	for some reference information, why we can not export this as a class.

-	The interface is assuming 8-bit characters, so be sure that you have Configuration Properties->General->Character Set set to "Not Set"

*/

#ifndef SERIAL_NO_OVERLAPPED
#error SERIAL_NO_OVERLAPPED has to be defined
#endif

CExtIODialog* m_pmodeless;
bool hasconsole=false;

unsigned long lo_freq=4624000L;
unsigned long tune_freq=0;
unsigned char modulation='U';
unsigned long iflimit_high=-1;
unsigned long iflimit_low=0;

// our serial port class. Have to access it from many places, so keep global.
CSerial	serial;
volatile bool serinit_ok=false;
volatile bool libusb_ok=false;
int HWType;

volatile bool do_datatask = false;			// indicator that the datatask must be run
extern volatile bool do_callbacktask;
volatile bool datatask_running = false;		// indicator that we are actually inside the data task
volatile bool datatask_done = false;		// will be set at exit to true to indicate that task has left a building
volatile bool lo_changed = false;
extern volatile bool channelmode_changed;

volatile bool do_callback105=false;			// indicate, if datatask should call callback 105
volatile bool update_registry=false;

extern volatile bool fifo_loaded;

volatile bool globalshutdown = false;		// indicate that all threads must terminate
volatile int threadcount=0;					// number of running threads;
CRITICAL_SECTION CriticalSection;	// global thread locking object

int ChannelMode;					// indicates, which channel mode is selected by extio dialog
int SyncTuning;
int SyncGain;

unsigned long lasttune_freqA=-1;	// init so, that HDSDR would update immediately on startup
unsigned long lasttune_freqB=-1;

extern volatile bool update_lo;
unsigned long lastlo_freqA=-1;
unsigned long lastlo_freqB=-1;

extern int Transparency;

long IQSampleRate;

int GainA, GainB;
int PhaseCoarse, PhaseFine;
int DebugConsole;

extern volatile bool update_phaseA;
extern volatile bool update_gain;

usb_dev_handle *dev = NULL;					// device handle to be used by libusb
const struct usb_version* libver = NULL;	// libusb version information
//char tmp[128];

/*
This entry is the first called by Winrad at startup time, and it is used both to tell to the DLL that it is 
time to initialize the hardware, and to get back a descriptive name and model (or Serial Number) of the HW, 
together with a type code.

Parameters :

	name	-	descriptive name of the hardware. Preferably not longer than about 16 characters, as it will be used in a Winrad menu.
	model	-	model code of the hardware, or its Serial Number. Keep also this field not too long, for the same reason of the previous one.
	type	-	this is an index code that Winrad uses to identify the hardware type supported by the DLL.
				Please use one the following values :
					3	the hardware does its own digitization and the audio data are returned to Winrad via the callback device. Data must be in 16-bit (short) format, little endian.
					4	The audio data are returned via the sound card managed by Winrad. The external hardware just controls the LO, and possibly a preselector, under DLL control.
					5	the hardware does its own digitization and the audio data are returned to Winrad via the callback device. Data are in 24-bit integer format, little endian.
					6	the hardware does its own digitization and the audio data are returned to Winrad via the callback device. Data are in 32-bit integer format, little endian.
					7	the hardware does its own digitization and the audio data are returned to Winrad via the callback device. Data are in 32-bit float format, little endian.
				
				Please ask me (i2phd@weaksignals.com) for the assignment of an index code for cases different from the above.
Return value :

	true	-	everything went well, the HW did initialize, and the return parameters have been filled.
	false	-	the HW did not initialize (error, or powered off, or other reasons).
*/

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


/*
Monitors the LO and Tuning frequency for channels and if changed, writes new value to registry
*/
void ExtIORegistryUpdateTask(void* dummy)
{
HANDLE sleepevent = CreateEvent(NULL, FALSE, FALSE, NULL);		// we are using that instead of sleep(), as it is more kind to overall system resources
static long lasttuna=-1, lasttunb=-1, lastloa=-1, lastlob=-1;
static long lasttransparency=-1, lastgaina=-1, lastgainb=-1, lastcoarse=-1, lastfine=-1;
int i;

	// Wait for fifo to be filled with data before returning to program
	while(!globalshutdown)
	{
		for (i=0; i<10; i++)
		{
			if (update_registry)
			{
				update_registry=false;			// should be in critical section really, but works without as well
				break;
			}
			WaitForSingleObject(sleepevent, 1000);					// do it only after every 10 seconds
		}

		if (lastlo_freqA != lastloa)
		{
			AfxGetApp()->WriteProfileInt(_T("Config"), _T("LastLO_A"), lastlo_freqA);
			lastloa=lastlo_freqA;
		}

		if (lastlo_freqA != lastloa)
		{
			AfxGetApp()->WriteProfileInt(_T("Config"), _T("LastLO_B"), lastlo_freqB);
			lastlob=lastlo_freqB;
		}

		if (lasttune_freqA != lasttuna)
		{
			AfxGetApp()->WriteProfileInt(_T("Config"), _T("LastTune_A"), lasttune_freqA);
			lasttuna=lasttune_freqA;
		}

		if (lasttune_freqB != lasttunb)
		{
			AfxGetApp()->WriteProfileInt(_T("Config"), _T("LastTune_B"), lasttune_freqB);
			lasttunb=lasttune_freqB;
		}

		if (Transparency != lasttransparency)
		{
			AfxGetApp()->WriteProfileInt(_T("Config"), _T("Transparency"), Transparency);
			lasttransparency=Transparency;
		}

		if (PhaseCoarse != lastcoarse)
		{
			AfxGetApp()->WriteProfileInt(_T("Config"), _T("PhaseCoarse"), PhaseCoarse);
			lastcoarse=PhaseCoarse;
		}

		if (PhaseFine != lastfine)
		{
			AfxGetApp()->WriteProfileInt(_T("Config"), _T("PhaseFine"), PhaseFine);
			lastfine=PhaseFine;
		}

		if (GainA != lastgaina)
		{
			AfxGetApp()->WriteProfileInt(_T("Config"), _T("GainA"), GainA);
			lastgaina=GainA;
		}

		if (GainB != lastgainb)
		{
			AfxGetApp()->WriteProfileInt(_T("Config"), _T("GainB"), GainB);
			lastgainb=GainB;
		}
	}
}

extern "C" bool __stdcall InitHW(char *name, char *model, int& type)
{
//static bool first = true;
char errstring[128];
int consoletransparency;
unsigned long long libvernum;

	DebugConsole=AfxGetApp()->GetProfileInt(_T("Config"), _T("DebugConsole"), 0);
	Transparency = AfxGetApp()->GetProfileInt(_T("Config"), _T("Transparency"), -1);

	consoletransparency=Transparency;

	if (consoletransparency == -1)
		consoletransparency = DEFAULTTRANSPARENCY;

	//--------------
	// Create console. This is convenient for _cprintf() debugging, but as we will 
	// set up this as a transparent window, it may also be of use for other things.
	//--------------
	if (DebugConsole)
	{
		if (!AllocConsole())
		{
			hasconsole=false;
			AfxMessageBox("Failed to create the console!", MB_ICONEXCLAMATION);
		}
		else
		{
		HWND hWnd;

			hasconsole=true;
			HANDLE nConsole = GetStdHandle(STD_OUTPUT_HANDLE);
			SetConsoleTextAttribute(nConsole, 0x0002|0x0008);	//green|white to get the bright color
			SetConsoleTitle("ExtIO DLL Console");

			hWnd=GetConsoleWindow();
			SetWindowLong(hWnd, GWL_EXSTYLE, ::GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);		// add layered attribute
			SetLayeredWindowAttributes(hWnd, 0, 255 * consoletransparency /*percent*//100, LWA_ALPHA);				// set transparency
			
			//Have to disable close button, as this will kill the application instance with no questions asked!
			//Note, that application is still terminated when the consle is closed from taskbar.
			DeleteMenu(GetSystemMenu(hWnd, false), SC_CLOSE, MF_BYCOMMAND);

			_cprintf("InitHW(): Console initialized\n");
		}
	}

	InitializeCriticalSectionAndSpinCount(&CriticalSection, 0x00000400);
	
	m_pmodeless=NULL;		// reset GUI so we will be able to track the state

	HWType = AfxGetApp()->GetProfileInt(_T("Config"), _T("HardwareType"), -1);

	// get channel mode and tuning sync setting
	ChannelMode=AfxGetApp()->GetProfileInt(_T("Config"), _T("ChannelMode"), CHMODE_A);
	SyncTuning=AfxGetApp()->GetProfileInt(_T("Config"), _T("SyncTuning"), 0);
	SyncGain=AfxGetApp()->GetProfileInt(_T("Config"), _T("SyncGain"), 0);
	GainA=AfxGetApp()->GetProfileInt(_T("Config"), _T("GainA"), 7);
	GainB=AfxGetApp()->GetProfileInt(_T("Config"), _T("GainB"), 7);
	PhaseCoarse=AfxGetApp()->GetProfileInt(_T("Config"), _T("PhaseCoarse"), 0);
	PhaseFine=AfxGetApp()->GetProfileInt(_T("Config"), _T("PhaseFine"), 0);	

	// for diversity modes, force tune syncing!
	if ((ChannelMode > 1)&&(ChannelMode < 5))
	{
		IQSampleRate = IQSAMPLERATE_DIVERSITY;
		SyncTuning=1;
	}
	else
	{
		IQSampleRate = IQSAMPLERATE_FULL;
	}

	if (HWType != 3)
		IQSampleRate = IQSAMPLERATE_AUDIO;

	// for all other modes than ExtIO DLL we are supporting only standard mode and diversity mode, which is the same as A-B
	if ((HWType != 3) && (!(ChannelMode == CHMODE_A)||(ChannelMode == CHMODE_AMB)))
		ChannelMode=CHMODE_A;

	_cprintf("ChannelMode = %d\n", ChannelMode);

	// There is not really a good way for fetching the LO and Tune frequency from HDSDR when noone has actually touched
	// the frequency controls. Therefore, we are mirroring those ourselves and init the HDSDR to appropriate values ...

	lastlo_freqA=AfxGetApp()->GetProfileInt(_T("Config"), _T("LastLO_A"), 0);
	lastlo_freqB=AfxGetApp()->GetProfileInt(_T("Config"), _T("LastLO_B"), 0);
	lasttune_freqA=AfxGetApp()->GetProfileInt(_T("Config"), _T("LastTune_A"), 0);
	lasttune_freqB=AfxGetApp()->GetProfileInt(_T("Config"), _T("LastTune_B"), 0);
	

	if (ChannelMode == CHMODE_B)
	{
		lo_freq=lastlo_freqB;
		tune_freq=lasttune_freqB;
	}
	else
	{
		lo_freq=lastlo_freqA;
		tune_freq=lasttune_freqA;
	}	

	_beginthread(ExtIORegistryUpdateTask, 0, NULL);

	if ((HWType == -1)||(HWType < 3) || (HWType > 7))
		HWType = 4;			//4 ==> data returned via the sound card

	type = HWType;

	_cprintf("HWType = %d\n", HWType);

	strcpy(name, "SDR MK1.5 Andrus");	// change with the name of your HW
	strcpy(model, "SDR MK1.5 Andrus");	// change with the model of your HW

	if (HWType == 3)
	{
		_cprintf("Initializing libusb\n");		
		////////////
		// init libusb 		
		usb_init();			// initialize the library 

		libver=usb_get_version();

		if (libver)
		{
			// for easy comparision, lets build a number out of versions! xxx.xxx.xxx.xxx
			libvernum=(libver->dll.major*1000000000)+(libver->dll.minor*1000000)+(libver->dll.micro*1000)+libver->dll.nano;
			if (libvernum < ((LIB_MIN_MAJOR*1000000000)+(LIB_MIN_MINOR*1000000)+(LIB_MIN_MICRO*1000)+LIB_MIN_NANO))
			{
				sprintf_s(errstring, 128, "Incompatible LibUSB-win32 DLL: v%d.%d.%d.%d (Minimum Required: v%d.%d.%d.%d)",
						libver->dll.major, libver->dll.minor, libver->dll.micro, libver->dll.nano,
						LIB_MIN_MAJOR, LIB_MIN_MINOR, LIB_MIN_MICRO, LIB_MIN_NANO);
				AfxMessageBox(errstring, MB_ICONEXCLAMATION);	
			}

			libvernum=(libver->driver.major*1000000000)+(libver->driver.minor*1000000)+(libver->driver.micro*1000)+libver->driver.nano;
			if (libvernum < ((LIB_MIN_MAJOR*1000000000)+(LIB_MIN_MINOR*1000000)+(LIB_MIN_MICRO*1000)+LIB_MIN_NANO))
			{
				sprintf_s(errstring, 128, "Incompatible LibUSB-win32 Driver: v%d.%d.%d.%d (Minimum Required: v%d.%d.%d.%d)",
						libver->driver.major, libver->driver.minor, libver->driver.micro, libver->driver.nano,
						LIB_MIN_MAJOR, LIB_MIN_MINOR, LIB_MIN_MICRO, LIB_MIN_NANO);
				AfxMessageBox(errstring, MB_ICONEXCLAMATION);	
			}
		}
		else
		{
			AfxMessageBox("Unable to Fetch LibUSB-win32 Version Information!", MB_ICONEXCLAMATION);
		}

		usb_find_busses();	// find all busses
		usb_find_devices(); // find all connected devices
			
		libusb_ok=true;

		if (!(dev = open_dev()))
		{
			sprintf_s(errstring, 128, "Error opening device: %s\n", usb_strerror());
			AfxMessageBox(errstring, MB_ICONEXCLAMATION);	
			_cprintf(errstring);
			libusb_ok=false;
		}
		else if (usb_set_configuration(dev, SDR_CONFIG) < 0)
		{
			sprintf_s(errstring, 128, "Error setting config #%d: %s\n", SDR_CONFIG, usb_strerror());
			AfxMessageBox(errstring, MB_ICONEXCLAMATION);
			_cprintf(errstring);
			usb_close(dev);
			libusb_ok=false;
		}
		else if (usb_claim_interface(dev, SDR_BULKIF) < 0) // check, if we can claim the bulk interface from radio?
		{
			sprintf_s(errstring, 128, "Error claiming interface #%d:\n%s\n", SDR_BULKIF, usb_strerror());
			AfxMessageBox(errstring, MB_ICONEXCLAMATION);
			_cprintf(errstring);
			usb_close(dev);
			libusb_ok=false;
		}
	}

	return true;
}

/*
This entry is called by Winrad each time the user specifies that Winrad should receive its audio data input through 
the hardware managed by this DLL, or, if still using the sound card for this, that the DLL must activate the control 
of the external hardware. It can be used by the DLL itself for delayed init tasks, like, e.g., the display of its own 
GUI, if the DLL has one.

It has no parameters.

Return value :

	true	-	everything went well.
	false	-	some error occurred, the external HW cannot be controlled by the DLL code.
*/

extern "C" bool __stdcall OpenHW(void)
{
	//.... display here the DLL panel ,if any....
	//.....if no graphical interface, delete the following statement
	//::SetWindowPos(F->handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

	// in the above statement, F->handle is the window handle of the panel displayed
	// by the DLL, if such a panel exists
	_cprintf("OpenHW() called\n");

	return true;
}

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
/*
This entry is called by Winrad each time the user presses the Start button on the Winrad main screen, after having 
previously specified that the DLL is in control of the external hardware.

Parameters :

	freq	-	an integer specifying the frequency the HW should be set to, expressed in Hz.

Return value :

	An integer specifying how many I/Q pairs are returned by the DLL each time the callback function is invoked (see later). 
	This information is used of course only when the input data are not coming from the sound card, but through the callback device.
	If the number is negative, that means that an error has occurred, Winrad interrupts the starting process and returns to the idle status.
	The number of I/Q pairs must be at least 512, or an integer multiple of that value.
*/



extern "C" int __stdcall StartHW(long freq)
{
char errstring[128];
long lLastError;
char sdrport[16];
char freqstring[32];
HANDLE sleepevent = CreateEvent(NULL, FALSE, FALSE, NULL);		// we are using that instead of sleep(), as it is more kind to overall system resources
unsigned long phaseword;
char modetmp[16];

	//if libusb is not OK for some reason (for example, when radio was not connected at startup)
	//try once more once here!

	if (!libusb_ok)
	{
		libusb_ok=true;

		usb_find_busses();	// find all busses
		usb_find_devices(); // find all connected devices

		if (!(dev = open_dev()))
		{
			sprintf_s(errstring, 128, "Error opening device: \n%s\n", usb_strerror());
			AfxMessageBox(errstring, MB_ICONEXCLAMATION);	
			_cprintf(errstring);
			libusb_ok=false;
		}
		else if (usb_set_configuration(dev, SDR_CONFIG) < 0)
		{
			sprintf_s(errstring, 128, "Error setting config #%d: %s\n", SDR_CONFIG, usb_strerror());
			AfxMessageBox(errstring, MB_ICONEXCLAMATION);
			_cprintf(errstring);
			usb_close(dev);
			libusb_ok=false;
		}
		else if (usb_claim_interface(dev, SDR_BULKIF) < 0) // check, if we can claim the bulk interface from radio?
		{
			sprintf_s(errstring, 128, "Error claiming interface #%d:\n%s\n", SDR_BULKIF, usb_strerror());
			AfxMessageBox(errstring, MB_ICONEXCLAMATION);
			_cprintf(errstring);
			usb_close(dev);
			libusb_ok=false;
		}
	}

	if (HWType == 3)		// extio DLL manages sample data, so we are going to bulk USB mode and control the radio using USB control messages!
	{		
		if (!libusb_ok)
		{
			_cprintf("StartHW(): libusb is not ok!\n");
			return -1;		// indicate error nd stop whole process
		}
		

		do_datatask=true;
		do_callbacktask=true;
		//libusb_ok=true;
		fifo_loaded=false;

		_beginthread(ExtIOCallbackTask, 0, NULL);
		_beginthread(ExtIODataTask, 0, NULL);		

		// Wait for fifo to be filled with data before returning to program
		while(!globalshutdown)
		{
			WaitForSingleObject(sleepevent, 0);					// give away timeslice
			if (fifo_loaded==true)
				break;
		}		
	}
	else
	{
		// regardless of whenever or not the radio actually has a bulk transfer capable firmware installed, 
		// try issuing a control message to it to put the radio back to 2x48kHz audio mode should we have been in bulk mode before.
		if (libusb_ok)
		{
			usb_clear_halt(dev, SDREP_IN);

			*(unsigned long*)&modetmp[0]=IQSAMPLERATE_AUDIO;
			modetmp[4]=16;
			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
									LIBUSB_SAMPLEMODE, 
									0,  
									SDR_BULKIF,  
									modetmp, 5, 1000);

			usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
									LIBUSB_MODE, 
									LIBMODE_OFF,  
									SDR_BULKIF,  
									modetmp, 1, 1000);
			
		}

		// Open serial port

		int nPortNum = AfxGetApp()->GetProfileInt(_T("Config"), _T("ComPort"), -1);

		if (nPortNum == -1)
		{
			sprintf_s(errstring, 128, "No COM Port Selected - Press ExtIO Button");
			AfxMessageBox(errstring, MB_ICONEXCLAMATION);	
			return(-1);
		}

		sprintf_s(sdrport, 16, "\\\\.\\\\COM%d", nPortNum);

		lLastError=serial.Open(sdrport,9*1024,9*1024,false);	// non-overlapped. Also, take 9k for input and output queues queue.
		if (lLastError != ERROR_SUCCESS)
		{
			sprintf_s(errstring, 128, "Unable to open port COM%d (err=%ld)", nPortNum, serial.GetLastError());
			AfxMessageBox(errstring, MB_ICONEXCLAMATION);
			return(-1);
		}

		// Setup serial port (8N1). For CDC port the baudrate is not really important
		lLastError += serial.Setup(CSerial::EBaud256000,CSerial::EData8,CSerial::EParNone,CSerial::EStop1);
		lLastError += serial.SetupHandshaking(CSerial::EHandshakeOff);
	/*
		lLastError += serial.SetMask(CSerial::EEventBreak |
									CSerial::EEventCTS   |
									CSerial::EEventDSR   |
									CSerial::EEventError |
									CSerial::EEventRing  |
									CSerial::EEventRLSD  |
									CSerial::EEventRecv);
	*/
		// Use 'non-blocking' reads, because we don't know how many bytes
		// will be received. This is normally the most convenient mode
		// (and also the default mode for reading data).
		lLastError += serial.SetupReadTimeouts(CSerial::EReadTimeoutNonblocking);

		if (lLastError)
		{
			sprintf_s(errstring, 128, "Unable to configure port COM%d", nPortNum);
			AfxMessageBox(errstring, MB_ICONEXCLAMATION);
			return(-1);
		}
	/*
		HANDLE hevtOverlapped = ::CreateEvent(0,TRUE,FALSE,0);;

		OVERLAPPED ov = {0};
		ov.hEvent = hevtOverlapped;

		HANDLE hevtStop = ::CreateEvent(0,TRUE,FALSE,_T("Overlapped_Stop_Event_SDR"));
	*/
		serinit_ok=true;

		// sync radio freq with HDSDR displayed freq.
		// This is mandatory, as HDSRD calls SetHWLO() before the function here, and as serial is not initialized 
		// at this point, we will havd LO frequency out of sync for SDR otherwise

		if (ChannelMode == CHMODE_AMB)
			sprintf_s(freqstring, 32, "diversity 1\n\r");
		else
			sprintf_s(freqstring, 32, "diversity 0\n\r");

		serial.Write(freqstring, strlen(freqstring));

		sprintf_s(freqstring, 32, "_fa %ld\n\r", lo_freq);
		serial.Write(freqstring, strlen(freqstring));
	}	

	// set or ask datatask to set default values for gain and phase

	if (HWType != 3)
	{
		sprintf_s(freqstring, 32, "_ga %d\n\r", GainA);
		serial.Write(freqstring, strlen(freqstring));
		sprintf_s(freqstring, 32, "_gb %d\n\r", GainB);
		serial.Write(freqstring, strlen(freqstring));

		phaseword=1820;	//65535=2pi rad = 360deg; 65535/360*1000
		phaseword*=(PhaseCoarse*100)+PhaseFine;
		phaseword/=1000;

		sprintf_s(freqstring, 32, "_pa %ld\n\r", phaseword);
		serial.Write(freqstring, strlen(freqstring));

		return(0);
	}
	else
	{
		update_gain=true;
		update_phaseA=true;
		channelmode_changed=true;
		lo_changed=true;
		return (IQPAIRS);	// number of complex elements returned each invocation of the callback routine
	}
}

/*
This entry is called by Winrad each time the user presses the Stop button on the Winrad main screen. It can be used by the DLL 
for whatever task might be needed in such an occurrence. If the external HW does not provide the audio data, being, e.g., 
just a DDS or some other sort of an oscillator, typically this call is a no-op. The DLL could also use this call to hide its GUI, if any.

If otherwise the external HW sends the audio data via the USB port, or any other hardware port managed by the DLL, when this 
entry is called, the HW should be commanded by the DLL to stop sending data.

It has no parameters and no return value.

*/

extern "C" void __stdcall StopHW(void)
{
HANDLE sleepevent = CreateEvent(NULL, FALSE, FALSE, NULL);		// we are using that instead of sleep(), as it is more kind to overall system resources
char modetmp[16];

	_cprintf("StopHW() called\n");

	if (serinit_ok)
	{
		serinit_ok=false;
		serial.Close();	
	}

	if (datatask_running)		// do we have a running data task?
	{
		_cprintf("StopHW: waiting for data thread to shut down (threadcount=%d)\n", threadcount);
		do_datatask=false;		// thread shutdown flag for ExtIOIQData task

		while (!datatask_done)
		{
			WaitForSingleObject(sleepevent, 0);		// give away timeslice
		}
		_cprintf("StopHW: data thread finished (threadcount=%d)\n", threadcount);
	}

	if ((HWType == 3)&&(libusb_ok))
	{
		// return SDR to audio mode
		*(unsigned long*)&modetmp[0]=IQSAMPLERATE_AUDIO;
		modetmp[4]=16;
		usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
								LIBUSB_SAMPLEMODE, 
								0,  
								SDR_BULKIF,  
								modetmp, 5, 1000);

		usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
								LIBUSB_MODE, 
								LIBMODE_OFF,  
								SDR_BULKIF,  
								modetmp, 1, 1000);
	}

	return; 
}

/*
This entry is called by Winrad when the User indicates that the control of the external HW is no longer needed or wanted. 
This is done in Winrad by choosing ShowOptions | Select Input then selecting either WAV file or Sound Card. The DLL can use this 
information to e.g. shut down its GUI interface, if any, and possibly to put the controlled HW in a idle status.

It has no parameters and no return value.
*/

extern "C" void __stdcall CloseHW(void)
{
	// ..... here you can shutdown your graphical interface, if any............

	// this is a right place to shut down graphical interface, as HideGUI() does not get called during he exit in some 
	// reason and therefore is not possible to use for cleanup.

	if (m_pmodeless)
	{
		m_pmodeless->DestroyWindow();
	}

	if (hasconsole)
	{
		hasconsole=false;
		if (!FreeConsole())
			AfxMessageBox("Could not free the console!");
	}

	if (dev)
	{
		usb_release_interface(dev, 0);
		usb_close(dev);
	}
}

/*
This entry point is used by Winrad to communicate and control the desired frequency of the external HW via the DLL. 
The frequency is expressed in units of Hz. The entry point is called at each change (done by any means) of the LO value in 
the Winrad main screen.

Parameters :

	LOfreq	-	a long integer specifying the frequency the HW LO should be set to, expressed in Hz.

Return values :

	0	-	The function did complete without errors.
	<0	-	(a negative number N) The specified frequency is lower than the minimum that the hardware 
			is capable to generate. The absolute value of N indicates what is the minimum supported by the HW.
	>0	-	(a positive number N) The specified frequency is greater than the maximum that the hardware 
			is capable to generate. The value of N indicates what is the maximum supported by the HW.
*/

extern "C" int __stdcall SetHWLO(long LOfreq)
{	
char freqstring[32];

	//	..... set here the LO frequency in the controlled hardware
	// NOTE! During startup, this is called before serial has been initialized by StartHW.
	// Therefore we will have to always re-init hardware frequency during startup with the 
	// lo_freq value set here, otherwise LO frequency is going to be out of sync.

	_cprintf("SetHWLO() called\n");

	// store channel-specific
	switch(ChannelMode)
	{
	case CHMODE_A:
		lastlo_freqA=LOfreq;
		break;

	case CHMODE_B:
		lastlo_freqB=LOfreq;
		break;

	default:
		lastlo_freqA=LOfreq;
		break;
	}

	lo_freq=LOfreq;
	lo_changed=true;			// this is a indicator for tunechanged(), that no action should be taken adjusting LO boundaries.

	update_lo=true;

	if (HWType == 3)
	{
/*
	Since libusb seems not to like being around multiple threads, data task is actually polling the lo_freq variable for changes and 
	if there is a change, it will send a control message to the radio
*/
	}
	else
	{
		if ((LOfreq <= 32000000L)&&(serinit_ok))
		{
			sprintf_s(freqstring, 32, "_fa %ld\n\r", LOfreq);
			//_cprintf("LO=%s", freqstring);
			serial.Write(freqstring, strlen(freqstring));
			return 0;
		}
		else
			return 0; // well - actually we are supposed to return max possible LO freq, but that does not fit into int?!
	}

	return 0;
}

/*
This entry point is meant to query the external hardware's set frequency via the DLL. It is used by Winrad to 
handle a asynchronous status of 101 (see below the callback device), but not checked at startup for its presence.

The return value is the current LO frequency, expressed in units of Hz.
*/
extern "C" long __stdcall GetHWLO(void)
{
	return lo_freq;	//LOfreq;
}

/*
This entry point is used to ask the external DLL which is the current value of the sampling rate. 
If the sampling rate is changed either by means of a hardware action or because the user specified a new 
sampling rate in the GUI of the DLL, Winrad must be informed by using the callback device (described below).

The return value is the value of the current sampling rate expressed in units of Hz.
*/

// this function gets called only if ExtIO DLL is I/Q data provider
extern "C" long __stdcall GetHWSR(void)
{
	_cprintf("GetHWSR() called\n");
	return IQSampleRate;
}

/*
This entry point is meant to query the DLL about the Tune value that Winrad should set. It is used by Winrad
to handle a asynchronous status of 105 (see below the callback device), but not checked at startup for its presence.

The return value is the desired Tune frequency, expressed in units of Hz.
*/

extern "C" long __stdcall GetTune(void)
{
	_cprintf("GetTune() called: returning %ld\n", tune_freq);
	return tune_freq;
}


/*
This entry point is meant to query the DLL about the new passband filter and the CW pitch to set.

Parameters :

	loCut	-	a reference to an int that will receive the new low cut frequency, in Hertz
	hiCut	-	a reference to an int that will receive the new high cut frequency, in Hertz
	pitch	-	a reference to an int that will receive the new CW pitch frequency, in Hertz
*/

extern "C" void __stdcall GetFilters(int& loCut, int& hiCut, int& pitch)
{

}

/*
This entry point is meant to query the DLL about the demodulation mode that Winrad should use. 
It is used by Winrad to handle a asynchronous status of 106 (see below the callback device), 
but not checked at startup for its presence.

The return value is a single character indicating the desired demodulation mode, according to the following table:

	‘A’ = AM ‘E’ = ECSS ‘F’ = FM ‘L’ = LSB ‘U’ = USB ‘C’ = CW ‘D’ = DRM
*/

extern "C" char __stdcall GetMode(void)
{
	return modulation;
}

/*
This entry point is meant to inform the DLL about the demodulation mode being currently set by Winrad.
It is invoked by Winrad each time the demodulation mode is changed, or initially set when the program starts. 
It is not checked at startup for its presence.

Parameter:

	mode	-	a single character indicating the current demodulation mode. For the meaning of the character, see the API GetMode()
*/

/*
Somehow this does not export correctly!
extern "C" void __stdcall ModeChanged(char mode)
{

}
*/

/*
This entry point is meant to allow the DLL to return a status information to Winrad, upon request. 
Presently it is never called by Winrad, though its existence is checked when the DLL is loaded. 
So it must implemented, even if in a dummy way. It is meant for future expansions, for complex HW 
that implement e.g. a preselector or some other controls other than a simple LO frequency selection.

The return value is an integer that is application dependent.
*/

extern "C" int __stdcall GetStatus(void)
{
	return 0;
}

/*
This entry point is used by Winrad to tell the DLL that the user did ask to see the GUI of the DLL itself, 
if it has one. The implementation of this call is optional

It has no return value.
*/

extern "C" void __stdcall ShowGUI(void)
{
	// not sure if this can ever happen, but in case the ShowGUI() is called twice without closing the window inbetween,
	// just re-activate the window rather than creating a new one.

	if(m_pmodeless)
	{
		m_pmodeless->ShowWindow(SW_RESTORE);
		m_pmodeless->SetForegroundWindow();	
	}
	else
	{
		m_pmodeless = new CExtIODialog;

		if (m_pmodeless)
		{
			m_pmodeless->Create(/*CGenericMFCDlg*/CExtIODialog::IDD, CWnd::GetActiveWindow() /*GetDesktopWindow()*/);
			m_pmodeless->ShowWindow(SW_SHOW);
		}
		else
		{
			AfxMessageBox("Unable to Create ExtIO DLL User Interface!", MB_OK | MB_ICONEXCLAMATION);
		}
	}

	return;
}

/*
This entry point is used by Winrad to tell the DLL that it has to hide its GUI, if it has one. 
The implementation of this call is optional

It has no return value.
*/

extern "C" void __stdcall HideGUI(void)
{
	//	......	If the DLL has a GUI, now you have to hide it

	// Noone seems to call this function, but it seems that it really _should_ be called during the exit.
	// Lets just implement GUI cleanup here for future compatibility when it starts to be called!

	if (m_pmodeless)
	{
		m_pmodeless->DestroyWindow();
	}

	return;
}

/*
This entry point is used by Winrad to communicate to the DLL that the user, through the Winrad GUI, 
has changed the span of frequencies visible in the Winrad spectrum/waterfall window. The information 
can be used by the DLL to change the LO value, so to implement a continuous tuning. For example, 
when the frequency tuned by the user is approaching the lower or higher limit (and this can be known 
via the TuneChanged API), the DLL could decide to alter the LO value so to bring the tuned frequency 
in the center of the window. Of course, Winrad must be informed of the change via the callback device, 
with status code 104. This will ensure that the tuned frequency value will not change.

Parameters:

	low		-	a long integer specifying the lower limit, expressed in Hz, of the visible spectrum/waterfall window
	high	-	a long integer specifying the upper limit, expressed in Hz, of the visible spectrum/waterfall window

It has no return value.
*/

extern "C" void __stdcall IFLimitsChanged(long low, long high)
{
	// Do whatever you want with the information about the new limits of the spectrum/waterfall window
	_cprintf("IFLimitsChanged() called: low=%ld high=%ld\n", low, high);
	iflimit_high=high;
	iflimit_low=low;

	return;
}

/*
This entry point is used by Winrad to communicate to the DLL that a change of the tuned frequency (done by any means) 
has taken place. This change can be used by a DLL that controls also a TX to know where to set the frequency of 
the transmitter part of the hardware. The implementation of this call is optional.

Parameters :

	freq	-	a long integer specifying the new frequency where Winrad is tuned to, expressed in Hz.

It has no return value.
*/

extern "C" void __stdcall TuneChanged(long freq)
{
unsigned long increment;

	
	// store channel-specific
	switch(ChannelMode)
	{
	case CHMODE_A:
		lasttune_freqA=freq;
		break;

	case CHMODE_B:
		lasttune_freqB=freq;
		break;

	default:
		lasttune_freqA=freq;
		break;
	}

	// should not get redundant, but still seems to help endless scrolling
	if (do_callback105)
		return;

	// if SetHWLO() was called, then we should not mess around with tuning boundaries
	if (lo_changed == true)
	{
		tune_freq=freq;
		lo_changed=false;
		return;
	}

	//first time the tune frequency is touched, so init
	if (!tune_freq)		// we init tune_freq to 0
		tune_freq=freq;

	// check if we are getting closer to edge than last tuning increment and if so, change the LO frequency by same value and do calback to winrad
	increment=abs(freq-tune_freq);

	_cprintf("TuneChanged() called: freq=%ld increment:%ld\n", freq, increment);
	
	if ((unsigned long)freq < tune_freq)
	{
		if ((unsigned long)freq < (iflimit_low+(increment)))
		{
			if (lo_freq > (increment))
				lo_freq-=(increment);

			tune_freq=freq;
			_cprintf("callback!\n");
			(*ExtIOCallback)(-1, 101, 0, NULL);		// indicate (and perform on radio) LO frequency change	
			//(*ExtIOCallback)(-1, 105, 0, NULL);		// change tune back to where it was (this will not execute TuneChanged() agin)
			do_callback105=true;		// if we execute it here, the gui will not be updated, so ask datatask to do that
		}

	} 
	else if ((unsigned long)freq > tune_freq)
	{
		if ((unsigned long)freq > (iflimit_high-(increment)))
		{	
			lo_freq+=(increment);
			tune_freq=freq;
			_cprintf("callback!\n");
			(*ExtIOCallback)(-1, 101, 0, NULL);		// indicate (and perform on radio) LO frequency change	
			//(*ExtIOCallback)(-1, 105, 0, NULL);		// change tune back to where it was (this will not execute TuneChanged() agin)
			do_callback105=true;		// if we execute it here, the gui will not be updated, so ask datatask to do that
		}
	}
	
	tune_freq=freq;

	return;
}

/*
Parameters :

	loCut	-	an int that specifies the low cut frequency, in Hertz
	hiCut	-	an int that specifies the high cut frequency, in Hertz
	pitch	-	an int that specifies the CW pitch frequency, in Hertz
	mute	-	a Boolean that indicates whether the audio is muted

It has no return value.
*/

extern "C" void __stdcall FiltersChanged(int loCut, int hiCut, int pitch, bool mute)
{

}

/*
This entry point is used by Winrad to communicate to the DLL the function address that it should invoke when a new buffer 
of audio data is ready, or when an asynchronous event must be communicated by the DLL. Of course the new buffer of audio 
data is only sent by DLLs that control HW that have their own internal digitizers and do not depend on the soundcard for 
input. In this case it’s up to the DLL to decide which I/O port is used to read from the HW the digitized audio data stream. 
One example is the USB port. If you don’t foresee the need of an asynchronous communication started from the DLL, simply do 
a return when Winrad calls this entry point.

The callback function in Winrad that the DLL is expected to call, is defined as follows :

	void extIOCallback(int cnt, int status, float IQoffs, void* IQdata)

Parameters :

	cnt		-	is the number of samples returned. As the data is complex (I/Q pairs), then there are two 16 bit values per sample. 
				If negative, then the callback was called just to indicate a status change, no data returned. Presently Winrad does 
				not use this value, but rather the return value of the StartHW() API, to allocate the buffers and process the audio 
				data returned by the DLL. The cnt value is checked only for negative value, meaning a status change.
	status	-	is a status indicator (see the call GetStatus). When the DLL detects a HW change, e.g. a power On or a power Off, 
				it calls the callback function with a cnt parameter negative, indicating that no data is returned, but that the call 
				is meant just to indicate a status change.
				Currently the status parameter has just two implemented values (apart from those used by the SDR-14/SDR-IQ hardware):

				100	-	This status value indicates that a sampling frequency change has taken place, either by a hardware action, 
						or by an interaction of the user with the DLL GUI.. When Winrad receives this status, it calls immediately 
						after the GetHWSR() API to know the new sampling rate.
				101	-	This status value indicates that a change of the LO frequency has taken place, either by a hardware action, 
						or by an interaction of the user with the DLL GUI.. When Winrad receives this status, it calls immediately 
						after the GetHWLO() API to know the new LO frequency.
				102	-	This status value indicates that the DLL has temporarily blocked any change to the LO frequency. This may 
						happen, e.g., when the DLL has started recording on a WAV file the incoming raw data. As the center frequency 
						has been written into the WAV file header, changing it during the recording would be an error.
				103	-	This status value indicates that changes to the LO frequency are again accepted by the DLL
				104 -	*************** 104 CURRENTLY NOT YET IMPLEMENTED ***************************
						This status value indicates that a change of the LO frequency has taken place, and that Winrad should act 
						so to keep the Tune frequency unchanged. When Winrad receives this status, it calls immediately after the 
						GetHWLO() API to know the new LO frequency
				105	-	This status value indicates that a change of the Tune frequency has taken place, either by a hardware action, 
						or by an interaction of the user with the DLL GUI.. When Winrad receives this status, it calls immediately 
						after the GetTune() API to know the new Tune frequency. The TuneChanged() API is not called when setting the 
						new Tune frequency
				106	-	This status value indicates that a change of the demodulation mode has taken place, either by a hardware action, 
						or by an interaction of the user with the DLL GUI.. When Winrad receives this status, it calls immediately after 
						the GetMode() API to know the new demodulation mode. The ModeChanged() API is not called when setting the new mode.
				107	-	This status value indicates that the DLL is asking Winrad to behave as if the user had pressed the Start button. 
						If Winrad is already started, this is equivalent of a no-op.
				108	-	This status value indicates that the DLL is asking Winrad to behave as if the user had pressed the Stop button. 
						If Winrad is already stopped, this is equivalent of a no-op.
				109	-	This status value indicates that the DLL is asking Winrad to change the passband limits and/or the CW pitch. 
						When Winrad receives this status, it calls immediately the GetFilters API .
					----- Legacy Winrad callbacks end here -----
				110	-	This status value indicates that the DLL is asking Winrad to enable audio output on the Mercury DAC when using the HPSDR
				111	-	This status value indicates that the DLL is asking Winrad to disable audio output on the Mercury DAC when using the HPSDR
				112	-	This status value indicates that the DLL is asking Winrad to enable audio output on the PC sound card when using the HPSDR
				113	-	This status value indicates that the DLL is asking Winrad to disable audio output on the PC sound card when using the HPSDR
				114	-	This status value indicates that the DLL is asking Winrad to mute the audio output
				115	-	This status value indicates that the DLL is asking Winrad to unmute the audio output
						Upon request from the DLL writer, the status flag could be managed also for other kinds of external hardware events.
	IQoffs	-	If the external HW has the capability of determining and providing an offset value which would cancel or minimize the DC offsets 
				of the two outputs, then the DLL should set this parameter to the specified value. Otherwise set it to zero.
	IQdata	-	This is a pointer to an array of samples where the DLL is expected to place the digitized audio data in interleaved format 
				(I-Q-I-Q-I-Q etc.)	in little endian ordering. The number of bytes returned must be equal to IQpairs * 2 * N, where IQpairs is the 
				return value of the StartHW() API, and N is the sizeof() of the type of data returned, as specified by the ‘type’ parameter of the InitHW() API.

				Note, that there is no throtling mechanism to feed Winrad with data, so extio dll must somehow maintain the appropriate data rate by its own. 
				The software will internally allocate internal fifo ring buffer which is 512*IQpairs*2*sizeof(datatype) long at startup, where IQpairs is the value 
				returned by StartHW(). The allocated buffer is a ring buffer where data is added with each call. The callback is not blocking any way, so if 
				the ring buffer gets full, it will just wrap over.

				It is also important to note, that although Winrad claims using internally a 32-bit values for samples, the fifo for 24-bit samples is actually 
				allocated as datatype would be 3 bytes long, so the fifo length is 512*IQpairs*2*3. Likewise, the I/Q data should consist of 3-byte values
*/

extern "C" void __stdcall SetCallback(void (* Callback)(int, int, float, void *))
{	
	_cprintf("SetCallback() called\n");
	
	ExtIOCallback=Callback;

	// right after callback, go sync frequency (InitHW() has fetched variables from registry)

	(*ExtIOCallback)(-1, 101, 0, NULL);			// sync lo frequency on display
	(*ExtIOCallback)(-1, 105, 0, NULL);			// sync tune frequency on display

	return;	
}

/*
This entry point is used by Winrad to communicate to the DLL the raw audio data just acquired via whatever method (sound card, WAV file, external HW). 
This can be used by the DLL either to plot the data or to pre-process them, before Winrad has any chance to act on them. Beware that any processing 
done by this call adds to the buffer processing time of Winrad, and, if too long, could cause audio buffer overflow with audio glitches and interruptions. 
Keep the processing done inside this call to a minimum.

Parameters :

	samprate	-	is the current value of the sampling rate, expressed in Hz.
	Ldata		-	Is the pointer to a buffer of 32 bit integers, whose 24 low order bits contain the digitized values for the left channel. 
					The 8 high order bits are set to zero. The number of samples in the buffer is given by the fourth parameter.
	Rdata		-	Is the pointer to a buffer of 32 bit integers, whose 24 low order bits contain the digitized values for the right channel. 
					The 8 high order bits are set to zero. The number of samples in the buffer is given by the fourth parameter.
	numsamples	-	Is an integer that indicates how many samples are in each of the left and right buffers.

It has no return value.
*/

extern "C" void __stdcall RawDataReady(long samprate, int *Ldata, int *Rdata, int numsamples)
{
	// data statistics and quality analysis can be done here, as well as some crude AGC if needed.
	// not implemented as of now.

	return;
}


//
// Currently there is no proper place to call this function, as there is no extit cleanup defined in Winrad API.
// The DLL just gets terminated at the Winrad/HDSDR exit together with all its resources.
// However, to keep a picture of what could theoretically be cleaned up, this function is maintained.
//

void ExtIODllCleanup(void)
{
HANDLE sleepevent = CreateEvent(NULL, FALSE, FALSE, NULL);		// we are using that instead of sleep(), as it is more kind to overall system resources
int i;

	globalshutdown=true;	// ask all threads to exit

	for (i=0; i<50; i++)	// wait up to 5sec fr threads to terminate
	{
		if (!threadcount)
			break;
		WaitForSingleObject(sleepevent, 100);		// Sleep ..
	}

	if (threadcount)
		_cprintf("%d threads failed to terminate properly\n", threadcount);

	DeleteCriticalSection(&CriticalSection);
}



