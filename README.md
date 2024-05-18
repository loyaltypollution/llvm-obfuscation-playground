# LLVM Obfuscation Playground

There are *a lot* of LLVM-based obfuscators out in the wild. This repository makes it (far) easier to get them up and running, using out-of-tree LLVM compilation.

Out-of-tree LLVM compilation differs in that, instead of compiling the *entire* LLVM project, we only compile the code as objects that are loaded by respective LLVM tools.

Supported LLVM versions:
- LLVM 16.0

### List of passes

| Projects     | Description                                                      | Author                                                  |
| :----------- | :--------------------------------------------------------------- | :------------------------------------------------------ |
| Goron        | [from goron project](https://github.com/amimo/goron)             | [amimo](https://github.com/amimo)                       |
| Hikari       | [from hikari project](https://github.com/61bcdefg/Hikari-LLVM15) | [*maintained by](https://github.com/61bcdefg)           |
| Loyalty      | stack-string obfuscation, outlining                              | [loyaltypollution](https://github.com/loyaltypollution) |
| Callfuscator | Per-instruction outlining to functions                           | [d0minik](https://github.com/d0mnik)                    |

### Loyalty
| Pass              | Description                                              |
| :---------------- | :------------------------------------------------------- |
| `-enable-sstring` | Performs stack-string obfuscation on all global strings. |
| `-enable-outline` | Performs outlining on all BinaryOperator instructions.   |


### Callfuscator

| Pass           | Description                              |
| :------------- | :--------------------------------------- |
| `outlinefuncs` | Performs outlinining on all instructions |


### Dockerfile

The project comes with a .devcontainer Dockerfile that can be used to spin up debian container with LLVM 16.0 pre-installed.

## Compiling out-of-tree procedure

The [default standard](https://llvm.org/docs/WritingAnLLVMNewPMPass.html) for pass-writing seems to build the entire project. After all, the introductory LLVM documentation also does so.

Here's how we can modify existing passes for out-of-tree compilation:
1. Place the folder containing the LLVM passes into `lib\[projectname]`. Likely, we will be looking at these directories:

headers:
- `llvm/include/llvm/Analysis/[projectname]`
- `llvm/include/llvm/Transforms/[projectname]`

source:
- `llvm/lib/Analysis/[projectname]`
- `llvm/lib/Transforms/[projectname]`
- `llvm/lib/[projectname]`
2. Rename all include paths. For instance, in-tree directories will place these header files in their respective directories.
```C
#include "llvm/Transforms/Obfuscation/AntiHook.h"
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
```
Instead, since we got lazy and placed everything in the same place, we have:
```C
#include "AntiHook.h"
#include "CryptoUtils.h"
```
3. In order to “register” the pass, we typically add it to `llvm/lib/Passes/PassRegistry.def` and so on.
    - When compiling out-of-tree, we add a new cpp file that loads in the plugin by interfacing with the PassBuilder object via `registerPipelineParsingCallback` directly. 
    - PassBuilder also exposes other public methods for us to register our passes.
    - Example:
```cpp
llvm::PassPluginLibraryInfo getObfuscationPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "[passname]", LLVM_VERSION_STRING,
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback([](StringRef Name, ModulePassManager &MPM,
                                              ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "...") {
            MPM.addPass(...);
            return true;
          }
          return false;
        });
      }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getObfuscationPluginInfo();
}
```
4. ensure the project has a CMakeLists.txt file that builds the project as an LLVM shared library. example:
```CMake
set(BUILD_SHARED_LIBS OFF)

llvm_add_library([projectname] SHARED
    [...].cpp
  )
```
5. upon building, we load all shared objects via
```bash
opt-16 -load-pass-plugin [built_object_path]
```
and continue usage as per normal
