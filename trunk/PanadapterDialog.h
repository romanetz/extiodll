#pragma once
#include "LayeredWindowHelperST\LayeredWindowHelperST.h"
#include "resource.h"		//oddly, if this is not included, the IDD_DIALOG2 is not visible, although it should be
//#include "SelectComPort\ComPortCombo.h"
#include "DialogClasses.h"
#include "RangeSlider.h"

// CExtIODialog dialog

#define ID_CLOCK_TIMER	1

class CPanadapterDialog : public CDialog
{
	DECLARE_DYNAMIC(CPanadapterDialog)

public:
	CPanadapterDialog(CWnd* pParent = NULL);   // standard constructor
	virtual ~CPanadapterDialog();

	CRangeSlider c_FreqRangeSlider;
	CRangeSlider c_ColorRangeSlider;



	HMODULE hParent;

	//void filterfft(float* fftpool, int fftbands);

//	void ChangeMode(unsigned long lofreq, unsigned long tunefreq);

// Dialog Data
	enum { IDD = IDD_DIALOG2 };

protected:
	// Generated message map functions
	//{{AFX_MSG(CGenericMFCDlg)
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	afx_msg void OnClose();
	//afx_msg BOOL OnDeviceChange(UINT nEventType, DWORD_PTR dwData);
	//}}AFX_MSG

	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	CSpeedSlider m_SpeedSlider;
	CRangeInfo m_RangeInfo;
	CActiveInfo m_ActiveInfo;

	LRESULT OnRangeChange(WPARAM  wParam, LPARAM /* lParam */);
	void OnTimer(UINT nIDEvent);
	//void OnPaint();
	BOOL OnEraseBkgnd(CDC* pDC2erase);
	void OnLButtonDown(UINT nFlags, CPoint point);
/*
	CComPortCombo m_comboPorts;
	CGainSliderCHA m_GainSliderA;
	CGainSliderCHB m_GainSliderB;
	CPhaseSliderCoarse m_PhaseSliderCoarse;
	CPhaseSliderFine m_PhaseSliderFine;
	CCheckBoxDiversity m_DiversityCheck;
	CCheckBoxSyncGain m_SyncGainCheck;
	CCheckBoxSyncTune m_SyncTuneCheck;
	CPhaseInfo m_PhaseInfo;
	CTransparencySlider m_TransparencySlider;
	CCheckBoxDllIQ m_DllIQ;	
	CDataRateInfo m_DataRateInfo;
	CCheckBoxDebugConsole m_DebugConsoleCheck;
*/	
	DECLARE_MESSAGE_MAP()

public:
//	int m_nChannelMode;
//	afx_msg void OnBnClickedOk();

private:
	CLayeredWindowHelperST	m_Layered;

public:
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	/*
	afx_msg void OnCbnSelchangeCombo1();
	afx_msg void OnBnClickedCheck1();
	afx_msg void OnBnClickedCheck2();
	afx_msg void OnBnClickedCheck3();
	afx_msg void OnBnClickedCheck4();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnTimer(UINT nIDEvent);
	afx_msg void OnBnClickedRadioCmode1();
	afx_msg void OnBnClickedRadioCmode2();
	afx_msg void OnBnClickedRadioCmode3();
	afx_msg void OnBnClickedRadioCmode4();
	afx_msg void OnBnClickedRadioCmode5();
	afx_msg void OnBnClickedRadioCmode6();
	afx_msg void OnBnClickedRadioCmode7();
	afx_msg void OnBnClickedButton1();
	*/
};
