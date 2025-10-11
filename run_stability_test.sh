set -e

mkdir -p build
cd build
cmake ..
make

./test_stabilnosci_2_synchroniczny
