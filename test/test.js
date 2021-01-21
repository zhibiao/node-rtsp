const { Rtsp } = require("../");

//const url = "rtsp://admin:20151201zl@192.168.1.222:554?id=0";
//const url = "rtsp://192.168.3.239/ch0/main";
const url = "";


const rtsp = new Rtsp();
    rtsp.open(url, (data) =>{
        console.log(data.length);
    });
    

/*
setInterval(() =>{



    const rtsp = new Rtsp();
    rtsp.open(url, (data) =>{
        console.log(data.length);
    });
    
    setTimeout(()=>{
        rtsp.close();
    },3*1000);
    





}, 5000);

*/