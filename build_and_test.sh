set -e

cd "$(dirname "$0")"

rm -rf build
mkdir build && cd build

cmake ..

make -j$(nproc)

ctest --output-on-failure
