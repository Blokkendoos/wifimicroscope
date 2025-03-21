# WiFi Microscope proof-of-concept

This code is based on the [CHZ-Soft](https://github.com/czietz/wifimicroscope) proof-of-concept.
A C++ version giving a somewhat better performance than the Python-code.
For use with the MS5 WiFi Microscope.

![Screenshot](Screenshot.png?raw=true "Screenshot")

### Dependencies
- OpenCV 4
- Qt 5

### Build and run
```C++
git clone https://github.com/blokkendoos/wifimicroscope
cd wifimicroscope/Cpp
mkdir build && cd build
qmake ..
make -j$(nproc)

# Connect to the Microscope WiFi network first, then run:
./WiFiMicroscope
```
