// PanadapterDialog.cpp : implementation file
//

#include "stdafx.h"

#include "PanadapterDialog.h"
#include "ExtIOFunctions.h"

#include <math.h>
#include <process.h>
#include <conio.h>
#include "Fourier.h"

// CPanadapterDialog dialog

extern int Transparency;
extern unsigned int pandata_wptr;
extern unsigned char PANData[];

extern unsigned long iflimit_high;	//ExtIOFunctions.cpp
extern unsigned long iflimit_low;
extern unsigned long lo_freq;
extern unsigned long tune_freq;
extern volatile bool update_lo;
extern volatile bool do_callback105;
extern volatile bool do_callback101;
volatile bool do_pantableupdate=false;

extern bool pancaching;
extern HANDLE sleepevent;

int scrolltimer=50;		//NB! this value is an actual timer value, NOT the slider value (we have slider reversed!)

PANENTRY panentry;
int fftbands=0;
int fftbandsneeded=0;
int skipzone=0;

int fftbands_new=8;
int fftbandsneeded_new=4;
int skipzone_new=8;

long long _lastrangemin=32000000;		// cause immdiate update on pass 1
long long _lastrangemax=0;

long long bytesprocessed=0;
long long lastmagicfound=0;

double dbrangemin=0, dbrangemax=1100;

CBitmap *pOldBitmap;

IMPLEMENT_DYNAMIC(CPanadapterDialog, CDialog)

inline double GetFrequencyIntensity(double re, double im)
{
	return sqrt((re*re)+(im*im));
}

#define mag_sqrd(re,im) (re*re+im*im)
#define Decibels(re,im) ((re == 0 && im == 0) ? (0) : 10.0 * log10(double(mag_sqrd(re,im))))
#define Amplitude(re,im,len) (GetFrequencyIntensity(re,im)/(len))
#define AmplitudeScaled(re,im,len,scale) ((int)Amplitude(re,im,len)%scale)

CPanadapterDialog::CPanadapterDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CPanadapterDialog::IDD, pParent)
{
}

CPanadapterDialog::~CPanadapterDialog()
{
}

void CPanadapterDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SLIDER1, m_SpeedSlider);
	DDX_Control(pDX, IDC_EDIT1, m_RangeInfo);
	DDX_Control(pDX, IDC_EDIT2, m_ActiveInfo);
	DDX_Control(pDX, IDC_CUSTOM1, c_FreqRangeSlider);
	DDX_Control(pDX, IDC_CUSTOM2, c_ColorRangeSlider);
}


BEGIN_MESSAGE_MAP(CPanadapterDialog, CDialog)
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_TIMER()			// we need timer to do periodic updates on FFT window. 
	//ON_WM_PAINT()
	ON_WM_LBUTTONDOWN()
	ON_WM_ERASEBKGND()
	ON_REGISTERED_MESSAGE(RANGE_CHANGED, OnRangeChange)
	ON_WM_CLOSE()
END_MESSAGE_MAP()

UINT PanadapterTask(LPVOID dummy);

CDC *MemDC, *pDC;
CRect rcWaterFall;	
CBitmap *MemBitmap;

volatile bool run_panadaptertask, panadaptertask_exited=false;

COLORREF Intensity(float _level)
{
int level;

	level=_level;

	if (level<0)
		level=0;
	if (level>1279)
		level=1279;

	if (level<256)
		return RGB(50,0,level);					//R=0, G=0, B=0..255
	else if ((level>=256)&&(level<512))
		return RGB(50, level-256, 255);			//R=0, G=0..255, B=255
	else if ((level>=512)&&(level<768))
		return RGB(0, 255, 255-(level-512));	//R=0, G=512, B=255..0
	else if ((level>=768)&&(level<1024))
		return RGB(level-768, 255, 0);			//R=0..255, G=255, B=0
	else if (level>=1024)
		return RGB(255, 255-(level-1024), 0);	//R=255, G=255..0, B=0
	else
		return RGB(255, 255, 255);				// we should never get here
}

// CPanadapterDialog message handlers

BOOL CPanadapterDialog::OnInitDialog()
{
int i, j;
	
	CDialog::OnInitDialog();

	hParent=GetModuleHandle(NULL);

	m_Layered.AddLayeredStyle(m_hWnd);
	m_Layered.SetTransparentPercentage(m_hWnd, Transparency);		// transparency is already set to its default by CExtIODialog::OnInitDiaog() so no need to care for defaults here

	// Set Minimum and Maximum.
	c_FreqRangeSlider.SetMinMax(0, 32000000);
    // Set Left and Right Arrows
	c_FreqRangeSlider.SetRange(0, 32000000);
	c_FreqRangeSlider.SetVisualMode(TRUE);
    // Set "Visual" range.
	c_FreqRangeSlider.SetVisualMinMax(10000000, 20000000);	// just something

	c_ColorRangeSlider.SetVerticalMode(TRUE);
	// Set Minimum and Maximum.
	c_ColorRangeSlider.SetMinMax(0, 1100);
    // Set Left and Right Arrows
	c_ColorRangeSlider.SetRange(0, 1110);
	c_ColorRangeSlider.SetVisualMode(TRUE);
	c_ColorRangeSlider.SetInvertedMode(TRUE);
    // Set "Visual" range.
	c_ColorRangeSlider.SetVisualMinMax(0, 0);

	m_SpeedSlider.SetRange(1, 120);
	//reversing the slider control is real pain, so we are interpreting the value bacwkards.
	//Note, that we are writing the actual timer value to registry, not the slider value!
	m_SpeedSlider.SetPos(m_SpeedSlider.GetRangeMax()+m_SpeedSlider.GetRangeMin()-scrolltimer);

	m_SpeedSlider.SetTicFreq(10);
	
	// build waterfall bitblt structures now

	GetDlgItem(IDC_WATERFALL)->GetClientRect(rcWaterFall);
	
	pDC = GetDlgItem(IDC_WATERFALL)->GetDC();

	MemDC = new CDC;
	MemDC->CreateCompatibleDC(pDC);
	MemBitmap = new CBitmap;
	MemBitmap->CreateCompatibleBitmap(pDC,rcWaterFall.right,rcWaterFall.bottom);

	// build empty waterfall
	pOldBitmap = (CBitmap*) MemDC->SelectObject(MemBitmap);

	for (i=0; i<rcWaterFall.bottom; i++)
	{
		for (j=0; j<rcWaterFall.right; j++)
		{
			MemDC->SetPixelV(j, i, RGB(0, 0, rand()%255));
		}
	}

	MemDC->SelectObject(pOldBitmap);

	panentry.magic_I=0x55A0;
	panentry.magic_Q=0xF0F0;
	panentry.samples=32;
	panentry.startfreq=0;
	panentry.stepfreq=64000;
	panentry.steps=500;

	do_pantableupdate=true;
	
	run_panadaptertask=true;
	panadaptertask_exited=false;

	AfxBeginThread((AFX_THREADPROC)PanadapterTask, NULL/*, THREAD_PRIORITY_IDLE*/);
	// set timer only after DC stuff is initialized
	SetTimer(ID_CLOCK_TIMER, scrolltimer, NULL);		//50ms timer, null show put it to task queue

	return true;
}


/*
Product detector of the FFT data. fftvars contain values form FMOD FFT pool, returns values between 0 and maxval to fftvars
*/

void filterfft(float* fftpool, int _fftbands)
{
int i;
float knee=1.0f; //1.5f;			//curvature of the graph. 1=perfect square, 100=flat, <1="over the square"
float expander=70.0f;		//75=good but too dark sometimes; 100=no expansion, <100=expansion upwards
float shift=10.0f;			// just brighten the overall picture by x dB

double multiplier;

	multiplier=110/((dbrangemax-dbrangemin)/10);

	for (i=0; i<_fftbands; i++)
	{
		fftpool[i]-=(dbrangemin/10);
		if (fftpool[i] < 0)
			fftpool[i]=0;		

		fftpool[i]*=multiplier;
	}
}


#define PAN_MAGIC	0
#define PAN_FREQ	1
#define PAN_DATA	2

unsigned int pandata_rptr=0;
int magicsfound=0;
unsigned char magic[12]={0xA0+0, 0x55, 0xF0+1, 0xF0, 0xA0+2, 0x55, 0xF0+3, 0xF0, 0xA0+4, 0x55, 0xF0+5, 0xF0};
int panstate=PAN_MAGIC;
int minfreq, maxfreq;
long long _freqrangemin=0, _freqrangemax=32000000, _freqspan=32000000;
float dbminmaxpool[0xFFF+1];


UINT PanadapterTask(LPVOID dummy)
{
int i=0, j=0, k;
unsigned int l_pandata_wptr;
char freqbuff[8];		// four last bytes are used for checking the trailer magic (supposed to be AA 55 F0 F0)

//HANDLE hSnapShot;

unsigned int pandatalen=0;
char* pandata;
double* pandataI;
double* pandataQ;
double* fftpoolI;
double* fftpoolQ;
float *fdraw;
long long freqpos;
double fftincrement, fdrawoffset, fftstart, fftend;
int* fftmap;
int* fftmap16;
double dummydbl;
double* window;
unsigned int iterations=0;

double avgval=0;
int avgcnt=0;

bool magicgone=false;

float re, im;

unsigned int dbpooloffset=0;

	//hSnapShot = lpfCreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0 );

	for (k=0; k<(0xFFF+1); k++)
	{
		dbminmaxpool[k]=0.0f;
	}

	fftpoolI=(double*)malloc(fftbands*sizeof(double));
	fftpoolQ=(double*)malloc(fftbands*sizeof(double));
	pandataI=(double*)malloc(fftbands*sizeof(double));
	pandataQ=(double*)malloc(fftbands*sizeof(double));
	window=(double*)malloc(fftbands*sizeof(double));
	fdraw=(float*)malloc(fftbands*sizeof(float));
	fftmap=(int*)malloc(fftbands*sizeof(int));
	fftmap16=(int*)malloc(fftbands*sizeof(int));

	pandata=(char*)malloc(PANDATAMASK+1);
	
	maxfreq=0;
	minfreq=32000000;

	MemDC->SelectObject(MemBitmap);

	panadaptertask_exited=false;

	while(run_panadaptertask)
	{
		WaitForSingleObject(sleepevent, 1);		//give away timeslice

		if ((bytesprocessed-lastmagicfound)>1024)
		{
			magicgone=true;
			_cprintf("Magic loss!\n");
		}

		if (fftbands_new > fftbands)
		{
			fftpoolI=(double*)realloc(fftpoolI, fftbands_new*sizeof(double));
			fftpoolQ=(double*)realloc(fftpoolQ, fftbands_new*sizeof(double));
			pandataI=(double*)realloc(pandataI, fftbands_new*sizeof(double));
			pandataQ=(double*)realloc(pandataQ, fftbands_new*sizeof(double));
			window=(double*)realloc(window, fftbands_new*sizeof(double));
			fdraw=(float*)realloc(fdraw, fftbands_new*sizeof(float));
			fftmap=(int*)realloc(fftmap, fftbands_new*sizeof(int));			// does not really have to be that large, but let it be ..
			fftmap16=(int*)realloc(fftmap16, fftbands_new*sizeof(int));		// does not really have to be that large, but let it be ..
		}

		if ((fftbands!=fftbands_new)||(fftbandsneeded!=fftbandsneeded_new)||(skipzone != skipzone_new))
		{
			// update with new values from OnTimer(), since we may have changed the range
			fftbands=fftbands_new;
			fftbandsneeded=fftbandsneeded_new;
			skipzone=skipzone_new;

			//we cant have floating point mathematics at the amount needed, so we will pre-calculate offsets inside 
			//fdraw[] structure for both, the 'fftbandsneeded' number of points and 16kHz first column
			
			fftstart=((fftbands)*16000);
			fftstart/=96000;					// fftstart is now the starting address for the 16kHz inside 96kHz fft data area
			fftend=((fftbands)*(16000+64000));
			fftend/=96000;

			fftincrement=(fftend-fftstart);		// how many points for the FFT range
			fftincrement/=fftbandsneeded;	

			for (k=0; k<fftbandsneeded; k++)
			{
				fdrawoffset=fftincrement;
				fdrawoffset*=k;
				fdrawoffset+=fftstart;

				fftmap[k]=(modf(fdrawoffset, &dummydbl)>=0.5)?ceil(fdrawoffset):floor(fdrawoffset);	//just a round() ...				
			}

			// and now for the first 16kHz
			fftstart=0;									// fftstart is now the starting address for the 0kHz inside 96kHz fft data area
			fftend=((fftbands)*16000);
			fftend/=96000;

			fftincrement=(fftend-fftstart);				// how many points for the FFT range
			fftincrement/=(fftbandsneeded/4);			//fftbandsneeded represents the 64kHz plot, so 16kHz is a 1/4 of that

			for (k=0; k<((fftbandsneeded/4)+1); k++)	//+1 for safety
			{
				fdrawoffset=fftincrement;
				fdrawoffset*=k;
				fdrawoffset+=fftstart;

				fftmap16[k]=(modf(fdrawoffset, &dummydbl)>=0.5)?ceil(fdrawoffset):floor(fdrawoffset);	//just a round() ...				
			}

			//Build FFT window. 
			for (k=0; k<fftbands; k++)
			{
				//Rectangular
				window[k]=1.0;

				//Blackman-Harris (http://www.katjaas.nl/FFTwindow/FFTwindow.html)
				//window[k]=0.35875 + (0.48829*cos(2*PI*k/fftbands)) + (0.14128*cos(4*PI*k/fftbands)) + (0.01168*cos(6*PI*k/fftbands));

				//Blackman (http://www.katjaas.nl/FFTwindow/FFTwindow.html)
				//window[k]=0.42 - (0.5*cos(2*pi*k/fftbands)) - (0.08*cos(4*pi*k/fftbands));

			}
		}

		l_pandata_wptr=pandata_wptr;		

		while (((pandata_rptr&PANDATAMASK)!=(l_pandata_wptr&PANDATAMASK))&&(run_panadaptertask))
		{			
			switch(panstate)
			{
			case PAN_MAGIC:
			case PAN_DATA:
				while (((pandata_rptr&PANDATAMASK)!=(l_pandata_wptr&PANDATAMASK))&&(run_panadaptertask))
				{			
					for (; i<8; i++)
					{
						bytesprocessed++;

						if (PANData[(pandata_rptr+i)&PANDATAMASK] == magic[i])
						{
							if (((pandata_rptr+i)&PANDATAMASK)==(l_pandata_wptr&PANDATAMASK))
								break;		// buffer end, so break, but do not reset scan pointer
							else
							{
								if (magicgone)
								{
									_cprintf("Magic reappeared after %ld\n", (bytesprocessed-lastmagicfound));
									magicgone=false;
								}
								lastmagicfound=bytesprocessed;		//internal debug only
								continue;	// magic matches and there is still data, so continue scan
							}
						}
						else
						{
							i=0;	// magic fail, so start over again
							break;
						}
					}

					if (i==8)				// all magic done?
					{											
						if ((panstate == PAN_DATA)&&(pandatalen >=(unsigned int)((fftbands*4)+(skipzone*4))))	// we need to have at least enough samples for FFT (16-bit I, 16-bit Q and 8-IQsample ignore zone what is captured while frequency was changed)
						{
							//new magic found, so we have to process the current pandata buffer and inject new FFT!
							//note, that (int)(*(int*)&freqbuff[0]) contains current starting frequency!
							
							// copy last IQ pairs to FFT source buffer
							for (i=0, j=skipzone*4; i<fftbands; i++)
							{								
								bytesprocessed++;

								// have to be copied with sign!
								pandataI[i]=(short)(short*)*(pandata+j);
								j+=2;
								pandataQ[i]=(short)(short*)*(pandata+j);
								j+=2;
							}
							
							//apply window now (we are using rectangular, so do not care)
							
							//for (i=0; i<fftbands; i++)
							//{
							//	pandataI[i]*=window[i];
							//	pandataQ[i]*=window[i];
							//}
							
							fft_double(fftbands, 0, pandataQ, pandataI, fftpoolQ, fftpoolI);

							// have to swap now first and a second half of the FFT results. pandata is used as buffer

							for (i=0; i<fftbands/2; i++)
							{
								pandataI[i]=fftpoolI[i];
								pandataQ[i]=fftpoolQ[i];
							}

							for (i=fftbands/2, k=0; i<fftbands; i++, k++)
							{
								fftpoolI[k]=fftpoolI[i];
								fftpoolQ[k]=fftpoolQ[i];
							}

							for (i=fftbands/2, k=0; i<fftbands; i++, k++)
							{
								fftpoolI[i]=pandataI[k];
								fftpoolQ[i]=pandataQ[k];
							}

							//now normalize the power according to actual samples used

							for (i=0; i<fftbands; i++)
							{
								fftpoolI[i]/=(fftbands/8);
								fftpoolQ[i]/=(fftbands/8);
							}

							for(i=0; i<fftbands; i++)
							{
								re = (float)fftpoolQ[i];
								im = (float)fftpoolI[i];
								
								fdraw[i] = Decibels(re, im);			//get Decibels in 0-110 range

								if (fdraw[i]) // first value in array is zero(?), so ignore
								{
									dbminmaxpool[dbpooloffset&0xFFF] = (float)fdraw[i];
									dbpooloffset++;
								}
							}

							// normalize data for graphic display now!
							filterfft(fdraw, fftbands);
							for (i=0; i<fftbands; i++)
								fdraw[i]=(fdraw[i]/110.0f)*1279.0f;		// 110db -> 1279

							// We do have 96kHz sampling rate, but our step frequency is 64kHz.
							// This is needed to have band overlap of 16kHz from both sides, so we will not have 
							// the edges of the scan area fading away because of DDC filtering. Therefore we will 
							// use frequency position 16kHz higher than indicated, as we are starting plotting from 
							// f+16kHz, except when plotting first column on the screen.

							(int)(*(int*)&freqbuff[0])-=(96000/2);	// we were bumping up the WFO frequency so the lower complex FFT boundary is actually our minimum frequency
							freqpos=(int)(*(int*)&freqbuff[0]);
							freqpos+=16000;

							//for safety, filter out the values what are out of bounds
							if (freqpos > _freqrangemax)
								freqpos=_freqrangemax;
							else if (freqpos < _freqrangemin)
								freqpos=_freqrangemin;

							freqpos-=_freqrangemin;		//panentry.startfreq;		// get frequency offset inside fft window
							freqpos*=(rcWaterFall.right-1);
							freqpos/=_freqspan;			//(panentry.steps*panentry.stepfreq)+64000;

							if (maxfreq<(int)(*(int*)&freqbuff[0]))
								maxfreq=(int)(*(int*)&freqbuff[0]);

							if (minfreq>(int)(*(int*)&freqbuff[0]))
								minfreq=(int)(*(int*)&freqbuff[0]);
							
							// Plot data. Freqpos is already corrected by 16kHz, now we have to find the appropriate 
							// FFT points to plot. the fdraw[] structure is likely having more points than we need, 
							// so what we do is, that we will cut off the 64kHz chunk of FFT data and then generate 
							// the required number of points from there.

							for (i=0, j=0, avgval=0, avgcnt=0; ((i<fftbands)&&(j<fftbandsneeded)); i++)
							{
								avgval+=fdraw[i];
								avgcnt++;

								if (i==fftmap[j])
								{
									avgval/=avgcnt;
									if ((freqpos+j)<=(rcWaterFall.right-1))
										MemDC->SetPixelV(freqpos+j, rcWaterFall.bottom-1, Intensity(avgval));
									j++;
									avgval=0;
									avgcnt=0;
								}
							}
							
							if ((int)(*(int*)&freqbuff[0]) == _freqrangemin)		// plot first 16kHz as well
							{
								for (i=0; i<((fftbandsneeded/4)+1); i++)		//+1 for safety, that we will not have blank pixels because of non-integer division
								{							
									if ((freqpos+i)<=(rcWaterFall.right-1))
										MemDC->SetPixelV(0+i, rcWaterFall.bottom-1, Intensity(fdraw[fftmap16[i]]));
								}
							}						
						}						

						i=0;				// reset i for new magic scan
						pandata_rptr+=8;	// had 8 bytes in for magic, so advance by 8				
						pandatalen=0;
						panstate=PAN_FREQ;	// show, that next state is going to be frequency
					
						break;				// break current while loop to enter PAN_FREQ state in state machine
					}					
					else if ((PANData[(pandata_rptr+i)&PANDATAMASK] == magic[i]) && (((pandata_rptr+i)&PANDATAMASK)==(l_pandata_wptr&PANDATAMASK)))
					{
						// if magic in progress, but we were just running out of buffer, go do other things and come back later
						break;
					}
					else					// no magic found, so continue scan, processing bytes if in data mode
					{
						if (panstate == PAN_DATA)
						{
							//copy panadapter data to the buffer
							if (pandatalen < (PANDATAMASK+1))
							{		
#pragma message(" ------- Why do we double-buffer here?")
								pandata[pandatalen]=PANData[pandata_rptr&PANDATAMASK];
								pandatalen++;
							}
						}
						pandata_rptr++;		//advance by one						
					}
				}
				break;

			case PAN_FREQ:
				while ((pandata_rptr&PANDATAMASK)!=(l_pandata_wptr&PANDATAMASK))
				{										
					freqbuff[i]=PANData[pandata_rptr&PANDATAMASK];
					i++;
					pandata_rptr++;

					bytesprocessed++;

					if (i==8)
					{
						i=0;

						if (!memcmp(freqbuff+4, magic+8, 4))
						{
							panstate=PAN_DATA;
							magicsfound++;		// increment successful tokens count
						}
						else
						{
							// trailer failure, so discard current header and start over
							panstate=PAN_MAGIC;
						}

						break;
					}
				}

				break;

			default:
				break;
			}
		}
	}

	free(pandataI);
	free(fftpoolI);
	free(pandataQ);
	free(fftpoolQ);
	free(pandata);
	free(fdraw);
	free(window);
	free(fftmap);
	free(fftmap16);

	panadaptertask_exited=true;

	return 1;
}

long lastclickX=0;
long lastclickY=0;

void CPanadapterDialog::OnTimer(UINT nIDEvent) 
{
char prntxt[256];
double freqrangemin, freqrangemax;

static int localmagic=0;
static long long  _selectedfreq=0;
static long long passesignored1=0, passesignored2=0;

CRect rect;
int i;
float fmin, fmax;
unsigned int steps, fftpoints, fftpointsP2, skip;


	if (nIDEvent == ID_CLOCK_TIMER)
	{
		KillTimer(ID_CLOCK_TIMER);		//50ms should be enough, but just to be sure to avoid recursion
		
		CBitmap *pxOldBitmap = (CBitmap*) MemDC->SelectObject(MemBitmap);
		pDC->BitBlt(0,0,rcWaterFall.right,rcWaterFall.bottom,MemDC,0,0,SRCCOPY);	// Copies the bitmap from the memory dc to the pdc using a fast bitblt function call.
		//bitblt internally by one line and add a new scanline at the bottom
		MemDC->BitBlt(0,0,rcWaterFall.right,rcWaterFall.bottom,MemDC,0,1,SRCCOPY);	// Copies the bitmap from the memory dc to the pdc using a fast bitblt function call.
		MemDC->SelectObject(pxOldBitmap);

		c_FreqRangeSlider.GetRange(freqrangemin, freqrangemax);

		// just a stupid method for making double to a power of 64000 :)
		_freqrangemin=freqrangemin;
		_freqrangemax=freqrangemax;

		_freqrangemin-=(_freqrangemin%64000);
		_freqrangemax-=(_freqrangemax%64000);

		freqrangemin=_freqrangemin;
		freqrangemax=_freqrangemax;

		_freqspan=_freqrangemax-_freqrangemin;
		if (!_freqspan)
			_freqspan=1;		// avoid divide by 0

		sprintf_s(prntxt, 256, "%.3f-%.3f MHz (%d-%d) [%d+%d*%d %dsps] [%d]", freqrangemin/1000000, freqrangemax/1000000, minfreq, maxfreq, panentry.startfreq, panentry.steps, panentry.stepfreq, panentry.samples, _selectedfreq);
		m_RangeInfo.SetWindowText(prntxt);

		GetDlgItem(IDC_WATERFALL)->GetWindowRect(&rect);
		ScreenToClient(&rect);

		passesignored1++;
		passesignored2++;

		if (passesignored2 > 10)
		{
			passesignored2=0;

			// we will take an minimum and maximum of last 0xFFF+1 fft points  
			for (i=0, fmin=999.9f, fmax=0.0f; i<(0xFFF+1); i++)
			{
				if (fmin > dbminmaxpool[i])
					fmin=dbminmaxpool[i];
				if (fmax < dbminmaxpool[i])
					fmax=dbminmaxpool[i];
			}

			c_ColorRangeSlider.SetVisualMinMax(fmin*10.0f, fmax*10.0f);
		}

		if (((_lastrangemin != _freqrangemin)||
			(_lastrangemax != _freqrangemax))&&(passesignored1 > 10))		// just do  not update full-speed, to save some time on meaningless updates
		{
			passesignored1=0;

			// frequency range has changed, so calculate new pantable and ask datatask to send it to radio
			
			steps=_freqspan/64000;				// how much steps do we ned to get the entire range covered
			if (!steps)
				steps=1;

			fftpoints=rect.right-rect.left;		// how much fft points we need to display for each step
			fftpoints/=steps;
			fftpoints++;						// just have one point for overlapping, since frequency position may jitter by one scanline

			//make fftpoints power of two now
			for (i=0, fftpointsP2=2; i<16; i++)
			{
				if ((unsigned int)(1<<i)>=fftpoints)
				{
					fftpointsP2=1<<(i+1);		//multiple by two on the fly, as for output complex FFT both sides (-N and +N) we need double the points
					break;
				}
			}

			if (fftpointsP2 < 16)
				skip=0;
			else if (fftpointsP2 < 32)
				skip=24;
			else
				skip=32;
				
			//EnterCriticalSection(&CriticalSection);		// lock while updating globals to avoid confusion in Panadapter thread (may be needed, may be not)
			//fill in the structure and request pantable update
			panentry.startfreq=_freqrangemin+(96000/2);	// Complex FFT gives us 48kHz up and down from LO frequency, so we need to bump up the freq
			panentry.steps=steps;
			panentry.samples=fftpointsP2;
			panentry.skip=skip;
			// the rest of the structure stays the same as set by OnInitDialog()
			
			// cant update immediately, so ask panadapter task to use new values for next pass 
			fftbands_new=fftpointsP2;				//update global fftbands
			fftbandsneeded_new=fftpoints;			//bands number actually needed to update display, +2 for safety
			skipzone_new=skip;						// number of samples (IQ pairs) to skip after frequency change
			
			do_pantableupdate=true;
			//LeaveCriticalSection(&CriticalSection);

			//cause received frequency entries minimums and maximums updated
			maxfreq=0;
			minfreq=32000000;

			_lastrangemin=_freqrangemin;
			_lastrangemax=_freqrangemax;
		}		

		c_ColorRangeSlider.GetRange(dbrangemin, dbrangemax);

		if ((lastclickX > rect.left)&&(lastclickX < rect.right) && 
			(lastclickY > rect.top)&&(lastclickY < rect.bottom))
		{
			lastclickX-=rect.left;		// get coordinate inside waterfall window

			// based on the currently selected range, find out the frequency clicked			
			_selectedfreq=lastclickX;
			_selectedfreq*=_freqspan;
			_selectedfreq/=rect.right-rect.left;
			_selectedfreq+=_freqrangemin;

			lo_freq=_selectedfreq;
			tune_freq=_selectedfreq;

			//lo_changed=true;			// this is a indicator for tunechanged(), that no action should be taken adjusting LO boundaries. (deprecated)
			update_lo=true;				// show datatask, that LO has to be changed

			//Now sync HDSDR display
			do_callback101=true;					// update LO freq through callback
			do_callback105=true;					// update tune freq through callback			
		}
		else
		{
			lastclickX=0;
			lastclickY=0;
			_selectedfreq=lo_freq;
		}

		sprintf_s(prntxt, 256, "%d (%d) [%d]", magicsfound, magicsfound-localmagic, bytesprocessed);
		m_ActiveInfo.SetWindowText(prntxt);

		lastclickX=0;
		lastclickY=0;

		localmagic=magicsfound;

		freqrangemin=lo_freq;
		freqrangemax=lo_freq;

		freqrangemin-=(iflimit_high-iflimit_low)/2;
		freqrangemax+=(iflimit_high-iflimit_low)/2;
		
		c_FreqRangeSlider.SetVisualMinMax(freqrangemin, freqrangemax);

		SetTimer(ID_CLOCK_TIMER, scrolltimer, NULL);		//restore our 50ms timer
	}

	CDialog::OnTimer(nIDEvent); 
}

void CPanadapterDialog::OnLButtonDown(UINT nFlags, CPoint point)
{
	lastclickX=point.x;
	lastclickY=point.y;

	CDialog::OnLButtonDown(nFlags, point);
}


BOOL CPanadapterDialog::OnEraseBkgnd(CDC* pDC2erase)
{
BOOL retval=false;

	if (pDC2erase != pDC)
	{
		retval=CDialog::OnEraseBkgnd(pDC2erase);
	}
	
	return retval;
}


void CPanadapterDialog::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
CSliderCtrl* pSld;

	pSld = (CSliderCtrl*)pScrollBar;

	// It seems, that having a reversed slider is a real pain, so we have to interpret 
	// the result backwards ourselves. Note, that we also write backwards value to the registry
	// for easing up initialization.

	if (*pSld == m_SpeedSlider)
	{
		//could this generate race condition when the OnTimer() is tryng to re-enable timer?
		scrolltimer=m_SpeedSlider.GetPos();
		scrolltimer=m_SpeedSlider.GetRangeMax()+m_SpeedSlider.GetRangeMin()-scrolltimer;
	}

	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}


LRESULT CPanadapterDialog::OnRangeChange(WPARAM wParam, LPARAM /* lParam */) 
{
  //c_RangeSlider.GetRange(m_Left, m_Right);
  //
  // Do what you have to do.
  // ...
  // 
  return static_cast<LRESULT>(0);
}


extern CPanadapterDialog* m_pmodelessPanadapter;

void CPanadapterDialog::PostNcDestroy()
{	
	AfxMessageBox("PostNcDestroy()");

	CDialog::PostNcDestroy();	

	m_pmodelessPanadapter = NULL;		
	delete this;
}

// We have to handle close event, as we do not have parent object what would perform destruction for us
void CPanadapterDialog::OnClose()
{
	AfxMessageBox("OnClose()");

	run_panadaptertask=false;

	while(!panadaptertask_exited)	// wait until panadapter task finishes
		WaitForSingleObject(sleepevent, 1);

	// release what we had for bitmap
	
	//MemDC.SelectObject(pOldBitmap);
	//MemBitmap->Detach();
	//MemBitmap->DeleteObject();
	delete MemBitmap;
	delete MemDC;

	//DeleteDC(MemDC);
	//ReleaseDC(pDC);

	// reset globals
	do_pantableupdate=false;

	fftbands=0;
	fftbandsneeded=0;
	skipzone=0;

	fftbands_new=8;
	fftbandsneeded_new=4;
	skipzone_new=8;

	dbrangemin=0;
	dbrangemax=1100;

	panstate=PAN_MAGIC;
	_freqrangemin=0;
	_freqrangemax=32000000; 
	_freqspan=32000000;

	_lastrangemin=32000000;		// cause immdiate update on pass 1
	_lastrangemax=0;

	if (m_pmodelessPanadapter)
	{
		m_pmodelessPanadapter->DestroyWindow();
		m_pmodelessPanadapter=NULL;
	}
}