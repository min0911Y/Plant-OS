#include <cmath>
#include <cstdio>
#include <llvm/AsmParser/Parser.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Triple.h>
#include <memory>
#include <syscall.h>
#include <thread>
#include <unistd.h>

static bool compile_and_run(bool protect) {
  llvm::LLVMContext context;
  llvm::SMDiagnostic diagnostic;
  auto module = llvm::parseAssemblyString(R"(
    @counter = global i64 37
    @weights = constant [4 x double] [double 0.125, double 0.25, double 0.5, double 1.0]
    declare double @sin(double)
    define double @evaluate(double %x, i64 %index) noredzone {
      %address = getelementptr [4 x double], ptr @weights, i64 0, i64 %index
      %weight = load double, ptr %address
      %sine = call double @sin(double %x)
      %result = fadd double %sine, %weight
      ret double %result
    }
    define i64 @bump(i64 %step) noredzone {
      %old = load volatile i64, ptr @counter
      %next = add i64 %old, %step
      store volatile i64 %next, ptr @counter
      ret i64 %next
    }
  )",
                                          diagnostic, context);
  if (!module) {
    diagnostic.print("llvmtest", llvm::errs());
    return false;
  }
  module->setTargetTriple(llvm::Triple("x86_64-unknown-none-elf"));
  std::string error;
  llvm::EngineBuilder builder(std::move(module));
  builder.setEngineKind(llvm::EngineKind::JIT)
      .setErrorStr(&error)
      .setMCPU("x86-64")
      .setMAttrs(std::vector<std::string>{"+sse2", "-mmx", "-avx", "-avx2",
                                          "-avx512f"})
      .setCodeModel(llvm::CodeModel::Large)
      .setOptLevel(llvm::CodeGenOptLevel::Default)
      .setMCJITMemoryManager(std::make_unique<llvm::SectionMemoryManager>());
  std::unique_ptr<llvm::ExecutionEngine> engine(builder.create());
  if (!engine) {
    logkf("LLVMTEST compiler: %s\n", error.c_str());
    return false;
  }
  engine->finalizeObject();
  if (engine->hasError()) {
    logkf("LLVMTEST finalize: %s\n", engine->getErrorMessage().c_str());
    return false;
  }
  auto evaluate = reinterpret_cast<double (*)(double, uint64_t)>(
      engine->getFunctionAddress("evaluate"));
  auto bump = reinterpret_cast<uint64_t (*)(uint64_t)>(
      engine->getFunctionAddress("bump"));
  if (!evaluate || !bump) {
    logkf("LLVMTEST missing compiled function\n");
    return false;
  }
  auto first = bump(5), second = bump(7);
  if (first != 42 || second != 49) {
    logkf("LLVMTEST global relocation: %llu %llu\n", (unsigned long long)first,
          (unsigned long long)second);
    return false;
  }
  const double weights[] = {0.125, 0.25, 0.5, 1.0};
  for (unsigned i = 0; i < 64; i++) {
    double x = i / 13.0;
    if (std::abs(evaluate(x, i % 4) - (std::sin(x) + weights[i % 4])) > 1e-12) {
      logkf("LLVMTEST math mismatch index=%u\n", i);
      return false;
    }
  }
  if (protect) {
    int child = fork();
    if (!child) {
      *reinterpret_cast<volatile unsigned char *>(evaluate) = 0;
      _exit(0);
    }
    int status = child < 0 ? 0 : waittid(child);
    double value = evaluate(0, 0);
    if (child < 0 || status == 0 || value != 0.125) {
      logkf("LLVMTEST protection child=%d status=%d value=%.17g\n", child,
            status, value);
      return false;
    }
  }
  return true;
}

int main() {
  if (llvm::InitializeNativeTarget() ||
      llvm::InitializeNativeTargetAsmPrinter() ||
      llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr))
    return 1;
  if (!compile_and_run(true)) {
    logkf("LLVMTEST FAIL: compilation, relocations or W^X\n");
    return 1;
  }
  bool results[2] = {};
  auto worker = [&](unsigned index) {
    results[index] = compile_and_run(false) && compile_and_run(false);
  };
  std::thread first(worker, 0), second(worker, 1);
  first.join();
  second.join();
  if (!results[0] || !results[1]) {
    logkf("LLVMTEST FAIL: concurrent JIT lifecycle\n");
    return 1;
  }
  puts("LLVMTEST PASS");
  logkf("LLVMTEST PASS\n");
  return 0;
}
