// auto detect 32-bit or 64-bit OS to run, and install Capso Vision USB driver
//

#include <stdio.h>
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include "stdafx.h"

static BOOL IsWow64( void );
static BOOL installResult;

static void PrintLastError( const WCHAR* apiname );

int __cdecl
	wmain( int argc, wchar_t* argv[] )
{

    STARTUPINFO si;
    PROCESS_INFORMATION ProcessInformation;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    //wprintf( L"Running dpinst driver installer depending on the underlying platform\n" );

	installResult = false;
    WCHAR * X86 = TEXT("Usb32.EXE");
// For separate driver package, WCHAR * X86 = TEXT(“Driver32\dpinst.exe”)
    WCHAR * X64 = TEXT("Usb64.EXE");
// For separate driver package, WCHAR * X64 = TEXT(“Driver64\dpinst.exe”)

    WCHAR * AppName = NULL;
    WCHAR * command = NULL;

    if (IsWow64()) 
    {
        AppName = X64;
        //wprintf( L"We are on 64 bit OS.\n" );
    }
    else
    {
        AppName = X86;
        //wprintf( L"We are on 32 bit OS.\n" );
    }

    command = (WCHAR*)HeapAlloc( GetProcessHeap(), 0, (wcslen(AppName)+1)*sizeof(WCHAR) );
    if( command == NULL )
    {
        wprintf( L"Out of memory.\n" );
        return 0;
    }
    
    StringCchCopy( command, wcslen(AppName)+1, AppName );

    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
	si.wShowWindow=SW_HIDE;
	si.dwFlags=STARTF_USESHOWWINDOW;
    ZeroMemory( &ProcessInformation, sizeof( ProcessInformation ) );
    if( CreateProcess( 
                NULL, 
                command, 
                NULL, 
                NULL, 
                FALSE, 
                CREATE_NO_WINDOW, // CREATE_NEW_CONSOLE, //0, 
                NULL, 
                NULL, 
                &si, 
                &ProcessInformation ) ) 
    {
        //wprintf( L"CreateProcess succeeded\n" );
		installResult = true;
    }
    else
    {
        //PrintLastError( L"CreateProcess()" );        
		installResult = false;
    }
#if 0
    if( ProcessInformation.hProcess )
    {
        DWORD Ret = WaitForSingleObject( ProcessInformation.hProcess, INFINITE ); 

        if( WAIT_OBJECT_0 == Ret )
		{
            //wprintf(L"Process finished.\n" );
		}
		else
		{
            //PrintLastError( L"WaitForSingleObject");
		}
        DWORD ExitCode;

        if( GetExitCodeProcess( ProcessInformation.hProcess, &ExitCode ) )
		{
            //wprintf( L"Process exit code = 0x%X\n", ExitCode );
		}
		else
		{
            //PrintLastError( L"GetExitCodeProcess()" );            
		}
        GetExitCodeThread( ProcessInformation.hThread, &ExitCode );
        //wprintf( L"Thread exit code = 0x%X\n", ExitCode );
        
        CloseHandle( ProcessInformation.hProcess );
        CloseHandle( ProcessInformation.hThread );
    }
    else
    {
        //wprintf( L"No process created\n");
    }
#endif
    if( command ) 
        HeapFree( GetProcessHeap(), 0, command );
    
    return installResult;
}

typedef UINT (WINAPI* GETSYSTEMWOW64DIRECTORY)(LPTSTR, UINT);
BOOL
IsWow64(
    void
    )
{
#ifdef _WIN64
    return FALSE;

#else
    GETSYSTEMWOW64DIRECTORY getSystemWow64Directory;
    HMODULE hKernel32;
    TCHAR Wow64Directory[MAX_PATH];

	// GetModuleHandle() - Retrieves a module handle for the specified module. The module must have been loaded by the calling process
    hKernel32 = GetModuleHandle(TEXT("kernel32.dll"));	
    if (hKernel32 == NULL) {
        //
        // This shouldn't happen, but if we can't get 
        // kernel32's module handle then assume we are 
        //on x86. We won't ever install 32-bit drivers
        // on 64-bit machines, we just want to catch it 
        // up front to give users a better error message.
        //
        return FALSE;
    }

	// GetProcAddress() - Retrieves the address of an exported function or variable from the specified dynamic-link library (DLL)
    getSystemWow64Directory = (GETSYSTEMWOW64DIRECTORY)
        GetProcAddress(hKernel32, "GetSystemWow64DirectoryW");	

    if (getSystemWow64Directory == NULL) {
        //
        // This most likely means we are running 
        // on Windows 2000, which didn't have this API 
        // and didn't have a 64-bit counterpart.
        //
        return FALSE;
    }

	// getSystemWow64Directory() - Retrieves the path of the system directory used by WOW64. This directory is not present on 32-bit Windows
    if ((getSystemWow64Directory(Wow64Directory, sizeof(Wow64Directory)/sizeof(TCHAR)) == 0) &&	
        (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)) {
        return FALSE;
    }
    
    //
    // GetSystemWow64Directory succeeded 
    // so we are on a 64-bit OS.
    //
    return TRUE;
#endif
}

// debug purpose: print error message and install status
void
PrintLastError( const WCHAR* apiname )
{
    DWORD error = GetLastError();
    LPVOID lpvMessageBuffer;
    if( FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                  NULL, error, 
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), //The user default language
                  (LPTSTR)&lpvMessageBuffer, 0, NULL) )
    {
        _tprintf( TEXT("ERROR: %s: 0x%X: %s\n"), apiname, error, lpvMessageBuffer );
        LocalFree(lpvMessageBuffer);
    }
}
