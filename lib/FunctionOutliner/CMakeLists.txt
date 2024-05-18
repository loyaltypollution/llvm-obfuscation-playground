set(BUILD_SHARED_LIBS OFF)

add_llvm_pass_plugin(LLVMFunctionOutliner source.cpp)