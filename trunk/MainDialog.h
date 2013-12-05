#pragma once


// CMainDialog dialog

class CMainDialog : public CDialog
{
	DECLARE_DYNAMIC(CMainDialog)

public:

	CMainDialog(CWnd* pParent = NULL);   // standard constructor
	virtual ~CMainDialog();

// Dialog Data
	enum { IDD = IDD_DIALOG3 };

protected:

	CTabCtrl m_tab;
	CDialog* m_dlg[2];

	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();
	void ShowTabDlg(int tabSel);
	void OnTabChange(NMHDR* pnmhdr, LRESULT* pResult);
	virtual void PostNcDestroy();

	afx_msg void OnClose();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);

	DECLARE_MESSAGE_MAP()
};
