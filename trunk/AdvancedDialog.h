#pragma once


// CAdvancedDialog dialog

class CAdvancedDialog : public CDialog
{
	DECLARE_DYNAMIC(CAdvancedDialog)

public:
	CAdvancedDialog(CWnd* pParent = NULL);   // standard constructor
	virtual ~CAdvancedDialog();

// Dialog Data
	enum { IDD = IDD_ADVANCEDDIALOG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
};
