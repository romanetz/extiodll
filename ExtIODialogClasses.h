#pragma once


// our empty class definition template. There are possibly other ways of doing this
// in c++, but this one works, so thee are no template experiments ..

#define ExtIODialogClassDef(ClassName, PublicClass)		\
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


ExtIODialogClassDef(CGainSliderCHA, CSliderCtrl)
ExtIODialogClassDef(CGainSliderCHB, CSliderCtrl)
ExtIODialogClassDef(CPhaseSliderCoarse, CSliderCtrl)
ExtIODialogClassDef(CPhaseSliderFine, CSliderCtrl) 
ExtIODialogClassDef(CCheckBoxDiversity, CButton)
ExtIODialogClassDef(CCheckBoxSyncGain, CButton)
ExtIODialogClassDef(CCheckBoxSyncTune, CButton)
ExtIODialogClassDef(CPhaseInfo, CEdit) 
ExtIODialogClassDef(CTransparencySlider, CSliderCtrl)
ExtIODialogClassDef(CCheckBoxDllIQ, CButton)
ExtIODialogClassDef(CCheckBoxDebugConsole, CButton)
ExtIODialogClassDef(CDataRateInfo, CEdit) 
