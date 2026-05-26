// ============================================================
//  Thread Pool Server  –  Sci-Fi GUI
//  Pure Win32 / GDI  –  NO external libraries required
//  Flicker-free via proper double-buffering + WM_ERASEBKGND
// ============================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cmath>
#include <memory>
#include <algorithm>

#include "ThreadPool.h"
#include "Tasks.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// ── globals used by Tasks.h ──────────────────────────────────────────────
std::function<void(const std::string&)> g_log;
std::mutex                              g_log_mtx;

// ── colours (GDI COLORREF = 0x00BBGGRR) ──────────────────────────────────
static const COLORREF C_BG    = RGB(0,   0,   0  );
static const COLORREF C_CYAN  = RGB(0,   211, 255);
static const COLORREF C_RED   = RGB(255, 0,   60 );
static const COLORREF C_GREEN = RGB(57,  255, 20 );
static const COLORREF C_GRID  = RGB(28,  28,  28 );
static const COLORREF C_DARK  = RGB(17,  17,  17 );
static const COLORREF C_PANEL = RGB(10,  10,  10 );

// ── window dimensions ─────────────────────────────────────────────────────
static const int WIN_W = 1100;
static const int WIN_H = 750;

// ── IDs ───────────────────────────────────────────────────────────────────
enum {
    ID_TIMER    = 1,
    ID_BTN_DEMO = 100, ID_BTN_BENCH, ID_BTN_WAIT,
    ID_BTN_TIME, ID_BTN_EXIT,
    ID_BTN_ADD_PRINT, ID_BTN_ADD_SLEEP, ID_BTN_ADD_CPU, ID_BTN_ADD_IO,
    ID_EDIT_PRINT, ID_EDIT_SLEEP, ID_EDIT_CPU, ID_EDIT_IO,
    ID_LOG_BOX,
};

// ── history ───────────────────────────────────────────────────────────────
struct HistEntry { std::string name, thread; double start=0, end=0; bool done=false; };

// ═══════════════════════════════════════════════════════════════════════════
//  App state
// ═══════════════════════════════════════════════════════════════════════════
struct AppState {
    HWND hwnd = nullptr;

    std::unique_ptr<ThreadPool> pool;

    std::queue<std::string> logQ;
    std::mutex              logMtx;

    std::vector<HistEntry>  history;
    std::mutex              histMtx;

    int  scanY        = 0;
    bool showTimeline = false;
    std::vector<HistEntry> timelineSnap;

    // ── Double-buffer: ONE persistent off-screen DC ───────────────────────
    // Created once in WM_CREATE, resized in WM_SIZE, destroyed in WM_DESTROY.
    HDC     memDC  = nullptr;
    HBITMAP memBmp = nullptr;
    HBITMAP memOld = nullptr;   // original 1x1 bitmap to restore before delete
    int     bmpW   = 0, bmpH   = 0;

    // child controls
    HWND hEditPrint=0, hEditSleep=0, hEditCpu=0, hEditIo=0, hLog=0;

    // font handles
    HFONT hFontBig=0, hFontMono=0, hFontSmall=0;

    // cached brushes (created once, destroyed on exit)
    HBRUSH brDark=0, brTerm=0;
} g;

// ═══════════════════════════════════════════════════════════════════════════
//  Resize (or create) the off-screen bitmap
// ═══════════════════════════════════════════════════════════════════════════
static void resizeBuffer(HDC screenDC, int w, int h) {
    if (g.memBmp) {
        SelectObject(g.memDC, g.memOld);   // deselect before deleting
        DeleteObject(g.memBmp);
        g.memBmp = nullptr;
    }
    g.memBmp = CreateCompatibleBitmap(screenDC, w, h);
    g.memOld = (HBITMAP)SelectObject(g.memDC, g.memBmp);
    g.bmpW = w; g.bmpH = h;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════════
static double nowSec() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::string editText(HWND h) {
    char buf[512]{}; GetWindowTextA(h, buf, 511); return buf;
}
static void logAppend(const std::string& s) {
    std::lock_guard<std::mutex> lk(g.logMtx);
    g.logQ.push(s);
}
static void drainLog() {
    std::lock_guard<std::mutex> lk(g.logMtx);
    while (!g.logQ.empty()) {
        std::string s = g.logQ.front(); g.logQ.pop();
        int cnt = (int)SendMessageA(g.hLog, LB_GETCOUNT, 0, 0);
        if (cnt > 500) SendMessageA(g.hLog, LB_DELETESTRING, 0, 0);
        int idx = (int)SendMessageA(g.hLog, LB_ADDSTRING, 0, (LPARAM)s.c_str());
        SendMessageA(g.hLog, LB_SETTOPINDEX, (WPARAM)idx, 0);
    }
}

// ─── GDI primitives (all draw into g.memDC) ──────────────────────────────
static void fillR(int x,int y,int w,int h,COLORREF c){
    HBRUSH br=CreateSolidBrush(c);
    RECT r={x,y,x+w,y+h}; FillRect(g.memDC,&r,br); DeleteObject(br);
}
static void outlineR(int x,int y,int w,int h,COLORREF c,int t=1){
    HPEN pen=CreatePen(PS_SOLID,t,c);
    HPEN op=(HPEN)SelectObject(g.memDC,pen);
    HBRUSH ob=(HBRUSH)SelectObject(g.memDC,GetStockObject(NULL_BRUSH));
    Rectangle(g.memDC,x,y,x+w,y+h);
    SelectObject(g.memDC,op); DeleteObject(pen);
    SelectObject(g.memDC,ob);
}
static void hline(int x,int y,int len,COLORREF c){
    HPEN pen=CreatePen(PS_SOLID,1,c);
    HPEN op=(HPEN)SelectObject(g.memDC,pen);
    MoveToEx(g.memDC,x,y,nullptr); LineTo(g.memDC,x+len,y);
    SelectObject(g.memDC,op); DeleteObject(pen);
}
static void vline(int x,int y,int len,COLORREF c){
    HPEN pen=CreatePen(PS_SOLID,1,c);
    HPEN op=(HPEN)SelectObject(g.memDC,pen);
    MoveToEx(g.memDC,x,y,nullptr); LineTo(g.memDC,x,y+len);
    SelectObject(g.memDC,op); DeleteObject(pen);
}
static void txt(const std::string& s,int x,int y,COLORREF c,HFONT f){
    HFONT pf=(HFONT)SelectObject(g.memDC,f);
    SetTextColor(g.memDC,c);
    SetBkMode(g.memDC,TRANSPARENT);
    TextOutA(g.memDC,x,y,s.c_str(),(int)s.size());
    SelectObject(g.memDC,pf);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Draw everything into g.memDC, then one BitBlt to screen
// ═══════════════════════════════════════════════════════════════════════════
static void doPaint(HDC screenDC) {
    int cw = g.bmpW, ch = g.bmpH;
    HDC dc = g.memDC;                   // ALL drawing goes here

    // ── background ───────────────────────────────────────────────────────
    fillR(0, 0, cw, ch, C_BG);

    // ── grid (dim lines only — fast) ─────────────────────────────────────
   // Grid drawing loops ko aise update karein:
for (int x = 0; x < cw; x += 40) vline(x, 0, 280, C_GRID);
for (int y = 0; y < 280; y += 40) hline(0, y, cw, C_GRID);

    // ── scan line (3 rows: dark / mid / bright) ──────────────────────────
    if (g.scanY > 0 && g.scanY < ch-2) {
        hline(0, g.scanY,   cw, RGB(0,40,50));
        hline(0, g.scanY+1, cw, RGB(0,100,120));
        hline(0, g.scanY+2, cw, C_CYAN);
    }

    // ── header ───────────────────────────────────────────────────────────
    txt("THREAD POOL SERVER", 20, 14, C_CYAN, g.hFontBig);

    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{}; localtime_s(&tm, &t);
    std::ostringstream oss; oss << std::put_time(&tm, "%H:%M:%S");
    txt(oss.str(), 960, 22, RGB(255,255,255), g.hFontMono);

    hline(20, 60, 1060, RGB(0,80,100));

    // ── thread status boxes ───────────────────────────────────────────────
    // doPaint function ke andar isay replace karein:
    {
        auto statuses = g.pool->getStatus();
        int n = 4; // Total 4 threads
        int bw = 240; // Box ki width thori barha di
        int bh = 50;  // Box ki height
        int spacing = 20;

        txt("ACTIVE THREADS", 20, 70, RGB(200,200,200), g.hFontSmall);

        for (int i = 0; i < n; ++i) {
            bool busy = (i < (int)statuses.size() && statuses[i]);

            // 2x2 Grid Logic:
            // x-position: pehla aur teesra thread left pe, doosra aur chotha right pe
            int bx = 20 + (i % 2) * (bw + spacing);
            // y-position: pehle do threads y=100 pe, baqi do y=160 pe
            int by = 100 + (i / 2) * (bh + 10);

            fillR(bx, by, bw, bh, busy ? C_RED : RGB(10,10,10));
            outlineR(bx, by, bw, bh, busy ? RGB(255,255,255) : C_CYAN);

            COLORREF tc = busy ? RGB(0,0,0) : C_GREEN;
            txt("Thread " + std::to_string(i+1), bx+12, by+6, tc, g.hFontMono);
            txt(busy ? "[BUSY]" : "[IDLE]", bx+12, by+26, tc, g.hFontMono);
        }

    }

    // ── left section labels ───────────────────────────────────────────────
    txt(":: TASK INPUTS ::", 20, 250, C_CYAN, g.hFontMono);
    {
        const char* labels[] = {"Print text:","Sleep (s):","Fib(n):","IO text:"};
        int ys[] = {308,348,388,428};
        for (int i=0;i<4;++i)
            txt(labels[i], 20, ys[i]+6, RGB(200,200,200), g.hFontSmall);
    }
    txt(":: CONTROLS ::", 20, 468, C_CYAN, g.hFontMono);

    // ── log panel outline ─────────────────────────────────────────────────
    outlineR(530, 98, 540, 580, C_CYAN);
    txt(" LOG OUTPUT ", 536, 103, C_CYAN, g.hFontSmall);

    // ── timeline overlay ──────────────────────────────────────────────────
    if (g.showTimeline && !g.timelineSnap.empty()) {
        // semi-dim the background (just a dark translucent rect)
        fillR(0, 0, cw, ch, RGB(0,0,0));

        fillR   (100,125,900,500, C_PANEL);
        outlineR(100,125,900,500, C_CYAN,2);

        txt("THREAD EXECUTION TIMELINE   [press T or click to close]",
            120,135, C_CYAN, g.hFontMono);

        double tMin=1e18, tMax=-1e18;
        std::vector<std::string> threads;
        for (auto& h : g.timelineSnap) {
            if (h.start < tMin) tMin = h.start;
            if (h.end   > tMax) tMax = h.end;
            if (std::find(threads.begin(),threads.end(),h.thread)==threads.end())
                threads.push_back(h.thread);
        }
        std::sort(threads.begin(),threads.end());
        double span = (tMax-tMin < 0.001) ? 0.001 : tMax-tMin;

        const float px=220,py=170,pw=760,rowH=50;
        for (int ti=0;ti<(int)threads.size();++ti){
            float ry=py+ti*rowH;
            txt(threads[ti],(int)115,(int)(ry+10),RGB(255,255,255),g.hFontSmall);
            for (auto& h : g.timelineSnap){
                if (h.thread!=threads[ti]||!h.done) continue;
                int bx2  = (int)(px+(h.start-tMin)/span*pw);
                int bwid = std::max(4,(int)((h.end-h.start)/span*pw));
                fillR   (bx2,(int)(ry+5),bwid,30,C_CYAN);
                outlineR(bx2,(int)(ry+5),bwid,30,RGB(200,200,200));
                txt(h.name,bx2+3,(int)(ry+12),RGB(0,0,0),g.hFontSmall);
            }
        }
        // axis labels
        txt("0s", (int)px, (int)(py+threads.size()*rowH+5), RGB(200,200,200), g.hFontSmall);
        std::ostringstream xr; xr<<std::fixed<<std::setprecision(1)<<span<<"s";
        txt(xr.str(),(int)(px+pw-24),(int)(py+threads.size()*rowH+5),RGB(200,200,200),g.hFontSmall);
    }

    // ── blit entire back-buffer to screen in ONE operation ────────────────
    BitBlt(screenDC, 0, 0, cw, ch, dc, 0, 0, SRCCOPY);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Task submission
// ═══════════════════════════════════════════════════════════════════════════
static void submitTask(const std::string& name, std::function<void()> fn) {
    HistEntry he; he.name=name; he.start=nowSec();
    { std::lock_guard<std::mutex> lk(g.histMtx); g.history.push_back(he); }
    int idx = (int)g.history.size()-1;
    g.pool->addTask([idx,fn,name]{
        std::ostringstream o; o<<std::this_thread::get_id();
        std::string thr="T-"+o.str().substr(o.str().size()>4?o.str().size()-4:0);
        { std::lock_guard<std::mutex> lk(g.histMtx);
          if (idx<(int)g.history.size()) g.history[idx].thread=thr; }
        fn();
        { std::lock_guard<std::mutex> lk(g.histMtx);
          if (idx<(int)g.history.size()) {
              g.history[idx].end=nowSec(); g.history[idx].done=true; } }
    });
}

// ═══════════════════════════════════════════════════════════════════════════
//  Actions
// ═══════════════════════════════════════════════════════════════════════════
static void doAddPrint(){ std::string v=editText(g.hEditPrint); submitTask("task_print",[v]{task_print(v);}); }
static void doAddSleep(){ try{double v=std::stod(editText(g.hEditSleep)); submitTask("task_sleep",[v]{task_sleep(v);});}catch(...){emit("ERR: invalid sleep");} }
static void doAddCPU()  { try{int v=std::stoi(editText(g.hEditCpu));     submitTask("task_cpu",[v]{task_cpu(v);});}    catch(...){emit("ERR: invalid n");} }
static void doAddIO()   { std::string v=editText(g.hEditIo);             submitTask("task_io_sim",[v]{task_io_sim(v);}); }

static void doDemo(){
    emit("--- DEMO STARTED ---");
    submitTask("task_print",  []{task_print("Demo-Task-1");});
    submitTask("task_sleep",  []{task_sleep(2.0);});
    submitTask("task_cpu",    []{task_cpu(30);});
    submitTask("task_io_sim", []{task_io_sim("Demo-File",3,0.5);});
    submitTask("task_print",  []{task_print("Demo-End");});
}
static void doBench(){
    std::thread([]{
        emit("--- BENCHMARK STARTED ---");
        auto t0=std::chrono::high_resolution_clock::now();
        for(int i=0;i<8;++i) fib(30);
        double ts=std::chrono::duration<double>(std::chrono::high_resolution_clock::now()-t0).count();
        ThreadPool p2(4);
        auto t1=std::chrono::high_resolution_clock::now();
        for(int i=0;i<8;++i) p2.addTask([]{task_cpu(30);});
        p2.join(); p2.shutdown();
        double tp=std::chrono::duration<double>(std::chrono::high_resolution_clock::now()-t1).count();
        std::ostringstream o;
        o<<std::fixed<<std::setprecision(2)
         <<"RESULT: Single="<<ts<<"s | Pool="<<tp<<"s | Speedup="<<ts/tp<<"x";
        emit(o.str());
    }).detach();
}
static void doWait(){
    std::thread([]{emit("--- WAITING ---"); g.pool->join(); emit("--- ALL DONE ---");}).detach();
}

// Show/Hide child controls when timeline overlay is opened/closed
static void setControlsVisible(bool visible) {
    int cmd = visible ? SW_SHOW : SW_HIDE;

    int ids[] = {
        ID_EDIT_PRINT, ID_EDIT_SLEEP, ID_EDIT_CPU, ID_EDIT_IO,
        ID_BTN_ADD_PRINT, ID_BTN_ADD_SLEEP, ID_BTN_ADD_CPU, ID_BTN_ADD_IO,
        ID_BTN_DEMO, ID_BTN_BENCH, ID_BTN_WAIT, ID_BTN_TIME, ID_BTN_EXIT,
        ID_LOG_BOX
    };

    for (int id : ids) {
        HWND h = GetDlgItem(g.hwnd, id);
        if (h) ShowWindow(h, cmd);
    }
}

static void doTimeline(){
    g.showTimeline = !g.showTimeline;

    if(g.showTimeline){
        std::lock_guard<std::mutex> lk(g.histMtx);
        g.timelineSnap.clear();

        for(auto& h:g.history)
            if(h.done) g.timelineSnap.push_back(h);

        if(g.timelineSnap.empty()){
            emit("Timeline: no finished tasks yet.");
            g.showTimeline = false;
            setControlsVisible(true);
        } else {
            // Hide child windows so timeline appears clean on top
            setControlsVisible(false);
        }
    } else {
        setControlsVisible(true);
    }

    InvalidateRect(g.hwnd, nullptr, FALSE);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Window procedure
// ═══════════════════════════════════════════════════════════════════════════
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    switch(msg){

    // ── CREATE ─────────────────────────────────────────────────────────────
    case WM_CREATE:{
        g.hwnd=hwnd;

        // Create off-screen DC once
        HDC screenDC=GetDC(hwnd);
        g.memDC=CreateCompatibleDC(screenDC);
        resizeBuffer(screenDC, WIN_W, WIN_H);
        ReleaseDC(hwnd,screenDC);

        // Brushes
        g.brDark=CreateSolidBrush(C_DARK);
        g.brTerm=CreateSolidBrush(RGB(5,5,5));

        // Fonts
        g.hFontBig  =CreateFontA(28,0,0,0,FW_BOLD,  0,0,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,"Consolas");
        g.hFontMono =CreateFontA(14,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,"Consolas");
        g.hFontSmall=CreateFontA(12,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,"Consolas");

        // Edit boxes
        auto mkEd=[&](HWND& out,int id,const char* def,int y){
            out=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT",def,
                WS_CHILD|WS_VISIBLE|ES_LEFT|ES_AUTOHSCROLL,
                175,y,230,24,hwnd,(HMENU)(INT_PTR)id,nullptr,nullptr);
            SendMessageA(out,WM_SETFONT,(WPARAM)g.hFontMono,TRUE);
        };
        mkEd(g.hEditPrint,ID_EDIT_PRINT,"Hello from GUI",308);
        mkEd(g.hEditSleep,ID_EDIT_SLEEP,"2.0",           348);
        mkEd(g.hEditCpu,  ID_EDIT_CPU,  "30",            388);
        mkEd(g.hEditIo,   ID_EDIT_IO,   "IOTask",        428);

        // ADD buttons
        auto mkAdd=[&](int id,int y){
            HWND h=CreateWindowA("BUTTON","ADD",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                415,y,60,24,hwnd,(HMENU)(INT_PTR)id,nullptr,nullptr);
            SendMessageA(h,WM_SETFONT,(WPARAM)g.hFontSmall,TRUE);
        };
        mkAdd(ID_BTN_ADD_PRINT,308); mkAdd(ID_BTN_ADD_SLEEP,348);
        mkAdd(ID_BTN_ADD_CPU,  388); mkAdd(ID_BTN_ADD_IO,   428);

        // Control buttons
        struct BD{int id;const char* lbl;int y;};
        BD btns[]={
            {ID_BTN_DEMO, "[ RUN DEMO ]",         484},
            {ID_BTN_BENCH,"[ CPU BENCHMARK ]",    524},
            {ID_BTN_WAIT, "[ WAIT - FINISH ALL ]",564},
            {ID_BTN_TIME, "[ SHOW TIMELINE ]",    604},
            {ID_BTN_EXIT, "[ EXIT ]",             644},
        };
        for(auto& b:btns){
            HWND h=CreateWindowA("BUTTON",b.lbl,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                20,b.y,480,36,hwnd,(HMENU)(INT_PTR)b.id,nullptr,nullptr);
            SendMessageA(h,WM_SETFONT,(WPARAM)g.hFontMono,TRUE);
        }

        // Log listbox
        g.hLog=CreateWindowExA(WS_EX_CLIENTEDGE,"LISTBOX","",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOINTEGRALHEIGHT|LBS_HASSTRINGS,
            550,125,500,530,hwnd,(HMENU)(INT_PTR)ID_LOG_BOX,nullptr,nullptr);
        SendMessageA(g.hLog,WM_SETFONT,(WPARAM)g.hFontSmall,TRUE);

        // Pool + log callback
        g.pool=std::unique_ptr<ThreadPool>(new ThreadPool(4));
        g_log=[](const std::string& s){ logAppend(s); };

        // Timer: 33ms (~30fps) is plenty for GDI and avoids flicker from over-drawing
        SetTimer(hwnd,ID_TIMER,30,nullptr);
        return 0;
    }

    // ── Suppress background erase — the #1 cause of flicker ───────────────
    case WM_ERASEBKGND:
        return 1;   // tell Windows "I handled it" — do NOT erase

    // ── Edit / listbox colors ──────────────────────────────────────────────
    case WM_CTLCOLOREDIT:{
        HDC hdc=(HDC)wp;
        SetTextColor(hdc,C_CYAN); SetBkColor(hdc,C_DARK);
        return (LRESULT)g.brDark;
    }
    case WM_CTLCOLORLISTBOX:{
        HDC hdc=(HDC)wp;
        SetTextColor(hdc,C_GREEN); SetBkColor(hdc,RGB(5,5,5));
        return (LRESULT)g.brTerm;
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)GetStockObject(BLACK_BRUSH);

    // ── Timer: update animation state, drain log, then repaint ────────────
        case WM_TIMER:
            if(wp == ID_TIMER){
                g.scanY += 4;
                // Scanline ko bhi sirf threads tak rakhein
                if(g.scanY > 220) g.scanY = 0;

                drainLog();

                // Refresh area ko sirf 230px tak rakhein.
                // Is se blinking ka sawaal hi paida nahi hota kyunke controls y=308 pe hain.
                RECT rcTop = { 0, 0, 550, 230 };
                InvalidateRect(hwnd, &rcTop, FALSE);
            }
            return 0;
    // ── Commands ───────────────────────────────────────────────────────────
    case WM_COMMAND:{
        switch(LOWORD(wp)){
        case ID_BTN_ADD_PRINT: doAddPrint(); break;
        case ID_BTN_ADD_SLEEP: doAddSleep(); break;
        case ID_BTN_ADD_CPU:   doAddCPU();   break;
        case ID_BTN_ADD_IO:    doAddIO();    break;
        case ID_BTN_DEMO:      doDemo();     break;
        case ID_BTN_BENCH:     doBench();    break;
        case ID_BTN_WAIT:      doWait();     break;
        case ID_BTN_TIME:      doTimeline(); break;
        case ID_BTN_EXIT:      DestroyWindow(hwnd); break;
        }
        return 0;
    }

    case WM_KEYDOWN:
        if(wp==VK_ESCAPE||wp=='T') doTimeline();
        return 0;

    case WM_LBUTTONDOWN:
        if(g.showTimeline){
            g.showTimeline = false;
            setControlsVisible(true);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    // ── Paint — draw into memDC, blit to screen in ONE call ───────────────
    case WM_PAINT:{
        PAINTSTRUCT ps;
        HDC dc=BeginPaint(hwnd,&ps);
        doPaint(dc);   // draws into g.memDC, then BitBlt to dc
        EndPaint(hwnd,&ps);
        return 0;
    }

    // ── Resize: recreate back-buffer at new size ───────────────────────────
    case WM_SIZE:{
        int w=LOWORD(lp), h=HIWORD(lp);
        if(w>0&&h>0&&g.memDC){
            HDC sdc=GetDC(hwnd);
            resizeBuffer(sdc,w,h);
            ReleaseDC(hwnd,sdc);
        }
        return 0;
    }

    // ── Destroy ────────────────────────────────────────────────────────────
    case WM_DESTROY:
        KillTimer(hwnd,ID_TIMER);
        g.pool->shutdown();
        // Clean up buffer
        if(g.memBmp){ SelectObject(g.memDC,g.memOld); DeleteObject(g.memBmp); }
        if(g.memDC)  DeleteDC(g.memDC);
        // Clean up GDI objects
        DeleteObject(g.hFontBig); DeleteObject(g.hFontMono); DeleteObject(g.hFontSmall);
        DeleteObject(g.brDark);   DeleteObject(g.brTerm);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  WinMain
// ═══════════════════════════════════════════════════════════════════════════
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int nShow){
    InitCommonControls();

    WNDCLASSEXA wc{};
    wc.cbSize       =sizeof(wc);
    wc.style        =CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc  =WndProc;
    wc.hInstance    =hInst;
    wc.hCursor      =LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName="ThreadPoolSciF";
    RegisterClassExA(&wc);

    HWND hwnd=CreateWindowExA(0,"ThreadPoolSciF",
        "Thread Pool Server  |  Sci-Fi Edition",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,WIN_W,WIN_H,
        nullptr,nullptr,hInst,nullptr);

    ShowWindow(hwnd,nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while(GetMessageA(&msg,nullptr,0,0)){
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
