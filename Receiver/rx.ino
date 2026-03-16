
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

// ────────────────────────────────────────────────────────────────────
//  CONFIGURATION
// ────────────────────────────────────────────────────────────────────
#define WIFI_SSID        "ESP"
#define WIFI_PASS        "abcd1234"
#define ADMIN_ID         "admin"
#define ADMIN_PASS       "admin123"
#define USER_ID          "user1"
#define USER_PASS        "pass123"
#define DEVICE_ID        "1A"

#define NTP_SERVER       "pool.ntp.org"
#define GMT_OFFSET_SEC   19800
#define DST_OFFSET_SEC   0

// Pin Map
#define LORA_SCK    12
#define LORA_MISO   11
#define LORA_MOSI   10
#define LORA_NSS    15
#define LORA_RST    14
#define LORA_DIO0   13
#define OLED_SDA    17
#define OLED_SCL    18
#define OLED_ADDR   0x3C
#define SCREEN_W    128
#define SCREEN_H    64

// LoRa
#define LORA_FREQ        433E6
#define LORA_BW          125E3
#define LORA_SF          7
#define LORA_TIMEOUT_MS  30000UL

// Timing
#define PREF_SAVE_INTERVAL_MS  300000UL
#define OLED_REFRESH_MS         10000UL
#define MONTH_CHECK_MS          60000UL
#define WIFI_WATCH_MS           15000UL

// ────────────────────────────────────────────────────────────────────
//  OBJECTS
// ────────────────────────────────────────────────────────────────────
SPIClass          loRaSPI(HSPI);
Adafruit_SSD1306  oled(SCREEN_W, SCREEN_H, &Wire, -1);
WebServer         server(80);
Preferences       prefs;

// ────────────────────────────────────────────────────────────────────
//  LIVE DATA  — all start at 0, never pre-loaded
// ────────────────────────────────────────────────────────────────────
struct EnergyData {
  float    voltage  = 0.0f;
  float    current  = 0.0f;
  float    power    = 0.0f;
  float    freq     = 0.0f;
  float    total    = 0.0f;
  float    monthly  = 0.0f;
  float    prevDay  = 0.0f;
  bool     relay    = true;
  int      rssi     = 0;
  uint32_t lastRx   = 0;
} ed;

// ────────────────────────────────────────────────────────────────────
//  FLAGS & STATE
// ────────────────────────────────────────────────────────────────────
bool     hasLiveData    = false;
bool     loraLost       = false;
bool     loRaActive     = false;
float    unitPrice      = 8.0f;
bool     billPaid       = false;
uint8_t  lastResetMonth = 255;

float dayHist[7] = {0, 0, 0, 0, 0, 0, 0};
float monHist[6] = {0, 0, 0, 0, 0, 0};

uint32_t lastPrefSave   = 0;
uint32_t lastOledRefresh= 0;
uint32_t lastMonthCheck = 0;
uint32_t lastWiFiCheck  = 0;

// ────────────────────────────────────────────────────────────────────
//  SESSION
// ────────────────────────────────────────────────────────────────────
char     sessionToken[17] = {0};
uint32_t sessionExp       = 0;
bool     isAdminSession   = false;

// ────────────────────────────────────────────────────────────────────
//  HTML BUFFER + HELPERS
// ────────────────────────────────────────────────────────────────────
static String g_html;

inline void H(const char* s)                { g_html += s; }
inline void H(const __FlashStringHelper* s) { g_html += s; }
inline void H(int v)                         { g_html += v; }
inline void H(float v, unsigned int d = 2)  { g_html += String(v, d); }
inline void H(bool v)                        { g_html += v ? "true" : "false"; }
inline void H(const String& s)              { g_html += s; }

// ────────────────────────────────────────────────────────────────────
//  PROGMEM: SHARED CSS
// ────────────────────────────────────────────────────────────────────
const char SHARED_CSS[] PROGMEM = R"CSS(
<style>
:root{
  --bg:#f0f4f8;--card:#fff;--sidebar:#1a2535;--sidebar-link:#8fa3b8;
  --text:#1e293b;--sub:#64748b;--border:#e2e8f0;--accent:#3b82f6;
  --green:#10b981;--red:#ef4444;--yellow:#f59e0b;
  --shadow:0 1px 3px rgba(0,0,0,.06),0 4px 16px rgba(0,0,0,.06);
  --radius:14px;--sidebar-w:230px;--topbar-h:58px;
}
body.dark{
  --bg:#0d1117;--card:#161b22;--sidebar:#0a0f16;--sidebar-link:#8b949e;
  --text:#e6edf3;--sub:#8b949e;--border:#30363d;
  --shadow:0 1px 3px rgba(0,0,0,.3),0 4px 16px rgba(0,0,0,.3);
}
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{font-family:'Segoe UI',system-ui,Arial,sans-serif;background:var(--bg);
  color:var(--text);display:flex;min-height:100vh;transition:background .25s,color .25s}
#sidebar{width:var(--sidebar-w);background:var(--sidebar);display:flex;
  flex-direction:column;min-height:100vh;position:fixed;left:0;top:0;
  z-index:300;transition:width .25s;overflow:hidden}
#sidebar.collapsed{width:58px}
#sidebar.collapsed .nav-label,#sidebar.collapsed .logo-text,
#sidebar.collapsed .sidebar-ver,#sidebar.collapsed .nav-section{display:none}
.logo{padding:18px 16px;display:flex;align-items:center;gap:10px;
  border-bottom:1px solid rgba(255,255,255,.06);white-space:nowrap;min-height:var(--topbar-h)}
.logo-icon{font-size:22px;flex-shrink:0}
.logo-text{font-size:15px;font-weight:700;color:#fff}
.sidebar-ver{font-size:10px;color:#4a5568;margin-top:2px}
.nav-section{padding:12px 10px 4px;font-size:10px;font-weight:600;
  color:#4a5568;text-transform:uppercase;letter-spacing:.8px;white-space:nowrap}
.nav-link{display:flex;align-items:center;gap:10px;padding:11px 16px;
  color:var(--sidebar-link);text-decoration:none;font-size:13.5px;
  border-radius:8px;margin:1px 8px;transition:all .2s;white-space:nowrap}
.nav-link:hover{background:rgba(59,130,246,.15);color:#fff}
.nav-link.active{background:rgba(59,130,246,.25);color:#3b82f6;font-weight:600}
.nav-icon{font-size:16px;flex-shrink:0;width:20px;text-align:center}
.nav-divider{height:1px;background:rgba(255,255,255,.06);margin:8px 16px}
.nav-logout{display:flex;align-items:center;gap:10px;padding:11px 16px;
  color:#f87171;cursor:pointer;font-size:13.5px;border-radius:8px;
  margin:1px 8px;transition:all .2s;white-space:nowrap;margin-top:auto}
.nav-logout:hover{background:rgba(239,68,68,.15)}
#topbar{position:fixed;top:0;left:var(--sidebar-w);right:0;
  height:var(--topbar-h);background:var(--card);
  border-bottom:1px solid var(--border);display:flex;align-items:center;
  padding:0 24px;gap:12px;z-index:200;transition:left .25s}
#topbar.wide{left:58px}
.topbar-title{flex:1;font-size:16px;font-weight:600;color:var(--text)}
.topbar-status{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--sub)}
.btn-icon{background:none;border:1.5px solid var(--border);border-radius:9px;
  padding:7px 13px;cursor:pointer;font-size:13px;color:var(--text);
  transition:all .2s;display:inline-flex;align-items:center;gap:5px}
.btn-icon:hover{background:var(--accent);color:#fff;border-color:var(--accent)}
#main{margin-left:var(--sidebar-w);margin-top:var(--topbar-h);
  flex:1;padding:28px;transition:margin .25s;min-width:0}
#main.wide{margin-left:58px}
.page-header{margin-bottom:24px}
.page-header h1{font-size:21px;font-weight:700}
.page-header p{font-size:13px;color:var(--sub);margin-top:4px}
.skeleton{background:linear-gradient(90deg,var(--border) 25%,var(--bg) 50%,var(--border) 75%);
  background-size:200% 100%;animation:shimmer 1.4s infinite;border-radius:6px}
@keyframes shimmer{0%{background-position:200% 0}100%{background-position:-200% 0}}
.skel-val{height:28px;width:70%;margin:4px 0}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(185px,1fr));
  gap:16px;margin-bottom:24px}
.card{background:var(--card);padding:20px 18px;border-radius:var(--radius);
  box-shadow:var(--shadow);border:1px solid var(--border);
  transition:transform .2s,box-shadow .2s;position:relative;overflow:hidden}
.card:hover{transform:translateY(-2px);box-shadow:0 8px 24px rgba(0,0,0,.1)}
.card-lbl{font-size:11px;font-weight:600;color:var(--sub);
  text-transform:uppercase;letter-spacing:.6px;margin-bottom:8px}
.card-val{font-size:22px;font-weight:700;color:var(--text);line-height:1.2}
.card-val.accent{color:var(--accent)}
.card-val.green{color:var(--green)}
.card-val.red{color:var(--red)}
.card-val.yellow{color:var(--yellow)}
.card-sub{font-size:11px;color:var(--sub);margin-top:5px}
.card-icon{position:absolute;top:16px;right:16px;font-size:22px;opacity:.12}
.sec-hdr{font-size:11px;font-weight:700;color:var(--sub);text-transform:uppercase;
  letter-spacing:.7px;margin:24px 0 12px;padding-bottom:8px;
  border-bottom:1px solid var(--border);display:flex;align-items:center;gap:8px}
.badge{display:inline-flex;align-items:center;gap:5px;padding:4px 10px;
  border-radius:20px;font-size:11px;font-weight:600}
.badge-green{background:#d1fae5;color:#065f46}
.badge-red{background:#fee2e2;color:#991b1b}
.badge-yellow{background:#fef3c7;color:#92400e}
.badge-blue{background:#dbeafe;color:#1e40af}
.badge-gray{background:var(--border);color:var(--sub)}
body.dark .badge-green{background:#064e3b;color:#6ee7b7}
body.dark .badge-red{background:#450a0a;color:#fca5a5}
body.dark .badge-yellow{background:#451a03;color:#fcd34d}
body.dark .badge-blue{background:#1e3a5f;color:#93c5fd}
.alert{display:flex;align-items:center;gap:10px;padding:12px 16px;
  border-radius:10px;font-size:13px;margin-bottom:18px;border:1px solid;
  animation:fadeIn .3s ease}
.alert-red{background:#fff1f2;border-color:#fecdd3;color:#9f1239}
.alert-yellow{background:#fffbeb;border-color:#fde68a;color:#92400e}
.alert-blue{background:#eff6ff;border-color:#bfdbfe;color:#1e40af}
body.dark .alert-red{background:#450a0a44;border-color:#991b1b;color:#fca5a5}
body.dark .alert-yellow{background:#451a0344;border-color:#92400e;color:#fcd34d}
body.dark .alert-blue{background:#1e3a5f44;border-color:#1e40af;color:#93c5fd}
.tbl-wrap{background:var(--card);border-radius:var(--radius);overflow:hidden;
  box-shadow:var(--shadow);border:1px solid var(--border);margin-bottom:20px}
table{width:100%;border-collapse:collapse}
thead th{background:var(--bg);padding:11px 16px;text-align:left;
  font-size:11px;font-weight:700;color:var(--sub);text-transform:uppercase;
  letter-spacing:.6px;border-bottom:1px solid var(--border)}
tbody td{padding:13px 16px;font-size:13.5px;border-bottom:1px solid var(--border)}
tbody tr:last-child td{border-bottom:none}
tbody tr:hover td{background:rgba(59,130,246,.03)}
td a{color:var(--accent);text-decoration:none;font-weight:600}
td a:hover{text-decoration:underline}
.chart-wrap{background:var(--card);padding:20px 20px 16px;
  border-radius:var(--radius);box-shadow:var(--shadow);
  border:1px solid var(--border);margin-bottom:20px}
.chart-title{font-size:12px;font-weight:600;color:var(--sub);
  text-transform:uppercase;letter-spacing:.5px;margin-bottom:14px}
.form-group{margin-bottom:14px}
.form-group label{display:block;font-size:12px;font-weight:600;color:var(--sub);
  margin-bottom:5px;text-transform:uppercase;letter-spacing:.4px}
input[type=text],input[type=password],input[type=number],select{
  width:100%;padding:10px 13px;border:1.5px solid var(--border);
  border-radius:9px;font-size:14px;background:var(--bg);
  color:var(--text);transition:border .2s,box-shadow .2s}
input:focus,select:focus{outline:none;border-color:var(--accent);
  box-shadow:0 0 0 3px rgba(59,130,246,.15)}
.btn{display:inline-flex;align-items:center;gap:6px;padding:10px 18px;
  border:none;border-radius:9px;cursor:pointer;font-size:13px;
  font-weight:600;transition:all .2s}
.btn:hover{transform:translateY(-1px)}
.btn-primary{background:var(--accent);color:#fff}
.btn-primary:hover{background:#2563eb}
.btn-success{background:var(--green);color:#fff}
.btn-success:hover{background:#059669}
.btn-danger{background:var(--red);color:#fff}
.btn-danger:hover{background:#dc2626}
.btn-block{display:flex;width:100%;justify-content:center;margin-top:12px}
.btn-sm{padding:7px 13px;font-size:12px}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;flex-shrink:0}
.dot-green{background:var(--green);box-shadow:0 0 6px var(--green)}
.dot-red{background:var(--red);box-shadow:0 0 6px var(--red)}
.dot-yellow{background:var(--yellow);box-shadow:0 0 6px var(--yellow)}
.dot-pulse{animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.info-row{display:flex;justify-content:space-between;align-items:center;
  padding:10px 0;border-bottom:1px solid var(--border);font-size:13.5px}
.info-row:last-child{border-bottom:none}
.info-row .lbl{color:var(--sub)}
.info-row .val{font-weight:600}
@keyframes fadeIn{from{opacity:0;transform:translateY(-6px)}to{opacity:1;transform:none}}
.fade-in{animation:fadeIn .3s ease}
@media(max-width:800px){
  #sidebar{width:58px}
  #sidebar .nav-label,#sidebar .logo-text,#sidebar .sidebar-ver,
  #sidebar .nav-section{display:none}
  #topbar{left:58px}
  #main{margin-left:58px;padding:16px}
  .cards{grid-template-columns:1fr 1fr}
}
@media(max-width:480px){
  .cards{grid-template-columns:1fr}
  #topbar{padding:0 14px;gap:8px}
}
</style>
)CSS";

// ────────────────────────────────────────────────────────────────────
//  PROGMEM: SHARED JS
// ────────────────────────────────────────────────────────────────────
const char SHARED_JS[] PROGMEM = R"JS(
<script>
(function(){
  const t=localStorage.getItem('em-theme')||'light';
  if(t==='dark')document.body.classList.add('dark');
})();
function toggleTheme(){
  const d=document.body.classList.toggle('dark');
  localStorage.setItem('em-theme',d?'dark':'light');
  const b=document.getElementById('themeBtn');
  if(b)b.textContent=d?'☀ Light':'🌙 Dark';
}
function toggleSidebar(){
  const s=document.getElementById('sidebar');
  const t=document.getElementById('topbar');
  const m=document.getElementById('main');
  const c=s.classList.toggle('collapsed');
  t.classList.toggle('wide',c);
  m.classList.toggle('wide',c);
  localStorage.setItem('em-sb',c?'1':'0');
}
(function(){
  if(localStorage.getItem('em-sb')!=='1')return;
  const s=document.getElementById('sidebar');
  const t=document.getElementById('topbar');
  const m=document.getElementById('main');
  if(s){s.classList.add('collapsed');
    if(t)t.classList.add('wide');
    if(m)m.classList.add('wide');}
})();
window.addEventListener('DOMContentLoaded',function(){
  const b=document.getElementById('themeBtn');
  if(b)b.textContent=document.body.classList.contains('dark')?'☀ Light':'🌙 Dark';
});
function setText(id,v){const e=document.getElementById(id);if(e)e.textContent=v;}
function setHTML(id,v){const e=document.getElementById(id);if(e)e.innerHTML=v;}
</script>
)JS";

// ────────────────────────────────────────────────────────────────────
//  PROGMEM: LOGIN PAGE
// ────────────────────────────────────────────────────────────────────
const char LOGIN_HTML[] PROGMEM = R"RAW(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Energy Meter Login</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--accent:#3b82f6;--text:#e6edf3;
  --sub:#8b949e;--border:#30363d;--red:#f87171}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',system-ui,Arial,sans-serif;
  background:radial-gradient(ellipse at 60% 0%,#1e3a5f 0%,var(--bg) 65%);
  min-height:100vh;display:flex;align-items:center;justify-content:center;color:var(--text)}
.box{background:var(--card);padding:40px 34px;border-radius:18px;width:350px;
  box-shadow:0 8px 40px rgba(0,0,0,.6);border:1px solid var(--border);text-align:center}
.logo-wrap{width:64px;height:64px;background:linear-gradient(135deg,#1e3a8a,#3b82f6);
  border-radius:16px;display:flex;align-items:center;justify-content:center;
  font-size:30px;margin:0 auto 16px;box-shadow:0 0 24px rgba(59,130,246,.4)}
h2{font-size:20px;margin-bottom:4px;font-weight:700}
.sub{color:var(--sub);font-size:13px;margin-bottom:30px}
.fg{text-align:left;margin-bottom:12px}
.fg label{display:block;font-size:11px;font-weight:600;color:var(--sub);
  margin-bottom:5px;text-transform:uppercase;letter-spacing:.5px}
input{width:100%;padding:11px 14px;background:#0d1117;border:1.5px solid var(--border);
  color:var(--text);border-radius:9px;font-size:14px;transition:.2s}
input:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px rgba(59,130,246,.2)}
.btn-l{width:100%;padding:13px;margin-top:6px;background:var(--accent);
  color:#fff;border:none;border-radius:9px;font-size:14px;font-weight:700;
  cursor:pointer;transition:.2s}
.btn-l:hover{background:#2563eb;transform:translateY(-1px)}
.err{color:var(--red);font-size:13px;margin-top:14px;padding:10px 14px;
  background:rgba(239,68,68,.12);border-radius:7px;border:1px solid rgba(239,68,68,.3)}
.brand{font-size:11px;color:var(--sub);margin-top:22px}
</style></head><body>
<div class="box">
  <div class="logo-wrap">&#9889;</div>
  <h2>Energy Meter</h2>
  <div class="sub">Postpaid Management System</div>
  <form method="POST" action="/auth" autocomplete="on">
    <div class="fg"><label>User ID</label>
      <input name="u" placeholder="Enter your ID" autocomplete="username" required></div>
    <div class="fg"><label>Password</label>
      <input type="password" name="p" placeholder="Enter password"
             autocomplete="current-password" required></div>
    <button class="btn-l" type="submit">Sign In</button>
  </form>
  __ERR__
  <div class="brand">ESP32-S3 &middot; LoRa 433 MHz &middot; v3.0</div>
</div>
</body></html>
)RAW";

// ────────────────────────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ────────────────────────────────────────────────────────────────────
void updateOLED();
bool checkAuth();
void redirectLogin();
void sendLoRa(const char* type, float val);
void checkMonthlyReset();

// ────────────────────────────────────────────────────────────────────
//  PAGE HEAD — sidebar + topbar
// ─────────────────────────────────────────���──────────────────────────
void pageHead(const char* title, bool adminView, const char* activeLink) {
  g_html.reserve(30000);
  g_html = "";
  H("<!DOCTYPE html><html lang='en'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>");
  H(title);
  H(" - Energy Meter</title>");
  H(FPSTR(SHARED_CSS));
  H("<script src='https://cdn.jsdelivr.net/npm/chart.js@4/dist/chart.umd.min.js'"
    " defer></script>");
  H("</head><body>");

  // Sidebar
  H("<nav id='sidebar'>");
  H("<div class='logo'>"
    "<span class='logo-icon'>&#9889;</span>"
    "<div><div class='logo-text'>Energy Meter</div>"
    "<div class='sidebar-ver'>v3.0 &middot; " DEVICE_ID "</div></div>"
    "</div>");

  if (adminView) {
    H("<div class='nav-section'>Admin</div>");
    H("<a class='nav-link");
    if (strcmp(activeLink, "/admin") == 0) H(" active");
    H("' href='/admin'><span class='nav-icon'>&#128202;</span>"
      "<span class='nav-label'> Dashboard</span></a>");

    H("<a class='nav-link");
    if (strcmp(activeLink, "/admin/unitprice") == 0) H(" active");
    H("' href='/admin/unitprice'><span class='nav-icon'>&#128178;</span>"
      "<span class='nav-label'> Unit Price</span></a>");

    H("<div class='nav-divider'></div>");
    H("<div class='nav-section'>System</div>");
    H("<a class='nav-link");
    if (strcmp(activeLink, "/settings") == 0) H(" active");
    H("' href='/settings'><span class='nav-icon'>&#9881;</span>"
      "<span class='nav-label'> Settings</span></a>");
  } else {
    H("<div class='nav-section'>My Meter</div>");
    H("<a class='nav-link");
    if (strcmp(activeLink, "/") == 0) H(" active");
    H("' href='/'><span class='nav-icon'>&#127968;</span>"
      "<span class='nav-label'> Home</span></a>");

    H("<a class='nav-link");
    if (strcmp(activeLink, "/usage") == 0) H(" active");
    H("' href='/usage'><span class='nav-icon'>&#128200;</span>"
      "<span class='nav-label'> Live Usage</span></a>");

    H("<a class='nav-link");
    if (strcmp(activeLink, "/bill") == 0) H(" active");
    H("' href='/bill'><span class='nav-icon'>&#129534;</span>"
      "<span class='nav-label'> My Bill</span></a>");

    H("<div class='nav-divider'></div>");
    H("<div class='nav-section'>Account</div>");
    H("<a class='nav-link");
    if (strcmp(activeLink, "/settings") == 0) H(" active");
    H("' href='/settings'><span class='nav-icon'>&#9881;</span>"
      "<span class='nav-label'> Settings</span></a>");
  }

  H("<div class='nav-divider'></div>");
  H("<div class='nav-logout' onclick=\"location='/out'\">"
    "<span class='nav-icon'>&#9211;</span>"
    "<span class='nav-label'> Logout</span></div>");
  H("</nav>");

  // Topbar
  H("<header id='topbar'>"
    "<button class='btn-icon' onclick='toggleSidebar()'>&#9776;</button>"
    "<span class='topbar-title'>");
  H(title);
  H("</span>");
  H("<span class='topbar-status' id='topbarStatus'>");
  if (!hasLiveData) {
    H("<span class='dot dot-yellow dot-pulse'></span> Waiting");
  } else if (loraLost) {
    H("<span class='dot dot-red'></span> Signal lost");
  } else {
    H("<span class='dot dot-green dot-pulse'></span> Live");
  }
  H("</span>");
  H("<button id='themeBtn' class='btn-icon' onclick='toggleTheme()'>&#127769; Dark</button>"
    "<button class='btn-icon' onclick=\"location='/out'\">&#9211;</button>"
    "</header>");

  H("<main id='main'>");
}

// ────────────────────────────────────────────────────────────────────
//  PAGE FOOT
// ────────────────────────────────────────────────────────────────────
void pageFoot() {
  H("</main>");
  H(FPSTR(SHARED_JS));
  H("</body></html>");
}

// ──────────────��─────────────────────────────────────────────────────
//  BADGE HELPERS
// ────────────────────────────────────────────────────────────────────
String valOrDash(float v, unsigned int dec = 2) {
  if (!hasLiveData) return "--";
  return String(v, dec);
}

const char* relayBadge() {
  if (ed.relay)
    return "<span class='badge badge-green'>"
           "<span class='dot dot-green'></span> ON</span>";
  return "<span class='badge badge-red'>"
         "<span class='dot dot-red'></span> OFF</span>";
}

const char* billBadge() {
  if (!hasLiveData)
    return "<span class='badge badge-gray'>Pending</span>";
  if (billPaid)
    return "<span class='badge badge-green'>&#10003; Paid</span>";
  return "<span class='badge badge-red'>&#10007; Unpaid</span>";
}

// ────────────────────────────────────────────────────────────────────
//  SHARED: ALERT BANNERS
// ────────────────────────────────────────────────────────────────────
void emitAlerts() {
  if (!hasLiveData) {
    H("<div class='alert alert-blue fade-in'>"
      "<span style='font-size:20px'>&#128225;</span>"
      "<div><strong>Waiting for meter data</strong>"
      "<div style='font-size:12px;margin-top:2px'>"
      "No LoRa packet received yet. Values appear after first transmission."
      "</div></div></div>");
    return;
  }
  if (loraLost) {
    H("<div class='alert alert-red fade-in'>"
      "<span style='font-size:20px'>&#9888;</span>"
      "<div><strong>LoRa signal lost</strong> &mdash; last packet ");
    H((int)((millis() - ed.lastRx) / 1000));
    H(" s ago. Showing frozen values.</div></div>");
  }
  if (!billPaid) {
    float bill = ed.monthly * unitPrice;
    if (bill > 0.0f) {
      H("<div class='alert alert-yellow fade-in'>"
        "<span style='font-size:18px'>&#129534;</span>"
        "<div><strong>Bill unpaid</strong> &mdash; &#8377;");
      H(bill, 2);
      H(" due. Contact your energy provider.</div></div>");
    }
  }
}

// ────────────────────────────────────────────────────────────────────
//  SHARED: POLL SCRIPT  (self-scheduling fetch, no page reload)
// ────────────────────────────────────────────────────────────────────
void emitPollScript(uint16_t ms, bool withRelay = false) {
  H("<script>(async function poll(){");
  H("try{");
  H("const r=await fetch('/api/data',{credentials:'same-origin'});");
  H("if(r.status===401){location='/login';return;}");
  H("const d=await r.json();");

  // Topbar status
  H("const ts=document.getElementById('topbarStatus');");
  H("if(ts){");
  H("if(!d.hasLiveData)"
    "ts.innerHTML=\"<span class='dot dot-yellow dot-pulse'></span> Waiting\";");
  H("else if(d.loraLost)"
    "ts.innerHTML=\"<span class='dot dot-red'></span> Signal lost\";");
  H("else "
    "ts.innerHTML=\"<span class='dot dot-green dot-pulse'></span> Live\";");
  H("}");

  H("if(d.hasLiveData){");
  H("setText('lv-v',   d.voltage.toFixed(1)+' V');");
  H("setText('lv-a',   d.current.toFixed(2)+' A');");
  H("setText('lv-w',   d.power.toFixed(1)+' W');");
  H("setText('lv-f',   d.freq.toFixed(1)+' Hz');");
  H("setText('lv-pd',  d.prevDay.toFixed(2)+' kWh');");
  H("setText('lv-mo',  d.monthly.toFixed(2)+' kWh');");
  H("setText('lv-tot', d.total.toFixed(3)+' kWh');");
  H("setText('lv-bill','\\u20B9'+d.bill.toFixed(2));");
  H("setText('lv-rssi',d.rssi+' dBm');");
  H("setHTML('lv-billstat',d.paid"
    "?\"<span class='badge badge-green'>&#10003; Paid</span>\""
    ":\"<span class='badge badge-red'>&#10007; Unpaid</span>\");");

  if (withRelay) {
    H("setHTML('lv-relay',d.relay"
      "?\"<span class='badge badge-green'>"
      "<span class='dot dot-green'></span> ON</span>\""
      ":\"<span class='badge badge-red'>"
      "<span class='dot dot-red'></span> OFF</span>\");");
    H("const rb=document.getElementById('relBtn');");
    H("if(rb){rb.innerHTML=d.relay"
      "?\"<button class='btn btn-danger btn-sm' onclick='setRelay(0)'>"
      "Cut Power</button>\""
      ":\"<button class='btn btn-success btn-sm' onclick='setRelay(1)'>"
      "Restore Power</button>\";}");
  }
  H("}");  // end if hasLiveData
  H("}catch(e){}");
  H("setTimeout(poll,"); H((int)ms); H(");");
  H("})();</script>");
}

// ────────────────────────────────────────────────────────────────────
//  CHART EMITTERS
// ────────────────────────────────────────────────────────────────────
void emitWeeklyChart(const char* cid) {
  H("<script>window.addEventListener('load',function(){");
  H("const ctx=document.getElementById('"); H(cid); H("');if(!ctx)return;");
  H("new Chart(ctx.getContext('2d'),{type:'bar',data:{");
  H("labels:['Mon','Tue','Wed','Thu','Fri','Sat','Sun'],");
  H("datasets:[{label:'kWh',data:[");
  for (int i = 0; i < 7; i++) { H(dayHist[i], 2); if (i < 6) H(","); }
  H("],backgroundColor:'rgba(59,130,246,0.75)',borderRadius:6}]},");
  H("options:{responsive:true,plugins:{legend:{display:false}},");
  H("scales:{x:{grid:{display:false}},y:{beginAtZero:true}}}});});</script>");
}

void emitMonthlyChart(const char* cid) {
  H("<script>window.addEventListener('load',function(){");
  H("const ctx=document.getElementById('"); H(cid); H("');if(!ctx)return;");
  H("new Chart(ctx.getContext('2d'),{type:'line',data:{");
  H("labels:['6m ago','5m ago','4m ago','3m ago','2m ago','This month'],");
  H("datasets:[{label:'kWh',data:[");
  for (int i = 0; i < 6; i++) { H(monHist[i], 2); if (i < 5) H(","); }
  H("],borderColor:'#3b82f6',backgroundColor:'rgba(59,130,246,0.08)',");
  H("tension:0.4,fill:true,pointBackgroundColor:'#3b82f6',pointRadius:5}]},");
  H("options:{responsive:true,plugins:{legend:{display:false}},");
  H("scales:{x:{grid:{display:false}},y:{beginAtZero:true}}}});});</script>");
}

// ────────────────────────────────────────────────────────────────────
//  PAGE: ADMIN TABLE  /admin
// ────────────────────────────────────────────────────────────────────
void buildAdminTable() {
  pageHead("Admin Dashboard", true, "/admin");
  H("<div class='page-header'><h1>Admin Dashboard</h1>"
    "<p>Monitor and manage connected meter devices.</p></div>");
  emitAlerts();

  float bill = hasLiveData ? (ed.monthly * unitPrice) : 0.0f;

  H("<div class='cards'>");
  H("<div class='card'><div class='card-icon'>&#127981;</div>"
    "<div class='card-lbl'>Total Devices</div>"
    "<div class='card-val accent'>1</div>"
    "<div class='card-sub'>Active on network</div></div>");

  H("<div class='card'><div class='card-icon'>&#9889;</div>"
    "<div class='card-lbl'>Monthly Consumption</div>"
    "<div class='card-val'>");
  H(hasLiveData ? String(ed.monthly, 2) + " kWh" : "--");
  H("</div><div class='card-sub'>Current billing period</div></div>");

  H("<div class='card'><div class='card-icon'>&#128176;</div>"
    "<div class='card-lbl'>Monthly Bill</div>"
    "<div class='card-val'>");
  H(hasLiveData ? "&#8377;" + String(bill, 2) : "--");
  H("</div><div class='card-sub'>@ &#8377;");
  H(unitPrice, 2);
  H("/kWh</div></div>");

  H("<div class='card'><div class='card-icon'>&#129534;</div>"
    "<div class='card-lbl'>Bill Status</div>"
    "<div class='card-val' style='font-size:16px;margin-top:4px'>");
  H(billBadge());
  H("</div></div>");
  H("</div>");

  H("<div class='sec-hdr'>Registered Devices</div>");
  H("<div class='tbl-wrap'><table>"
    "<thead><tr><th>Unique ID</th><th>Total kWh</th>"
    "<th>Monthly Bill</th><th>Status</th><th>Signal</th></tr></thead><tbody>");
  H("<tr>");
  H("<td><a href='/admin?id=" DEVICE_ID "'>" DEVICE_ID "</a></td>");
  H("<td>"); H(hasLiveData ? String(ed.total, 3) + " kWh" : "--"); H("</td>");
  H("<td>"); H(hasLiveData ? "&#8377;" + String(bill, 2) : "--"); H("</td>");
  H("<td>"); H(billBadge()); H("</td>");
  H("<td>");
  if (!hasLiveData) {
    H("<span class='badge badge-yellow'>"
      "<span class='dot dot-yellow'></span> Waiting</span>");
  } else if (loraLost) {
    H("<span class='badge badge-red'>"
      "<span class='dot dot-red'></span> Lost</span>");
  } else {
    H("<span class='badge badge-green'>"
      "<span class='dot dot-green dot-pulse'></span> Online</span>");
  }
  H("</td></tr></tbody></table></div>");

  H("<div class='chart-wrap'>"
    "<div class='chart-title'>Monthly Consumption Trend (kWh)</div>"
    "<canvas id='mc' height='75'></canvas></div>");
  emitMonthlyChart("mc");
  emitPollScript(5000);
  pageFoot();
}

// ────────────────────────────────────────────────────────────────────
//  PAGE: ADMIN DETAIL  /admin?id=1A
// ─────────���──────────────────────────────────────────────────────────
void buildAdminDetail() {
  pageHead("Device " DEVICE_ID, true, "/admin");
  H("<div class='page-header'>"
    "<p style='font-size:12px;color:var(--sub);margin-bottom:6px'>"
    "<a href='/admin' style='color:var(--accent)'>Admin</a> &rsaquo; "
    "Device " DEVICE_ID "</p>"
    "<h1>Device " DEVICE_ID " &mdash; Detail</h1>"
    "<p>Real-time readings, billing and relay control.</p></div>");
  emitAlerts();

  // Live readings
  H("<div class='sec-hdr'>Live Readings</div><div class='cards'>");

  struct { const char* icon; const char* lbl; const char* id; String val; const char* sub; } cards[] = {
    {"&#128268;","Voltage",   "lv-v",  valOrDash(ed.voltage, 1) + " V",   "Nominal 230 V"},
    {"&#12316;", "Current",   "lv-a",  valOrDash(ed.current, 2) + " A",   "Load current" },
    {"&#128161;","Power",     "lv-w",  valOrDash(ed.power,   1) + " W",   "Active power" },
    {"&#128260;","Frequency", "lv-f",  valOrDash(ed.freq,    1) + " Hz",  "Grid frequency"},
  };
  for (auto& c : cards) {
    H("<div class='card'><div class='card-icon'>"); H(c.icon); H("</div>");
    H("<div class='card-lbl'>"); H(c.lbl); H("</div>");
    if (!hasLiveData) {
      H("<div class='skeleton skel-val'></div>");
    } else {
      H("<div class='card-val' id='"); H(c.id); H("'>"); H(c.val); H("</div>");
    }
    H("<div class='card-sub'>"); H(c.sub); H("</div></div>");
  }
  H("</div>");

  // Consumption
  H("<div class='sec-hdr'>Consumption</div><div class='cards'>");
  struct { const char* icon; const char* lbl; const char* id; String val; const char* sub; } cc[] = {
    {"&#128197;","Previous Day",   "lv-pd",  valOrDash(ed.prevDay, 2) + " kWh", "Yesterday"},
    {"&#128198;","This Month",     "lv-mo",  valOrDash(ed.monthly, 2) + " kWh", "Billing period"},
    {"&#128193;","Lifetime Total", "lv-tot", valOrDash(ed.total,   3) + " kWh", "All time"},
  };
  for (auto& c : cc) {
    H("<div class='card'><div class='card-icon'>"); H(c.icon); H("</div>");
    H("<div class='card-lbl'>"); H(c.lbl); H("</div>");
    if (!hasLiveData) {
      H("<div class='skeleton skel-val'></div>");
    } else {
      H("<div class='card-val' id='"); H(c.id); H("'>"); H(c.val); H("</div>");
    }
    H("<div class='card-sub'>"); H(c.sub); H("</div></div>");
  }
  H("</div>");

  // Billing
  H("<div class='sec-hdr'>Billing</div><div class='cards'>");

  H("<div class='card'><div class='card-icon'>&#128178;</div>"
    "<div class='card-lbl'>Unit Price</div>"
    "<div class='card-val accent'>&#8377;");
  H(unitPrice, 2);
  H("/kWh</div><div class='card-sub'>Admin configurable</div></div>");

  H("<div class='card'><div class='card-icon'>&#129534;</div>"
    "<div class='card-lbl'>Monthly Bill</div>"
    "<div class='card-val' id='lv-bill'>");
  H(hasLiveData ? "&#8377;" + String(ed.monthly * unitPrice, 2) : "--");
  H("</div><div class='card-sub'>Monthly kWh x Unit Price</div></div>");

  H("<div class='card'><div class='card-icon'>&#9989;</div>"
    "<div class='card-lbl'>Bill Status</div>"
    "<div id='lv-billstat' style='margin-top:6px'>");
  H(billBadge());
  H("</div>");
  if (hasLiveData) {
    H("<div style='margin-top:10px'>"
      "<button class='btn btn-sm ");
    H(billPaid ? "btn-danger" : "btn-success");
    H("' onclick='markBill(");
    H(billPaid ? 0 : 1);
    H(")'>");
    H(billPaid ? "Mark Unpaid" : "Mark Paid");
    H("</button></div>");
  }
  H("</div>");
  H("</div>"); // end billing cards

  // Relay control
  H("<div class='sec-hdr'>Relay Control</div><div class='cards'>");

  H("<div class='card'><div class='card-icon'>&#128268;</div>"
    "<div class='card-lbl'>Relay Status</div>"
    "<div id='lv-relay' style='margin-top:6px'>");
  H(relayBadge());
  H("</div><div class='card-sub'>Manual admin control only</div></div>");

  H("<div class='card'><div class='card-icon'>&#127899;</div>"
    "<div class='card-lbl'>Control</div>"
    "<div id='relBtn' style='margin-top:10px'>");
  if (ed.relay) {
    H("<button class='btn btn-danger btn-sm' onclick='setRelay(0)'>Cut Power</button>");
  } else {
    H("<button class='btn btn-success btn-sm' onclick='setRelay(1)'>Restore Power</button>");
  }
  H("</div><div class='card-sub'>Sent via LoRa</div></div>");

  H("<div class='card'><div class='card-icon'>&#128246;</div>"
    "<div class='card-lbl'>LoRa RSSI</div>"
    "<div class='card-val' id='lv-rssi'>");
  H(hasLiveData ? String(ed.rssi) + " dBm" : "--");
  H("</div><div class='card-sub'>Signal strength</div></div>");
  H("</div>"); // end relay cards

  // Charts
  H("<div class='chart-wrap'>"
    "<div class='chart-title'>Weekly Usage (kWh)</div>"
    "<canvas id='wc' height='80'></canvas></div>");
  H("<div class='chart-wrap'>"
    "<div class='chart-title'>Monthly Trend (kWh)</div>"
    "<canvas id='mc' height='80'></canvas></div>");
  emitWeeklyChart("wc");
  emitMonthlyChart("mc");

  H("<script>"
    "async function setRelay(s){"
    "const r=await fetch('/api/relay?s='+s,{method:'POST',credentials:'same-origin'});"
    "if(r.ok)setTimeout(()=>location.reload(),700);}"
    "async function markBill(s){"
    "const r=await fetch('/api/billstatus?s='+s,{method:'POST',credentials:'same-origin'});"
    "if(r.ok)setTimeout(()=>location.reload(),500);}"
    "</script>");

  emitPollScript(3000, true);
  pageFoot();
}

// ────────────────────────────────────────────────────────────────────
//  PAGE: UNIT PRICE  /admin/unitprice
// ────────────────────────────────────────────────────────────────────
void buildUnitPrice() {
  pageHead("Unit Price", true, "/admin/unitprice");
  H("<div class='page-header'><h1>Unit Price Configuration</h1>"
    "<p>Set the tariff rate for monthly billing.</p></div>");

  H("<div class='cards'>");
  H("<div class='card'><div class='card-icon'>&#128178;</div>"
    "<div class='card-lbl'>Current Price</div>"
    "<div class='card-val green'>&#8377;");
  H(unitPrice, 2);
  H("/kWh</div></div>");

  H("<div class='card'><div class='card-icon'>&#129534;</div>"
    "<div class='card-lbl'>Current Monthly Bill</div>"
    "<div class='card-val'>");
  H(hasLiveData ? "&#8377;" + String(ed.monthly * unitPrice, 2) : "--");
  H("</div><div class='card-sub'>");
  H(hasLiveData
    ? String(ed.monthly, 2) + " kWh x &#8377;" + String(unitPrice, 2)
    : "No live data yet");
  H("</div></div>");
  H("</div>");

  H("<div style='max-width:420px'><div class='card'>");
  H("<div class='sec-hdr' style='margin-top:0'>Set New Price</div>");
  H("<div class='form-group'><label>New Price (&#8377; per kWh)</label>"
    "<input type='number' id='price' step='0.5' min='1' max='100'"
    " placeholder='e.g. 8.00' value='");
  H(unitPrice, 2);
  H("'></div>");
  H("<button class='btn btn-primary btn-block' onclick='savePrice()'>Save Price</button>");
  H("<p id='pmsg' style='margin-top:12px;font-size:13px;"
    "color:var(--green);min-height:18px'></p>");
  H("</div></div>");

  if (hasLiveData) {
    H("<div class='sec-hdr'>Pricing Impact</div>");
    H("<div class='tbl-wrap'><table>"
      "<thead><tr><th>Description</th><th>Value</th></tr></thead><tbody>");
    H("<tr><td>Monthly Usage</td><td>");
    H(ed.monthly, 2); H(" kWh</td></tr>");
    H("<tr><td>Monthly Bill (current price)</td><td>&#8377;");
    H(ed.monthly * unitPrice, 2); H("</td></tr>");
    H("<tr><td>Lifetime Consumption</td><td>");
    H(ed.total, 3); H(" kWh</td></tr>");
    H("<tr><td>Lifetime Cost (current price)</td><td>&#8377;");
    H(ed.total * unitPrice, 2); H("</td></tr>");
    H("</tbody></table></div>");
  }

  H("<script>"
    "async function savePrice(){"
    "const p=parseFloat(document.getElementById('price').value);"
    "if(isNaN(p)||p<=0||p>100){alert('Enter a valid price (1-100)');return;}"
    "const r=await fetch('/api/unitprice',{"
    "method:'POST',credentials:'same-origin',"
    "body:'price='+p,"
    "headers:{'Content-Type':'application/x-www-form-urlencoded'}});"
    "const m=document.getElementById('pmsg');"
    "if(r.ok){m.style.color='var(--green)';"
    "m.textContent='Saved: \u20B9'+p.toFixed(2)+'/kWh';"
    "setTimeout(()=>location.reload(),1500);}"
    "else{m.style.color='var(--red)';m.textContent='Failed to save.';}"
    "}"
    "</script>");

  emitPollScript(10000);
  pageFoot();
}

// ────────────────────────────────────────────────────────────────────
//  PAGE: USER HOME  /
// ────────────────────────────────────────────────────────────────────
void buildUserHome() {
  pageHead("Home", false, "/");
  H("<div class='page-header'><h1>Welcome</h1>"
    "<p>Your postpaid energy meter overview.</p></div>");
  emitAlerts();

  float bill     = hasLiveData ? (ed.monthly * unitPrice) : 0.0f;
  float dailyAvg = (hasLiveData && ed.monthly > 0.0f) ? (ed.monthly / 30.0f) : 0.0f;

  H("<div class='cards'>");
  H("<div class='card'><div class='card-icon'>&#128198;</div>"
    "<div class='card-lbl'>Monthly Usage</div>"
    "<div class='card-val' id='lv-mo'>");
  H(hasLiveData ? String(ed.monthly, 2) + " kWh" : "--");
  H("</div><div class='card-sub'>Billing period</div></div>");

  H("<div class='card'><div class='card-icon'>&#128176;</div>"
    "<div class='card-lbl'>Monthly Bill</div>"
    "<div class='card-val' id='lv-bill'>");
  H(hasLiveData ? "&#8377;" + String(bill, 2) : "--");
  H("</div><div class='card-sub'>@ &#8377;"); H(unitPrice, 2); H("/kWh</div></div>");

  H("<div class='card'><div class='card-icon'>&#128202;</div>"
    "<div class='card-lbl'>Daily Average</div>"
    "<div class='card-val'>");
  H(hasLiveData ? String(dailyAvg, 2) + " kWh" : "--");
  H("</div><div class='card-sub'>Estimated from month</div></div>");

  H("<div class='card'><div class='card-icon'>&#129534;</div>"
    "<div class='card-lbl'>Bill Status</div>"
    "<div id='lv-billstat' style='margin-top:6px'>");
  H(billBadge());
  H("</div></div>");
  H("</div>");

  H("<div class='chart-wrap'>"
    "<div class='chart-title'>Monthly Consumption Trend (kWh)</div>"
    "<canvas id='mc' height='75'></canvas></div>");
  H("<div class='chart-wrap'>"
    "<div class='chart-title'>Last 7 Days (kWh)</div>"
    "<canvas id='wc' height='75'></canvas></div>");
  emitMonthlyChart("mc");
  emitWeeklyChart("wc");
  emitPollScript(5000);
  pageFoot();
}

// ────────────────────────────────────────────────────────────────────
//  PAGE: USER USAGE  /usage
// ────────────────────────────────────────────────────────────────────
void buildUserUsage() {
  pageHead("Live Usage", false,"/usage");
  H("<div class='page-header'><h1>Live Usage</h1>"
    "<p>Real-time electrical readings from your meter.</p></div>");
  emitAlerts();

  H("<div class='sec-hdr'>Real-Time Readings</div><div class='cards'>");

  struct { const char* icon; const char* lbl; const char* id;
           String val; const char* sub; } lr[] = {
    {"&#128268;","Voltage",   "lv-v", valOrDash(ed.voltage, 1)+" V",  "Nominal 230 V"},
    {"&#12316;", "Current",   "lv-a", valOrDash(ed.current, 2)+" A",  "Load current"},
    {"&#128161;","Power",     "lv-w", valOrDash(ed.power,   1)+" W",  "Active power"},
    {"&#128260;","Frequency", "lv-f", valOrDash(ed.freq,    1)+" Hz", "Grid frequency"},
  };
  for (auto& c : lr) {
    H("<div class='card'><div class='card-icon'>"); H(c.icon); H("</div>");
    H("<div class='card-lbl'>"); H(c.lbl); H("</div>");
    if (!hasLiveData) {
      H("<div class='skeleton skel-val'></div>");
    } else {
      H("<div class='card-val' id='"); H(c.id); H("'>"); H(c.val); H("</div>");
    }
    H("<div class='card-sub'>"); H(c.sub); H("</div></div>");
  }
  H("</div>");

  H("<div class='sec-hdr'>Consumption Summary</div><div class='cards'>");

  struct { const char* icon; const char* lbl; const char* id;
           String val; const char* sub; } cs[] = {
    {"&#128197;","Previous Day",   "lv-pd",  valOrDash(ed.prevDay, 2)+" kWh","Yesterday"},
    {"&#128198;","This Month",     "lv-mo",  valOrDash(ed.monthly, 2)+" kWh","Billing period"},
    {"&#128193;","Lifetime Total", "lv-tot", valOrDash(ed.total,   3)+" kWh","All time"},
  };
  for (auto& c : cs) {
    H("<div class='card'><div class='card-icon'>"); H(c.icon); H("</div>");
    H("<div class='card-lbl'>"); H(c.lbl); H("</div>");
    if (!hasLiveData) {
      H("<div class='skeleton skel-val'></div>");
    } else {
      H("<div class='card-val' id='"); H(c.id); H("'>"); H(c.val); H("</div>");
    }
    H("<div class='card-sub'>"); H(c.sub); H("</div></div>");
  }
  H("</div>");

  H("<div class='chart-wrap'>"
    "<div class='chart-title'>Last 7 Days (kWh)</div>"
    "<canvas id='wc' height='80'></canvas></div>");
  emitWeeklyChart("wc");
  emitPollScript(3000);
  pageFoot();
}

// ────────────────────────────────────────────────────────────────────
//  PAGE: USER BILL  /bill
// ────────────────────────────────────────────────────────────────────
void buildUserBill() {
  pageHead("My Bill", false, "/bill");
  H("<div class='page-header'><h1>My Bill</h1>"
    "<p>Current billing period summary and payment status.</p></div>");
  emitAlerts();

  float bill = hasLiveData ? (ed.monthly * unitPrice) : 0.0f;

  H("<div class='cards'>");
  H("<div class='card'><div class='card-icon'>&#128198;</div>"
    "<div class='card-lbl'>Monthly Usage</div>"
    "<div class='card-val' id='lv-mo'>");
  H(hasLiveData ? String(ed.monthly, 2) + " kWh" : "--");
  H("</div></div>");

  H("<div class='card'><div class='card-icon'>&#128178;</div>"
    "<div class='card-lbl'>Unit Price</div>"
    "<div class='card-val accent'>&#8377;");
  H(unitPrice, 2);
  H("/kWh</div></div>");

  H("<div class='card'><div class='card-icon'>&#128176;</div>"
    "<div class='card-lbl'>Total Bill</div>"
    "<div class='card-val' id='lv-bill'>");
  H(hasLiveData ? "&#8377;" + String(bill, 2) : "--");
  H("</div></div>");

  H("<div class='card'><div class='card-icon'>&#129534;</div>"
    "<div class='card-lbl'>Status</div>"
    "<div id='lv-billstat' style='margin-top:6px'>");
  H(billBadge());
  H("</div></div>");
  H("</div>"); // end cards

  // Bill breakdown table
  H("<div class='sec-hdr'>Bill Breakdown</div>");
  H("<div class='tbl-wrap'><table>"
    "<thead><tr><th>Description</th><th>Detail</th><th>Amount</th></tr></thead><tbody>");

  H("<tr><td>Energy Consumed</td><td>");
  H(hasLiveData ? String(ed.monthly, 2) + " kWh x &#8377;" + String(unitPrice, 2) : "--");
  H("</td><td>");
  H(hasLiveData ? "&#8377;" + String(bill, 2) : "--");
  H("</td></tr>");

  H("<tr><td>Previous Day Usage</td><td>");
  H(hasLiveData ? String(ed.prevDay, 2) + " kWh" : "--");
  H("</td><td>&mdash;</td></tr>");

  H("<tr><td>Lifetime Consumption</td><td>");
  H(hasLiveData ? String(ed.total, 3) + " kWh" : "--");
  H("</td><td>&mdash;</td></tr>");

  H("</tbody></table></div>");

  if (hasLiveData && !billPaid) {
    H("<div class='alert alert-yellow'>"
      "<span style='font-size:18px'>&#8505;</span>"
      "<div>Your bill is <strong>unpaid</strong>. "
      "Please contact your energy provider to settle the amount.</div></div>");
  }

  emitPollScript(8000);
  pageFoot();
}

// ────────────────────────────────────────────────────────────────────
//  PAGE: SETTINGS  /settings
// ────────────────────────────────────────────────────────────────────
void buildSettings() {
  pageHead("Settings", isAdminSession, "/settings");
  H("<div class='page-header'><h1>Settings</h1>"
    "<p>System information and account details.</p></div>");

  H("<div class='sec-hdr'>Profile</div>");
  H("<div style='max-width:480px'><div class='card'>");

  // Info row helper via lambda
  auto IR = [&](const char* lbl, const char* val) {
    g_html += "<div class='info-row'>"
              "<span class='lbl'>"; g_html += lbl; g_html += "</span>"
              "<span class='val'>"; g_html += val; g_html += "</span>"
              "</div>";
  };

  IR("Role",       isAdminSession ? "Administrator" : "User");
  IR("User ID",    isAdminSession ? ADMIN_ID : USER_ID);
  IR("Device ID",  DEVICE_ID);
  IR("Firmware",   "v3.0 Postpaid");

  {
    String ssid = WiFi.SSID();
    IR("WiFi SSID", ssid.c_str());
    String ip = (WiFi.status() == WL_CONNECTED)
                  ? WiFi.localIP().toString()
                  : "Not connected";
    IR("IP Address", ip.c_str());
  }

  {
    char rb[20];
    if (hasLiveData) {
      snprintf(rb, sizeof(rb), "%d dBm", ed.rssi);
    } else {
      snprintf(rb, sizeof(rb), "--");
    }
    IR("LoRa RSSI", rb);
    IR("LoRa Status",
       !hasLiveData ? "Waiting for data"
       : (loraLost  ? "Signal lost"
                    : "Online"));
    IR("Live Data", hasLiveData ? "Active" : "Waiting");
  }

  H("</div></div>");

  H("<div class='sec-hdr'>Help</div>");
  H("<div style='max-width:480px'><div class='card'>"
    "<p style='font-size:13px;color:var(--sub);line-height:1.9'>"
    "&#x2022; Pages update via /api/data fetch polling &mdash; no full reloads.<br>"
    "&#x2022; Admin controls relay manually. No automatic cut-off.<br>"
    "&#x2022; Monthly kWh resets automatically on the 1st of each month.<br>"
    "&#x2022; Unit price is saved in flash memory across reboots.<br>"
    "&#x2022; LoRa signal loss freezes last known values with a warning.<br>"
    "&#x2022; Values show -- until the first valid LoRa packet is received."
    "</p></div></div>");

  H("<div class='sec-hdr'>Session</div>");
  H("<div style='max-width:480px'><div class='card'>"
    "<p style='font-size:13px;color:var(--sub);margin-bottom:14px'>"
    "End your current session securely.</p>"
    "<button class='btn btn-danger' onclick=\"location='/out'\">Logout Now</button>"
    "</div></div>");

  pageFoot();
}

// ────────────────────────────────────────────────────────────────────
//  LoRa PACKET VALIDATOR + PARSER
//  Format: TX,<id>,<V>,<A>,<W>,<Hz>,<kWhtotal>,<kWhmonthly>,<kWhprevday>,<relay>
// ────────────────────────────────────────────────────────────────────
bool parseLoRaPacket(const String& raw) {
  if (!raw.startsWith("TX,")) return false;

  // Require at least 9 commas (10 fields)
  uint8_t commas = 0;
  for (size_t i = 0; i < raw.length(); i++) {
    if (raw[i] == ',') commas++;
  }
  if (commas < 9) return false;

  char buf[256];
  raw.toCharArray(buf, sizeof(buf));

  char* tok;
  tok = strtok(buf, ","); if (!tok) return false;  // "TX"
  tok = strtok(NULL, ","); if (!tok) return false;  // device id

  tok = strtok(NULL, ","); if (!tok) return false;
  float v = atof(tok);
  if (v < 50.0f || v > 500.0f) return false;        // voltage sanity
  ed.voltage = v;

  tok = strtok(NULL, ","); if (!tok) return false;
  float a = atof(tok);
  if (a < 0.0f || a > 100.0f) return false;         // current sanity
  ed.current = a;

  tok = strtok(NULL, ","); if (!tok) return false;
  float w = atof(tok);
  if (w < 0.0f || w > 25000.0f) return false;       // power sanity
  ed.power = w;

  tok = strtok(NULL, ","); if (!tok) return false;
  float f = atof(tok);
  if (f < 40.0f || f > 70.0f) return false;         // frequency sanity
  ed.freq = f;

  tok = strtok(NULL, ","); if (!tok) return false;
  ed.total = atof(tok);

  tok = strtok(NULL, ","); if (!tok) return false;
  ed.monthly = atof(tok);

  tok = strtok(NULL, ","); if (!tok) return false;
  ed.prevDay = atof(tok);

  tok = strtok(NULL, ","); if (!tok) return false;
  ed.relay = (atoi(tok) == 1);

  return true;
}

// ────────────────────────────────────────────────────────────────────
//  OLED — yellow zone (y 0-15): title | blue zone (y 16-63): IP+RSSI
// ────────────────────────────────────────────────────────────────────
void updateOLED() {
  lastOledRefresh = millis();
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);

  // Yellow zone — centered title
  oled.setCursor(22, 4);
  oled.print(F("ENERGY MONITOR"));
  oled.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  // Blue zone — IP
  oled.setCursor(0, 21);
  oled.print(F("IP:"));
  oled.setCursor(20, 21);
  if (WiFi.status() == WL_CONNECTED) {
    oled.print(WiFi.localIP());
  } else {
    oled.print(F("Connecting..."));
  }

  // Blue zone — RSSI
  oled.setCursor(0, 38);
  oled.print(F("RSSI:"));
  oled.setCursor(34, 38);
  if (hasLiveData) {
    oled.print(ed.rssi);
    oled.print(F(" dBm"));
  } else {
    oled.print(F("--"));
  }

  // Status line
  oled.setCursor(0, 54);
  if (loraLost) {
    oled.print(F("! Signal lost"));
  } else if (!hasLiveData) {
    oled.print(F("Waiting for data"));
  }

  oled.display();
}

// ────────────────────────────────────────────────────────────────────
//  LoRa TX — send command to transmitter
// ────────────────────────────────────────────────────────────────────
void sendLoRa(const char* type, float val) {
  if (!loRaActive) return;
  LoRa.beginPacket();
  LoRa.print(type);
  LoRa.print(',');
  LoRa.print(val, 2);
  LoRa.endPacket();
}

// ────────────────────────────────────────────────────────────────────
//  MONTHLY RESET — NTP timestamp based
// ────────────────────────────────────────────────────────────────────
void checkMonthlyReset() {
  struct tm t;
  if (!getLocalTime(&t)) return;
  uint8_t curMonth = (uint8_t)t.tm_mon;
  if (lastResetMonth == 255) {
    lastResetMonth = curMonth;
    return;
  }
  if (curMonth != lastResetMonth) {
    // Shift monthly history buffer
    for (int i = 0; i < 5; i++) monHist[i] = monHist[i + 1];
    monHist[5]     = ed.monthly;
    ed.monthly     = 0.0f;
    billPaid       = false;
    lastResetMonth = curMonth;
    prefs.begin("energy", false);
    prefs.putFloat("monthly",  0.0f);
    prefs.putBool ("paid",     false);
    prefs.putUChar("resetmon", curMonth);
    prefs.end();
    Serial.println(F("[BILLING] Monthly reset done."));
  }
}

// ────────────────────────────────────────────────────────────────────
//  AUTH HELPERS
// ────────────────────────────────────────────────────────────────────
bool checkAuth() {
  if (!server.hasHeader("Cookie")) return false;
  String cookie = server.header("Cookie");
  int idx = cookie.indexOf("SES=");
  if (idx == -1) return false;
  String tok = cookie.substring(idx + 4, idx + 20);
  if (tok == String(sessionToken) && millis() < sessionExp) {
    sessionExp = millis() + 3600000UL; // sliding window
    return true;
  }
  return false;
}

void redirectLogin() {
  server.sendHeader("Location", "/login");
  server.send(303);
}

void sendForbidden() {
  server.send(403, F("application/json"), F("{\"error\":\"forbidden\"}"));
}

// ────────────────────────────────────────────────────────────────────
//  SETUP
// ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println(F("\n[BOOT] Postpaid Energy Meter v3.0"));

  // ── OLED ─────────────────────────────────────────────────────────
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("[OLED] FAIL"));
  } else {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(22, 4);
    oled.print(F("ENERGY MONITOR"));
    oled.drawLine(0, 15, 127, 15, SSD1306_WHITE);
    oled.setCursor(0, 24);
    oled.print(F("Booting..."));
    oled.display();
    Serial.println(F("[OLED] OK"));
  }

  // ── Preferences — ONLY unitPrice loaded ────────────────���─────────
  prefs.begin("energy", true);
  unitPrice      = prefs.getFloat("unitprice",  8.0f);
  billPaid       = prefs.getBool ("paid",       false);
  lastResetMonth = prefs.getUChar("resetmon",   255);
  prefs.end();
  Serial.printf("[PREFS] unitPrice=%.2f\n", unitPrice);
  // NOTE: total / monthly / prevDay intentionally NOT loaded.
  // All energy values start at 0 and populate from LoRa only.

  // ── LoRa ─────────────────────────────────────────────────────────
  loRaSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setSPI(loRaSPI);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (LoRa.begin(LORA_FREQ)) {
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW);
    LoRa.setCodingRate4(5);
    LoRa.enableCrc();
    loRaActive = true;
    Serial.println(F("[LoRa] OK"));
  } else {
    Serial.println(F("[LoRa] FAIL - WiFi-only mode"));
  }

  // ── WiFi ─────────────────────────────────────────────────────────
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("[WiFi] Connecting"));
  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 40) {
    delay(300);
    Serial.print('.');
    if (tries % 6 == 0) {
      oled.clearDisplay();
      oled.setCursor(22, 4);
      oled.print(F("ENERGY MONITOR"));
      oled.drawLine(0, 15, 127, 15, SSD1306_WHITE);
      oled.setCursor(0, 24);
      oled.print(F("WiFi connecting..."));
      oled.setCursor(0, 38);
      oled.print(tries / 6);
      oled.print(F(" attempts"));
      oled.display();
    }
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[WiFi] IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("[WiFi] Failed - check credentials"));
  }

  // ── NTP ──────────────────────────────────────────────────────────
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
  Serial.println(F("[NTP] Sync requested"));

  // ── Initial OLED ─────────────────────────────────────────────────
  updateOLED();

  // ── Web Server Routes ─────────────────────────────────────────────
  const char* hdrs[] = {"Cookie"};
  server.collectHeaders(hdrs, 1);

  // /login
  server.on("/login", HTTP_GET, []() {
    String page = FPSTR(LOGIN_HTML);
    page.replace("__ERR__", "");
    server.send(200, F("text/html"), page);
  });

  // /auth
  server.on("/auth", HTTP_POST, []() {
    String u = server.arg("u");
    String p = server.arg("p");
    bool adm  = (u == ADMIN_ID && p == ADMIN_PASS);
    bool usr  = (u == USER_ID  && p == USER_PASS);
    if (adm || usr) {
      uint32_t r1 = esp_random(), r2 = esp_random();
      snprintf(sessionToken, sizeof(sessionToken),
               "%08lx%08lx", (unsigned long)r1, (unsigned long)r2);
      sessionExp     = millis() + 3600000UL;
      isAdminSession = adm;
      server.sendHeader("Location", adm ? "/admin" : "/");
      server.sendHeader("Set-Cookie",
        String("SES=") + sessionToken + "; Path=/; HttpOnly");
      server.send(303);
      Serial.printf("[AUTH] OK: %s (%s)\n",
                    u.c_str(), adm ? "admin" : "user");
    } else {
      String page = FPSTR(LOGIN_HTML);
      page.replace("__ERR__",
        "<div class='err'>Invalid credentials. Please try again.</div>");
      server.send(200, F("text/html"), page);
      Serial.println(F("[AUTH] Failed attempt"));
    }
  });

  // /out
  server.on("/out", HTTP_GET, []() {
    memset(sessionToken, 0, sizeof(sessionToken));
    sessionExp = 0;
    server.sendHeader("Set-Cookie", "SES=; Path=/; Max-Age=0");
    server.sendHeader("Location", "/login");
    server.send(303);
  });

  // /  (user home)
  server.on("/", HTTP_GET, []() {
    if (!checkAuth()) { redirectLogin(); return; }
    g_html = "";
    buildUserHome();
    server.send(200, F("text/html"), g_html);
  });

  // /usage
  server.on("/usage", HTTP_GET, []() {
    if (!checkAuth()) { redirectLogin(); return; }
    g_html = "";
    buildUserUsage();
    server.send(200, F("text/html"), g_html);
  });

  // /bill
  server.on("/bill", HTTP_GET, []() {
    if (!checkAuth()) { redirectLogin(); return; }
    g_html = "";
    buildUserBill();
    server.send(200, F("text/html"), g_html);
  });

  // /settings
  server.on("/settings", HTTP_GET, []() {
    if (!checkAuth()) { redirectLogin(); return; }
    g_html = "";
    buildSettings();
    server.send(200, F("text/html"), g_html);
  });

  // /admin  (table or detail)
  server.on("/admin", HTTP_GET, []() {
    if (!checkAuth() || !isAdminSession) { redirectLogin(); return; }
    g_html = "";
    if (server.hasArg("id") && server.arg("id") == DEVICE_ID) {
      buildAdminDetail();
    } else {
      buildAdminTable();
    }
    server.send(200, F("text/html"), g_html);
  });

  // /admin/unitprice
  server.on("/admin/unitprice", HTTP_GET, []() {
    if (!checkAuth() || !isAdminSession) { redirectLogin(); return; }
    g_html = "";
    buildUnitPrice();
    server.send(200, F("text/html"), g_html);
  });

  // /api/data  — JSON polling endpoint
  server.on("/api/data", HTTP_GET, []() {
    if (!checkAuth()) {
      server.send(401, F("application/json"),
                  F("{\"error\":\"unauthorized\"}"));
      return;
    }
    char jbuf[400];
    if (!hasLiveData) {
      snprintf(jbuf, sizeof(jbuf),
        "{\"hasLiveData\":false,\"loraLost\":false,"
        "\"voltage\":0,\"current\":0,\"power\":0,\"freq\":0,"
        "\"total\":0,\"monthly\":0,\"prevDay\":0,"
        "\"relay\":true,\"rssi\":0,"
        "\"unitPrice\":%.2f,\"bill\":0,\"paid\":false}",
        unitPrice);
    } else {
      snprintf(jbuf, sizeof(jbuf),
        "{\"hasLiveData\":true,\"loraLost\":%s,"
        "\"voltage\":%.1f,\"current\":%.2f,"
        "\"power\":%.1f,\"freq\":%.1f,"
        "\"total\":%.3f,\"monthly\":%.2f,\"prevDay\":%.2f,"
        "\"relay\":%s,\"rssi\":%d,"
        "\"unitPrice\":%.2f,\"bill\":%.2f,\"paid\":%s}",
        loraLost ? "true" : "false",
        ed.voltage, ed.current, ed.power, ed.freq,
        ed.total, ed.monthly, ed.prevDay,
        ed.relay  ? "true" : "false",
        ed.rssi,
        unitPrice,
        ed.monthly * unitPrice,
        billPaid  ? "true" : "false");
    }
    server.sendHeader("Cache-Control", "no-cache, no-store");
    server.send(200, F("application/json"), jbuf);
  });

  // /api/relay
  server.on("/api/relay", HTTP_POST, []() {
    if (!checkAuth() || !isAdminSession) { sendForbidden(); return; }
    if (!server.hasArg("s")) {
      server.send(400, F("application/json"),
                  F("{\"error\":\"missing s\"}"));
      return;
    }
    int s = server.arg("s").toInt();
    if (s != 0 && s != 1) {
      server.send(400, F("application/json"),
                  F("{\"error\":\"s must be 0 or 1\"}"));
      return;
    }
    ed.relay = (s == 1);
    sendLoRa("RELAY", (float)s);
    Serial.printf("[RELAY] -> %s\n", ed.relay ? "ON" : "OFF");
    server.send(200, F("application/json"), F("{\"ok\":true}"));
  });

  // /api/unitprice
  server.on("/api/unitprice", HTTP_POST, []() {
    if (!checkAuth() || !isAdminSession) { sendForbidden(); return; }
    if (!server.hasArg("price")) {
      server.send(400, F("application/json"),
                  F("{\"error\":\"missing price\"}"));
      return;
    }
    float p = server.arg("price").toFloat();
    if (p <= 0.0f || p > 100.0f) {
      server.send(400, F("application/json"),
                  F("{\"error\":\"price out of range\"}"));
      return;
    }
    unitPrice = p;
    prefs.begin("energy", false);
    prefs.putFloat("unitprice", unitPrice);
    prefs.end();
    Serial.printf("[PRICE] Rs%.2f/kWh\n", unitPrice);
    char resp[48];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"unitPrice\":%.2f}", unitPrice);
    server.send(200, F("application/json"), resp);
  });

  // /api/billstatus
  server.on("/api/billstatus", HTTP_POST, []() {
    if (!checkAuth() || !isAdminSession) { sendForbidden(); return; }
    if (!server.hasArg("s")) {
      server.send(400, F("application/json"),
                  F("{\"error\":\"missing s\"}"));
      return;
    }
    billPaid = (server.arg("s").toInt() == 1);
    prefs.begin("energy", false);
    prefs.putBool("paid", billPaid);
    prefs.end();
    Serial.printf("[BILL] -> %s\n", billPaid ? "PAID" : "UNPAID");
    char resp[42];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"paid\":%s}",
             billPaid ? "true" : "false");
    server.send(200, F("application/json"), resp);
  });

  // 404 catch-all
  server.onNotFound([]() {
    server.sendHeader("Location",
      checkAuth() ? (isAdminSession ? "/admin" : "/") : "/login");
    server.send(303);
  });

  server.begin();
  Serial.println(F("[SERVER] Running on port 80"));
  updateOLED();
}

// ────────────────────────────────────────────────────────────────────
//  LOOP
// ────────────────────────────────────────────────────────────────────
void loop() {

  // 1. HTTP clients
  server.handleClient();

  // 2. LoRa RX — receive, validate, parse
  if (loRaActive) {
    int pktSize = LoRa.parsePacket();
    if (pktSize > 0 && pktSize < 256) {
      String raw = "";
      raw.reserve(pktSize + 1);
      while (LoRa.available()) raw += (char)LoRa.read();
      Serial.print(F("[LoRa] RX: "));
      Serial.println(raw);

      if (parseLoRaPacket(raw)) {
        ed.rssi   = LoRa.packetRssi();
        ed.lastRx = millis();

        if (!hasLiveData) {
          hasLiveData = true;
          loraLost    = false;
          Serial.println(F("[LoRa] First valid packet - live data active"));
          updateOLED();
        }

        if (loraLost) {
          loraLost = false;
          Serial.println(F("[LoRa] Signal recovered"));
          updateOLED();
        }

        // Update today's slot in day history
        dayHist[6] = ed.prevDay;

        Serial.printf(
          "[DATA] V=%.1f A=%.2f W=%.1f Hz=%.1f "
          "tot=%.3f mo=%.2f pd=%.2f rssi=%d\n",
          ed.voltage, ed.current, ed.power, ed.freq,
          ed.total, ed.monthly, ed.prevDay, ed.rssi);

        // Throttled Preferences save — never save zeros
        uint32_t now = millis();
        if ((now - lastPrefSave) > PREF_SAVE_INTERVAL_MS) {
          lastPrefSave = now;
          if (ed.total > 0.0f || ed.monthly > 0.0f) {
            prefs.begin("energy", false);
            prefs.putFloat("total",     ed.total);
            prefs.putFloat("monthly",   ed.monthly);
            prefs.putFloat("prevday",   ed.prevDay);
            prefs.putFloat("unitprice", unitPrice);
            prefs.end();
            Serial.println(F("[PREFS] Saved."));
          }
        }

        updateOLED();

      } else {
        Serial.println(F("[LoRa] Packet rejected - validation failed"));
      }
    }
  }

  // 3. LoRa fail-safe — detect signal loss (only after first packet)
  if (loRaActive && hasLiveData && !loraLost) {
    if ((millis() - ed.lastRx) > LORA_TIMEOUT_MS) {
      loraLost = true;
      Serial.println(F("[LoRa] SIGNAL LOST - values frozen"));
      updateOLED();
    }
  }

  // 4. OLED keep-alive (flicker-free, only when due)
  if ((millis() - lastOledRefresh) > OLED_REFRESH_MS) {
    updateOLED();
  }

  // 5. Monthly billing reset check
  if ((millis() - lastMonthCheck) > MONTH_CHECK_MS) {
    lastMonthCheck = millis();
    checkMonthlyReset();
  }

  // 6. WiFi watchdog
  if ((millis() - lastWiFiCheck) > WIFI_WATCH_MS) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("[WiFi] Disconnected - reconnecting..."));
      WiFi.reconnect();
    }
  }
}