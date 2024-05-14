set(BUILD_SHARED_LIBS OFF)

llvm_add_library(Loyalty SHARED
  AddOutliner.cpp
  CryptoUtils.cpp
  Utils.cpp
  StackString.cpp
  Plugin.cpp
  )