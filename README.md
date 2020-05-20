Build:
a. Cross Compiling
   1. Setup 'G8R100A0' as instructed.
   2. make
b. For Linux host
   make

c. Cross Compile for Android:
   We support this just for test, since we don't have CGW device right now.
   1. Download & Extract Android NDK, for example, to ~/Android/Ndk
   2. ~/Android/Ndk/ndk-build -C ./
   3. The executables are generated to folder "libs/arm64-v8a"


Run:
a. As a socket server
  orchestrator -o
b. As a http server
  orchestrator

Test:
a. To test socket server
   nc 127.0.0.1 8018
b. To test http server
   curl http://127.0.0.1:8018/test/livei
   curl --header "Content-Type: application/json" -d "{\"dev_id\":\"VDCM\",\"payload\":{\"url\":\"VDCM\"}}" http://localhost:8018/pkg/new
   curl --header "Content-Type: application/json" -d "{\"dev_id\":\"xxx\",\"payload\":{\"url\":\"127.0.0.1\"}}" http://localhost:8018/pkg/new
