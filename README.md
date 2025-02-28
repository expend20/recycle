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

# Issues to address later

- [ ] Can optimizer explore blocks?
- [ ] Why is it needed to run optimize twice?
- [ ] How do I build/test on Github Actions?