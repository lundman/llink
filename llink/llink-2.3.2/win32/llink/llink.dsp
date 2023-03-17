# Microsoft Developer Studio Project File - Name="llink" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=llink - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "llink.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "llink.mak" CFG="llink - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "llink - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "llink - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "llink - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "../../lion/" /D "NDEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /D strncasecmp=strnicmp /D strcasecmp=stricmp /D strtoull=_strtoui64 /YX /FD /c
# ADD BASE RSC /l 0x809 /d "NDEBUG"
# ADD RSC /l 0x809 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 libeay32s.lib SSLeay32s.lib wsock32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386 /out:"../../src/llink.exe" /libpath:"$(SSL)\out32dll\Release"

!ELSEIF  "$(CFG)" == "llink - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../../lion/" /D "DEBUG" /D "_DEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /D strncasecmp=strnicmp /D strcasecmp=stricmp /D strtoull=_strtoui64 /YX /FD /GZ /c
# ADD BASE RSC /l 0x809 /d "_DEBUG"
# ADD RSC /l 0x809 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 wsock32.lib libeay32s.lib SSLeay32s.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /out:"../../src/llink.exe" /pdbtype:sept /libpath:"$(SSL)\out32dll\Debug"

!ENDIF 

# Begin Target

# Name "llink - Win32 Release"
# Name "llink - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\..\src\bookmark.c
# End Source File
# Begin Source File

SOURCE=..\..\src\conf.c
# End Source File
# Begin Source File

SOURCE=..\..\src\debug.c
# End Source File
# Begin Source File

SOURCE=..\..\src\external.c
# End Source File
# Begin Source File

SOURCE=..\..\src\file.c
# End Source File
# Begin Source File

SOURCE=..\..\src\getopt.c
# End Source File
# Begin Source File

SOURCE=..\..\src\httpd.c
# End Source File
# Begin Source File

SOURCE=..\..\src\main.c
# End Source File
# Begin Source File

SOURCE=..\..\src\mime.c
# End Source File
# Begin Source File

SOURCE=..\..\src\parser.c
# End Source File
# Begin Source File

SOURCE=..\..\src\process.c
# End Source File
# Begin Source File

SOURCE=..\..\src\query.c
# End Source File
# Begin Source File

SOURCE=..\..\src\request.c
# End Source File
# Begin Source File

SOURCE=..\..\src\root.c
# End Source File
# Begin Source File

SOURCE=..\..\src\skin.c
# End Source File
# Begin Source File

SOURCE=..\..\src\ssdp.c
# End Source File
# Begin Source File

SOURCE=..\..\src\win32.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\src\bookmark.h
# End Source File
# Begin Source File

SOURCE=..\..\src\conf.h
# End Source File
# Begin Source File

SOURCE=..\..\src\debug.h
# End Source File
# Begin Source File

SOURCE=..\..\src\external.h
# End Source File
# Begin Source File

SOURCE=..\..\src\file.h
# End Source File
# Begin Source File

SOURCE=..\..\src\httpd.h
# End Source File
# Begin Source File

SOURCE=..\..\src\mime.h
# End Source File
# Begin Source File

SOURCE=..\..\src\parser.h
# End Source File
# Begin Source File

SOURCE=..\..\src\process.h
# End Source File
# Begin Source File

SOURCE=..\..\src\query.h
# End Source File
# Begin Source File

SOURCE=..\..\src\request.h
# End Source File
# Begin Source File

SOURCE=..\..\src\root.h
# End Source File
# Begin Source File

SOURCE=..\..\src\skin.h
# End Source File
# Begin Source File

SOURCE=..\..\src\ssdp.h
# End Source File
# Begin Source File

SOURCE=..\..\src\version.h
# End Source File
# Begin Source File

SOURCE=..\..\src\win32.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# Begin Source File

SOURCE=..\..\src\llink.conf
# End Source File
# End Target
# End Project
