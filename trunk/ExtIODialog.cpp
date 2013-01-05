// ExtIODialog.cpp : implementation file
//

#include "stdafx.h"
#include "ExtIODll.h"
#include "ExtIODialog.h"
#include "SelectComPort\ComPortCombo.h"
#include "serial\serial.h"
#include "libusb\lusb0_usb.h"
#include "ExtIOFunctions.h"
#include "PanadapterDialog.h"

#include <Dbt.h>				// Equates for WM_DEVICECHANGE and BroadcastSystemMessagewaitforsinglr
#include <conio.h>				// gives _cprintf()

extern CSerial serial;
char serbuff[128];

extern usb_dev_handle *dev;

extern int ChannelMode;
extern int SyncTuning;
extern int SyncGain;

extern unsigned long lo_freq;
extern unsigned long lastlo_freqA;
extern unsigned long lastlo_freqB;

extern unsigned long tune_freq;
extern unsigned long lasttune_freqA;
extern unsigned long lasttune_freqB;

volatile bool channelmode_changed=false;		// set true to have datatask changing the channel mode (by sending USB control message to radio)
volatile bool samplerate_changed=false;			// set true to ask datatask to change sample rate for radio
volatile bool update_lo=false;					// trigger LO change on datatask

extern volatile bool update_registry;
extern volatile bool update_phaseA;
extern volatile bool update_gain;
extern volatile bool do_callback105;

extern long IQSampleRate;
long last_samplerate;

extern int GainA, GainB;
extern int PhaseCoarse, PhaseFine;
extern int DebugConsole;

extern const struct usb_version* libver;

CPanadapterDialog* m_pmodelessPanadapter = NULL;

extern CWnd* MainWindow;

// CExtIODialog dialog

IMPLEMENT_DYNAMIC(CExtIODialog, CDialog)

CExtIODialog::CExtIODialog(CWnd* pParent /*=NULL*/)
	: CDialog(CExtIODialog::IDD, pParent)
{

}

CExtIODialog::~CExtIODialog()
{
}

void CExtIODialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
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
	DDX_Control(pDX, IDC_BUTTON1, m_Button1);
}


BEGIN_MESSAGE_MAP(CExtIODialog, CDialog)
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
	ON_BN_CLICKED(IDC_RADIO_CMODE6, &CExtIODialog::OnBnClickedRadioCmode6)
	ON_BN_CLICKED(IDC_RADIO_CMODE7, &CExtIODialog::OnBnClickedRadioCmode7)
END_MESSAGE_MAP()


// CExtIODialog message handlers

int Transparency=0;
int fw_major=-1, fw_minor=0, extio_major=DLLVER_MAJOR, extio_minor=DLLVER_MINOR;
char verinfo[128];

BOOL CExtIODialog::OnInitDialog()
{
int ret;
int hardwaretype;

	CDialog::OnInitDialog();

	m_Layered.AddLayeredStyle(m_hWnd);
	
	if ((Transparency == -1)||(Transparency == 0))
		Transparency=DEFAULTTRANSPARENCY;
	m_Layered.SetTransparentPercentage(m_hWnd, Transparency);

	//Work on COM ports combo box

	// By default, the first combo box item is "<None>".
	//m_comboPorts.SetNoneItem(0);
    // The default strings may be also change using SetNoneStr().
	//m_listPorts.SetNoneStr(_T("No port"));
    // By default, all COM ports are listed.
	//m_listPorts.SetOnlyPhysical(1);
    // By default, only present COM ports are listed.
	//m_comboPorts.SetOnlyPresent(0);

	// Pre-select the configured port
	int nPortNum = AfxGetApp()->GetProfileInt(_T("Config"), _T("ComPort"), -1);
	m_comboPorts.InitList(nPortNum);
	// You may also use the COM port file name "COM<n>" or "\\.\COM<n>".
	//    CString strPort = AfxGetApp()->GetProfileString(_T("Config"), _T("ComPortStr"));
	//    m_listPorts.InitList(strPort.GetString());

	// init sliders
	m_GainSliderA.SetRange(0, 15);
	m_GainSliderA.SetPos(GainA);
	m_GainSliderA.SetTicFreq(1);

	m_GainSliderB.SetRange(0, 15);
	m_GainSliderB.SetPos(GainB);
	m_GainSliderB.SetTicFreq(1);

	m_PhaseSliderCoarse.SetRange(0, 359);	
	m_PhaseSliderCoarse.SetTicFreq(10);
	m_PhaseSliderCoarse.SetPos(PhaseCoarse);
	//m_PhaseSliderCoarse.EnableWindow(false);

	m_PhaseSliderFine.SetRange(0, 100);	
	m_PhaseSliderFine.SetTicFreq(10);
	m_PhaseSliderFine.SetPos(PhaseFine);	
	//m_PhaseSliderFine.EnableWindow(false);

	m_TransparencySlider.SetRange(2, 10);	
	m_TransparencySlider.SetTicFreq(1);
	m_TransparencySlider.SetPos(Transparency/10);	

	m_PhaseInfo.SetWindowText("0.0");
	m_DataRateInfo.SetWindowText("0 Mbit/s");

	hardwaretype=AfxGetApp()->GetProfileInt(_T("Config"), _T("HardwareType"), -1);
	if (hardwaretype == 3)
		m_DllIQ.SetCheck(BST_CHECKED);
	else
		m_DllIQ.SetCheck(BST_UNCHECKED);

	SetTimer(ID_CLOCK_TIMER, 500, NULL);		//500ms timer, null show put it to task queue

	// have we fetched radio firmware version?
	if (fw_major == -1)
	{
		ret=-1;

		if (libusb_ok)
		{
			ret=usb_control_msg(dev, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
								LIBUSB_GETVER, 
								0,  
								SDR_BULKIF,  
								verinfo, 4, 1000);
		}

		if (ret > 0)
		{
			fw_major=*(__int16*)&verinfo[0];
			fw_minor=*(__int16*)&verinfo[2];
		}
		else
		{
			_cprintf("No Firmwre Version info: ret=%d (%s)\n", ret, usb_strerror());
			fw_major=0;
			fw_minor=0;
		}
	}

	sprintf_s(verinfo, 128, "ExtIO DLL V%d.%d", extio_major, extio_minor);
	GetDlgItem(IDC_STATIC_EXTIOVER)->SendMessage(WM_SETTEXT, 0, (LPARAM)verinfo);
	
	if ((fw_major < MIN_FW_MAJOR)||(fw_minor < MIN_FW_MINOR) || (fw_major == 256)) //1.58 had wrong endian for version info, so check for 256 major
	{
		sprintf_s(verinfo, 128, "Radio FW  V%d.%d (Obsolete - Please Upgrade!)", fw_major, fw_minor);
	}
	else
	{
		sprintf_s(verinfo, 128, "Radio FW  V%d.%d", fw_major, fw_minor);
	}
	GetDlgItem(IDC_STATIC_FWVER)->SendMessage(WM_SETTEXT, 0, (LPARAM)verinfo);

	if (libver)
	{
		sprintf_s(verinfo, 128, "LibUSB-win32 DLL:%d.%d.%d.%d  Driver:%d.%d.%d.%d", 
						libver->dll.major, libver->dll.minor, libver->dll.micro, libver->dll.nano, 
						libver->driver.major, libver->driver.minor, libver->driver.micro, libver->driver.nano);
	}
	else
		sprintf_s(verinfo, 128, "No LibUSB-win32 Version Info");

	GetDlgItem(IDC_STATIC_LIBUSBVER)->SendMessage(WM_SETTEXT, 0, (LPARAM)verinfo);

	m_nChannelMode=AfxGetApp()->GetProfileInt(_T("Config"), _T("ChannelMode"), 0);

	// override channel mode if data is not supplied by ExtIO DLL
	if (hardwaretype != 3)
	{
		if ((m_nChannelMode != CHMODE_A)&&(m_nChannelMode!=CHMODE_AMB))
			m_nChannelMode=CHMODE_A;

		// disable radio buttons what should not be used

		GetDlgItem(IDC_RADIO_CMODE2)->EnableWindow(false);
		GetDlgItem(IDC_RADIO_CMODE3)->EnableWindow(false);
		GetDlgItem(IDC_RADIO_CMODE5)->EnableWindow(false);
		GetDlgItem(IDC_RADIO_CMODE6)->EnableWindow(false);
		GetDlgItem(IDC_RADIO_CMODE7)->EnableWindow(false);
	}

	// only enable panadapter if firmware version supports it
	if ((fw_major < MIN_FW_MAJOR_PAN)||(fw_minor < MIN_FW_MINOR_PAN) || (fw_major == 256)) //1.58 had wrong endian for version info, so check for 256 major
	{
		if ((m_nChannelMode >=5)&&(m_nChannelMode <=6))
			m_nChannelMode=CHMODE_A;

		GetDlgItem(IDC_RADIO_CMODE6)->EnableWindow(false);
		GetDlgItem(IDC_RADIO_CMODE7)->EnableWindow(false);
	}

	m_SyncGainCheck.SetCheck(AfxGetApp()->GetProfileInt(_T("Config"), _T("SyncGain"), 0));

	if ((m_nChannelMode > 1)&&(m_nChannelMode < 5))		// fix tuning for diversity modes
	{
		m_SyncTuneCheck.SetCheck(1);
		m_SyncTuneCheck.EnableWindow(false);
		m_Button1.EnableWindow(false);
	}
	else if (m_nChannelMode <= 1)
	{
		m_SyncTuneCheck.SetCheck(AfxGetApp()->GetProfileInt(_T("Config"), _T("SyncTuning"), 0));
		m_Button1.EnableWindow(false);
	}
	else if ((m_nChannelMode >= 5)&&(m_nChannelMode <= 6))
	{
		m_SyncTuneCheck.SetCheck(0);
		m_SyncTuneCheck.EnableWindow(false);
		m_SyncGainCheck.SetCheck(0);
		m_SyncGainCheck.EnableWindow(false);

		SyncTuning=0;
		SyncGain=0;

		m_Button1.EnableWindow(true);
	}

	UpdateData(false);			//update radio buttons

	last_samplerate=IQSampleRate;

	m_DebugConsoleCheck.SetCheck(DebugConsole);

	return true;
}


void CExtIODialog::OnBnClickedOk()
{
	update_registry=true;		// ask ExtIORegistryUpdate() to perform update
	OnOK();
}

extern CExtIODialog* m_pmodeless;


void CExtIODialog::PostNcDestroy()
{
	CDialog::PostNcDestroy();	
	m_pmodeless = NULL;		
	delete this;
}

// We have to handle close event, as we do not have parent object what would perform destruction for us
void CExtIODialog::OnClose()
{
	if (m_pmodelessPanadapter)
	{
		m_pmodelessPanadapter->CloseWindow();

		while(m_pmodelessPanadapter)
		{}
	}

	if (m_pmodeless)
	{
		m_pmodeless->DestroyWindow();
	}
}

// WM_DEVICECHANGE handler.
// Used here to detect plug-in and -out of devices providing virtual COM ports.
// May open/close the configured virtual serial port when it matches the plugged device.
BOOL CExtIODialog::OnDeviceChange(UINT nEventType, DWORD_PTR dwData)
{
	//TRACE1("WM_DEVICECHANGE event %#X\n", nEventType);

	BOOL bResult = CDialog::OnDeviceChange(nEventType, dwData);

/*
CDC Events are something else than PDEV_BROADCAST_PORT, so we are procesing all removal and arrival requests for hardware.
The code below would work for hadware- and virtual serial ports, but not for CDC ports.

	// Assume port changing (serial or parallel). The devicetype field is part of header of all structures.
	// The name passed here is always the COM port name (even with virtual ports).
	PDEV_BROADCAST_PORT pPort = reinterpret_cast<PDEV_BROADCAST_PORT>(dwData);

	if ((nEventType == DBT_DEVICEARRIVAL || nEventType == DBT_DEVICEREMOVECOMPLETE) &&
		pPort &&
		pPort->dbcp_devicetype == DBT_DEVTYP_PORT &&	// serial or parallel port
		CEnumDevices::GetPortFromName(pPort->dbcp_name) > 0)
	{
		if (nEventType == DBT_DEVICEARRIVAL)
			TRACE1("Device %s is now available\n", pPort->dbcp_name);
		else
			TRACE1("Device %s has been removed\n", pPort->dbcp_name);
		// Update the lists.
		m_comboPorts.InitList();
	}
*/

	//_cprintf("device change, nEventType=%X\n", nEventType);

	// In some reason we are always getting the DBT_DEVNODES_CHANGED status here, so handle all three
	if (nEventType == DBT_DEVICEARRIVAL || nEventType == DBT_DEVICEREMOVECOMPLETE || nEventType == DBT_DEVNODES_CHANGED)
		m_comboPorts.InitList();

	return bResult;
}


void CExtIODialog::OnCbnSelchangeCombo1()
{
	int nComboPortNum = m_comboPorts.GetPortNum();

	if (m_comboPorts.GetCount() && nComboPortNum <= 0)
	{
		AfxMessageBox(_T("No COM port has been selected from the combo box"));
		return;
	}

	//_cprintf("selected port: COM%d\n", nComboPortNum);

	// write down to registry
	AfxGetApp()->WriteProfileInt(_T("Config"), _T("ComPort"), nComboPortNum);
}

void CExtIODialog::OnBnClickedCheck1()
{
	if (m_DllIQ.GetCheck())
	{
		AfxGetApp()->WriteProfileInt(_T("Config"), _T("HardwareType"), 3);		// 16-bit ExtIODll managed I/Q source		
	}
	else
	{
		AfxGetApp()->WriteProfileInt(_T("Config"), _T("HardwareType"), 4);		// Audio card managed by HDSDR
	}

	AfxMessageBox("I/Q Data Source Changed, Restart Application!", MB_ICONEXCLAMATION);
}

void CExtIODialog::OnBnClickedCheck2()
{
	if (m_SyncGainCheck.GetCheck())
		SyncGain=1;
	else
		SyncGain=0;

	AfxGetApp()->WriteProfileInt(_T("Config"), _T("SyncGain"), SyncGain);

	// if sync gain is checked, sync both gains to Ch A gain
	if (SyncGain)
	{
		GainB=GainA;
		m_GainSliderB.EnableWindow(false);
		m_GainSliderB.SetPos(GainB);
		update_gain=true;		// ask datatask to update gain
	}
	else
		m_GainSliderB.EnableWindow(true);
}

void CExtIODialog::OnBnClickedCheck3()
{
	if (m_SyncTuneCheck.GetCheck())
		SyncTuning=1;
	else
		SyncTuning=0;

	AfxGetApp()->WriteProfileInt(_T("Config"), _T("SyncTuning"), SyncTuning);	

	if (SyncTuning)
	{
		update_lo=true;
		do_callback105=true;
	}
}

void CExtIODialog::OnBnClickedCheck4()
{
	AfxGetApp()->WriteProfileInt(_T("Config"), _T("DebugConsole"), m_DebugConsoleCheck.GetCheck());
	if (m_DebugConsoleCheck.GetCheck())
		AfxMessageBox("Restart Application to Enable Debug Console!", MB_ICONEXCLAMATION);
	else
		AfxMessageBox("Restart Application to Disable Debug Console!", MB_ICONEXCLAMATION);
}

void CExtIODialog::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
unsigned long phaseword;
CSliderCtrl* pSld;

	pSld = (CSliderCtrl*)pScrollBar;

	if (*pSld == m_GainSliderA)
	{
		GainA = m_GainSliderA.GetPos();
		
		if (HWType != 3)
		{
			sprintf_s(serbuff, 128, "_ga %d\n\r", GainA);
			serial.Write(serbuff, strlen(serbuff));
		}
		else
			update_gain=true;

		if (SyncGain)
		{
			GainB = GainA;
			m_GainSliderB.SetPos(GainB);
		}
		else
		{
			if (GainA > 7)
			{
				//gains higher than 7 mean, that exponent is going to be fixed for both channels, so move the 
				//channel B slider accordingly
				
				if (m_GainSliderB.GetPos() < 8)
				{
					GainB=8;
					if (HWType !=3)
					{
						sprintf_s(serbuff, 128, "_gb 8\n\r");
						serial.Write(serbuff, strlen(serbuff));
					}
					else
						update_gain=true;

					m_GainSliderB.SetPos(GainB);
				}
			}
			else
			{
				if (m_GainSliderB.GetPos() > 7)
				{
					GainB=7;
					if (HWType != 3)
					{
						sprintf_s(serbuff, 128, "_gb 7\n\r");
						serial.Write(serbuff, strlen(serbuff));
					}
					else
						update_gain=true;

					m_GainSliderB.SetPos(GainB);
				}
			}	
		}
	}
	else if ((*pSld == m_GainSliderB) || (SyncGain))
	{
		GainB = m_GainSliderB.GetPos();
		
		if (HWType != 3)
		{
			sprintf_s(serbuff, 128, "_gb %d\n\r", GainB);
			serial.Write(serbuff, strlen(serbuff));
		}
		else
			update_gain=true;

		if (GainB > 7)
		{
			//gains higher than 7 mean that exponent is going to be fixed for both channels, so move the 
			//channel B slider accordingly
			
			if (m_GainSliderA.GetPos() < 8)
			{
				GainA=8;
				if (HWType != 3)
				{
					sprintf_s(serbuff, 128, "_ga 8\n\r");
					serial.Write(serbuff, strlen(serbuff));
				}
				else
					update_gain=true;

				m_GainSliderA.SetPos(GainA);
			}
		}
		else
		{
			if (m_GainSliderA.GetPos() > 7)
			{
				GainA=7;
				if (HWType != 3)
				{
					sprintf_s(serbuff, 128, "_ga 7\n\r");
					serial.Write(serbuff, strlen(serbuff));
				}
				else
					update_gain=true;

				m_GainSliderA.SetPos(GainA);
			}
		}
	}
	else if ((*pSld == m_PhaseSliderCoarse)||(*pSld == m_PhaseSliderFine))
	{
		PhaseFine=m_PhaseSliderFine.GetPos();
		PhaseCoarse=m_PhaseSliderCoarse.GetPos();

		sprintf_s(serbuff, 128, "%d.%d", PhaseCoarse+(PhaseFine/100), PhaseFine%100);
		m_PhaseInfo.SetWindowText(serbuff);

		// calculate phase word from the sliders		

		if (HWType != 3)
		{
			phaseword=1820;	//65535=2pi rad = 360deg; 65535/360*1000
			phaseword*=(PhaseCoarse*100)+PhaseFine;
			phaseword/=1000;

			sprintf_s(serbuff, 128, "_pa %ld\n\r", phaseword);
			serial.Write(serbuff, strlen(serbuff));
		}
		else
		{
			update_phaseA=true;			// ask datatask to update phase word!
		}
	}
	else if (*pSld == m_TransparencySlider)
	{
		Transparency=m_TransparencySlider.GetPos()*10;
		m_Layered.SetTransparentPercentage(m_hWnd, Transparency);
	}	

	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}

char global_dataratestr[64]="0 Mbit/s";

void CExtIODialog::OnTimer(UINT nIDEvent) 
{
	switch(nIDEvent)
	{
		case ID_CLOCK_TIMER:
			m_DataRateInfo.SetWindowText(global_dataratestr);
			break;

		//case ID_COUNT_TIMER:		
		//	break;
	}

	CDialog::OnTimer(nIDEvent);
}

void CExtIODialog::ChangeMode(unsigned long _lofreq, unsigned long _tunefreq)
{
unsigned long lofreq;
unsigned long tunefreq;

	UpdateData(true);			// read radio buttons
	ChannelMode=m_nChannelMode;	// set global

	AfxGetApp()->WriteProfileInt(_T("Config"), _T("ChannelMode"), m_nChannelMode);

	switch(ChannelMode)
	{
	case CHMODE_A:
	case CHMODE_B:

		SyncTuning=AfxGetApp()->GetProfileInt(_T("Config"), _T("SyncTuning"), 0);
		m_SyncTuneCheck.SetCheck(SyncTuning);
		m_SyncTuneCheck.EnableWindow(true);
		m_SyncGainCheck.SetCheck(SyncGain);
		m_SyncGainCheck.EnableWindow(true);
		if (HWType == 3)
			IQSampleRate=IQSAMPLERATE_FULL;

		m_Button1.EnableWindow(false);
		break;

	case CHMODE_APB:
	case CHMODE_AMB:
	case CHMODE_BMA:			// fix tuning for diversity modes

		m_SyncTuneCheck.SetCheck(1);
		m_SyncTuneCheck.EnableWindow(false);
		m_SyncGainCheck.SetCheck(SyncGain);
		m_SyncGainCheck.EnableWindow(true);

		SyncTuning=1;
		if (HWType == 3)
			IQSampleRate=IQSAMPLERATE_DIVERSITY;

		m_Button1.EnableWindow(false);
		break;	

	case CHMODE_ABPAN:
	case CHMODE_BAPAN:

		m_SyncTuneCheck.SetCheck(0);
		m_SyncTuneCheck.EnableWindow(false);
		m_SyncGainCheck.SetCheck(0);
		m_SyncGainCheck.EnableWindow(false);

		SyncTuning=0;
		SyncGain=0;

		if (HWType == 3)
			IQSampleRate=IQSAMPLERATE_PANADAPTER;

		m_Button1.EnableWindow(true);
		break;
	
	default:			// should not get here
		break;	
	}

	if (SyncTuning)				// override frequencies with channel A if synchronous tuning is selected!
	{
		lofreq=lastlo_freqA;
		tunefreq=lasttune_freqA;
	}
	else
	{
		lofreq=_lofreq;
		tunefreq=_tunefreq;
	}

	if ((long)lofreq !=-1)
	{		
		lo_freq=lofreq;
		update_lo=true;
		(*ExtIOCallback)(-1, 101, 0, NULL);			// sync lo frequency
	}

	if ((long)tunefreq !=-1)
	{		
		tune_freq=tunefreq;
		(*ExtIOCallback)(-1, 105, 0, NULL);			// sync tune frequency
	}	

	if (HWType != 3)
	{
		if (ChannelMode == 0)
			sprintf_s(serbuff, 128, "diversity 0\n\r");
		else
			sprintf_s(serbuff, 128, "diversity 1\n\r");

		serial.Write(serbuff, strlen(serbuff));		
	}
	else
	{
		// theoretically we should do that inside critical section, but hopefuly works without as well!
		channelmode_changed=true;	// indicate datatask that channel mode has changed (issues USB control message with new LIBMODE_xxx)
	}
	
	// sample rate change forces the HDSDR to completely stop and restart the playback.
	// Therefore, ask sample rate change only in case it actually changes!
	if (last_samplerate != IQSampleRate)
	{
		last_samplerate=IQSampleRate;
		samplerate_changed=true;	// indicate datatask, that sample rate must be changed
	}
}

void CExtIODialog::OnBnClickedRadioCmode1()		//A
{	
	ChangeMode(lastlo_freqA, lasttune_freqA);
}

void CExtIODialog::OnBnClickedRadioCmode2()		//B
{
	ChangeMode(lastlo_freqB, lasttune_freqB);
}

void CExtIODialog::OnBnClickedRadioCmode3()		//A+B
{
	ChangeMode(lastlo_freqA, lasttune_freqA);
}

void CExtIODialog::OnBnClickedRadioCmode4()		//A-B
{
	ChangeMode(lastlo_freqA, lasttune_freqA);
}

void CExtIODialog::OnBnClickedRadioCmode5()		//B-A
{
	ChangeMode(lastlo_freqB, lasttune_freqB);
}

void CExtIODialog::OnBnClickedRadioCmode6()		//A+Panadapter
{
	ChangeMode(lastlo_freqA, lasttune_freqA);
}

void CExtIODialog::OnBnClickedRadioCmode7()		//B+Panadapter
{
	ChangeMode(lastlo_freqB, lasttune_freqB);
}

void CExtIODialog::OnBnClickedButton1()
{
	if(m_pmodelessPanadapter)
	{
		m_pmodelessPanadapter->ShowWindow(SW_RESTORE);
		m_pmodelessPanadapter->SetForegroundWindow();	
	}
	else
	{
		m_pmodelessPanadapter = new CPanadapterDialog;

		if (m_pmodelessPanadapter)
		{
			m_pmodelessPanadapter->Create(/*CGenericMFCDlg*/CPanadapterDialog::IDD, CWnd::GetActiveWindow() /*MainWindow*/ /*GetDesktopWindow()*/);
			m_pmodelessPanadapter->ShowWindow(SW_SHOW);
		}
		else
		{
			AfxMessageBox("Unable to Create Panadapter Window!", MB_OK | MB_ICONEXCLAMATION);
		}
	}
}
