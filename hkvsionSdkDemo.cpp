// hkvsionSdkDemo.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <list>
#include <iostream>
#include <ctime>
#include <mutex>

#include "Windows.h"
#include "HCNetSDK.h"
#include "plaympeg4.h"

#pragma comment(lib,"GdiPlus.lib")
#pragma comment(lib,"HCCore.lib")
#pragma comment(lib,"HCNetSDK.lib")
#pragma comment(lib,"PlayCtrl.lib")

using namespace std;
LONG lPort = -1; //全局的播放库port号

typedef HWND(WINAPI* PROCGETCONSOLEWINDOW)();
PROCGETCONSOLEWINDOW GetConsoleWindowAPI;

struct Frame {
    std::shared_ptr<uint8_t> buffer;
    uint32_t size;
};
class StreamSourceList {
public:
    using Ptr = std::shared_ptr<StreamSourceList>;
    StreamSourceList() { _list.resize(25); };
    ~StreamSourceList() = default;

    void pushFrame(Frame& frame) {
        std::lock_guard<decltype(_rwMtx)> lk(_rwMtx);
        while (_list.size() >= 25) {
            _list.pop_front();
        }
        cout << _list.size() << endl;
        _list.emplace_back(frame);
    }
    bool getFrame(Frame& frame) {
        std::lock_guard<decltype(_rwMtx)> lk(_rwMtx);
        if (_list.empty())
            return false;
        frame = _list.front();
        _list.pop_front();

        return true;
    }

    bool empty() {
        return   _list.empty();
    }

private:
    std::list<Frame> _list;   // char *  len ;
    std::recursive_mutex _rwMtx;
    FILE* _outFile = nullptr;   // 用于测试保存pull下的数据

};
static int findHeader(const uint8_t* data, uint32_t size, int* flag) {
    uint32_t pos = 0;
    int startCode = 0;
    while (pos < size - 4) {
        if (data[pos] == 0x0 && data[pos + 1] == 0x0 && data[pos + 2] == 0x1) {
            startCode = 3;
            break;
        }
        if (data[pos] == 0x0 && data[pos + 1] == 0x0 && data[pos + 2] == 0x0 && data[pos + 3] == 0x1) {
            startCode = 4;
            break;
        }
        pos++;
    }
    *flag = 1;
    int nalType = (data[pos + startCode] & 0x1F);
    if (nalType == 0x5 || nalType == 0x1 || nalType == 0x6 || nalType == 0x7 || nalType == 0x8
        && (data[pos + startCode + 1] & 0x80) == 0x80)
        *flag = 0;
    return pos;
}

void CALLBACK g_RealDataCallBack_V30(LONG lRealHandle, DWORD dwDataType, BYTE* pBuffer, DWORD dwBufSize, void* dwUser)
{
    cout << "g_RealDataCallBack_V30:%d" << dwBufSize << endl;
    StreamSourceList* list = (StreamSourceList *) (dwUser);
    int isH264 = 0;
    int pos = findHeader(pBuffer, dwBufSize, &isH264);
    dwBufSize -= pos;
    Frame frame;
    frame.buffer.reset(new uint8_t(dwBufSize));
    frame.size = dwBufSize;
    memcpy(frame.buffer.get(), pBuffer + pos, dwBufSize);
    list->pushFrame(frame);

    //HWND hWnd = GetConsoleWindowAPI();
    //switch (dwDataType) {
    //case NET_DVR_SYSHEAD: //系统头
    //    if (lPort >= 0) {
    //        break;  //该通道取流之前已经获取到句柄，后续接口不需要再调用
    //    }

    //    if (!PlayM4_GetPort(&lPort))  //获取播放库未使用的通道号
    //    {
    //        break;
    //    }
    //    //m_iPort = lPort; //第一次回调的是系统头，将获取的播放库port号赋值给全局port，下次回调数据时即使用此port号播放
    //    if (dwBufSize > 0) {
    //        if (!PlayM4_SetStreamOpenMode(lPort, STREAME_REALTIME))  //设置实时流播放模式
    //        {
    //            break;
    //        }

    //        if (!PlayM4_OpenStream(lPort, pBuffer, dwBufSize, 1024 * 1024)) //打开流接口
    //        {
    //            break;
    //        }

    //        if (!PlayM4_Play(lPort, hWnd)) //播放开始
    //        {
    //            break;
    //        }
    //    }
    //    break;
    //case NET_DVR_STREAMDATA:   //码流数据
    //    if (dwBufSize > 0 && lPort != -1) {
    //        if (!PlayM4_InputData(lPort, pBuffer, dwBufSize)) {
    //            break;
    //        }
    //    }
    //    break;
    //default: //其他数据
    //    if (dwBufSize > 0 && lPort != -1) {
    //        if (!PlayM4_InputData(lPort, pBuffer, dwBufSize)) {
    //            break;
    //        }
    //    }
    //    break;
    //}

}

void CALLBACK g_ExceptionCallBack(DWORD dwType, LONG lUserID, LONG lHandle, void* pUser)
{
    char tempbuf[256] = { 0 };
    switch (dwType) {
    case EXCEPTION_RECONNECT:    //预览时重连
        printf("----------reconnect--------%lld\n", time(NULL));
        break;
    default:
        break;
    }

}

class hkPull {
public:
    using Ptr = std::shared_ptr<hkPull>;
    using UPtr = std::unique_ptr<hkPull>;
    hkPull() {
        //---------------------------------------
        // 初始化
        NET_DVR_Init();
        //设置连接时间与重连时间
        NET_DVR_SetConnectTime(2000, 1);
        NET_DVR_SetReconnect(10000, true);

        //---------------------------------------
        //设置异常消息回调函数
        NET_DVR_SetExceptionCallBack_V30(0, NULL, g_ExceptionCallBack, NULL);

        //---------------------------------------
        // 获取控制台窗口句柄
        HMODULE hKernel32 = GetModuleHandle("kernel32");
        GetConsoleWindowAPI = (PROCGETCONSOLEWINDOW)GetProcAddress(hKernel32, "GetConsoleWindow");

        //登录参数，包括设备地址、登录用户、密码等
        NET_DVR_USER_LOGIN_INFO struLoginInfo = { 0 };
        struLoginInfo.bUseAsynLogin = 0; //同步登录方式
        strcpy(struLoginInfo.sDeviceAddress, "192.168.0.38"); //设备IP地址
        struLoginInfo.wPort = 8000; //设备服务端口
        strcpy(struLoginInfo.sUserName, "admin"); //设备登录用户名
        strcpy(struLoginInfo.sPassword, "a12345678"); //设备登录密码
        
        //设备信息, 输出参数
        NET_DVR_DEVICEINFO_V40 struDeviceInfoV40 = { 0 };

        _lUserID = NET_DVR_Login_V40(&struLoginInfo, &struDeviceInfoV40);
        if (_lUserID < 0) {
            printf("Login failed, error code: %d\n", NET_DVR_GetLastError());
            NET_DVR_Cleanup();
            return;
        }
    }

    void start() {
        NET_DVR_PREVIEWINFO struPlayInfo = { 0 };
        struPlayInfo.hPlayWnd = NULL;         //需要SDK解码时句柄设为有效值，仅取流不解码时可设为空
        struPlayInfo.lChannel = 1;       //预览通道号
        struPlayInfo.dwStreamType = 0;       //0-主码流，1-子码流，2-码流3，3-码流4，以此类推
        struPlayInfo.dwLinkMode = 0;       //0- TCP方式，1- UDP方式，2- 多播方式，3- RTP方式，4-RTP/RTSP，5-RSTP/HTTP
        struPlayInfo.bBlocked = 1;       //0- 非阻塞取流，1- 阻塞取流

        _lRealPlayHandle = NET_DVR_RealPlay_V40(_lUserID, &struPlayInfo, g_RealDataCallBack_V30, _list.get());
        if (_lRealPlayHandle < 0) {
            printf("NET_DVR_RealPlay_V40 error, %d\n", NET_DVR_GetLastError());
            NET_DVR_Logout(_lUserID);
            NET_DVR_Cleanup();
            return;
        }
          
    }
    void stop() {
        //关闭预览
        NET_DVR_StopRealPlay(_lRealPlayHandle);

        //释放播放库资源
        PlayM4_Stop(lPort);
        PlayM4_CloseStream(lPort);
        PlayM4_FreePort(lPort);

        //注销用户
        NET_DVR_Logout(_lUserID);
        NET_DVR_Cleanup();
    }
    ~hkPull() {
        stop();
    }

    StreamSourceList::Ptr _list;
private:
    // 注册设备
    LONG _lUserID;
    //启动预览并设置回调数据流
    LONG _lRealPlayHandle;
    

};

FILE* g_file = fopen("d://hk2.h264", "wb+");
bool g_saveFileData = false;
int saveData(StreamSourceList::Ptr list, FILE * file) {
    while (g_saveFileData) {
        Frame data;
        if (list.get()->getFrame(data)) {
            fwrite(data.buffer.get(), data.size, 1, file);
        } else {
            Sleep(100);
        }
    }
    fflush(file);
    fclose(file);
    return 0;
}
int main() {

    hkPull::UPtr device(new hkPull);
    device->start();
    g_saveFileData = true;
    saveData(device->_list, g_file);
    return 0;
}

