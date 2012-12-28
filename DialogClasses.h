#pragma once


// our empty class definition template. There are possibly other ways of doing this
// in c++, but this one works, so thee are no template experiments ..

#define DialogClassDef(ClassName, PublicClass)		\
	class ClassName : public PublicClass				\
	{													\
		DECLARE_DYNAMIC(ClassName)						\
														\
	public:												\
		##ClassName();									\
		virtual ~##ClassName();							\
														\
	protected:											\
														\
		DECLARE_MESSAGE_MAP()							\
	};

// ExtIO Dialog
DialogClassDef(CGainSliderCHA, CSliderCtrl)
DialogClassDef(CGainSliderCHB, CSliderCtrl)
DialogClassDef(CPhaseSliderCoarse, CSliderCtrl)
DialogClassDef(CPhaseSliderFine, CSliderCtrl) 
DialogClassDef(CCheckBoxDiversity, CButton)
DialogClassDef(CCheckBoxSyncGain, CButton)
DialogClassDef(CCheckBoxSyncTune, CButton)
DialogClassDef(CPhaseInfo, CEdit) 
DialogClassDef(CTransparencySlider, CSliderCtrl)
DialogClassDef(CCheckBoxDllIQ, CButton)
DialogClassDef(CCheckBoxDebugConsole, CButton)
DialogClassDef(CDataRateInfo, CEdit) 
DialogClassDef(CButton1, CButton) 

// Panadapter Dialog
DialogClassDef(CSpeedSlider, CSliderCtrl)
DialogClassDef(CRangeInfo, CEdit)
DialogClassDef(CActiveInfo, CEdit)
