// MainDialog.cpp : implementation file
//

#include "stdafx.h"
#include "ExtIODll.h"
#include "MainDialog.h"
#include "ExtIODialog.h"
#include "AdvancedDialog.h"

#define myARRAYSIZE(x)  (sizeof(x)/sizeof(x[0]))

extern CMainDialog* m_pmodeless;

// CMainDialog dialog

IMPLEMENT_DYNAMIC(CMainDialog, CDialog)

CMainDialog::CMainDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CMainDialog::IDD, pParent)
{

}

CMainDialog::~CMainDialog()
{
}

void CMainDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CMainDialog, CDialog)
	//ON_WM_DESTROY()
	ON_WM_CLOSE()
	ON_WM_SHOWWINDOW()
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB1, OnTabChange)
END_MESSAGE_MAP()


// CMainDialog message handlers

BOOL CMainDialog::OnInitDialog()
{
	CDialog::OnInitDialog();
	
	m_tab.SubclassWindow(*GetDlgItem(IDC_TAB1));
	// create tabs
	m_tab.InsertItem(0, "ExtIO");
	m_tab.InsertItem(1, "Advanced");

	// create tab dialogs
	m_dlg[0] = new CExtIODialog;
	m_dlg[1] = new CAdvancedDialog;
	VERIFY(m_dlg[0]->Create(IDD_EXTIODIALOG, this));
	VERIFY(m_dlg[1]->Create(IDD_ADVANCEDDIALOG, this));

	CenterWindow();

	return TRUE;
}

void CMainDialog::OnShowWindow(BOOL bShow, UINT nStatus)
{
	// initialize tabbed view - show first dialog
	ShowTabDlg(m_tab.GetCurSel());
}

void CMainDialog::ShowTabDlg(int tabSel)
{
	// hide all tab dialogs
	for(int idx = 0; idx < myARRAYSIZE(m_dlg); idx++)
		m_dlg[idx]->ShowWindow(SW_HIDE);

	// show selected one
	CRect rc;
	m_tab.GetClientRect(rc);
	m_tab.AdjustRect(FALSE, rc);
	//m_tab.ClientToScreen(rc);		Seems that we do not need this for modeless dialogs
	//								as they are already operating with screen coordinates.
	m_dlg[tabSel]->SetWindowPos(NULL, rc.left+2, rc.top+2, 0, 0, SWP_NOZORDER|SWP_NOSIZE|SWP_SHOWWINDOW|SWP_NOACTIVATE);
}


void CMainDialog::OnTabChange(NMHDR* pnmhdr, LRESULT* pResult)
{
	ShowTabDlg(m_tab.GetCurSel());
	*pResult = 0;
}

/*
void CMainDialog::OnDestroy()
{
	AfxMessageBox("OnDestroy()");

	for(int idx = 0; idx < myARRAYSIZE(m_dlg); idx++)
		delete m_dlg[idx];
}
*/

//This is needed when we are closing the window using the ExtIO button.

void CMainDialog::PostNcDestroy()
{
	CDialog::PostNcDestroy();	

	for(int idx = 0; idx < myARRAYSIZE(m_dlg); idx++)
	{
		if (m_dlg[idx])
		{
			delete m_dlg[idx];
			m_dlg[idx]=0;
		}
	}	

	delete this;
	m_pmodeless=NULL;		//xx
}

// This is needed on a case we are closing window using the [X] at the corner

void CMainDialog::OnClose()
{	
	for(int idx = 0; idx < myARRAYSIZE(m_dlg); idx++)
	{
		if (m_dlg[idx])
			m_dlg[idx]->ShowWindow(SW_HIDE);
	}

	CDialog::OnClose();
}
