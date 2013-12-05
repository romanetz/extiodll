
#include "stdafx.h"
#include "ExtIODll.h"
#include "ExtIOFunctions.h"

#include <conio.h>				// gives _cprintf()

bool hasconsole=false;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//
//	Note!
//
//		If this DLL is _dynamically_ linked against the MFC
//		DLLs, any functions exported from this DLL which
//		call into MFC must have the AFX_MANAGE_STATE macro
//		added at the very beginning of the function.
//
//		For example:
//
//		extern "C" BOOL PASCAL EXPORT ExportedFunction()
//		{
//			AFX_MANAGE_STATE(AfxGetStaticModuleState());
//			// normal function body here
//		}
//
//		It is very important that this macro appear in each
//		function, prior to any calls into MFC.  This means that
//		it must appear as the first statement within the 
//		function, even before any object variable declarations
//		as their constructors may generate calls into the MFC
//		DLL.
//
//		Please see MFC Technical Notes 33 and 58 for additional
//		details.
//

/////////////////////////////////////////////////////////////////////////////
// CMyDllApp

BEGIN_MESSAGE_MAP(CExtIODllApp, CWinApp)
	//{{AFX_MSG_MAP(CMyDllApp)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMyDllApp construction

CExtIODllApp::CExtIODllApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance	 
}

HANDLE sleepevent;

BOOL CExtIODllApp::InitInstance()
{
int debugcon, transp;

	CWinApp::InitInstance();	

	SetRegistryKey("Satrian");

	//not nice to fetch these parameters here from the registry, but we eant this to be done before anything else starts, so there is no way really.
	//All other registry loading is at the InitHW()
	
	debugcon = AfxGetApp()->GetProfileInt(_T("Config"), _T("DebugConsole"), 0);
	transp = AfxGetApp()->GetProfileInt(_T("Config"), _T("Transparency"), 90);		//default transparency is 90%

	//--------------
	// Create console. This is convenient for _cprintf() debugging, but as we will 
	// set up this as a transparent window, it may also be of use for other things.
	//--------------
	if (debugcon)
	{
		if (!AllocConsole())
		{
			hasconsole=false;
			AfxMessageBox("Failed to create the console!", MB_ICONEXCLAMATION);
		}
		else
		{
		HWND hWnd;

			hasconsole=true;
			HANDLE nConsole = GetStdHandle(STD_OUTPUT_HANDLE);
			SetConsoleTextAttribute(nConsole, 0x0002|0x0008);	//green|white to get the bright color
			SetConsoleTitle("ExtIO DLL Console");

			hWnd=GetConsoleWindow();
			SetWindowLong(hWnd, GWL_EXSTYLE, ::GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);		// add layered attribute
			SetLayeredWindowAttributes(hWnd, 0, 255 * transp /*percent*//100, LWA_ALPHA);				// set transparency
			
			//Have to disable close button, as this will kill the application instance with no questions asked!
			//Note, that application is still terminated when the consle is closed from taskbar.
			DeleteMenu(GetSystemMenu(hWnd, false), SC_CLOSE, MF_BYCOMMAND);

			_cprintf("InitInstance(): Console initialized\n");
		}
	}
/*
	{
	DWORD dwError, dwThreadPri;

		dwThreadPri = GetThreadPriority();
		_cprintf("Current thread priority is %d\n", dwThreadPri);

		if(!SetThreadPriority(THREAD_MODE_BACKGROUND_BEGIN))
		{
			dwError = GetLastError();
			if( ERROR_THREAD_MODE_ALREADY_BACKGROUND == dwError)
				_cprintf("Already in background mode\n");
			else 
				_cprintf("Failed to enter background mode (%d)\n", dwError);
		} 
	}

	dwThreadPri = GetThreadPriority();
	_cprintf("Thread priority is set to %d\n", dwThreadPri);
*/
	sleepevent = CreateEvent(NULL, FALSE, FALSE, NULL);		// we are using that instead of sleep(), as it is more kind to overall system resources

	_cprintf("Initializing memwatch ..\n");
	mwInit();

	return TRUE;
}

int CExtIODllApp::ExitInstance() 
{
	mwTerm();

	//	deallocate console
	if (hasconsole)
	{
		AfxMessageBox("All done. Check the console now or press OK to continue closing application", MB_ICONINFORMATION); 

		if (!FreeConsole())
			AfxMessageBox("Could not free the console!");

		hasconsole=false;		//just in case update flag
	}

	return CWinApp::ExitInstance();
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CMyDllApp object

CExtIODllApp theApp;


