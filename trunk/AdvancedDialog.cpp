// AdvancedDialog.cpp : implementation file
//

#include "stdafx.h"
#include "ExtIODll.h"
#include "AdvancedDialog.h"


// CAdvancedDialog dialog

IMPLEMENT_DYNAMIC(CAdvancedDialog, CDialog)

CAdvancedDialog::CAdvancedDialog(CWnd* pParent /*=NULL*/)
	: CDialog(CAdvancedDialog::IDD, pParent)
{

}

CAdvancedDialog::~CAdvancedDialog()
{
}

void CAdvancedDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CAdvancedDialog, CDialog)
END_MESSAGE_MAP()


// CAdvancedDialog message handlers
