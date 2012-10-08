#include "serial\serial.h"

#if !defined(AFX_EXTIOCLASS_H__247C1094_0293_40d5_846A_6CC900C82E80__INCLUDED_)
#define AFX_EXTIOCLASS_H__247C1094_0293_40d5_846A_6CC900C82E80__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

	extern "C" __declspec(dllexport) bool __stdcall InitHW(char *name, char *model, int& type);
	extern "C" __declspec(dllexport) bool __stdcall OpenHW(void);
	extern "C" __declspec(dllexport) int __stdcall StartHW(long freq);
	extern "C" __declspec(dllexport) void __stdcall StopHW(void);
	extern "C" __declspec(dllexport) void __stdcall CloseHW(void);
	extern "C" __declspec(dllexport) int __stdcall SetHWLO(long LOfreq);
	extern "C" __declspec(dllexport) long __stdcall GetHWLO(void);
	extern "C" __declspec(dllexport) long __stdcall GetHWSR(void);
	extern "C" __declspec(dllexport) long __stdcall GetTune(void);
	extern "C" __declspec(dllexport) void __stdcall GetFilters(int& loCut, int& hiCut, int& pitch);
	extern "C" __declspec(dllexport) char __stdcall GetMode(void);
	//extern "C" __declspec(dllexport) void __stdcall ModeChanged(char mode);
	extern "C" __declspec(dllexport) int __stdcall GetStatus(void);
	extern "C" __declspec(dllexport) void __stdcall ShowGUI(void);
	extern "C" __declspec(dllexport) void __stdcall HideGUI(void);
	extern "C" __declspec(dllexport) void __stdcall IFLimitsChanged(long low, long high);
	extern "C" __declspec(dllexport) void __stdcall TuneChanged(long freq);
	extern "C" __declspec(dllexport) void __stdcall FiltersChanged(int loCut, int hiCut, int pitch, bool mute);
	extern "C" __declspec(dllexport) void __stdcall SetCallback(void (* Callback)(int, int, float, void *));
	extern "C" __declspec(dllexport) void __stdcall RawDataReady(long samprate, int *Ldata, int *Rdata, int numsamples);

extern void (* ExtIOCallback)(int, int, float, void *);

#define DLLVER_MAJOR	1
#define DLLVER_MINOR	24

#define MIN_FW_MAJOR	1
#define MIN_FW_MINOR	70

#define LIB_MIN_MAJOR	1
#define LIB_MIN_MINOR	2
#define LIB_MIN_MICRO	6
#define LIB_MIN_NANO	0

#define	DEFAULTTRANSPARENCY	90

extern volatile bool do_datatask;
extern volatile bool datatask_done;
extern volatile bool datatask_running;

extern volatile bool globalshutdown;
extern volatile int threadcount;
extern CRITICAL_SECTION CriticalSection; 

extern CSerial serial;
extern volatile bool serinit_ok;
extern volatile bool libusb_ok;
extern int HWType;

#define IQSAMPLERATE_FULL		231884 //234432	//192000
#define IQSAMPLERATE_DIVERSITY	187683
#define IQSAMPLERATE_AUDIO		48000

#define SDRIQDATASIZE	2048	// this is minimum for 16 bit, as at least 512 IQ pqirs have to be transmitted once
#define BYTESPERSAMPLE	2		//16-bit data == 2x char
#define IQPAIRS	(SDRIQDATASIZE/BYTESPERSAMPLE/2)	// returned bytes/bytes-per-sample/2_values_per_complex_pair(I+Q) = number of IQ pairs returned

// in cache, larger margin is safer, but the tuning will lag behind by the same amount of data ..
#define DATATASK2CACHE		20		// how many SDRIQDATASIZE buffers to be cached by datatask
#define	WINRAD2CACHE		10		// how many SDRIQDATASIZE buffers to be cached into Winrad/HDSDR before play

// Device vendor and product id.
#define SDR_VID 0x03EB
#define SDR_PID 0x204E

// Device configuration and interface id.
#define SDR_CONFIG		1
#define SDR_BULKIF		6

// Device endpoint(s)
#define SDREP_IN		0x84
#define SDREP_OUT		0x07		// fake, non-existant endpoint

// USB endpoint control commands

#define		LIBUSB_MODE				14
//#define		LIBUSB_MODE				15
#define		LIBUSB_READREG			16
#define		LIBUSB_WRITEREG			17
#define		LIBUSB_SETFREQ			18
#define		LIBUSB_GETFREQ			19
#define		LIBUSB_SAMPLEMODE		20
#define		LIBUSB_GETVER			21
#define		LIBUSB_SETGAIN			22
#define		LIBUSB_SETPHASE			23

#define		LIBUSB_CHA			0
#define		LIBUSB_CHB			1

// LIBUSB_MODE flags

#define		LIBMODE_16A			0
#define		LIBMODE_16B			1
#define		LIBMODE_16APB		2
#define		LIBMODE_16AMB		3
#define		LIBMODE_16BMA		4
#define		LIBMODE_16AB		5

#define		LIBMODE_OFF			10
#define		LIBMODE_SPEEDTEST	11
#define		LIBMODE_TXTEST		12

#define		LIBMODE_24A			20
#define		LIBMODE_24B			21

#define		LIBMODE_24AB		25

// Channel mode radio button states

#define		CHMODE_A		0
#define		CHMODE_B		1
#define		CHMODE_APB		2		// A+B
#define		CHMODE_AMB		3		// A-B
#define		CHMODE_BMA		4		// B-A

#endif // !defined(AFX_EXTIOCLASS_H__247C1094_0293_40d5_846A_6CC900C82E80__INCLUDED_)
