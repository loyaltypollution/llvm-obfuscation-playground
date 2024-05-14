set(BUILD_SHARED_LIBS OFF)

llvm_add_library(Goron SHARED
  CryptoUtils.cpp
  Substitution.cpp
  BogusControlFlow.cpp
  Utils.cpp
  SplitBasicBlock.cpp
  Flattening.cpp
  Plugin.cpp
  )
  