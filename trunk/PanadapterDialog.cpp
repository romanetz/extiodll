// PanadapterDialog.cpp : implementation file
//

#include "stdafx.h"

#include "PanadapterDialog.h"
#include "ExtIOFunctions.h"

#include <math.h>
#include <process.h>
#include "Fourier.h"

//#include "ExtIODll.h"
//#include "ExtIODialog.h"
//#include "SelectComPort\ComPortCombo.h"
//#include "serial\serial.h"
//#include "libusb\lusb0_usb.h"
//#include "ExtIOFunctions.h"

//#include <Dbt.h>				// Equates for WM_DEVICECHANGE and BroadcastSystemMessage
//#include <conio.h>				// gives _cprintf()

// CPanadapterDialog dialog

extern int Transparency;
extern unsigned int pandata_wptr;
extern unsigned char* PANData;

extern unsigned long iflimit_high;	//ExtIOFunctions.cpp
extern unsigned long iflimit_low;
extern unsigned long lo_freq;
extern unsigned long tune_freq;
extern volatile bool update_lo;
extern volatile bool do_callback105;
extern volatile bool do_callback101;
volatile bool do_pantableupdate=false;

PANENTRY panentry;
int fftbands=8;
int fftbandsneeded=4;

double dbrangemin=0, dbrangemax=1100;


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

	/*
	DDX_Control(pDX, IDC_COMBO1, m_comboPorts);
	DDX_Control(pDX, IDC_SLIDER1, m_GainSliderA);
	DDX_Control(pDX, IDC_SLIDER2, m_GainSliderB);
	DDX_Control(pDX, IDC_SLIDER3, m_PhaseSliderCoarse);
	DDX_Control(pDX, IDC_SLIDER4, m_PhaseSliderFine);
	DDX_Control(pDX, IDC_EDIT1, m_PhaseInfo);
	DDX_Control(pDX, IDC_SLIDER5, m_TransparencySlider);
	DDX_Control(pDX, IDC_CHECK1, m_DllIQ);
	DDX_Control(pDX, IDC_EDIT2, m_DataRateInfo);
	DDX_Radio(pDX, IDC_RADIO_CMODE1, m_nChannelMode);
	DDX_Control(pDX, IDC_CHECK2, m_SyncGainCheck);
	DDX_Control(pDX, IDC_CHECK3, m_SyncTuneCheck);
	DDX_Control(pDX, IDC_CHECK4, m_DebugConsoleCheck);
	*/
}


BEGIN_MESSAGE_MAP(CPanadapterDialog, CDialog)
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_TIMER()			// we need timer to do periodic updates on FFT window. 
	//ON_WM_PAINT()
	ON_WM_LBUTTONDOWN()
	ON_WM_ERASEBKGND()
	ON_REGISTERED_MESSAGE(RANGE_CHANGED, OnRangeChange)
/*
	ON_BN_CLICKED(IDOK, &CExtIODialog::OnBnClickedOk)
	ON_BN_CLICKED(IDC_BUTTON1, &CExtIODialog::OnBnClickedButton1)
	ON_WM_DEVICECHANGE()
	ON_CBN_SELCHANGE(IDC_COMBO1, &CExtIODialog::OnCbnSelchangeCombo1)
	ON_BN_CLICKED(IDC_CHECK1, &CExtIODialog::OnBnClickedCheck1)
	ON_BN_CLICKED(IDC_CHECK2, &CExtIODialog::OnBnClickedCheck2)
	ON_BN_CLICKED(IDC_CHECK3, &CExtIODialog::OnBnClickedCheck3)
	ON_BN_CLICKED(IDC_CHECK4, &CExtIODialog::OnBnClickedCheck4)
	ON_WM_HSCROLL()
	ON_WM_TIMER()			// we need timer to do periodic updates on dialog window. Doing it by accessing contols from different task directly corrupts something
	ON_BN_CLICKED(IDC_RADIO_CMODE1, &CExtIODialog::OnBnClickedRadioCmode1)
	ON_BN_CLICKED(IDC_RADIO_CMODE2, &CExtIODialog::OnBnClickedRadioCmode2)
	ON_BN_CLICKED(IDC_RADIO_CMODE3, &CExtIODialog::OnBnClickedRadioCmode3)
	ON_BN_CLICKED(IDC_RADIO_CMODE4, &CExtIODialog::OnBnClickedRadioCmode4)
	ON_BN_CLICKED(IDC_RADIO_CMODE5, &CExtIODialog::OnBnClickedRadioCmode5)
	ON_BN_CLICKED(IDC_RADIO_CMODE5, &CExtIODialog::OnBnClickedRadioCmode6)
	ON_BN_CLICKED(IDC_RADIO_CMODE5, &CExtIODialog::OnBnClickedRadioCmode7)
*/
END_MESSAGE_MAP()

void PanadapterTask(void* dummy);

CDC MemDC, *pDC;
CRect rcWaterFall;	
CBitmap MemBitmap;

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

	// build waterfall bitblt structures now

	GetDlgItem(IDC_WATERFALL)->GetClientRect(rcWaterFall);
	
	pDC = GetDlgItem(IDC_WATERFALL)->GetDC();

	MemDC.CreateCompatibleDC(pDC);
	MemBitmap.CreateCompatibleBitmap(pDC,rcWaterFall.right,rcWaterFall.bottom);

	// build empty waterfall
	CBitmap *pOldBitmap = (CBitmap*) MemDC.SelectObject(&MemBitmap);

	for (i=0; i<rcWaterFall.bottom; i++)
	{
		for (j=0; j<rcWaterFall.right; j++)
		{
			MemDC.SetPixelV(j, i, RGB(0, 0, rand()%255));
		}
	}

	MemDC.SelectObject(pOldBitmap);

	panentry.magic_I=0x55A0;
	panentry.magic_Q=0xF0F0;
	panentry.samples=32;
	panentry.startfreq=0;
	panentry.stepfreq=64000;
	panentry.steps=500;

	do_pantableupdate=true;
	
	run_panadaptertask=true;
	panadaptertask_exited=false;

	_beginthread(PanadapterTask, 0, NULL);
	// set timer only after DC stuff is initialized
	SetTimer(ID_CLOCK_TIMER, 50, NULL);		//50ms timer, null show put it to task queue

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

	
	/*
	for (i=0; i<_fftbands; i++)
	{
		// expander
		fftpool[i]*=(fftpool[i]/knee)+(100-(100*knee));
		//fftpool[i]*=fftpool[i];		//make a little more "logarithmic" to filter noise
		fftpool[i]/=expander;			//100=no expansion; below 100 will cause expansion
		fftpool[i]+=shift;

		if (fftpool[i] > 100.0f)
			fftpool[i]=100.0f;			// set limiter to 100dB

	}
	*/
	multiplier=(110-(dbrangemin/10))/(dbrangemax/10);

	for (i=0; i<_fftbands; i++)
	{
		fftpool[i]-=(dbrangemin/10);			// crop from below;
		if (fftpool[i] < 0)
			fftpool[i]=0;

		//now multiply, so dbrangemax == 110, clip the rest to 110

		fftpool[i]*=multiplier;
		if (fftpool[i]>110)
			fftpool[i]=110;
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
long long _lastrangemin=32000000;		// cause immdiate update on pass 1
long long _lastrangemax=0;

float dbminmaxpool[0xFFF+1];

void PanadapterTask(void* dummy)
{
static int i=0, j=0, k;
unsigned int l_pandata_wptr;
//char prntxt[64];
char freqbuff[8];		// four last bytes are used for checking the trailer magic (supposed to be AA 55 F0 F0)
HANDLE sleepevent = CreateEvent(NULL, FALSE, FALSE, NULL);		// we are using that instead of sleep(), as it is more kind to overall system resources
HANDLE hArray[2];
//HANDLE hSnapShot;

unsigned int pandatalen=0;
char* pandata;
double* pandataI;
double* pandataQ;
double* fftpoolI;
double* fftpoolQ;
float *fdraw;
long long freqpos;
static int oldfftbands;

float re, im;

unsigned int dbpooloffset=0;

	hArray[0] = sleepevent;
	//hArray[1] = hParent;

	//hSnapShot = lpfCreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0 );

	for (k=0; k<(0xFFF+1); k++)
	{
		dbminmaxpool[k]=0.0f;
	}

	fftpoolI=(double*)malloc(fftbands*sizeof(double));
	fftpoolQ=(double*)malloc(fftbands*sizeof(double));
	pandataI=(double*)malloc(fftbands*sizeof(double));
	pandataQ=(double*)malloc(fftbands*sizeof(double));
	fdraw=(float*)malloc(fftbands*sizeof(float));

	pandata=(char*)malloc(PANDATAMASK+1);
	
	maxfreq=0;
	minfreq=32000000;
	oldfftbands=fftbands;

	/*CBitmap *pOldBitmap = (CBitmap*)*/ MemDC.SelectObject(&MemBitmap);

	panadaptertask_exited=false;

	while(run_panadaptertask)
	{
		//MsgWaitForMultipleObjects(1, hArray, FALSE, 0, QS_ALLINPUT);					// give away timeslice
		WaitForSingleObject(sleepevent, 50);		// sleep for 50ms

		if (oldfftbands<fftbands)
		{
			fftpoolI=(double*)realloc(fftpoolI, fftbands*sizeof(double));
			fftpoolQ=(double*)realloc(fftpoolQ, fftbands*sizeof(double));
			pandataI=(double*)realloc(pandataI, fftbands*sizeof(double));
			pandataQ=(double*)realloc(pandataQ, fftbands*sizeof(double));
			fdraw=(float*)realloc(fdraw, fftbands*sizeof(float));

			oldfftbands=fftbands;
		}

		l_pandata_wptr=pandata_wptr;		

		while (((pandata_rptr&PANDATAMASK)!=(l_pandata_wptr&PANDATAMASK))&&(run_panadaptertask))
		{
			WaitForSingleObject(sleepevent, 0);					// give away timeslice
			//MsgWaitForMultipleObjects(1, hArray, FALSE, 0, QS_ALLINPUT);					// give away timeslice

			switch(panstate)
			{
			case PAN_MAGIC:
			case PAN_DATA:
				while (((pandata_rptr&PANDATAMASK)!=(l_pandata_wptr&PANDATAMASK))&&(run_panadaptertask))
				{
					WaitForSingleObject(sleepevent, 0);					// give away timeslice
					//MsgWaitForMultipleObjects(1, hArray, FALSE, 0, QS_ALLINPUT);					// give away timeslice
					
					for (; i<8; i++)
					{
						if (PANData[(pandata_rptr+i)&PANDATAMASK] == magic[i])
						{
							if (((pandata_rptr+i)&PANDATAMASK)==(l_pandata_wptr&PANDATAMASK))
								break;		// buffer end, so break, but do not reset scan pointer
							else
								continue;	// magic matches and there is still data, so continue scan
						}
						else
						{
							i=0;	// magic fail, so start over again
							break;
						}
					}

					if (i==8)				// all magic done?
					{						
						if ((panstate == PAN_DATA)&&(pandatalen >=(fftbands*4)))
						{
							//new magic found, so we have to process the current pandata buffer and inject new FFT!
							//note, that (int)(*(int*)&freqbuff[0]) contains current starting frequency!
							
							// copy last 16 IQ pairs to FFT source buffer
							for (i=0, j=(pandatalen&0xFFFFFFF4)-(fftbands*4); i<fftbands; i++)
							{
								WaitForSingleObject(sleepevent, 0);					// give away timeslice
								//MsgWaitForMultipleObjects(1, hArray, FALSE, 0, QS_ALLINPUT);					// give away timeslice
								
								// have to be copied with sign!
								pandataI[i]=(short)(short*)*(pandata+j);
								j+=2;
								pandataQ[i]=(short)(short*)*(pandata+j);
								j+=2;
							}
							
							fft_double(fftbands, 0, pandataQ, pandataI, fftpoolQ, fftpoolI);

							for(i=0; i<fftbands/2; i++)	//Use FFT_LEN/4 since the data is mirrored within the array.
							{
								WaitForSingleObject(sleepevent, 0);					// give away timeslice
								//MsgWaitForMultipleObjects(1, hArray, FALSE, 0, QS_ALLINPUT);					// give away timeslice

								re = (float)fftpoolQ[i];
								im = (float)fftpoolI[i];
								
								fdraw[i] = Decibels(re, im);			//get Decibels in 0-110 range

								if (fdraw[i]) // first value in array is zero, so ignore
								{
									dbminmaxpool[dbpooloffset&0xFFF] = (float)fdraw[i];
									dbpooloffset++;
								}
							}

							// normalize data for graphic display now!
							filterfft(fdraw, fftbands/2);
							for (i=0; i<fftbands/2; i++)
								fdraw[i]=(fdraw[i]/110.0f)*1279.0f;		// 0db -> 1279

							//add scanline data. We have window which is n pixels wide and are scanning with 8 fft points per 
							//96kHz, with 64kHz frequency step.

							freqpos=(int)(*(int*)&freqbuff[0]);

							//for safety, filter out the values what are out of bounds
							if (freqpos > _lastrangemax)
								freqpos=_lastrangemax;
							else if (freqpos < _lastrangemin)
								freqpos=_lastrangemin;

							freqpos-=panentry.startfreq;		// get frequency offset inside fft window
							freqpos*=(rcWaterFall.right-1);
							freqpos/=panentry.startfreq+(panentry.steps*panentry.stepfreq);

							if (maxfreq<(int)(*(int*)&freqbuff[0]))
								maxfreq=(int)(*(int*)&freqbuff[0]);

							if (minfreq>(int)(*(int*)&freqbuff[0]))
								minfreq=(int)(*(int*)&freqbuff[0]);
							
							// take from the middle of fft data as much bands as needed
							for (i=0, j=(fftbands/4)-(fftbandsneeded/2); i<fftbandsneeded; i++, j++)
							{
								if ((freqpos+i)<=(rcWaterFall.right-1))
									MemDC.SetPixelV(freqpos+i, rcWaterFall.bottom-1, Intensity(fdraw[j]));
							}

							/*
							for (j=0; j<rcWaterFall.right; j++)
							{
								//MsgWaitForMultipleObjects(1, hArray, FALSE, 0, QS_ALLINPUT);					// give away timeslice
								WaitForSingleObject(sleepevent, 0);
								MemDC.SetPixelV(j, rcWaterFall.bottom-1, RGB(0, 0, rand()%255));
							}
							*/
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
								//WaitForSingleObject(sleepevent, 0);					// give away timeslice
								//MsgWaitForMultipleObjects(1, hArray, FALSE, 0, QS_ALLINPUT);					// give away timeslice
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
					WaitForSingleObject(sleepevent, 0);					// give away timeslice					
					//MsgWaitForMultipleObjects(1, hArray, FALSE, 0, QS_ALLINPUT);					// give away timeslice

					freqbuff[i]=PANData[pandata_rptr&PANDATAMASK];
					i++;
					pandata_rptr++;

					if (i==8)
					{
						if (!memcmp(freqbuff+4, magic+8, 4))
						{
							i=0;
							panstate=PAN_DATA;
							magicsfound++;		// increment successful tokens count
						}
						else
						{
							// trailer failure, so discard current header and start over
							i=0;
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


	
		//CBitmap *pOldBitmap = (CBitmap*) MemDC.SelectObject(&MemBitmap);
		
		//CBrush bkBrush(HS_FDIAGONAL,RGB(0,rand()%255,0));	// Creates a brush with random shades of green. The rand()%255 generates a value between 0 and 255 randomly. 
		//MemDC.FillRect(rcWaterFall,&bkBrush);

		/*
		for (j=0; j<rcWaterFall.right; j++)
		{
			MemDC.SetPixelV(j, rcWaterFall.bottom-1, RGB(0, 0, rand()%255));
		}
		*/
		//pDC->BitBlt(0,0,rcWaterFall.right,rcWaterFall.bottom,&MemDC,0,0,SRCCOPY);	// Copies the bitmap from the memory dc to the pdc using a fast bitblt function call.		
	}

	//MemDC.SelectObject(pOldBitmap);

	//ReleaseDC(pDC);
	//ReleaseDC(&MemDC);

	free(pandataI);
	free(fftpoolI);
	free(pandataQ);
	free(fftpoolQ);
	free(pandata);
	free(fdraw);

	panadaptertask_exited=true;
}

long lastclickX=0;
long lastclickY=0;

void CPanadapterDialog::OnTimer(UINT nIDEvent) 
{
char prntxt[64];
static int localmagic=0;
double freqrangemin, freqrangemax;
long long _freqrangemin, _freqrangemax, _freqspan;
static long long  _selectedfreq=0;
static long long passesignored1=0, passesignored2=0;
CRect rect;
int i;
float fmin, fmax;

	if (nIDEvent == ID_CLOCK_TIMER)
	{
		CBitmap *pOldBitmap = (CBitmap*) MemDC.SelectObject(&MemBitmap);
		pDC->BitBlt(0,0,rcWaterFall.right,rcWaterFall.bottom,&MemDC,0,0,SRCCOPY);	// Copies the bitmap from the memory dc to the pdc using a fast bitblt function call.
		//bitblt internally by one line and add a new scanline at the bottom
		MemDC.BitBlt(0,0,rcWaterFall.right,rcWaterFall.bottom,&MemDC,0,1,SRCCOPY);	// Copies the bitmap from the memory dc to the pdc using a fast bitblt function call.
		MemDC.SelectObject(pOldBitmap);

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

		sprintf_s(prntxt, 64, "%.3f-%.3f MHz (%d-%d)", freqrangemin/1000000, freqrangemax/1000000, minfreq, maxfreq);
		m_RangeInfo.SetWindowText(prntxt);

		GetDlgItem(IDC_WATERFALL)->GetWindowRect(&rect);
		ScreenToClient(&rect);

		passesignored1++;
		passesignored2++;

		if (passesignored2 > 20)
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
			(_lastrangemax != _freqrangemax))&&(passesignored1 > 20))		// just do  not update full-speed, to save some time on meaningless updates
		{
			passesignored1=0;

			// frequency range has changed, so calculate new pantable and ask datatask to send it to radio
			{
			unsigned int steps, fftpoints, fftpointsP2;

				steps=_freqspan/64000;				// how much steps do we ned to get the entire range covered
				if (!steps)
					steps=1;

				fftpoints=rect.right-rect.left;		// how much fft points we need to display for each step
				fftpoints/=steps;
				fftpoints++;						// just have one point for overlapping, since frequency position may jitter by one scanline

				//make fftpoints power of two now
				for (i=0, fftpointsP2=2; i<16; i++)
				{
					if ((1<<i)>=fftpoints)
					{
						fftpointsP2=1<<(i+1);		//multiple by two on the fly, as output fft points are symmetrical, so we need twice as much data to process
						break;
					}
				}
					
				EnterCriticalSection(&CriticalSection);		// lock while updating globals to avoid confusion in Panadapter thread (may be needed, may be not)
				//fill in the structure and request pantable update
				panentry.startfreq=_freqrangemin;
				panentry.steps=steps;
				panentry.samples=fftpointsP2;
				// the rest of the structure stays the same as set by OnInitDialog()
				
				fftbands=fftpointsP2;				//update global fftbands
				fftbandsneeded=fftpoints;			//bands number actually needed to update display, +2 for safety
				
				do_pantableupdate=true;
				LeaveCriticalSection(&CriticalSection);

				maxfreq=0;
				minfreq=32000000;
			}
			
			_lastrangemin=_freqrangemin;
			_lastrangemax=_freqrangemax;
		}

		c_ColorRangeSlider.GetRange(dbrangemin, dbrangemax);

		if ((lastclickX > rect.left)&&(lastclickX < rect.right) && 
			(lastclickY > rect.top) && (lastclickY < rect.bottom))
		{
			lastclickX-=rect.left;		// get coordinate inside waterfall window

			// based on the currently selected range, find out the frequency clicked			
			_selectedfreq=lastclickX;
			_selectedfreq*=_freqspan;
			_selectedfreq/=rect.right-rect.left;
			_selectedfreq+=_freqrangemin;

			lo_freq=_selectedfreq;
			tune_freq=_selectedfreq;

			//lo_changed=true;			// this is a indicator for tunechanged(), that no action should be taken adjusting LO boundaries.
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

		sprintf_s(prntxt, 64, "%d (%d) [%d]", magicsfound, magicsfound-localmagic, _selectedfreq);
		m_ActiveInfo.SetWindowText(prntxt);

		lastclickX=0;
		lastclickY=0;

		localmagic=magicsfound;

		freqrangemin=lo_freq;
		freqrangemax=lo_freq;

		freqrangemin-=(iflimit_high-iflimit_low)/2;
		freqrangemax+=(iflimit_high-iflimit_low)/2;
		
		c_FreqRangeSlider.SetVisualMinMax(freqrangemin, freqrangemax);
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

	if (*pSld == m_SpeedSlider)
	{
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
	CDialog::PostNcDestroy();	
	m_pmodelessPanadapter = NULL;		
	delete this;
}

// We have to handle close event, as we do not have parent object what would perform destruction for us
void CPanadapterDialog::OnClose()
{
	run_panadaptertask=false;

	while(!panadaptertask_exited)	// wait until panadapter task finishes
	{}

	if (m_pmodelessPanadapter)
	{
		m_pmodelessPanadapter->DestroyWindow();
	}
}