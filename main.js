const { Rtsp } = require('./build/Release/rtsp.node');

const rtsp = new Rtsp();

rtsp.open("rtsp://192.168.3.239/ch0/main");

for (;;) {
   const buff = rtsp.read();
   console.log(buff && buff.length);
}

rtsp.close(); 