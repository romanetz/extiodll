// GainSliders.cpp : implementation file
//

#include "stdafx.h"
#include "ExtIODll.h"
#include "ExtIODialog.h"


// Our empty class definition template. While there are likely more beautiful ways for this 
// in c++, this one works, so there are no experiments with templates.

// This approach is useful for getting the DDX variablesset up for objects, so we 
// can use DDX method for communication.

// All the classes are left empty, since we really need these only as a variables
// of the right type. All the setup for the controls is performed at the 
// ExtIODlg.cpp OnInitDialog() to keep all control initializations at the same place
//
// Basically, what you need to do is following:
//
//	- Create control on the resource screen
//	- Add class definition with the macro here and DialogClasses.h, be sure to use appropriate public class
//	- Add class to the CExtIODialog class definition at ExtIODialog.h
//	- Add DDX message handler to the ExtIODialog.cpp CExtIODialog::DoDataExchange()
//
// See also: http://msdn.microsoft.com/en-us/library/xwz5tb1x%28v=vs.110%29.asp


//	NB! If the main dialog has to receive specific events from control, appropriate messaging definitions
//	still have to be added to message map at ExtIODialog.cpp 
//

#define DialogClass(ClassName, PublicClass)	\
	IMPLEMENT_DYNAMIC(ClassName, PublicClass)		\
													\
	ClassName::##ClassName()						\
	{												\
	}												\
													\
	ClassName::~##ClassName()						\
	{												\
	}												\
													\
	BEGIN_MESSAGE_MAP(ClassName, PublicClass)		\
	END_MESSAGE_MAP()

//ExtIO Dialog
DialogClass(CGainSliderCHA, CSliderCtrl)
DialogClass(CGainSliderCHB, CSliderCtrl)
DialogClass(CPhaseSliderCoarse, CSliderCtrl)
DialogClass(CPhaseSliderFine, CSliderCtrl)
DialogClass(CCheckBoxDiversity, CButton)
DialogClass(CCheckBoxSyncGain, CButton)
DialogClass(CCheckBoxSyncTune, CButton)
DialogClass(CPhaseInfo, CEdit)
DialogClass(CTransparencySlider, CSliderCtrl)
DialogClass(CCheckBoxDllIQ, CButton)
DialogClass(CCheckBoxDebugConsole, CButton)
DialogClass(CCheckBoxAGC, CButton)
DialogClass(CRFGainASlider, CSliderCtrl)
DialogClass(CRFGainBSlider, CSliderCtrl)
DialogClass(CDataRateInfo, CEdit)
DialogClass(CButton1, CButton) 

//Panadapter Dialog
DialogClass(CSpeedSlider, CSliderCtrl)
DialogClass(CRangeInfo, CEdit)
DialogClass(CActiveInfo, CEdit)