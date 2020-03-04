Build:
a. Cross Compiling
   1. Setup 'G8R100A0' as instructed.
   2. make
b. For Linux host
   make

Run:
a. As a socket server
  orchestrator -o
b. As a http server
  orchestrator

Test:
a. To test socket server
   nc 127.0.0.1 8018
b. To test http server
   curl http://127.0.0.1:8018/test/live
