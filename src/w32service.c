
#include <windows.h>
//#include <iostream.h>
#include <stdio.h>


// Source contributed by Nameless. Don't blame me, Lund.

/*

  in main, if switch is whatever, call 'InstallService' or 
'UninstallService'. if there are no switches, call 'StartedByService'

add the ftpd start code to  'ServiceThread'

and things should hopefully work.

  */


// thingies
//LPTSTR		               g_pzServiceName = NULL;
char *               g_pzServiceName = NULL;
SERVICE_STATUS_HANDLE   g_serviceStatusHandle = NULL;
BOOL		               g_runningService = FALSE;
BOOL		               g_pauseService = FALSE;
HANDLE                  g_terminateEvent = NULL;
HANDLE		            g_threadHandle = 0;				// Thread for the actual work


// These should be defined in the other sources
extern int windows_service;
void arguments(int argc, char **argv);




// crappy c style forward declaration. 
BOOL lftpdSetServiceStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint);
void	LftpdServiceMain(DWORD argc, LPTSTR *argv);
void StopService();
DWORD WINAPI ServiceThread(LPDWORD lParam);

int    realmain(int argc, char **argv);
void StartedByService();

struct ErrEntry {
	int code;
	const char* msg;
};
struct ErrEntry ErrList[] = {
   // error codes stolen from :
   // http://msdn.microsoft.com/library/default.asp?url=/library/en-us/debug/base/system_error_codes.asp
	{ 0,	"No error" },
	{ 1055,	"The service database is locked." },
	{ 1056,	"An instance of the service is already running." },
	{ 1060, "The service does not exist as an installed service." },
	{ 1061,	"The service cannot accept control messages at this time." },
	{ 1062, "The service has not been started." },
	{ 1063, "The service process could not connect to the service controller." },
	{ 1064,	"An exception occurred in the service when handling the control request." },
	{ 1065,	"The database specified does not exist." },
	{ 1066,	"The service has returned a service-specific error code." },
	{ 1067,	"The process terminated unexpectedly." },
	{ 1068,	"The dependency service or group failed to start." },
	{ 1069,	"The service did not start due to a logon failure." },
	{ 1070,	"After starting, the service hung in a start-pending state." },
	{ 1071,	"The specified service database lock is invalid." },
	{ 1072, "The service marked for deletion." },
	{ 1073, "The service already exists." },
	{ 1078,	"The name is already in use as either a service name or a service display name." },
};
const int nErrList = sizeof(ErrList) / sizeof(struct ErrEntry);



int main(int argc, char **argv) 
{

	arguments(argc, argv);

	if (windows_service) {
		StartedByService();
	} else {
		realmain(argc, argv);
	}
	exit(0);
}




void ErrorHandler(char *s, int err)
{
	FILE*		               g_pLog = NULL;
	int i;
	char *msg = NULL;
	for (i = 0; i < nErrList; ++i) {
		if (ErrList[i].code == err) {
         // report error message
			msg = ErrList[i].msg;
			break;
		}
	}
	if (i == nErrList) {
      // report unknown error
	}

	if (!g_pLog)
		g_pLog = fopen("llink.log","a");

	if (g_pLog) {
		fprintf(g_pLog, "%s failed, error code = %d : %s\n",s , err, msg ? msg : "unknown"); 
		fclose(g_pLog);
	}

    if (err)
	  ExitProcess(err);
}


void StartedByService()
{
   BOOL success;
   SERVICE_TABLE_ENTRY serviceTable[] =
   {
	   { NULL, (LPSERVICE_MAIN_FUNCTION) LftpdServiceMain},
	   { NULL, NULL}
   };

   #if 1
	{
	HANDLE hOutFile = CreateFile("stdout.log", GENERIC_READ | GENERIC_WRITE,
				     FILE_SHARE_READ | FILE_SHARE_WRITE,
				     NULL, OPEN_ALWAYS, 0, NULL);

	HANDLE hErrFile = CreateFile("stdout.log", GENERIC_READ | GENERIC_WRITE,
				     FILE_SHARE_READ | FILE_SHARE_WRITE,
				     NULL, OPEN_ALWAYS, 0, NULL);

	if (hOutFile)   SetStdHandle(STD_OUTPUT_HANDLE, hOutFile);
	if (hErrFile)   SetStdHandle(STD_ERROR_HANDLE, hErrFile);
	}
#endif


   // Default Service name
   if (!g_pzServiceName)
	   g_pzServiceName = strdup("llink");

   serviceTable->lpServiceName = g_pzServiceName;

   success = StartServiceCtrlDispatcher(serviceTable);
	if (!success)
		ErrorHandler("StartServiceCtrlDispatcher",GetLastError());
}

void ResumeService()
{
	g_pauseService = FALSE;
	ResumeThread(g_threadHandle);
}

//pauses service
void PauseService()
{
	g_pauseService = TRUE;
	SuspendThread(g_threadHandle);
}

void ServiceCtrlHandler(DWORD controlCode)
{
	DWORD currentState = SERVICE_RUNNING;
	BOOL success;

	switch(controlCode)
	{
		case SERVICE_CONTROL_STOP:
			currentState = SERVICE_STOP_PENDING;
			//notify SCM
			success = lftpdSetServiceStatus(
				SERVICE_STOP_PENDING,
				NO_ERROR,
				0,
				1,
				5000);
			StopService();
			return;
		case SERVICE_CONTROL_PAUSE:
			if (g_runningService && !g_pauseService)
			{
				//notify SCM
				success = lftpdSetServiceStatus(
					SERVICE_PAUSE_PENDING,
					NO_ERROR,
					0,
					1,
					1000);
				PauseService();
				currentState = SERVICE_PAUSED;
			}
			break;
		case SERVICE_CONTROL_CONTINUE:
			if (g_runningService && g_pauseService)
			{
				success = lftpdSetServiceStatus(
					SERVICE_CONTINUE_PENDING,
					NO_ERROR,
					0,
					1,
					1000);
				ResumeService();
				currentState = SERVICE_RUNNING;
			}
			break;
		case SERVICE_CONTROL_INTERROGATE:
			break;
			
		case SERVICE_CONTROL_SHUTDOWN:
			//do nothing
			return;
		default:
			break;
	}
	//notify SCM current state
	lftpdSetServiceStatus(currentState, NO_ERROR, 0, 0, 0);
}
void Terminate(DWORD error)
{
	//close event handle
	if (g_terminateEvent)
		CloseHandle(g_terminateEvent);

	//notify SCM service stopped
	if (g_serviceStatusHandle)
		lftpdSetServiceStatus(SERVICE_STOPPED, error, 0, 0, 0);

	//close thread handle
	if (g_threadHandle)
		CloseHandle(g_threadHandle);
}

BOOL InitService()
{
	DWORD id;

	// Start the service's thread
	g_threadHandle = CreateThread(
	NULL,
	0,
	(LPTHREAD_START_ROUTINE) ServiceThread,
	NULL,
	0,
	&id);
	
	if (g_threadHandle == 0)
		return FALSE;
	else
	{
		g_runningService = TRUE;
		return TRUE;
	}
}

void LftpdServiceMain(DWORD argc, LPTSTR *argv)
{
	BOOL success;

	// Is this needed here, in a new thread?
	if (!g_pzServiceName)
		g_pzServiceName = strdup("llink");

	g_serviceStatusHandle = RegisterServiceCtrlHandler(g_pzServiceName, (LPHANDLER_FUNCTION)ServiceCtrlHandler);
	if (!g_serviceStatusHandle)
	{
		Terminate(GetLastError());
		return;
	}


	success = lftpdSetServiceStatus(SERVICE_START_PENDING, NO_ERROR, 0 , 1, 5000);
	if (!success)
	{ 
		Terminate(GetLastError());
		return;
	}

	//create termination event
	g_terminateEvent = CreateEvent (0, TRUE, FALSE, 0);
	if (!g_terminateEvent)
	{
		Terminate(GetLastError());
		return;
	}

	success = lftpdSetServiceStatus(SERVICE_START_PENDING, NO_ERROR, 0 , 2, 1000);
	if (!success)
	{ 
		Terminate(GetLastError());
		return;
	}

	success = lftpdSetServiceStatus(SERVICE_START_PENDING, NO_ERROR, 0 , 3, 5000);
	if (!success)
	{ 
		Terminate(GetLastError());
		return;
	}

	//start service
	success = InitService();
	if (!success)
	{ 
		Terminate(GetLastError());
		return;
	}

	//notify SCM service is runnning
	success = lftpdSetServiceStatus(SERVICE_RUNNING, NO_ERROR, 0 , 0, 0);
	if (!success)
	{ 
		Terminate(GetLastError());
		return;
	}

	//wait for stop signal and then terminate
	WaitForSingleObject(g_terminateEvent, INFINITE);

	Terminate(0);
}

void GetStatus(SC_HANDLE service)
{
	SERVICE_STATUS status;	
	DWORD dwCurrentState;


	QueryServiceStatus(service, &status);


	switch(status.dwCurrentState)
	{
		case SERVICE_RUNNING:
			dwCurrentState = SERVICE_RUNNING;
			break;
		case SERVICE_STOPPED:
			dwCurrentState = SERVICE_STOPPED;
			break;
		case SERVICE_PAUSED:
			dwCurrentState = SERVICE_PAUSED;
			break;
		case SERVICE_CONTINUE_PENDING:
			dwCurrentState = SERVICE_CONTINUE_PENDING;
			break;
		case SERVICE_PAUSE_PENDING:
			dwCurrentState = SERVICE_PAUSE_PENDING;
			break;
		case SERVICE_START_PENDING:
			dwCurrentState = SERVICE_START_PENDING;
			break;
		case SERVICE_STOP_PENDING:
			dwCurrentState = SERVICE_STOP_PENDING;
			break;
		default:
         // oops
			break;
	}
	lftpdSetServiceStatus(dwCurrentState, NO_ERROR, 0, 0, 0);
}

BOOL ServiceRun() 
{ 
    SC_HANDLE scm, Service;
	 SERVICE_STATUS ssStatus; 
    DWORD dwOldCheckPoint; 
    DWORD dwStartTickCount;
    DWORD dwWaitTime;
    DWORD dwStatus;
 	
	
	scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!scm)
		ErrorHandler("OpenSCManager", GetLastError());

	
	if (!g_pzServiceName)
		g_pzServiceName = strdup("llink");


	
	Service = OpenService(scm, g_pzServiceName, SERVICE_ALL_ACCESS);
	if(!Service)
	{
		ErrorHandler("OpenService", GetLastError());
		return FALSE;
	}
	else
	{
		//start service
		StartService(Service, 0, NULL);
		GetStatus(Service);

		// Check the status until the service is no longer start pending. 
		if (!QueryServiceStatus( Service, &ssStatus) )
			ErrorHandler("QueryServiceStatus", GetLastError());
		dwStartTickCount = GetTickCount();
		dwOldCheckPoint = ssStatus.dwCheckPoint;



		while (ssStatus.dwCurrentState == SERVICE_START_PENDING) 
		{ 
         // no less than 1 second, no more than 10
			dwWaitTime = ssStatus.dwWaitHint / 10;

			if( dwWaitTime < 1000 )
				dwWaitTime = 1000;
			else if ( dwWaitTime > 10000 )
				dwWaitTime = 10000;



			Sleep( dwWaitTime );

			// Check the status again. 
			if (!QueryServiceStatus(Service, &ssStatus) )
				break; 

			if ( ssStatus.dwCheckPoint > dwOldCheckPoint )
			{
				// The service is making progress.
				dwStartTickCount = GetTickCount();
				dwOldCheckPoint = ssStatus.dwCheckPoint;
			}
			else
			{
				if(GetTickCount()-dwStartTickCount > ssStatus.dwWaitHint)
				{
					// No progress made within the wait hint
					break;
				}
			}
		}

		
		if (ssStatus.dwCurrentState == SERVICE_RUNNING) 
		{
			GetStatus(Service);
			dwStatus = NO_ERROR;
		}
		else 
		{ 
			dwStatus = GetLastError();
         // report error?
		} 	
	}


	CloseServiceHandle(scm);
   CloseServiceHandle(Service); 
   return TRUE;
}

BOOL InstallService(int argc, char **argv)
{
	SC_HANDLE newService;
	SC_HANDLE scm; 
    char szBuffer[MAX_PATH];
    char lpBuffer[MAX_PATH];
    static char lpBin[MAX_PATH];
    char szPath[MAX_PATH];
    char szCwd[MAX_PATH];
	int i, len;

	*szBuffer = 0;
	*szPath = 0;
	*szCwd = 0;

	// Default Service name
	if (!g_pzServiceName)
		g_pzServiceName = strdup("llink");

	printf("Installing Windows Service...\n");

	//GetModuleFileName( GetModuleHandle(NULL), szPath, MAX_PATH );
	GetModuleFileName( NULL, szPath, MAX_PATH );

	getcwd(szCwd, sizeof(szCwd));
#if 1
	
	for (i = 1; i < argc; i++) {
		len = strlen(szBuffer);
		_snprintf(&szBuffer[len], sizeof(szBuffer) - len,
				 " %s", argv[i]);
	}
	// Go char to wchar if UNICODE
	//MultiByteToWideChar(CP_ACP, 0, szBuffer, -1, lpBuffer, sizeof(lpBuffer));

	_snprintf(lpBin, sizeof(lpBin), "\"%s\" -P -w \"%s\" %s",
			 szPath, szCwd, szBuffer);

#endif

	//get SCM
	scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (!scm)
		ErrorHandler("OpenSCManager", GetLastError());
	

	//install service
	newService = CreateService(
		scm,
		g_pzServiceName,
		g_pzServiceName,
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START,
		SERVICE_ERROR_NORMAL,
		lpBin,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL);
	
	if(!newService)
	{
		ErrorHandler("CreateService", GetLastError());
		return FALSE;
	}
	else
	{
		arguments(argc,argv);
		ServiceRun();
	}

	//clean up
	CloseServiceHandle(newService);
	CloseServiceHandle(scm);

	return TRUE;
}

#include <fcntl.h>

DWORD WINAPI ServiceThread(LPDWORD lParam)
{
	int i;

	ErrorHandler("starting", 0);
	//_asm{int 3};
	//i = freopen("c:\\src\\llink-2.0.4\\src\\stdout.log", "w", stdout);
    //i = freopen("c:\\src\\llink-2.0.4\\src\\stderr.log", "w", stderr);
	// start the ftpd thingie here


	realmain(0, NULL);
	//loop();
	return 0;
}

BOOL UninstallService()
{
	SC_HANDLE service;
	SC_HANDLE scm;
	BOOL success;
	SERVICE_STATUS status;


	// Default Service name
	if (!g_pzServiceName)
		g_pzServiceName = strdup("llink");

	//get SCM
	scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (!scm)
		ErrorHandler("OpenSCManager", GetLastError());

	//Get service's handle
	service = OpenService(scm, g_pzServiceName, SERVICE_ALL_ACCESS | DELETE);
	if (!service)
		ErrorHandler("OpenService", GetLastError());

	success= QueryServiceStatus(service, &status);
	if (!success)
		ErrorHandler("QueryServiceStatus", GetLastError());
	
	//Stop service if necessary		
	if (status.dwCurrentState != SERVICE_STOPPED)
	{
		success= ControlService(service, SERVICE_CONTROL_STOP, &status);
		if (!success)
			ErrorHandler("ControlService", GetLastError());
		Sleep(500);
	}

	//Delete service
	success= DeleteService(service);
	if (!success)
		ErrorHandler("DeleteService", GetLastError());

	//Clean up
	CloseServiceHandle(service);
	CloseServiceHandle(scm);

	return TRUE;
}



void StopService()
{
	g_runningService = FALSE;
	//set the event that is holding ServiceMain
	SetEvent(g_terminateEvent);
}

BOOL lftpdSetServiceStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint)
{
	BOOL success;
	SERVICE_STATUS serviceStatus;

	if (!g_pzServiceName)
		g_pzServiceName = strdup("llink");

	//fill in all of the SERVICE_STATUS fields
	serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	serviceStatus.dwCurrentState = dwCurrentState;

	//if in the process of something, then accept
	//no control events, else accept anything
	if (dwCurrentState == SERVICE_START_PENDING)
		serviceStatus.dwControlsAccepted = 0;
	else
		serviceStatus.dwControlsAccepted = 
			SERVICE_ACCEPT_STOP | 
			SERVICE_ACCEPT_PAUSE_CONTINUE |
			SERVICE_ACCEPT_SHUTDOWN;

	//if a specific exit code is defines, set up the win32 exit code properly
	if (dwServiceSpecificExitCode == 0)
		serviceStatus.dwWin32ExitCode = dwWin32ExitCode;
	else
		serviceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
	
	serviceStatus.dwServiceSpecificExitCode = dwServiceSpecificExitCode;
	serviceStatus.dwCheckPoint = dwCheckPoint;
	serviceStatus.dwWaitHint = dwWaitHint;
	
	g_serviceStatusHandle = RegisterServiceCtrlHandler(g_pzServiceName, (LPHANDLER_FUNCTION)ServiceCtrlHandler);
	if (!g_serviceStatusHandle)
	{
		Terminate(GetLastError());
		return -1;
	}
	success = SetServiceStatus (g_serviceStatusHandle, &serviceStatus);

	
	if (!success)
		StopService();

	return success;
}

