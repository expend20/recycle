# build

```
cmake \
    -DCMAKE_TOOLCHAIN_FILE=/home/john/git/vcpkg_ubuntu-22.04_llvm-17_amd64/scripts/buildsystems/vcpkg.cmake \
    -DLLVM_DIR=/home/john/git/vcpkg_ubuntu-22.04_llvm-17_amd64/installed/x64-linux-rel/share/llvm \
    -DXED_DIR=/home/john/git/vcpkg_ubuntu-22.04_llvm-17_amd64/installed/x64-linux-rel/share/xed \
    -Dglog_DIR=/home/john/git/vcpkg_ubuntu-22.04_llvm-17_amd64/installed/x64-linux-rel/share/glog \
    -DZ3_DIR=/home/john/git/lifting-bits-downloads/vcpkg_ubuntu-22.04_llvm-17_amd64/installed/x64-linux-rel/share/z3 \
    -Dgflags_DIR=/home/john/git/vcpkg_ubuntu-22.04_llvm-17_amd64/installed/x64-linux-rel/share/gflags \
    -B build -G Ninja \
    && \
cmake --build build --config Release

```

# run

```
./build/bin/remill-lift --arch x86_64 --os linux --entrypoint main --output-format=llvmbc test.ll
```
