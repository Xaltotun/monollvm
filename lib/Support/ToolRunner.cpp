//===-- ToolRunner.cpp ----------------------------------------------------===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements the interfaces described in the ToolRunner.h file.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "toolrunner"
#include "llvm/Support/ToolRunner.h"
#include "llvm/Config/config.h"   // for HAVE_LINK_R
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileUtilities.h"
#include <fstream>
#include <sstream>
using namespace llvm;

ToolExecutionError::~ToolExecutionError() throw() { }

static void ProcessFailure(std::string ProgPath, const char** Args) {
  std::ostringstream OS;
  OS << "\nError running tool:\n ";
  for (const char **Arg = Args; *Arg; ++Arg)
    OS << " " << *Arg;
  OS << "\n";

  // Rerun the compiler, capturing any error messages to print them.
  std::string ErrorFilename = getUniqueFilename("error_messages");
  RunProgramWithTimeout(ProgPath, Args, "/dev/null", ErrorFilename.c_str(),
                        ErrorFilename.c_str());

  // Print out the error messages generated by GCC if possible...
  std::ifstream ErrorFile(ErrorFilename.c_str());
  if (ErrorFile) {
    std::copy(std::istreambuf_iterator<char>(ErrorFile),
              std::istreambuf_iterator<char>(),
              std::ostreambuf_iterator<char>(OS));
    ErrorFile.close();
  }

  removeFile(ErrorFilename);
  throw ToolExecutionError(OS.str());
}

//===---------------------------------------------------------------------===//
// LLI Implementation of AbstractIntepreter interface
//
namespace {
  class LLI : public AbstractInterpreter {
    std::string LLIPath;          // The path to the LLI executable
    std::vector<std::string> ToolArgs; // Args to pass to LLI
  public:
    LLI(const std::string &Path, const std::vector<std::string> *Args)
      : LLIPath(Path) {
      ToolArgs.clear ();
      if (Args) { ToolArgs = *Args; }
    }
    
    virtual int ExecuteProgram(const std::string &Bytecode,
                               const std::vector<std::string> &Args,
                               const std::string &InputFile,
                               const std::string &OutputFile,
                               const std::vector<std::string> &SharedLibs = 
                               std::vector<std::string>(),
                               unsigned Timeout = 0);
  };
}

int LLI::ExecuteProgram(const std::string &Bytecode,
                        const std::vector<std::string> &Args,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        const std::vector<std::string> &SharedLibs,
                        unsigned Timeout) {
  if (!SharedLibs.empty())
    throw ToolExecutionError("LLI currently does not support "
                             "loading shared libraries.");

  std::vector<const char*> LLIArgs;
  LLIArgs.push_back(LLIPath.c_str());
  LLIArgs.push_back("-force-interpreter=true");

  // Add any extra LLI args.
  for (unsigned i = 0, e = ToolArgs.size(); i != e; ++i)
    LLIArgs.push_back(ToolArgs[i].c_str());

  LLIArgs.push_back(Bytecode.c_str());
  // Add optional parameters to the running program from Argv
  for (unsigned i=0, e = Args.size(); i != e; ++i)
    LLIArgs.push_back(Args[i].c_str());
  LLIArgs.push_back(0);

  std::cout << "<lli>" << std::flush;
  DEBUG(std::cerr << "\nAbout to run:\t";
        for (unsigned i=0, e = LLIArgs.size()-1; i != e; ++i)
          std::cerr << " " << LLIArgs[i];
        std::cerr << "\n";
        );
  return RunProgramWithTimeout(LLIPath, &LLIArgs[0],
                               InputFile, OutputFile, OutputFile, Timeout);
}

// LLI create method - Try to find the LLI executable
AbstractInterpreter *AbstractInterpreter::createLLI(const std::string &ProgPath,
                                                    std::string &Message,
                                     const std::vector<std::string> *ToolArgs) {
  std::string LLIPath = FindExecutable("lli", ProgPath);
  if (!LLIPath.empty()) {
    Message = "Found lli: " + LLIPath + "\n";
    return new LLI(LLIPath, ToolArgs);
  }

  Message = "Cannot find `lli' in executable directory or PATH!\n";
  return 0;
}

//===----------------------------------------------------------------------===//
// LLC Implementation of AbstractIntepreter interface
//
void LLC::OutputAsm(const std::string &Bytecode, std::string &OutputAsmFile) {
  OutputAsmFile = getUniqueFilename(Bytecode+".llc.s");
  std::vector<const char *> LLCArgs;
  LLCArgs.push_back (LLCPath.c_str());

  // Add any extra LLC args.
  for (unsigned i = 0, e = ToolArgs.size(); i != e; ++i)
    LLCArgs.push_back(ToolArgs[i].c_str());

  LLCArgs.push_back ("-o");
  LLCArgs.push_back (OutputAsmFile.c_str()); // Output to the Asm file
  LLCArgs.push_back ("-f");                  // Overwrite as necessary...
  LLCArgs.push_back (Bytecode.c_str());      // This is the input bytecode
  LLCArgs.push_back (0);

  std::cout << "<llc>" << std::flush;
  DEBUG(std::cerr << "\nAbout to run:\t";
        for (unsigned i=0, e = LLCArgs.size()-1; i != e; ++i)
          std::cerr << " " << LLCArgs[i];
        std::cerr << "\n";
        );
  if (RunProgramWithTimeout(LLCPath, &LLCArgs[0], "/dev/null", "/dev/null",
                            "/dev/null"))
    ProcessFailure(LLCPath, &LLCArgs[0]);
}

void LLC::compileProgram(const std::string &Bytecode) {
  std::string OutputAsmFile;
  OutputAsm(Bytecode, OutputAsmFile);
  removeFile(OutputAsmFile);
}

int LLC::ExecuteProgram(const std::string &Bytecode,
                        const std::vector<std::string> &Args,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        const std::vector<std::string> &SharedLibs,
                        unsigned Timeout) {

  std::string OutputAsmFile;
  OutputAsm(Bytecode, OutputAsmFile);
  FileRemover OutFileRemover(OutputAsmFile);

  // Assuming LLC worked, compile the result with GCC and run it.
  return gcc->ExecuteProgram(OutputAsmFile, Args, GCC::AsmFile,
                             InputFile, OutputFile, SharedLibs, Timeout);
}

/// createLLC - Try to find the LLC executable
///
LLC *AbstractInterpreter::createLLC(const std::string &ProgramPath,
                                    std::string &Message,
                                    const std::vector<std::string> *Args) {
  std::string LLCPath = FindExecutable("llc", ProgramPath);
  if (LLCPath.empty()) {
    Message = "Cannot find `llc' in executable directory or PATH!\n";
    return 0;
  }

  Message = "Found llc: " + LLCPath + "\n";
  GCC *gcc = GCC::create(ProgramPath, Message);
  if (!gcc) {
    std::cerr << Message << "\n";
    exit(1);
  }
  return new LLC(LLCPath, gcc, Args);
}

//===---------------------------------------------------------------------===//
// JIT Implementation of AbstractIntepreter interface
//
namespace {
  class JIT : public AbstractInterpreter {
    std::string LLIPath;          // The path to the LLI executable
    std::vector<std::string> ToolArgs; // Args to pass to LLI
  public:
    JIT(const std::string &Path, const std::vector<std::string> *Args)
      : LLIPath(Path) {
      ToolArgs.clear ();
      if (Args) { ToolArgs = *Args; }
    }
    
    virtual int ExecuteProgram(const std::string &Bytecode,
                               const std::vector<std::string> &Args,
                               const std::string &InputFile,
                               const std::string &OutputFile,
                               const std::vector<std::string> &SharedLibs = 
                               std::vector<std::string>(), unsigned Timeout =0);
  };
}

int JIT::ExecuteProgram(const std::string &Bytecode,
                        const std::vector<std::string> &Args,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        const std::vector<std::string> &SharedLibs,
                        unsigned Timeout) {
  // Construct a vector of parameters, incorporating those from the command-line
  std::vector<const char*> JITArgs;
  JITArgs.push_back(LLIPath.c_str());
  JITArgs.push_back("-force-interpreter=false");

  // Add any extra LLI args.
  for (unsigned i = 0, e = ToolArgs.size(); i != e; ++i)
    JITArgs.push_back(ToolArgs[i].c_str());

  for (unsigned i = 0, e = SharedLibs.size(); i != e; ++i) {
    JITArgs.push_back("-load");
    JITArgs.push_back(SharedLibs[i].c_str());
  }
  JITArgs.push_back(Bytecode.c_str());
  // Add optional parameters to the running program from Argv
  for (unsigned i=0, e = Args.size(); i != e; ++i)
    JITArgs.push_back(Args[i].c_str());
  JITArgs.push_back(0);

  std::cout << "<jit>" << std::flush;
  DEBUG(std::cerr << "\nAbout to run:\t";
        for (unsigned i=0, e = JITArgs.size()-1; i != e; ++i)
          std::cerr << " " << JITArgs[i];
        std::cerr << "\n";
        );
  DEBUG(std::cerr << "\nSending output to " << OutputFile << "\n");
  return RunProgramWithTimeout(LLIPath, &JITArgs[0],
                               InputFile, OutputFile, OutputFile, Timeout);
}

/// createJIT - Try to find the LLI executable
///
AbstractInterpreter *AbstractInterpreter::createJIT(const std::string &ProgPath,
                   std::string &Message, const std::vector<std::string> *Args) {
  std::string LLIPath = FindExecutable("lli", ProgPath);
  if (!LLIPath.empty()) {
    Message = "Found lli: " + LLIPath + "\n";
    return new JIT(LLIPath, Args);
  }

  Message = "Cannot find `lli' in executable directory or PATH!\n";
  return 0;
}

void CBE::OutputC(const std::string &Bytecode,
                 std::string &OutputCFile) {
  OutputCFile = getUniqueFilename(Bytecode+".cbe.c");
  std::vector<const char *> LLCArgs;
  LLCArgs.push_back (LLCPath.c_str());

  // Add any extra LLC args.
  for (unsigned i = 0, e = ToolArgs.size(); i != e; ++i)
    LLCArgs.push_back(ToolArgs[i].c_str());

  LLCArgs.push_back ("-o");
  LLCArgs.push_back (OutputCFile.c_str());   // Output to the C file
  LLCArgs.push_back ("-march=c");            // Output C language
  LLCArgs.push_back ("-f");                  // Overwrite as necessary...
  LLCArgs.push_back (Bytecode.c_str());      // This is the input bytecode
  LLCArgs.push_back (0);

  std::cout << "<cbe>" << std::flush;
  DEBUG(std::cerr << "\nAbout to run:\t";
        for (unsigned i=0, e = LLCArgs.size()-1; i != e; ++i)
          std::cerr << " " << LLCArgs[i];
        std::cerr << "\n";
        );
  if (RunProgramWithTimeout(LLCPath, &LLCArgs[0], "/dev/null", "/dev/null",
                            "/dev/null"))
    ProcessFailure(LLCPath, &LLCArgs[0]);
}

void CBE::compileProgram(const std::string &Bytecode) {
  std::string OutputCFile;
  OutputC(Bytecode, OutputCFile);
  removeFile(OutputCFile);
}

int CBE::ExecuteProgram(const std::string &Bytecode,
                        const std::vector<std::string> &Args,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        const std::vector<std::string> &SharedLibs,
                        unsigned Timeout) {
  std::string OutputCFile;
  OutputC(Bytecode, OutputCFile);

  FileRemover CFileRemove(OutputCFile);

  return gcc->ExecuteProgram(OutputCFile, Args, GCC::CFile, 
                             InputFile, OutputFile, SharedLibs, Timeout);
}

/// createCBE - Try to find the 'llc' executable
///
CBE *AbstractInterpreter::createCBE(const std::string &ProgramPath,
                                    std::string &Message,
                                    const std::vector<std::string> *Args) {
  std::string LLCPath = FindExecutable("llc", ProgramPath);
  if (LLCPath.empty()) {
    Message = 
      "Cannot find `llc' in executable directory or PATH!\n";
    return 0;
  }

  Message = "Found llc: " + LLCPath + "\n";
  GCC *gcc = GCC::create(ProgramPath, Message);
  if (!gcc) {
    std::cerr << Message << "\n";
    exit(1);
  }
  return new CBE(LLCPath, gcc, Args);
}

//===---------------------------------------------------------------------===//
// GCC abstraction
//
int GCC::ExecuteProgram(const std::string &ProgramFile,
                        const std::vector<std::string> &Args,
                        FileType fileType,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        const std::vector<std::string> &SharedLibs,
                        unsigned Timeout) {
  std::vector<const char*> GCCArgs;

  GCCArgs.push_back(GCCPath.c_str());

  // Specify the shared libraries to link in...
  for (unsigned i = 0, e = SharedLibs.size(); i != e; ++i)
    GCCArgs.push_back(SharedLibs[i].c_str());
  
  // Specify -x explicitly in case the extension is wonky
  GCCArgs.push_back("-x");
  if (fileType == CFile) {
    GCCArgs.push_back("c");
    GCCArgs.push_back("-fno-strict-aliasing");
  } else {
    GCCArgs.push_back("assembler");
  }
  GCCArgs.push_back(ProgramFile.c_str());  // Specify the input filename...
  GCCArgs.push_back("-o");
  std::string OutputBinary = getUniqueFilename(ProgramFile+".gcc.exe");
  GCCArgs.push_back(OutputBinary.c_str()); // Output to the right file...
  GCCArgs.push_back("-lm");                // Hard-code the math library...
  GCCArgs.push_back("-O2");                // Optimize the program a bit...
#if defined (HAVE_LINK_R)
  GCCArgs.push_back("-Wl,-R.");            // Search this dir for .so files
#endif
  GCCArgs.push_back(0);                    // NULL terminator

  std::cout << "<gcc>" << std::flush;
  if (RunProgramWithTimeout(GCCPath, &GCCArgs[0], "/dev/null", "/dev/null",
                            "/dev/null")) {
    ProcessFailure(GCCPath, &GCCArgs[0]);
    exit(1);
  }

  std::vector<const char*> ProgramArgs;
  ProgramArgs.push_back(OutputBinary.c_str());
  // Add optional parameters to the running program from Argv
  for (unsigned i=0, e = Args.size(); i != e; ++i)
    ProgramArgs.push_back(Args[i].c_str());
  ProgramArgs.push_back(0);                // NULL terminator

  // Now that we have a binary, run it!
  std::cout << "<program>" << std::flush;
  DEBUG(std::cerr << "\nAbout to run:\t";
        for (unsigned i=0, e = ProgramArgs.size()-1; i != e; ++i)
          std::cerr << " " << ProgramArgs[i];
        std::cerr << "\n";
        );

  FileRemover OutputBinaryRemover(OutputBinary);
  return RunProgramWithTimeout(OutputBinary, &ProgramArgs[0],
                               InputFile, OutputFile, OutputFile, Timeout);
}

int GCC::MakeSharedObject(const std::string &InputFile, FileType fileType,
                          std::string &OutputFile) {
  OutputFile = getUniqueFilename(InputFile+SHLIBEXT);
  // Compile the C/asm file into a shared object
  const char* GCCArgs[] = {
    GCCPath.c_str(),
    "-x", (fileType == AsmFile) ? "assembler" : "c",
    "-fno-strict-aliasing",
    InputFile.c_str(),           // Specify the input filename...
#if defined(sparc) || defined(__sparc__) || defined(__sparcv9)
    "-G",                        // Compile a shared library, `-G' for Sparc
#elif (defined(__POWERPC__) || defined(__ppc__)) && defined(__APPLE__)
    "-dynamiclib",               // `-dynamiclib' for MacOS X/PowerPC
    "-fno-common",               // allow global vars w/o initializers to live
    "-undefined",                // in data segment, rather than generating
    "dynamic_lookup",            // blocks. dynamic_lookup requires that you set
                                 // MACOSX_DEPLOYMENT_TARGET=10.3 in your env.
#else
    "-shared",                   // `-shared' for Linux/X86, maybe others
#endif
    "-o", OutputFile.c_str(),    // Output to the right filename...
    "-O2",                       // Optimize the program a bit...
    0
  };
  
  std::cout << "<gcc>" << std::flush;
  if (RunProgramWithTimeout(GCCPath, GCCArgs, "/dev/null", "/dev/null",
                            "/dev/null")) {
    ProcessFailure(GCCPath, GCCArgs);
    return 1;
  }
  return 0;
}

/// create - Try to find the `gcc' executable
///
GCC *GCC::create(const std::string &ProgramPath, std::string &Message) {
  std::string GCCPath = FindExecutable("gcc", ProgramPath);
  if (GCCPath.empty()) {
    Message = "Cannot find `gcc' in executable directory or PATH!\n";
    return 0;
  }

  Message = "Found gcc: " + GCCPath + "\n";
  return new GCC(GCCPath);
}
