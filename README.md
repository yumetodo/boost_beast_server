# boost_beast_server

A simple wrapper of boost.beast

## Dependency

## boost beast

boost 1.72 or later is required.

```powershell
.\vcpkg install boost-beast
```

## How to build

Use CMake.

```bash
mkdir build
cd build
cmake ..
make
sudo make install
```

For msys2 enviroment, don't forget to pass `-G "MSYS Makefiles"`.

## Usage

See `example/example.cpp`.
