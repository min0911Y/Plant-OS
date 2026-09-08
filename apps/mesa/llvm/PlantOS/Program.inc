// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Native LLVM program services. External compiler processes are not used by
// the local JIT and require a process/redirect ABI which Plant OS does not
// have.
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace llvm::sys;

ProcessInfo::ProcessInfo() : Pid(0), ReturnCode(0) {}

ErrorOr<std::string> sys::findProgramByName(StringRef Name,
                                            ArrayRef<StringRef> Paths) {
  if (Name.contains('/') || Name.contains(':'))
    return Name.str();
  if (Paths.empty() && fs::can_execute(Name))
    return Name.str();
  for (StringRef Directory : Paths) {
    SmallString<128> Candidate(Directory);
    path::append(Candidate, Name);
    if (fs::can_execute(Candidate))
      return Candidate.str().str();
  }
  return errc::no_such_file_or_directory;
}

static bool Execute(ProcessInfo &PI, StringRef Program,
                    ArrayRef<StringRef> Args,
                    std::optional<ArrayRef<StringRef>> Env,
                    ArrayRef<std::optional<StringRef>> Redirects,
                    unsigned MemoryLimit, std::string *ErrMsg,
                    BitVector *AffinityMask, bool DetachProcess) {
  if (ErrMsg)
    *ErrMsg = "LLVM external process execution is not supported on Plant OS";
  return false;
}

ProcessInfo sys::Wait(const ProcessInfo &PI,
                      std::optional<unsigned> SecondsToWait,
                      std::string *ErrMsg,
                      std::optional<ProcessStatistics> *ProcStat,
                      bool Polling) {
  if (ErrMsg)
    *ErrMsg = "LLVM external process waiting is not supported on Plant OS";
  ProcessInfo Result;
  Result.ReturnCode = -1;
  return Result;
}

std::error_code sys::ChangeStdinMode(fs::OpenFlags) { return {}; }
std::error_code sys::ChangeStdoutMode(fs::OpenFlags) { return {}; }
std::error_code sys::ChangeStdinToBinary() { return {}; }
std::error_code sys::ChangeStdoutToBinary() { return {}; }

std::error_code sys::writeFileWithEncoding(StringRef Name, StringRef Contents,
                                           WindowsEncodingMethod Encoding) {
  std::error_code Error;
  raw_fd_ostream Output(Name, Error, fs::OF_None);
  if (Error)
    return Error;
  Output << Contents;
  Output.flush();
  return Output.error();
}

bool sys::commandLineFitsWithinSystemLimits(StringRef Program,
                                            ArrayRef<StringRef> Args) {
  size_t Size = Program.size();
  for (StringRef Argument : Args) {
    if (Argument.size() >= SIZE_MAX - Size)
      return false;
    Size += Argument.size() + 1;
  }
  return true;
}
