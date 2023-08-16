# conan.lock creation

```
rm conan.lock
conan lock create conanfile.py --version="1.0.0" --update
```

# Build

```
rm -rf build
conan lock create conanfile.py --version "1.0.0" --lockfile=conan.lock --lockfile-out=build/conan.lock
conan install conanfile.py --lockfile=build/conan.lock -of build --build=missing

cmake --preset conan-release
cmake --build --preset conan-release -j4
```


# Usage

Run the server and ...

curl "http://localhost:8080/bigfile?total_size=1000000&chunk_size=5000&delay_ms=100" --output output.bin
curl "http://localhost:8080/bigfile?total_size=10000000&chunk_size=5000&delay_ms=10" --output output.bin
curl "http://localhost:8080/bigfile?total_size=100000000&chunk_size=5000&delay_ms=10" --output output.bin
curl "http://localhost:8080/bigfile?total_size=1000000000&chunk_size=5000&delay_ms=0" --output output.bin

