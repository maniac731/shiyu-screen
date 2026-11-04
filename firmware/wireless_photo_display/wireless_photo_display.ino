#include <DNSServer.h>
#include <FFat.h>
#include <WebServer.h>
#include <WiFi.h>

#include "gui_paint.h"
#include "rgb_lcd_port.h"

namespace {

constexpr char kApName[] = "ShiYu-Screen-A02";
constexpr char kApPassword[] = "shiyu2026";
constexpr char kImagePath[] = "/current.rgb";
constexpr char kTempImagePath[] = "/upload.tmp";
constexpr size_t kImageBytes = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2;

DNSServer dnsServer;
WebServer server(80);
File uploadFile;
uint8_t *screenImage = nullptr;
size_t uploadedBytes = 0;
bool uploadSucceeded = false;

const char kUploadPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <title>食愈屏幕</title>
  <style>
    :root { color-scheme: light; font-family: -apple-system,BlinkMacSystemFont,"PingFang SC",sans-serif; }
    body { margin:0; background:#f5f3ee; color:#1d2a24; }
    main { max-width:560px; margin:auto; padding:28px 18px 42px; }
    .card { background:white; border-radius:22px; padding:22px; box-shadow:0 12px 32px #263c3020; }
    h1 { margin:0 0 8px; font-size:28px; }
    p { color:#66736c; line-height:1.6; }
    canvas { width:100%; aspect-ratio:1024/600; background:#17201c; border-radius:14px; object-fit:contain; }
    input { display:none; }
    button,label { display:block; box-sizing:border-box; width:100%; border:0; border-radius:14px; padding:15px;
      text-align:center; font-size:17px; font-weight:650; cursor:pointer; }
    label { margin-top:16px; background:#e8efe9; color:#24543a; }
    button { margin-top:12px; background:#24613f; color:white; }
    button:disabled { opacity:.4; }
    #status { min-height:24px; margin:14px 0 0; text-align:center; color:#486054; }
    .hint { font-size:13px; }
  </style>
</head>
<body><main><section class="card">
  <h1>发送到食愈屏幕</h1>
  <p>选择照片后会自动裁满 1024 × 600 屏幕。</p>
  <canvas id="preview" width="1024" height="600"></canvas>
  <label for="photo">从相册选择图片</label>
  <input id="photo" type="file" accept="image/*">
  <button id="send" disabled>发送到屏幕</button>
  <div id="status"></div>
  <p class="hint">上传时请保持手机连接 ShiYu-Screen-A02。</p>
</section></main>
<script>
const W=1024,H=600,canvas=document.querySelector('#preview'),ctx=canvas.getContext('2d',{willReadFrequently:true});
const input=document.querySelector('#photo'),send=document.querySelector('#send'),status=document.querySelector('#status');
let ready=false;
ctx.fillStyle='#17201c';ctx.fillRect(0,0,W,H);
input.onchange=()=>{
  const file=input.files[0]; if(!file)return;
  const img=new Image();
  img.onload=()=>{
    const scale=Math.max(W/img.width,H/img.height);
    const dw=img.width*scale,dh=img.height*scale;
    ctx.drawImage(img,(W-dw)/2,(H-dh)/2,dw,dh);
    URL.revokeObjectURL(img.src); ready=true; send.disabled=false; status.textContent='预览完成，可以发送';
  };
  img.onerror=()=>status.textContent='无法读取这张图片，请换一张';
  img.src=URL.createObjectURL(file);
};
send.onclick=()=>{
  if(!ready)return;
  send.disabled=true; status.textContent='正在转换图片…';
  setTimeout(()=>{
    const rgba=ctx.getImageData(0,0,W,H).data;
    const rgb565=new Uint8Array(W*H*2);
    for(let i=0,j=0;i<rgba.length;i+=4,j+=2){
      const v=((rgba[i]>>3)<<11)|((rgba[i+1]>>2)<<5)|(rgba[i+2]>>3);
      rgb565[j]=v&255; rgb565[j+1]=v>>8;
    }
    const form=new FormData(); form.append('image',new Blob([rgb565]),'screen.rgb');
    const xhr=new XMLHttpRequest(); xhr.open('POST','/upload');
    xhr.upload.onprogress=e=>{if(e.lengthComputable)status.textContent=`正在发送 ${Math.round(e.loaded/e.total*100)}%`;};
    xhr.onload=()=>{status.textContent=xhr.status===200?'发送成功，看看屏幕吧':'发送失败：'+xhr.responseText;send.disabled=false;};
    xhr.onerror=()=>{status.textContent='连接中断，请确认手机仍连接屏幕 Wi-Fi';send.disabled=false;};
    xhr.send(form);
  },50);
};
</script></body></html>
)HTML";

void showSetupScreen() {
  Paint_NewImage(screenImage, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 0, WHITE);
  Paint_SetScale(65);
  Paint_Clear(WHITE);
  Paint_DrawString_EN(90, 120, "SHI YU PHOTO SCREEN", &Font24, 0x2468, WHITE);
  Paint_DrawString_EN(90, 220, "1. Connect Wi-Fi: ShiYu-Screen-A02", &Font24, BLACK, WHITE);
  Paint_DrawString_EN(90, 270, "2. Password: shiyu2026", &Font24, BLACK, WHITE);
  Paint_DrawString_EN(90, 320, "3. Open: http://192.168.4.1", &Font24, BLACK, WHITE);
  Paint_DrawString_EN(90, 400, "Choose a photo on your phone and send it.", &Font24, 0x4B4B, WHITE);
  wavesahre_rgb_lcd_display(screenImage);
}

bool displaySavedImage() {
  File image = FFat.open(kImagePath, FILE_READ);
  if (!image || image.size() != kImageBytes) {
    if (image) image.close();
    return false;
  }
  const size_t readBytes = image.read(screenImage, kImageBytes);
  image.close();
  if (readBytes != kImageBytes) return false;
  wavesahre_rgb_lcd_display(screenImage);
  return true;
}

void handleUploadData() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadedBytes = 0;
    uploadSucceeded = false;
    if (uploadFile) uploadFile.close();
    FFat.remove(kTempImagePath);
    uploadFile = FFat.open(kTempImagePath, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadedBytes + upload.currentSize <= kImageBytes) {
      const size_t written = uploadFile.write(upload.buf, upload.currentSize);
      uploadedBytes += written;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    if (uploadedBytes == kImageBytes) {
      FFat.remove(kImagePath);
      uploadSucceeded = FFat.rename(kTempImagePath, kImagePath) && displaySavedImage();
    } else {
      FFat.remove(kTempImagePath);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    FFat.remove(kTempImagePath);
  }
}

void startPhotoServer() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(kApName, kApPassword);
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", kUploadPage); });
  server.on("/upload", HTTP_POST,
            []() {
              if (uploadSucceeded) server.send(200, "text/plain; charset=utf-8", "OK");
              else server.send(400, "text/plain; charset=utf-8", "图片数据不完整，请重试");
            },
            handleUploadData);
  server.onNotFound([]() { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302); });
  server.begin();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  if (!psramFound()) {
    Serial.println("PSRAM is required.");
    return;
  }

  DEV_I2C_Init();
  IO_EXTENSION_Init();
  waveshare_esp32_s3_rgb_lcd_init();
  wavesahre_rgb_lcd_bl_on();

  screenImage = static_cast<uint8_t *>(heap_caps_malloc(kImageBytes, MALLOC_CAP_SPIRAM));
  if (!screenImage) {
    Serial.println("Failed to allocate screen buffer.");
    return;
  }

  const bool storageReady = FFat.begin(true);
  if (!storageReady || !displaySavedImage()) showSetupScreen();
  startPhotoServer();

  Serial.printf("Connect to %s and open http://%s\n", kApName, WiFi.softAPIP().toString().c_str());
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  delay(2);
}
