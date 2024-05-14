set(BUILD_SHARED_LIBS OFF)

llvm_add_library(Hikari SHARED
  FunctionCallObfuscate.cpp
  CryptoUtils.cpp
  BogusControlFlow.cpp
  SubstituteImpl.cpp
  Substitution.cpp
  Flattening.cpp
  Utils.cpp
  SplitBasicBlocks.cpp
  AntiClassDump.cpp
  AntiDebugging.cpp
  AntiHooking.cpp
  StringEncryption.cpp
  IndirectBranch.cpp
  FunctionWrapper.cpp
  ConstantEncryption.cpp
  Obfuscation.cpp
  Plugin.cpp
  )