
var lastUpdateTime="0";
var client;
var username=0;
var password=0;
var appname = 'Kongfu';
var thingkey = "2bc34272-ac27-4f3b-877d-fc8d759fb683-kongfu";
var sendcase = 0;
var localdir = '';

var firstInit = true;
var SEND_LOG = 0;
var SEND_IMG = 1;
var SEND_PMT = 2;
var IMG_MAX = 6;
var REC_MAX = 5;
var LOG_MAX = 200;
var image_pointer = 0;
var image_json = [
	{"name": ""},
	{"name": ""},
	{"name": ""},
	{"name": ""},
	{"name": ""},
    {"name": ""}
];

function initParam(){
    var url = location.href; 
    var paraString = url.substring(url.indexOf("?")+1,url.length).split("&"); 
    
    for (i=0; j=paraString[i]; i++){
        var key = j.substring(0,j.indexOf("="));
        var value = j.substring(j.indexOf("=")+1,j.length);
        if (key == "username")
            {
            username = decodeURIComponent(value);
            }
        if (key == "password")
            {
            password = decodeURIComponent(value);
            }
        }
        
    client = new Paho.MQTT.Client('api.devicecloud.windriver.com', 443, '/mqtt-ssl', 'Kongfu');

    client.onConnectionLost = onConnectionLost;
    client.onMessageArrived = onMessageArrived;
    client.connect({
    userName : username,
    password : password,
    useSSL: true,
    mqttVersion : 3,
    onSuccess : onConnect,
    onFailure : onFailure
    });
}

function getLog () {
    var cmd = JSON.stringify({
        "cmd": {
        "command": "attribute.history",
        "params": {
        "thingKey": thingkey,
        "key": "sec_record",
        "records": LOG_MAX
        }
        }
    });

    sendcase = SEND_LOG;
    var message = new Paho.MQTT.Message(cmd);
    message.destinationName = 'api';
    client.send(message);
}

function onConnect() {
    console.log('Connected.');
    
    getLog();
}

function onFailure(responseObject) {
    console.log('Failed to connect: ' + responseObject.errorMessage);
}

function onConnectionLost(responseObject) {
    console.log('Connection lost: ' + responseObject.errorMessage);
}

function downloadImg(fileIdx)
{
    var cmd = JSON.stringify({
        "cmd": {
        "command": "file.get",
        "params": {
        "thingKey": thingkey,
        "fileName": image_json[fileIdx].name + '.jpg'
        }
        }
    });

    sendcase = SEND_IMG;
    var message = new Paho.MQTT.Message(cmd);
    message.destinationName = 'api';
    client.send(message);
}

function showImg(img)
{
    var fileurl;
    
    if (img.cmd.success == true){
        fileid = img.cmd.params.fileId;
    }
    else
        return;
    
    fileurl = 'http://api.devicecloud.windriver.com/file/' + fileid;
    
    $('#current_image_section img').attr("src", fileurl);
}

function logParse(log) {
    var first = true;
	var filePointer = image_pointer;
	var fileNum = 0;
    var recNum = 0;
    var lastUpdateTimeTmp = "0";

    if (log.cmd.success == true){
        var logmax = log.cmd.params.count;
        
        if (logmax > LOG_MAX)
            logmax = LOG_MAX;
        for (i=(logmax-1); i >= 0; i--){
            if (fileNum < IMG_MAX && log.cmd.params.values[i].value.indexOf("warning, unknown person") != -1)
            {
            var time = log.cmd.params.values[i].ts;
                
            if (time > lastUpdateTime)
                {
				fileNum++;
                var msg = log.cmd.params.values[i].value;
                var filename = msg.split("person")[1].replace(/(^\s*)|(\s*$)/g, "");
                
                if (first == true)
                    {
                    lastUpdateTimeTmp = time;
                    first = false;
                    }
                
                if (filename != '')
                    {
                    image_json[filePointer].name = filename;
                    document.getElementById('img'+filePointer).innerHTML = filename;
                    filePointer=(filePointer+1)%IMG_MAX;
                    }
                }
            else
                {
                break;
                }
            }
            
            if (recNum < REC_MAX && log.cmd.params.values[i].value.indexOf("hello, welcome") != -1)
            {
            var msg = log.cmd.params.values[i].value;
            var recname = msg.split("welcome")[1].replace(/(^\s*)|(\s*$)/g, "");
            
            document.getElementById('rec'+recNum).innerHTML = recname;
            recNum++;
            }
        }
        
        if (lastUpdateTimeTmp != "0")
            {
            lastUpdateTime = lastUpdateTimeTmp;
            }
        if (firstInit == true)
            {
            downloadImg(image_pointer);
            firstInit = false;
            }
    }
}

function onMessageArrived(message) {
    console.log(message.destinationName + ': ' + message.payloadString);
    
    var jsonstruct = JSON.parse (message.payloadString);
    
    if (sendcase == SEND_LOG)
        {
        logParse(jsonstruct);
        }
    else if (sendcase == SEND_IMG)
        {
        showImg(jsonstruct);
        }
    else if (sendcase == SEND_PMT)
        {
        alert('permit success');
        }
}

$(document).ready(function(){
    $("button").click(function(){
        var element = document.getElementById("button_turn");
        permit();
    })
});

function permit()
{
    var cmd = JSON.stringify({
        "cmd": {
        "command": "method.exec",
        "params": {
            "thingKey": "2bc34272-ac27-4f3b-877d-fc8d759fb683-kongfu",
            "method": "permit",
            "ackTimeout": 30
        }
        }
    });

    sendcase = SEND_PMT;
    var message = new Paho.MQTT.Message(cmd);
    message.destinationName = 'api';
    client.send(message);
}

function pressImg(imageIdx)
{
	var content = 'img' + imageIdx;
    
    if (imageIdx != image_pointer && image_json[imageIdx].name != "")
        {
        document.getElementById(content).className = "menu-item selected";
        content = 'img' + image_pointer;
        document.getElementById(content).className = "menu-item";
        image_pointer = imageIdx;
        downloadImg(imageIdx);
        }
}

initParam();
timename=setInterval("getLog();", 100000);

