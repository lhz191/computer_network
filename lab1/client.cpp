#include <iostream>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>  // 引入 vector 头文件

#include <cstdlib> // 包含 system() 函数
#include <iostream>
#include <cstdlib> // 包含 system() 函数
#include<windows.h> 
// 清空客户端屏幕的函数
void clearScreen() {
#ifdef _WIN32
    system("cls"); // Windows 系统
#else
    system("clear"); // Linux/Unix 系统
#endif
}
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int BUFFER_SIZE = 1024;
#include <windows.h> 

void setConsoleColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}
int flag = 1;
// 客户端线程处理函数
void clientThread(SOCKET sockfd) {
    char buffer[BUFFER_SIZE];
    int bytesRead;

    // 发送用户名
    //cout << endl;
    cout << "请输入您的用户名: ";

    string username;
    //cout << flag;
    if (flag) {
        getline(cin, username);
    }
    cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): " << endl;
    flag = 0;
    send(sockfd, username.c_str(), username.length(), 0);
    while (true) {
        // 接收服务器消息
        bytesRead = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (bytesRead <= 0) {
            cerr << "连接服务器中断。" << endl;
            closesocket(sockfd);
            return;
        }
        buffer[bytesRead] = '\0';


        string serverMessage(buffer);

        // 检查是否是私聊消息
        if (serverMessage.find("[私聊]") == 0) {
            // 如果是私聊消息，使用不同的颜色显示
            setConsoleColor(11); // 11 表示亮蓝色
            cout << endl << serverMessage << endl;
            setConsoleColor(7); // 恢复默认颜色
        }
        else if (serverMessage.find("User") == 0) {
            // 如果是私聊消息，使用不同的颜色显示
            setConsoleColor(11); // 11 表示亮蓝色
            cout << endl << serverMessage << endl;
            setConsoleColor(7); // 恢复默认颜色
        }
        else if (serverMessage.find("User") == 0) {
            // 如果是私聊消息，使用不同的颜色显示
            setConsoleColor(11); // 11 表示亮蓝色
            cout << endl << serverMessage << endl;
            setConsoleColor(7); // 恢复默认颜色
        }
        else {
            //setConsoleColor(10); // 10 表示亮绿色
            cout << endl << "[服务器] " << endl << buffer << endl;  // 日志输出
        }
        // 输出完服务器消息后，提示用户输入消息
        cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): ";
    }
}
std::string Utf8ToGbk(const std::string& utf8)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    std::unique_ptr<wchar_t[]> wstr(new wchar_t[len + 1]);
    memset(wstr.get(), 0, (len + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wstr.get(), len);
    len = WideCharToMultiByte(CP_ACP, 0, wstr.get(), -1, NULL, 0, NULL, NULL);
    std::unique_ptr<char[]> str(new char[len + 1]);
    memset(str.get(), 0, (len + 1) * sizeof(char));
    WideCharToMultiByte(CP_ACP, 0, wstr.get(), -1, str.get(), len, NULL, NULL);
    return std::string(str.get());
}
int flag1;
void inputThread(SOCKET sockfd) {
    char buffer[BUFFER_SIZE];
    int bytesRead;
    /*cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): " << endl;*/
    while (true) {
        // 提示用户输入消息
        //cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): ";
        string message;
        if (flag1 == 1) {
            getline(cin, message);
        }
        flag1 = 1;
        //cout << "q1111" << endl;
        // 退出机制
        if (message == "q") {
            cout << "您已退出聊天。" << endl;
            closesocket(sockfd);
            return;
        }
        if (message == "/clear")
        {
            clearScreen();
            cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): ";
            continue;
        }
        // 文件上传功能
        if (message.substr(0, 7) == "/upload") {
            string filePath = message.substr(8);  // 获取文件路径
            ifstream file(filePath, ios::binary);
            if (!file.is_open()) {
                cerr << "无法打开文件: " << filePath << endl;
                continue;
            }

            // 获取文件大小
            file.seekg(0, ios::end);
            streamsize fileSize = file.tellg();
            file.seekg(0, ios::beg);
            // 以 UTF-8 编码发送文件内容
            char buffer[409600];  // 定义一个缓冲区
            file.imbue(std::locale("zh_CN.UTF-8"));  // 设置文件流的编码为 UTF-8
            while (file) {
                // 读取文件的一部分并发送
                file.read(buffer, sizeof(buffer));
                streamsize bytesRead = file.gcount(); // 获取实际读取的字节数
                // 将 UTF-8 编码的 buffer 转换为 GBK 编码
                std::string gbkBuffer = Utf8ToGbk(std::string(buffer, bytesRead));
                //cout << buffer << endl;
                //cout << gbkBuffer << endl;
                if (bytesRead > 0) {
                    send(sockfd, gbkBuffer.c_str(), gbkBuffer.length(), 0);
                }
            }

            cout << endl << "文件上传成功! " << filePath << endl;
            file.close();
            continue;
        }
        // 发送消息
        if (message.substr(0, 3) == "/pm") {
            send(sockfd, message.c_str(), message.length(), 0);  // 第四个参数为 0
            cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): ";
            continue;
        }
        if (message.substr(0, 5) == "/kick") {
            send(sockfd, message.c_str(), message.length(), 0);  // 第四个参数为 0
            cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): ";
            continue;
        }
        // 添加好友
        if (message.substr(0, 7) == "/addfrd") {
            /*cout << 1111111 << endl;*/
            string friendUsername = message.substr(8);  // 获取好友用户名
            string addFriendMessage = "/addfrd " + friendUsername;
            send(sockfd, addFriendMessage.c_str(), addFriendMessage.length(), 0);
            cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): ";
            continue;
        }
        //添加黑名单
        if (message.substr(0, 6) == "/addsb") {
            string sbUsername = message.substr(7);  // 获取黑名单用户名
            string addSbMessage = "/addsb " + sbUsername;
            send(sockfd, addSbMessage.c_str(), addSbMessage.length(), 0);
            cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): ";
            continue;
        }
        send(sockfd, message.c_str(), message.length(), 0);  // 第四个参数为 0
        //cout << "输入消息 ('q'退出, '/upload <文件路径>'上传文件): ";
    }
}

int main() {
    // 设置控制台/命令行窗口的代码页为 GBK
    SetConsoleOutputCP(936);
    SetConsoleCP(936);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "无法初始化 Winsock。" << endl;
        return 1;
    }

    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET) {
        cerr << "无法创建客户端套接字。" << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);

    // 使用 inet_pton 替代 inet_addr
    if (inet_pton(AF_INET, "192.168.0.1", &serverAddr.sin_addr) <= 0) {
        cerr << "IP 地址格式错误。" << endl;
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    if (connect(sockfd, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "无法连接到服务器。" << endl;
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    thread clientThreadInstance(clientThread, sockfd);
    thread inputThreadInstance(inputThread, sockfd);

    clientThreadInstance.join();
    inputThreadInstance.join();

    WSACleanup();
    return 0;
}