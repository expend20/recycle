# Env setup

Use Ubuntu 22.04, go to Remill's readme and build remill with:

```
./remill/scripts/build.sh
```

Then install it to the system with:

```
cd ./remill-build
sudo make install
```

# Build

Reuse the vcpkg toolchain file and build with:

```
cmake \
    -DCMAKE_TOOLCHAIN_FILE=/home/john/git/vcpkg_ubuntu-22.04_llvm-17_amd64/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=x64-linux-rel \
    -G Ninja \
    -B build \
    && \
cmake --build build --config Release
```

# Thoughts

- [x] Can optimizer explore blocks?
    - If you optimize something early and lift something which is actually using that later (e.g. stack write), it's a problem (upd: not really because of stack could be external variable for some period or you can disable some optimizations e.g. DSE)
    - If you've inlined a basic block (function) and there is a jump on it again, it's nowhere to jump (upd: keep lifted ones and merge each time?)
- [ ] How do I lift it properly?
  - pass to replace write memory to GEP
- [ ] Why is it needed to run optimize twice?
- [ ] How do I build/test on Github Actions?

# Memory manager

no write - ok: https://godbolt.org/z/Kh16Eqz14
with write - fail: https://godbolt.org/z/98K1MKEK5

# Missing block handler

https://godbolt.org/z/rr5sreW4x

# Troubleshooting

Print all passes

```
opt-18 -O2 -debug-pass-manager -disable-output .ll
```

