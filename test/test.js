const { Rtsp } = require("../");

const rtsp = new Rtsp();
rtsp.open("rtsp://192.168.3.239/ch0/main", (data) =>{
    console.log(data.length);
});

setTimeout(()=>{
    rtsp.close();
}, 10000);
