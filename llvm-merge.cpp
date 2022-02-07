#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Linker/IRMover.h"

using namespace llvm;

static cl::opt<std::string> DstFileName(cl::Positional,
                                        cl::desc("<destination file>"));
static cl::opt<std::string> SrcFileName("src", cl::desc("<source file>"),
                                        cl::Required);
static cl::opt<std::string> OutFileName("o", cl::desc("<output file>"),
                                        cl::init("-"));
static cl::list<std::string>
    FuncsToMerge("funcs", cl::desc("list of funcs to merge from source to dst"),
                 cl::CommaSeparated);

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv,
                              "Merge a list of functions from SRC to DST\n");

  LLVMContext Context;
  SMDiagnostic Err;

  std::unique_ptr<Module> DstM = parseIRFile(DstFileName, Err, Context);
  if (!DstM) {
    Err.print(argv[0], errs());
    return 1;
  }

  std::unique_ptr<Module> SrcM = parseIRFile(DstFileName, Err, Context);
  if (!SrcM) {
    Err.print(argv[0], errs());
    return 1;
  }

  std::vector<GlobalValue *> FuncsToMove;
  for (StringRef FuncName : FuncsToMerge) {
    if (auto *F = DstM->getFunction(FuncName))
      F->deleteBody();
    if (auto *F = SrcM->getFunction(FuncName))
      FuncsToMove.push_back(F);
  }

  IRMover Mover(*DstM);
  if (Error Err = Mover.move(
      std::move(SrcM), FuncsToMove, [](GlobalValue &, IRMover::ValueAdder) {},
      true /*is performing import*/)) {
    report_fatal_error("Function Import: link error: " + toString(std::move(Err)));
  }

  std::error_code EC;
  ToolOutputFile Out(OutFileName, EC, sys::fs::OF_None);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  WriteBitcodeToFile(*DstM, Out.os());
  Out.keep();
}