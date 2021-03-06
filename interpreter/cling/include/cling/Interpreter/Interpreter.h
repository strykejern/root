//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// version: $Id: cd58ec48f1f279a480adfbf866320fc658a41b2d $
// author:  Lukasz Janyst <ljanyst@cern.ch>
//------------------------------------------------------------------------------

#ifndef CLING_INTERPRETER_H
#define CLING_INTERPRETER_H

#include "cling/Interpreter/InvocationOptions.h"

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringRef.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace llvm {
  class raw_ostream;
  struct GenericValue;
  class ExecutionEngine;
  class LLVMContext;
  class Module;
  class Type;
  template <typename T> class SmallVectorImpl;

  namespace sys {
    class DynamicLibrary;
  }
}

namespace clang {
  class ASTContext;
  class ASTDeserializationListener;
  class CodeGenerator;
  class CompilerInstance;
  class Decl;
  class DeclContext;
  class FunctionDecl;
  class NamedDecl;
  class MangleContext;
  class Parser;
  class QualType;
  class Sema;
}

namespace cling {
  namespace runtime {
    namespace internal {
      class DynamicExprInfo;
      template <typename T>
      T EvaluateT(DynamicExprInfo* ExprInfo, clang::DeclContext* DC);
      class LifetimeHandler;
    }
  }
  class ClingCallbackAdaptor;
  class CompilationOptions;
  class ExecutionContext;
  class IncrementalParser;
  class InterpreterCallbacks;
  class LookupHelper;
  class StoredValueRef;
  class Transaction;

  ///\brief Class that implements the interpreter-like behavior. It manages the
  /// incremental compilation.
  ///
  class Interpreter {
  public:

    ///\brief Pushes a new transaction, which will collect the decls that came
    /// within the scope of the RAII object. Calls commit transaction at 
    /// destruction.
    class PushTransactionRAII {
    private:
      Transaction* m_Transaction;
      Interpreter* m_Interpreter;
    public:
      PushTransactionRAII(Interpreter* i);
      ~PushTransactionRAII();
      void pop();
    };

    ///\brief Describes the return result of the different routines that do the
    /// incremental compilation.
    ///
    enum CompilationResult {
      kSuccess,
      kFailure,
      kMoreInputExpected
    };

    ///\brief Describes the result of loading a library.
    ///
    enum LoadLibResult {
      kLoadLibSuccess, // library loaded successfully
      kLoadLibExists,  // library was already loaded
      kLoadLibError, // library was not found
      kLoadLibNumResults
    };

    ///\brief Describes the result of running a function.
    ///
    enum ExecutionResult {
      ///\brief The function was run successfully.
      kExeSuccess,
      ///\brief Code generator is unavailable; not an error.
      kExeNoCodeGen,

      ///\brief First error value.
      kExeFirstError,
      ///\brief The function is not known and cannot be called.
      kExeFunctionNotCompiled = kExeFirstError,
      ///\brief While compiling the function, unknown symbols were encountered.
      kExeUnresolvedSymbols,
      ///\brief Compilation error.
      kExeCompilationError,
      ///\brief The function is not known.
      kExeUnkownFunction,

      ///\brief Number of possible results.
      kNumExeResults
    };


    class LoadedFileInfo {
    public:
      enum FileType {
        kSource,
        kDynamicLibrary,
        kBitcode,
        kNumFileTypes
      };

      ///\brief Name as loaded for the first time.
      ///
      const std::string& getName() const { return m_Name; }

      ///\brief Type of the file.
      FileType getType() const { return m_Type; }

      ///\brief Pointer to Interpreter::m_DyLibs entry if dynamic library
      ///
      const llvm::sys::DynamicLibrary* getDynLib() const { return m_DynLib; }

    private:
      ///\brief Constructor used by Interpreter.
      LoadedFileInfo(const std::string& name, FileType type,
                     const llvm::sys::DynamicLibrary* dynLib):
        m_Name(name), m_Type(type), m_DynLib(dynLib) {}

      ///\brief Name as loaded for the first time.
      ///
      std::string m_Name;

      ///\brief Type of the file.
      FileType m_Type;

      ///\brief Pointer to Interpreter::m_DyLibs entry if dynamic library
      ///
      const llvm::sys::DynamicLibrary* m_DynLib;

      friend class Interpreter;
    };

    ///\brief Collection of DynamicLibraries loaded by this Interpreter;
    /// or rather type-opaque wrapper thereof.
    ///
    class DynLibSetBase {
    public:
      virtual ~DynLibSetBase() {}
    };

  private:

    ///\brief Interpreter invocation options.
    ///
    InvocationOptions m_Opts;

    ///\brief The llvm library state, a per-thread object.
    ///
    llvm::OwningPtr<llvm::LLVMContext> m_LLVMContext;

    ///\brief Cling's execution engine - a well wrapped llvm execution engine.
    ///
    llvm::OwningPtr<ExecutionContext> m_ExecutionContext;

    ///\brief Cling's worker class implementing the incremental compilation.
    ///
    llvm::OwningPtr<IncrementalParser> m_IncrParser;

    ///\brief Cling's reflection information query.
    ///
    llvm::OwningPtr<LookupHelper> m_LookupHelper;

    ///\brief Helper object for mangling names.
    ///
    mutable llvm::OwningPtr<clang::MangleContext> m_MangleCtx;

    ///\brief Counter used when we need unique names.
    ///
    unsigned long long m_UniqueCounter;

    ///\brief Flag toggling the AST printing on or off.
    ///
    bool m_PrintAST;

    ///\brief Flag toggling the AST printing on or off.
    ///
    bool m_PrintIR;

    ///\brief Flag toggling the dynamic scopes on or off.
    ///
    bool m_DynamicLookupEnabled;

    ///\brief Flag toggling the raw input on or off.
    ///
    bool m_RawInputEnabled;

    ///\brief Interpreter callbacks.
    ///
    llvm::OwningPtr<InterpreterCallbacks> m_Callbacks;

    ///\brief Interpreter adaptor to clang interfaces.
    ///
    ClingCallbackAdaptor* m_CallbackAdaptor; // owned, but d'tor hidden

    ///\breif Helper that manages when the destructor of an object to be called.
    ///
    /// The object is registered first as an CXAAtExitElement and then cling
    /// takes the control of it's destruction.
    ///
    struct CXAAtExitElement {
      ///\brief Constructs an element, whose destruction time will be managed by
      /// the interpreter. (By registering a function to be called by exit
      /// or when a shared library is unloaded.)
      ///
      /// Registers destructors for objects with static storage duration with
      /// the _cxa atexit function rather than the atexit function. This option
      /// is required for fully standards-compliant handling of static
      /// destructors(many of them created by cling), but will only work if
      /// your C library supports __cxa_atexit (means we have our own work
      /// around for Windows). More information about __cxa_atexit could be
      /// found in the Itanium C++ ABI spec.
      ///
      ///\param [in] func - The function to be called on exit or unloading of
      ///                   shared lib.(The destructor of the object.)
      ///\param [in] arg - The argument the func to be called with.
      ///\param [in] dso - The dynamic shared object handle.
      ///\param [in] fromTLD - The unloading of this top level declaration will
      ///                      trigger the atexit function.
      ///
      CXAAtExitElement(void (*func) (void*), void* arg, void* dso,
                       clang::Decl* fromTLD):
        m_Func(func), m_Arg(arg), m_DSO(dso), m_FromTLD(fromTLD) {}

      ///\brief The function to be called.
      ///
      void (*m_Func)(void*);

      ///\brief The single argument passed to the function.
      ///
      void* m_Arg;

      /// \brief The DSO handle.
      ///
      void* m_DSO;

      ///\brief Clang's top level declaration, whose unloading will trigger the
      /// call this atexit function.
      ///
      clang::Decl* m_FromTLD;
    };

    ///\brief Static object, which are bound to unloading of certain declaration
    /// to be destructed.
    ///
    std::vector<CXAAtExitElement> m_AtExitFuncs;

    ///\brief DynamicLibraries loaded by this Interpreter.
    ///
    llvm::OwningPtr<DynLibSetBase> m_DyLibs;

    ///\brief Information about loaded files.
    ///
    std::vector<LoadedFileInfo*> m_LoadedFiles;

    ///\brief Try to load a library file via the llvm::Linker.
    ///
    LoadLibResult tryLinker(const std::string& filename, bool permanent,
                            bool isAbsolute, bool& exists, bool& isDyLib);

    void addLoadedFile(const std::string& name, LoadedFileInfo::FileType type,
                       const llvm::sys::DynamicLibrary* dyLib = 0);

    ///\brief Processes the invocation options.
    ///
    void handleFrontendOptions();

    ///\brief Worker function, building block for interpreter's public
    /// interfaces.
    ///
    ///\param [in] input - The input being compiled.
    ///\param [in] CompilationOptions - The option set driving the compilation.
    ///\param [out] T - The cling::Transaction of the compiled input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult DeclareInternal(const std::string& input,
                                      const CompilationOptions& CO,
                                      Transaction** T = 0) const;

    ///\brief Worker function, building block for interpreter's public
    /// interfaces.
    ///
    ///\param [in] input - The input being compiled.
    ///\param [in] CompilationOptions - The option set driving the compilation.
    ///\param [in,out] V - The result of the evaluation of the input. Must be
    ///       initialized to point to the return value's location if the 
    ///       expression result is an aggregate.
    ///\param [out] T - The cling::Transaction of the compiled input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult EvaluateInternal(const std::string& input,
                                       const CompilationOptions& CO,
                                       StoredValueRef* V = 0,
                                       Transaction** T = 0);

    ///\brief Decides whether the input line should be wrapped or not by using
    /// simple lexing to determine whether it is known that it should be on the
    /// global scope or not.
    ///
    ///\param[in] input - The input being scanned.
    ///
    ///\returns true if the input should be wrapped.
    ///
    bool ShouldWrapInput(const std::string& input);

    ///\brief Wraps a given input.
    ///
    /// The interpreter must be able to run statements on the fly, which is not
    /// C++ standard-compliant operation. In order to do that we must wrap the
    /// input into a artificial function, containing the statements and run it.
    ///\param [out] input - The input to wrap.
    ///\param [out] fname - The wrapper function's name.
    ///
    void WrapInput(std::string& input, std::string& fname);

    ///\brief Runs given function.
    ///
    ///\param [in] fname - The function name.
    ///\param [in,out] res - The return result of the run function. Must be
    ///       initialized to point to the return value's location if the 
    ///       expression result is an aggregate.
    ///
    ///\returns The result of the execution.
    ///
    ExecutionResult RunFunction(const clang::FunctionDecl* FD,
                                StoredValueRef* res = 0);

    ///\brief Forwards to cling::ExecutionContext::addSymbol.
    ///
    bool addSymbol(const char* symbolName,  void* symbolAddress);

    ///\brief Ignores meaningless diagnostics in the context of the incremental
    /// compilation. Eg. unused expression warning and so on.
    ///
    void ignoreFakeDiagnostics() const;

  public:

    void unload();

    Interpreter(int argc, const char* const *argv, const char* llvmdir = 0);
    virtual ~Interpreter();

    const InvocationOptions& getOptions() const { return m_Opts; }
    InvocationOptions& getOptions() { return m_Opts; }

    const llvm::LLVMContext* getLLVMContext() const {
      return m_LLVMContext.get();
    }

    llvm::LLVMContext* getLLVMContext() { return m_LLVMContext.get(); }

    const LookupHelper& getLookupHelper() const { return *m_LookupHelper; }

    const clang::Parser& getParser() const;

    clang::CodeGenerator* getCodeGenerator() const;

    ///\brief Shows the current version of the project.
    ///
    ///\returns The current svn revision (svn Id).
    ///
    const char* getVersion() const;

    ///\brief Creates unique name that can be used for various aims.
    ///
    void createUniqueName(std::string& out);

    ///\brief Get the mangled name of a NamedDecl.
    ///
    ///\param [in]  D - try to mangle this decl's name.
    ///\param [out] mangledName - put the mangled name in here.
    ///
    void maybeMangleDeclName(const clang::NamedDecl* D, 
                             std::string& mangledName) const;

    ///\brief Checks whether the name was generated by Interpreter's unique name
    /// generator.
    ///
    ///\param[in] name - The name being checked.
    ///
    ///\returns true if the name is generated.
    ///
    bool isUniqueName(llvm::StringRef name);

    ///\brief Super efficient way of creating unique names, which will be used
    /// as a part of the compilation process.
    ///
    /// Creates the name directly in the compiler's identifier table, so that
    /// next time the compiler looks for that name it will find it directly
    /// there.
    ///
    llvm::StringRef createUniqueWrapper();

    ///\brief Checks whether the name was generated by Interpreter's unique
    /// wrapper name generator.
    ///
    ///\param[in] name - The name being checked.
    ///
    ///\returns true if the name is generated.
    ///
    bool isUniqueWrapper(llvm::StringRef name);

    ///\brief Adds an include path (-I).
    ///
    void AddIncludePath(llvm::StringRef incpath);

    ///\brief Prints the current include paths that are used.
    ///
    ///\param[out] incpaths - Pass in a llvm::SmallVector<std::string, N> with
    ///       sufficiently sized N, to hold the result of the call.
    ///\param[in] withSystem - if true, incpaths will also contain system
    ///       include paths (framework, STL etc).
    ///\param[in] withFlags - if true, each element in incpaths will be prefixed
    ///       with a "-I" or similar, and some entries of incpaths will signal
    ///       a new include path region (e.g. "-cxx-isystem"). Also, flags
    ///       defining header search behavior will be included in incpaths, e.g.
    ///       "-nostdinc".
    void GetIncludePaths(llvm::SmallVectorImpl<std::string>& incpaths,
                         bool withSystem, bool withFlags);

    ///\brief Prints the current include paths that are used.
    ///
    void DumpIncludePath();

    ///\brief Compiles the given input.
    ///
    /// This interface helps to run everything that cling can run. From
    /// declaring header files to running or evaluating single statements.
    /// Note that this should be used when there is no idea of what kind of
    /// input is going to be processed. Otherwise if is known, for example
    /// only header files are going to be processed it is much faster to run the
    /// specific interface for doing that - in the particular case - declare().
    ///
    ///\param[in] input - The input to be compiled.
    ///\param[in,out] V - The result of the evaluation of the input. Must be
    ///       initialized to point to the return value's location if the 
    ///       expression result is an aggregate.
    ///\param[out] T - The cling::Transaction of the compiled input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult process(const std::string& input, StoredValueRef* V = 0,
                              Transaction** T = 0);

    ///\brief Parses input line, which doesn't contain statements. No code 
    /// generation is done.
    ///
    /// Same as declare without codegening. Useful when a library is loaded and
    /// the header files need to be imported.
    ///
    ///\param[in] input - The input containing the declarations.
    ///\param[out] T - The cling::Transaction of the parsed input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult parse(const std::string& input, 
                            Transaction** T = 0) const;

    ///\brief Looks for a already generated PCM for the given header file and 
    /// loads it.
    ///
    ///\param[in] headerFile - The header file for which a module should be 
    ///                        loaded.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult loadModuleForHeader(const std::string& headerFile);

    ///\brief Parses input line, which doesn't contain statements. Code 
    /// generation needed to make the module functional.
    ///
    /// Same as declare without most of the codegening.  Only a few 
    /// things, like inline function are codegened.  Useful when a 
    /// library is loaded and the header files need to be imported.
    ///
    ///\param[in] input - The input containing the declarations.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult parseForModule(const std::string& input);

    ///\brief Compiles input line, which doesn't contain statements.
    ///
    /// The interface circumvents the most of the extra work necessary to
    /// compile and run statements.
    ///
    /// @param[in] input - The input containing only declarations (aka
    ///                    Top Level Declarations)
    /// @param[out] T - The cling::Transaction of the input
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult declare(const std::string& input, Transaction** T = 0);

    ///\brief Compiles input line, which contains only expressions.
    ///
    /// The interface circumvents the most of the extra work necessary extract
    /// the declarations from the input.
    ///
    /// @param[in] input - The input containing only expressions
    /// @param[in,out] V - The value of the executed input. Must be
    ///       initialized to point to the return value's location if the 
    ///       expression result is an aggregate.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult evaluate(const std::string& input, StoredValueRef& V);

    ///\brief Compiles input line, which contains only expressions and prints
    /// out the result of its execution.
    ///
    /// The interface circumvents the most of the extra work necessary extract
    /// the declarations from the input.
    ///
    /// @param[in] input - The input containing only expressions.
    /// @param[in,out] V - The value of the executed input. Must be
    ///       initialized to point to the return value's location if the 
    ///       expression result is an aggregate.
    ///
    ///\returns Whether the operation was fully successful.
    /// 
    CompilationResult echo(const std::string& input, StoredValueRef* V = 0);

    ///\brief Compiles input line and runs.
    ///
    /// The interface is the fastest way to compile and run a statement or
    /// expression. It just wraps the input and runs the wrapper, without any
    /// other "magic"
    ///
    /// @param[in] input - The input containing only expressions.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult execute(const std::string& input);

    ///\brief Generates code for all Decls of a transaction.
    ///
    /// @param[in] T - The cling::Transaction that contains the declarations and
    ///                the compilation/generation options.
    ///
    ///\returns Whether the operation was fully successfil.
    ///
    CompilationResult emitAllDecls(Transaction* T);

    ///\brief Loads header file or shared library.
    ///
    ///\param [in] filename - The file to loaded.
    ///\param [in] allowSharedLib - Whether to try to load the file as shared
    ///                             library.
    ///
    ///\returns result of the compilation.
    ///
    CompilationResult loadFile(const std::string& filename,
                               bool allowSharedLib = true);

    ///\brief Loads a shared library.
    ///
    ///\param [in] filename - The file to loaded.
    ///\param [in] permanent - If false, the file can be unloaded later.
    ///\param [out] tryCode - If not NULL, it will be set to false if this file
    ///        cannot be included.
    ///
    ///\returns kLoadLibSuccess on success, kLoadLibExists if the library was
    /// already loaded, kLoadLibError if the library cannot be found or any
    /// other error was encountered.
    ///
    LoadLibResult loadLibrary(const std::string& filename, bool permanent,
                              bool *tryCode = 0);

    ///\brief Get the collection of loaded files.
    ///
    const std::vector<LoadedFileInfo*>& getLoadedFiles() const {
      return m_LoadedFiles; }

    bool isPrintingAST() const { return m_PrintAST; }
    void enablePrintAST(bool print = true) { m_PrintAST = print; }

    bool isPrintingIR() const { return m_PrintIR; }
    void enablePrintIR(bool print = true) { m_PrintIR = print; }

    void enableDynamicLookup(bool value = true);
    bool isDynamicLookupEnabled() const { return m_DynamicLookupEnabled; }

    bool isRawInputEnabled() const { return m_RawInputEnabled; }
    void enableRawInput(bool raw = true) { m_RawInputEnabled = raw; }

    clang::CompilerInstance* getCI() const;
    const clang::Sema& getSema() const;
    clang::Sema& getSema();
    llvm::ExecutionEngine* getExecutionEngine() const;

    llvm::Module* getModule() const;

    //FIXME: This must be in InterpreterCallbacks.
    void installLazyFunctionCreator(void* (*fp)(const std::string&));
    void suppressLazyFunctionCreatorDiags(bool suppressed = true);

    //FIXME: Terrible hack to let the IncrementalParser run static inits on
    // transaction completed.
    ExecutionResult runStaticInitializersOnce(const Transaction& T) const;

    ExecutionResult runStaticDestructorsOnce();

    int CXAAtExit(void (*func) (void*), void* arg, void* dso);

    ///\brief Evaluates given expression within given declaration context.
    ///
    ///\param[in] expr - The expression.
    ///\param[in] DC - The declaration context in which the expression is going
    ///                to be evaluated.
    ///\param[in] ValuePrinterReq - Whether the value printing is requested.
    ///
    ///\returns The result of the evaluation if the expression.
    ///
    StoredValueRef Evaluate(const char* expr, clang::DeclContext* DC,
                            bool ValuePrinterReq = false);

    ///\brief Interpreter callbacks accessors.
    /// Note that this class takes ownership of any callback object given to it.
    ///
    void setCallbacks(InterpreterCallbacks* C);
    const InterpreterCallbacks* getCallbacks() const {return m_Callbacks.get();}
    InterpreterCallbacks* getCallbacks() { return m_Callbacks.get(); }

    // FIXME: remove once modules are there; see
    // DeclCollector::HandleTopLevelDecl().
    clang::ASTDeserializationListener* getASTDeserializationListener() const;

    const Transaction* getFirstTransaction() const;

    ///\brief Gets the address of an existing global and whether it was JITted.
    ///
    /// JIT symbols might not be immediately convertible to e.g. a function
    /// pointer as their call setup is different.
    ///
    ///\param[in]  D       - the global's Decl to find
    ///\param[out] fromJIT - whether the symbol was JITted.
    ///
    void* getAddressOfGlobal(const clang::NamedDecl* D, bool* fromJIT = 0) const;

    ///\brief Gets the address of an existing global and whether it was JITted.
    ///
    /// JIT symbols might not be immediately convertible to e.g. a function
    /// pointer as their call setup is different.
    ///
    ///\param[in]  SymName - the name of the global to search
    ///\param[out] fromJIT - whether the symbol was JITted.
    ///
    void* getAddressOfGlobal(const char* SymName, bool* fromJIT = 0) const;

    ///\brief Asks clang::CodeGen::CodeGenTypes for the low level (llvm) type of
    /// a given QualType.
    ///
    ///\param [in] QT - The QualType.
    ///
    ///\returns The llvm::Type corresponing to the given QualType.
    ///
    const llvm::Type* getLLVMType(clang::QualType QT);

    friend class runtime::internal::LifetimeHandler;
  };

  namespace internal {
    // Force symbols needed by runtime to be included in binaries.
    void symbol_requester();
    static struct ForceSymbolsAsUsed {
      ForceSymbolsAsUsed(){
        // Never true, but don't tell the compiler.
        // Prevents stripping the symbol due to dead-code optimization.
        if (std::getenv("bar") == (char*) -1) symbol_requester();
      }
    } sForceSymbolsAsUsed;
  }
} // namespace cling

#endif // CLING_INTERPRETER_H
