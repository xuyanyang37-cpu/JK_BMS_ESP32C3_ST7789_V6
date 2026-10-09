/*
 * ================================================================
 * WebConfig.cpp - 热点网页配置层
 *
 * 只有进入 BOOT_HOTSPOT 后，main.cpp 才会调用 begin()。
 *
 * 页面请求流程：
 *   手机 -> 192.168.4.1
 *        -> WebServer
 *        -> handleXxx()
 *        -> BmsBle / Preferences / g_bmsData
 *
 * 本文件不解析原始BMS帧。
 * ================================================================
 */

#include "WebConfig.h"
#include <WiFi.h>
#include <Preferences.h>
#include <pgmspace.h>

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>JK BMS 设置</title>
<style>
*{box-sizing:border-box}
body{font-family:Arial,"Microsoft YaHei",sans-serif;background:#0b1015;color:#eee;margin:0;padding:14px}
.card{max-width:760px;margin:auto;background:#151c23;border-radius:16px;padding:18px;box-shadow:0 5px 25px #000}
h2{margin:0 0 12px}
button{padding:11px 15px;margin:5px;border:0;border-radius:9px;background:#1976d2;color:#fff;font-size:15px}
input,select{padding:11px;margin:5px 0;width:100%;border-radius:8px;border:1px solid #4a5662;background:#0d1217;color:#fff}
.item{padding:12px;border:1px solid #394652;border-radius:10px;margin:8px 0;background:#10161c}
.row{display:flex;gap:8px;align-items:center;justify-content:space-between}
.small{color:#aeb8c2;font-size:13px}
.ok{color:#45e27b}.warn{color:#ffc107}
.data{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:12px}
.data div{background:#0e141a;border-radius:9px;padding:10px}
.num{font-size:20px;font-weight:bold;color:#55d9ff}
.tip{color:#8e9aa5;font-size:13px;margin:4px 0 10px}
</style>
</head>
<body>
<div class="card">
<h2>JK BMS 蓝牙设置</h2>
<div id="status" class="small">读取状态...</div>

<label>保护板 MAC（可留空自动扫描）</label>
<input id="mac" placeholder="例如 AA:BB:CC:DD:EE:FF">

<label>协议型号</label>
<select id="proto">
<option value="32">JK02_32S</option>
<option value="24">JK02_24S</option>
</select>

<label>每公里耗电量（Wh/km）</label>
<input id="consumption" type="number" min="1" max="1000" step="1" value="100">
<div class="tip">用于估算剩余公里数：剩余容量 × 电压 ÷ 每公里耗电量</div>

<div>
<button onclick="scan()">扫描蓝牙电池</button>
<button onclick="save()">保存参数</button>
<button onclick="location.href='/protection'">保护板设置</button><button onclick="location.href='/display'">屏幕显示设置</button>
</div>
<div id="list"></div>

<div class="data">
<div>电压<br><span id="v" class="num">0 V</span></div>
<div>电流<br><span id="i" class="num">0 A</span></div>
<div>功率<br><span id="p" class="num">0 W</span></div>
<div>剩余容量<br><span id="ah" class="num">0 Ah</span></div>
<div>剩余公里数<br><span id="km" class="num">0 km</span></div>
<div>SOC<br><span id="soc" class="num">0%</span></div>
</div>
</div>

<script>
async function api(url,opt){return await (await fetch(url,opt)).json();}
let statusBusy=false;
async function status(){
  if(statusBusy)return;
  statusBusy=true;
  try{
    let s=await api('/api/status');
    document.getElementById('status').innerHTML=
      '<b class="'+(s.online?'ok':'warn')+'">'+(s.online?'已连接':'未连接')+
      '</b>　'+s.message+'<br>MAC: '+(s.mac||'未设置')+'　IP: '+s.ip;
    // 连接成功后，把实际连接的蓝牙地址自动回填到 MAC 输入框。
    if(s.mac) document.getElementById('mac').value=s.mac;
    if(s.proto) document.getElementById('proto').value=s.proto;
    document.getElementById('v').textContent=s.voltage.toFixed(2)+' V';
    document.getElementById('i').textContent=s.current.toFixed(2)+' A';
    document.getElementById('p').textContent=s.power.toFixed(0)+' W';
    document.getElementById('ah').textContent=s.remainingAh.toFixed(2)+' Ah';
    document.getElementById('km').textContent=s.remainingKm.toFixed(1)+' km';
    document.getElementById('soc').textContent=s.soc.toFixed(0)+'%';
    document.getElementById('consumption').value=s.consumption.toFixed(0);
  }catch(e){} finally { statusBusy=false; }
}
async function scan(){
  document.getElementById('list').innerHTML='正在扫描蓝牙电池，请等待 5 秒...';
  let r=await api('/api/scan');
  let h='<h3>扫描结果（点击连接）</h3>';
  if(!r.items.length) h+='<div class="item">没有找到 JK / BMS 设备</div>';
  r.items.forEach((x,i)=>{
    h+='<div class="item"><div class="row"><b>'+x.name+'</b><span>RSSI '+x.rssi+' dBm</span></div>'+
       '<div class="small">'+x.address+'</div>'+
       '<button onclick="connectTo('+i+')">连接此电池</button></div>';
  });
  document.getElementById('list').innerHTML=h;
}
async function connectTo(i){
  document.getElementById('status').textContent='正在连接选中的蓝牙电池...';
  let r=await api('/api/connect?index='+i);
  document.getElementById('status').textContent=r.message;
  if(r.mac) document.getElementById('mac').value=r.mac;
  status();
}
async function save(){
  let fd=new FormData();
  fd.append('mac',document.getElementById('mac').value);
  fd.append('proto',document.getElementById('proto').value);
  fd.append('consumption',document.getElementById('consumption').value);
  let r=await api('/api/save',{method:'POST',body:fd});
  alert(r.message);
  status();
}
status();
setInterval(status,2000);
</script>
</body></html>)HTML";

WebConfig::WebConfig():server_(80),ble_(nullptr),active_(false){}

String WebConfig::jsonEscape(const String& s){
  String o;
  for(size_t i=0;i<s.length();i++){
    char c=s[i];
    if(c=='"') o+="\\\"";
    else if(c=='\\') o+="\\\\";
    else if(c=='\n') o+="\\n";
    else if(c=='\r') o+="\\r";
    else if(c=='\t') o+="\\t";
    else o+=c;
  }
  return o;
}

String WebConfig::makeStatusJson(){
  String j;
  j.reserve(640);
  j="{";
  j+="\"online\":"+String(g_bmsData.online?"true":"false");
  j+=",\"state\":"+String((int)g_bmsData.bootState);
  j+=",\"message\":\""+jsonEscape(g_bmsData.statusMessage)+"\"";
  j+=",\"mac\":\""+jsonEscape(g_bmsData.mac.length()?g_bmsData.mac:ble_?ble_->getConfiguredAddress():"")+"\"";
  j+=",\"name\":\""+jsonEscape(g_bmsData.deviceName)+"\"";
  j+=",\"proto\":\""+jsonEscape(ble_?ble_->getPreferredProtocol():"NONE")+"\"";
  j+=",\"jkModel\":"+String(ble_ && ble_->isProtocol32S()?"32":"24");
  j+=",\"protoName\":\""+jsonEscape(ble_?ble_->getPreferredProtocol():"NONE")+"\"";
  j+=",\"ip\":\""+jsonEscape(WiFi.softAPIP().toString())+"\"";
  j+=",\"scanCount\":"+String(ble_?ble_->getScanCount():0);
  j+=",\"scanAttempt\":"+String(g_bmsData.scanAttempt);
  j+=",\"voltage\":"+String(g_bmsData.totalVoltage,3);
  j+=",\"current\":"+String(g_bmsData.current,3);
  j+=",\"power\":"+String(g_bmsData.power,1);
  j+=",\"remainingAh\":"+String(g_bmsData.remainingCapacityAh,2);
  j+=",\"remainingKm\":"+String(g_bmsData.remainingRangeKm,1);
  j+=",\"consumption\":"+String(g_bmsData.energyConsumptionWhKm,1);
  j+=",\"totalAh\":"+String(g_bmsData.totalCapacityAh,2);
  j+=",\"soc\":"+String(g_bmsData.soc,1);
  return j+"}";
}

// [网页流程1] 启动WebServer并注册所有HTTP接口。
void WebConfig::begin(BmsBle* ble){
  // 只初始化一次。热点模式下不要重复注册 WebServer 路由，
  // 否则会不断创建回调对象并造成堆碎片。
  if(active_) return;
  ble_=ble;
  active_=true;

  // 从 Preferences 读取上次保存的单位里程耗电量。
  Preferences p;
  p.begin("jkcfg",true);
  g_bmsData.energyConsumptionWhKm=p.getFloat("whkm",100.0f);
  p.end();

  if(g_bmsData.energyConsumptionWhKm<1.0f || g_bmsData.energyConsumptionWhKm>1000.0f)
    g_bmsData.energyConsumptionWhKm=100.0f;

  server_.on("/",HTTP_GET,[this](){handleRoot();});
  server_.on("/protection",HTTP_GET,[this](){handleProtection();});
  server_.on("/display",HTTP_GET,[this](){handleDisplay();});
  server_.on("/api/display",HTTP_GET,[this](){handleDisplayStatus();});
  server_.on("/api/status",HTTP_GET,[this](){handleStatus();});
  server_.on("/api/scan",HTTP_GET,[this](){handleScan();});
  server_.on("/api/connect",HTTP_GET,[this](){handleConnect();});
  server_.on("/api/save",HTTP_POST,[this](){handleSave();});
  server_.on("/api/display/save",HTTP_POST,[this](){handleDisplaySave();});
  server_.on("/api/protection/save",HTTP_POST,[this](){handleProtectionSave();});
  server_.on("/api/protection/clear",HTTP_POST,[this](){handleProtectionClear();});
  server_.on("/api/restart",HTTP_POST,[this](){handleRestart();});
  server_.onNotFound([this](){handleNotFound();});
  server_.begin();
}

// [网页流程2] 每次主循环处理一次HTTP请求。
void WebConfig::loop(){
  if(active_) server_.handleClient();
}

void WebConfig::handleRoot(){
  // 页面常量放在 Flash，访问网页时不再创建几 KB 的临时 String。
  server_.send_P(200,"text/html; charset=utf-8",WEB_PAGE);
}

/*
 * GET /display
 * ---------------------------------------------------------------
 * 返回屏幕配置页面。
 * 页面只负责收集用户设置，不直接操作Display对象。
 * 保存时通过POST /api/display/save提交。
 */
void WebConfig::handleDisplay(){
  static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>屏幕显示设置</title>
<style>
body{font-family:Arial,"Microsoft YaHei",sans-serif;background:#0b1015;color:#eee;margin:0;padding:14px}
.card{max-width:760px;margin:auto;background:#151c23;border-radius:16px;padding:18px}
.item{padding:12px;border:1px solid #394652;border-radius:10px;margin:9px 0;background:#10161c}
select,input{padding:9px;border-radius:8px;border:1px solid #4a5662;background:#0d1217;color:#fff}
select{width:48%}input[type=color]{width:52px;height:38px;padding:2px;vertical-align:middle}
button{padding:11px 15px;margin:5px;border:0;border-radius:9px;background:#1976d2;color:#fff;font-size:15px}
.small{color:#aeb8c2;font-size:13px}.row{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
</style></head><body><div class="card">
<h2>1.9寸屏幕显示设置</h2>
<div class="small">320×170 固定布局；只改变每个位置显示的数据、颜色和字体，不改变屏幕结构。</div>
<div id="rows"></div>
<div class="item">
<b>SOC / 左下区域</b>
<div class="row">SOC颜色 <input id="socColor" type="color"> 温度颜色 <input id="tempColor" type="color"> 容量颜色 <input id="capColor" type="color"></div>
<label><input id="bar" type="checkbox"> 显示底部SOC渐变条</label>
</div>
<button onclick="save()">保存并立即应用</button>
<button onclick="location.href='/'">返回主页</button>
<div id="msg" class="small"></div>
</div>
<script>
const metrics=[
["0","电压"],["1","电流"],["2","功率"],["3","最低电压"],["4","最高电压"],["5","平均电压"],
["6","单体压差"],["7","电芯数量"],["8","MOS温度"],["9","温度1"],["10","温度2"],
["11","剩余容量"],["12","总容量"],["13","剩余里程"],["14","SOC"],["15","循环次数"]];
let cfg;
function color16(v){let r=((v>>11)&31)*255/31,g=((v>>5)&63)*255/63,b=(v&31)*255/31;return "#"+[r,g,b].map(x=>Math.round(x).toString(16).padStart(2,"0")).join("");}
function hex16(s){let n=parseInt(s.slice(1),16);let r=(n>>16)&255,g=(n>>8)&255,b=n&255;return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
function build(){
 let h="";
 for(let i=0;i<4;i++){
   let x=cfg.row[i];
   h+='<div class="item"><b>第'+(i+1)+'行</b><div class="row">';
   h+='<select id="m'+i+'">'+metrics.map(m=>'<option value="'+m[0]+'" '+(Number(m[0])===x.metric?'selected':'')+'>'+m[1]+'</option>').join("")+'</select>';
   h+='<input id="c'+i+'" type="color" value="'+color16(x.color)+'">';
   h+='<select id="f'+i+'"><option value="2">字体2</option><option value="3">字体3</option><option value="4">字体4</option></select>';
   h+='</div></div>';
 }
 document.getElementById("rows").innerHTML=h;
 for(let i=0;i<4;i++)document.getElementById("f"+i).value=cfg.row[i].font;
 document.getElementById("socColor").value=color16(cfg.socColor);
 document.getElementById("tempColor").value=color16(cfg.tempColor);
 document.getElementById("capColor").value=color16(cfg.capacityColor);
 document.getElementById("bar").checked=cfg.socBar;
}
async function load(){cfg=await (await fetch("/api/display")).json();build();}
async function save(){
 let data=new URLSearchParams();
 for(let i=0;i<4;i++){
   data.append("m"+i,document.getElementById("m"+i).value);
   data.append("c"+i,document.getElementById("c"+i).value);
   data.append("f"+i,document.getElementById("f"+i).value);
 }
 data.append("socColor",document.getElementById("socColor").value);
 data.append("tempColor",document.getElementById("tempColor").value);
 data.append("capColor",document.getElementById("capColor").value);
 data.append("bar",document.getElementById("bar").checked?"1":"0");

 // Arduino WebServer原生稳定支持application/x-www-form-urlencoded。
 // 使用URLSearchParams而不是multipart FormData，避免ESP32端hasArg()收不到参数。
 let r=await (await fetch("/api/display/save",{
   method:"POST",
   headers:{"Content-Type":"application/x-www-form-urlencoded"},
   body:data.toString()
 })).json();
 document.getElementById("msg").textContent=r.message||"已保存";
 load();
}
load();
</script></body></html>)HTML";
  server_.send_P(200,"text/html; charset=utf-8",PAGE);
}

void WebConfig::handleProtection(){
  static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>保护板设置</title>
<style>
body{font-family:Arial,"Microsoft YaHei",sans-serif;background:#0b1015;color:#eee;margin:0;padding:14px}
.card{max-width:760px;margin:auto;background:#151c23;border-radius:16px;padding:18px}
button,select{padding:11px;margin:5px;border:0;border-radius:9px;font-size:15px}
button{background:#1976d2;color:#fff}.danger{background:#b3261e}.gray{background:#45515c}
.item{padding:12px;border:1px solid #394652;border-radius:10px;margin:9px 0;background:#10161c}
.small{color:#aeb8c2;font-size:13px}.num{font-size:24px;font-weight:bold;color:#55d9ff}
</style></head><body><div class="card">
<h2>← 保护板设置</h2>
<div class="item"><b>当前保护板</b><br><span id="proto" class="num">读取中...</span><div id="detail" class="small">正在读取状态...</div></div>
<div class="item"><b>手动选择保护板</b><br>
<select id="sel"><option value="JK">极空 JK</option><option value="ANT">蚂蚁 ANT</option><option value="JBD">JBD</option><option value="DALY">Daly</option><option value="TT">铁塔</option><option value="YANYANG">彦阳保护板</option></select>
<br><button onclick="save()">保存保护板</button><button class="gray" onclick="location.href='/'">返回主页</button>
</div>
<div class="item"><b>协议识别</b><br><button onclick="load()">刷新识别结果</button><button class="danger" onclick="clearProto()">清除已保存协议</button></div>
<div class="item"><b>系统</b><br><span class="small">重启 ESP32 不会删除已保存的蓝牙地址、协议和网页参数。</span><br><button onclick="restart()">重启 ESP32</button></div>
</div>
<script>
async function api(u,o){return await (await fetch(u,o)).json();}
async function load(){try{let s=await api("/api/status");let p=(s.protoName||s.proto||"NONE").toUpperCase();document.getElementById("proto").textContent=p;document.getElementById("detail").textContent="蓝牙："+(s.name||"未命名")+" MAC："+(s.mac||"未设置")+" 状态："+s.message;document.getElementById("sel").value=p;}catch(e){}}
async function save(){let fd=new FormData();fd.append("protocol",document.getElementById("sel").value);let r=await api("/api/protection/save",{method:"POST",body:fd});alert(r.message||"已保存");load();}
async function clearProto(){if(!confirm("清除已保存协议？"))return;let r=await api("/api/protection/clear",{method:"POST"});alert(r.message||"已清除");load();}
async function restart(){if(!confirm("确定重启 ESP32？"))return;try{await api("/api/restart",{method:"POST"});}catch(e){}document.getElementById("detail").textContent="ESP32 正在重启，请稍候...";}
load();setInterval(load,2000);
</script></body></html>)HTML";
  server_.send_P(200,"text/html; charset=utf-8",PAGE);
}

// [接口] 返回屏幕显示配置 JSON。
/*
 * GET /api/display
 * ---------------------------------------------------------------
 * 返回当前DisplayConfig给网页JavaScript。
 * 颜色保持RGB565整数，网页负责转换成HTML #RRGGBB。
 */
void WebConfig::handleDisplayStatus(){
  String j="{\"row\":[";
  for(uint8_t i=0;i<4;i++){
    if(i) j+=",";
    j+="{\"metric\":"+String(g_displayConfig.row[i].metric)+
      ",\"name\":\""+jsonEscape(displayMetricName(g_displayConfig.row[i].metric))+
      "\",\"color\":"+String(g_displayConfig.row[i].color)+
      ",\"font\":"+String(g_displayConfig.row[i].font)+"}";
  }
  j+="],\"socColor\":"+String(g_displayConfig.socColor)+
     ",\"tempColor\":"+String(g_displayConfig.tempColor)+
     ",\"capacityColor\":"+String(g_displayConfig.capacityColor)+
     ",\"socBar\":"+(String(g_displayConfig.socBar?"true":"false"))+"}";
  server_.send(200,"application/json; charset=utf-8",j);
}

// [接口] 返回当前BMS状态，前端定时刷新。
void WebConfig::handleStatus(){
  server_.send(200,"application/json; charset=utf-8",makeStatusJson());
}

// [接口] 网页手动扫描BLE设备。
void WebConfig::handleScan(){
  if(!ble_){
    server_.send(500,"application/json","{\"message\":\"BLE未初始化\",\"items\":[]}");
    return;
  }
  uint8_t count=ble_->scanDevices(5);
  String j="{\"message\":\"扫描完成\",\"items\":[";
  for(uint8_t i=0;i<count;i++){
    if(i) j+=",";
    const BmsScanItem& x=ble_->getScanItem(i);
    j+="{\"name\":\""+jsonEscape(x.name)+"\",\"address\":\""+
      jsonEscape(x.address)+"\",\"rssi\":"+String(x.rssi)+"}";
  }
  j+="]}";
  server_.send(200,"application/json; charset=utf-8",j);
}

// [接口] 网页选择扫描结果后按index连接。
void WebConfig::handleConnect(){
  if(!ble_){
    server_.send(500,"application/json","{\"message\":\"BLE未初始化\"}");
    return;
  }
  if(!server_.hasArg("index")){
    server_.send(400,"application/json","{\"message\":\"缺少index\"}");
    return;
  }
  String indexText=server_.arg("index");
  indexText.trim();
  if(indexText.length()==0){
    server_.send(400,"application/json; charset=utf-8","{\"message\":\"index无效\"}");
    return;
  }

  int index=indexText.toInt();
  if(index<0 || index>=ble_->getScanCount()){
    server_.send(400,"application/json; charset=utf-8","{\"message\":\"index超出扫描结果范围\"}");
    return;
  }

  bool ok=ble_->connectDeviceByIndex((uint8_t)index);
  String j="{\"ok\":" + String(ok?"true":"false")+
           ",\"message\":\""+String(ok?"连接成功":"连接失败")+"\""+
           ",\"mac\":\""+jsonEscape(g_bmsData.mac)+"\"}";
  server_.send(ok?200:500,"application/json; charset=utf-8",j);
}

// [接口] 保存MAC、24S/32S和Wh/km到Preferences。
void WebConfig::handleSave(){
  if(!ble_){
    server_.send(500,"application/json","{\"message\":\"BLE未初始化\"}");
    return;
  }

  String mac=server_.hasArg("mac")?server_.arg("mac"):"";
  mac.trim();

  // 没有手工填写时，直接保存当前已经连接的 JK 蓝牙地址。
  if(mac.length()==0 && g_bmsData.mac.length())
    mac=g_bmsData.mac;

  bool is32=server_.hasArg("proto") ? server_.arg("proto")=="32" : true;

  float whkm=server_.hasArg("consumption") ? server_.arg("consumption").toFloat() : 100.0f;
  if(whkm<1.0f) whkm=1.0f;
  if(whkm>1000.0f) whkm=1000.0f;

  ble_->setConfiguredAddress(mac);
  ble_->setProtocol32S(is32);

  g_bmsData.energyConsumptionWhKm=whkm;

  Preferences p;
  p.begin("jkcfg",false);
  p.putBool("32s",is32);
  p.putFloat("whkm",whkm);
  p.end();

  // 保存后立即重新计算一次，网页不用重启即可看到新结果。
  if(g_bmsData.totalVoltage>0.1f && whkm>1.0f){
    g_bmsData.remainingRangeKm=
      (g_bmsData.remainingCapacityAh*g_bmsData.totalVoltage)/whkm;
  }

  server_.send(200,"application/json; charset=utf-8",
               "{\"message\":\"参数已保存，下次开机自动使用\""
               ",\"mac\":\""+jsonEscape(mac)+"\"}");
}

// [接口] 返回屏幕显示配置。
/*
 * POST /api/display/save
 * ---------------------------------------------------------------
 * 接收网页的四行指标、颜色、字体和SOC条设置。
 * 处理顺序：
 *   1. 读取HTTP参数
 *   2. 限制枚举/字体范围
 *   3. RGB888转换为ST7789使用的RGB565
 *   4. 写入g_displayConfig
 *   5. Preferences持久化
 *
 * Display::update()下一次运行时检测配置变化，
 * 重新绘制受影响的局部区域，因此无需重启。
 */
void WebConfig::handleDisplaySave(){
  for(uint8_t i=0;i<4;i++){
    String mk="m"+String(i), ck="c"+String(i), fk="f"+String(i);
    if(server_.hasArg(mk.c_str())) g_displayConfig.row[i].metric=(uint8_t)server_.arg(mk.c_str()).toInt();
    if(server_.hasArg(fk.c_str())) g_displayConfig.row[i].font=(uint8_t)server_.arg(fk.c_str()).toInt();
    if(g_displayConfig.row[i].metric>DISPLAY_CYCLE_COUNT) g_displayConfig.row[i].metric=DISPLAY_VOLTAGE;
    if(g_displayConfig.row[i].font<2 || g_displayConfig.row[i].font>4) g_displayConfig.row[i].font=4;
    if(server_.hasArg(ck.c_str())){
      String s=server_.arg(ck.c_str()); s.trim();
      if(s.length()==7 && s[0]=='#'){
        long n=strtol(s.c_str()+1,nullptr,16);
        uint8_t r=(n>>16)&255, g=(n>>8)&255, b=n&255;
        g_displayConfig.row[i].color=((uint16_t)(r>>3)<<11)|((uint16_t)(g>>2)<<5)|(uint16_t)(b>>3);
      }
    }
  }
  auto parseColor=[this](const char* key,uint16_t fallback)->uint16_t{
    if(!server_.hasArg(key)) return fallback;
    String s=server_.arg(key); s.trim();
    if(s.length()!=7 || s[0]!='#') return fallback;
    long n=strtol(s.c_str()+1,nullptr,16);
    uint8_t r=(n>>16)&255,g=(n>>8)&255,b=n&255;
    return ((uint16_t)(r>>3)<<11)|((uint16_t)(g>>2)<<5)|(uint16_t)(b>>3);
  };
  g_displayConfig.socColor=parseColor("socColor",g_displayConfig.socColor);
  g_displayConfig.tempColor=parseColor("tempColor",g_displayConfig.tempColor);
  g_displayConfig.capacityColor=parseColor("capColor",g_displayConfig.capacityColor);
  g_displayConfig.socBar=server_.hasArg("bar") && server_.arg("bar")=="1";
  g_displayConfig.save();

  server_.send(200,"application/json; charset=utf-8",
               R"({"message":"屏幕显示设置已保存并立即生效"})");
}

// [接口] 保存保护板手动选择。
void WebConfig::handleProtectionSave(){
  if(!ble_){
    server_.send(500,"application/json; charset=utf-8",R"({"message":"BLE未初始化"})");
    return;
  }
  String name=server_.hasArg("protocol")?server_.arg("protocol"):"";
  name.trim();
  name.toUpperCase();
  if(name!="JK" && name!="ANT" && name!="JBD" && name!="DALY" && name!="TT" && name!="YANYANG"){
    server_.send(400,"application/json; charset=utf-8",R"({"message":"协议类型无效"})");
    return;
  }
  ble_->setPreferredProtocol(name);
  String j=R"({"message":"保护板协议已保存","protocol":")";
  j+=jsonEscape(ble_->getPreferredProtocol());
  j+="\"}";
  server_.send(200,"application/json; charset=utf-8",j);
}

// [接口] 清除保存的协议，但保留蓝牙 MAC。
void WebConfig::handleProtectionClear(){
  Preferences p;
  p.begin("jkcfg",false);
  p.remove("protocol");
  p.end();
  if(ble_) ble_->setPreferredProtocol("JK");
  p.begin("jkcfg",false);
  p.remove("protocol");
  p.end();
  server_.send(200,"application/json; charset=utf-8",R"({"message":"已清除保存协议"})");
}

// [接口] 网页重启 ESP32，不删除 Preferences。
void WebConfig::handleRestart(){
  server_.send(200,"application/json; charset=utf-8",R"({"message":"ESP32即将重启"})");
  delay(300);
  ESP.restart();
}

void WebConfig::handleNotFound(){
  server_.send(404,"text/plain; charset=utf-8","404");
}
