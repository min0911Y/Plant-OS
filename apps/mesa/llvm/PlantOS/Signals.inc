// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Native asynchronous signals do not provide synchronous fault recovery or
// stack unwinding. The kernel reports and terminates ordinary user faults.
#include "llvm/Support/ExitCodes.h"
#include <signal.h>

namespace {
std::atomic<void (*)()> InterruptFunction{nullptr};
std::atomic<void (*)()> PipeFunction{nullptr};

void nativeSignalHandler(int Signal) {
  auto &Handler = Signal == SIGPIPE ? PipeFunction : InterruptFunction;
  if (Signal != SIGTERM) {
    if (auto Function = Handler.exchange(nullptr)) {
      Function();
      return;
    }
  }
  sys::RunSignalHandlers();
  std::_Exit(128 + Signal);
}
} // namespace

void sys::RunInterruptHandlers() {}

bool sys::RemoveFileOnSignal(StringRef, std::string *Error) {
  if (Error)
    *Error = "Plant OS cannot remove files after a fatal user fault";
  return true;
}

void sys::DontRemoveFileOnSignal(StringRef) {}

void sys::AddSignalHandler(SignalHandlerCallback Callback, void *Cookie) {
  insertSignalHandler(Callback, Cookie);
  signal(SIGTERM, nativeSignalHandler);
}

void sys::SetInterruptFunction(void (*Handler)()) {
  InterruptFunction.store(Handler);
  signal(SIGINT, Handler ? nativeSignalHandler : SIG_DFL);
}

void sys::SetInfoSignalFunction(void (*Handler)()) {
  if (Handler)
    report_fatal_error("Plant OS has no information signal");
}

void sys::SetOneShotPipeSignalFunction(void (*Handler)()) {
  PipeFunction.store(Handler);
  signal(SIGPIPE, Handler ? nativeSignalHandler : SIG_DFL);
}

void sys::DefaultOneShotPipeSignalHandler() { std::_Exit(EX_IOERR); }

void sys::CleanupOnSignal(uintptr_t) { RunSignalHandlers(); }

void sys::unregisterHandlers() {
  InterruptFunction.store(nullptr);
  PipeFunction.store(nullptr);
  signal(SIGINT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGPIPE, SIG_DFL);
}

void sys::DisableSystemDialogsOnCrash() {}
void sys::PrintStackTraceOnErrorSignal(StringRef, bool) {}

void sys::PrintStackTrace(raw_ostream &Output, int) {
  Output << "LLVM stack unwinding is unavailable on Plant OS\n";
}

static bool findModulesAndOffsets(void **, int, const char **, intptr_t *,
                                  const char *, StringSaver &) {
  return false;
}

static bool printMarkupContext(raw_ostream &, const char *) { return false; }
