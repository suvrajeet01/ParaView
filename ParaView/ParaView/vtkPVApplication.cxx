/*=========================================================================

  Program:   ParaView
  Module:    vtkPVApplication.cxx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

Copyright (c) 2000-2001 Kitware Inc. 469 Clifton Corporate Parkway,
Clifton Park, NY, 12065, USA.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name of Kitware nor the names of any contributors may be used
   to endorse or promote products derived from this software without specific 
   prior written permission.

 * Modified source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=========================================================================*/
#include "vtkPVApplication.h"

#include "vtkToolkits.h"
#include "vtkPVConfig.h"
#ifdef VTK_USE_MPI
#include "vtkMPIController.h"
#include "vtkMPICommunicator.h"
#include "vtkMPIGroup.h"
#endif

#include "vtkCallbackCommand.h"
#include "vtkCharArray.h"
#include "vtkDataSet.h"
#include "vtkDirectory.h"
#include "vtkDoubleArray.h"
#include "vtkFloatArray.h"
#include "vtkIntArray.h"
#include "vtkKWDialog.h"
#include "vtkKWEvent.h"
#include "vtkKWLabeledFrame.h"
#include "vtkKWLoadSaveDialog.h"
#include "vtkPVTraceFileDialog.h"
#include "vtkKWSplashScreen.h"
#include "vtkKWWindowCollection.h"
#include "vtkLongArray.h"
#include "vtkMapper.h"
#include "vtkMultiProcessController.h"
#include "vtkObjectFactory.h"
#include "vtkOutputWindow.h"
#include "vtkPVRenderView.h"
#include "vtkPVWindow.h"
#include "vtkPolyDataMapper.h"
#include "vtkProbeFilter.h"
#include "vtkRenderWindow.h"
#include "vtkRenderer.h"
#include "vtkShortArray.h"
#include "vtkString.h"
#include "vtkTclUtil.h"
#include "vtkTimerLog.h"
#include "vtkUnsignedCharArray.h"
#include "vtkUnsignedIntArray.h"
#include "vtkUnsignedLongArray.h"
#include "vtkUnsignedShortArray.h"
#include "vtkPolyData.h"
#include "vtkPVHelpPaths.h"
#include "vtkPVSourceInterfaceDirectories.h"
#include "vtkPVRenderGroupDialog.h"

#include <sys/stat.h>
#include <stdarg.h>
#include <signal.h>

#ifdef _WIN32
#include "vtkKWRegisteryUtilities.h"

#include "ParaViewRC.h"

#include "htmlhelp.h"
#include "direct.h"
#endif

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPVApplication);
vtkCxxRevisionMacro(vtkPVApplication, "1.156.2.14");

int vtkPVApplicationCommand(ClientData cd, Tcl_Interp *interp,
                            int argc, char *argv[]);

//----------------------------------------------------------------------------
extern "C" int Vtktkrenderwidget_Init(Tcl_Interp *interp);
extern "C" int Vtkkwparaviewtcl_Init(Tcl_Interp *interp);

vtkPVApplication* vtkPVApplication::MainApplication = 0;

static void vtkPVAppProcessMessage(vtkObject* vtkNotUsed(object),
                                   unsigned long vtkNotUsed(event), 
                                   void *clientdata, void *calldata)
{
  vtkPVApplication *self = static_cast<vtkPVApplication*>( clientdata );
  const char* message = static_cast<char*>( calldata );
  cout << "# Error or warning: " << message << endl;
  self->AddTraceEntry("# Error or warning:");
  int cc;
  ostrstream str;
  for ( cc= 0; cc < vtkString::Length(message); cc ++ )
    {
    str << message[cc];
    if ( message[cc] == '\n' )
      {
      str << "# ";
      }
    }
  str << ends;
  self->AddTraceEntry("# %s\n#", str.str());
  //cout << "# " << str.str() << endl;
  str.rdbuf()->freeze(0);
}

// initialze the class variables
int vtkPVApplication::GlobalLODFlag = 0;

// Output window which prints out the process id
// with the error or warning messages
class VTK_EXPORT vtkPVOutputWindow : public vtkOutputWindow
{
public:
  vtkTypeMacro(vtkPVOutputWindow,vtkOutputWindow);
  
  static vtkPVOutputWindow* New();

  void DisplayText(const char* t)
  {
    if ( this->Windows && this->Windows->GetNumberOfItems() &&
         this->Windows->GetLastKWWindow() )
      {
      vtkKWWindow *win = this->Windows->GetLastKWWindow();
      char buffer[1024];      
      const char *message = strstr(t, "): ");
      char type[1024], file[1024];
      int line;
      sscanf(t, "%[^:]: In %[^,], line %d", type, file, &line);
      if ( message )
        {
        int error = 0;
        if ( !strncmp(t, "ERROR", 5) )
          {
          error = 1;
          }
        message += 3;
        char *rmessage = vtkString::Duplicate(message);
        int last = vtkString::Length(rmessage)-1;
        while ( last > 0 && 
                (rmessage[last] == ' ' || rmessage[last] == '\n' || 
                 rmessage[last] == '\r' || rmessage[last] == '\t') )
          {
          rmessage[last] = 0;
          last--;
          }
        sprintf(buffer, "There was a VTK %s in file: %s (%d)\n %s", 
                (error ? "Error" : "Warning"),
                file, line,
                rmessage);
        if ( error )
          {
          win->ErrorMessage(buffer);
          }
        else 
          {
          win->WarningMessage(buffer);
          }
        delete [] rmessage;
        }
      }
  }

  vtkPVOutputWindow()
  {
    this->Windows = 0;
  }
  
  void SetWindowCollection(vtkKWWindowCollection *windows)
  {
    this->Windows = windows;
  }

protected:
  vtkKWWindowCollection *Windows;

private:
  vtkPVOutputWindow(const vtkPVOutputWindow&);
  void operator=(const vtkPVOutputWindow&);
};

vtkStandardNewMacro(vtkPVOutputWindow);

Tcl_Interp *vtkPVApplication::InitializeTcl(int argc, char *argv[])
{

  Tcl_Interp *interp = vtkKWApplication::InitializeTcl(argc,argv);
  
  //  if (Vtkparalleltcl_Init(interp) == TCL_ERROR) 
  //  {
   // cerr << "Init Parallel error\n";
   // }

  // Why is this here?  Doesn't the superclass initialize this?
  if (vtkKWApplication::GetWidgetVisibility())
    {
    Vtktkrenderwidget_Init(interp);
    }
   
  Vtkkwparaviewtcl_Init(interp);
  
  // Create the component loader procedure in Tcl.
  char* script = vtkString::Duplicate(vtkPVApplication::LoadComponentProc);  
  if (Tcl_GlobalEval(interp, script) != TCL_OK)
    {
    // ????
    }  
  delete [] script;
  
  return interp;
}

//----------------------------------------------------------------------------
vtkPVApplication::vtkPVApplication()
{
  this->AboutDialog = 0;
  this->Display3DWidgets = 0;
  this->ProcessId = 0;
  this->RunningParaViewScript = 0;

  char name[128];
  this->CommandFunction = vtkPVApplicationCommand;
  this->MajorVersion = 0;
  this->MinorVersion = 6;
  this->SetApplicationName("ParaView");
  sprintf(name, "ParaView%d.%d", this->MajorVersion, this->MinorVersion);
  this->SetApplicationVersionName(name);
  this->SetApplicationReleaseName("1");

  this->Controller = NULL;
  this->NumberOfPipes = 1;

  this->UseRenderingGroup = 0;
  this->GroupFileName = 0;

  this->UseTiledDisplay = 0;
  this->TileDimensions[0] = this->TileDimensions[1] = 1;

  // GUI style & consistency

  vtkKWLabeledFrame::AllowShowHideOn();
  vtkKWLabeledFrame::SetLabelCaseToLowercaseFirst();
  vtkKWLabeledFrame::BoldLabelOn();
  
  // The following is necessary to make sure that the tcl object
  // created has the right command function. Without this,
  // the tcl object has the vtkKWApplication's command function
  // since it is first created in vtkKWApplication's constructor
  // (in vtkKWApplication's constructor GetClassName() returns
  // the wrong value because the virtual table is not setup yet)
  char* tclname = vtkString::Duplicate(this->GetTclName());
  vtkTclUpdateCommand(this->MainInterp, tclname, this);
  delete[] tclname;

  this->HasSplashScreen = 1;
  if (this->HasRegisteryValue(
    2, "RunTime", VTK_KW_SPLASH_SCREEN_REG_KEY))
    {
    this->ShowSplashScreen = this->GetIntRegisteryValue(
      2, "RunTime", VTK_KW_SPLASH_SCREEN_REG_KEY);
    }
  else
    {
    this->ShowSplashScreen = 1;
    }

  this->TraceFileName = 0;
  this->Argv0 = 0;
}

//----------------------------------------------------------------------------
vtkPVApplication::~vtkPVApplication()
{
  if ( this->AboutDialog )
    {
    this->AboutDialog->Delete();
    this->AboutDialog = 0;
    }
  this->SetController(NULL);
  if ( this->TraceFile )
    {
    delete this->TraceFile;
    this->TraceFile = 0;
    }
  if (this->TraceFileName)
    {
    unlink(this->TraceFileName);
    }
  this->SetGroupFileName(0);
  this->SetTraceFileName(0);
  this->SetArgv0(0);
}


//----------------------------------------------------------------------------
vtkPVWindow *vtkPVApplication::GetMainWindow()
{
  this->Windows->InitTraversal();
  return (vtkPVWindow*)(this->Windows->GetNextItemAsObject());
}


//----------------------------------------------------------------------------
void vtkPVApplication::SetController(vtkMultiProcessController *c)
{
  if (this->Controller == c)
    {
    return;
    }

  if (c)
    {
    c->Register(this);
    this->ProcessId = c->GetLocalProcessId();
    }
  if (this->Controller)
    {
    this->Controller->UnRegister(this);
    }
  

  this->Controller = c;

  this->NumberOfPipes = 1;
}


//----------------------------------------------------------------------------
void vtkPVApplication::RemoteScript(int id, char *format, ...)
{
  char event[1600];
  char* buffer = event;
  
  va_list ap;
  va_start(ap, format);
  int length = this->EstimateFormatLength(format, ap);
  va_end(ap);
  
  if(length > 1599)
    {
    buffer = new char[length+1];
    }
  
  va_list var_args;
  va_start(var_args, format);
  vsprintf(buffer, format, var_args);
  va_end(var_args);  
  
  this->RemoteSimpleScript(id, buffer);
  
  if(buffer != event)
    {
    delete [] buffer;
    }
}
//----------------------------------------------------------------------------
void vtkPVApplication::RemoteSimpleScript(int remoteId, const char *str)
{
  int length;

  // send string to evaluate.
  length = vtkString::Length(str) + 1;
  if (length <= 1)
    {
    return;
    }

  if (this->Controller->GetLocalProcessId() == remoteId)
    {
    this->SimpleScript(str);
    return;
    }
  
  this->Controller->TriggerRMI(remoteId, const_cast<char*>(str), 
                               VTK_PV_SLAVE_SCRIPT_RMI_TAG);
}

//----------------------------------------------------------------------------
void vtkPVApplication::BroadcastScript(char *format, ...)
{
  char event[1600];
  char* buffer = event;
  
  va_list ap;
  va_start(ap, format);
  int length = this->EstimateFormatLength(format, ap);
  va_end(ap);
  
  if(length > 1599)
    {
    buffer = new char[length+1];
    }
  
  va_list var_args;
  va_start(var_args, format);
  vsprintf(buffer, format, var_args);
  va_end(var_args);
  
  this->BroadcastSimpleScript(buffer);
  
  if(buffer != event)
    {
    delete [] buffer;
    }
}

//----------------------------------------------------------------------------
void vtkPVApplication::BroadcastSimpleScript(const char *str)
{
  int id, num;
  
  num = this->Controller->GetNumberOfProcesses();

  int len = vtkString::Length(str);
  if (!str || (len < 1))
    {
    return;
    }

  for (id = 1; id < num; ++id)
    {
    this->RemoteSimpleScript(id, str);
    }
  
  // Do reverse order, because 0 will block.
  this->SimpleScript(str);
}

//----------------------------------------------------------------------------
int vtkPVApplication::AcceptLicense()
{
  return 1;
}

//----------------------------------------------------------------------------
int vtkPVApplication::AcceptEvaluation()
{
  return 1;
}

//----------------------------------------------------------------------------
int vtkPVApplication::PromptRegistration(char* vtkNotUsed(name), 
                                         char* vtkNotUsed(IDS))
{
  return 1;
}

//----------------------------------------------------------------------------
int vtkPVApplication::CheckRegistration()
{
  return 1;
}

//----------------------------------------------------------------------------
int vtkPVApplication::CheckForArgument(int argc, char* argv[], 
                                       const char* arg, int& index)
{
  if (!arg)
    {
    return VTK_ERROR;
    }

  int i;
  int retVal = VTK_ERROR;
  for (i=0; i < argc; i++)
    {
    char* newarg = vtkString::Duplicate(argv[i]);
    int len = (int)(strlen(newarg));
    for (int j=0; j<len; j++)
      {
      if (newarg[j] == '=')
        {
        newarg[j] = '\0';
        }
      }
    if (newarg && strcmp(arg, newarg) == 0)
      {
      index = i;
      retVal = VTK_OK;
      delete[] newarg;
      break;
      }
    delete[] newarg;
    }
  return retVal;
}

const char vtkPVApplication::ArgumentList[vtkPVApplication::NUM_ARGS][128] = 
{ "--start-empty" , "-e", 
  "Start ParaView without any default modules.", 
  "--disable-registry", "-dr", 
  "Do not use registry when running ParaView (for testing).", 
#ifdef VTK_USE_MPI
  "--use-rendering-group", "-p",
  "Use a subset of processes to render.",
  "--group-file", "-gf",
  "--group-file=fname where fname is the name of the input file listing number of processors to render on.",
  "--use-tiled-display", "-td",
  "Duplicate the final data to all nodes and tile node displays 1-N into one large display.",
  "--tile-dimensions-x", "-tdx",
  "-tdx=X where X is number of displays in each row of the display.",
  "--tile-dimensions-y", "-tdy",
  "-tdy=Y where Y is number of displays in each column of the display.",
#endif
#ifdef VTK_MANGLE_MESA
  "--use-software-rendering", "-r", 
  "Use software (Mesa) rendering (supports off-screen rendering).", 
  "--use-satellite-software", "-s", 
  "Use software (Mesa) rendering (supports off-screen rendering) only on satellite processes.", 
#endif
  "--play-demo", "-pd",
  "Run the ParaView demo.",
  "--help", "",
  "Displays available command line arguments.",
  "" 
};

char* vtkPVApplication::CreateHelpString()
{
  ostrstream error;
  error << "Valid arguments are: " << endl;

  int j=0;
  const char* argument1 = vtkPVApplication::ArgumentList[j];
  const char* argument2 = vtkPVApplication::ArgumentList[j+1];
  const char* help = vtkPVApplication::ArgumentList[j+2];
  while (argument1 && argument1[0])
    {
    error << argument1;
    if (argument2[0])
      {
      error << ", " << argument2;
      }
    error << " : " << help << endl;
    j += 3;
    argument1 = vtkPVApplication::ArgumentList[j];
    if (argument1 && argument1[0]) 
      {
      argument2 = vtkPVApplication::ArgumentList[j+1];
      help = vtkPVApplication::ArgumentList[j+2];
      }
    }
  error << ends;
  return error.str();
  
}

int vtkPVApplication::IsParaViewScriptFile(const char* arg)
{
  if (!arg || strlen(arg) < 4)
    {
    return 0;
    }
  if (strcmp(arg + strlen(arg) - 4,".pvs") == 0)
    {
    return 1;
    }
  return 0;
}


//----------------------------------------------------------------------------
void vtkPVApplication::SetEnvironmentVariable(const char* str)
{ 
  char* envstr = vtkString::Duplicate(str);
  putenv(envstr);
}

//----------------------------------------------------------------------------
int vtkPVApplication::CheckForTraceFile(char* name, unsigned int maxlen)
{
  char buf[256];
  if ( vtkDirectory::GetCurrentWorkingDirectory(buf, 256) )
    {
    vtkDirectory* dir = vtkDirectory::New();
    if ( !dir->Open(buf) )
      {
      dir->Delete();
      return 0;
      }
    int retVal = 0;
    int numFiles = dir->GetNumberOfFiles();
    int len = strlen("ParaViewTrace");
    for(int i=0; i<numFiles; i++)
      {
      const char* fname = dir->GetFile(i);
      if ( strncmp(fname, "ParaViewTrace", len) == 0 )
        {
        retVal = 1;
        strncpy(name, fname, maxlen);
        break;
        }
      }
    dir->Delete();
    return retVal;
    }
  else
    {
    return 0;
    }
}

//----------------------------------------------------------------------------
void vtkPVApplication::SaveTraceFile(const char* fname)
{
  vtkKWLoadSaveDialog* exportDialog = vtkKWLoadSaveDialog::New();
  this->GetMainWindow()->RetrieveLastPath(exportDialog, "SaveTracePath");
  exportDialog->Create(this, 0);
  exportDialog->SaveDialogOn();
  exportDialog->SetTitle("Save ParaView Trace");
  exportDialog->SetDefaultExt(".pvs");
  exportDialog->SetFileTypes("{{ParaView Scripts} {.pvs}} {{All Files} {.*}}");
  if ( exportDialog->Invoke() && 
       vtkString::Length(exportDialog->GetFileName())>0 )
    {
    if (rename(fname, exportDialog->GetFileName()) != 0)
      {
      vtkKWMessageDialog::PopupMessage(
        this->GetApplication(), this->GetMainWindow(),
        "Error Saving", "Could not save trace file.",
        vtkKWMessageDialog::ErrorIcon);
      }
    else
      {
      this->GetMainWindow()->SaveLastPath(exportDialog, "SaveTracePath");
      }
    }
  exportDialog->Delete();
}

//----------------------------------------------------------------------------
void vtkPVApplication::Start(int argc, char*argv[])
{
  vtkOutputWindow::GetInstance()->PromptUserOn();

  // set the font size to be small
#ifdef _WIN32
  this->Script("option add *font {{Tahoma} 8}");
#else
  // Specify a font only if there isn't one in the database
  this->Script(
    "toplevel .tmppvwindow -class ParaView;"
    "if {[option get .tmppvwindow font ParaView] == \"\"} {"
    "option add *font "
    "-adobe-helvetica-medium-r-normal--12-120-75-75-p-67-iso8859-1};"
    "destroy .tmppvwindow");
  this->Script("option add *highlightThickness 0");
  this->Script("option add *highlightBackground #ccc");
  this->Script("option add *activeBackground #eee");
  this->Script("option add *activeForeground #000");
  this->Script("option add *background #ccc");
  this->Script("option add *foreground #000");
  this->Script("option add *Entry.background #ffffff");
  this->Script("option add *Text.background #ffffff");
  this->Script("option add *Button.padX 6");
  this->Script("option add *Button.padY 3");
  this->Script("option add *selectColor #666");
#endif


  int i;
  for (i=1; i < argc; i++)
    {
    if ( vtkPVApplication::IsParaViewScriptFile(argv[i]) )
      {
      this->RunningParaViewScript = 1;
      break;
      }
    }

  if (!this->RunningParaViewScript)
    {
    for (i=1; i < argc; i++)
      {
      int valid=0;
      if (argv[i])
        {
        int  j=0;
        const char* argument1 = vtkPVApplication::ArgumentList[j];
        const char* argument2 = vtkPVApplication::ArgumentList[j+1];
        while (argument1 && argument1[0])
          {

          char* newarg = vtkString::Duplicate(argv[i]);
          int len = (int)(strlen(newarg));
          for (int i=0; i<len; i++)
            {
            if (newarg[i] == '=')
              {
              newarg[i] = '\0';
              }
            }

          if ( strcmp(newarg, argument1) == 0 || 
               strcmp(newarg, argument2) == 0)
            {
            valid = 1;
            }
          delete[] newarg;
          j += 3;
          argument1 = vtkPVApplication::ArgumentList[j];
          if (argument1 && argument1[0]) 
            {
            argument2 = vtkPVApplication::ArgumentList[j+1];
            }
          }
        }
      if (!valid)
        {
        char* error = this->CreateHelpString();
        vtkErrorMacro("Unrecognized argument " << argv[i] << "." << endl
                      << error);
        delete[] error;
        this->Exit();
        return;
        }
      }
    }

  int index=-1;

  if ( vtkPVApplication::CheckForArgument(argc, argv, "--help",
                                          index) == VTK_OK )
    {
    char* error = this->CreateHelpString();
    vtkWarningMacro(<<error);
    delete[] error;
    this->Exit();
    return;
    }

  int playDemo=0;
  if ( vtkPVApplication::CheckForArgument(argc, argv, "--play-demo",
                                          index) == VTK_OK ||
       vtkPVApplication::CheckForArgument(argc, argv, "-pd",
                                          index) == VTK_OK )
    {
    playDemo = 1;
    }

  if ( vtkPVApplication::CheckForArgument(argc, argv, "--disable-registry",
                                          index) == VTK_OK ||
       vtkPVApplication::CheckForArgument(argc, argv, "-dr",
                                          index) == VTK_OK )
    {
    this->RegisteryLevel = 0;
    }

#ifdef VTK_USE_MPI
  if ( vtkPVApplication::CheckForArgument(argc, argv, "--use-rendering-group",
                                          index) == VTK_OK ||
       vtkPVApplication::CheckForArgument(argc, argv, "-p",
                                          index) == VTK_OK )
    {
    this->UseRenderingGroup = 1;
    }

  if ( vtkPVApplication::CheckForArgument(argc, argv, "--group-file",
                                          index) == VTK_OK ||
       vtkPVApplication::CheckForArgument(argc, argv, "-gf",
                                          index) == VTK_OK )
    {
    const char* newarg=0;

    int len = (int)(strlen(argv[index]));
    for (int i=0; i<len; i++)
      {
      if (argv[index][i] == '=')
        {
        newarg = &(argv[index][i+1]);
        }
      }
    this->SetGroupFileName(newarg);
    }

  if ( vtkPVApplication::CheckForArgument(argc, argv, "--use-tiled-display",
                                          index) == VTK_OK ||
       vtkPVApplication::CheckForArgument(argc, argv, "-td",
                                          index) == VTK_OK )
    {
    this->UseTiledDisplay = 1;

    if ( vtkPVApplication::CheckForArgument(argc, argv, "--tile-dimensions-x",
                                            index) == VTK_OK ||
         vtkPVApplication::CheckForArgument(argc, argv, "-tdx",
                                          index) == VTK_OK )
      {
      // Strip string to equals sign.
      const char* newarg=0;
      int len = (int)(strlen(argv[index]));
      for (int i=0; i<len; i++)
        {
        if (argv[index][i] == '=')
          {
          newarg = &(argv[index][i+1]);
          }
        }
      this->TileDimensions[0] = atoi(newarg);
      }
    if ( vtkPVApplication::CheckForArgument(argc, argv, "--tile-dimensions-y",
                                            index) == VTK_OK ||
         vtkPVApplication::CheckForArgument(argc, argv, "-tdy",
                                          index) == VTK_OK )
      {
      // Strip string to equals sign.
      const char* newarg=0;
      int len = (int)(strlen(argv[index]));
      for (int i=0; i<len; i++)
        {
        if (argv[index][i] == '=')
          {
          newarg = &(argv[index][i+1]);
          }
        }
      this->TileDimensions[1] = atoi(newarg);
      }
    }
#endif


#ifdef VTK_MANGLE_MESA
  
  if ( vtkPVApplication::CheckForArgument(argc, argv, "--use-software-rendering",
                                          index) == VTK_OK ||
       vtkPVApplication::CheckForArgument(argc, argv, "-r",
                                          index) == VTK_OK ||
       vtkPVApplication::CheckForArgument(argc, argv, "--use-satellite-software",
                                          index) == VTK_OK ||
       vtkPVApplication::CheckForArgument(argc, argv, "-s",
                                          index) == VTK_OK ||
       getenv("PV_SOFTWARE_RENDERING") )
    {
    this->BroadcastScript("vtkGraphicsFactory _graphics_fact\n"
                          "_graphics_fact SetUseMesaClasses 1\n"
                          "_graphics_fact Delete");
    this->BroadcastScript("vtkImagingFactory _imaging_fact\n"
                          "_imaging_fact SetUseMesaClasses 1\n"
                          "_imaging_fact Delete");
    if ( getenv("PV_SOFTWARE_RENDERING") ||
         vtkPVApplication::CheckForArgument(
           argc, argv, "--use-satellite-software", index) == VTK_OK ||
         vtkPVApplication::CheckForArgument(argc, argv, "-s",
                                            index) == VTK_OK)
      {
      this->Script("vtkGraphicsFactory _graphics_fact\n"
                   "_graphics_fact SetUseMesaClasses 0\n"
                   "_graphics_fact Delete");
      this->Script("vtkImagingFactory _imaging_fact\n"
                   "_imaging_fact SetUseMesaClasses 0\n"
                   "_imaging_fact Delete");
      }
    }
#endif

  // Handle setting up the SGI pipes.
  if (this->UseRenderingGroup)
    {
    int numProcs = this->Controller->GetNumberOfProcesses();
    int numPipes = 1;
    int id;
    // Until I add a user interface to set the number of pipes,
    // just read it from a file.
    ifstream ifs;
    if (this->GroupFileName)
      {
      ifs.open(this->GroupFileName,ios::in);
      }
    else
      {
      ifs.open("pipes.inp",ios::in);
      }
    if (ifs.fail())
      {
      if (this->GroupFileName)
        {
        vtkErrorMacro("Could not find the file " << this->GroupFileName);
        }
      else
        {
        vtkErrorMacro("Could not find the file pipes.inp");
        }
        numPipes = numProcs;
      }
    else
      {
      ifs >> numPipes;
      if (numPipes > numProcs) { numPipes = numProcs; }
      if (numPipes < 1) { numPipes = 1; }
      }

    
    vtkPVRenderGroupDialog *rgDialog = vtkPVRenderGroupDialog::New();
    const char *displayString;
    displayString = getenv("DISPLAY");
    if (displayString)
      {
      rgDialog->SetDisplayString(0, displayString);
      }
    rgDialog->SetNumberOfProcessesInGroup(numPipes);
    rgDialog->Create(this);

    rgDialog->Invoke();
    numPipes = rgDialog->GetNumberOfProcessesInGroup();
    
    this->BroadcastScript("$Application SetNumberOfPipes %d", numPipes);    
    
    if (displayString)
      {    
      for (id = 1; id < numPipes; ++id)
        {
        // Format a new display string based on process.
        displayString = rgDialog->GetDisplayString(id);
        this->RemoteScript(
          id, "$Application SetEnvironmentVariable {DISPLAY=%s}", 
          displayString);
        }
      }
    rgDialog->Delete();
    }

  // Splash screen ?

  if (this->ShowSplashScreen)
    {
    this->CreateSplashScreen();
    this->SplashScreen->SetProgressMessage("Initializing application...");
    }

  // Application Icon 
#ifdef _WIN32
  this->Script("SetApplicationIcon %s.exe %d big",
               this->GetApplicationName(),
               IDI_PARAVIEWICO32);
  // No, we can't set the same icon, even if it has both 32x32 and 16x16
  this->Script("SetApplicationIcon %s.exe %d small",
               this->GetApplicationName(),
               IDI_PARAVIEWICO16);
#endif

  vtkPVWindow *ui = vtkPVWindow::New();
  this->Windows->AddItem(ui);

  vtkCallbackCommand *ccm = vtkCallbackCommand::New();
  ccm->SetClientData(this);
  ccm->SetCallback(::vtkPVAppProcessMessage);  
  ui->AddObserver(vtkKWEvent::WarningMessageEvent, ccm);
  ui->AddObserver(vtkKWEvent::ErrorMessageEvent, ccm);
  ccm->Delete();

  if (this->ShowSplashScreen)
    {
    this->SplashScreen->SetProgressMessage("Creating icons...");
    }

  this->CreateButtonPhotos();

  if ( vtkPVApplication::CheckForArgument(argc, argv, "--start-empty", index) 
       == VTK_OK || 
       vtkPVApplication::CheckForArgument(argc, argv, "-e", index) 
       == VTK_OK)
    {
    ui->InitializeDefaultInterfacesOff();
    }

  if (this->ShowSplashScreen)
    {
    this->SplashScreen->SetProgressMessage("Creating UI...");
    }

  ui->Create(this,"");

  // ui has ref. count of at least 1 because of AddItem() above
  ui->Delete();

  this->Script("proc bgerror { m } "
               "{ global Application; $Application DisplayTCLError $m }");
  vtkPVOutputWindow *window = vtkPVOutputWindow::New();
  window->SetWindowCollection( this->Windows );
  this->OutputWindow = window;
  vtkOutputWindow::SetInstance(this->OutputWindow);

  // Check if there is an existing ParaViewTrace file.
  if (this->ShowSplashScreen)
    {
    this->SplashScreen->SetProgressMessage("Looking for old trace files...");
    }
  char traceName[128];
  int foundTrace = this->CheckForTraceFile(traceName, 128);

  if (this->ShowSplashScreen)
    {
    this->SplashScreen->Hide();
    }

  // If there is already an existing trace file, ask the
  // user what she wants to do with it.
  if (foundTrace && ! this->RunningParaViewScript) 
    {
    vtkPVTraceFileDialog *dlg2 = vtkPVTraceFileDialog::New();
    dlg2->SetMasterWindow(ui);
    dlg2->Create(this,"");
    ostrstream str;
    str << "Do you want to save the existing tracefile?\n\n"
        << "A tracefile called " << traceName << " was found in "
        << "the current directory. This might mean that there is "
        << "another instance of ParaView running or ParaView crashed "
        << "previously and failed to delete it.\n"
        << "If ParaView crashed previously, this tracefile can "
        << "be useful in tracing the problem and you should consider "
        << "saving it.\n"
        << "If there is another instance of ParaView running, select "
        << "\"Do Nothing\" to avoid potential problems."
        << ends;
    dlg2->SetText(str.str());
    str.rdbuf()->freeze(0);
    dlg2->SetTitle("Tracefile found");
    dlg2->SetIcon();
    int shouldSave = dlg2->Invoke();
    dlg2->Delete();
    if (shouldSave == 2)
      {
      this->SaveTraceFile(traceName);
      }
    else if (shouldSave == 1)
      {
      unlink(traceName);
      }
    }

  // Open the trace file.
  int count = 0;
  struct stat fs;
  while(1)
    {
    ostrstream str;
    str << "ParaViewTrace" << count++ << ".pvs" << ends;
    if ( stat(str.str(), &fs) != 0 || count >= 10 )
      {
      this->SetTraceFileName(str.str());
      str.rdbuf()->freeze(0);
      break;
      }
    str.rdbuf()->freeze(0);
    }

  this->TraceFile = new ofstream(this->TraceFileName, ios::out);
    
  // Initialize a couple of variables in the trace file.
  this->AddTraceEntry("set kw(%s) [$Application GetMainWindow]",
                      ui->GetTclName());
  ui->SetTraceInitialized(1);
  // We have to set this variable after the window variable is set,
  // so it has to be done here.
  this->AddTraceEntry("set kw(%s) [$kw(%s) GetMainView]",
                      ui->GetMainView()->GetTclName(), ui->GetTclName());
  ui->GetMainView()->SetTraceInitialized(1);

  // If any of the argumens has a .pvs extension, load it as a script.
  for (i=1; i < argc; i++)
    {
    if (vtkPVApplication::IsParaViewScriptFile(argv[i]))
      {
      this->RunningParaViewScript = 1;
      ui->LoadScript(argv[i]);
      this->RunningParaViewScript = 0;
      }
    }

  if (playDemo)
    {
    this->Script("set pvDemoCommandLine 1");
    ui->PlayDemo();
    }
  else
    {
    this->vtkKWApplication::Start(argc,argv);
    }
  vtkOutputWindow::SetInstance(0);
  this->OutputWindow->Delete();
}


#ifdef VTK_USE_MPI
//----------------------------------------------------------------------------
vtkMultiProcessController *vtkPVApplication::NewController(int minId, int maxId)
{
  vtkMPICommunicator* localComm = vtkMPICommunicator::New();
  vtkMPIGroup* localGroup= vtkMPIGroup::New();
  vtkMPIController* localController = vtkMPIController::New();
  vtkMPICommunicator* worldComm = vtkMPICommunicator::GetWorldCommunicator();

  // I might want to pass the reference controller as a parameter.
  localGroup->Initialize( static_cast<vtkMPIController*>(this->Controller) );
  for(int i=minId; i<=maxId; i++)
    {
    localGroup->AddProcessId(i);
    }
  localComm->Initialize(worldComm, localGroup);
  localGroup->UnRegister(0);

  // Create a local controller (for the sub-group)
  localController->SetCommunicator(localComm);
  localComm->UnRegister(0);

  return localController;
}
#else
//----------------------------------------------------------------------------
vtkMultiProcessController *vtkPVApplication::NewController(int, int)
{
  return NULL;
}
#endif


//----------------------------------------------------------------------------
void vtkPVApplication::Exit()
{
  int id, myId, num;
  
  // Send a break RMI to each of the slaves.
  num = this->Controller->GetNumberOfProcesses();
  myId = this->Controller->GetLocalProcessId();
  
  this->vtkKWApplication::Exit();
  if ( this->AboutDialog )
    {
    this->AboutDialog->Delete();
    this->AboutDialog = 0;
    }

  for (id = 0; id < num; ++id)
    {
    if (id != myId)
      {
      this->Controller->TriggerRMI(id, 
                                   vtkMultiProcessController::BREAK_RMI_TAG);
      }
    }

}


//----------------------------------------------------------------------------
void vtkPVApplication::SendDataBounds(vtkDataSet *data)
{
  float *bounds;
  
  if (this->Controller->GetLocalProcessId() == 0)
    {
    return;
    }
  bounds = data->GetBounds();
  this->Controller->Send(bounds, 6, 0, 1967);
}

//----------------------------------------------------------------------------
void vtkPVApplication::SendDataNumberOfCells(vtkDataSet *data)
{
  int num;
  
  if (this->Controller->GetLocalProcessId() == 0)
    {
    return;
    }
  num = data->GetNumberOfCells();
  this->Controller->Send(&num, 1, 0, 1968);
}

//----------------------------------------------------------------------------
void vtkPVApplication::SendDataNumberOfPoints(vtkDataSet *data)
{
  int num;
  
  if (this->Controller->GetLocalProcessId() == 0)
    {
    return;
    }
  num = data->GetNumberOfPoints();
  this->Controller->Send(&num, 1, 0, 1969);
}

//----------------------------------------------------------------------------
void vtkPVApplication::GetMapperColorRange(float range[2],
                                           vtkPolyDataMapper *mapper)
{
  vtkDataSetAttributes *attr = NULL;
  vtkDataArray *array;
  
  if (mapper == NULL || mapper->GetInput() == NULL)
    {
    range[0] = VTK_LARGE_FLOAT;
    range[1] = -VTK_LARGE_FLOAT;
    return;
    }

  // Determine and get the array used to color the model.
  if (mapper->GetScalarMode() == VTK_SCALAR_MODE_USE_POINT_FIELD_DATA)
    {
    attr = mapper->GetInput()->GetPointData();
    }
  if (mapper->GetScalarMode() == VTK_SCALAR_MODE_USE_CELL_FIELD_DATA)
    {
    attr = mapper->GetInput()->GetCellData();
    }

  // Sanity check.
  if (attr == NULL)
    {
    range[0] = VTK_LARGE_FLOAT;
    range[1] = -VTK_LARGE_FLOAT;
    return;
    }

  array = attr->GetArray(mapper->GetArrayName());
  if (array == NULL)
    {
    range[0] = VTK_LARGE_FLOAT;
    range[1] = -VTK_LARGE_FLOAT;
    return;
    }

  array->GetRange( range, mapper->GetArrayComponent());
}


//----------------------------------------------------------------------------
void vtkPVApplication::SendMapperColorRange(vtkPolyDataMapper *mapper)
{
  float range[2];
  
  if (this->Controller->GetLocalProcessId() == 0)
    {
    return;
    }

  this->GetMapperColorRange(range, mapper);
  this->Controller->Send(range, 2, 0, 1969);
}

//----------------------------------------------------------------------------
void vtkPVApplication::SendDataArrayRange(vtkDataSet *data, 
                                          int pointDataFlag, 
                                          char *arrayName,
                                          int component)
{
  float range[2];
  vtkDataArray *array;

  if (this->Controller->GetLocalProcessId() == 0)
    {
    return;
    }
  
  if (pointDataFlag)
    {
    array = data->GetPointData()->GetArray(arrayName);
    }
  else
    {
    array = data->GetCellData()->GetArray(arrayName);
    }

  if (array && component >= 0 && component < array->GetNumberOfComponents())
    {
    array->GetRange(range, component);
    }
  else
    {
    range[0] = VTK_LARGE_FLOAT;
    range[1] = -VTK_LARGE_FLOAT;
    }

  this->Controller->Send(range, 2, 0, 1976);
}

//----------------------------------------------------------------------------
void vtkPVApplication::StartRecordingScript(char *filename)
{
  if (this->TraceFile)
    {
    *this->TraceFile << "$Application StartRecordingScript " << filename << endl;
    this->StopRecordingScript();
    }

  this->TraceFile = new ofstream(filename, ios::out);
  if (this->TraceFile && this->TraceFile->fail())
    {
    vtkErrorMacro("Could not open trace file " << filename);
    delete this->TraceFile;
    this->TraceFile = NULL;
    return;
    }

  // Initialize a couple of variables in the trace file.
  this->AddTraceEntry("set kw(%s) [$Application GetMainWindow]",
                      this->GetMainWindow()->GetTclName());
  this->GetMainWindow()->SetTraceInitialized(1);
  this->SetTraceFileName(filename);
}

//----------------------------------------------------------------------------
void vtkPVApplication::StopRecordingScript()
{
  if (this->TraceFile)
    {
    this->TraceFile->close();
    delete this->TraceFile;
    this->TraceFile = NULL;
    }
}


//----------------------------------------------------------------------------
void vtkPVApplication::CompleteArrays(vtkMapper *mapper, char *mapperTclName)
{
  int i, j;
  int numProcs;
  int nonEmptyFlag = 0;
  int activeAttributes[5];

  if (mapper->GetInput() == NULL || this->Controller == NULL ||
      mapper->GetInput()->GetNumberOfPoints() > 0 ||
      mapper->GetInput()->GetNumberOfCells() > 0)
    {
    return;
    }

  // Find the first non empty data object on another processes.
  numProcs = this->Controller->GetNumberOfProcesses();
  for (i = 1; i < numProcs; ++i)
    {
    this->RemoteScript(i, "$Application SendCompleteArrays %s", mapperTclName);
    this->Controller->Receive(&nonEmptyFlag, 1, i, 987243);
    if (nonEmptyFlag)
      { // This process has data.  Receive all the arrays, type and component.
      int num = 0;
      vtkDataArray *array = 0;
      char *name;
      int nameLength = 0;
      int type = 0;
      int numComps = 0;
      
      // First Point data.
      this->Controller->Receive(&num, 1, i, 987244);
      for (j = 0; j < num; ++j)
        {
        this->Controller->Receive(&type, 1, i, 987245);
        switch (type)
          {
          case VTK_INT:
            array = vtkIntArray::New();
            break;
          case VTK_FLOAT:
            array = vtkFloatArray::New();
            break;
          case VTK_DOUBLE:
            array = vtkDoubleArray::New();
            break;
          case VTK_CHAR:
            array = vtkCharArray::New();
            break;
          case VTK_LONG:
            array = vtkLongArray::New();
            break;
          case VTK_SHORT:
            array = vtkShortArray::New();
            break;
          case VTK_UNSIGNED_CHAR:
            array = vtkUnsignedCharArray::New();
            break;
          case VTK_UNSIGNED_INT:
            array = vtkUnsignedIntArray::New();
            break;
          case VTK_UNSIGNED_LONG:
            array = vtkUnsignedLongArray::New();
            break;
          case VTK_UNSIGNED_SHORT:
            array = vtkUnsignedShortArray::New();
            break;
          }
        this->Controller->Receive(&numComps, 1, i, 987246);
        array->SetNumberOfComponents(numComps);
        this->Controller->Receive(&nameLength, 1, i, 987247);
        name = new char[nameLength];
        this->Controller->Receive(name, nameLength, i, 987248);
        array->SetName(name);
        delete [] name;
        mapper->GetInput()->GetPointData()->AddArray(array);
        array->Delete();
        } // end of loop over point arrays.
      // Which scalars, ... are active?
      this->Controller->Receive(activeAttributes, 5, i, 987258);
      mapper->GetInput()->GetPointData()->SetActiveAttribute(activeAttributes[0],0);
      mapper->GetInput()->GetPointData()->SetActiveAttribute(activeAttributes[1],1);
      mapper->GetInput()->GetPointData()->SetActiveAttribute(activeAttributes[2],2);
      mapper->GetInput()->GetPointData()->SetActiveAttribute(activeAttributes[3],3);
      mapper->GetInput()->GetPointData()->SetActiveAttribute(activeAttributes[4],4);
 
      // Next Cell data.
      this->Controller->Receive(&num, 1, i, 987244);
      for (j = 0; j < num; ++j)
        {
        this->Controller->Receive(&type, 1, i, 987245);
        switch (type)
          {
          case VTK_INT:
            array = vtkIntArray::New();
            break;
          case VTK_FLOAT:
            array = vtkFloatArray::New();
            break;
          case VTK_DOUBLE:
            array = vtkDoubleArray::New();
            break;
          case VTK_CHAR:
            array = vtkCharArray::New();
            break;
          case VTK_LONG:
            array = vtkLongArray::New();
            break;
          case VTK_SHORT:
            array = vtkShortArray::New();
            break;
          case VTK_UNSIGNED_CHAR:
            array = vtkUnsignedCharArray::New();
            break;
          case VTK_UNSIGNED_INT:
            array = vtkUnsignedIntArray::New();
            break;
          case VTK_UNSIGNED_LONG:
            array = vtkUnsignedLongArray::New();
            break;
          case VTK_UNSIGNED_SHORT:
            array = vtkUnsignedShortArray::New();
            break;
          }
        this->Controller->Receive(&numComps, 1, i, 987246);
        array->SetNumberOfComponents(numComps);
        this->Controller->Receive(&nameLength, 1, i, 987247);
        name = new char[nameLength];
        this->Controller->Receive(name, nameLength, i, 987248);
        array->SetName(name);
        delete [] name;
        mapper->GetInput()->GetCellData()->AddArray(array);
        array->Delete();
        } // end of loop over cell arrays.
      // Which scalars, ... are active?
      this->Controller->Receive(activeAttributes, 5, i, 987258);
      mapper->GetInput()->GetCellData()->SetActiveAttribute(activeAttributes[0],0);
      mapper->GetInput()->GetCellData()->SetActiveAttribute(activeAttributes[1],1);
      mapper->GetInput()->GetCellData()->SetActiveAttribute(activeAttributes[2],2);
      mapper->GetInput()->GetCellData()->SetActiveAttribute(activeAttributes[3],3);
      mapper->GetInput()->GetCellData()->SetActiveAttribute(activeAttributes[4],4);
      
      // We only need information from one.
      return;
      } // End of if-non-empty check.
    }// End of loop over processes.
}



//----------------------------------------------------------------------------
void vtkPVApplication::SendCompleteArrays(vtkMapper *mapper)
{
  int nonEmptyFlag;
  int num;
  int i;
  int type;
  int numComps;
  int nameLength;
  const char *name;
  vtkDataArray *array;
  int activeAttributes[5];

  if (mapper->GetInput() == NULL ||
      (mapper->GetInput()->GetNumberOfPoints() == 0 &&
       mapper->GetInput()->GetNumberOfCells() == 0))
    {
    nonEmptyFlag = 0;
    this->Controller->Send(&nonEmptyFlag, 1, 0, 987243);
    return;
    }
  nonEmptyFlag = 1;
  this->Controller->Send(&nonEmptyFlag, 1, 0, 987243);

  // First point data.
  num = mapper->GetInput()->GetPointData()->GetNumberOfArrays();
  this->Controller->Send(&num, 1, 0, 987244);
  for (i = 0; i < num; ++i)
    {
    array = mapper->GetInput()->GetPointData()->GetArray(i);
    type = array->GetDataType();

    this->Controller->Send(&type, 1, 0, 987245);
    numComps = array->GetNumberOfComponents();

    this->Controller->Send(&numComps, 1, 0, 987246);
    name = array->GetName();
    if (name == NULL)
      {
      name = "";
      }
    nameLength = vtkString::Length(name)+1;
    this->Controller->Send(&nameLength, 1, 0, 987247);
    // I am pretty sure that Send does not modify the string.
    this->Controller->Send(const_cast<char*>(name), nameLength, 0, 987248);
    }
  mapper->GetInput()->GetPointData()->GetAttributeIndices(activeAttributes);
  this->Controller->Send(activeAttributes, 5, 0, 987258);

  // Next cell data.
  num = mapper->GetInput()->GetCellData()->GetNumberOfArrays();
  this->Controller->Send(&num, 1, 0, 987244);
  for (i = 0; i < num; ++i)
    {
    array = mapper->GetInput()->GetCellData()->GetArray(i);
    type = array->GetDataType();

    this->Controller->Send(&type, 1, 0, 987245);
    numComps = array->GetNumberOfComponents();

    this->Controller->Send(&numComps, 1, 0, 987246);
    name = array->GetName();
    if (name == NULL)
      {
      name = "";
      }
    nameLength = vtkString::Length(name+1);
    this->Controller->Send(&nameLength, 1, 0, 987247);
    this->Controller->Send(const_cast<char*>(name), nameLength, 0, 987248);
    }
  mapper->GetInput()->GetCellData()->GetAttributeIndices(activeAttributes);
  this->Controller->Send(activeAttributes, 5, 0, 987258);
}


void vtkPVApplication::SetGlobalLODFlag(int val)
{
  vtkPVApplication::GlobalLODFlag = val;

  if (this->Controller->GetLocalProcessId() == 0)
    {
    int idx, num;
    num = this->Controller->GetNumberOfProcesses();
    for (idx = 1; idx < num; ++idx)
      {
      this->RemoteScript(idx, "$Application SetGlobalLODFlag %d", val);
      }
    }
}

int vtkPVApplication::GetGlobalLODFlag()
{
  return vtkPVApplication::GlobalLODFlag;
}


//============================================================================
// Make instances of sources.
//============================================================================

//----------------------------------------------------------------------------
vtkObject *vtkPVApplication::MakeTclObject(const char *className,
                                           const char *tclName)
{
  this->BroadcastScript("%s %s", className, tclName);
  return this->TclToVTKObject(tclName);
}

//----------------------------------------------------------------------------
vtkObject *vtkPVApplication::TclToVTKObject(const char *tclName)
{
  vtkObject *o;
  int error;

  o = (vtkObject *)(vtkTclGetPointerFromObject(
                      tclName, "vtkObject", this->GetMainInterp(), error));
  
  if (o == NULL)
    {
    vtkErrorMacro("Could not get object from pointer.");
    }
  
  return o;
}

void vtkPVApplication::DisplayHelp(vtkKWWindow* master)
{
#ifdef _WIN32
  char temp[1024];
  char loc[1024];
  struct stat fs;

  vtkKWRegisteryUtilities *reg = this->GetRegistery();
  if (!this->GetRegisteryValue(2, "Setup", "InstalledPath", 0))
    {
    this->GetRegistery()->SetGlobalScope(1);
    }

  if (this->GetRegisteryValue(2, "Setup", "InstalledPath", loc))
    {
    sprintf(temp, "%s/%s.chm", loc, this->ApplicationName);
    }
  else
    {
    sprintf(temp, "%s.chm", this->ApplicationName);
    }
  this->GetRegistery()->SetGlobalScope(0);
  if (stat(temp, &fs) == 0) 
    {
    HtmlHelp(NULL, temp, HH_DISPLAY_TOPIC, 0);
    return;
    }

  const char** dir;
  for(dir=VTK_PV_HELP_PATHS; *dir; ++dir)
    {
    sprintf(temp, "%s/%s.chm", *dir, this->ApplicationName);
    if (stat(temp, &fs) == 0) 
      {
      HtmlHelp(NULL, temp, HH_DISPLAY_TOPIC, 0);
      return;
      }
    }

  vtkKWMessageDialog *dlg = vtkKWMessageDialog::New();
  dlg->SetTitle("ParaView Help");
  dlg->SetMasterWindow(master);
  dlg->Create(this,"");
  dlg->SetText(
    "The help file could not be found. Please make sure that ParaView "
    "is installed properly.");
  dlg->Invoke();  
  dlg->Delete();
  return;
    
#else

  vtkKWMessageDialog *dlg = vtkKWMessageDialog::New();
  dlg->SetTitle("ParaView Help");
  dlg->SetMasterWindow(master);
  dlg->Create(this,"");
  dlg->SetText(
    "HTML help is included in the Documentation/HTML subdirectory of"
    "this application. You can view this help using a standard web browser.");
  dlg->Invoke();  
  dlg->Delete();


#endif
}

//----------------------------------------------------------------------------
void vtkPVApplication::LogStartEvent(char* str)
{
  vtkTimerLog::MarkStartEvent(str);
}

//----------------------------------------------------------------------------
void vtkPVApplication::LogEndEvent(char* str)
{
  vtkTimerLog::MarkEndEvent(str);
}

#ifdef PV_HAVE_TRAPS_FOR_SIGNALS
//----------------------------------------------------------------------------
void vtkPVApplication::SetupTrapsForSignals(int nodeid)
{
  vtkPVApplication::MainApplication = this;
#ifndef _WIN32
  signal(SIGHUP, vtkPVApplication::TrapsForSignals);
#endif
  signal(SIGINT, vtkPVApplication::TrapsForSignals);
  if ( nodeid == 0 )
    {
    signal(SIGILL,  vtkPVApplication::TrapsForSignals);
    signal(SIGABRT, vtkPVApplication::TrapsForSignals);
    signal(SIGSEGV, vtkPVApplication::TrapsForSignals);

#ifndef _WIN32
    signal(SIGQUIT, vtkPVApplication::TrapsForSignals);
    signal(SIGPIPE, vtkPVApplication::TrapsForSignals);
    signal(SIGBUS,  vtkPVApplication::TrapsForSignals);
#endif
    }
}

//----------------------------------------------------------------------------
void vtkPVApplication::TrapsForSignals(int signal)
{
  if ( !vtkPVApplication::MainApplication )
    {
    exit(1);
    }
  
  switch ( signal )
    {
#ifndef _WIN32
    case SIGHUP:
      return;
      break;
#endif
    case SIGINT:
#ifndef _WIN32
    case SIGQUIT: 
#endif
      break;
    case SIGILL:
    case SIGABRT: 
    case SIGSEGV:
#ifndef _WIN32
    case SIGPIPE:
    case SIGBUS:
#endif
      vtkPVApplication::ErrorExit(); 
      break;      
    }
  
  if ( vtkPVApplication::MainApplication->GetProcessId() )
    {
    return;
    }
  vtkPVWindow *win = vtkPVApplication::MainApplication->GetMainWindow();
  if ( !win )
    {
    cout << "Call exit on application" << endl;
    vtkPVApplication::MainApplication->Exit();
    }
  cout << "Call exit on window" << endl;
  win->Exit();
}

//----------------------------------------------------------------------------
void vtkPVApplication::ErrorExit()
{
  // This { is here because compiler is smart enough to know that exit
  // exits the code without calling destructors. By adding this,
  // destructors are called before the exit.
  {
  cout << "There was a major error! Trying to exit..." << endl;
  char name[] = "ErrorApplication";
  char *n = name;
  char** args = &n;
  Tcl_Interp *interp = vtkKWApplication::InitializeTcl(1, args);
  ostrstream str;
  char buffer[1024];
#ifdef _WIN32
  _getcwd(buffer, 1023);
#else
  getcwd(buffer, 1023);
#endif

  Tcl_GlobalEval(interp, "wm withdraw .");
#ifdef _WIN32
  str << "option add *font {{MS Sans Serif} 8}" << endl;
#else
  str << "option add *font "
    "-adobe-helvetica-medium-r-normal--12-120-75-75-p-67-iso8859-1" << endl;
  str << "option add *highlightThickness 0" << endl;
  str << "option add *highlightBackground #ccc" << endl;
  str << "option add *activeBackground #eee" << endl;
  str << "option add *activeForeground #000" << endl;
  str << "option add *background #ccc" << endl;
  str << "option add *foreground #000" << endl;
  str << "option add *Entry.background #ffffff" << endl;
  str << "option add *Text.background #ffffff" << endl;
  str << "option add *Button.padX 6" << endl;
  str << "option add *Button.padY 3" << endl;
#endif
  str << "tk_messageBox -type ok -message {It looks like ParaView "
      << "or one of its libraries performed an illegal opeartion and "
      << "it will be terminated. Please report this error to "
      << "bug-report@kitware.com. You may want to include a small "
      << "description of what you did when this happened and your "
      << "ParaView trace file: " << buffer
      << "/ParaViewTrace.pvs} -icon error"
      << ends;
  Tcl_GlobalEval(interp, str.str());
  str.rdbuf()->freeze(0);
  }
  exit(1);
}
#endif // PV_HAVE_TRAPS_FOR_SIGNALS

//----------------------------------------------------------------------------
void vtkPVApplication::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
  os << indent << "Controller: " << this->Controller << endl;;
  os << indent << "MajorVersion: " << this->MajorVersion << endl;
  os << indent << "MinorVersion: " << this->MinorVersion << endl;
  os << indent << "RunningParaViewScript: " 
     << ( this->RunningParaViewScript ? "on" : " off" ) << endl;
  os << indent << "Current Process Id: " << this->ProcessId << endl;
  os << indent << "NumberOfPipes: " << this->NumberOfPipes << endl;
  os << indent << "UseRenderingGroup: " << (this->UseRenderingGroup?"on":"off") 
     << endl;
  if (this->UseTiledDisplay)
    { 
    os << indent << "UseTiledDisplay: On\n";
    os << indent << "TileDimensions: " << this->TileDimensions[0]
       << ", " << this->TileDimensions[1] << endl;
    }
  os << indent << "Display3DWidgets: " << (this->Display3DWidgets?"on":"off") 
     << endl;
  os << indent << "TraceFileName: " 
     << (this->TraceFileName ? this->TraceFileName : "(none)") << endl;
  os << indent << "Argv0: " 
     << (this->Argv0 ? this->Argv0 : "(none)") << endl;
}

void vtkPVApplication::DisplayTCLError(const char* message)
{
  vtkErrorMacro("TclTk error: "<<message);
}

//----------------------------------------------------------------------------
const char* const vtkPVApplication::LoadComponentProc =
"namespace eval ::paraview {\n"
"    proc load_component {name {optional_paths {}}} {\n"
"        \n"
"        global tcl_platform auto_path env\n"
"        \n"
"        # First dir is empty, to let Tcl try in the current dir\n"
"        \n"
"        set dirs $optional_paths\n"
"        set dirs [concat $dirs {\"\"}]\n"
"        set ext [info sharedlibextension]\n"
"        if {$tcl_platform(platform) == \"unix\"} {\n"
"            set prefix \"lib\"\n"
"            # Help Unix a bit by browsing into $auto_path and /usr/lib...\n"
"            set dirs [concat $dirs /usr/local/lib /usr/local/lib/vtk $auto_path]\n"
"            if {[info exists env(LD_LIBRARY_PATH)]} {\n"
"                set dirs [concat $dirs [split $env(LD_LIBRARY_PATH) \":\"]]\n"
"            }\n"
"            if {[info exists env(PATH)]} {\n"
"                set dirs [concat $dirs [split $env(PATH) \":\"]]\n"
"            }\n"
"        } else {\n"
"            set prefix \"\"\n"
"            if {$tcl_platform(platform) == \"windows\"} {\n"
"                if {[info exists env(PATH)]} {\n"
"                    set dirs [concat $dirs [split $env(PATH) \";\"]]\n"
"                }\n"
"            }\n"
"        }\n"
"        \n"
"        foreach dir $dirs {\n"
"            set libname [file join $dir ${prefix}${name}${ext}]\n"
"            if {[file exists $libname]} {\n"
"                if {![catch {load $libname} errormsg]} {\n"
"                    # WARNING: it HAS to be \"\" so that pkg_mkIndex work (since\n"
"                    # while evaluating a package ::paraview::load_component won't\n"
"                    # exist and will default to the unknown() proc that \n"
"                    # returns \"\"\n"
"                    return \"\"\n"
"                } else {\n"
"                    # If not loaded but file was found, oops\n"
"                    puts stderr $errormsg\n"
"                }\n"
"            }\n"
"        }\n"
"        \n"
"        puts stderr \"::paraview::load_component: $name could not be found.\"\n"
"        \n"
"        return 1\n"
"    }\n"
"    namespace export load_component\n"
"}\n";
