#include "photo_portal.h"

#include <FFat.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>

#include "rgb_lcd_port.h"

namespace {
const char kUploadPage[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><title>食愈屏幕传图</title>
<style>:root{font-family:-apple-system,BlinkMacSystemFont,"PingFang SC",sans-serif;color:#26372f;background:#f6f1e8}body{margin:0}main{max-width:620px;margin:auto;padding:24px 16px 40px}.card{background:#fffdfc;border-radius:22px;padding:20px;box-shadow:0 12px 32px #26372f18}h1{margin:0 0 8px;font-size:26px}p{color:#708078;line-height:1.6}canvas{display:block;width:100%;aspect-ratio:1024/600;background:#26372f;border-radius:14px}input{display:none}button,label{box-sizing:border-box;display:block;width:100%;border:0;border-radius:14px;padding:15px;margin-top:12px;text-align:center;font-size:17px;font-weight:650}.choose{background:#e5eee7;color:#26372f}.primary{background:#39734f;color:white}.finish{background:#26372f;color:white}.status{min-height:26px;margin:14px 0;text-align:center;color:#39734f}</style></head>
<body><main><section class="card"><h1>发送照片到食愈屏幕</h1><p>照片会在手机上裁成屏幕比例。发送期间屏幕会显示进度，完成后照片会保存在设备中。</p><canvas id="preview" width="1024" height="600"></canvas><label class="choose" for="photo">从相册选择照片</label><input id="photo" type="file" accept="image/*"><button class="primary" id="send" disabled>发送到屏幕</button><div class="status" id="status">等待选择照片</div><button class="finish" id="finish">完成并关闭传图</button></section></main>
<script>const W=1024,H=600,c=document.querySelector('#preview'),x=c.getContext('2d'),input=document.querySelector('#photo'),send=document.querySelector('#send'),status=document.querySelector('#status');let ready=false;x.fillStyle='#26372f';x.fillRect(0,0,W,H);input.onchange=()=>{const f=input.files[0];if(!f)return;const i=new Image();i.onload=()=>{const s=Math.max(W/i.width,H/i.height),w=i.width*s,h=i.height*s;x.drawImage(i,(W-w)/2,(H-h)/2,w,h);URL.revokeObjectURL(i.src);ready=true;send.disabled=false;status.textContent='预览完成'};i.src=URL.createObjectURL(f)};send.onclick=()=>{if(!ready)return;send.disabled=true;status.textContent='正在压缩…';c.toBlob(async b=>{try{let r=await fetch('/upload-start?size='+b.size,{method:'POST'});if(!r.ok)throw Error(await r.text());const f=new FormData();f.append('image',b,'screen.jpg');const q=new XMLHttpRequest();q.open('POST','/upload');q.upload.onprogress=e=>{if(e.lengthComputable)status.textContent=`正在发送 ${Math.round(e.loaded/e.total*100)}%`};q.onload=()=>{status.textContent=q.status===200?'照片已保存':'失败：'+q.responseText;send.disabled=false};q.onerror=()=>{status.textContent='连接中断';send.disabled=false};q.send(f)}catch(e){status.textContent='失败：'+e.message;send.disabled=false}},'image/jpeg',.82)};document.querySelector('#finish').onclick=async()=>{status.textContent='正在关闭传图…';await fetch('/finish',{method:'POST'});document.body.innerHTML='<main><section class="card"><h1>传图完成</h1><p>屏幕将恢复到设置页，手机可以切回原来的Wi-Fi。</p></section></main>'};</script></body></html>
)HTML";
}

bool PhotoPortal::start() {
  if (active_) return true;
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(kApName, kApPassword)) return false;
  dns_.start(53, "*", WiFi.softAPIP());
  if (!routesRegistered_) {
    registerRoutes();
    routesRegistered_ = true;
  }
  server_.begin();
  active_ = true;
  stage_ = PortalStage::Waiting;
  lastActivityMs_ = millis();
  return true;
}

void PhotoPortal::loop() {
  if (!active_) return;
  dns_.processNextRequest();
  server_.handleClient();
}

void PhotoPortal::stop() {
  if (!active_) return;
  if (uploadFile_) uploadFile_.close();
  server_.stop();
  dns_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  active_ = false;
  stage_ = PortalStage::Inactive;
  finishRequested_ = false;
}

bool PhotoPortal::exitRequested() const {
  return active_ && (finishRequested_ || millis() - lastActivityMs_ >= kIdleTimeoutMs);
}

bool PhotoPortal::consumePhotoReady() {
  const bool ready = photoReady_;
  photoReady_ = false;
  return ready;
}

uint8_t PhotoPortal::progress() const {
  if (!expectedBytes_) return 0;
  return min<size_t>(100, receivedBytes_ * 100 / expectedBytes_);
}

void PhotoPortal::touchActivity() { lastActivityMs_ = millis(); }

void PhotoPortal::registerRoutes() {
  server_.on("/", HTTP_GET, [this]() { touchActivity(); server_.send_P(200, "text/html; charset=utf-8", kUploadPage); });
  server_.on("/upload-start", HTTP_POST, [this]() {
    touchActivity();
    expectedBytes_ = server_.arg("size").toInt();
    receivedBytes_ = 0;
    if (!expectedBytes_ || expectedBytes_ > PhotoStorage::kMaximumUploadBytes) {
      stage_ = PortalStage::Error;
      return server_.send(400, "text/plain; charset=utf-8", "压缩后的图片过大");
    }
    server_.send(200, "text/plain", "OK");
  });
  server_.on("/upload", HTTP_POST,
    [this]() { touchActivity(); server_.send(stage_ == PortalStage::Saved ? 200 : 400, "text/plain; charset=utf-8", stage_ == PortalStage::Saved ? "OK" : "图片不完整或格式不正确"); },
    [this]() { handleUploadData(); });
  server_.on("/finish", HTTP_POST, [this]() { touchActivity(); server_.send(200, "text/plain", "OK"); finishRequested_ = true; });
  server_.onNotFound([this]() { touchActivity(); server_.sendHeader("Location", "http://192.168.4.1/", true); server_.send(302); });
}

void PhotoPortal::handleUploadData() {
  touchActivity();
  HTTPUpload &upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    receivedBytes_ = 0;
    photoReady_ = false;
    stage_ = PortalStage::Receiving;
    if (uploadFile_) uploadFile_.close();
    FFat.remove(PhotoStorage::kTemporaryPath);
    uploadFile_ = FFat.open(PhotoStorage::kTemporaryPath, FILE_WRITE);
    if (!uploadFile_) stage_ = PortalStage::Error;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile_ && receivedBytes_ + upload.currentSize <= PhotoStorage::kMaximumUploadBytes) {
      receivedBytes_ += uploadFile_.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile_) uploadFile_.close();
    uint16_t width = 0;
    uint16_t height = 0;
    const bool valid = expectedBytes_ && receivedBytes_ == expectedBytes_ &&
      TJpgDec.getFsJpgSize(&width, &height, PhotoStorage::kTemporaryPath, FFat) == JDR_OK &&
      width == EXAMPLE_LCD_H_RES && height == EXAMPLE_LCD_V_RES;
    if (valid && storage_.acceptUpload()) {
      stage_ = PortalStage::Saved;
      photoReady_ = true;
    } else {
      FFat.remove(PhotoStorage::kTemporaryPath);
      stage_ = PortalStage::Error;
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile_) uploadFile_.close();
    FFat.remove(PhotoStorage::kTemporaryPath);
    stage_ = PortalStage::Error;
  }
}
