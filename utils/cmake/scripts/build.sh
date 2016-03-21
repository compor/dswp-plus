CC=clang \
cmake \
  -DLLVM_DIR=$(llvm-config --prefix)/share/llvm/cmake/ .. \
  -DCMAKE_BUILD_TYPE=DEBUG -DCMAKE_INSTALL_PREFIX=../install \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

